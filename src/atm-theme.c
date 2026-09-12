#include "atm-theme.h"

#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

#define THEME_SCHEMA_VERSION 1

GQuark
atm_theme_error_quark(void)
{
    return g_quark_from_static_string("atm-theme-error");
}

static gchar *
key_file_optional_string(GKeyFile *key_file, const gchar *group, const gchar *key)
{
    g_autoptr(GError) error = NULL;
    gchar *value = g_key_file_get_string(key_file, group, key, &error);

    if (error != NULL)
        return NULL;
    if (value != NULL)
        g_strstrip(value);
    if (value != NULL && *value == '\0')
        g_clear_pointer(&value, g_free);
    return value;
}

static gboolean
is_safe_id(const gchar *id)
{
    const guchar *cursor;

    if (id == NULL || *id == '\0' || strlen(id) > 64)
        return FALSE;

    for (cursor = (const guchar *) id; *cursor != '\0'; cursor++) {
        if (!(g_ascii_islower(*cursor) || g_ascii_isdigit(*cursor) ||
              *cursor == '-' || *cursor == '_'))
            return FALSE;
    }
    return TRUE;
}

static gboolean
is_safe_theme_name(const gchar *name)
{
    const guchar *cursor;

    if (name == NULL || *name == '\0' || strlen(name) > 128)
        return FALSE;
    for (cursor = (const guchar *) name; *cursor != '\0'; cursor++) {
        if (g_ascii_iscntrl(*cursor) || *cursor == '/' || *cursor == '\\')
            return FALSE;
    }
    return TRUE;
}

static gboolean
is_safe_display_name(const gchar *name)
{
    const guchar *cursor;

    if (name == NULL || *name == '\0' || strlen(name) > 128)
        return FALSE;
    for (cursor = (const guchar *) name; *cursor != '\0'; cursor++) {
        if (g_ascii_iscntrl(*cursor))
            return FALSE;
    }
    return TRUE;
}

static gboolean
is_hex_color(const gchar *value)
{
    guint i;

    if (value == NULL || strlen(value) != 7 || value[0] != '#')
        return FALSE;
    for (i = 1; i < 7; i++) {
        if (!g_ascii_isxdigit(value[i]))
            return FALSE;
    }
    return TRUE;
}

AtmTheme *
atm_theme_load(const gchar *directory, GError **error)
{
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autofree gchar *manifest = NULL;
    g_autoptr(AtmTheme) theme = NULL;
    gint schema_version;

    g_return_val_if_fail(directory != NULL, NULL);

    manifest = g_build_filename(directory, "theme.ini", NULL);
    if (!g_key_file_load_from_file(key_file, manifest, G_KEY_FILE_NONE, error))
        return NULL;

    schema_version = g_key_file_get_integer(key_file, "Theme", "SchemaVersion", error);
    if (error != NULL && *error != NULL)
        return NULL;
    if (schema_version != THEME_SCHEMA_VERSION) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_UNSUPPORTED,
                    "Unsupported SchemaVersion %d (expected %d)",
                    schema_version,
                    THEME_SCHEMA_VERSION);
        return NULL;
    }

    theme = g_new0(AtmTheme, 1);
    theme->directory = g_canonicalize_filename(directory, NULL);
    theme->id = g_key_file_get_string(key_file, "Theme", "ID", error);
    if (theme->id == NULL)
        return NULL;
    theme->name = g_key_file_get_string(key_file, "Theme", "Name", error);
    if (theme->name == NULL)
        return NULL;
    g_strstrip(theme->id);
    g_strstrip(theme->name);

    theme->description = key_file_optional_string(key_file, "Theme", "Description");
    theme->author = key_file_optional_string(key_file, "Theme", "Author");
    theme->version = key_file_optional_string(key_file, "Theme", "Version");
    theme->preview = key_file_optional_string(key_file, "Theme", "Preview");

    theme->wallpaper = key_file_optional_string(key_file, "Components", "Wallpaper");
    theme->cinnamon_theme = key_file_optional_string(key_file, "Components", "CinnamonTheme");
    theme->cinnamon_path = key_file_optional_string(key_file, "Components", "CinnamonPath");
    theme->gtk_theme = key_file_optional_string(key_file, "Components", "GtkTheme");
    theme->gtk_path = key_file_optional_string(key_file, "Components", "GtkPath");
    theme->icon_theme = key_file_optional_string(key_file, "Components", "IconTheme");
    theme->icon_path = key_file_optional_string(key_file, "Components", "IconPath");
    theme->cursor_theme = key_file_optional_string(key_file, "Components", "CursorTheme");
    theme->cursor_path = key_file_optional_string(key_file, "Components", "CursorPath");
    theme->lock_css = key_file_optional_string(key_file, "Components", "LockCss");
    theme->color_scheme = key_file_optional_string(key_file, "Appearance", "ColorScheme");
    theme->accent_rgb = key_file_optional_string(key_file, "Appearance", "AccentRGB");

    theme->cursor_size = 24;
    if (g_key_file_has_key(key_file, "Components", "CursorSize", NULL)) {
        theme->cursor_size = g_key_file_get_integer(key_file, "Components", "CursorSize", error);
        if (error != NULL && *error != NULL)
            return NULL;
    }

    theme->screen_lock_styled = FALSE;
    if (g_key_file_has_key(key_file, "Components", "ScreenLockStyled", NULL)) {
        theme->screen_lock_styled = g_key_file_get_boolean(key_file,
                                                           "Components",
                                                           "ScreenLockStyled",
                                                           error);
        if (error != NULL && *error != NULL)
            return NULL;
    }

    if (!atm_theme_validate(theme, error))
        return NULL;
    return g_steal_pointer(&theme);
}

