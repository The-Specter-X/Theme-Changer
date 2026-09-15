#include "atm-preview.h"

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

    theme = gtk_icon_theme_new();
    gtk_icon_theme_set_custom_theme(theme, theme_name);
    pixbuf = gtk_icon_theme_load_icon(theme,
                                      "folder",
                                      size,
                                      GTK_ICON_LOOKUP_FORCE_SIZE,
                                      NULL);
    g_object_unref(theme);
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
    const gchar *suffix;

    if (theme_name == NULL || *theme_name == '\0')
        return NULL;
    if (kind == ATM_DISCOVERY_ICONS)
        return load_icon_theme_preview(theme_name, MIN(width, height));

    switch (kind) {
    case ATM_DISCOVERY_CINNAMON:
        suffix = "cinnamon";
        break;
    case ATM_DISCOVERY_GTK:
        suffix = "gtk-3.0";
        break;
    case ATM_DISCOVERY_CURSOR:
        suffix = "cursors";
        break;
    default:
        return NULL;
    }

    base = atm_discovery_get_path(kind, theme_name);
    path = find_existing_file(base, suffix, theme_name);
    if (path == NULL)
        return NULL;
    return gdk_pixbuf_new_from_file_at_scale(path,
                                             width,
                                             height,
                                             TRUE,
                                             NULL);
}
