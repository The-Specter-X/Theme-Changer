#include "atm-applier.h"
#include "atm-store.h"
#include "atm-discovery.h"

#include <errno.h>
#include <glib/gstdio.h>

#define INTERFACE_SCHEMA  "org.cinnamon.desktop.interface"
#define CINNAMON_SCHEMA   "org.cinnamon.theme"
#define BACKGROUND_SCHEMA "org.cinnamon.desktop.background"
#define PORTAL_SCHEMA     "org.x.apps.portal"

GQuark
atm_applier_error_quark(void)
{
    return g_quark_from_static_string("atm-applier-error");
}

static gchar *
get_state_directory(void)
{
    const gchar *override = g_getenv("AXIONIS_THEME_STATE_DIR");
    const gchar *state_home;

    if (override != NULL && *override != '\0')
        return g_canonicalize_filename(override, NULL);
    state_home = g_getenv("XDG_STATE_HOME");
    if (state_home != NULL && *state_home != '\0')
        return g_build_filename(state_home, "axionis-theme-manager", NULL);
    return g_build_filename(g_get_home_dir(),
                            ".local",
                            "state",
                            "axionis-theme-manager",
                            NULL);
}

static GSettings *
settings_for(const gchar *schema_id,
             const gchar * const *keys,
             GError **error)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = NULL;
    guint i;

    if (source == NULL) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_SCHEMA,
                    "No GSettings schema source is available");
        return NULL;
    }
    schema = g_settings_schema_source_lookup(source, schema_id, TRUE);
    if (schema == NULL) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_SCHEMA,
                    "Required GSettings schema is missing: %s",
                    schema_id);
        return NULL;
    }
    for (i = 0; keys[i] != NULL; i++) {
        if (!g_settings_schema_has_key(schema, keys[i])) {
            g_set_error(error,
                        ATM_APPLIER_ERROR,
                        ATM_APPLIER_ERROR_SCHEMA,
                        "Required GSettings key is missing: %s.%s",
                        schema_id,
                        keys[i]);
            return NULL;
        }
    }
    return g_settings_new_full(schema, NULL, NULL);
}

static gboolean
set_string_checked(GSettings *settings,
                   const gchar *key,
                   const gchar *value,
                   GError **error)
{
    g_autofree gchar *current = g_settings_get_string(settings, key);

    if (g_strcmp0(current, value) == 0)
        return TRUE;
    if (!g_settings_is_writable(settings, key)) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_APPLY,
                    "Setting is locked by policy: %s",
                    key);
        return FALSE;
    }
    if (!g_settings_set_string(settings, key, value)) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_APPLY,
                    "Failed to set %s",
                    key);
        return FALSE;
    }
    return TRUE;
}

static gboolean
set_int_checked(GSettings *settings,
                const gchar *key,
                gint value,
                GError **error)
{
    if (g_settings_get_int(settings, key) == value)
        return TRUE;
    if (!g_settings_is_writable(settings, key)) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_APPLY,
                    "Setting is locked by policy: %s",
                    key);
        return FALSE;
    }
    if (!g_settings_set_int(settings, key, value)) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_APPLY,
                    "Failed to set %s",
                    key);
        return FALSE;
    }
    return TRUE;
}

static void
snapshot_string(GKeyFile *snapshot,
                const gchar *key,
                GSettings *settings,
                const gchar *settings_key)
{
    g_autofree gchar *value = g_settings_get_string(settings, settings_key);
    g_key_file_set_string(snapshot, "Settings", key, value);
}

static gboolean
save_snapshot(const AtmTheme *theme,
              AtmComponent components,
              GSettings *interface,
              GSettings *cinnamon,
              GSettings *background,
              GSettings *portal,
              GError **error)
{
    g_autoptr(GKeyFile) snapshot = g_key_file_new();
    g_autofree gchar *previous_theme = atm_applier_get_current_theme_id();
    g_autofree gchar *directory = get_state_directory();
    g_autofree gchar *path = g_build_filename(directory, "previous.ini", NULL);
    g_autofree gchar *contents = NULL;
    gsize length;

    g_key_file_set_string(snapshot, "Snapshot", "NextTheme", theme->id);
    g_key_file_set_uint64(snapshot, "Snapshot", "Components", (guint64) components);
    if (previous_theme != NULL && *previous_theme != '\0')
        g_key_file_set_string(snapshot, "Snapshot", "PreviousTheme", previous_theme);

    if ((components & ATM_COMPONENT_GTK) != 0)
        snapshot_string(snapshot, "GtkTheme", interface, "gtk-theme");
    if ((components & ATM_COMPONENT_ICONS) != 0)
        snapshot_string(snapshot, "IconTheme", interface, "icon-theme");
    if ((components & ATM_COMPONENT_CURSOR) != 0) {
        snapshot_string(snapshot, "CursorTheme", interface, "cursor-theme");
        g_key_file_set_integer(snapshot,
                               "Settings",
                               "CursorSize",
                               g_settings_get_int(interface, "cursor-size"));
    }
    if ((components & ATM_COMPONENT_CINNAMON) != 0)
        snapshot_string(snapshot, "CinnamonTheme", cinnamon, "name");
    if ((components & ATM_COMPONENT_WALLPAPER) != 0)
        snapshot_string(snapshot, "Wallpaper", background, "picture-uri");
    if ((components & ATM_COMPONENT_APPEARANCE) != 0) {
        snapshot_string(snapshot, "ColorScheme", portal, "color-scheme");
        snapshot_string(snapshot, "AccentRGB", portal, "accent-rgb");
    }

    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error,
                    G_IO_ERROR,
                    g_io_error_from_errno(errno),
                    "Cannot create state directory %s: %s",
                    directory,
                    g_strerror(errno));
        return FALSE;
    }
    contents = g_key_file_to_data(snapshot, &length, error);
    if (contents == NULL)
        return FALSE;
    return g_file_set_contents(path, contents, (gssize) length, error);
}

