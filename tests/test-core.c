#include "atm-applier.h"
#include "atm-components.h"
#include "atm-discovery.h"
#include "atm-store.h"
#include "atm-theme.h"

#include <glib/gstdio.h>

static gchar *test_root;

static gchar *
fixture_path(const gchar *name)
{
    return g_build_filename(TEST_SOURCE_DIR, "fixtures", name, NULL);
}

static void
test_theme_load_valid(void)
{
    g_autofree gchar *path = fixture_path("valid");
    g_autoptr(GError) error = NULL;
    g_autoptr(AtmTheme) theme = atm_theme_load(path, &error);
    AtmComponent components;

    g_assert_no_error(error);
    g_assert_nonnull(theme);
    g_assert_cmpstr(theme->id, ==, "test-night");
    g_assert_cmpstr(theme->color_scheme, ==, "prefer-dark");
    g_assert_cmpint(theme->cursor_size, ==, 28);
    components = atm_theme_get_components(theme);
    g_assert_true((components & ATM_COMPONENT_WALLPAPER) != 0);
    g_assert_true((components & ATM_COMPONENT_LOCK) != 0);
    g_assert_true((components & ATM_COMPONENT_APPEARANCE) != 0);
}

static void
test_theme_rejects_traversal(void)
{
    g_autofree gchar *path = fixture_path("traversal");
    g_autoptr(GError) error = NULL;
    g_autoptr(AtmTheme) theme = atm_theme_load(path, &error);

    g_assert_null(theme);
    g_assert_error(error, ATM_THEME_ERROR, ATM_THEME_ERROR_UNSAFE_PATH);
}

