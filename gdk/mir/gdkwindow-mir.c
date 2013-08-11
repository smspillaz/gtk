/*
 * Copyright © 2010 Intel Corporation
 * Copyright (c) 2013 Sam Spilsbury <smspillaz@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <netinet/in.h>
#include <unistd.h>

#include "gdk.h"
#include "gdkmir.h"

#include "gdkwindow.h"
#include "gdkwindowimpl.h"
#include "gdkdisplay-mir.h"
#include "gdkprivate-mir.h"
#include "gdkinternals.h"
#include "gdkdeviceprivate.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

#include <mir_toolkit/mir_client_library.h>

#define WINDOW_IS_TOPLEVEL_OR_FOREIGN(window) \
  (GDK_WINDOW_TYPE (window) != GDK_WINDOW_CHILD &&   \
   GDK_WINDOW_TYPE (window) != GDK_WINDOW_OFFSCREEN)

#define WINDOW_IS_TOPLEVEL(window)		     \
  (GDK_WINDOW_TYPE (window) != GDK_WINDOW_CHILD &&   \
   GDK_WINDOW_TYPE (window) != GDK_WINDOW_FOREIGN && \
   GDK_WINDOW_TYPE (window) != GDK_WINDOW_OFFSCREEN)

/* Return whether time1 is considered later than time2 as far as xserver
 * time is concerned.  Accounts for wraparound.
 */
#define XSERVER_TIME_IS_LATER(time1, time2)                        \
  ( (( time1 > time2 ) && ( time1 - time2 < ((guint32)-1)/2 )) ||  \
    (( time1 < time2 ) && ( time2 - time1 > ((guint32)-1)/2 ))     \
  )

#define MIR_SURFACE_RESIZE_LEFT 0
#define MIR_SURFACE_RESIZE_TOP 1
#define MIR_SURFACE_RESIZE_TOP_LEFT 2
#define MIR_SURFACE_RESIZE_RIGHT 3
#define MIR_SURFACE_RESIZE_BOTTOM_LEFT 4
#define MIR_SURFACE_RESIZE_BOTTOM 5
#define MIR_SURFACE_RESIZE_BOTTOM_RIGHT 6

typedef struct _GdkMirWindow GdkMirWindow;
typedef struct _GdkMirWindowClass GdkMirWindowClass;

struct _GdkMirWindow {
  GdkWindow parent;
};

struct _GdkMirWindowClass {
  GdkWindowClass parent_class;
};

G_DEFINE_TYPE (GdkMirWindow, _gdk_mir_window, GDK_TYPE_WINDOW)

static void
_gdk_mir_window_class_init (GdkMirWindowClass *mir_window_class)
{
}

static void
_gdk_mir_window_init (GdkMirWindow *mir_window)
{
}

#define GDK_TYPE_WINDOW_IMPL_MIR              (_gdk_window_impl_mir_get_type ())
#define GDK_WINDOW_IMPL_MIR(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_WINDOW_IMPL_MIR, GdkWindowImplMir))
#define GDK_WINDOW_IMPL_MIR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_WINDOW_IMPL_MIR, GdkWindowImplMirClass))
#define GDK_IS_WINDOW_IMPL_MIR(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_WINDOW_IMPL_MIR))
#define GDK_IS_WINDOW_IMPL_MIR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_WINDOW_IMPL_MIR))
#define GDK_WINDOW_IMPL_MIR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_WINDOW_IMPL_MIR, GdkWindowImplMirClass))

typedef struct _GdkWindowImplMir GdkWindowImplMir;
typedef struct _GdkWindowImplMirClass GdkWindowImplMirClass;

struct _GdkWindowImplMir
{
  GdkWindowImpl parent_instance;

  GdkWindow *wrapper;

  GdkCursor *cursor;

  gint8 toplevel_window_type;

  MirSurface *surface;
  unsigned int mapped : 1;
  GdkWindow *transient_for;
  GdkWindowTypeHint hint;

  /* Async queue for the next graphics_region_surface
   * as allocated by next_buffer_available */
  GAsyncQueue *graphics_region_surface_queue;

  /* The surface which is being "drawn to" to. This
   * will be set to NULL in case we end up waiting
   * on a new buffer. This may be updated in another thread*/
  cairo_surface_t *graphics_region_surface;

  /* The "back buffer" surface. This is persistent and
   * will be flipped to graphics_region_surface on
   * gdk_window_process updates */
  cairo_surface_t *cairo_surface;

  uint32_t resize_edges;

  int focus_count;

  gulong map_serial;	/* Serial of last transition from unmapped */

  cairo_surface_t *icon_pixmap;
  cairo_surface_t *icon_mask;

  /* Time of most recent user interaction. */
  gulong user_time;

  GdkGeometry geometry_hints;
  GdkWindowHints geometry_mask;

  guint32 grab_time;
  GdkDevice *grab_device;
};

struct _GdkWindowImplMirClass
{
  GdkWindowImplClass parent_class;
};

G_DEFINE_TYPE (GdkWindowImplMir, _gdk_window_impl_mir, GDK_TYPE_WINDOW_IMPL)

static void
_gdk_window_impl_mir_init (GdkWindowImplMir *impl)
{
  impl->toplevel_window_type = -1;
}

void
_gdk_mir_window_add_focus (GdkWindow *window)
{
  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);

  impl->focus_count++;
  if (impl->focus_count == 1)
    gdk_synthesize_window_state (window, 0, GDK_WINDOW_STATE_FOCUSED);
}

void
_gdk_mir_window_remove_focus (GdkWindow *window)
{
  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);

  impl->focus_count--;
  if (impl->focus_count == 0)
    gdk_synthesize_window_state (window, GDK_WINDOW_STATE_FOCUSED, 0);
}

void
_gdk_mir_window_set_device_grabbed (GdkWindow *window,
				    GdkDevice *device,
				    guint32   time)
{
  g_return_if_fail (window != NULL);

  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);

  impl->grab_time = time;
  impl->grab_device = device;
}

