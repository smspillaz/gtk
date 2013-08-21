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

#include <mir_toolkit/mir_client_library.h>

#include "gdkmireventlistener.h"

struct _GdkMirEventListener
{
  GObject parent;

  MirWaitHandle *wait_handle;

  gpointer                    notify_fd_user_data;
  GdkMirEventListenerFdNotify notify_fd_created;
  GdkMirEventListenerFdNotify notify_fd_destroyed;

  GAsyncQueue   *event_queue;
  GDestroyNotify event_queue_object_destroy_notify;
  int            event_queue_wakeup_pipe[2];

  GdkMirEventListenerDispatch dispatch;
  gpointer                    dispatch_user_data;
  GDestroyNotify              dispatch_destroy_notify;
};

struct _GdkMirEventListenerClass
{
  GObjectClass parent_class;
};

enum GdkMirEventListenerProperties
{
  PROP_0,
  PROP_DESTROY_NOTIFY_FUNC,
  PROP_FD_LISTENER_USER_DATA,
  PROP_FD_REGISTER_FUNC,
  PROP_FD_UNREGISTER_FUNC,
  PROP_DISPATCH_FUNC,
  PROP_DISPATCH_USER_DATA,
  PROP_DISPATCH_DESTROY_NOTIFY_FUNC,
  N_PROPERTIES
};

G_DEFINE_TYPE (GdkMirEventListener, gdk_mir_event_listener, G_TYPE_OBJECT)

static void
gdk_mir_event_listener_dispose(GObject *object)
{
  GdkMirEventListener *listener = GDK_MIR_EVENT_LISTENER (object);

  listener->notify_fd_destroyed (listener->notify_fd_user_data,
                                 listener,
                                 listener->event_queue_wakeup_pipe[0]);
  g_async_queue_unref (listener->event_queue);

  if (listener->dispatch_destroy_notify)
    listener->dispatch_destroy_notify (listener->dispatch_user_data);

  G_OBJECT_CLASS (gdk_mir_event_listener_parent_class)->dispose (object);
}

static void
gdk_mir_event_listener_finalize (GObject *object)
{
  GdkMirEventListener *listener = GDK_MIR_EVENT_LISTENER (object);

  close (listener->event_queue_wakeup_pipe[0]);
  close (listener->event_queue_wakeup_pipe[1]);

  G_OBJECT_CLASS (gdk_mir_event_listener_parent_class)->finalize (object);
}