static gboolean
write_current_theme(const gchar *id, GError **error)
{
    g_autofree gchar *directory = get_state_directory();
    g_autofree gchar *path = g_build_filename(directory, "current", NULL);

    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error,
                    G_IO_ERROR,
                    g_io_error_from_errno(errno),
                    "Cannot create state directory %s: %s",
                    directory,
                    g_strerror(errno));
        return FALSE;
    }
    return g_file_set_contents(path, id, -1, error);
}

static gboolean
clear_current_theme(GError **error)
{
    g_autofree gchar *directory = get_state_directory();
    g_autofree gchar *path = g_build_filename(directory, "current", NULL);

    if (g_remove(path) == 0 || errno == ENOENT)
        return TRUE;
    g_set_error(error,
                G_IO_ERROR,
                g_io_error_from_errno(errno),
                "Cannot clear current theme marker: %s",
                g_strerror(errno));
    return FALSE;
}

static gboolean
apply_internal(const AtmTheme *theme,
               AtmComponent requested,
               gboolean record_bundle,
               GError **error)
{
    static const gchar *interface_keys[] = {
        "gtk-theme", "icon-theme", "cursor-theme", "cursor-size", NULL
    };
    static const gchar *cinnamon_keys[] = { "name", NULL };
    static const gchar *background_keys[] = { "picture-uri", NULL };
    static const gchar *portal_keys[] = { "color-scheme", "accent-rgb", NULL };
    g_autoptr(GSettings) interface = NULL;
    g_autoptr(GSettings) cinnamon = NULL;
    g_autoptr(GSettings) background = NULL;
    g_autoptr(GSettings) portal = NULL;
    g_autofree gchar *wallpaper_path = NULL;
    g_autofree gchar *wallpaper_uri = NULL;
    AtmComponent components;

    g_return_val_if_fail(theme != NULL, FALSE);

    components = requested & atm_theme_get_components(theme);
    /* Lock CSS is loaded by Cinnamon Screensaver through the GTK theme. */
    components &= ~ATM_COMPONENT_LOCK;
    if (components == ATM_COMPONENT_NONE) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_APPLY,
                    "None of the requested independently-applied components are present");
        return FALSE;
    }

    if (!atm_store_expose_components(theme, components, error))
        return FALSE;
    if (!atm_discovery_validate_theme(theme, components, error))
        return FALSE;

    interface = settings_for(INTERFACE_SCHEMA, interface_keys, error);
    cinnamon = settings_for(CINNAMON_SCHEMA, cinnamon_keys, error);
    background = settings_for(BACKGROUND_SCHEMA, background_keys, error);
    portal = settings_for(PORTAL_SCHEMA, portal_keys, error);
    if (interface == NULL || cinnamon == NULL || background == NULL || portal == NULL)
        return FALSE;

    if (!save_snapshot(theme,
                       components,
                       interface,
                       cinnamon,
                       background,
                       portal,
                       error))
        return FALSE;

    if ((components & ATM_COMPONENT_WALLPAPER) != 0) {
        wallpaper_path = atm_theme_resolve_path(theme, theme->wallpaper, error);
        if (wallpaper_path == NULL)
            goto rollback;
        wallpaper_uri = g_filename_to_uri(wallpaper_path, NULL, error);
        if (wallpaper_uri == NULL ||
            !set_string_checked(background, "picture-uri", wallpaper_uri, error))
            goto rollback;
    }
    if ((components & ATM_COMPONENT_CINNAMON) != 0 &&
        !set_string_checked(cinnamon, "name", theme->cinnamon_theme, error))
        goto rollback;
    if ((components & ATM_COMPONENT_GTK) != 0 &&
        !set_string_checked(interface, "gtk-theme", theme->gtk_theme, error))
        goto rollback;
    if ((components & ATM_COMPONENT_ICONS) != 0 &&
        !set_string_checked(interface, "icon-theme", theme->icon_theme, error))
        goto rollback;
    if ((components & ATM_COMPONENT_CURSOR) != 0 &&
        (!set_string_checked(interface, "cursor-theme", theme->cursor_theme, error) ||
         !set_int_checked(interface, "cursor-size", theme->cursor_size, error)))
        goto rollback;
    if ((components & ATM_COMPONENT_APPEARANCE) != 0) {
        if (theme->color_scheme != NULL &&
            !set_string_checked(portal, "color-scheme", theme->color_scheme, error))
            goto rollback;
        if (theme->accent_rgb != NULL &&
            !set_string_checked(portal, "accent-rgb", theme->accent_rgb, error))
            goto rollback;
    }

    g_settings_sync();
    if (record_bundle &&
        components == (atm_theme_get_components(theme) & ~ATM_COMPONENT_LOCK)) {
        if (!write_current_theme(theme->id, error))
            goto rollback;
    } else if (!clear_current_theme(error)) {
        goto rollback;
    }
    return TRUE;

