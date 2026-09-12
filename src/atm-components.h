#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_COMPONENT_NONE       = 0,
    ATM_COMPONENT_WALLPAPER  = 1u << 0,
    ATM_COMPONENT_CINNAMON   = 1u << 1,
    ATM_COMPONENT_GTK        = 1u << 2,
    ATM_COMPONENT_ICONS      = 1u << 3,
    ATM_COMPONENT_CURSOR     = 1u << 4,
    ATM_COMPONENT_LOCK       = 1u << 5,
    ATM_COMPONENT_APPEARANCE = 1u << 6,
    ATM_COMPONENT_ALL        = (1u << 7) - 1,
} AtmComponent;

typedef struct {
    const gchar *number;
    const gchar *id;
    const gchar *title;
    const gchar *description;
    AtmComponent component;
    gboolean independently_applied;
} AtmComponentInfo;

const AtmComponentInfo *atm_component_map(gsize *length);
const AtmComponentInfo *atm_component_lookup(const gchar *selector);
gboolean atm_component_parse_list(const gchar *text,
                                  AtmComponent *components,
                                  GError **error);
gchar *atm_component_map_json(void);

G_END_DECLS