GdkWindow *
_gdk_mir_screen_create_root_window(GdkScreen *screen, 
                                   int width, 
                                   int height)
{
  GdkWindow *window;
  GdkWindowImplMir *impl;

  window = _gdk_display_create_window (gdk_screen_get_display (screen));
  window->impl = g_object_new (GDK_TYPE_WINDOW_IMPL_MIR, NULL);
  window->impl_window = window;
  window->visual = gdk_screen_get_system_visual (screen);

  impl = GDK_WINDOW_IMPL_MIR (window->impl);

  impl->wrapper = GDK_WINDOW (window);

  window->window_type = GDK_WINDOW_ROOT;
  window->depth = 32;

  window->x = 0;
  window->y = 0;
  window->abs_x = 0;
  window->abs_y = 0;
  window->width = width;
  window->height = height;
  window->viewable = TRUE;

  /* see init_randr_support() in gdkscreen-mir.c */
  window->event_mask = GDK_STRUCTURE_MASK;

  return window;
}

static const gchar *
get_default_title (void)
{
  const char *title;

  title = g_get_application_name ();
  if (!title)
    title = g_get_prgname ();
  if (!title)
    title = "";

  return title;
}

static void
post_event_to_main_loop_for_further_processing (GdkDisplay *display,
                                                GdkEvent   *event)
{
  GdkMirDisplay *mir_display = GDK_MIR_DISPLAY (display);

  g_async_queue_push (mir_display->event_queue,
                      (gpointer) event);
  if (write (mir_display->wakeup_pipe[1],
             (const void *) "1",
             1) < 0)
    {
      switch (errno)
        {
        case EWOULDBLOCK:
          break;
        default:
          g_critical (strerror (errno));
        }
    }
}

/* There is no guaruntee as to which thread this will be
 * called in, and as such should only touch:
 * - graphics_region
 * - cairo_surface
 * - next_surface_wait_handle
 */
static void
next_buffer_arrived (MirSurface *surface,
                     gpointer   data)
{
  GdkWindowImplMir *impl = (GdkWindowImplMir *) data;

  MirGraphicsRegion graphics_region;

  /* Update the graphics region and cairo surface */
  mir_surface_get_graphics_region (surface, &graphics_region);
  cairo_surface_t *next_graphics_region_surface =
    cairo_image_surface_create_for_data ((guchar *) graphics_region.vaddr,
                                         CAIRO_FORMAT_ARGB32,
                                         graphics_region.width,
                                         graphics_region.height,
                                         graphics_region.stride);

  GdkEvent *event = gdk_event_new (GDK_EXPOSE);
  event->expose.window = g_object_ref (impl->wrapper);
  event->expose.send_event = FALSE;
  event->expose.count = 0;

  cairo_rectangle_int_t area =
  {
    0,
    0,
    impl->wrapper->x,
    impl->wrapper->y
  };

  event->expose.area = area;
  event->expose.region = cairo_region_create_rectangle (&event->expose.area);

  post_event_to_main_loop_for_further_processing (gdk_window_get_display (impl->wrapper),
                                                  event);

  g_async_queue_push (impl->graphics_region_surface_queue,
                      next_graphics_region_surface);}

static void
cairo_surface_destroy_notify_func (gpointer surface)
{
  cairo_surface_destroy ((cairo_surface_t *) surface);
}

void
_gdk_mir_display_create_window_impl (GdkDisplay    *display,
				     GdkWindow     *window,
				     GdkWindow     *real_parent,
				     GdkScreen     *screen,
				     GdkEventMask   event_mask,
				     GdkWindowAttr *attributes,
				     gint           attributes_mask)
{
  GdkWindowImplMir *impl;
  const char *title;

  impl = g_object_new (GDK_TYPE_WINDOW_IMPL_MIR, NULL);
  window->impl = GDK_WINDOW_IMPL (impl);
  impl->wrapper = GDK_WINDOW (window);

  if (window->width > 65535 ||
      window->height > 65535)
    {
      g_warning ("Native Windows wider or taller than 65535 pixels are not supported");

      if (window->width > 65535)
	window->width = 65535;
      if (window->height > 65535)
	window->height = 65535;
    }

  g_object_ref (window);

  switch (GDK_WINDOW_TYPE (window))
    {
    case GDK_WINDOW_TOPLEVEL:
    case GDK_WINDOW_TEMP:
      if (attributes_mask & GDK_WA_TITLE)
	title = attributes->title;
      else
	title = get_default_title ();

      gdk_window_set_title (window, title);
      break;

    case GDK_WINDOW_CHILD:
    default:
      break;
    }

  if (attributes_mask & GDK_WA_TYPE_HINT)
    gdk_window_set_type_hint (window, attributes->type_hint);

  impl->graphics_region_surface_queue =
    g_async_queue_new_full (cairo_surface_destroy_notify_func);
  impl->cairo_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                    window->width,
                                                    window->height);
}

static void
gdk_window_impl_mir_finalize (GObject *object)
{
    GdkWindowImplMir *impl;

    g_return_if_fail (GDK_IS_WINDOW_IMPL_MIR (object));

    impl = GDK_WINDOW_IMPL_MIR (object);

    if (impl->cursor)
      g_object_unref (impl->cursor);
    if (impl->graphics_region_surface)
      cairo_surface_destroy (impl->graphics_region_surface);
    if (impl->graphics_region_surface_queue)
      g_async_queue_unref (impl->graphics_region_surface_queue);
    if (impl->surface) 
      mir_surface_release_sync (impl->surface);

    G_OBJECT_CLASS (_gdk_window_impl_mir_parent_class)->finalize (object);
}

static void
gdk_mir_window_configure (GdkWindow *window,
                          int width, int height, int edges)
{
  /* TODO: Unimplemented */
  return;
}

static void
gdk_mir_window_set_user_time (GdkWindow *window, guint32 user_time)
{
}

static void
gdk_mir_window_map (GdkWindow *window)
{
  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);

  if (!impl->mapped)
    impl->mapped = TRUE;
}

static void
populate_focus_change_event (GdkEvent  *event,
                             GdkWindow *window,
                             GdkDevice *device,
                             gboolean  in)
{
  GdkDisplay *display = gdk_window_get_display (window);
  GdkDeviceManager *device_manager =
    gdk_display_get_device_manager (display);
  GdkDevice *keyboard =
    _gdk_mir_device_manager_get_core_keyboard (device_manager);

  event->focus_change.window =
    _gdk_mir_device_get_hovered_window (device);
  event->focus_change.send_event = FALSE;
  event->focus_change.in = in;
  gdk_event_set_device (event, keyboard);
}

