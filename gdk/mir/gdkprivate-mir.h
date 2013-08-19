/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

/*
 * Private uninstalled header defining things local to Mir
 */

#ifndef __GDK_PRIVATE_MIR_H__
#define __GDK_PRIVATE_MIR_H__

#include <gdk/gdkcursor.h>
#include <gdk/gdkprivate.h>
#include <gdk/mir/gdkdisplay-mir.h>

#include "gdkinternals.h"

#include "config.h"

GType _gdk_mir_window_get_type    (void);
void _gdk_mir_window_add_focus    (GdkWindow *window);
void _gdk_mir_window_remove_focus (GdkWindow *window);
void _gdk_mir_window_set_device_grabbed (GdkWindow *window,
					 GdkDevice *device,
					 guint32   time);
void _gdk_mir_window_generate_focus_event (GdkWindow *window,
					   GdkWindow *last,
					   GdkDevice *device);
void _gdk_mir_window_frame_arrived_in_main_loop (GdkWindow *window);

GdkKeymap *_gdk_mir_keymap_new (void);
GdkCursor *_gdk_mir_display_get_cursor_for_type (GdkDisplay    *display,
						 GdkCursorType  cursor_type);
GdkCursor *_gdk_mir_display_get_cursor_for_name (GdkDisplay  *display,
						 const gchar *name);
GdkCursor *_gdk_mir_display_get_cursor_for_surface (GdkDisplay      *display,
						    cairo_surface_t *surface,
						    gdouble         x,
						    gdouble         y);
void       _gdk_mir_display_get_default_cursor_size (GdkDisplay *display,
						     guint       *width,
						     guint       *height);
void       _gdk_mir_display_get_maximal_cursor_size (GdkDisplay *display,
						     guint       *width,
						     guint       *height);
gboolean   _gdk_mir_display_supports_cursor_alpha (GdkDisplay *display);
gboolean   _gdk_mir_display_supports_cursor_color (GdkDisplay *display);

GdkDragProtocol _gdk_mir_window_get_drag_protocol (GdkWindow *window,
						       GdkWindow **target);

void            _gdk_mir_window_register_dnd (GdkWindow *window);
GdkDragContext *_gdk_mir_window_drag_begin (GdkWindow *window,
					    GdkDevice *device,
					    GList     *targets);

void _gdk_mir_display_create_window_impl (GdkDisplay    *display,
					  GdkWindow     *window,
					  GdkWindow     *real_parent,
					  GdkScreen     *screen,
					  GdkEventMask   event_mask,
					  GdkWindowAttr *attributes,
					  gint           attributes_mask);

GdkWindow *_gdk_mir_display_get_selection_owner (GdkDisplay *display,
						 GdkAtom     selection);
gboolean   _gdk_mir_display_set_selection_owner (GdkDisplay *display,
						 GdkWindow  *owner,
						 GdkAtom     selection,
						 guint32     time,
						 gboolean    send_event);
void       _gdk_mir_display_send_selection_notify (GdkDisplay *dispay,
						   GdkWindow        *requestor,
						   GdkAtom          selection,
						   GdkAtom          target,
						   GdkAtom          property,
						   guint32          time);
gint       _gdk_mir_display_get_selection_property (GdkDisplay  *display,
						    GdkWindow   *requestor,
						    guchar     **data,
						    GdkAtom     *ret_type,
						    gint        *ret_format);
void       _gdk_mir_display_convert_selection (GdkDisplay *display,
					       GdkWindow  *requestor,
					       GdkAtom     selection,
					       GdkAtom     target,
					       guint32     time);
gint        _gdk_mir_display_text_property_to_utf8_list (GdkDisplay    *display,
							 GdkAtom        encoding,
							 gint           format,
							 const guchar  *text,
							 gint           length,
							 gchar       ***list);
gchar *     _gdk_mir_display_utf8_to_string_target (GdkDisplay  *display,
						    const gchar *str);

GdkDeviceManager *_gdk_mir_device_manager_new (GdkDisplay *display);

GdkKeymap *_gdk_mir_device_get_keymap (GdkDevice *device);

GSource *_gdk_mir_display_event_source_new (GdkDisplay *display);

GdkAppLaunchContext *_gdk_mir_display_get_app_launch_context (GdkDisplay *display);

GdkDisplay *_gdk_mir_display_open (const gchar *display_name);
void        _gdk_mir_display_make_default (GdkDisplay *display);

GdkWindow *_gdk_mir_screen_create_root_window (GdkScreen *screen,
					       int width,
					       int height);

GdkScreen *_gdk_mir_screen_new (GdkDisplay *display);

/* Update devices from mir events */
void _gdk_mir_mouse_update_from_button_event (GdkDevice            *device,
					      GdkWindow            *window,
					      const MirMotionEvent *event,
					      MirMotionButton      *gained_buttons,
					      MirMotionButton      *lost_buttons);

void _gdk_mir_mouse_update_from_motion_event (GdkDevice            *device,
					      GdkWindow            *window,
					      const MirMotionEvent *event);

void _gdk_mir_mouse_update_from_crossing_event (GdkDevice            *device,
						GdkWindow            *window,
						const MirMotionEvent *event);

GdkWindow * _gdk_mir_device_get_hovered_window (GdkDevice *device);
GdkDevice * _gdk_mir_device_manager_get_core_keyboard (GdkDeviceManager *device);
#endif /* __GDK_PRIVATE_MIR_H__ */
