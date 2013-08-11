/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2009 Carlos Garnacho <carlosg@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "config.h"

#include <string.h>
#include <gdk/gdkwindow.h>
#include <gdk/gdktypes.h>
#include "gdkprivate-mir.h"
#include "gdkmir.h"
#include "gdkkeysyms.h"
#include "gdkdeviceprivate.h"
#include "gdkdevicemanagerprivate.h"

#include <sys/time.h>
#include <sys/mman.h>

typedef struct _GdkMirDeviceData GdkMirDeviceData;
typedef struct _GdkMirDevice GdkMirDevice;
typedef struct _GdkMirDeviceClass GdkMirDeviceClass;

struct _GdkMirDeviceData
{
  /* Mutex must always be locked access to avoid
   * race conditions between the update threads
   * and the reading threads */
  GMutex mutex;

  /* Bitwise OR mask of the buttons down on this mouse */
  MirMotionButton button_state;

  /* Pointer co-ordinates */
  gdouble         surface_pointer_x;
  gdouble         surface_pointer_y;

  /* hovered_window is the window the mouse is currently
   * inside of */
  GdkWindow       *hovered_window;

  /* These fields may only be accessed in the main thread */
  GdkWindow       *button_grab_window;
  GdkWindow       *focused_window;
  guint32         time;
};

struct _GdkMirDevice
{
  GdkDevice parent_instance;
  GdkMirDeviceData *device;
};

struct _GdkMirDeviceClass
{
  GdkDeviceClass parent_class;
};

G_DEFINE_TYPE (GdkMirDevice, gdk_mir_device, GDK_TYPE_DEVICE)

#define GDK_TYPE_MIR_DEVICE_MANAGER        (gdk_mir_device_manager_get_type ())
#define GDK_MIR_DEVICE_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDK_TYPE_MIR_DEVICE_MANAGER, GdkMirDeviceManager))
#define GDK_MIR_DEVICE_MANAGER_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), GDK_TYPE_MIR_DEVICE_MANAGER, GdkMirDeviceManagerClass))
#define GDK_IS_MIR_DEVICE_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDK_TYPE_MIR_DEVICE_MANAGER))
#define GDK_IS_MIR_DEVICE_MANAGER_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), GDK_TYPE_MIR_DEVICE_MANAGER))
#define GDK_MIR_DEVICE_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDK_TYPE_MIR_DEVICE_MANAGER, GdkMirDeviceManagerClass))

typedef struct _GdkMirDeviceManager GdkMirDeviceManager;
typedef struct _GdkMirDeviceManagerClass GdkMirDeviceManagerClass;

struct _GdkMirDeviceManager
{
  GdkDeviceManager parent_object;
  GList *devices;
  GdkMirDeviceData *core_seat_data;
};

struct _GdkMirDeviceManagerClass
{
  GdkDeviceManagerClass parent_class;
};

G_DEFINE_TYPE (GdkMirDeviceManager,
	       gdk_mir_device_manager, GDK_TYPE_DEVICE_MANAGER)

static gboolean
gdk_mir_device_get_history (GdkDevice      *device,
			    GdkWindow      *window,
			    guint32         start,
			    guint32         stop,
			    GdkTimeCoord ***events,
			    gint           *n_events)
{
  return FALSE;
}

static void
gdk_mir_device_get_state (GdkDevice       *device,
			  GdkWindow       *window,
			  gdouble         *axes,
			  GdkModifierType *mask)
{
  gdouble x, y;

  gdk_window_get_device_position_double (window, device, &x, &y, mask);

  if (axes)
    {
      axes[0] = x;
      axes[1] = y;
    }
}

static void
gdk_mir_device_set_window_cursor (GdkDevice *device,
				  GdkWindow *window,
				  GdkCursor *cursor)
{
}

static void
gdk_mir_device_warp (GdkDevice *device,
                     GdkScreen *screen,
                     gdouble    x,
                     gdouble    y)
{
}

