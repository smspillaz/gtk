/*
 * Copyright © 2010 Intel Corporation
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

#include "gdkdndprivate.h"

#include "gdkmain.h"
#include "gdkinternals.h"
#include "gdkproperty.h"
#include "gdkprivate-mir.h"
#include "gdkdisplay-mir.h"

#include <string.h>

#define GDK_TYPE_MIR_DRAG_CONTEXT              (gdk_mir_drag_context_get_type ())
#define GDK_MIR_DRAG_CONTEXT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_DRAG_CONTEXT, GdkMirDragContext))
#define GDK_MIR_DRAG_CONTEXT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_MIR_DRAG_CONTEXT, GdkMirDragContextClass))
#define GDK_IS_MIR_DRAG_CONTEXT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_DRAG_CONTEXT))
#define GDK_IS_MIR_DRAG_CONTEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_MIR_DRAG_CONTEXT))
#define GDK_MIR_DRAG_CONTEXT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_MIR_DRAG_CONTEXT, GdkMirDragContextClass))

typedef struct _GdkMirDragContext GdkMirDragContext;
typedef struct _GdkMirDragContextClass GdkMirDragContextClass;

struct _GdkMirDragContext
{
  GdkDragContext context;
};

struct _GdkMirDragContextClass
{
  GdkDragContextClass parent_class;
};

static GList *contexts;

G_DEFINE_TYPE (GdkMirDragContext, gdk_mir_drag_context, GDK_TYPE_DRAG_CONTEXT)

static void
gdk_mir_drag_context_finalize (GObject *object)
{
  GdkDragContext *context = GDK_DRAG_CONTEXT (object);

  contexts = g_list_remove (contexts, context);

  G_OBJECT_CLASS (gdk_mir_drag_context_parent_class)->finalize (object);
}

static GdkWindow *
gdk_mir_drag_context_find_window (GdkDragContext  *context,
				  GdkWindow       *drag_window,
				  GdkScreen       *screen,
				  gint             x_root,
				  gint             y_root,
				  GdkDragProtocol *protocol)
{
  return NULL;
}

static gboolean
gdk_mir_drag_context_drag_motion (GdkDragContext *context,
				  GdkWindow      *dest_window,
				  GdkDragProtocol protocol,
				  gint            x_root,
				  gint            y_root,
				  GdkDragAction   suggested_action,
				  GdkDragAction   possible_actions,
				  guint32         time)
{
  return FALSE;
}

static void
gdk_mir_drag_context_drag_abort (GdkDragContext *context,
                                 guint32         time)
{
}

static void
gdk_mir_drag_context_drag_drop (GdkDragContext *context,
                                guint32         time)
{
}

/* Destination side */

static void
gdk_mir_drag_context_drag_status (GdkDragContext *context,
				  GdkDragAction   action,
				  guint32         time_)
{
}

static void
gdk_mir_drag_context_drop_reply (GdkDragContext *context,
				 gboolean        accepted,
				 guint32         time_)
{
}

static void
gdk_mir_drag_context_drop_finish (GdkDragContext *context,
				  gboolean        success,
				  guint32         time)
{
}

static gboolean
gdk_mir_drag_context_drop_status (GdkDragContext *context)
{
  return FALSE;
}

static GdkAtom
gdk_mir_drag_context_get_selection (GdkDragContext *context)
{
    return GDK_NONE;
}

static void
gdk_mir_drag_context_init (GdkMirDragContext *context)
{
  contexts = g_list_prepend (contexts, context);
}

static void
gdk_mir_drag_context_class_init (GdkMirDragContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkDragContextClass *context_class = GDK_DRAG_CONTEXT_CLASS (klass);

  object_class->finalize = gdk_mir_drag_context_finalize;

  context_class->find_window = gdk_mir_drag_context_find_window;
  context_class->drag_status = gdk_mir_drag_context_drag_status;
  context_class->drag_motion = gdk_mir_drag_context_drag_motion;
  context_class->drag_abort = gdk_mir_drag_context_drag_abort;
  context_class->drag_drop = gdk_mir_drag_context_drag_drop;
  context_class->drop_reply = gdk_mir_drag_context_drop_reply;
  context_class->drop_finish = gdk_mir_drag_context_drop_finish;
  context_class->drop_status = gdk_mir_drag_context_drop_status;
  context_class->get_selection = gdk_mir_drag_context_get_selection;
}

GdkDragProtocol
_gdk_mir_window_get_drag_protocol (GdkWindow *window, GdkWindow **target)
{
  return 0;
}

void
_gdk_mir_window_register_dnd (GdkWindow *window)
{
}

GdkDragContext *
_gdk_mir_window_drag_begin (GdkWindow *window,
			    GdkDevice *device,
			    GList     *targets)
{
  GdkDragContext *context;

  context = (GdkDragContext *) g_object_new (GDK_TYPE_MIR_DRAG_CONTEXT, NULL);

  gdk_drag_context_set_device (context, device);

  return context;
}
