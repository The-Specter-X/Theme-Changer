#include "atm-window.h"

#include <xapp/libxapp/xapp-preferences-window.h>

#include "atm-applier.h"
#include "atm-components.h"
#include "atm-store.h"

typedef struct {
    GtkWidget *window;
    GtkWidget *list;
    GtkWidget *preview;
    GtkWidget *name;
    GtkWidget *description;
    GtkWidget *details;
    GtkWidget *apply_button;
    GPtrArray *themes;
    AtmTheme *selected;
} WindowData;

static void refresh_themes(WindowData *data);

static void
show_message(WindowData *data,
             GtkMessageType type,
             const gchar *primary,
             const gchar *secondary)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->window),
                                                GTK_DIALOG_MODAL |
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                type,
                                                GTK_BUTTONS_CLOSE,
                                                "%s",
                                                primary);
    if (secondary != NULL)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "%s",
                                                 secondary);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gchar *
component_summary(const AtmTheme *theme)
{
    AtmComponent components = atm_theme_get_components(theme);
    g_autoptr(GString) text = g_string_new(NULL);
    gsize length;
    const AtmComponentInfo *map = atm_component_map(&length);
    gsize i;

    for (i = 0; i < length; i++) {
        if (!map[i].independently_applied ||
            (components & map[i].component) == 0)
            continue;
        if (text->len > 0)
            g_string_append(text, "  •  ");
        g_string_append(text, map[i].title);
    }
    if ((components & ATM_COMPONENT_LOCK) != 0) {
        if (text->len > 0)
            g_string_append(text, "  •  ");
        g_string_append(text, "Screen lock");
    }
    return g_string_free(g_steal_pointer(&text), FALSE);
}

static void
set_preview(WindowData *data, const AtmTheme *theme)
{
    g_autofree gchar *preview_path = NULL;
    g_autofree gchar *summary = NULL;
    g_autofree gchar *meta = NULL;

    data->selected = (AtmTheme *) theme;
    gtk_label_set_text(GTK_LABEL(data->name), theme != NULL ? theme->name : "Select a theme");
    gtk_label_set_text(GTK_LABEL(data->description),
                       theme != NULL && theme->description != NULL ?
                       theme->description : "");
    gtk_widget_set_sensitive(data->apply_button, theme != NULL);

    if (theme == NULL) {
        gtk_image_clear(GTK_IMAGE(data->preview));
        gtk_label_set_text(GTK_LABEL(data->details), "");
        return;
    }

    summary = component_summary(theme);
    meta = g_strdup_printf("%s%s%s%s\n%s",
                           theme->author != NULL ? "By " : "",
                           theme->author != NULL ? theme->author : "",
                           theme->author != NULL && theme->version != NULL ? "  •  " : "",
                           theme->version != NULL ? theme->version : "",
                           summary);
    gtk_label_set_text(GTK_LABEL(data->details), meta);

    if (theme->preview != NULL)
        preview_path = atm_theme_resolve_path(theme, theme->preview, NULL);
    if (preview_path == NULL && theme->wallpaper != NULL)
        preview_path = atm_theme_resolve_path(theme, theme->wallpaper, NULL);

    if (preview_path != NULL) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new_from_file_at_scale(preview_path,
                                                                       560,
                                                                       320,
                                                                       TRUE,
                                                                       &error);
        if (pixbuf != NULL)
            gtk_image_set_from_pixbuf(GTK_IMAGE(data->preview), pixbuf);
        else
            gtk_image_set_from_icon_name(GTK_IMAGE(data->preview),
                                         "preferences-desktop-theme",
                                         GTK_ICON_SIZE_DIALOG);
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(data->preview),
                                     "preferences-desktop-theme",
                                     GTK_ICON_SIZE_DIALOG);
    }
}

static void
on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    WindowData *data = user_data;
    AtmTheme *theme;
    (void) box;

    if (row == NULL) {
        set_preview(data, NULL);
        return;
    }
    theme = g_object_get_data(G_OBJECT(row), "atm-theme");
    set_preview(data, theme);
}

static GtkWidget *
create_theme_row(AtmTheme *theme, gboolean current)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *title = gtk_label_new(NULL);
    GtkWidget *subtitle = gtk_label_new(theme->description);
    g_autofree gchar *markup = NULL;

    markup = g_markup_printf_escaped("<b>%s</b>%s",
                                     theme->name,
                                     current ? "  ✓" : "");
    gtk_label_set_markup(GTK_LABEL(title), markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), subtitle, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    g_object_set_data(G_OBJECT(row), "atm-theme", theme);
    gtk_widget_show_all(row);
    return row;
}

static void
clear_list(GtkWidget *list)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    GList *cursor;

    for (cursor = children; cursor != NULL; cursor = cursor->next)
        gtk_widget_destroy(GTK_WIDGET(cursor->data));
    g_list_free(children);
}

