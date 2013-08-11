/*
 * Copyright © 2010 Intel Corporation
 * Copyright (C) 2013 Deepin, Inc.
 *               2013 Zhai Xiang <zhaixiang@linuxdeepin.com>
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

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include "gdkscreenprivate.h"
#include "gdkvisualprivate.h"
#include "gdkdisplay.h"
#include "gdkdisplay-mir.h"
#include "gdkmir.h"
#include "gdkprivate-mir.h"

typedef struct _GdkMirScreen      GdkMirScreen;
typedef struct _GdkMirScreenClass GdkMirScreenClass;

#define GDK_TYPE_MIR_SCREEN              (_gdk_mir_screen_get_type ())
#define GDK_MIR_SCREEN(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_SCREEN, GdkMirScreen))
#define GDK_MIR_SCREEN_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TYPE_MIR_SCREEN, GdkMirScreenClass))
#define GDK_IS_MIR_SCREEN(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_SCREEN))
#define GDK_IS_MIR_SCREEN_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TYPE_MIR_SCREEN))
#define GDK_MIR_SCREEN_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TYPE_MIR_SCREEN, GdkMirScreenClass))

typedef struct _GdkMirMonitor GdkMirMonitor;

struct _GdkMirScreen
{
  GdkScreen parent_instance;

  GdkDisplay *display;
  GdkWindow *root_window;

  int width, height;
  int width_mm, height_mm;

  /* Visual Part */
  GdkVisual *visual;

  /* Xinerama/RandR 1.2 */
  gint		     n_monitors;
  GdkMirMonitor *monitors;
  gint               primary_monitor;
};

struct _GdkMirScreenClass
{
  GdkScreenClass parent_class;

  void (* window_manager_changed) (GdkMirScreen *screen_mir);
};

struct _GdkMirMonitor
{
  GdkRectangle  geometry;
  int		width_mm;
  int		height_mm;
  char *	output_name;
  char *	manufacturer;
};

G_DEFINE_TYPE (GdkMirScreen, _gdk_mir_screen, GDK_TYPE_SCREEN)

#define MM_PER_INCH 25
#define DEFAULT_DPI 96

static void
init_monitor_geometry (GdkMirMonitor *monitor,
		       int x, int y, int width, int height)
{
  monitor->geometry.x = x;
  monitor->geometry.y = y;
  monitor->geometry.width = width;
  monitor->geometry.height = height;

  monitor->width_mm = width/DEFAULT_DPI*MM_PER_INCH;
  monitor->height_mm = height/DEFAULT_DPI*MM_PER_INCH;
  monitor->output_name = NULL;
  monitor->manufacturer = NULL;
}

static void
free_monitors (GdkMirMonitor *monitors,
               gint           n_monitors)
{
  int i;

  for (i = 0; i < n_monitors; ++i)
    {
      g_free (monitors[i].output_name);
      g_free (monitors[i].manufacturer);
    }

  g_free (monitors);
}

static void
deinit_multihead (GdkScreen *screen)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (screen);

  free_monitors (screen_mir->monitors, screen_mir->n_monitors);

  screen_mir->n_monitors = 0;
  screen_mir->monitors = NULL;
}

static void
init_multihead (GdkScreen *screen)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (screen);

  /* No multihead support of any kind for this screen */
  screen_mir->n_monitors = 1;
  screen_mir->monitors = g_new0 (GdkMirMonitor, 1);
  screen_mir->primary_monitor = 0;

  init_monitor_geometry (screen_mir->monitors, 0, 0,
			 screen_mir->width, screen_mir->height);
}

static void
gdk_mir_screen_dispose (GObject *object)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (object);

  if (screen_mir->root_window)
    _gdk_window_destroy (screen_mir->root_window, TRUE);

  G_OBJECT_CLASS (_gdk_mir_screen_parent_class)->dispose (object);
}

