/*
 * gdkdisplay-mir.h
 * 
 * Copyright 2001 Sun Microsystems Inc.
 * Copyright 2013 Deepin, Inc. 
 *
 * Erwann Chenede <erwann.chenede@sun.com>
 * Zhai Xiang <zhaixiang@linuxdeepin.com>
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

#ifndef __GDK_MIR_DISPLAY__
#define __GDK_MIR_DISPLAY__

#include <config.h>
#include <stdint.h>
#include <mir_toolkit/mir_client_library.h>

#include <glib.h>
#include <gdk/gdkkeys.h>
#include <gdk/gdkwindow.h>
#include <gdk/gdkinternals.h>
#include <gdk/gdk.h>		/* For gdk_get_program_class() */

#include "gdkdisplayprivate.h"

G_BEGIN_DECLS

typedef struct _GdkMirDisplay GdkMirDisplay;
typedef struct _GdkMirDisplayClass GdkMirDisplayClass;

struct _GdkMirDisplay
{
  GdkDisplay parent_instance;
  GdkScreen *screen;

  /* Keyboard related information */
  GdkKeymap *keymap;

  /* input GdkDevice list */
  GList *input_devices;

  /* Startup notification */
  gchar *startup_notification_id;

  /* Time of most recent user interaction and most recent serial */
  gulong user_time;
  guint32 serial;

  /* Mir fields below */
  MirConnection *mir_connection;
  MirDisplayInfo mir_display_info;

  GSource *event_source;

  /* Event queue - each window pushes its own events into the
   * queue in their own monitor threads created by mirclient.
   * event_queue_monitor is responsible for watching the
   * event_queue for incoming events and setting
   * first_pending_event as well as writing data to
   * the wakeup_pipe as a result.
   *
   * event_queue_monitor_messages is responsible for
   * telling the event_queue_monitor to start
   * watching the event queue again as we've entered
   * poll() or to stop monitoring alltogether as we've quit
   */
  GAsyncQueue *event_queue;
  GThread     *event_queue_monitor;
  int         wakeup_pipe[2];
};

struct _GdkMirDisplayClass
{
  GdkDisplayClass parent_class;
};

GType      _gdk_mir_display_get_type            (void);

G_END_DECLS

#endif				/* __GDK_MIR_DISPLAY__ */
