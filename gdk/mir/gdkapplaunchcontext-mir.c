/*
 * Copyright © 2010 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <gio/gdesktopappinfo.h>

#include "gdkmir.h"
#include "gdkprivate-mir.h"
#include "gdkapplaunchcontextprivate.h"
#include "gdkscreen.h"
#include "gdkinternals.h"
#include "gdkintl.h"

static char *
gdk_mir_app_launch_context_get_startup_notify_id (GAppLaunchContext *context,
						  GAppInfo          *info,
						  GList             *files)
{
  return NULL;
}

static void
gdk_mir_app_launch_context_launch_failed (GAppLaunchContext *context, 
                                          const char        *startup_notify_id)
{
}

typedef struct _GdkMirAppLaunchContext GdkMirAppLaunchContext;
typedef struct _GdkMirAppLaunchContextClass GdkMirAppLaunchContextClass;

struct _GdkMirAppLaunchContext
{
  GdkAppLaunchContext base;
  gchar *name;
  guint serial;
};

struct _GdkMirAppLaunchContextClass
{
  GdkAppLaunchContextClass base_class;
};

G_DEFINE_TYPE (GdkMirAppLaunchContext, gdk_mir_app_launch_context, GDK_TYPE_APP_LAUNCH_CONTEXT)

static void
gdk_mir_app_launch_context_class_init (GdkMirAppLaunchContextClass *klass)
{
  GAppLaunchContextClass *ctx_class = G_APP_LAUNCH_CONTEXT_CLASS (klass);

  ctx_class->get_startup_notify_id = gdk_mir_app_launch_context_get_startup_notify_id;
  ctx_class->launch_failed = gdk_mir_app_launch_context_launch_failed;
}

static void
gdk_mir_app_launch_context_init (GdkMirAppLaunchContext *ctx)
{
}

GdkAppLaunchContext *
_gdk_mir_display_get_app_launch_context (GdkDisplay *display)
{
  GdkAppLaunchContext *ctx;

  ctx = g_object_new (gdk_mir_app_launch_context_get_type (),
                      "display", display,
                      NULL);

  return ctx;
}
