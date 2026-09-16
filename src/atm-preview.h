#pragma once

#include <gtk/gtk.h>
#include "atm-discovery.h"

G_BEGIN_DECLS

GdkPixbuf *atm_preview_load(AtmDiscoveryKind kind,
                            const gchar *theme_name,
                            gint width,
                            gint height);
void atm_preview_cache_clear(void);

G_END_DECLS