static void
gdk_mir_device_query_state (GdkDevice        *device,
			    GdkWindow        *window,
			    GdkWindow       **root_window,
			    GdkWindow       **child_window,
			    gdouble          *root_x,
			    gdouble          *root_y,
			    gdouble          *win_x,
			    gdouble          *win_y,
			    GdkModifierType  *mask)
{
  GdkMirDeviceData *mir_device = GDK_MIR_DEVICE (device)->device;
  GdkDisplay *display = gdk_device_get_display (device);
  GdkScreen *screen = gdk_display_get_default_screen (display);
  GdkWindow *root = gdk_screen_get_root_window (screen);

  g_mutex_lock (&mir_device->mutex);

  if (root_window)
    *root_window = root;
  if (child_window)
    *child_window = mir_device->hovered_window;
  if (root_x)
    *root_x = mir_device->surface_pointer_x;
  if (root_y)
    *root_y = mir_device->surface_pointer_y;
  if (win_x)
    *win_x = mir_device->surface_pointer_x;
  if (win_y)
    *win_y = mir_device->surface_pointer_y;
  if (mask)
    *mask = 0;

  g_mutex_unlock (&mir_device->mutex);
}

static GdkGrabStatus
gdk_mir_device_grab (GdkDevice    *device,
		     GdkWindow    *window,
		     gboolean      owner_events,
		     GdkEventMask  event_mask,
		     GdkWindow    *confine_to,
		     GdkCursor    *cursor,
		     guint32       time_)
{
  if (gdk_device_get_source (device) == GDK_SOURCE_KEYBOARD)
    return GDK_GRAB_SUCCESS;
  else
    {
      GdkMirDeviceData *mir_device = GDK_MIR_DEVICE (device)->device;

      if (mir_device->button_grab_window != NULL &&
          time_ != 0 && mir_device->time > time_)
        return GDK_GRAB_ALREADY_GRABBED;

      if (time_ == 0)
        time_ = mir_device->time;


      /* No lock necessary here as the writer thread is not interested in
       * button_grab_window */

      _gdk_mir_window_set_device_grabbed (window, device, time_);
    }

  return GDK_GRAB_SUCCESS;
}

static void
gdk_mir_device_ungrab (GdkDevice *device,
		       guint32    time_)
{
  if (gdk_device_get_source (device) == GDK_SOURCE_MOUSE)
    {
      GdkMirDeviceData *mir_device = GDK_MIR_DEVICE (device)->device;
      GdkDeviceGrabInfo *grab =
        _gdk_display_get_last_device_grab (gdk_device_get_display (device),
                                           device);

      if (grab)
        grab->serial_end = grab->serial_start;

      /* No lock necessary here as the writer thread is not interested in
       * button_grab_window */

      if (mir_device->button_grab_window)
        _gdk_mir_window_set_device_grabbed (mir_device->button_grab_window,
                                            NULL,
                                            0);
    }
}

static GdkWindow *
gdk_mir_device_window_at_position (GdkDevice       *device,
				   gdouble         *win_x,
				   gdouble         *win_y,
				   GdkModifierType *mask,
				   gboolean         get_toplevel)
{
  GdkMirDeviceData *mir_device = GDK_MIR_DEVICE (device)->device;

  g_mutex_lock (&mir_device->mutex);

  if (win_x)
    *win_x = mir_device->surface_pointer_x;
  if (win_y)
    *win_y = mir_device->surface_pointer_y;
  if (mask)
    *mask = 0;

  GdkWindow *hovered = mir_device->hovered_window;

  g_mutex_unlock (&mir_device->mutex);

  return hovered;
}

static void
gdk_mir_device_select_window_events (GdkDevice    *device,
                                     GdkWindow    *window,
                                     GdkEventMask  event_mask)
{
}

static void
gdk_mir_device_class_init (GdkMirDeviceClass *klass)
{
  GdkDeviceClass *device_class = GDK_DEVICE_CLASS (klass);

  device_class->get_history = gdk_mir_device_get_history;
  device_class->get_state = gdk_mir_device_get_state;
  device_class->set_window_cursor = gdk_mir_device_set_window_cursor;
  device_class->warp = gdk_mir_device_warp;
  device_class->query_state = gdk_mir_device_query_state;
  device_class->grab = gdk_mir_device_grab;
  device_class->ungrab = gdk_mir_device_ungrab;
  device_class->window_at_position = gdk_mir_device_window_at_position;
  device_class->select_window_events = gdk_mir_device_select_window_events;
}

static void
gdk_mir_device_init (GdkMirDevice *device_core)
{
  GdkDevice *device;

  device = GDK_DEVICE (device_core);

  _gdk_device_add_axis (device, GDK_NONE, GDK_AXIS_X, 0, 0, 1);
  _gdk_device_add_axis (device, GDK_NONE, GDK_AXIS_Y, 0, 0, 1);
}

