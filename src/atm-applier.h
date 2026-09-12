#pragma once

#include "atm-theme.h"

G_BEGIN_DECLS

#define ATM_APPLIER_ERROR (atm_applier_error_quark())

typedef enum {
    ATM_APPLIER_ERROR_SCHEMA,
    ATM_APPLIER_ERROR_APPLY,
    ATM_APPLIER_ERROR_NO_SNAPSHOT,
} AtmApplierError;

GQuark atm_applier_error_quark(void);
gboolean atm_applier_apply(const AtmTheme *theme,
                           AtmComponent components,
                           GError **error);
gboolean atm_applier_restore(GError **error);
gchar *atm_applier_get_current_theme_id(void);

G_END_DECLS
