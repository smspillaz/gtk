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

#include "config.h"

#define GDK_PIXBUF_ENABLE_BACKEND

#include <string.h>

#include "gdkprivate-mir.h"
#include "gdkcursorprivate.h"
#include "gdkdisplay-mir.h"
#include "gdkmir.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

#define GDK_TYPE_MIR_CURSOR              (_gdk_mir_cursor_get_type ())
#define GDK_MIR_CURSOR(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_CURSOR, GdkMirCursor))
#define GDK_MIR_CURSOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_MIR_CURSOR, GdkMirCursorClass))
#define GDK_IS_MIR_CURSOR(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_CURSOR))
#define GDK_IS_MIR_CURSOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_MIR_CURSOR))
#define GDK_MIR_CURSOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_MIR_CURSOR, GdkMirCursorClass))

typedef struct _GdkMirCursor GdkMirCursor;
typedef struct _GdkMirCursorClass GdkMirCursorClass;

struct _GdkMirCursor
{
  GdkCursor cursor;
  gchar *name;
  guint serial;
  int hotspot_x, hotspot_y;
  int width, height;
};

struct _GdkMirCursorClass
{
  GdkCursorClass cursor_class;
};

G_DEFINE_TYPE (GdkMirCursor, _gdk_mir_cursor, GDK_TYPE_CURSOR)

static guint theme_serial = 0;

static void
gdk_mir_cursor_finalize (GObject *object)
{
  GdkMirCursor *cursor = GDK_MIR_CURSOR (object);

  g_free (cursor->name);

  G_OBJECT_CLASS (_gdk_mir_cursor_parent_class)->finalize (object);
}

static cairo_surface_t *
gdk_mir_cursor_get_surface (GdkCursor *cursor,
                            gdouble   *xhot,
                            gdouble   *yhot)
{
  /* Return a dummy surface */
  *xhot = 0;
  *yhot = 0;
  return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
}

static void
_gdk_mir_cursor_class_init (GdkMirCursorClass *mir_cursor_class)
{
  GdkCursorClass *cursor_class = GDK_CURSOR_CLASS (mir_cursor_class);
  GObjectClass *object_class = G_OBJECT_CLASS (mir_cursor_class);

  object_class->finalize = gdk_mir_cursor_finalize;

  cursor_class->get_surface = gdk_mir_cursor_get_surface;
}

static void
_gdk_mir_cursor_init (GdkMirCursor *cursor)
{
}

/* TODO: Extend this table */
static const struct {
  GdkCursorType type;
  const gchar *cursor_name;
} cursor_mapping[] = {
  { GDK_BLANK_CURSOR,          NULL   },
  { GDK_HAND1,                "hand1" },
  { GDK_HAND2,                "hand2" },
  { GDK_LEFT_PTR,             "left_ptr" },
  { GDK_SB_H_DOUBLE_ARROW,    "sb_h_double_arrow" },
  { GDK_SB_V_DOUBLE_ARROW,    "sb_v_double_arrow" },
  { GDK_XTERM,                "xterm" },
  { GDK_BOTTOM_RIGHT_CORNER,  "bottom_right_corner" }
};

GdkCursor *
_gdk_mir_display_get_cursor_for_type (GdkDisplay    *display,
                                      GdkCursorType  cursor_type)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (cursor_mapping); i++)
    {
      if (cursor_mapping[i].type == cursor_type)
	break;
    }

  if (i == G_N_ELEMENTS (cursor_mapping))
    {
      g_warning ("Unhandled cursor type %d, falling back to blank\n",
                 cursor_type);
      i = 0;
    }

  return _gdk_mir_display_get_cursor_for_name (display,
                                               cursor_mapping[i].cursor_name);
}

GdkCursor *
_gdk_mir_display_get_cursor_for_name (GdkDisplay  *display,
                                     const gchar *name)
{
  GdkMirCursor *private;

  g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);

  private = g_object_new (GDK_TYPE_MIR_CURSOR,
                          "cursor-type", GDK_CURSOR_IS_PIXMAP,
                          "display", display,
                          NULL);
  private->name = g_strdup (name);
  private->serial = theme_serial;

  /* Blank cursor case */
  if (!name)
    return GDK_CURSOR (private);

  private->hotspot_x = 0;
  private->hotspot_y = 0;
  private->width = 48;
  private->height = 48;

  return GDK_CURSOR (private);
}

/* TODO: Needs implementing */
GdkCursor *
_gdk_mir_display_get_cursor_for_surface (GdkDisplay      *display,
					 cairo_surface_t *surface,
					 gdouble         x,
					 gdouble         y)
{
  GdkMirCursor *private;

  g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);
  g_return_val_if_fail (surface != NULL, NULL);

  double x1, y1, x2, y2;
  cairo_t *cr = cairo_create (surface);
  cairo_clip_extents (cr, &x1, &y1, &x2, &y2);
  cairo_destroy (cr);

  g_return_val_if_fail (0 <= x && x < x2 - x1, NULL);
  g_return_val_if_fail (0 <= y && y < y2 - y1, NULL);

  private = g_object_new (GDK_TYPE_MIR_CURSOR,
                          "cursor-type", GDK_CURSOR_IS_PIXMAP,
                          "display", display,
                          NULL);

  private->name = NULL;
  private->serial = theme_serial;

  return GDK_CURSOR (private);
}

void
_gdk_mir_display_get_default_cursor_size (GdkDisplay *display,
					  guint       *width,
					  guint       *height)
{
  *width = 32;
  *height = 32;
}

void
_gdk_mir_display_get_maximal_cursor_size (GdkDisplay *display,
					  guint       *width,
					  guint       *height)
{
  *width = 256;
  *height = 256;
}

gboolean
_gdk_mir_display_supports_cursor_alpha (GdkDisplay *display)
{
  return TRUE;
}

gboolean
_gdk_mir_display_supports_cursor_color (GdkDisplay *display)
{
  return TRUE;
}
