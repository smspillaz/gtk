/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2000 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/mman.h>

#include "gdk.h"
#include "gdkmir.h"

#include "gdkprivate-mir.h"
#include "gdkinternals.h"
#include "gdkkeysprivate.h"

#include <xkbcommon/xkbcommon.h>

typedef struct _GdkMirKeymap          GdkMirKeymap;
typedef struct _GdkMirKeymapClass     GdkMirKeymapClass;

struct _GdkMirKeymap
{
  GdkKeymap parent_instance;
};

struct _GdkMirKeymapClass
{
  GdkKeymapClass parent_class;
};

#define GDK_TYPE_MIR_KEYMAP          (_gdk_mir_keymap_get_type ())
#define GDK_MIR_KEYMAP(object)       (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_MIR_KEYMAP, GdkMirKeymap))
#define GDK_IS_MIR_KEYMAP(object)    (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_MIR_KEYMAP))

G_DEFINE_TYPE (GdkMirKeymap, _gdk_mir_keymap, GDK_TYPE_KEYMAP)

static void
gdk_mir_keymap_finalize (GObject *object)
{
  G_OBJECT_CLASS (_gdk_mir_keymap_parent_class)->finalize (object);
}

static PangoDirection
gdk_mir_keymap_get_direction (GdkKeymap *keymap)
{
    return PANGO_DIRECTION_NEUTRAL;
}

static gboolean
gdk_mir_keymap_have_bidi_layouts (GdkKeymap *keymap)
{
    return FALSE;
}

static gboolean
gdk_mir_keymap_get_caps_lock_state (GdkKeymap *keymap)
{
    return TRUE;
}

static gboolean
gdk_mir_keymap_get_num_lock_state (GdkKeymap *keymap)
{
    return TRUE;
}

static gboolean
gdk_mir_keymap_get_entries_for_keyval (GdkKeymap     *keymap,
				       guint          keyval,
				       GdkKeymapKey **keys,
				       gint          *n_keys)
{
  if (n_keys)
    *n_keys = 1;
  if (keys)
    {
      *keys = g_new0 (GdkKeymapKey, 1);
      (*keys)->keycode = keyval;
    }

  return TRUE;
}

static gboolean
gdk_mir_keymap_get_entries_for_keycode (GdkKeymap     *keymap,
					guint          hardware_keycode,
					GdkKeymapKey **keys,
					guint        **keyvals,
					gint          *n_entries)
{
 if (n_entries)
    *n_entries = 1;
  if (keys)
    {
      *keys = g_new0 (GdkKeymapKey, 1);
      (*keys)->keycode = hardware_keycode;
    }
  if (keyvals)
    {
      *keyvals = g_new0 (guint, 1);
      (*keyvals)[0] = hardware_keycode;
    }
  return TRUE;
}

static guint
gdk_mir_keymap_lookup_key (GdkKeymap          *keymap,
                           const GdkKeymapKey *key)
{
  return key->keycode;
}

static gboolean
gdk_mir_keymap_translate_keyboard_state (GdkKeymap       *keymap,
					 guint            hardware_keycode,
					 GdkModifierType  state,
					 gint             group,
					 guint           *keyval,
					 gint            *effective_group,
					 gint            *level,
					 GdkModifierType *consumed_modifiers)
{
  g_return_val_if_fail (keymap == NULL || GDK_IS_KEYMAP (keymap), FALSE);
  g_return_val_if_fail (group < 4, FALSE);

  if (keyval)
    *keyval = hardware_keycode;
  if (effective_group)
    *effective_group = 0;
  if (level)
    *level = 0;
  if (consumed_modifiers)
    *consumed_modifiers = 0;

  return TRUE;
}

static void
gdk_mir_keymap_add_virtual_modifiers (GdkKeymap       *keymap,
					  GdkModifierType *state)
{
  return;
}

static gboolean
gdk_mir_keymap_map_virtual_modifiers (GdkKeymap       *keymap,
					  GdkModifierType *state)
{
  return TRUE;
}

static void
_gdk_mir_keymap_class_init (GdkMirKeymapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkKeymapClass *keymap_class = GDK_KEYMAP_CLASS (klass);

  object_class->finalize = gdk_mir_keymap_finalize;

  keymap_class->get_direction = gdk_mir_keymap_get_direction;
  keymap_class->have_bidi_layouts = gdk_mir_keymap_have_bidi_layouts;
  keymap_class->get_caps_lock_state = gdk_mir_keymap_get_caps_lock_state;
  keymap_class->get_num_lock_state = gdk_mir_keymap_get_num_lock_state;
  keymap_class->get_entries_for_keyval = gdk_mir_keymap_get_entries_for_keyval;
  keymap_class->get_entries_for_keycode = gdk_mir_keymap_get_entries_for_keycode;
  keymap_class->lookup_key = gdk_mir_keymap_lookup_key;
  keymap_class->translate_keyboard_state = gdk_mir_keymap_translate_keyboard_state;
  keymap_class->add_virtual_modifiers = gdk_mir_keymap_add_virtual_modifiers;
  keymap_class->map_virtual_modifiers = gdk_mir_keymap_map_virtual_modifiers;
}

static void
_gdk_mir_keymap_init (GdkMirKeymap *keymap)
{
}

GdkKeymap *
_gdk_mir_keymap_new ()
{
  GdkMirKeymap *keymap;

  keymap = g_object_new (_gdk_mir_keymap_get_type(), NULL);

  return GDK_KEYMAP (keymap);
}