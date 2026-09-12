#include "atm-components.h"

static const AtmComponentInfo component_map[] = {
    { "1",   "wallpaper",             "Wallpaper",             "Desktop background image",                         ATM_COMPONENT_WALLPAPER,  TRUE  },
    { "2",   "desktop",               "Cinnamon desktop",      "Cinnamon shell theme as one apply unit",           ATM_COMPONENT_CINNAMON,   TRUE  },
    { "2.1", "desktop.panel",         "Panel",                 "Panel colors, borders and states",                 ATM_COMPONENT_CINNAMON,   FALSE },
    { "2.2", "desktop.menu",          "Menu",                  "Menu, search and category styling",                ATM_COMPONENT_CINNAMON,   FALSE },
    { "2.3", "desktop.tray",          "Tray",                  "Status and tray icon containers",                  ATM_COMPONENT_CINNAMON,   FALSE },
    { "2.4", "desktop.notifications", "Notifications",         "Notification banners and action buttons",          ATM_COMPONENT_CINNAMON,   FALSE },
    { "2.5", "desktop.popovers",      "Calendar and popovers", "Calendar and applet popup styling",                ATM_COMPONENT_CINNAMON,   FALSE },
    { "2.6", "desktop.osd",           "OSD and dialogs",       "Volume, brightness and Cinnamon dialog styling",   ATM_COMPONENT_CINNAMON,   FALSE },
    { "3",   "applications",          "Applications",          "GTK 3 application and XApp theme",                 ATM_COMPONENT_GTK,        TRUE  },
    { "3.1", "applications.controls", "Controls",              "Buttons, entries, lists and other controls",        ATM_COMPONENT_GTK,        FALSE },
    { "3.2", "applications.windows",  "Windows",               "Header bars, title bars, borders and shadows",      ATM_COMPONENT_GTK,        FALSE },
    { "4",   "icons",                 "Icons",                 "Application, file-manager and symbolic icons",     ATM_COMPONENT_ICONS,      TRUE  },
    { "5",   "cursor",                "Cursor",                "Cursor theme and size",                            ATM_COMPONENT_CURSOR,     TRUE  },
    { "5.1", "cursor.theme",          "Cursor theme",          "Cursor artwork",                                   ATM_COMPONENT_CURSOR,     FALSE },
    { "5.2", "cursor.size",           "Cursor size",           "Logical cursor size",                              ATM_COMPONENT_CURSOR,     FALSE },
    { "6",   "lock",                  "Screen lock",           "Cinnamon Screensaver styling supplied by GTK CSS", ATM_COMPONENT_LOCK,       FALSE },
    { "6.1", "lock.stage",            "Lock stage",            "Lock-screen foreground and overlay",               ATM_COMPONENT_LOCK,       FALSE },
    { "6.2", "lock.unlock",           "Unlock controls",       "Password entry and unlock controls",               ATM_COMPONENT_LOCK,       FALSE },
    { "6.3", "lock.status",           "Clock and status",      "Clock, media and status elements",                  ATM_COMPONENT_LOCK,       FALSE },
    { "7",   "appearance",            "Appearance preference", "Light/dark preference and accent color",            ATM_COMPONENT_APPEARANCE, TRUE  },
    { "7.1", "appearance.mode",       "Light/dark preference", "Preference advertised to compatible applications", ATM_COMPONENT_APPEARANCE, FALSE },
    { "7.2", "appearance.accent",     "Accent color",          "Preferred RGB accent color",                       ATM_COMPONENT_APPEARANCE, FALSE },
};

GQuark
atm_components_error_quark(void)
{
    return g_quark_from_static_string("atm-components-error");
}

const AtmComponentInfo *
atm_component_map(gsize *length)
{
    if (length != NULL)
        *length = G_N_ELEMENTS(component_map);
    return component_map;
}

const AtmComponentInfo *
atm_component_lookup(const gchar *selector)
{
    gsize i;

    if (selector == NULL)
        return NULL;

    for (i = 0; i < G_N_ELEMENTS(component_map); i++) {
        if (g_str_equal(selector, component_map[i].number) ||
            g_str_equal(selector, component_map[i].id))
            return &component_map[i];
    }
    return NULL;
}

gboolean
atm_component_parse_list(const gchar *text,
                         AtmComponent *components,
                         GError **error)
{
    g_auto(GStrv) fields = NULL;
    AtmComponent parsed = ATM_COMPONENT_NONE;
    guint i;

    g_return_val_if_fail(components != NULL, FALSE);

    if (text == NULL || *text == '\0') {
        *components = ATM_COMPONENT_ALL;
        return TRUE;
    }

    fields = g_strsplit(text, ",", -1);
    for (i = 0; fields[i] != NULL; i++) {
        const AtmComponentInfo *info;
        g_strstrip(fields[i]);
        info = atm_component_lookup(fields[i]);
        if (info == NULL) {
            g_set_error(error,
                        atm_components_error_quark(),
                        1,
                        "Unknown component selector '%s'",
                        fields[i]);
            return FALSE;
        }
        if (!info->independently_applied) {
            g_set_error(error,
                        atm_components_error_quark(),
                        2,
                        "Component %s (%s) is an authoring selector; apply its parent component %u",
                        info->number,
                        info->title,
                        info->component == ATM_COMPONENT_CINNAMON ? 2u :
                        info->component == ATM_COMPONENT_GTK ? 3u :
                        info->component == ATM_COMPONENT_CURSOR ? 5u :
                        info->component == ATM_COMPONENT_LOCK ? 6u : 7u);
            return FALSE;
        }
        parsed |= info->component;
    }

    *components = parsed;
    return TRUE;
}

static void
append_json_string(GString *output, const gchar *text)
{
    const guchar *cursor = (const guchar *) text;

    g_string_append_c(output, '"');
    for (; *cursor != '\0'; cursor++) {
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
atm_component_map_json(void)
{
    g_autoptr(GString) output = g_string_new("[\n");
    gsize i;

    for (i = 0; i < G_N_ELEMENTS(component_map); i++) {
        const AtmComponentInfo *info = &component_map[i];
        g_string_append(output, "  {\"number\":");
        append_json_string(output, info->number);
        g_string_append(output, ",\"id\":");
        append_json_string(output, info->id);
        g_string_append(output, ",\"title\":");
        append_json_string(output, info->title);
        g_string_append(output, ",\"description\":");
        append_json_string(output, info->description);
        g_string_append_printf(output,
                               ",\"independently_applied\":%s}%s\n",
                               info->independently_applied ? "true" : "false",
                               i + 1 == G_N_ELEMENTS(component_map) ? "" : ",");
    }
    g_string_append_c(output, ']');
    return g_string_free(g_steal_pointer(&output), FALSE);
}
