#pragma once

#include "atm-theme.h"

G_BEGIN_DECLS

typedef enum {
    ATM_DISCOVERY_CINNAMON,
    ATM_DISCOVERY_GTK,
    ATM_DISCOVERY_ICONS,
    ATM_DISCOVERY_CURSOR,
} AtmDiscoveryKind;

GPtrArray *atm_discovery_list(AtmDiscoveryKind kind);
gchar *atm_discovery_get_path(AtmDiscoveryKind kind, const gchar *name);
gboolean atm_discovery_has(AtmDiscoveryKind kind, const gchar *name);
gboolean atm_discovery_validate_theme(const AtmTheme *theme,
                                      AtmComponent components,
                                      GError **error);
gboolean atm_discovery_gtk_has_lock_style(const gchar *name);

G_END_DECLS