static void
gdk_mir_screen_finalize (GObject *object)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (object);

  if (screen_mir->root_window)
    g_object_unref (screen_mir->root_window);

  g_object_unref (screen_mir->visual);

  deinit_multihead (GDK_SCREEN (object));

  G_OBJECT_CLASS (_gdk_mir_screen_parent_class)->finalize (object);
}

static GdkDisplay *
gdk_mir_screen_get_display (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->display;
}

static gint
gdk_mir_screen_get_width (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->width;
}

static gint
gdk_mir_screen_get_height (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->height;
}

static gint
gdk_mir_screen_get_width_mm (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->width_mm;
}

static gint
gdk_mir_screen_get_height_mm (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->height_mm;
}

static gint
gdk_mir_screen_get_number (GdkScreen *screen)
{
  return 0;
}

static GdkWindow *
gdk_mir_screen_get_root_window (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->root_window;
}

static gint
gdk_mir_screen_get_n_monitors (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->n_monitors;
}

static gint
gdk_mir_screen_get_primary_monitor (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->primary_monitor;
}

static gint
gdk_mir_screen_get_monitor_width_mm	(GdkScreen *screen,
					 gint       monitor_num)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (screen);

  return screen_mir->monitors[monitor_num].width_mm;
}

static gint
gdk_mir_screen_get_monitor_height_mm (GdkScreen *screen,
					  gint       monitor_num)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (screen);

  return screen_mir->monitors[monitor_num].height_mm;
}

static gchar *
gdk_mir_screen_get_monitor_plug_name (GdkScreen *screen,
					  gint       monitor_num)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (screen);

  return g_strdup (screen_mir->monitors[monitor_num].output_name);
}

static void
gdk_mir_screen_get_monitor_geometry (GdkScreen    *screen,
					 gint          monitor_num,
					 GdkRectangle *dest)
{
  GdkMirScreen *screen_mir = GDK_MIR_SCREEN (screen);

  if (dest)
    *dest = screen_mir->monitors[monitor_num].geometry;
}

static GdkVisual *
gdk_mir_screen_get_system_visual (GdkScreen * screen)
{
  return (GdkVisual *) GDK_MIR_SCREEN (screen)->visual;
}

static GdkVisual *
gdk_mir_screen_get_rgba_visual (GdkScreen *screen)
{
  return (GdkVisual *) GDK_MIR_SCREEN (screen)->visual;
}

static gboolean
gdk_mir_screen_is_composited (GdkScreen *screen)
{
  return TRUE;
}

static gchar *
gdk_mir_screen_make_display_name (GdkScreen *screen)
{
  return NULL;
}

static GdkWindow *
gdk_mir_screen_get_active_window (GdkScreen *screen)
{
  return NULL;
}

static GList *
gdk_mir_screen_get_window_stack (GdkScreen *screen)
{
  return NULL;
}

static void
gdk_mir_screen_broadcast_client_message (GdkScreen *screen,
					     GdkEvent  *event)
{
}

