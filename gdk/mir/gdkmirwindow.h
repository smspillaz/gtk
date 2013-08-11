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

#ifndef __GDK_MIR_WINDOW_H__
#define __GDK_MIR_WINDOW_H__

#if !defined (__GDKMIR_H_INSIDE__) && !defined (GDK_COMPILATION)
#error "Only <gdk/gdkmir.h> can be included directly."
#endif

#include <gdk/gdk.h>

#include <mir_toolkit/mir_client_library.h>

G_BEGIN_DECLS

#ifdef GDK_COMPILATION
typedef struct _GdkMirWindow GdkMirWindow;
#else
typedef GdkWindow GdkMirWindow;
#endif
typedef struct _GdkMirWindowClass GdkMirWindowClass;

#define GDK_TYPE_MIR_WINDOW              (gdk_mir_window_get_type())
#define GDK_MIR_WINDOW(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_WINDOW, GdkMirWindow))
#define GDK_MIR_WINDOW_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_MIR_WINDOW, GdkMirWindowClass))
#define GDK_IS_MIR_WINDOW(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_WINDOW))
#define GDK_IS_MIR_WINDOW_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_MIR_WINDOW))
#define GDK_MIR_WINDOW_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_MIR_WINDOW, GdkMirWindowClass))

GDK_AVAILABLE_IN_ALL
GType                    gdk_mir_window_get_type             (void);

GDK_AVAILABLE_IN_ALL
MirSurface              *gdk_mir_window_get_mir_surface      (GdkWindow *window);
GDK_AVAILABLE_IN_ALL
void                     gdk_mir_window_set_use_custom_surface (GdkWindow *window);

G_END_DECLS

#endif /* __GDK_MIR_WINDOW_H__ */