rollback:
    {
        g_autoptr(GError) rollback_error = NULL;
        atm_applier_restore(&rollback_error);
        if (rollback_error != NULL)
            g_warning("Theme rollback also failed: %s", rollback_error->message);
    }
    return FALSE;
}

gboolean
atm_applier_apply(const AtmTheme *theme,
                  AtmComponent components,
                  GError **error)
{
    return apply_internal(theme, components, TRUE, error);
}

gboolean
atm_applier_apply_custom(const AtmAppearanceSettings *settings, GError **error)
{
    AtmTheme theme = { 0 };
    AtmComponent components;
    g_autofree gchar *wallpaper = NULL;
    g_autofree gchar *directory = NULL;
    g_autofree gchar *basename = NULL;

    g_return_val_if_fail(settings != NULL, FALSE);

    if (settings->wallpaper_path != NULL) {
        wallpaper = g_canonicalize_filename(settings->wallpaper_path, NULL);
        if (!g_file_test(wallpaper, G_FILE_TEST_IS_REGULAR)) {
            g_set_error(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_FOUND,
                        "Wallpaper file does not exist: %s",
                        wallpaper);
            return FALSE;
        }
        directory = g_path_get_dirname(wallpaper);
        basename = g_path_get_basename(wallpaper);
    }

    theme.id = (gchar *) "custom-composition";
    theme.name = (gchar *) "Custom appearance";
    theme.directory = directory != NULL ? directory : (gchar *) "/";
    theme.wallpaper = basename;
    theme.cinnamon_theme = settings->cinnamon_theme;
    theme.gtk_theme = settings->gtk_theme;
    theme.icon_theme = settings->icon_theme;
    theme.cursor_theme = settings->cursor_theme;
    theme.cursor_size = settings->cursor_size;
    theme.color_scheme = settings->color_scheme;
    theme.accent_rgb = settings->accent_rgb;

    components = atm_theme_get_components(&theme);
    return apply_internal(&theme, components, FALSE, error);
}

static gboolean
restore_optional_string(GKeyFile *snapshot,
                        const gchar *snapshot_key,
                        GSettings *settings,
                        const gchar *settings_key,
                        GError **error)
{
    g_autofree gchar *value = NULL;

    if (!g_key_file_has_key(snapshot, "Settings", snapshot_key, NULL))
        return TRUE;
    value = g_key_file_get_string(snapshot, "Settings", snapshot_key, error);
    return value != NULL && set_string_checked(settings, settings_key, value, error);
}

AtmAppearanceSettings *
atm_applier_read_current(GError **error)
{
    static const gchar *interface_keys[] = {
        "gtk-theme", "icon-theme", "cursor-theme", "cursor-size", NULL
    };
    static const gchar *cinnamon_keys[] = { "name", NULL };
    static const gchar *background_keys[] = { "picture-uri", NULL };
    static const gchar *portal_keys[] = { "color-scheme", "accent-rgb", NULL };
    g_autoptr(GSettings) interface = NULL;
    g_autoptr(GSettings) cinnamon = NULL;
    g_autoptr(GSettings) background = NULL;
    g_autoptr(GSettings) portal = NULL;
    g_autoptr(AtmAppearanceSettings) current = g_new0(AtmAppearanceSettings, 1);
    g_autofree gchar *wallpaper_uri = NULL;

    interface = settings_for(INTERFACE_SCHEMA, interface_keys, error);
    cinnamon = settings_for(CINNAMON_SCHEMA, cinnamon_keys, error);
    background = settings_for(BACKGROUND_SCHEMA, background_keys, error);
    portal = settings_for(PORTAL_SCHEMA, portal_keys, error);
    if (interface == NULL || cinnamon == NULL ||
        background == NULL || portal == NULL)
        return NULL;

    current->gtk_theme = g_settings_get_string(interface, "gtk-theme");
    current->icon_theme = g_settings_get_string(interface, "icon-theme");
    current->cursor_theme = g_settings_get_string(interface, "cursor-theme");
    current->cursor_size = g_settings_get_int(interface, "cursor-size");
    current->cinnamon_theme = g_settings_get_string(cinnamon, "name");
    current->color_scheme = g_settings_get_string(portal, "color-scheme");
    current->accent_rgb = g_settings_get_string(portal, "accent-rgb");

    wallpaper_uri = g_settings_get_string(background, "picture-uri");
    if (wallpaper_uri != NULL && *wallpaper_uri != '\0')
        current->wallpaper_path = g_filename_from_uri(wallpaper_uri, NULL, NULL);
    return g_steal_pointer(&current);
}

