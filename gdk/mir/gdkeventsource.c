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

#include <errno.h>
#include <string.h>

typedef struct _GdkMirEventSource {
  GSource source;
  GPollFD pfd;
  uint32_t mask;
  GdkDisplay *display;
} GdkMirEventSource;

static GList *event_sources = NULL;

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

static gboolean
gdk_event_source_check (GSource *base)
{
  GdkMirEventSource *source = (GdkMirEventSource *) base;

  return pipe_had_data (GDK_MIR_DISPLAY (source->display)->wakeup_pipe[0]);
}

static gboolean
gdk_event_source_dispatch(GSource *base,
			  GSourceFunc callback,
			  gpointer data)
{
  GdkMirEventSource *source = (GdkMirEventSource *) base;
  GdkDisplay *display = source->display;
  GdkEvent *event;

  gdk_threads_enter ();

  event = gdk_display_get_event (display);

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
  event_sources = g_list_remove (event_sources, source);
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
  GSource *source;
  GdkMirEventSource *mir_source;
  GdkMirDisplay *display_mir;
  char *name;

  source = g_source_new (&mir_glib_source_funcs,
			 sizeof (GdkMirEventSource));
  name = g_strdup_printf ("GDK Mir Event source (%s)", "display name");
  g_source_set_name (source, name);
  g_free (name);
  mir_source = (GdkMirEventSource *) source;

  display_mir = GDK_MIR_DISPLAY (display);
  mir_source->display = display;
  mir_source->pfd.fd = display_mir->wakeup_pipe[0];
  mir_source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
  g_source_add_poll(source, &mir_source->pfd);

  g_source_set_priority (source, GDK_PRIORITY_EVENTS);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  event_sources = g_list_prepend (event_sources, source);

  return source;
}