static void
gdk_mir_event_listener_set_property (GObject      *object,
                                     guint        property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  GdkMirEventListener *listener = GDK_MIR_EVENT_LISTENER (object);

  switch (property_id)
    {
    case PROP_DESTROY_NOTIFY_FUNC:
      g_return_if_fail (!listener->event_queue_object_destroy_notify);
      listener->event_queue_object_destroy_notify =
        g_value_get_pointer (value);
      break;
    case PROP_FD_LISTENER_USER_DATA:
      g_return_if_fail (!listener->notify_fd_user_data);
      listener->notify_fd_user_data = g_value_get_pointer (value);
      break;
    case PROP_FD_REGISTER_FUNC:
      g_return_if_fail (!listener->notify_fd_created);
      listener->notify_fd_created = g_value_get_pointer (value);
      break;
    case PROP_FD_UNREGISTER_FUNC:
      g_return_if_fail (!listener->notify_fd_destroyed);
      listener->notify_fd_destroyed = g_value_get_pointer (value);
      break;
    case PROP_DISPATCH_FUNC:
      g_return_if_fail (!listener->dispatch);
      listener->dispatch = g_value_get_pointer (value);
      break;
    case PROP_DISPATCH_USER_DATA:
      g_return_if_fail (!listener->dispatch_user_data);
      listener->dispatch_user_data = g_value_get_pointer (value);
      break;
    case PROP_DISPATCH_DESTROY_NOTIFY_FUNC:
      g_return_if_fail (!listener->dispatch_destroy_notify);
      listener->dispatch_destroy_notify = g_value_get_pointer (value);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

static void
gdk_mir_event_listener_init (GdkMirEventListener *listener)
{
}

static void
gdk_mir_event_listener_constructed (GObject *object)
{
  GdkMirEventListener *listener = GDK_MIR_EVENT_LISTENER (object);
  g_return_if_fail (listener->notify_fd_created);
  g_return_if_fail (listener->notify_fd_destroyed);
  g_return_if_fail (listener->dispatch);

  if (pipe2(listener->event_queue_wakeup_pipe, O_CLOEXEC | O_NONBLOCK) == -1)
    g_critical (strerror (errno));

  if (listener->event_queue_object_destroy_notify)
    listener->event_queue =
      g_async_queue_new_full (listener->event_queue_object_destroy_notify);
  else
    listener->event_queue = g_async_queue_new ();
  listener->wait_handle = NULL;

  listener->notify_fd_created (listener->notify_fd_user_data,
                               listener,
                               listener->event_queue_wakeup_pipe[0]);
}

static GParamSpec *gdk_mir_event_listener_class_properties[N_PROPERTIES];

static void
gdk_mir_event_listener_class_init (GdkMirEventListenerClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = gdk_mir_event_listener_dispose;
  object_class->finalize = gdk_mir_event_listener_finalize;
  object_class->set_property = gdk_mir_event_listener_set_property;
  object_class->constructed = gdk_mir_event_listener_constructed;

  gdk_mir_event_listener_class_properties[PROP_DESTROY_NOTIFY_FUNC] =
    g_param_spec_pointer ("destroy-notify",
                          "Queue object destroy function",
                          "A GDestroyNotify for each remaining object "
                          "queue which has not been flushed by the time "
                          "this object is finalized",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  gdk_mir_event_listener_class_properties[PROP_FD_LISTENER_USER_DATA] =
    g_param_spec_pointer ("fd-listener",
                          "File descriptor registration callback user data",
                          "User data for GdkMirEventListenerFdNotify callback",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  gdk_mir_event_listener_class_properties[PROP_FD_REGISTER_FUNC] =
    g_param_spec_pointer ("fd-register",
                          "File descriptor registration callback",
                          "A GdkMirEventListenerFdNotify to be called "
                          "with the fd to poll on when events are ready "
                          "in this listener",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  gdk_mir_event_listener_class_properties[PROP_FD_UNREGISTER_FUNC] =
    g_param_spec_pointer ("fd-unregister",
                          "File descriptor unregistration callback",
                          "A GdkMirEventListenerFdNotify to be called "
                          "when this fd should be removed from the "
                          "list of fd's to poll",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  gdk_mir_event_listener_class_properties[PROP_DISPATCH_FUNC] =
    g_param_spec_pointer ("dispatch-func",
                          "Queue dispatch func",
                          "A GdkMirEventDispatch to be called upon events "
                          "being dispatched from gdk_mir_event_listener_dispatch_pending ",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  gdk_mir_event_listener_class_properties[PROP_DISPATCH_USER_DATA] =
    g_param_spec_pointer ("dispatch-user-data",
                          "Queue dispatch user data",
                          "User data to be provided upon queue dispatch",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  gdk_mir_event_listener_class_properties[PROP_DISPATCH_DESTROY_NOTIFY_FUNC] =
    g_param_spec_pointer ("dispatch-destroy-notify",
                          "Queue dispatch user data destroy function",
                          "A GDestroyNotify for dispatch queue user data",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     gdk_mir_event_listener_class_properties);
}

void
gdk_mir_event_listener_listen_to_event_stream (GdkMirEventListener *listener,
                                               MirWaitHandle       *request_handle)
{
  g_atomic_pointer_set (&listener->wait_handle,
                        request_handle);
}

/* Waits for all requests on this event stream to complete */
void
gdk_mir_event_listener_wait_for_mir_event_stream_completion (GdkMirEventListener *listener)
{
  mir_wait_for (g_atomic_pointer_get (&listener->wait_handle));
}

/* Waits for the next request on this event stream to complete. This does
 * not necessarily mean the most recent request, just waits until a request
 * of some sort has completed */
void
gdk_mir_event_listener_wait_for_one_mir_request_completion (GdkMirEventListener *listener)
{
  mir_wait_for_one (g_atomic_pointer_get (&listener->wait_handle));
}

/* May do nothing if there are no events pending */
void gdk_mir_event_listener_dispatch_pending (GdkMirEventListener *listener)
{
  gpointer data = NULL;

  do
    {
      data = g_async_queue_try_pop (listener->event_queue);
      if (data)
        listener->dispatch (listener,
                            data,
                            listener->event_queue_object_destroy_notify,
                            listener->dispatch_user_data);
    }
  while (data);
}

void
gdk_mir_event_listener_push (GdkMirEventListener *listener,
                             gpointer            data)
{
  g_async_queue_push (listener->event_queue, data);
  if (write (listener->event_queue_wakeup_pipe[1],
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

GdkMirEventListener *
gdk_mir_event_listener_new (GDestroyNotify object_destroy,
                            gpointer fd_listener_data,
                            GdkMirEventListenerFdNotify fd_created_notify,
                            GdkMirEventListenerFdNotify fd_destroyed_notify,
                            GdkMirEventListenerDispatch dispatch,
                            gpointer dispatch_user_data,
                            GDestroyNotify dispatch_destroy)
{
  return GDK_MIR_EVENT_LISTENER (g_object_new (GDK_TYPE_MIR_EVENT_LISTENER,
                                               "destroy-notify", object_destroy,
                                               "fd-listener", fd_listener_data,
                                               "fd-register", fd_created_notify,
                                               "fd-unregister", fd_destroyed_notify,
                                               "dispatch-func", dispatch,
                                               "dispatch-user-data", dispatch_user_data,
                                               "dispatch-destroy-notify", dispatch_destroy,
                                               NULL));
}
