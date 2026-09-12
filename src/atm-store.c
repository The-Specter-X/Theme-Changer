#define _POSIX_C_SOURCE 200809L

#include "atm-store.h"
#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

#define MAX_BUNDLE_FILES 4096u
#define MAX_BUNDLE_BYTES ((guint64) 256 * 1024 * 1024)

typedef struct {
    guint files;
    guint64 bytes;
} CopyBudget;

GQuark
atm_store_error_quark(void)
{
    return g_quark_from_static_string("atm-store-error");
}

gchar *
atm_store_get_user_theme_dir(void)
{
    const gchar *override = g_getenv("AXIONIS_THEME_USER_DIR");

    if (override != NULL && *override != '\0')
        return g_canonicalize_filename(override, NULL);
    return g_build_filename(g_get_user_data_dir(),
                            "axionis-theme-manager",
                            "themes",
                            NULL);
}

gchar *
atm_store_get_system_theme_dir(void)
{
    const gchar *override = g_getenv("AXIONIS_THEME_SYSTEM_DIR");

    if (override != NULL && *override != '\0')
        return g_canonicalize_filename(override, NULL);
    return g_build_filename(ATM_DATADIR,
                            "axionis-theme-manager",
                            "themes",
                            NULL);
}

static gint
compare_theme_name(gconstpointer a, gconstpointer b)
{
    const AtmTheme *theme_a = *(AtmTheme * const *) a;
    const AtmTheme *theme_b = *(AtmTheme * const *) b;
    return g_utf8_collate(theme_a->name, theme_b->name);
}

static void
scan_root(const gchar *root, GHashTable *themes)
{
    g_autoptr(GDir) directory = NULL;
    const gchar *entry;

    directory = g_dir_open(root, 0, NULL);
    if (directory == NULL)
        return;

    while ((entry = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *path = g_build_filename(root, entry, NULL);
        g_autofree gchar *manifest = g_build_filename(path, "theme.ini", NULL);
        g_autoptr(GError) local_error = NULL;
        AtmTheme *theme;

        if (!g_file_test(path, G_FILE_TEST_IS_DIR) ||
            !g_file_test(manifest, G_FILE_TEST_IS_REGULAR))
            continue;
        theme = atm_theme_load(path, &local_error);
        if (theme == NULL) {
            g_warning("Ignoring invalid theme at %s: %s",
                      path,
                      local_error != NULL ? local_error->message : "unknown error");
            continue;
        }
        g_hash_table_replace(themes, g_strdup(theme->id), theme);
    }
}

GPtrArray *
atm_store_list(GError **error)
{
    g_autofree gchar *system_root = atm_store_get_system_theme_dir();
    g_autofree gchar *user_root = atm_store_get_user_theme_dir();
    g_autoptr(GHashTable) themes = NULL;
    g_autoptr(GPtrArray) result = NULL;
    GHashTableIter iter;
    gpointer value;

    (void) error;
    themes = g_hash_table_new_full(g_str_hash,
                                   g_str_equal,
                                   g_free,
                                   (GDestroyNotify) atm_theme_free);
    scan_root(system_root, themes);
    scan_root(user_root, themes);

    result = g_ptr_array_new_with_free_func((GDestroyNotify) atm_theme_free);
    g_hash_table_iter_init(&iter, themes);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        g_ptr_array_add(result, value);
        g_hash_table_iter_steal(&iter);
    }
    g_ptr_array_sort(result, compare_theme_name);
    return g_steal_pointer(&result);
}

AtmTheme *
atm_store_find(const gchar *id, GError **error)
{
    g_autoptr(GPtrArray) themes = atm_store_list(error);
    guint i;

    if (themes == NULL)
        return NULL;
    for (i = 0; i < themes->len; i++) {
        AtmTheme *theme = g_ptr_array_index(themes, i);
        if (g_strcmp0(theme->id, id) == 0) {
            g_ptr_array_steal_index(themes, i);
            return theme;
        }
    }
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "Theme '%s' was not found",
                id != NULL ? id : "(null)");
    return NULL;
}

