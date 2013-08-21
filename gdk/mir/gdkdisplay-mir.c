/*
 * Copyright © 2010 Intel Corporation
 * Copyright (C) 2013 Deepin, Inc.
 *                    Zhai Xiang <zhaixiang@linuxdeepin.com>
 * Copyright (c) 2013 Sam Spilsbury <smspillaz@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/poll.h>

#include <glib.h>
#include "gdkmir.h"
#include "gdkdisplay.h"
#include "gdkdisplay-mir.h"
#include "gdkscreen.h"
#include "gdkinternals.h"
#include "gdkdeviceprivate.h"
#include "gdkdevicemanager.h"
#include "gdkkeysprivate.h"
#include "gdkprivate-mir.h"
#include "gdkmireventlistener.h"

G_DEFINE_TYPE (GdkMirDisplay, _gdk_mir_display, GDK_TYPE_DISPLAY)

static void
gdk_mir_display_process_event_internal (GdkDisplay *display,
                                        GdkEvent   *event);

static void
gdk_input_init (GdkDisplay *display)
{
  GdkMirDisplay *display_mir;
  GdkDeviceManager *device_manager;
  GdkDevice *device;
  GList *list, *l;

  display_mir = GDK_MIR_DISPLAY (display);
  device_manager = gdk_display_get_device_manager (display);

  /* For backwards compatibility, just add
   * floating devices that are not keyboards.
   */
  list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_FLOATING);

  for (l = list; l; l = l->next)
    {
      device = l->data;

      if (gdk_device_get_source (device) == GDK_SOURCE_KEYBOARD)
	continue;

      display_mir->input_devices = g_list_prepend (display_mir->input_devices, l->data);
    }

  g_list_free (list);

  /* Now set "core" pointer to the first
   * master device that is a pointer.
   */
  list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

  for (l = list; l; l = l->next)
    {
      device = list->data;

      if (gdk_device_get_source (device) != GDK_SOURCE_MOUSE)
	continue;

      display->core_pointer = device;
      break;
    }

  /* Add the core pointer to the devices list */
  display_mir->input_devices = g_list_prepend (display_mir->input_devices, display->core_pointer);

  g_list_free (list);
}

static void
free_gdk_event_for_queue (gpointer event)
{
  gdk_event_free ((GdkEvent *) event);
}

GdkDisplay *
_gdk_mir_display_open(const gchar *display_name)
{
  GdkDisplay *display = NULL;
  GdkMirDisplay *display_mir = NULL;

  display = g_object_new (GDK_TYPE_MIR_DISPLAY, NULL);
  display_mir = GDK_MIR_DISPLAY(display);

  display_mir->mir_connection = mir_connect_sync (display_name,
                                                  __PRETTY_FUNCTION__);
  if (display_mir->mir_connection == NULL)
    return NULL;

  if (mir_connection_is_valid (display_mir->mir_connection) == 0)
    return NULL;

  mir_connection_get_display_info (display_mir->mir_connection,
                                   &display_mir->mir_display_info);

  display->device_manager = _gdk_mir_device_manager_new (display);
  display_mir->screen = _gdk_mir_screen_new (display);

  display_mir->event_source =
    _gdk_mir_display_event_source_new (display);
  display_mir->event_listeners =
    g_hash_table_new (g_direct_hash,
                      g_direct_equal);
  display_mir->event_queue = g_queue_new ();

  gdk_input_init (display);

  g_signal_emit_by_name (display, "opened");
  g_signal_emit_by_name (gdk_display_manager_get (),
                         "display_opened", display);

  return display;
}

static void
gdk_mir_display_dispose(GObject *object)
{
  GdkMirDisplay *display_mir = GDK_MIR_DISPLAY(object);

  g_list_foreach (display_mir->input_devices,
                  (GFunc) g_object_run_dispose, NULL);

  _gdk_screen_close( display_mir->screen);

  if (display_mir->event_queue)
    g_queue_free_full (display_mir->event_queue,
                       free_gdk_event_for_queue);
  if (display_mir->event_listeners)
    g_hash_table_destroy (display_mir->event_listeners);
  if (display_mir->event_source)
    {
      g_source_destroy (display_mir->event_source);
      g_source_unref (display_mir->event_source);
      display_mir->event_source = NULL;
    }

  mir_connection_release (display_mir->mir_connection);

  G_OBJECT_CLASS (_gdk_mir_display_parent_class)->dispose (object);
}