gchar *
atm_theme_resolve_path(const AtmTheme *theme,
                       const gchar *relative_path,
                       GError **error)
{
    g_autofree gchar *candidate = NULL;
    g_autofree gchar *prefix = NULL;

    g_return_val_if_fail(theme != NULL, NULL);

    if (relative_path == NULL || *relative_path == '\0' ||
        g_path_is_absolute(relative_path)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_UNSAFE_PATH,
                    "Theme asset path must be a non-empty relative path");
        return NULL;
    }

    candidate = g_canonicalize_filename(relative_path, theme->directory);
    prefix = g_strconcat(theme->directory, G_DIR_SEPARATOR_S, NULL);
    if (!g_str_has_prefix(candidate, prefix)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_UNSAFE_PATH,
                    "Theme asset path escapes the bundle: %s",
                    relative_path);
        return NULL;
    }
    return g_steal_pointer(&candidate);
}

static gboolean
validate_asset(const AtmTheme *theme,
               const gchar *relative_path,
               gboolean directory,
               GError **error)
{
    g_autofree gchar *path = NULL;
    GFileTest test;

    if (relative_path == NULL)
        return TRUE;

    path = atm_theme_resolve_path(theme, relative_path, error);
    if (path == NULL)
        return FALSE;
    if (g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_UNSAFE_PATH,
                    "Bundle assets may not be symbolic links: %s",
                    relative_path);
        return FALSE;
    }
    test = directory ? G_FILE_TEST_IS_DIR : G_FILE_TEST_IS_REGULAR;
    if (!g_file_test(path, test)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_MISSING_ASSET,
                    "Missing %s asset: %s",
                    directory ? "directory" : "file",
                    relative_path);
        return FALSE;
    }
    return TRUE;
}

static gboolean
validate_component_pair(const AtmTheme *theme,
                        const gchar *name,
                        const gchar *path,
                        const gchar *label,
                        GError **error)
{
    if (path != NULL && name == NULL) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "%sPath requires %sTheme",
                    label,
                    label);
        return FALSE;
    }
    if (name != NULL && !is_safe_theme_name(name)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "Unsafe %s theme name: %s",
                    label,
                    name);
        return FALSE;
    }
    return validate_asset(theme, path, TRUE, error);
}

static gboolean
validate_component_layout(const AtmTheme *theme,
                          const gchar *base,
                          const gchar *child,
                          gboolean directory,
                          GError **error)
{
    g_autofree gchar *relative = NULL;

    if (base == NULL)
        return TRUE;
    relative = g_build_filename(base, child, NULL);
    return validate_asset(theme, relative, directory, error);
}