static void
test_component_selectors(void)
{
    g_autoptr(GError) error = NULL;
    AtmComponent components = ATM_COMPONENT_NONE;

    g_assert_true(atm_component_parse_list("1,applications,5,7", &components, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(components,
                     ==,
                     ATM_COMPONENT_WALLPAPER | ATM_COMPONENT_GTK |
                     ATM_COMPONENT_CURSOR | ATM_COMPONENT_APPEARANCE);

    components = ATM_COMPONENT_NONE;
    g_assert_false(atm_component_parse_list("2.2", &components, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_false(atm_component_parse_list("99", &components, &error));
    g_assert_nonnull(error);
}


static gboolean
ptr_array_contains_string(GPtrArray *array, const gchar *value)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        if (g_strcmp0(g_ptr_array_index(array, i), value) == 0)
            return TRUE;
    }
    return FALSE;
}

static void
test_discovery_and_missing_reference(void)
{
    g_autofree gchar *gtk_dir =
        g_build_filename(g_get_user_data_dir(),
                         "themes",
                         "DiscoveredGtk",
                         "gtk-3.0",
                         NULL);
    g_autofree gchar *gtk_css =
        g_build_filename(gtk_dir, "gtk.css", NULL);
    g_autoptr(GPtrArray) themes = NULL;
    g_autoptr(GError) error = NULL;
    AtmTheme missing = { 0 };

    g_assert_cmpint(g_mkdir_with_parents(gtk_dir, 0700), ==, 0);
    g_assert_true(g_file_set_contents(gtk_css,
                                     ".csstage { color: #fff; }\n",
                                     -1,
                                     &error));
    g_assert_no_error(error);

    themes = atm_discovery_list(ATM_DISCOVERY_GTK);
    g_assert_true(ptr_array_contains_string(themes, "DiscoveredGtk"));
    g_assert_true(atm_discovery_has(ATM_DISCOVERY_GTK, "DiscoveredGtk"));
    {
        g_autofree gchar *discovered =
            atm_discovery_get_path(ATM_DISCOVERY_GTK, "DiscoveredGtk");
        g_assert_nonnull(discovered);
        g_assert_true(g_str_has_suffix(discovered, "/DiscoveredGtk"));
    }
    g_assert_true(atm_discovery_gtk_has_lock_style("DiscoveredGtk"));

    missing.id = (gchar *) "missing-reference";
    missing.name = (gchar *) "Missing reference";
    missing.directory = test_root;
    missing.gtk_theme = (gchar *) "ThemeThatCannotExist";
    g_assert_false(atm_applier_apply(&missing, ATM_COMPONENT_GTK, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_store_priority(void)
{
    g_autofree gchar *fixtures = fixture_path("");
    g_autoptr(GPtrArray) themes = NULL;
    g_autoptr(GError) error = NULL;

    g_setenv("AXIONIS_THEME_SYSTEM_DIR", fixtures, TRUE);
    themes = atm_store_list(&error);
    g_assert_no_error(error);
    g_assert_nonnull(themes);
    g_assert_cmpuint(themes->len, ==, 2);
}

static void
test_store_rejects_executable_content(void)
{
    g_autofree gchar *path = fixture_path("unsafe");
    g_autoptr(GError) error = NULL;
    g_autoptr(AtmTheme) theme = atm_store_import(path, &error);

    g_assert_null(theme);
    g_assert_error(error, ATM_STORE_ERROR, ATM_STORE_ERROR_UNSAFE_CONTENT);
}

static void
test_store_imports_valid_bundle(void)
{
    g_autofree gchar *path = fixture_path("valid");
    g_autofree gchar *expected_root = atm_store_get_user_theme_dir();
    g_autofree gchar *expected_manifest = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(AtmTheme) theme = atm_store_import(path, &error);

    g_assert_no_error(error);
    g_assert_nonnull(theme);
    g_assert_cmpstr(theme->id, ==, "test-night");
    g_assert_true(g_str_has_prefix(theme->directory, expected_root));
    expected_manifest = g_build_filename(theme->directory, "theme.ini", NULL);
    g_assert_true(g_file_test(expected_manifest, G_FILE_TEST_IS_REGULAR));

    g_clear_pointer(&theme, atm_theme_free);
    theme = atm_store_import(path, &error);
    g_assert_null(theme);
    g_assert_error(error, ATM_STORE_ERROR, ATM_STORE_ERROR_ALREADY_EXISTS);
}

static void
test_apply_and_restore(void)
{
    g_autofree gchar *path = fixture_path("valid");
    g_autoptr(GError) error = NULL;
    g_autoptr(AtmTheme) theme = atm_theme_load(path, &error);
    g_autoptr(GSettings) interface = NULL;
    g_autoptr(GSettings) cinnamon = NULL;
    g_autoptr(GSettings) background = NULL;
    g_autoptr(GSettings) portal = NULL;
    g_autofree gchar *value = NULL;

    g_assert_no_error(error);
    g_assert_nonnull(theme);
    g_assert_true(atm_applier_apply(theme, ATM_COMPONENT_ALL, &error));
    g_assert_no_error(error);
    {
        g_autofree gchar *current = atm_applier_get_current_theme_id();
        g_assert_cmpstr(current, ==, "test-night");
    }

    interface = g_settings_new("org.cinnamon.desktop.interface");
    cinnamon = g_settings_new("org.cinnamon.theme");
    background = g_settings_new("org.cinnamon.desktop.background");
    portal = g_settings_new("org.x.apps.portal");

#define ASSERT_SETTING(settings, key, expected) \
    G_STMT_START { \
        g_autofree gchar *actual__ = g_settings_get_string(settings, key); \
        g_assert_cmpstr(actual__, ==, expected); \
    } G_STMT_END

    ASSERT_SETTING(interface, "gtk-theme", "FixtureGtk");
    ASSERT_SETTING(interface, "icon-theme", "FixtureIcons");
    ASSERT_SETTING(interface, "cursor-theme", "FixtureCursor");
    g_assert_cmpint(g_settings_get_int(interface, "cursor-size"), ==, 28);
    ASSERT_SETTING(cinnamon, "name", "FixtureCinnamon");
    ASSERT_SETTING(portal, "color-scheme", "prefer-dark");
    ASSERT_SETTING(portal, "accent-rgb", "#12B8D6");
    value = g_settings_get_string(background, "picture-uri");
    g_assert_true(g_str_has_suffix(value, "/1-background/wallpaper.svg"));
    {
        g_autofree gchar *gtk_link = g_build_filename(g_get_user_data_dir(),
                                                       "themes",
                                                       "FixtureGtk",
                                                       NULL);
        g_autofree gchar *icon_link = g_build_filename(g_get_user_data_dir(),
                                                        "icons",
                                                        "FixtureIcons",
                                                        NULL);
        g_assert_true(g_file_test(gtk_link, G_FILE_TEST_IS_SYMLINK));
        g_assert_true(g_file_test(icon_link, G_FILE_TEST_IS_SYMLINK));
    }

    g_assert_true(atm_applier_restore(&error));
    g_assert_no_error(error);
    ASSERT_SETTING(interface, "gtk-theme", "InitialGtk");
    ASSERT_SETTING(interface, "icon-theme", "InitialIcons");
    ASSERT_SETTING(interface, "cursor-theme", "InitialCursor");
    g_assert_cmpint(g_settings_get_int(interface, "cursor-size"), ==, 32);
    ASSERT_SETTING(cinnamon, "name", "InitialCinnamon");
    ASSERT_SETTING(background, "picture-uri", "file:///initial.svg");
    ASSERT_SETTING(portal, "color-scheme", "default");
    ASSERT_SETTING(portal, "accent-rgb", "");
    g_assert_null(atm_applier_get_current_theme_id());

    g_assert_true(atm_applier_apply(theme, ATM_COMPONENT_WALLPAPER, &error));
    g_assert_no_error(error);
    g_assert_null(atm_applier_get_current_theme_id());
    ASSERT_SETTING(interface, "gtk-theme", "InitialGtk");
    g_clear_pointer(&value, g_free);
    value = g_settings_get_string(background, "picture-uri");
    g_assert_true(g_str_has_suffix(value, "/1-background/wallpaper.svg"));
    g_assert_true(atm_applier_restore(&error));
    g_assert_no_error(error);
    ASSERT_SETTING(background, "picture-uri", "file:///initial.svg");
#undef ASSERT_SETTING
}

int
main(int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *data_home = NULL;
    g_autofree gchar *state_home = NULL;
    g_autofree gchar *bundle_home = NULL;

    test_root = g_dir_make_tmp("axionis-theme-tests-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(test_root);
    data_home = g_build_filename(test_root, "data", NULL);
    state_home = g_build_filename(test_root, "state", NULL);
    bundle_home = g_build_filename(data_home, "bundles", NULL);
    g_setenv("XDG_DATA_HOME", data_home, TRUE);
    g_setenv("XDG_STATE_HOME", state_home, TRUE);
    g_setenv("AXIONIS_THEME_USER_DIR", bundle_home, TRUE);
    g_setenv("AXIONIS_THEME_STATE_DIR", state_home, TRUE);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/theme/load-valid", test_theme_load_valid);
    g_test_add_func("/theme/reject-traversal", test_theme_rejects_traversal);
    g_test_add_func("/components/selectors", test_component_selectors);
    g_test_add_func("/discovery/installed-and-missing",
                    test_discovery_and_missing_reference);
    g_test_add_func("/store/priority", test_store_priority);
    g_test_add_func("/store/reject-executable", test_store_rejects_executable_content);
    g_test_add_func("/applier/apply-restore", test_apply_and_restore);
    g_test_add_func("/store/import-valid", test_store_imports_valid_bundle);
    return g_test_run();
}