static void
refresh_themes(WindowData *data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *current = atm_applier_get_current_theme_id();
    guint i;

    data->selected = NULL;
    clear_list(data->list);
    g_clear_pointer(&data->themes, g_ptr_array_unref);
    data->themes = atm_store_list(&error);
    if (data->themes == NULL) {
        show_message(data, GTK_MESSAGE_ERROR, "Could not load themes", error->message);
        return;
    }
    for (i = 0; i < data->themes->len; i++) {
        AtmTheme *theme = g_ptr_array_index(data->themes, i);
        GtkWidget *row = create_theme_row(theme, g_strcmp0(theme->id, current) == 0);
        gtk_container_add(GTK_CONTAINER(data->list), row);
        if (data->selected == NULL || g_strcmp0(theme->id, current) == 0) {
            data->selected = theme;
            gtk_list_box_select_row(GTK_LIST_BOX(data->list), GTK_LIST_BOX_ROW(row));
        }
    }
    if (data->themes->len == 0)
        set_preview(data, NULL);
}

static void
on_apply_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    g_autoptr(GError) error = NULL;
    (void) button;

    if (data->selected == NULL)
        return;
    if (!atm_applier_apply(data->selected, ATM_COMPONENT_ALL, &error)) {
        show_message(data, GTK_MESSAGE_ERROR, "The theme could not be applied", error->message);
        return;
    }
    show_message(data,
                 GTK_MESSAGE_INFO,
                 "Theme applied",
                 "Already-running applications may need to be restarted.");
    refresh_themes(data);
}

static void
on_restore_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    g_autoptr(GError) error = NULL;
    (void) button;

    if (!atm_applier_restore(&error)) {
        show_message(data, GTK_MESSAGE_ERROR, "The previous theme could not be restored", error->message);
        return;
    }
    show_message(data, GTK_MESSAGE_INFO, "Previous appearance restored", NULL);
    refresh_themes(data);
}

static void
on_import_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    GtkWidget *dialog;
    (void) button;

    dialog = gtk_file_chooser_dialog_new("Import an Axionis theme",
                                         GTK_WINDOW(data->window),
                                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Import", GTK_RESPONSE_ACCEPT,
                                         NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_autofree gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        g_autoptr(GError) error = NULL;
        g_autoptr(AtmTheme) theme = atm_store_import(path, &error);

        if (theme == NULL)
            show_message(data, GTK_MESSAGE_ERROR, "The theme could not be imported", error->message);
        else {
            show_message(data, GTK_MESSAGE_INFO, "Theme imported", theme->name);
            refresh_themes(data);
        }
    }
    gtk_widget_destroy(dialog);
}

static void
window_data_free(WindowData *data)
{
    g_clear_pointer(&data->themes, g_ptr_array_unref);
    g_free(data);
}

GtkWidget *
atm_window_new(GtkApplication *application)
{
    WindowData *data = g_new0(WindowData, 1);
    XAppPreferencesWindow *window = xapp_preferences_window_new();
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *import_button = gtk_button_new_with_label("Import Theme…");
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Previous");

    data->window = GTK_WIDGET(window);
    data->list = gtk_list_box_new();
    data->preview = gtk_image_new_from_icon_name("preferences-desktop-theme",
                                                 GTK_ICON_SIZE_DIALOG);
    data->name = gtk_label_new("Select a theme");
    data->description = gtk_label_new("");
    data->details = gtk_label_new("");
    data->apply_button = gtk_button_new_with_label("Apply Theme");

    gtk_window_set_application(GTK_WINDOW(window), application);
    gtk_window_set_title(GTK_WINDOW(window), "Axionis Theme Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 920, 600);
    gtk_window_set_icon_name(GTK_WINDOW(window), "io.axionis.ThemeManager");
    gtk_container_set_border_width(GTK_CONTAINER(page), 18);

    gtk_list_box_set_selection_mode(GTK_LIST_BOX(data->list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(data->list), TRUE);
    gtk_widget_set_size_request(sidebar_scroll, 300, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sidebar_scroll), data->list);

    gtk_label_set_xalign(GTK_LABEL(data->name), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(data->description), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(data->details), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(data->description), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(data->details), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(data->name), "title");
    gtk_style_context_add_class(gtk_widget_get_style_context(data->details),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start(GTK_BOX(header), data->name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), data->description, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), data->preview, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), data->details, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(content), button_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(button_box), import_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), restore_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(button_box), data->apply_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), sidebar_scroll, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), content, TRUE, TRUE, 0);
    xapp_preferences_window_add_page(window, page, "themes", "Themes");

    g_signal_connect(data->list, "row-selected", G_CALLBACK(on_row_selected), data);
    g_signal_connect(data->apply_button, "clicked", G_CALLBACK(on_apply_clicked), data);
    g_signal_connect(import_button, "clicked", G_CALLBACK(on_import_clicked), data);
    g_signal_connect(restore_button, "clicked", G_CALLBACK(on_restore_clicked), data);
    g_object_set_data_full(G_OBJECT(window), "atm-window-data", data,
                           (GDestroyNotify) window_data_free);

    refresh_themes(data);
    gtk_widget_show_all(GTK_WIDGET(window));
    return GTK_WIDGET(window);
}