void
atm_appearance_settings_free(AtmAppearanceSettings *settings)
{
    if (settings == NULL)
        return;
    g_free(settings->cinnamon_theme);
    g_free(settings->gtk_theme);
    g_free(settings->icon_theme);
    g_free(settings->cursor_theme);
    g_free(settings->wallpaper_path);
    g_free(settings->color_scheme);
    g_free(settings->accent_rgb);
    g_free(settings);
}

gboolean
atm_applier_restore(GError **error)
{
    static const gchar *interface_keys[] = {
        "gtk-theme", "icon-theme", "cursor-theme", "cursor-size", NULL
    };
    static const gchar *cinnamon_keys[] = { "name", NULL };
    static const gchar *background_keys[] = { "picture-uri", NULL };
    static const gchar *portal_keys[] = { "color-scheme", "accent-rgb", NULL };
    g_autofree gchar *directory = get_state_directory();
    g_autofree gchar *path = g_build_filename(directory, "previous.ini", NULL);
    g_autoptr(GKeyFile) snapshot = g_key_file_new();
    g_autoptr(GSettings) interface = NULL;
    g_autoptr(GSettings) cinnamon = NULL;
    g_autoptr(GSettings) background = NULL;
    g_autoptr(GSettings) portal = NULL;
    g_autofree gchar *previous_theme = NULL;

    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error,
                    ATM_APPLIER_ERROR,
                    ATM_APPLIER_ERROR_NO_SNAPSHOT,
                    "There is no previous theme snapshot to restore");
        return FALSE;
    }
    if (!g_key_file_load_from_file(snapshot, path, G_KEY_FILE_NONE, error))
        return FALSE;

    interface = settings_for(INTERFACE_SCHEMA, interface_keys, error);
    cinnamon = settings_for(CINNAMON_SCHEMA, cinnamon_keys, error);
    background = settings_for(BACKGROUND_SCHEMA, background_keys, error);
    portal = settings_for(PORTAL_SCHEMA, portal_keys, error);
    if (interface == NULL || cinnamon == NULL || background == NULL || portal == NULL)
        return FALSE;

    if (!restore_optional_string(snapshot, "GtkTheme", interface, "gtk-theme", error) ||
        !restore_optional_string(snapshot, "IconTheme", interface, "icon-theme", error) ||
        !restore_optional_string(snapshot, "CursorTheme", interface, "cursor-theme", error) ||
        !restore_optional_string(snapshot, "CinnamonTheme", cinnamon, "name", error) ||
        !restore_optional_string(snapshot, "Wallpaper", background, "picture-uri", error) ||
        !restore_optional_string(snapshot, "ColorScheme", portal, "color-scheme", error) ||
        !restore_optional_string(snapshot, "AccentRGB", portal, "accent-rgb", error))
        return FALSE;
    if (g_key_file_has_key(snapshot, "Settings", "CursorSize", NULL)) {
        gint size = g_key_file_get_integer(snapshot, "Settings", "CursorSize", error);
        if ((error != NULL && *error != NULL) ||
            !set_int_checked(interface, "cursor-size", size, error))
            return FALSE;
    }
    g_settings_sync();

    previous_theme = g_key_file_get_string(snapshot,
                                           "Snapshot",
                                           "PreviousTheme",
                                           NULL);
    if (previous_theme != NULL) {
        if (!write_current_theme(previous_theme, error))
            return FALSE;
    } else if (!clear_current_theme(error)) {
        return FALSE;
    }
    return TRUE;
}

gchar *
atm_applier_get_current_theme_id(void)
{
    g_autofree gchar *directory = get_state_directory();
    g_autofree gchar *path = g_build_filename(directory, "current", NULL);
    gchar *contents = NULL;

    if (!g_file_get_contents(path, &contents, NULL, NULL))
        return NULL;
    g_strstrip(contents);
    return contents;
}
