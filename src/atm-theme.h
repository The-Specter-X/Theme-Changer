#pragma once

#include <gio/gio.h>
#include "atm-components.h"

G_BEGIN_DECLS

#define ATM_THEME_ERROR (atm_theme_error_quark())

typedef enum {
    ATM_THEME_ERROR_INVALID,
    ATM_THEME_ERROR_UNSAFE_PATH,
    ATM_THEME_ERROR_MISSING_ASSET,
    ATM_THEME_ERROR_UNSUPPORTED,
} AtmThemeError;

typedef struct {
    gchar *directory;
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *author;
    gchar *version;
    gchar *preview;

    gchar *wallpaper;
    gchar *cinnamon_theme;
    gchar *cinnamon_path;
    gchar *gtk_theme;
    gchar *gtk_path;
    gchar *icon_theme;
    gchar *icon_path;
    gchar *cursor_theme;
    gchar *cursor_path;
    gint cursor_size;
    gboolean screen_lock_styled;
    gchar *lock_css;
    gchar *color_scheme;
    gchar *accent_rgb;
} AtmTheme;

GQuark atm_theme_error_quark(void);
AtmTheme *atm_theme_load(const gchar *directory, GError **error);
gboolean atm_theme_validate(AtmTheme *theme, GError **error);
AtmComponent atm_theme_get_components(const AtmTheme *theme);
gchar *atm_theme_resolve_path(const AtmTheme *theme,
                              const gchar *relative_path,
                              GError **error);
gchar *atm_theme_to_json(const AtmTheme *theme);
void atm_theme_free(AtmTheme *theme);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AtmTheme, atm_theme_free)

G_END_DECLS