void
_gdk_mir_window_generate_focus_event (GdkWindow *window,
                                      GdkWindow *last,
                                      GdkDevice *device)
{
  GdkEvent *focus_change_event = NULL;
  if (last && last != window)
    {
      focus_change_event = gdk_event_new (GDK_FOCUS_CHANGE);
      populate_focus_change_event (focus_change_event,
                                   last,
                                   device,
                                   FALSE);
      post_event_to_main_loop_for_further_processing (gdk_device_get_display (device),
                                                      focus_change_event);

      focus_change_event = gdk_event_new (GDK_FOCUS_CHANGE);
      populate_focus_change_event (focus_change_event, window, device, TRUE);
      post_event_to_main_loop_for_further_processing (gdk_device_get_display (device),
                                                      focus_change_event);
    }
}

static void
populate_crossing_event (GdkEvent             *event,
                         const MirMotionEvent *me,
                         GdkWindow            *window,
                         GdkDevice            *pointer)
{
  event->crossing.window = g_object_ref (window);
  gdk_event_set_device (event, pointer);
  event->crossing.subwindow = NULL;
  event->crossing.time = (guint32) (g_get_monotonic_time () / 1000);
  event->crossing.x = me->pointer_coordinates[0].x;
  event->crossing.y = me->pointer_coordinates[0].y;
  event->crossing.mode = GDK_CROSSING_NORMAL;
  event->crossing.detail = GDK_NOTIFY_ANCESTOR;
  event->crossing.focus = TRUE;
  event->crossing.state = 0;
}

static void
surface_handle_hover_enter (MirSurface           *surface,
                            const MirMotionEvent *me,
                            GdkDisplay           *display,
                            GdkWindow            *window,
                            GdkDevice            *pointer)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (me != NULL);
  g_return_if_fail (window != NULL);

  GdkEvent *event = gdk_event_new (GDK_ENTER_NOTIFY);
  populate_crossing_event (event, me, window, pointer);
  post_event_to_main_loop_for_further_processing (display, event);
}

static void
surface_handle_hover_leave (MirSurface           *surface,
                            const MirMotionEvent *me,
                            GdkDisplay           *display,
                            GdkWindow            *window,
                            GdkDevice            *pointer)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (me != NULL);
  g_return_if_fail (window != NULL);

  GdkEvent *event = gdk_event_new (GDK_LEAVE_NOTIFY);
  populate_crossing_event (event, me, window, pointer);
  post_event_to_main_loop_for_further_processing (display, event);
}

static void
populate_motion_event (GdkEvent             *event,
                       const MirMotionEvent *me,
                       GdkDisplay           *display,
                       GdkWindow            *window,
                       GdkDevice            *pointer)
{
  event->motion.window = g_object_ref (window);
  gdk_event_set_device (event, pointer);
  event->motion.time = (guint32) (g_get_monotonic_time () / 1000);
  event->motion.x = me->pointer_coordinates[0].x;
  event->motion.y = me->pointer_coordinates[0].y;
  event->motion.axes = NULL;
  event->motion.state = 0;
  event->motion.is_hint = 0;
  gdk_event_set_screen (event, gdk_display_get_screen (display, 0));
}

static void
surface_handle_motion (MirSurface           *surface,
                       const MirMotionEvent *me,
                       GdkDisplay           *display,
                       GdkWindow            *window,
                       GdkDevice            *pointer)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (me != NULL);
  g_return_if_fail (window != NULL);

  GdkEvent *event = gdk_event_new (GDK_MOTION_NOTIFY);
  populate_motion_event (event, me, display, window, pointer);
  post_event_to_main_loop_for_further_processing (display, event);
}

gboolean
mir_button_mask_has_button (MirMotionButton button_state,
                            guint           index)
{
  return (button_state >> (index - 1)) & (1 << 0);
}

typedef void (*MirButtonMaskIterateFunc) (guint    button,
                                          gpointer data);

void
mir_button_mask_iterate_buttons (MirMotionButton          button_state,
                                 MirButtonMaskIterateFunc func,
                                 gpointer                 data)
{
  static const guint max_gdk_button = 5;
  guint gdk_button = 1;

  for (; gdk_button <= max_gdk_button; ++gdk_button)
    if (mir_button_mask_has_button (button_state, gdk_button))
      (*func) (gdk_button, data);
}

typedef struct _PopulateButtonEventData
{
  GdkEventType event_type;
  const MirMotionEvent *me;
  GdkDisplay *display;
  GdkWindow  *window;
  GdkDevice  *device;
} PopulateButtonEventData;

static void
populate_and_send_button_event (guint    button_index,
                                gpointer data)
{
  PopulateButtonEventData *pd = (PopulateButtonEventData *) data;

  GdkEvent *event = gdk_event_new (pd->event_type);

  event->button.window = g_object_ref (pd->window);
  event->button.time = (guint32) (g_get_monotonic_time () / 1000);
  event->button.x = pd->me->pointer_coordinates[0].x;
  event->button.y = pd->me->pointer_coordinates[0].y;
  event->button.axes = NULL;
  event->button.state = 0;
  event->button.button = button_index;

  gdk_event_set_screen (event, gdk_display_get_screen (pd->display, 0));
  gdk_event_set_device (event, pd->device);

  post_event_to_main_loop_for_further_processing (pd->display,
                                                  event);
}

static void
surface_handle_button_down (MirSurface           *surface,
                            const MirMotionEvent *me,
                            GdkDisplay           *display,
                            GdkWindow            *window,
                            GdkDevice            *pointer)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (me != NULL);
  g_return_if_fail (window != NULL);

  MirMotionButton gained_buttons = 0;
  MirMotionButton lost_buttons = 0;

  _gdk_mir_mouse_update_from_button_event (pointer,
                                           window,
                                           me,
                                           &gained_buttons,
                                           &lost_buttons);

  PopulateButtonEventData data =
  {
    GDK_BUTTON_PRESS,
    me,
    display,
    window,
    pointer
  };

  mir_button_mask_iterate_buttons (gained_buttons,
                                   populate_and_send_button_event,
                                   (gpointer) &data);
}

static void
surface_handle_button_up (MirSurface           *surface,
                          const MirMotionEvent *me,
                          GdkDisplay           *display,
                          GdkWindow            *window,
                          GdkDevice            *pointer)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (me != NULL);
  g_return_if_fail (window != NULL);

  MirMotionButton gained_buttons = 0;
  MirMotionButton lost_buttons = 0;

  _gdk_mir_mouse_update_from_button_event (pointer,
                                           window,
                                           me,
                                           &gained_buttons,
                                           &lost_buttons);

  PopulateButtonEventData data =
  {
    GDK_BUTTON_RELEASE,
    me,
    display,
    window,
    pointer
  };

  mir_button_mask_iterate_buttons (lost_buttons,
                                   populate_and_send_button_event,
                                   (gpointer) &data);
}