static void
gdk_mir_display_finalize (GObject *object)
{
  GdkMirDisplay *display_mir = GDK_MIR_DISPLAY (object);

  /* Keymap */
  if (display_mir->keymap)
    g_object_unref (display_mir->keymap);

  /* input GdkDevice list */
  g_list_free_full (display_mir->input_devices, g_object_unref);

  g_object_unref(display_mir->event_queue);

  g_object_unref (display_mir->screen);
  g_free (display_mir->startup_notification_id);

  G_OBJECT_CLASS (_gdk_mir_display_parent_class)->finalize (object);
}

static const gchar *
gdk_mir_display_get_name (GdkDisplay *display)
{
  return "Mir";
}

static GdkScreen *
gdk_mir_display_get_default_screen (GdkDisplay *display)
{
  g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);

  return GDK_MIR_DISPLAY (display)->screen;
}

static void
gdk_mir_display_beep (GdkDisplay *display)
{
  g_return_if_fail (GDK_IS_DISPLAY (display));
}

static void
gdk_mir_display_sync (GdkDisplay *display)
{
  /* Possibly keep all the MirWaitHandle's in a list here
   * and then wait on all of them ? */
}

static void
gdk_mir_display_flush (GdkDisplay *display)
{
  g_return_if_fail (GDK_IS_DISPLAY (display));
}

static gboolean
gdk_mir_display_has_pending (GdkDisplay *display)
{
  return FALSE;
}

static GdkWindow *
gdk_mir_display_get_default_group (GdkDisplay *display)
{
  g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);

  return NULL;
}


static gboolean
gdk_mir_display_supports_selection_notification (GdkDisplay *display)
{
  return TRUE;
}

static gboolean
gdk_mir_display_request_selection_notification (GdkDisplay *display,
						    GdkAtom     selection)

{
    return FALSE;
}

static gboolean
gdk_mir_display_supports_clipboard_persistence (GdkDisplay *display)
{
  return FALSE;
}

static void
gdk_mir_display_store_clipboard (GdkDisplay    *display,
				     GdkWindow     *clipboard_window,
				     guint32        time_,
				     const GdkAtom *targets,
				     gint           n_targets)
{
}

static gboolean
gdk_mir_display_supports_shapes (GdkDisplay *display)
{
  return TRUE;
}

static gboolean
gdk_mir_display_supports_input_shapes (GdkDisplay *display)
{
  return TRUE;
}

static gboolean
gdk_mir_display_supports_composite (GdkDisplay *display)
{
  return TRUE;
}

static GList *
gdk_mir_display_list_devices (GdkDisplay *display)
{
  g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);

  return GDK_MIR_DISPLAY (display)->input_devices;
}

static void
gdk_mir_display_before_process_all_updates (GdkDisplay *display)
{
}

static void
gdk_mir_display_after_process_all_updates (GdkDisplay *display)
{
  /* Post the damage here instead? */
}

static gulong
gdk_mir_display_get_next_serial (GdkDisplay *display)
{
  static gulong serial = 0;
  return ++serial;
}

void
_gdk_mir_display_make_default (GdkDisplay *display)
{
}

/**
 * gdk_mir_display_broadcast_startup_message:
 * @display: a #GdkDisplay
 * @message_type: startup notification message type ("new", "change",
 * or "remove")
 * @...: a list of key/value pairs (as strings), terminated by a
 * %NULL key. (A %NULL value for a key will cause that key to be
 * skipped in the output.)
 *
 * Sends a startup notification message of type @message_type to
 * @display. 
 *
 * This is a convenience function for use by code that implements the
 * freedesktop startup notification specification. Applications should
 * not normally need to call it directly. See the <ulink
 * url="http://standards.freedesktop.org/startup-notification-spec/startup-notification-latest.txt">Startup
 * Notification Protocol specification</ulink> for
 * definitions of the message types and keys that can be used.
 *
 * Since: 2.12
 **/
void
gdk_mir_display_broadcast_startup_message (GdkDisplay *display,
					   const char *message_type,
					   ...)
{
  GString *message;
  va_list ap;
  const char *key, *value, *p;

  message = g_string_new (message_type);
  g_string_append_c (message, ':');

  va_start (ap, message_type);
  while ((key = va_arg (ap, const char *)))
    {
      value = va_arg (ap, const char *);
      if (!value)
	continue;

      g_string_append_printf (message, " %s=\"", key);
      for (p = value; *p; p++)
	{
	  switch (*p)
	    {
	    case ' ':
	    case '"':
	    case '\\':
	      g_string_append_c (message, '\\');
	      break;
	    }

	  g_string_append_c (message, *p);
	}
      g_string_append_c (message, '\"');
    }
  va_end (ap);

  printf ("startup message: %s\n", message->str);

  g_string_free (message, TRUE);
}

