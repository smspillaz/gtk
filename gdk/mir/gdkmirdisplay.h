/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2013 Jan Arne Petersen
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

#ifndef __GDK_MIR_DISPLAY_H__
#define __GDK_MIR_DISPLAY_H__

#if !defined (__GDKMIR_H_INSIDE__) && !defined (GDK_COMPILATION)
#error "Only <gdk/gdkmir.h> can be included directly."
#endif

#include <gdk/gdk.h>

#include <mir_toolkit/mir_client_library.h>

G_BEGIN_DECLS

#ifdef GDK_COMPILATION
typedef struct _GdkMirDisplay GdkMirDisplay;
#else
typedef GdkDisplay GdkMirDisplay;
#endif
typedef struct _GdkMirDisplayClass GdkMirDisplayClass;

#define GDK_TYPE_MIR_DISPLAY              (_gdk_mir_display_get_type())
#define GDK_MIR_DISPLAY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_DISPLAY, GdkMirDisplay))
#define GDK_MIR_DISPLAY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_MIR_DISPLAY, GdkMirDisplayClass))
#define GDK_IS_MIR_DISPLAY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_DISPLAY))
#define GDK_IS_MIR_DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_MIR_DISPLAY))
#define GDK_MIR_DISPLAY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_MIR_DISPLAY, GdkMirDisplayClass))

GDK_AVAILABLE_IN_ALL
GType                   gdk_mir_display_get_type            (void);

GDK_AVAILABLE_IN_ALL
MirConnection          *gdk_mir_display_get_connection      (GdkDisplay *display);
GDK_AVAILABLE_IN_3_10
void                    gdk_mir_display_set_cursor_theme    (GdkDisplay  *display,
                                                             const gchar *theme,
                                                             gint         size);

G_END_DECLS

#endif /* __GDK_MIR_DISPLAY_H__ */