static void
surface_handle_motion_event (MirSurface           *surface,
                             const MirMotionEvent *me,
                             GdkWindow            *window)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (me != NULL);
  g_return_if_fail (window != NULL);

  GdkDisplay *display = gdk_window_get_display (window);
  GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
  GdkDevice *pointer = gdk_device_manager_get_client_pointer (device_manager);

  switch (me->action)
    {
    case mir_motion_action_down:
      surface_handle_button_down (surface, me, display, window, pointer);
      break;
    case mir_motion_action_up:
      surface_handle_button_up (surface, me, display, window, pointer);
      break;
    case mir_motion_action_hover_enter:
      /* Currently Mir is generating leave events on button down
       * which confuses GDK, so crossing events are disabled for now
       * surface_handle_hover_enter (surface, me, display, window, pointer); */
      break;
    case mir_motion_action_hover_exit:
      /* Currently Mir is generating leave events on button down
       * which confuses GDK, so crossing events are disabled for now
       * surface_handle_hover_leave (surface, me, display, window, pointer); */
      break;
    case mir_motion_action_hover_move:
    case mir_motion_action_move:
      surface_handle_motion (surface, me, display, window, pointer);
      break;
    }
}

static void
populate_key_event (GdkEvent          *event,
                    const MirKeyEvent *ke,
                    GdkDevice         *device,
                    GdkWindow         *window)
{
  event->key.window = g_object_ref (window);
  event->key.state = 0;
  event->key.group = 0;
  event->key.hardware_keycode = ke->scan_code;
  event->key.keyval = ke->key_code;
  event->key.is_modifier = FALSE;

  gdk_event_set_device (event, device);
}

static void
surface_handle_key_down_event (MirSurface        *surface,
                               const MirKeyEvent *ke,
                               GdkDisplay        *display,
                               GdkDevice         *device,
                               GdkWindow         *window)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (ke != NULL);
  g_return_if_fail (window != NULL);

  GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
  populate_key_event (event, ke, device, window);
  post_event_to_main_loop_for_further_processing (display,
                                                  event);
}

static void
surface_handle_key_up_event (MirSurface        *surface,
                             const MirKeyEvent *ke,
                             GdkDisplay        *display,
                             GdkDevice         *device,
                             GdkWindow         *window)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (ke != NULL);
  g_return_if_fail (window != NULL);

  GdkEvent *event = gdk_event_new (GDK_KEY_RELEASE);
  populate_key_event (event, ke, device, window);
  post_event_to_main_loop_for_further_processing (display,
                                                  event);
}

static void
surface_handle_keyboard_event (MirSurface        *surface,
                               const MirKeyEvent *ke,
                               GdkWindow         *window)
{
  g_return_if_fail (surface != NULL);
  g_return_if_fail (ke != NULL);
  g_return_if_fail (window != NULL);

  GdkDisplay *display = gdk_window_get_display (window);
  GdkDeviceManager *device_manager =
    gdk_display_get_device_manager (display);
  GdkDevice *keyboard =
    _gdk_mir_device_manager_get_core_keyboard (device_manager);

  switch (ke->action)
    {
    case mir_key_action_down:
      surface_handle_key_down_event (surface, ke, display, keyboard, window);
      break;
    case mir_key_action_up:
      surface_handle_key_up_event (surface, ke, display, keyboard, window);
      break;
    default:
      break;
    }
}

static void
surface_handle_surface_event (MirSurface            *surface,
                              const MirSurfaceEvent *se,
                              GdkWindow             *window)
{
}

static void 
surface_handle_event (MirSurface     *surface,
                      const MirEvent *ev,
                      gpointer       data)
{
  switch (ev->type)
    {
    case mir_event_type_motion:
      surface_handle_motion_event(surface,
                                  (const MirMotionEvent *) ev,
                                  (GdkWindow *) data);
      break;
    case mir_event_type_key:
      surface_handle_keyboard_event(surface,
                                    (const MirKeyEvent *) ev,
                                    (GdkWindow *) data);
      break;
    case mir_event_type_surface:
      surface_handle_surface_event(surface,
                                   (const MirSurfaceEvent *) ev,
                                   (GdkWindow *) data);
      break;
    }
}

static void
gdk_mir_window_show (GdkWindow *window, gboolean already_mapped)
{
    GdkDisplay *display = NULL;
    GdkMirDisplay *display_mir = NULL;
    GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR(window->impl);
    GdkEvent *event;
    MirEventDelegate delegate =                                                 
    {                                                                           
        surface_handle_event,
        window
    };

    display = gdk_window_get_display(window);
    display_mir = GDK_MIR_DISPLAY(display);

    if (impl->user_time != 0 &&
        display_mir->user_time != 0) 
      gdk_mir_window_set_user_time(window, impl->user_time);

    /* TODO: Check if ARGB8888 is supported */
    MirPixelFormat pixel_format = mir_pixel_format_argb_8888;
    MirSurfaceParameters const request_params = 
    {
      __PRETTY_FUNCTION__,
      gdk_window_get_width (window),
      gdk_window_get_height (window),
      pixel_format,
      mir_buffer_usage_software
    };

    impl->surface = mir_connection_create_surface_sync (display_mir->mir_connection,
                                                        &request_params);
    if (mir_surface_is_valid (impl->surface))
      mir_surface_set_event_handler (impl->surface, &delegate);

    gdk_window_set_type_hint (window, impl->hint);  

    _gdk_make_event (window, GDK_MAP, NULL, FALSE);
    event = _gdk_make_event (window, GDK_VISIBILITY_NOTIFY, NULL, FALSE);
    event->visibility.state = GDK_VISIBILITY_UNOBSCURED;

    /* Call the next surface handler from within the main thread so that we
     * will immediately have a cairo surfaec to render on */
    next_buffer_arrived (impl->surface, impl);
}

static void
gdk_mir_window_hide (GdkWindow *window)
{
  /* TODO: Unsupported */
  _gdk_window_clear_update_area (window);
}

static void
gdk_window_mir_withdraw (GdkWindow *window)
{
  if (!window->destroyed)
    {
      if (GDK_WINDOW_IS_MAPPED (window))
	gdk_synthesize_window_state (window, 0, GDK_WINDOW_STATE_WITHDRAWN);

      g_assert (!GDK_WINDOW_IS_MAPPED (window));

      /* TODO: Unsupported */
    }
}

