#include "atm-discovery.h"

#include <string.h>
#include <glib/gstdio.h>

#define MAX_CSS_SCAN_BYTES (4 * 1024 * 1024)

static gint
compare_names(gconstpointer a, gconstpointer b)
{
    const gchar *name_a = *(gchar * const *) a;
    const gchar *name_b = *(gchar * const *) b;
    return g_utf8_collate(name_a, name_b);
}

static void
add_root(GPtrArray *roots, const gchar *path)
{
    guint i;

    if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_DIR))
        return;
    for (i = 0; i < roots->len; i++) {
        if (g_strcmp0(g_ptr_array_index(roots, i), path) == 0)
            return;
    }
    g_ptr_array_add(roots, g_strdup(path));
}

static GPtrArray *
get_roots(AtmDiscoveryKind kind)
{
    g_autoptr(GPtrArray) roots = g_ptr_array_new_with_free_func(g_free);
    const gchar *category =
        (kind == ATM_DISCOVERY_ICONS || kind == ATM_DISCOVERY_CURSOR) ?
        "icons" : "themes";
    const gchar * const *system_dirs = g_get_system_data_dirs();
    g_autofree gchar *path = NULL;
    guint i;

    path = g_build_filename(g_get_home_dir(),
                            g_str_equal(category, "icons") ? ".icons" : ".themes",
                            NULL);
    add_root(roots, path);
    g_clear_pointer(&path, g_free);

    path = g_build_filename(g_get_user_data_dir(), category, NULL);
    add_root(roots, path);
    g_clear_pointer(&path, g_free);

    for (i = 0; system_dirs[i] != NULL; i++) {
        path = g_build_filename(system_dirs[i], category, NULL);
        add_root(roots, path);
        g_clear_pointer(&path, g_free);
    }
    return g_steal_pointer(&roots);
}

static gboolean
theme_is_blacklisted(const gchar *name)
{
    static const gchar *blacklist[] = {
        "gnome", "hicolor", "adwaita", "adwaita-dark",
        "adwaitalegacy", "highcontrast", "epapirus",
        "epapirus-dark", "ubuntu-mono", "ubuntu-mono-dark",
        "ubuntu-mono-light", "loginicons", "humanity",
        "humanity-dark", NULL
    };
    g_autofree gchar *lower = g_ascii_strdown(name, -1);
    guint i;

    for (i = 0; blacklist[i] != NULL; i++) {
        if (g_str_equal(lower, blacklist[i]))
            return TRUE;
    }
    return FALSE;
}

static gboolean
icon_index_has_directories(const gchar *index_path)
{
    g_autoptr(GKeyFile) key_file = g_key_file_new();

    if (!g_key_file_load_from_file(key_file, index_path, G_KEY_FILE_NONE, NULL))
        return FALSE;
    if (g_key_file_get_boolean(key_file, "Icon Theme", "Hidden", NULL))
        return FALSE;
    return g_key_file_has_key(key_file, "Icon Theme", "Directories", NULL) ||
           g_key_file_has_key(key_file, "Icon Theme", "ScaledDirectories", NULL);
}

