#pragma once

#include <gtk/gtk.h>
#include "atm-discovery.h"

G_BEGIN_DECLS

GdkPixbuf *atm_preview_load(AtmDiscoveryKind kind,
                            const gchar *theme_name,
                            gint width,
                            gint height);

G_END_DECLS