static void
gdk_window_mir_set_events (GdkWindow    *window,
			       GdkEventMask  event_mask)
{
  GDK_WINDOW (window)->event_mask = event_mask;
}

static GdkEventMask
gdk_window_mir_get_events (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return 0;
  else
    return GDK_WINDOW (window)->event_mask;
}

static void
gdk_window_mir_raise (GdkWindow *window)
{
  /* FIXME: Unsupported */
}

static void
gdk_window_mir_lower (GdkWindow *window)
{
  /* FIXME: Unsupported */
}

static void
gdk_window_mir_restack_under (GdkWindow *window,
			      GList *native_siblings)
{
}

static void
gdk_window_mir_restack_toplevel (GdkWindow *window,
				 GdkWindow *sibling,
				 gboolean   above)
{
}

static void
gdk_window_mir_move_resize (GdkWindow *window,
                                gboolean   with_move,
                                gint       x,
                                gint       y,
                                gint       width,
                                gint       height)
{
  if (with_move)
    {
      window->x = x;
      window->y = y;
    }

  /* If this function is called with width and height = -1 then that means
   * just move the window - don't update its size
   */
  if (width > 0 && height > 0)
    gdk_mir_window_configure (window, width, height, 0);
}

static void
gdk_window_mir_set_background (GdkWindow      *window,
			       cairo_pattern_t *pattern)
{
}

static gboolean
gdk_window_mir_reparent (GdkWindow *window,
			 GdkWindow *new_parent,
			 gint       x,
			 gint       y)
{
  return FALSE;
}

static void
gdk_window_mir_set_device_cursor (GdkWindow *window,
				  GdkDevice *device,
				  GdkCursor *cursor)
{
  g_return_if_fail (GDK_IS_WINDOW (window));
  g_return_if_fail (GDK_IS_DEVICE (device));

  if (!GDK_WINDOW_DESTROYED (window))
    GDK_DEVICE_GET_CLASS (device)->set_window_cursor (device, window, cursor);
}

static void
gdk_window_mir_get_geometry (GdkWindow *window,
			     gint      *x,
			     gint      *y,
			     gint      *width,
			     gint      *height)
{
  if (!GDK_WINDOW_DESTROYED (window))
    {
      if (x)
	*x = window->x;
      if (y)
	*y = window->y;
      if (width)
	*width = window->width;
      if (height)
	*height = window->height;
    }
}

void
_gdk_mir_window_offset (GdkWindow *window,
                        gint      *x_out,
                        gint      *y_out)
{
  GdkWindowImplMir *impl, *parent_impl;
  GdkWindow *parent_window;
  gint x_offset = 0, y_offset = 0;

  impl = GDK_WINDOW_IMPL_MIR (window->impl);

  parent_window = impl->transient_for;
  while (parent_window)
    {
      parent_impl = GDK_WINDOW_IMPL_MIR (parent_window->impl);

      x_offset += window->x;
      y_offset += window->y;

      parent_window = parent_impl->transient_for;
    }

  *x_out = x_offset;
  *y_out = y_offset;
}

static gint
gdk_window_mir_get_root_coords (GdkWindow *window,
				gint       x,
				gint       y,
				gint      *root_x,
				gint      *root_y)
{
  gint x_offset, y_offset;

  _gdk_mir_window_offset (window, &x_offset, &y_offset);

  *root_x = x_offset + x;
  *root_y = y_offset + y;

  return 1;
}

static gboolean
gdk_window_mir_get_device_state (GdkWindow       *window,
				 GdkDevice       *device,
				 gdouble         *x,
				 gdouble         *y,
				 GdkModifierType *mask)
{
  gboolean return_val;

  g_return_val_if_fail (window == NULL || GDK_IS_WINDOW (window), FALSE);

  return_val = TRUE;

  if (!GDK_WINDOW_DESTROYED (window))
    {
      GdkWindow *child;

      GDK_DEVICE_GET_CLASS (device)->query_state (device, window,
						  NULL, &child,
						  NULL, NULL,
						  x, y, mask);
      return_val = (child != NULL);
    }

  return return_val;
}

static void
gdk_window_mir_shape_combine_region (GdkWindow       *window,
					 const cairo_region_t *shape_region,
					 gint             offset_x,
					 gint             offset_y)
{
}

static void 
gdk_window_mir_input_shape_combine_region (GdkWindow       *window,
					       const cairo_region_t *shape_region,
					       gint             offset_x,
					       gint             offset_y)
{
}

static gboolean
gdk_window_mir_set_static_gravities (GdkWindow *window,
					 gboolean   use_static)
{
  return TRUE;
}

static gboolean
gdk_mir_window_queue_antiexpose (GdkWindow *window,
				     cairo_region_t *area)
{
  return FALSE;
}

static void
gdk_mir_window_destroy (GdkWindow *window,
			gboolean   recursing,
			gboolean   foreign_destroy)
{
  g_return_if_fail (GDK_IS_WINDOW (window));
}

static void
gdk_window_mir_destroy_foreign (GdkWindow *window)
{
}

static cairo_surface_t *
gdk_window_mir_resize_cairo_surface (GdkWindow       *window,
				     cairo_surface_t *surface,
				     gint             width,
				     gint             height)
{
  return surface;
}

static cairo_region_t *
gdk_mir_window_get_shape (GdkWindow *window)
{
  return NULL;
}

static cairo_region_t *
gdk_mir_window_get_input_shape (GdkWindow *window)
{
  return NULL;
}

static void
gdk_mir_window_focus (GdkWindow *window,
                      guint32    timestamp)
{
}