static gboolean
gdk_mir_screen_get_setting (GdkScreen   *screen,
				const gchar *name,
				GValue      *value)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);

  if (strcmp ("gtk-theme-name", name) == 0)
    {
      const gchar *s = "Adwaita";
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : %s\n", name, s));
      g_value_set_string (value, s);
      return TRUE;
    }
  else if (strcmp ("gtk-icon-theme-name", name) == 0)
    {
      const gchar *s = "gnome";
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : %s\n", name, s));
      g_value_set_string (value, s);
      return TRUE;
    }
  else if (strcmp ("gtk-double-click-time", name) == 0)
    {
      gint i = 250;
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : %d\n", name, i));
      g_value_set_int (value, i);
      return TRUE;
    }
  else if (strcmp ("gtk-double-click-distance", name) == 0)
    {
      gint i = 5;
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : %d\n", name, i));
      g_value_set_int (value, i);
      return TRUE;
    }
  else if (strcmp ("gtk-dnd-drag-threshold", name) == 0)
    {
      gint i = 8;
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : %d\n", name, i));
      g_value_set_int (value, i);
      return TRUE;
    }
  else if (strcmp ("gtk-split-cursor", name) == 0)
    {
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : FALSE\n", name));
      g_value_set_boolean (value, FALSE);
      return TRUE;
    }
  else if (strcmp ("gtk-alternative-button-order", name) == 0)
    {
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : TRUE\n", name));
      g_value_set_boolean (value, TRUE);
      return TRUE;
    }
  else if (strcmp ("gtk-alternative-sort-arrows", name) == 0)
    {
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : TRUE\n", name));
      g_value_set_boolean (value, TRUE);
      return TRUE;
    }
  else if (strcmp ("gtk-xft-dpi", name) == 0)
    {
      gint i = 96*1024;
      GDK_NOTE(MISC, g_print("gdk_screen_get_setting(\"%s\") : TRUE\n", name));
      g_value_set_int (value, i);
      return TRUE;
    }

  return FALSE;
}

typedef struct _GdkMirVisual	GdkMirVisual;
typedef struct _GdkMirVisualClass	GdkMirVisualClass;

struct _GdkMirVisual
{
  GdkVisual visual;
};

struct _GdkMirVisualClass
{
  GdkVisualClass parent_class;
};

G_DEFINE_TYPE (GdkMirVisual, _gdk_mir_visual, GDK_TYPE_VISUAL)

static void
_gdk_mir_visual_class_init (GdkMirVisualClass *klass)
{
}

static void
_gdk_mir_visual_init (GdkMirVisual *visual)
{
}

static gint
gdk_mir_screen_visual_get_best_depth (GdkScreen *screen)
{
  return 32;
}

static GdkVisualType
gdk_mir_screen_visual_get_best_type (GdkScreen *screen)
{
  return GDK_VISUAL_TRUE_COLOR;
}

static GdkVisual*
gdk_mir_screen_visual_get_best (GdkScreen *screen)
{
  return GDK_MIR_SCREEN (screen)->visual;
}

static GdkVisual*
gdk_mir_screen_visual_get_best_with_depth (GdkScreen *screen,
					       gint       depth)
{
  return GDK_MIR_SCREEN (screen)->visual;
}

static GdkVisual*
gdk_mir_screen_visual_get_best_with_type (GdkScreen     *screen,
					      GdkVisualType  visual_type)
{
  return GDK_MIR_SCREEN (screen)->visual;
}

static GdkVisual*
gdk_mir_screen_visual_get_best_with_both (GdkScreen     *screen,
					      gint           depth,
					      GdkVisualType  visual_type)
{
  return GDK_MIR_SCREEN (screen)->visual;
}

static void
gdk_mir_screen_query_depths  (GdkScreen  *screen,
				  gint      **depths,
				  gint       *count)
{
  static gint static_depths[] = { 32 };

  *count = G_N_ELEMENTS(static_depths);
  *depths = static_depths;
}

static void
gdk_mir_screen_query_visual_types (GdkScreen      *screen,
				       GdkVisualType **visual_types,
				       gint           *count)
{
  static GdkVisualType static_visual_types[] = { GDK_VISUAL_TRUE_COLOR };

  *count = G_N_ELEMENTS(static_visual_types);
  *visual_types = static_visual_types;
}

static GList *
gdk_mir_screen_list_visuals (GdkScreen *screen)
{
  GList *list;
  GdkMirScreen *screen_mir;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);
  screen_mir = GDK_MIR_SCREEN (screen);

  list = g_list_append (NULL, screen_mir->visual);

  return list;
}

#define GDK_TYPE_MIR_VISUAL              (_gdk_mir_visual_get_type ())
#define GDK_MIR_VISUAL(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_VISUAL, GdkMirVisual))