static gboolean
has_forbidden_suffix(const gchar *name)
{
    static const gchar *suffixes[] = {
        ".sh", ".bash", ".zsh", ".fish", ".py", ".pyc", ".lua",
        ".pl", ".rb", ".js", ".so", ".desktop", ".service", ".timer",
        ".socket", ".mount", ".policy", NULL
    };
    g_autofree gchar *lower = g_ascii_strdown(name, -1);
    guint i;

    for (i = 0; suffixes[i] != NULL; i++) {
        if (g_str_has_suffix(lower, suffixes[i]))
            return TRUE;
    }
    return FALSE;
}

static gboolean
remove_tree(const gchar *path)
{
    g_autoptr(GDir) directory = NULL;
    const gchar *entry;

    if (!g_file_test(path, G_FILE_TEST_IS_DIR) ||
        g_file_test(path, G_FILE_TEST_IS_SYMLINK))
        return g_remove(path) == 0;

    directory = g_dir_open(path, 0, NULL);
    if (directory == NULL)
        return FALSE;
    while ((entry = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *child = g_build_filename(path, entry, NULL);
        if (!remove_tree(child))
            return FALSE;
    }
    g_clear_pointer(&directory, g_dir_close);
    return g_rmdir(path) == 0;
}

static gboolean
copy_tree_checked(const gchar *source,
                  const gchar *destination,
                  CopyBudget *budget,
                  GError **error)
{
    GStatBuf status;

    if (g_file_test(source, G_FILE_TEST_IS_SYMLINK)) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_UNSAFE_CONTENT,
                    "Symbolic links are not allowed in imported themes: %s",
                    source);
        return FALSE;
    }
    if (g_lstat(source, &status) != 0) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_IO,
                    "Cannot inspect %s: %s",
                    source,
                    g_strerror(errno));
        return FALSE;
    }

    if (S_ISDIR(status.st_mode)) {
        g_autoptr(GDir) directory = NULL;
        const gchar *entry;

        if (g_mkdir_with_parents(destination, 0700) != 0) {
            g_set_error(error,
                        ATM_STORE_ERROR,
                        ATM_STORE_ERROR_IO,
                        "Cannot create %s: %s",
                        destination,
                        g_strerror(errno));
            return FALSE;
        }
        directory = g_dir_open(source, 0, error);
        if (directory == NULL)
            return FALSE;
        while ((entry = g_dir_read_name(directory)) != NULL) {
            g_autofree gchar *source_child = g_build_filename(source, entry, NULL);
            g_autofree gchar *destination_child = g_build_filename(destination, entry, NULL);
            if (!copy_tree_checked(source_child, destination_child, budget, error))
                return FALSE;
        }
        return TRUE;
    }

    if (!S_ISREG(status.st_mode)) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_UNSAFE_CONTENT,
                    "Only regular files and directories are allowed: %s",
                    source);
        return FALSE;
    }
    if ((status.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 ||
        has_forbidden_suffix(source)) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_UNSAFE_CONTENT,
                    "Executable content is not allowed in imported themes: %s",
                    source);
        return FALSE;
    }

    budget->files++;
    budget->bytes += (guint64) status.st_size;
    if (budget->files > MAX_BUNDLE_FILES || budget->bytes > MAX_BUNDLE_BYTES) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_UNSAFE_CONTENT,
                    "Theme exceeds the import limit (%u files, %u MiB)",
                    MAX_BUNDLE_FILES,
                    (guint) (MAX_BUNDLE_BYTES / 1024 / 1024));
        return FALSE;
    }

    {
        g_autoptr(GFile) source_file = g_file_new_for_path(source);
        g_autoptr(GFile) destination_file = g_file_new_for_path(destination);
        return g_file_copy(source_file,
                           destination_file,
                           G_FILE_COPY_NONE,
                           NULL,
                           NULL,
                           NULL,
                           error);
    }
}