gboolean
atm_theme_validate(AtmTheme *theme, GError **error)
{
    g_autofree gchar *lock_path = NULL;
    g_autofree gchar *lock_contents = NULL;

    g_return_val_if_fail(theme != NULL, FALSE);

    if (!is_safe_id(theme->id)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "Theme ID must use lowercase ASCII letters, digits, '-' or '_'");
        return FALSE;
    }
    if (!is_safe_display_name(theme->name)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "Theme Name is empty, too long or contains control characters");
        return FALSE;
    }
    if (!validate_asset(theme, theme->preview, FALSE, error) ||
        !validate_asset(theme, theme->wallpaper, FALSE, error) ||
        !validate_component_pair(theme, theme->cinnamon_theme, theme->cinnamon_path, "Cinnamon", error) ||
        !validate_component_pair(theme, theme->gtk_theme, theme->gtk_path, "Gtk", error) ||
        !validate_component_pair(theme, theme->icon_theme, theme->icon_path, "Icon", error) ||
        !validate_component_pair(theme, theme->cursor_theme, theme->cursor_path, "Cursor", error) ||
        !validate_component_layout(theme, theme->cinnamon_path,
                                   "cinnamon/cinnamon.css", FALSE, error) ||
        !validate_component_layout(theme, theme->gtk_path,
                                   "gtk-3.0/gtk.css", FALSE, error) ||
        !validate_component_layout(theme, theme->icon_path,
                                   "index.theme", FALSE, error) ||
        !validate_component_layout(theme, theme->cursor_path,
                                   "index.theme", FALSE, error) ||
        !validate_component_layout(theme, theme->cursor_path,
                                   "cursors", TRUE, error))
        return FALSE;

    if (theme->cursor_theme != NULL &&
        (theme->cursor_size < 16 || theme->cursor_size > 128)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "CursorSize must be between 16 and 128");
        return FALSE;
    }

    if (theme->color_scheme != NULL &&
        !g_str_equal(theme->color_scheme, "default") &&
        !g_str_equal(theme->color_scheme, "prefer-dark") &&
        !g_str_equal(theme->color_scheme, "prefer-light")) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "ColorScheme must be default, prefer-dark or prefer-light");
        return FALSE;
    }
    if (theme->accent_rgb != NULL && !is_hex_color(theme->accent_rgb)) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "AccentRGB must use #RRGGBB format");
        return FALSE;
    }

    if (theme->screen_lock_styled) {
        g_autofree gchar *gtk_path = NULL;
        g_autofree gchar *gtk_prefix = NULL;

        if (theme->gtk_theme == NULL || theme->gtk_path == NULL || theme->lock_css == NULL) {
            g_set_error(error,
                        ATM_THEME_ERROR,
                        ATM_THEME_ERROR_INVALID,
                        "ScreenLockStyled requires GtkTheme, GtkPath and LockCss");
            return FALSE;
        }
        if (!validate_asset(theme, theme->lock_css, FALSE, error))
            return FALSE;
        lock_path = atm_theme_resolve_path(theme, theme->lock_css, error);
        gtk_path = atm_theme_resolve_path(theme, theme->gtk_path, error);
        if (lock_path == NULL || gtk_path == NULL)
            return FALSE;
        gtk_prefix = g_strconcat(gtk_path, G_DIR_SEPARATOR_S, NULL);
        if (!g_str_has_prefix(lock_path, gtk_prefix)) {
            g_set_error(error,
                        ATM_THEME_ERROR,
                        ATM_THEME_ERROR_INVALID,
                        "LockCss must be inside GtkPath so Cinnamon Screensaver can load it");
            return FALSE;
        }
        if (!g_file_get_contents(lock_path, &lock_contents, NULL, error))
            return FALSE;
        if (lock_contents == NULL ||
            strstr(lock_contents, ".csstage") == NULL) {
            g_set_error(error,
                        ATM_THEME_ERROR,
                        ATM_THEME_ERROR_INVALID,
                        "LockCss must contain Cinnamon Screensaver .csstage selectors");
            return FALSE;
        }
    } else if (theme->lock_css != NULL) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "LockCss is present but ScreenLockStyled is false");
        return FALSE;
    }

    if (atm_theme_get_components(theme) == ATM_COMPONENT_NONE) {
        g_set_error(error,
                    ATM_THEME_ERROR,
                    ATM_THEME_ERROR_INVALID,
                    "Theme contains no supported components");
        return FALSE;
    }
    return TRUE;
}

