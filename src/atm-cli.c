#include "config.h"
#include "atm-applier.h"
#include "atm-components.h"
#include "atm-store.h"

#include <stdio.h>

static gboolean option_list;
static gboolean option_json;
static gboolean option_map;
static gboolean option_restore;
static gboolean option_version;
static gchar *option_validate;
static gchar *option_import;
static gchar *option_apply;
static gchar *option_only;

static GOptionEntry options[] = {
    { "list",     'l', 0, G_OPTION_ARG_NONE,   &option_list,     "List available themes", NULL },
    { "json",     'j', 0, G_OPTION_ARG_NONE,   &option_json,     "Use machine-readable JSON", NULL },
    { "map",       0,  0, G_OPTION_ARG_NONE,   &option_map,      "Print the stable component map", NULL },
    { "validate", 'v', 0, G_OPTION_ARG_FILENAME, &option_validate, "Validate a theme directory", "DIRECTORY" },
    { "import",   'i', 0, G_OPTION_ARG_FILENAME, &option_import,   "Safely import a theme directory", "DIRECTORY" },
    { "apply",    'a', 0, G_OPTION_ARG_STRING, &option_apply,    "Apply a theme by ID", "ID" },
    { "only",     'o', 0, G_OPTION_ARG_STRING, &option_only,     "Apply top-level component numbers or IDs", "LIST" },
    { "restore",  'r', 0, G_OPTION_ARG_NONE,   &option_restore,  "Restore the previous appearance", NULL },
    { "version",   0,  0, G_OPTION_ARG_NONE,   &option_version,  "Print the program version", NULL },
    { NULL }
};

static gint
count_actions(void)
{
    return (option_list ? 1 : 0) + (option_map ? 1 : 0) +
           (option_validate != NULL ? 1 : 0) +
           (option_import != NULL ? 1 : 0) +
           (option_apply != NULL ? 1 : 0) + (option_restore ? 1 : 0) +
           (option_version ? 1 : 0);
}

static void
print_map(void)
{
    gsize length;
    const AtmComponentInfo *map = atm_component_map(&length);
    gsize i;

    if (option_json) {
        g_autofree gchar *json = atm_component_map_json();
        g_print("%s\n", json);
        return;
    }
    for (i = 0; i < length; i++)
        g_print("%-4s %-24s %s%s\n",
                map[i].number,
                map[i].id,
                map[i].title,
                map[i].independently_applied ? "" : " (authoring selector)");
}

static gboolean
list_themes(GError **error)
{
    g_autoptr(GPtrArray) themes = atm_store_list(error);
    guint i;

    if (themes == NULL)
        return FALSE;
    if (option_json)
        g_print("[\n");
    for (i = 0; i < themes->len; i++) {
        AtmTheme *theme = g_ptr_array_index(themes, i);
        if (option_json) {
            g_autofree gchar *json = atm_theme_to_json(theme);
            g_print("  %s%s\n", json, i + 1 == themes->len ? "" : ",");
        } else {
            g_print("%-24s %s\n", theme->id, theme->name);
        }
    }
    if (option_json)
        g_print("]\n");
    return TRUE;
}

int
main(int argc, char **argv)
{
    g_autoptr(GOptionContext) context = NULL;
    g_autoptr(GError) error = NULL;
    gboolean ok = FALSE;

    context = g_option_context_new("— manage Axionis appearance bundles");
    g_option_context_add_main_entries(context, options, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("axionis-theme: %s\n", error->message);
        return 2;
    }
    if (count_actions() != 1) {
        g_autofree gchar *help = g_option_context_get_help(context, TRUE, NULL);
        g_printerr("Choose exactly one action.\n\n%s", help);
        return 2;
    }

    if (option_version) {
        g_print("axionis-theme %s\n", ATM_VERSION);
        return 0;
    }
    if (option_map) {
        print_map();
        return 0;
    }
    if (option_list)
        ok = list_themes(&error);
    else if (option_validate != NULL) {
        g_autoptr(AtmTheme) theme = atm_theme_load(option_validate, &error);
        ok = theme != NULL;
        if (ok) {
            if (option_json) {
                g_autofree gchar *json = atm_theme_to_json(theme);
                g_print("%s\n", json);
            } else {
                g_print("Valid Axionis theme: %s (%s)\n", theme->name, theme->id);
            }
        }
    } else if (option_apply != NULL) {
        g_autoptr(AtmTheme) theme = atm_store_find(option_apply, &error);
        AtmComponent components = ATM_COMPONENT_ALL;
        if (theme != NULL &&
            atm_component_parse_list(option_only, &components, &error))
            ok = atm_applier_apply(theme, components, &error);
    } else if (option_import != NULL) {
        g_autoptr(AtmTheme) theme = atm_store_import(option_import, &error);
        ok = theme != NULL;
        if (ok) {
            if (option_json) {
                g_autofree gchar *json = atm_theme_to_json(theme);
                g_print("%s\n", json);
            } else {
                g_print("Imported Axionis theme: %s (%s)\n", theme->name, theme->id);
            }
        }
    } else if (option_restore)
        ok = atm_applier_restore(&error);

    if (!ok) {
        g_printerr("axionis-theme: %s\n",
                   error != NULL ? error->message : "operation failed");
        return 1;
    }
    return 0;
}