AtmTheme *
atm_store_import(const gchar *source_directory, GError **error)
{
    g_autoptr(AtmTheme) source_theme = NULL;
    g_autoptr(AtmTheme) imported_theme = NULL;
    g_autofree gchar *user_root = NULL;
    g_autofree gchar *destination = NULL;
    CopyBudget budget = { 0, 0 };

    source_theme = atm_theme_load(source_directory, error);
    if (source_theme == NULL)
        return NULL;

    user_root = atm_store_get_user_theme_dir();
    destination = g_build_filename(user_root, source_theme->id, NULL);
    if (g_file_test(destination, G_FILE_TEST_EXISTS)) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_ALREADY_EXISTS,
                    "A user theme with ID '%s' already exists",
                    source_theme->id);
        return NULL;
    }
    if (g_mkdir_with_parents(user_root, 0700) != 0) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_IO,
                    "Cannot create theme directory %s: %s",
                    user_root,
                    g_strerror(errno));
        return NULL;
    }
    if (!copy_tree_checked(source_directory, destination, &budget, error)) {
        remove_tree(destination);
        return NULL;
    }

    imported_theme = atm_theme_load(destination, error);
    if (imported_theme == NULL) {
        remove_tree(destination);
        return NULL;
    }
    return g_steal_pointer(&imported_theme);
}

static gboolean
link_matches(const gchar *link_path, const gchar *target)
{
    g_autofree gchar *value = NULL;
    g_autofree gchar *parent = NULL;
    gchar buffer[4096];
    ssize_t length;

    length = readlink(link_path, buffer, sizeof(buffer) - 1);
    if (length < 0)
        return FALSE;
    buffer[length] = '\0';
    parent = g_path_get_dirname(link_path);
    value = g_canonicalize_filename(buffer, parent);
    return g_strcmp0(value, target) == 0;
}

static gboolean
expose_one(const AtmTheme *theme,
           const gchar *relative_path,
           const gchar *name,
           const gchar *category,
           GError **error)
{
    g_autofree gchar *source = NULL;
    g_autofree gchar *destination_root = NULL;
    g_autofree gchar *destination = NULL;

    if (relative_path == NULL)
        return TRUE;

    source = atm_theme_resolve_path(theme, relative_path, error);
    if (source == NULL)
        return FALSE;
    destination_root = g_build_filename(g_get_user_data_dir(), category, NULL);
    destination = g_build_filename(destination_root, name, NULL);
    if (g_mkdir_with_parents(destination_root, 0700) != 0) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_IO,
                    "Cannot create %s: %s",
                    destination_root,
                    g_strerror(errno));
        return FALSE;
    }

    if (g_file_test(destination, G_FILE_TEST_IS_SYMLINK)) {
        if (link_matches(destination, source))
            return TRUE;
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_CONFLICT,
                    "Refusing to replace existing theme link %s",
                    destination);
        return FALSE;
    }
    if (g_file_test(destination, G_FILE_TEST_EXISTS)) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_CONFLICT,
                    "A theme named '%s' already exists in %s",
                    name,
                    destination_root);
        return FALSE;
    }
    if (symlink(source, destination) != 0) {
        g_set_error(error,
                    ATM_STORE_ERROR,
                    ATM_STORE_ERROR_IO,
                    "Cannot expose %s: %s",
                    name,
                    g_strerror(errno));
        return FALSE;
    }
    return TRUE;
}

gboolean
atm_store_expose_components(const AtmTheme *theme,
                            AtmComponent components,
                            GError **error)
{
    g_return_val_if_fail(theme != NULL, FALSE);

    if ((components & ATM_COMPONENT_CINNAMON) != 0 &&
        !expose_one(theme,
                    theme->cinnamon_path,
                    theme->cinnamon_theme,
                    "themes",
                    error))
        return FALSE;
    if ((components & ATM_COMPONENT_GTK) != 0 &&
        !expose_one(theme,
                    theme->gtk_path,
                    theme->gtk_theme,
                    "themes",
                    error))
        return FALSE;
    if ((components & ATM_COMPONENT_ICONS) != 0 &&
        !expose_one(theme,
                    theme->icon_path,
                    theme->icon_theme,
                    "icons",
                    error))
        return FALSE;
    if ((components & ATM_COMPONENT_CURSOR) != 0 &&
        !expose_one(theme,
                    theme->cursor_path,
                    theme->cursor_theme,
                    "icons",
                    error))
        return FALSE;
    return TRUE;
}
