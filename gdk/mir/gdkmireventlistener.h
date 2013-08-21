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

#ifndef __GDK_MIR_EVENT_LISTENER_H__
#define __GDK_MIR_EVENT_LISTENER_H__

#include <glib.h>
#include <glib-object.h>

#if !defined (__GDKMIR_H_INSIDE__) && !defined (GDK_COMPILATION)
#error "Only <gdk/gdkmir.h> can be included directly."
#endif

typedef struct MirWaitHandle MirWaitHandle;
typedef struct _GAsyncQueue GAsyncQueue;

G_BEGIN_DECLS

typedef struct _GdkMirEventListener GdkMirEventListener;
typedef struct _GdkMirEventListenerClass GdkMirEventListenerClass;

#define GDK_TYPE_MIR_EVENT_LISTENER              (gdk_mir_event_listener_get_type())
#define GDK_MIR_EVENT_LISTENER(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_EVENT_LISTENER, GdkMirEventListener))
#define GDK_MIR_EVENT_LISTENER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_MIR_EVENT_LISTENER, GdkMirEventListenerClass))
#define GDK_IS_MIR_EVENT_LISTENER(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_EVENT_LISTENER))
#define GDK_IS_MIR_EVENT_LISTENER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_MIR_EVENT_LISTENER))
#define GDK_MIR_EVENT_LISTENER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_MIR_EVENT_LISTENER, GdkMirEventListenerClass))

GType                   gdk_mir_event_listener_get_type            (void);

typedef void (*GdkMirEventListenerDispatch) (GdkMirEventListener *listener,
                                             gpointer            queue_data,
                                             GDestroyNotify      queue_data_destroy,
                                             gpointer            user_data);
typedef void (*GdkMirEventListenerFdNotify) (gpointer            data,
                                             GdkMirEventListener *listener,
                                             int                 fd);

GdkMirEventListener * gdk_mir_event_listener_new (GDestroyNotify object_destroy,
                                                  gpointer fd_listener_data,
                                                  GdkMirEventListenerFdNotify fd_created_notify,
                                                  GdkMirEventListenerFdNotify fd_destroyed_notify,
                                                  GdkMirEventListenerDispatch dispatch,
                                                  gpointer dispatch_user_data,
                                                  GDestroyNotify dispatch_destroy);

void gdk_mir_event_listener_listen_to_event_stream (GdkMirEventListener *listener,
                                                    MirWaitHandle       *request_handle);

/* Waits for all requests on this event stream to complete */
void gdk_mir_event_listener_wait_for_mir_event_stream_completion (GdkMirEventListener *listener);

/* Waits for the next request on this event stream to complete. This does
 * not necessarily mean the most recent request, just waits until a request
 * of some sort has completed */
void gdk_mir_event_listener_wait_for_one_mir_request_completion (GdkMirEventListener *listener);

/* May do nothing if there are no events pending */
void gdk_mir_event_listener_dispatch_pending (GdkMirEventListener *listener);

void gdk_mir_event_listener_push (GdkMirEventListener *listener,
                                  gpointer            data);
G_END_DECLS

#endif /* __GDK_MIR_EVENT_LISTENER_H__ */