static void
gdk_mir_display_notify_startup_complete (GdkDisplay  *display,
                                         const gchar *startup_id)
{
  gdk_mir_display_broadcast_startup_message (display, "remove",
						 "ID", startup_id,
						 NULL);
}

static void
gdk_mir_display_event_data_copy (GdkDisplay     *display,
				 const GdkEvent *src,
				 GdkEvent       *dst)
{
}

static void
gdk_mir_display_event_data_free (GdkDisplay *display,
				 GdkEvent   *event)
{
}

GdkKeymap *
_gdk_mir_display_get_keymap (GdkDisplay *display)
{
  return _gdk_mir_keymap_new ();
}

static void
gdk_mir_display_push_error_trap (GdkDisplay *display)
{
}

static gint
gdk_mir_display_pop_error_trap (GdkDisplay *display,
                                gboolean    ignored)
{
  return 0;
}

static void
gdk_mir_display_deliver_event (GdkDisplay *display, GdkEvent *event)
{
  GList *node;

  node = _gdk_event_queue_append (display, event);
  _gdk_windowing_got_event (display, node, event,
                            _gdk_display_get_next_serial (display));
}

void
synthesize_leave_event_from_enter_event (GdkDisplay *display,
                                         GdkEvent   *event)
{
  GdkDevice *device = gdk_event_get_device (event);
  GdkPointerWindowInfo *info =
    _gdk_display_get_pointer_info (display,
                                   device);

  /* This function should only be called with enter events */
  g_return_if_fail (event->type == GDK_ENTER_NOTIFY);

  if (info->toplevel_under_pointer &&
      info->toplevel_under_pointer != event->crossing.window)
    {
      GdkEvent *leave_event = gdk_event_new (GDK_LEAVE_NOTIFY);
      leave_event->crossing.window = g_object_ref (info->toplevel_under_pointer);
      leave_event->crossing.x = info->toplevel_x;
      leave_event->crossing.y = info->toplevel_y;
      leave_event->crossing.time = (guint32) (g_get_monotonic_time () / 1000);
      leave_event->crossing.mode = GDK_CROSSING_NORMAL;
      leave_event->crossing.detail = GDK_NOTIFY_ANCESTOR;
      leave_event->crossing.focus = TRUE;
      leave_event->crossing.state = 0;

      gdk_event_set_device (leave_event,
                            device);

      /* Queue the synthetic leave event */
      gdk_mir_display_process_event_internal (display, leave_event);
      gdk_mir_display_deliver_event (display, leave_event);
    }
}

static void
adjust_state_for_enter_event (GdkDisplay *display,
                              GdkEvent   *event)
{
  /* Because we can't reliably separate the two types hover_exit events
   * that Mir sends us we need to synthesize a leave event here for
   * the currently hovered window
   *
   * Explanation: Mir's hover_enter and hover_exit events map more closely
   * to the concept of touch "hovering", eg, hovering over an object
   * but not yet "pressing" on it. They are normally sent when the input
   * device enters and leaves the geometric area of a surface. But
   * They are also sent on button_down and button_up when it could be said
   * that we have stopped, logically "hovering" the surface from a touch
   * perspective. As such, they cannot be treated as a GDK_LEAVE_NOTIFY or
   * GDK_ENTER_NOTIFY.
   *
   * TODO: This should be removed once Mir gets a concept of geometric leave
   * and enter.
   */
  synthesize_leave_event_from_enter_event (display, event);
  _gdk_mir_window_add_focus (event->crossing.window);
}

static void
adjust_state_for_leave_event (GdkDisplay *display,
                              GdkEvent   *event)
{
  _gdk_mir_window_remove_focus (event->crossing.window);
}

static void
gdk_mir_display_process_event_internal (GdkDisplay *display,
                                        GdkEvent   *event)
{
  switch (event->type)
    {
    case GDK_ENTER_NOTIFY:
      adjust_state_for_enter_event (display, event);
      break;
    case GDK_LEAVE_NOTIFY:
      adjust_state_for_leave_event (display, event);
      break;
    default:
      break;
    }
}