GdkKeymap *
_gdk_mir_device_get_keymap (GdkDevice *device)
{
  return _gdk_mir_keymap_new ();
}

void
_gdk_mir_mouse_update_from_button_event (GdkDevice            *device,
					 GdkWindow            *window,
					 const MirMotionEvent *event,
					 MirMotionButton      *gained_buttons,
					 MirMotionButton      *lost_buttons)
{
  g_return_if_fail (GDK_IS_MIR_DEVICE (device));
  g_return_if_fail (event != NULL);
  g_return_if_fail (window != NULL);
  g_return_if_fail (gained_buttons != NULL);
  g_return_if_fail (lost_buttons != NULL);

  GdkMirDevice *mir_device = GDK_MIR_DEVICE (device);
  GdkMirDeviceData *mir_device_data = mir_device->device;

  *gained_buttons = (~mir_device_data->button_state & event->button_state);
  *lost_buttons = (mir_device_data->button_state & ~event->button_state);

  /* Change focus on button press */
  _gdk_mir_window_generate_focus_event (window,
                                        mir_device_data->hovered_window,
                                        device);

  mir_device_data->button_state = event->button_state;
  mir_device_data->surface_pointer_x = event->pointer_coordinates[0].x;
  mir_device_data->surface_pointer_y = event->pointer_coordinates[0].y;
  mir_device_data->hovered_window = window;
}

void
_gdk_mir_mouse_update_from_motion_event (GdkDevice            *device,
                                         GdkWindow            *window,
                                         const MirMotionEvent *event)
{
  g_return_if_fail (GDK_IS_MIR_DEVICE (device));
  g_return_if_fail (event != NULL);
  g_return_if_fail (window != NULL);

  GdkMirDevice *mir_device = GDK_MIR_DEVICE (device);
  GdkMirDeviceData *mir_device_data = mir_device->device;

  mir_device_data->surface_pointer_x = event->pointer_coordinates[0].x;
  mir_device_data->surface_pointer_y = event->pointer_coordinates[0].y;
  mir_device_data->hovered_window = window;
}

void
_gdk_mir_mouse_update_from_crossing_event (GdkDevice            *device,
                                           GdkWindow            *window,
                                           const MirMotionEvent *event)
{
  g_return_if_fail (GDK_IS_MIR_DEVICE (device));
  g_return_if_fail (event != NULL);
  g_return_if_fail (window != NULL);

  GdkMirDevice *mir_device = GDK_MIR_DEVICE (device);
  GdkMirDeviceData *mir_device_data = mir_device->device;

  mir_device_data->surface_pointer_x = event->pointer_coordinates[0].x;
  mir_device_data->surface_pointer_y = event->pointer_coordinates[0].y;
  mir_device_data->hovered_window = window;
}

GdkWindow *
_gdk_mir_device_get_hovered_window (GdkDevice *device)
{
  GdkMirDeviceData *data = GDK_MIR_DEVICE (device)->device;

  g_mutex_lock (&data->mutex);
  GdkWindow *window = data->hovered_window;
  g_mutex_unlock (&data->mutex);

  return window;
}

GdkDevice *
_gdk_mir_device_manager_get_core_keyboard (GdkDeviceManager *manager)
{
  GdkDevice *keyboard = NULL;
  GList *device_list = gdk_device_manager_list_devices (manager,
                                                        GDK_DEVICE_TYPE_MASTER);
  GList *node = device_list;
  while (node)
    {
      GdkDevice *dev = (GdkDevice *) node->data;
      if (gdk_device_get_source (dev) == GDK_SOURCE_KEYBOARD)
        keyboard = dev;
      node = g_list_next (node);
    }

  return keyboard;
}

static void
free_device (gpointer data)
{
  g_object_unref (data);
}

static void
gdk_mir_device_manager_finalize (GObject *object)
{
  GdkMirDeviceManager *device_manager;

  device_manager = GDK_MIR_DEVICE_MANAGER (object);

  g_list_free_full (device_manager->devices, free_device);
  g_mutex_clear (&device_manager->core_seat_data->mutex);
  g_free (device_manager->core_seat_data);

  G_OBJECT_CLASS (gdk_mir_device_manager_parent_class)->finalize (object);
}

