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

#include "config.h"

#include "gdkinternals.h"
#include "gdkprivate-mir.h"
#include "gdkmir.h"
#include "gdkmireventlistener.h"

#include <errno.h>
#include <string.h>

typedef struct _GdkMirEventSource {
  GSource source;
  uint32_t mask;
  GdkDisplay *display;
  GHashTable *fd_tags;
} GdkMirEventSource;

static ssize_t
read_all_data_from_pipe_into_null (int fd)
{
  char buffer[8];
  ssize_t read_bytes = 0;
  ssize_t result = 0;

  do
    {
      result = read (fd, buffer, 8);

      /* Return any error value immediately,
       * otherwise accumulate in read_bytes */
      if (result >= 0)
        read_bytes += result;
      else if (!read_bytes)
        return result;
    }
  while (result > 0);

  return read_bytes;
}

static gboolean
pipe_had_data (int fd)
{
  ssize_t read_bytes =
    read_all_data_from_pipe_into_null (fd);

  if (read_bytes == -1)
    {
      /* EWOULDBLOCK is fine, it just means we had no data to read
       * so return false but don't error out */
      switch (errno)
        {
        case EWOULDBLOCK:
          return FALSE;
        default:
          g_critical (strerror (errno));
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
gdk_event_source_prepare(GSource *base, gint *timeout)
{
  GdkMirEventSource *source = (GdkMirEventSource *) base;

  *timeout = -1;

  if (source->display->event_pause_count > 0)
    return FALSE;

  /* We have to add/remove the GPollFD if we want to update our
   * poll event mask dynamically.  Instead, let's just flush all
   * write on idle instead, which is what this amounts to. */

  if (_gdk_event_queue_find_first (source->display) != NULL)
    return TRUE;

  return FALSE;
}

static void
check_event_listeners_for_any_pending_data (gpointer key,
                                            gpointer value,
                                            gpointer user_data)
{
  gboolean *ready = user_data;

  /* We don't return immediately if ready is TRUE here as
   * we should still drain all of the pending listeners'
   * wakeup pipes as we will be dispatching any pending
   * events on them anyways */

  *ready |= pipe_had_data (GPOINTER_TO_INT (key));
}

static gboolean
gdk_event_source_check (GSource *base)
{
  GdkMirEventSource *source = (GdkMirEventSource *) base;
  GdkMirDisplay *mir_display = GDK_MIR_DISPLAY (source->display);

  gboolean ready = FALSE;

  g_hash_table_foreach (mir_display->event_listeners,
                        check_event_listeners_for_any_pending_data,
                        &ready);

  return ready;
}

void
dispatch_event_listeners (gpointer key,
			  gpointer value,
			  gpointer user_data)
{
  GdkMirEventListener *listener = (GdkMirEventListener *) value;
  gdk_mir_event_listener_dispatch_pending (listener);
}

static gboolean
gdk_event_source_dispatch(GSource *base,
			  GSourceFunc callback,
			  gpointer data)
{
  GdkMirEventSource *source = (GdkMirEventSource *) base;
  GdkMirDisplay *mir_display = GDK_MIR_DISPLAY (source->display);
  GdkEvent *event;

  /* Dispatch internal event listeners which may cause
   * events to be queued (but not emitted) */
  g_hash_table_foreach (mir_display->event_listeners,
                        dispatch_event_listeners,
                        NULL);

  /* Dispatch queued events */
  gdk_threads_enter ();

  event = gdk_display_get_event (source->display);

  if (event)
    {
      _gdk_event_emit (event);

      gdk_event_free (event);
    }

  gdk_threads_leave ();

  return TRUE;
}

static void
gdk_event_source_finalize (GSource *source)
{
}

static GSourceFuncs mir_glib_source_funcs = {
  gdk_event_source_prepare,
  gdk_event_source_check,
  gdk_event_source_dispatch,
  gdk_event_source_finalize
};

GSource *
_gdk_mir_display_event_source_new (GdkDisplay *display)
{
  GSource *source = g_source_new (&mir_glib_source_funcs,
                                  sizeof (GdkMirEventSource));
  GdkMirEventSource *mir_source = (GdkMirEventSource *) source;
  char *name = g_strdup_printf ("GDK Mir Event source (%s)", "display name");;

  g_source_set_name (source, name);
  g_free (name);

  mir_source->display = display;
  mir_source->fd_tags = g_hash_table_new (g_direct_hash, g_direct_equal);

  g_source_set_priority (source, GDK_PRIORITY_EVENTS);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return source;
}

void
_gdk_mir_display_register_event_stream_fd (gpointer            data,
					   GdkMirEventListener *listener,
					   int                 fd)
{
  GdkDisplay *display = (GdkDisplay *) data;
  GdkMirDisplay *mir_display = GDK_MIR_DISPLAY (display);
  GdkMirEventSource *mir_source = (GdkMirEventSource *) mir_display->event_source;

  g_return_if_fail (mir_display->event_source);

  g_hash_table_insert (mir_display->event_listeners,
                       GINT_TO_POINTER (fd),
                       listener);


  gpointer tag = g_source_add_unix_fd (mir_display->event_source,
                                       fd,
                                       G_IO_IN | G_IO_ERR | G_IO_HUP);

  g_hash_table_insert (mir_source->fd_tags, GINT_TO_POINTER (fd), tag);
}

void
_gdk_mir_display_unregister_event_stream_fd (gpointer            data,
					     GdkMirEventListener *listener,
					     int                 fd)
{
  GdkDisplay *display = (GdkDisplay *) data;
  GdkMirDisplay *mir_display = GDK_MIR_DISPLAY (display);
  GdkMirEventSource *mir_source = (GdkMirEventSource *) mir_display->event_source;

  g_hash_table_remove (mir_display->event_listeners,
                       GINT_TO_POINTER (fd));

  gpointer tag = g_hash_table_lookup (mir_source->fd_tags,
                                      GINT_TO_POINTER (fd));
  g_source_remove_unix_fd (mir_display->event_source,
                           tag);
  g_hash_table_remove (mir_source->fd_tags, GINT_TO_POINTER (fd));
}

void
_gdk_mir_display_dispatch_event_in_main_thread (GdkMirEventListener *listener,
						gpointer            queue_data,
						GDestroyNotify      queue_data_destroy,
						gpointer            user_data)
{
  GdkDisplay *display = (GdkDisplay *) user_data;
  GdkMirDisplay *mir_display = GDK_MIR_DISPLAY (display);

  g_queue_push_head (mir_display->event_queue, queue_data);
}
