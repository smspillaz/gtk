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

#ifndef __GDK_MIR_DEVICE_H__
#define __GDK_MIR_DEVICE_H__

#if !defined (__GDKMIR_H_INSIDE__) && !defined (GDK_COMPILATION)
#error "Only <gdk/gdkmir.h> can be included directly."
#endif

#include <gdk/gdk.h>

#include <mir_toolkit/mir_client_library.h>

G_BEGIN_DECLS

#ifdef GDK_COMPILATION
typedef struct _GdkMirDevice GdkMirDevice;
#else
typedef GdkDevice GdkMirDevice;
#endif
typedef struct _GdkMirDeviceClass GdkMirDeviceClass;

#define GDK_TYPE_MIR_DEVICE         (gdk_mir_device_get_type ())
#define GDK_MIR_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDK_TYPE_MIR_DEVICE, GdkMirDevice))
#define GDK_MIR_DEVICE_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), GDK_TYPE_MIR_DEVICE, GdkMirDeviceClass))
#define GDK_IS_MIR_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDK_TYPE_MIR_DEVICE))
#define GDK_IS_MIR_DEVICE_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), GDK_TYPE_MIR_DEVICE))
#define GDK_MIR_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDK_TYPE_MIR_DEVICE, GdkMirDeviceClass))

GDK_AVAILABLE_IN_ALL
GType                gdk_mir_device_get_type            (void);


G_END_DECLS

#endif /* __GDK_MIR_DEVICE_H__ */
