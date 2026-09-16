#include "atm-window.h"

#include <gtk/gtk.h>

static void
drain_events(void)
{
    while (gtk_events_pending())
        gtk_main_iteration();
}

static GtkWidget *
find_named_widget(GtkWidget *widget, const gchar *name)
{
    GList *children;
    GList *cursor;

    if (g_strcmp0(gtk_widget_get_name(widget), name) == 0)
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;

    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (cursor = children; cursor != NULL; cursor = cursor->next) {
        GtkWidget *found =
            find_named_widget(GTK_WIDGET(cursor->data), name);
        if (found != NULL) {
            g_list_free(children);
            return found;
        }
    }
    g_list_free(children);
    return NULL;
}

static GtkWidget *
find_flow_box(GtkWidget *widget)
{
    GList *children;
    GList *cursor;

    if (GTK_IS_FLOW_BOX(widget))
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;

    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (cursor = children; cursor != NULL; cursor = cursor->next) {
        GtkWidget *found = find_flow_box(GTK_WIDGET(cursor->data));
        if (found != NULL) {
            g_list_free(children);
            return found;
        }
    }
    g_list_free(children);
    return NULL;
}

int
main(int argc, char **argv)
{
    g_autoptr(GtkApplication) application = NULL;
    g_autoptr(GError) error = NULL;
    GtkWidget *window;
    GtkWidget *chooser;
    GtkWidget *cursor_chooser;
    GtkPopover *popover;
    GtkWidget *content;
    GtkWidget *flowbox;
    GList *tiles;
    gint minimum_width;
    gint natural_width;
    gint minimum_height;
    gint natural_height;

    gtk_init(&argc, &argv);
    application = gtk_application_new("io.axionis.ThemeManager.UiTest",
                                      G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(application),
                                         NULL,
                                         &error));
    g_assert_no_error(error);

    window = atm_window_new(application);
    gtk_widget_show(window);
    drain_events();

    chooser = find_named_widget(window, "theme-preview-chooser");
    g_assert_nonnull(chooser);
    g_assert_true(GTK_IS_MENU_BUTTON(chooser));

    cursor_chooser = find_named_widget(window, "cursor-theme-chooser");
    g_assert_nonnull(cursor_chooser);
    g_assert_true(GTK_IS_MENU_BUTTON(cursor_chooser));
    gtk_widget_get_size_request(cursor_chooser,
                                &minimum_width,
                                &minimum_height);
    g_assert_cmpint(minimum_width, ==, 210);
    g_assert_cmpint(minimum_height, ==, 56);

    popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(chooser));
    g_assert_nonnull(popover);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chooser), TRUE);
    drain_events();

    content = gtk_bin_get_child(GTK_BIN(popover));
    g_assert_nonnull(content);
    g_assert_true(gtk_widget_get_visible(content));

    gtk_widget_get_preferred_width(content,
                                   &minimum_width,
                                   &natural_width);
    gtk_widget_get_preferred_height(content,
                                    &minimum_height,
                                    &natural_height);
    g_assert_cmpint(minimum_width, >=, 650);
    g_assert_cmpint(natural_width, >=, 650);
    g_assert_cmpint(minimum_height, >=, 380);
    g_assert_cmpint(natural_height, >=, 380);

    flowbox = find_flow_box(content);
    g_assert_nonnull(flowbox);
    g_assert_true(gtk_widget_get_visible(flowbox));
    tiles = gtk_container_get_children(GTK_CONTAINER(flowbox));
    g_assert_nonnull(tiles);
    g_list_free(tiles);

    gtk_popover_popdown(popover);
    gtk_widget_destroy(window);
    drain_events();
    return 0;
}