static void
gdk_mir_window_set_type_hint (GdkWindow        *window,
                              GdkWindowTypeHint hint)
{
  GdkWindowImplMir *impl;

  impl = GDK_WINDOW_IMPL_MIR (window->impl);

  if (GDK_WINDOW_DESTROYED (window))
    return;

  impl->hint = hint;

  switch (hint)
    {
    case GDK_WINDOW_TYPE_HINT_MENU:
    case GDK_WINDOW_TYPE_HINT_TOOLBAR:
    case GDK_WINDOW_TYPE_HINT_UTILITY:
    case GDK_WINDOW_TYPE_HINT_DOCK:
    case GDK_WINDOW_TYPE_HINT_DESKTOP:
    case GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU:
    case GDK_WINDOW_TYPE_HINT_POPUP_MENU:
    case GDK_WINDOW_TYPE_HINT_TOOLTIP:
    case GDK_WINDOW_TYPE_HINT_NOTIFICATION:
    case GDK_WINDOW_TYPE_HINT_COMBO:
    case GDK_WINDOW_TYPE_HINT_DND:
      mir_surface_set_type (impl->surface, mir_surface_type_popover);
      break;
    default:
      g_warning ("Unknown hint %d passed to gdk_window_set_type_hint", hint);
      /* Fall thru */
    case GDK_WINDOW_TYPE_HINT_DIALOG:
    case GDK_WINDOW_TYPE_HINT_NORMAL:
    case GDK_WINDOW_TYPE_HINT_SPLASHSCREEN:
      mir_surface_set_type (impl->surface, mir_surface_type_normal);
      break;
    }
}

static GdkWindowTypeHint
gdk_mir_window_get_type_hint (GdkWindow *window)
{
  return GDK_WINDOW_TYPE_HINT_NORMAL;
}

void
gdk_mir_window_set_modal_hint (GdkWindow *window,
                               gboolean   modal)
{
}

static void
gdk_mir_window_set_skip_taskbar_hint (GdkWindow *window,
                                      gboolean   skips_taskbar)
{
}

static void
gdk_mir_window_set_skip_pager_hint (GdkWindow *window,
                                    gboolean   skips_pager)
{
}

static void
gdk_mir_window_set_urgency_hint (GdkWindow *window,
                                 gboolean   urgent)
{
}

static void
gdk_mir_window_set_geometry_hints (GdkWindow         *window,
				   const GdkGeometry *geometry,
				   GdkWindowHints     geom_mask)
{
  GdkWindowImplMir *impl;

  if (GDK_WINDOW_DESTROYED (window) ||
      !WINDOW_IS_TOPLEVEL_OR_FOREIGN (window))
    return;

  impl = GDK_WINDOW_IMPL_MIR (window->impl);

  impl->geometry_hints = *geometry;
  impl->geometry_mask = geom_mask;

  /*
   * GDK_HINT_POS
   * GDK_HINT_USER_POS
   * GDK_HINT_USER_SIZE
   * GDK_HINT_MIN_SIZE
   * GDK_HINT_MAX_SIZE
   * GDK_HINT_BASE_SIZE
   * GDK_HINT_RESIZE_INC
   * GDK_HINT_ASPECT
   * GDK_HINT_WIN_GRAVITY
   */
}

static void
gdk_mir_window_set_title (GdkWindow   *window,
                          const gchar *title)
{
  g_return_if_fail (title != NULL);

  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_set_role (GdkWindow   *window,
                         const gchar *role)
{
}

static void
gdk_mir_window_set_startup_id (GdkWindow   *window,
                               const gchar *startup_id)
{
}

static void
gdk_mir_window_set_transient_for (GdkWindow *window,
                                  GdkWindow *parent)
{
  GdkWindowImplMir *impl;

  impl = GDK_WINDOW_IMPL_MIR (window->impl);
  impl->transient_for = parent;
}

static void
gdk_mir_window_get_root_origin (GdkWindow *window,
				gint      *x,
				gint      *y)
{
  if (x)
    *x = 0;

  if (y)
    *y = 0;
}

static void
gdk_mir_window_get_frame_extents (GdkWindow    *window,
                                  GdkRectangle *rect)
{
  rect->x = window->x;
  rect->y = window->y;
  rect->width = window->width;
  rect->height = window->height;
}

static void
gdk_mir_window_set_override_redirect (GdkWindow *window,
                                      gboolean override_redirect)
{
}

static void
gdk_mir_window_set_accept_focus (GdkWindow *window,
                                 gboolean accept_focus)
{
}

static void
gdk_mir_window_set_focus_on_map (GdkWindow *window,
                                 gboolean focus_on_map)
{
  focus_on_map = focus_on_map != FALSE;

  if (window->focus_on_map != focus_on_map)
    {
      window->focus_on_map = focus_on_map;

      if ((!GDK_WINDOW_DESTROYED (window)) &&
	  (!window->focus_on_map) &&
	  WINDOW_IS_TOPLEVEL_OR_FOREIGN (window))
	gdk_mir_window_set_user_time (window, 0);
    }
}

static void
gdk_mir_window_set_icon_list (GdkWindow *window,
				  GList     *pixbufs)
{
}

static void
gdk_mir_window_set_icon_name (GdkWindow   *window,
				  const gchar *name)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_iconify (GdkWindow *window)
{
}

static void
gdk_mir_window_deiconify (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window) ||
      !WINDOW_IS_TOPLEVEL_OR_FOREIGN (window))
    return;

  if (GDK_WINDOW_IS_MAPPED (window))
    {  
      gdk_window_show (window);
    }
  else
    {
      /* Flip our client side flag, the real work happens on map. */
      gdk_synthesize_window_state (window, GDK_WINDOW_STATE_ICONIFIED, 0);
    }
}