static GdkDevice *
create_core_pointer (GdkDeviceManager *device_manager,
                     GdkDisplay       *display,
                     GdkMirDeviceData *device_data)
{
  GObject *object =
    g_object_new (GDK_TYPE_MIR_DEVICE,
                 "name", "Core Pointer",
                 "type", GDK_DEVICE_TYPE_MASTER,
                 "input-source", GDK_SOURCE_MOUSE,
                 "input-mode", GDK_MODE_SCREEN,
                 "has-cursor", TRUE,
                 "display", display,
                 "device-manager", device_manager,
                 NULL);
  GdkMirDevice *mir_device = GDK_MIR_DEVICE (object);
  mir_device->device = device_data;
  return GDK_DEVICE (object);
}

static GdkDevice *
create_core_keyboard (GdkDeviceManager *device_manager,
                      GdkDisplay       *display,
                      GdkMirDeviceData *device_data)
{
  GObject *object =
    g_object_new (GDK_TYPE_MIR_DEVICE,
                  "name", "Core Keyboard",
                  "type", GDK_DEVICE_TYPE_MASTER,
                  "input-source", GDK_SOURCE_KEYBOARD,
                  "input-mode", GDK_MODE_SCREEN,
                  "has-cursor", FALSE,
                  "display", display,
                  "device-manager", device_manager,
                  NULL);
  GdkMirDevice *mir_device = GDK_MIR_DEVICE (object);
  mir_device->device = device_data;
  return GDK_DEVICE (object);
}

static void
gdk_mir_device_manager_constructed (GObject *object)
{
  GdkMirDeviceManager *manager = GDK_MIR_DEVICE_MANAGER(object);
  GdkDisplay *display = gdk_device_manager_get_display(GDK_DEVICE_MANAGER(manager));

  manager->core_seat_data = g_new0(GdkMirDeviceData, 1);
  g_mutex_init (&manager->core_seat_data->mutex);

  GdkDevice *core_keyboard = create_core_keyboard(GDK_DEVICE_MANAGER(manager),
                                                  display,
                                                  manager->core_seat_data);
  GdkDevice *core_pointer = create_core_pointer(GDK_DEVICE_MANAGER(manager),
                                                display,
                                                manager->core_seat_data);

  /* Create associations */
  _gdk_device_set_associated_device (core_pointer, core_keyboard);
  _gdk_device_set_associated_device (core_keyboard, core_pointer);

  /* Add both to the list of devices */
  manager->devices = g_list_prepend(manager->devices, core_keyboard);
  manager->devices = g_list_prepend(manager->devices, core_pointer);
}

static GList *
gdk_mir_device_manager_list_devices (GdkDeviceManager *device_manager,
				     GdkDeviceType     type)
{
  GdkMirDeviceManager *mir_device_manager;
  GList *devices = NULL;

  /* We only have master devices */
  if (type == GDK_DEVICE_TYPE_MASTER)
    {
      mir_device_manager = (GdkMirDeviceManager *) device_manager;
      devices = g_list_copy(mir_device_manager->devices);
    }

  return devices;
}

static GdkDevice *
gdk_mir_device_manager_get_client_pointer (GdkDeviceManager *device_manager)
{
  GdkMirDeviceManager *mir_device_manager;
  GList *l;

  mir_device_manager = (GdkMirDeviceManager *) device_manager;

  /* Find the first pointer device */
  for (l = mir_device_manager->devices; l != NULL; l = l->next)
    {
      GdkDevice *device = l->data;

      if (gdk_device_get_source (device) == GDK_SOURCE_MOUSE)
        return device;
    }

  return NULL;
}

static void
gdk_mir_device_manager_class_init (GdkMirDeviceManagerClass *klass)
{
  GdkDeviceManagerClass *device_manager_class = GDK_DEVICE_MANAGER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdk_mir_device_manager_finalize;
  object_class->constructed = gdk_mir_device_manager_constructed;
  device_manager_class->list_devices = gdk_mir_device_manager_list_devices;
  device_manager_class->get_client_pointer = gdk_mir_device_manager_get_client_pointer;
}

static void
gdk_mir_device_manager_init (GdkMirDeviceManager *device_manager)
{
}

GdkDeviceManager *
_gdk_mir_device_manager_new (GdkDisplay *display)
{
  return g_object_new (GDK_TYPE_MIR_DEVICE_MANAGER,
                       "display", display,
                       NULL);
}