static GdkVisual *
gdk_mir_visual_new (GdkScreen *screen)
{
  GdkVisual *visual;

  visual = g_object_new (GDK_TYPE_MIR_VISUAL, NULL);
  visual->screen = GDK_SCREEN (screen);
  visual->type = GDK_VISUAL_TRUE_COLOR;
  visual->depth = 32;

  return visual;
}

GdkScreen *
_gdk_mir_screen_new(GdkDisplay *display)
{
  GdkScreen *screen;
  GdkMirScreen *screen_mir;

  screen = g_object_new (GDK_TYPE_MIR_SCREEN, NULL);

  screen_mir = GDK_MIR_SCREEN (screen);
  screen_mir->display = display;
  screen_mir->width = 8192;
  screen_mir->height = 8192;

  screen_mir->visual = gdk_mir_visual_new (screen);

  screen_mir->root_window =
    _gdk_mir_screen_create_root_window (screen,
					    screen_mir->width,
					    screen_mir->height);

  init_multihead (screen);

  return screen;
}

static void
_gdk_mir_screen_class_init (GdkMirScreenClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkScreenClass *screen_class = GDK_SCREEN_CLASS (klass);

  object_class->dispose = gdk_mir_screen_dispose;
  object_class->finalize = gdk_mir_screen_finalize;

  screen_class->get_display = gdk_mir_screen_get_display;
  screen_class->get_width = gdk_mir_screen_get_width;
  screen_class->get_height = gdk_mir_screen_get_height;
  screen_class->get_width_mm = gdk_mir_screen_get_width_mm;
  screen_class->get_height_mm = gdk_mir_screen_get_height_mm;
  screen_class->get_number = gdk_mir_screen_get_number;
  screen_class->get_root_window = gdk_mir_screen_get_root_window;
  screen_class->get_n_monitors = gdk_mir_screen_get_n_monitors;
  screen_class->get_primary_monitor = gdk_mir_screen_get_primary_monitor;
  screen_class->get_monitor_width_mm = gdk_mir_screen_get_monitor_width_mm;
  screen_class->get_monitor_height_mm = gdk_mir_screen_get_monitor_height_mm;
  screen_class->get_monitor_plug_name = gdk_mir_screen_get_monitor_plug_name;
  screen_class->get_monitor_geometry = gdk_mir_screen_get_monitor_geometry;
  screen_class->get_monitor_workarea = gdk_mir_screen_get_monitor_geometry;
  screen_class->get_system_visual = gdk_mir_screen_get_system_visual;
  screen_class->get_rgba_visual = gdk_mir_screen_get_rgba_visual;
  screen_class->is_composited = gdk_mir_screen_is_composited;
  screen_class->make_display_name = gdk_mir_screen_make_display_name;
  screen_class->get_active_window = gdk_mir_screen_get_active_window;
  screen_class->get_window_stack = gdk_mir_screen_get_window_stack;
  screen_class->broadcast_client_message = gdk_mir_screen_broadcast_client_message;
  screen_class->get_setting = gdk_mir_screen_get_setting;
  screen_class->visual_get_best_depth = gdk_mir_screen_visual_get_best_depth;
  screen_class->visual_get_best_type = gdk_mir_screen_visual_get_best_type;
  screen_class->visual_get_best = gdk_mir_screen_visual_get_best;
  screen_class->visual_get_best_with_depth = gdk_mir_screen_visual_get_best_with_depth;
  screen_class->visual_get_best_with_type = gdk_mir_screen_visual_get_best_with_type;
  screen_class->visual_get_best_with_both = gdk_mir_screen_visual_get_best_with_both;
  screen_class->query_depths = gdk_mir_screen_query_depths;
  screen_class->query_visual_types = gdk_mir_screen_query_visual_types;
  screen_class->list_visuals = gdk_mir_screen_list_visuals;
}

static void
_gdk_mir_screen_init (GdkMirScreen *screen_mir)
{
}