static void
gdk_mir_window_stick (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_unstick (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_maximize (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_unmaximize (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_fullscreen (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_unfullscreen (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_set_keep_above (GdkWindow *window,
				   gboolean   setting)
{
  g_return_if_fail (GDK_IS_WINDOW (window));

  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static void
gdk_mir_window_set_keep_below (GdkWindow *window, gboolean setting)
{
  g_return_if_fail (GDK_IS_WINDOW (window));

  if (GDK_WINDOW_DESTROYED (window))
    return;
}

static GdkWindow *
gdk_mir_window_get_group (GdkWindow *window)
{
  if (GDK_WINDOW_DESTROYED (window) ||
      !WINDOW_IS_TOPLEVEL (window))
    return NULL;

  return NULL;
}

static void
gdk_mir_window_set_group (GdkWindow *window,
			      GdkWindow *leader)
{
  g_return_if_fail (GDK_IS_WINDOW (window));
  g_return_if_fail (GDK_WINDOW_TYPE (window) != GDK_WINDOW_CHILD);
  g_return_if_fail (leader == NULL || GDK_IS_WINDOW (leader));
}

static void
gdk_mir_window_set_decorations (GdkWindow      *window,
                                GdkWMDecoration decorations)
{
}

static gboolean
gdk_mir_window_get_decorations (GdkWindow       *window,
                                GdkWMDecoration *decorations)
{
  return FALSE;
}

static void
gdk_mir_window_set_functions (GdkWindow    *window,
                              GdkWMFunction functions)
{
}

static void
gdk_mir_window_begin_resize_drag (GdkWindow     *window,
                                  GdkWindowEdge  edge,
                                  GdkDevice     *device,
                                  gint           button,
                                  gint           root_x,
                                  gint           root_y,
                                  guint32        timestamp)
{
  uint32_t grab_type;

  if (GDK_WINDOW_DESTROYED (window) ||
      !WINDOW_IS_TOPLEVEL_OR_FOREIGN (window))
    return;

  switch (edge)
    {
    case GDK_WINDOW_EDGE_NORTH_WEST:
      grab_type = MIR_SURFACE_RESIZE_TOP_LEFT;
      break;

    case GDK_WINDOW_EDGE_NORTH:
      grab_type = MIR_SURFACE_RESIZE_TOP;
      break;

    case GDK_WINDOW_EDGE_NORTH_EAST:
      grab_type = MIR_SURFACE_RESIZE_RIGHT;
      break;

    case GDK_WINDOW_EDGE_WEST:
      grab_type = MIR_SURFACE_RESIZE_LEFT;
      break;

    case GDK_WINDOW_EDGE_EAST:
      grab_type = MIR_SURFACE_RESIZE_RIGHT;
      break;

    case GDK_WINDOW_EDGE_SOUTH_WEST:
      grab_type = MIR_SURFACE_RESIZE_BOTTOM_LEFT;
      break;

    case GDK_WINDOW_EDGE_SOUTH:
      grab_type = MIR_SURFACE_RESIZE_BOTTOM;
      break;

    case GDK_WINDOW_EDGE_SOUTH_EAST:
      grab_type = MIR_SURFACE_RESIZE_BOTTOM_RIGHT;
      break;

    default:
      g_warning ("gdk_window_begin_resize_drag: bad resize edge %d!",
                 edge);
      return;
    }

  (void) grab_type;
}

static void
gdk_mir_window_begin_move_drag (GdkWindow *window,
				GdkDevice *device,
				gint       button,
				gint       root_x,
				gint       root_y,
				guint32    timestamp)
{
}

static void
gdk_mir_window_enable_synchronized_configure (GdkWindow *window)
{
}

static void
gdk_mir_window_configure_finished (GdkWindow *window)
{
  if (!WINDOW_IS_TOPLEVEL (window))
    return;

  if (!GDK_IS_WINDOW_IMPL_MIR (window->impl))
    return;
}

static void
gdk_mir_window_set_opacity (GdkWindow *window,
                            gdouble    opacity)
{
}

static void
gdk_mir_window_set_composited (GdkWindow *window,
                               gboolean   composited)
{
}

static void
gdk_mir_window_destroy_notify (GdkWindow *window)
{
  if (!GDK_WINDOW_DESTROYED (window))
    {
      if (GDK_WINDOW_TYPE(window) != GDK_WINDOW_FOREIGN)
	g_warning ("GdkWindow %p unexpectedly destroyed", window);

      _gdk_window_destroy (window, TRUE);
    }

  g_object_unref (window);
}

static void
wait_for_any_pending_cairo_surface (GdkWindow *window)
{
  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);

  impl->graphics_region_surface =
    g_async_queue_pop (impl->graphics_region_surface_queue);

  g_assert (impl->graphics_region_surface != NULL);
}

static gboolean
gdk_mir_window_begin_paint_region (GdkWindow       *window,
                                   const cairo_region_t *region)
{
  return TRUE;
}

static cairo_surface_t *
gdk_mir_window_ref_cairo_surface (GdkWindow *window)
{
  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);

  if (GDK_WINDOW_DESTROYED (impl->wrapper))
    return NULL;

  cairo_surface_reference (impl->cairo_surface);

  return impl->cairo_surface;
}

static cairo_surface_t *
gdk_mir_window_create_similar_image_surface (GdkWindow *     window,
                                             cairo_format_t  format,
                                             int             width,
                                             int             height)
{
  return cairo_image_surface_create (format, width, height);
}

static void
gdk_mir_window_process_updates_recurse (GdkWindow      *window,
                                        cairo_region_t *region)
{
  GdkWindowImplMir *impl = GDK_WINDOW_IMPL_MIR (window->impl);
  guint i, n = cairo_region_num_rectangles (region);;
  cairo_rectangle_int_t rect;
  cairo_t *graphics_region_context = NULL;

  /* Ensure that the window is mapped */
  gdk_mir_window_map (window);

  /* Wait for any new surface to arrive */
  wait_for_any_pending_cairo_surface (window);

  /* Copy from pending buffer to new backbuffer and flip
   * the buffers on the server side */
  graphics_region_context = cairo_create (impl->graphics_region_surface);
  cairo_set_operator (graphics_region_context, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface (graphics_region_context,
                            impl->cairo_surface,
                            0.0f,
                            0.0f);

  /* TODO: Use the native buffer age to determine which
   * areas need to be copied */
  for (i = 0; i < n; ++i)
    {
      cairo_region_get_rectangle (region, i, &rect);
    }

  /* Copy from pending to back */
  cairo_rectangle (graphics_region_context,
                   0.0f,
                   0.0f,
                   window->width,
                   window->height);
  cairo_fill (graphics_region_context);

  cairo_destroy (graphics_region_context);

  cairo_surface_finish (impl->graphics_region_surface);
  cairo_surface_destroy (impl->graphics_region_surface);
  impl->graphics_region_surface = NULL;

  mir_surface_swap_buffers (impl->surface, next_buffer_arrived, impl);

  _gdk_window_process_updates_recurse (window, region);
}

static void
gdk_mir_window_sync_rendering (GdkWindow *window)
{
}

static gboolean
gdk_mir_window_simulate_key (GdkWindow      *window,
			     gint            x,
			     gint            y,
			     guint           keyval,
			     GdkModifierType modifiers,
			     GdkEventType    key_pressrelease)
{
  return FALSE;
}

static gboolean
gdk_mir_window_simulate_button (GdkWindow      *window,
				gint            x,
				gint            y,
				guint           button, /*1..3*/
				GdkModifierType modifiers,
				GdkEventType    button_pressrelease)
{
  return FALSE;
}

static gboolean
gdk_mir_window_get_property (GdkWindow   *window,
			     GdkAtom      property,
			     GdkAtom      type,
			     gulong       offset,
			     gulong       length,
			     gint         pdelete,
			     GdkAtom     *actual_property_type,
			     gint        *actual_format_type,
			     gint        *actual_length,
			     guchar     **data)
{
  return FALSE;
}

static void
gdk_mir_window_change_property (GdkWindow    *window,
				GdkAtom       property,
				GdkAtom       type,
				gint          format,
				GdkPropMode   mode,
				const guchar *data,
				gint          nelements)
{
}

static void
gdk_mir_window_delete_property (GdkWindow *window,
				GdkAtom    property)
{
}

static void
_gdk_window_impl_mir_class_init (GdkWindowImplMirClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkWindowImplClass *impl_class = GDK_WINDOW_IMPL_CLASS (klass);

  object_class->finalize = gdk_window_impl_mir_finalize;

  impl_class->ref_cairo_surface = gdk_mir_window_ref_cairo_surface;
  impl_class->create_similar_image_surface = gdk_mir_window_create_similar_image_surface;
  impl_class->show = gdk_mir_window_show;
  impl_class->hide = gdk_mir_window_hide;
  impl_class->withdraw = gdk_window_mir_withdraw;
  impl_class->set_events = gdk_window_mir_set_events;
  impl_class->get_events = gdk_window_mir_get_events;
  impl_class->raise = gdk_window_mir_raise;
  impl_class->lower = gdk_window_mir_lower;
  impl_class->restack_under = gdk_window_mir_restack_under;
  impl_class->restack_toplevel = gdk_window_mir_restack_toplevel;
  impl_class->move_resize = gdk_window_mir_move_resize;
  impl_class->set_background = gdk_window_mir_set_background;
  impl_class->reparent = gdk_window_mir_reparent;
  impl_class->set_device_cursor = gdk_window_mir_set_device_cursor;
  impl_class->get_geometry = gdk_window_mir_get_geometry;
  impl_class->get_root_coords = gdk_window_mir_get_root_coords;
  impl_class->get_device_state = gdk_window_mir_get_device_state;
  impl_class->shape_combine_region = gdk_window_mir_shape_combine_region;
  impl_class->input_shape_combine_region = gdk_window_mir_input_shape_combine_region;
  impl_class->set_static_gravities = gdk_window_mir_set_static_gravities;
  impl_class->queue_antiexpose = gdk_mir_window_queue_antiexpose;
  impl_class->destroy = gdk_mir_window_destroy;
  impl_class->destroy_foreign = gdk_window_mir_destroy_foreign;
  impl_class->resize_cairo_surface = gdk_window_mir_resize_cairo_surface;
  impl_class->get_shape = gdk_mir_window_get_shape;
  impl_class->get_input_shape = gdk_mir_window_get_input_shape;
  /* impl_class->beep */

  impl_class->focus = gdk_mir_window_focus;
  impl_class->set_type_hint = gdk_mir_window_set_type_hint;
  impl_class->get_type_hint = gdk_mir_window_get_type_hint;
  impl_class->set_modal_hint = gdk_mir_window_set_modal_hint;
  impl_class->set_skip_taskbar_hint = gdk_mir_window_set_skip_taskbar_hint;
  impl_class->set_skip_pager_hint = gdk_mir_window_set_skip_pager_hint;
  impl_class->set_urgency_hint = gdk_mir_window_set_urgency_hint;
  impl_class->set_geometry_hints = gdk_mir_window_set_geometry_hints;
  impl_class->set_title = gdk_mir_window_set_title;
  impl_class->set_role = gdk_mir_window_set_role;
  impl_class->set_startup_id = gdk_mir_window_set_startup_id;
  impl_class->set_transient_for = gdk_mir_window_set_transient_for;
  impl_class->get_root_origin = gdk_mir_window_get_root_origin;
  impl_class->get_frame_extents = gdk_mir_window_get_frame_extents;
  impl_class->set_override_redirect = gdk_mir_window_set_override_redirect;
  impl_class->set_accept_focus = gdk_mir_window_set_accept_focus;
  impl_class->set_focus_on_map = gdk_mir_window_set_focus_on_map;
  impl_class->set_icon_list = gdk_mir_window_set_icon_list;
  impl_class->set_icon_name = gdk_mir_window_set_icon_name;
  impl_class->iconify = gdk_mir_window_iconify;
  impl_class->deiconify = gdk_mir_window_deiconify;
  impl_class->stick = gdk_mir_window_stick;
  impl_class->unstick = gdk_mir_window_unstick;
  impl_class->maximize = gdk_mir_window_maximize;
  impl_class->unmaximize = gdk_mir_window_unmaximize;
  impl_class->fullscreen = gdk_mir_window_fullscreen;
  impl_class->unfullscreen = gdk_mir_window_unfullscreen;
  impl_class->set_keep_above = gdk_mir_window_set_keep_above;
  impl_class->set_keep_below = gdk_mir_window_set_keep_below;
  impl_class->get_group = gdk_mir_window_get_group;
  impl_class->set_group = gdk_mir_window_set_group;
  impl_class->set_decorations = gdk_mir_window_set_decorations;
  impl_class->get_decorations = gdk_mir_window_get_decorations;
  impl_class->set_functions = gdk_mir_window_set_functions;
  impl_class->begin_resize_drag = gdk_mir_window_begin_resize_drag;
  impl_class->begin_move_drag = gdk_mir_window_begin_move_drag;
  impl_class->enable_synchronized_configure = gdk_mir_window_enable_synchronized_configure;
  impl_class->configure_finished = gdk_mir_window_configure_finished;
  impl_class->set_opacity = gdk_mir_window_set_opacity;
  impl_class->set_composited = gdk_mir_window_set_composited;
  impl_class->destroy_notify = gdk_mir_window_destroy_notify;
  impl_class->get_drag_protocol = _gdk_mir_window_get_drag_protocol;
  impl_class->register_dnd = _gdk_mir_window_register_dnd;
  impl_class->drag_begin = _gdk_mir_window_drag_begin;
  impl_class->process_updates_recurse = gdk_mir_window_process_updates_recurse;
  impl_class->begin_paint_region = gdk_mir_window_begin_paint_region;
  impl_class->sync_rendering = gdk_mir_window_sync_rendering;
  impl_class->simulate_key = gdk_mir_window_simulate_key;
  impl_class->simulate_button = gdk_mir_window_simulate_button;
  impl_class->get_property = gdk_mir_window_get_property;
  impl_class->change_property = gdk_mir_window_change_property;
  impl_class->delete_property = gdk_mir_window_delete_property;
}
