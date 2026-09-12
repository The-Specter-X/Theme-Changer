#pragma once

#include "atm-theme.h"

G_BEGIN_DECLS

#define ATM_STORE_ERROR (atm_store_error_quark())

typedef enum {
    ATM_STORE_ERROR_IO,
    ATM_STORE_ERROR_UNSAFE_CONTENT,
    ATM_STORE_ERROR_ALREADY_EXISTS,
    ATM_STORE_ERROR_CONFLICT,
} AtmStoreError;

GQuark atm_store_error_quark(void);
gchar *atm_store_get_user_theme_dir(void);
gchar *atm_store_get_system_theme_dir(void);
GPtrArray *atm_store_list(GError **error);
AtmTheme *atm_store_find(const gchar *id, GError **error);
AtmTheme *atm_store_import(const gchar *source_directory, GError **error);
gboolean atm_store_expose_components(const AtmTheme *theme,
                                     AtmComponent components,
                                     GError **error);

G_END_DECLS
