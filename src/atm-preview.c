#include "atm-preview.h"

static GHashTable *preview_cache;
static GHashTable *preview_misses;
static GtkIconTheme *preview_icon_theme;

static void
ensure_preview_caches(void)
{
    if (preview_cache == NULL)
        preview_cache =
            g_hash_table_new_full(g_str_hash,
                                  g_str_equal,
                                  g_free,
                                  g_object_unref);
    if (preview_misses == NULL)
        preview_misses =
            g_hash_table_new_full(g_str_hash,
                                  g_str_equal,
                                  g_free,
                                  NULL);
}

void
atm_preview_cache_clear(void)
{
    if (preview_cache != NULL)
        g_hash_table_remove_all(preview_cache);
    if (preview_misses != NULL)
        g_hash_table_remove_all(preview_misses);
    g_clear_object(&preview_icon_theme);
}

static gchar *
find_existing_file(const gchar *base,
                   const gchar *suffix,
                   const gchar *name)
{
    const gchar * const *system_dirs = g_get_system_data_dirs();
    g_autofree gchar *candidate = NULL;
    g_autofree gchar *thumbnail_name =
        g_strdup_printf("%s.png", name);
    guint i;

    if (base != NULL) {
        candidate = g_build_filename(base, suffix, "thumbnail.png", NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
            return g_steal_pointer(&candidate);
        g_clear_pointer(&candidate, g_free);

        if (g_str_equal(suffix, "gtk-3.0")) {
            g_autoptr(GDir) directory = g_dir_open(base, 0, NULL);
            const gchar *entry;

            while (directory != NULL &&
                   (entry = g_dir_read_name(directory)) != NULL) {
                if (!g_str_has_prefix(entry, "gtk-3."))
                    continue;
                candidate = g_build_filename(base,
                                             entry,
                                             "thumbnail.png",
                                             NULL);
                if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
                    return g_steal_pointer(&candidate);
                g_clear_pointer(&candidate, g_free);
            }
        }

        candidate = g_build_filename(base, "thumbnail.png", NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
            return g_steal_pointer(&candidate);
        g_clear_pointer(&candidate, g_free);
    }

    candidate = g_build_filename(g_get_user_data_dir(),
                                 "cinnamon", "thumbnails",
                                 suffix, thumbnail_name, NULL);
    if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
        return g_steal_pointer(&candidate);
    g_clear_pointer(&candidate, g_free);

    for (i = 0; system_dirs[i] != NULL; i++) {
        candidate = g_build_filename(system_dirs[i],
                                     "cinnamon", "thumbnails",
                                     suffix, thumbnail_name, NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
            return g_steal_pointer(&candidate);
        g_clear_pointer(&candidate, g_free);
    }

    for (i = 0; system_dirs[i] != NULL; i++) {
        candidate = g_build_filename(system_dirs[i],
                                     "cinnamon", "thumbnails",
                                     suffix, "unknown.png", NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
            return g_steal_pointer(&candidate);
        g_clear_pointer(&candidate, g_free);
    }
    return NULL;
}

static GdkPixbuf *
load_icon_theme_preview(const gchar *theme_name, gint size)
{
    GtkIconTheme *theme;
    GdkPixbuf *pixbuf;

    if (preview_icon_theme == NULL)
        preview_icon_theme = gtk_icon_theme_new();
    theme = preview_icon_theme;
    gtk_icon_theme_set_custom_theme(theme, theme_name);
    pixbuf = gtk_icon_theme_load_icon(theme,
                                      "folder",
                                      size,
                                      GTK_ICON_LOOKUP_FORCE_SIZE,
                                      NULL);
    return pixbuf;
}

GdkPixbuf *
atm_preview_load(AtmDiscoveryKind kind,
                 const gchar *theme_name,
                 gint width,
                 gint height)
{
    g_autofree gchar *base = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *key = NULL;
    GdkPixbuf *pixbuf = NULL;
    const gchar *suffix;

    if (theme_name == NULL || *theme_name == '\0')
        return NULL;

    ensure_preview_caches();
    key = g_strdup_printf("%u:%s:%d:%d",
                          (guint) kind,
                          theme_name,
                          width,
                          height);
    pixbuf = g_hash_table_lookup(preview_cache, key);
    if (pixbuf != NULL)
        return g_object_ref(pixbuf);
    if (g_hash_table_contains(preview_misses, key))
        return NULL;

    switch (kind) {
    case ATM_DISCOVERY_CINNAMON:
        suffix = "cinnamon";
        break;
    case ATM_DISCOVERY_GTK:
        suffix = "gtk-3.0";
        break;
    case ATM_DISCOVERY_ICONS:
        suffix = "icons";
        break;
    case ATM_DISCOVERY_CURSOR:
        suffix = "cursors";
        break;
    default:
        return NULL;
    }

    base = atm_discovery_get_path(kind, theme_name);
    path = find_existing_file(base, suffix, theme_name);
    if (path != NULL)
        pixbuf = gdk_pixbuf_new_from_file_at_scale(path,
                                                  width,
                                                  height,
                                                  TRUE,
                                                  NULL);
    if (pixbuf == NULL && kind == ATM_DISCOVERY_ICONS)
        pixbuf = load_icon_theme_preview(theme_name, MIN(width, height));

    if (pixbuf != NULL)
        g_hash_table_insert(preview_cache,
                            g_steal_pointer(&key),
                            g_object_ref(pixbuf));
    else
        g_hash_table_add(preview_misses, g_steal_pointer(&key));
    return pixbuf;
}