static gboolean
gtk_theme_matches(const gchar *candidate)
{
    g_autoptr(GDir) directory = g_dir_open(candidate, 0, NULL);
    const gchar *entry;

    if (directory == NULL)
        return FALSE;
    while ((entry = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *css = NULL;

        if (!g_str_has_prefix(entry, "gtk-3."))
            continue;
        css = g_build_filename(candidate, entry, "gtk.css", NULL);
        if (g_file_test(css, G_FILE_TEST_IS_REGULAR))
            return TRUE;
    }
    return FALSE;
}

static gboolean
candidate_matches(const gchar *candidate, AtmDiscoveryKind kind)
{
    g_autofree gchar *required = NULL;
    g_autofree gchar *index_path = NULL;

    switch (kind) {
    case ATM_DISCOVERY_CINNAMON:
        required = g_build_filename(candidate, "cinnamon", "cinnamon.css", NULL);
        return g_file_test(required, G_FILE_TEST_IS_REGULAR);
    case ATM_DISCOVERY_GTK:
        return gtk_theme_matches(candidate);
    case ATM_DISCOVERY_ICONS:
        index_path = g_build_filename(candidate, "index.theme", NULL);
        return g_file_test(index_path, G_FILE_TEST_IS_REGULAR) &&
               icon_index_has_directories(index_path);
    case ATM_DISCOVERY_CURSOR:
        index_path = g_build_filename(candidate, "index.theme", NULL);
        required = g_build_filename(candidate, "cursors", NULL);
        return g_file_test(index_path, G_FILE_TEST_IS_REGULAR) &&
               g_file_test(required, G_FILE_TEST_IS_DIR);
    default:
        return FALSE;
    }
}

static gboolean
builtin_cinnamon_matches(const gchar *path)
{
    g_autofree gchar *css = g_build_filename(path, "cinnamon.css", NULL);
    return g_file_test(css, G_FILE_TEST_IS_REGULAR);
}

static gchar *
find_builtin_cinnamon(void)
{
    const gchar * const *system_dirs = g_get_system_data_dirs();
    g_autofree gchar *path = NULL;
    guint i;

    path = g_build_filename(g_get_user_data_dir(),
                            "cinnamon", "theme", NULL);
    if (builtin_cinnamon_matches(path))
        return g_steal_pointer(&path);
    g_clear_pointer(&path, g_free);

    for (i = 0; system_dirs[i] != NULL; i++) {
        path = g_build_filename(system_dirs[i],
                                "cinnamon", "theme", NULL);
        if (builtin_cinnamon_matches(path))
            return g_steal_pointer(&path);
        g_clear_pointer(&path, g_free);
    }
    return NULL;
}

gchar *
atm_discovery_get_path(AtmDiscoveryKind kind, const gchar *name)
{
    g_autoptr(GPtrArray) roots = NULL;
    guint i;

    if (name == NULL || *name == '\0' ||
        strchr(name, G_DIR_SEPARATOR) != NULL)
        return NULL;

    if (kind == ATM_DISCOVERY_CINNAMON &&
        g_strcmp0(name, "cinnamon") == 0)
        return find_builtin_cinnamon();

    roots = get_roots(kind);
    for (i = 0; i < roots->len; i++) {
        g_autofree gchar *candidate =
            g_build_filename(g_ptr_array_index(roots, i), name, NULL);
        if (candidate_matches(candidate, kind))
            return g_steal_pointer(&candidate);
    }
    return NULL;
}

GPtrArray *
atm_discovery_list(AtmDiscoveryKind kind)
{
    g_autoptr(GPtrArray) roots = get_roots(kind);
    g_autoptr(GHashTable) names =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_autoptr(GPtrArray) result = g_ptr_array_new_with_free_func(g_free);
    guint i;

    for (i = 0; i < roots->len; i++) {
        g_autoptr(GDir) directory = g_dir_open(g_ptr_array_index(roots, i), 0, NULL);
        const gchar *entry;

        if (directory == NULL)
            continue;
        while ((entry = g_dir_read_name(directory)) != NULL) {
            g_autofree gchar *candidate =
                g_build_filename(g_ptr_array_index(roots, i), entry, NULL);
            if (!theme_is_blacklisted(entry) &&
                candidate_matches(candidate, kind))
                g_hash_table_add(names, g_strdup(entry));
        }
    }

    if (kind == ATM_DISCOVERY_CINNAMON) {
        g_autofree gchar *builtin = find_builtin_cinnamon();
        if (builtin != NULL)
            g_hash_table_add(names, g_strdup("cinnamon"));
    }

    {
        GHashTableIter iter;
        gpointer key;

        g_hash_table_iter_init(&iter, names);
        while (g_hash_table_iter_next(&iter, &key, NULL))
            g_ptr_array_add(result, g_strdup(key));
    }
    g_ptr_array_sort(result, compare_names);
    return g_steal_pointer(&result);
}

gboolean
atm_discovery_has(AtmDiscoveryKind kind, const gchar *name)
{
    g_autofree gchar *path = atm_discovery_get_path(kind, name);
    return path != NULL;
}

static gboolean
validate_one(AtmDiscoveryKind kind,
             const gchar *label,
             const gchar *name,
             GError **error)
{
    if (name != NULL && !atm_discovery_has(kind, name)) {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_NOT_FOUND,
                    "%s theme '%s' is not installed or is incomplete",
                    label,
                    name);
        return FALSE;
    }
    return TRUE;
}

gboolean
atm_discovery_validate_theme(const AtmTheme *theme,
                             AtmComponent components,
                             GError **error)
{
    g_return_val_if_fail(theme != NULL, FALSE);

    if ((components & ATM_COMPONENT_CINNAMON) != 0 &&
        !validate_one(ATM_DISCOVERY_CINNAMON,
                      "Cinnamon desktop",
                      theme->cinnamon_theme,
                      error))
        return FALSE;
    if ((components & ATM_COMPONENT_GTK) != 0 &&
        !validate_one(ATM_DISCOVERY_GTK,
                      "Application",
                      theme->gtk_theme,
                      error))
        return FALSE;
    if ((components & ATM_COMPONENT_ICONS) != 0 &&
        !validate_one(ATM_DISCOVERY_ICONS,
                      "Icon",
                      theme->icon_theme,
                      error))
        return FALSE;
    if ((components & ATM_COMPONENT_CURSOR) != 0 &&
        !validate_one(ATM_DISCOVERY_CURSOR,
                      "Cursor",
                      theme->cursor_theme,
                      error))
        return FALSE;
    return TRUE;
}

static gboolean
css_tree_has_lock_style(const gchar *path, guint depth)
{
    g_autoptr(GDir) directory = NULL;
    const gchar *entry;

    if (depth > 4)
        return FALSE;
    directory = g_dir_open(path, 0, NULL);
    if (directory == NULL)
        return FALSE;

    while ((entry = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *child = g_build_filename(path, entry, NULL);

        if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
            if (css_tree_has_lock_style(child, depth + 1))
                return TRUE;
        } else if (g_str_has_suffix(entry, ".css")) {
            GStatBuf status;
            g_autofree gchar *contents = NULL;

            if (g_stat(child, &status) == 0 &&
                status.st_size >= 0 &&
                status.st_size <= MAX_CSS_SCAN_BYTES &&
                g_file_get_contents(child, &contents, NULL, NULL) &&
                strstr(contents, ".csstage") != NULL)
                return TRUE;
        }
    }
    return FALSE;
}

gboolean
atm_discovery_gtk_has_lock_style(const gchar *name)
{
    g_autofree gchar *path = atm_discovery_get_path(ATM_DISCOVERY_GTK, name);
    g_autofree gchar *gtk_path = NULL;

    if (path == NULL)
        return FALSE;
    gtk_path = g_build_filename(path, "gtk-3.0", NULL);
    return css_tree_has_lock_style(gtk_path, 0);
}
