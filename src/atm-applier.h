#pragma once

#include "atm-theme.h"

G_BEGIN_DECLS

#define ATM_APPLIER_ERROR (atm_applier_error_quark())

typedef enum {
    ATM_APPLIER_ERROR_SCHEMA,
    ATM_APPLIER_ERROR_APPLY,
    ATM_APPLIER_ERROR_NO_SNAPSHOT,
} AtmApplierError;

typedef struct {
    gchar *cinnamon_theme;
    gchar *gtk_theme;
    gchar *icon_theme;
    gchar *cursor_theme;
    gint cursor_size;
    gchar *wallpaper_path;
    gchar *color_scheme;
    gchar *accent_rgb;
} AtmAppearanceSettings;

GQuark atm_applier_error_quark(void);
gboolean atm_applier_apply(const AtmTheme *theme,
                           AtmComponent components,
                           GError **error);
gboolean atm_applier_apply_custom(const AtmAppearanceSettings *settings,
                                  GError **error);
AtmAppearanceSettings *atm_applier_read_current(GError **error);
void atm_appearance_settings_free(AtmAppearanceSettings *settings);
gboolean atm_applier_restore(GError **error);
gchar *atm_applier_get_current_theme_id(void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AtmAppearanceSettings,
                             atm_appearance_settings_free)

G_END_DECLS