static void
gdk_mir_display_queue_events (GdkDisplay *display)
{
  GdkMirDisplay *display_mir = GDK_MIR_DISPLAY (display);

  /* Deliver the first event */
  GdkEvent *event = g_queue_pop_tail (display_mir->event_queue);

  while (event)
    {
      /* _gdk_windowing_got_event may free and unlink the event, so we
       * need to do any state adjustments here first */
      gdk_mir_display_process_event_internal (display, event);
      gdk_mir_display_deliver_event (display, event);

      event = (GdkEvent *) g_queue_pop_tail (display_mir->event_queue);
    }
}

static void
_gdk_mir_display_class_init (GdkMirDisplayClass * class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GdkDisplayClass *display_class = GDK_DISPLAY_CLASS (class);

  object_class->dispose = gdk_mir_display_dispose;
  object_class->finalize = gdk_mir_display_finalize;

  display_class->window_type = _gdk_mir_window_get_type ();
  display_class->get_name = gdk_mir_display_get_name;
  display_class->get_default_screen = gdk_mir_display_get_default_screen;
  display_class->beep = gdk_mir_display_beep;
  display_class->sync = gdk_mir_display_sync;
  display_class->flush = gdk_mir_display_flush;
  display_class->has_pending = gdk_mir_display_has_pending;
  display_class->get_default_group = gdk_mir_display_get_default_group;
  display_class->supports_selection_notification = gdk_mir_display_supports_selection_notification;
  display_class->request_selection_notification = gdk_mir_display_request_selection_notification;
  display_class->supports_clipboard_persistence = gdk_mir_display_supports_clipboard_persistence;
  display_class->store_clipboard = gdk_mir_display_store_clipboard;
  display_class->supports_shapes = gdk_mir_display_supports_shapes;
  display_class->supports_input_shapes = gdk_mir_display_supports_input_shapes;
  display_class->supports_composite = gdk_mir_display_supports_composite;
  display_class->list_devices = gdk_mir_display_list_devices;
  display_class->get_app_launch_context = _gdk_mir_display_get_app_launch_context;
  display_class->get_default_cursor_size = _gdk_mir_display_get_default_cursor_size;
  display_class->get_maximal_cursor_size = _gdk_mir_display_get_maximal_cursor_size;
  display_class->get_cursor_for_type = _gdk_mir_display_get_cursor_for_type;
  display_class->get_cursor_for_name = _gdk_mir_display_get_cursor_for_name;
  display_class->get_cursor_for_surface = _gdk_mir_display_get_cursor_for_surface;
  display_class->supports_cursor_alpha = _gdk_mir_display_supports_cursor_alpha;
  display_class->supports_cursor_color = _gdk_mir_display_supports_cursor_color;
  display_class->before_process_all_updates = gdk_mir_display_before_process_all_updates;
  display_class->after_process_all_updates = gdk_mir_display_after_process_all_updates;
  display_class->get_next_serial = gdk_mir_display_get_next_serial;
  display_class->notify_startup_complete = gdk_mir_display_notify_startup_complete;
  display_class->event_data_copy = gdk_mir_display_event_data_copy;
  display_class->event_data_free = gdk_mir_display_event_data_free;
  display_class->create_window_impl = _gdk_mir_display_create_window_impl;
  display_class->get_keymap = _gdk_mir_display_get_keymap;
  display_class->push_error_trap = gdk_mir_display_push_error_trap;
  display_class->pop_error_trap = gdk_mir_display_pop_error_trap;
  display_class->get_selection_owner = _gdk_mir_display_get_selection_owner;
  display_class->set_selection_owner = _gdk_mir_display_set_selection_owner;
  display_class->send_selection_notify = _gdk_mir_display_send_selection_notify;
  display_class->get_selection_property = _gdk_mir_display_get_selection_property;
  display_class->convert_selection = _gdk_mir_display_convert_selection;
  display_class->text_property_to_utf8_list = _gdk_mir_display_text_property_to_utf8_list;
  display_class->utf8_to_string_target = _gdk_mir_display_utf8_to_string_target;
  display_class->queue_events = gdk_mir_display_queue_events;
}

static void
_gdk_mir_display_init (GdkMirDisplay *display)
{
}

guint32
_gdk_mir_display_get_serial (GdkMirDisplay *mir_display)
{
  return mir_display->serial;
}

void
_gdk_mir_display_update_serial (GdkMirDisplay *mir_display,
                                guint32            serial)
{
  if (serial > mir_display->serial)
    mir_display->serial = serial;
}