AtmComponent
atm_theme_get_components(const AtmTheme *theme)
{
    AtmComponent components = ATM_COMPONENT_NONE;

    g_return_val_if_fail(theme != NULL, ATM_COMPONENT_NONE);

    if (theme->wallpaper != NULL)
        components |= ATM_COMPONENT_WALLPAPER;
    if (theme->cinnamon_theme != NULL)
        components |= ATM_COMPONENT_CINNAMON;
    if (theme->gtk_theme != NULL)
        components |= ATM_COMPONENT_GTK;
    if (theme->icon_theme != NULL)
        components |= ATM_COMPONENT_ICONS;
    if (theme->cursor_theme != NULL)
        components |= ATM_COMPONENT_CURSOR;
    if (theme->screen_lock_styled)
        components |= ATM_COMPONENT_LOCK;
    if (theme->color_scheme != NULL || theme->accent_rgb != NULL)
        components |= ATM_COMPONENT_APPEARANCE;
    return components;
}

static void
append_json_string(GString *output, const gchar *text)
{
    const guchar *cursor;

    if (text == NULL) {
        g_string_append(output, "null");
        return;
    }
    g_string_append_c(output, '"');
    for (cursor = (const guchar *) text; *cursor != '\0'; cursor++) {
        switch (*cursor) {
        case '"': g_string_append(output, "\\\""); break;
        case '\\': g_string_append(output, "\\\\"); break;
        case '\n': g_string_append(output, "\\n"); break;
        case '\r': g_string_append(output, "\\r"); break;
        case '\t': g_string_append(output, "\\t"); break;
        default:
            if (*cursor < 0x20)
                g_string_append_printf(output, "\\u%04x", (guint) *cursor);
            else
                g_string_append_c(output, (gchar) *cursor);
        }
    }
    g_string_append_c(output, '"');
}

gchar *
atm_theme_to_json(const AtmTheme *theme)
{
    g_autoptr(GString) output = g_string_new("{");
    AtmComponent components;

    g_return_val_if_fail(theme != NULL, NULL);
    components = atm_theme_get_components(theme);

#define APPEND_FIELD(field, value) \
    G_STMT_START { \
        if (output->len > 1) g_string_append_c(output, ','); \
        append_json_string(output, field); \
        g_string_append_c(output, ':'); \
        append_json_string(output, value); \
    } G_STMT_END

    APPEND_FIELD("id", theme->id);
    APPEND_FIELD("name", theme->name);
    APPEND_FIELD("description", theme->description);
    APPEND_FIELD("author", theme->author);
    APPEND_FIELD("version", theme->version);
    APPEND_FIELD("directory", theme->directory);
    APPEND_FIELD("wallpaper", theme->wallpaper);
    APPEND_FIELD("cinnamon_theme", theme->cinnamon_theme);
    APPEND_FIELD("gtk_theme", theme->gtk_theme);
    APPEND_FIELD("icon_theme", theme->icon_theme);
    APPEND_FIELD("cursor_theme", theme->cursor_theme);
    APPEND_FIELD("color_scheme", theme->color_scheme);
    APPEND_FIELD("accent_rgb", theme->accent_rgb);
    g_string_append_printf(output,
                           ",\"cursor_size\":%d,\"screen_lock_styled\":%s,\"component_mask\":%u}",
                           theme->cursor_size,
                           theme->screen_lock_styled ? "true" : "false",
                           (guint) components);
#undef APPEND_FIELD

    return g_string_free(g_steal_pointer(&output), FALSE);
}

void
atm_theme_free(AtmTheme *theme)
{
    if (theme == NULL)
        return;
    g_free(theme->directory);
    g_free(theme->id);
    g_free(theme->name);
    g_free(theme->description);
    g_free(theme->author);
    g_free(theme->version);
    g_free(theme->preview);
    g_free(theme->wallpaper);
    g_free(theme->cinnamon_theme);
    g_free(theme->cinnamon_path);
    g_free(theme->gtk_theme);
    g_free(theme->gtk_path);
    g_free(theme->icon_theme);
    g_free(theme->icon_path);
    g_free(theme->cursor_theme);
    g_free(theme->cursor_path);
    g_free(theme->lock_css);
    g_free(theme->color_scheme);
    g_free(theme->accent_rgb);
    g_free(theme);
}
