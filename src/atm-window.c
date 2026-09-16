#include "atm-window.h"

#include <xapp/libxapp/xapp-preferences-window.h>

#include "atm-applier.h"
#include "atm-components.h"
#include "atm-discovery.h"
#include "atm-preview.h"
#include "atm-store.h"

typedef struct {
    AtmComponent component;
    const gchar *label;
} BundleOption;

static const BundleOption bundle_options[] = {
    { ATM_COMPONENT_WALLPAPER,  "1  Wallpaper" },
    { ATM_COMPONENT_CINNAMON,   "2  Cinnamon desktop" },
    { ATM_COMPONENT_GTK,        "3  Applications / 6 Screen lock" },
    { ATM_COMPONENT_ICONS,      "4  Icons" },
    { ATM_COMPONENT_CURSOR,     "5  Cursor and size" },
    { ATM_COMPONENT_APPEARANCE, "7  Light/dark and accent" },
};

typedef struct _ThemeChooser ThemeChooser;
typedef void (*ThemeChooserChanged)(ThemeChooser *chooser,
                                    gpointer user_data);

struct _ThemeChooser {
    AtmDiscoveryKind kind;
    GtkWidget *button;
    GtkWidget *button_image;
    GtkWidget *button_label;
    GtkWidget *popover;
    GtkWidget *scroll;
    GtkWidget *flowbox;
    GPtrArray *available;
    GPtrArray *tiles;
    gchar *selected;
    guint preview_index;
    guint idle_id;
    gboolean loaded;
    ThemeChooserChanged changed;
    gpointer changed_data;
};

typedef struct {
    GtkWidget *window;

    GtkWidget *bundle_list;
    GtkWidget *preview;
    GtkWidget *bundle_name;
    GtkWidget *description;
    GtkWidget *details;
    GtkWidget *bundle_status;
    GtkWidget *apply_bundle_button;
    GtkWidget *bundle_checks[G_N_ELEMENTS(bundle_options)];
    GPtrArray *themes;
    AtmTheme *selected;

    ThemeChooser *cinnamon_chooser;
    ThemeChooser *gtk_chooser;
    ThemeChooser *icon_chooser;
    ThemeChooser *cursor_chooser;
    GtkWidget *cursor_size;
    GtkWidget *wallpaper_button;
    GtkWidget *wallpaper_preview;
    GtkWidget *mode_combo;
    GtkWidget *accent_switch;
    GtkWidget *accent_button;
    GtkWidget *lock_status;
    GHashTable *lock_style_cache;
} WindowData;

#define PREVIEW_LOAD_INTERVAL_MS 32
#define CURSOR_CHOOSER_WIDTH 210
#define CURSOR_CHOOSER_HEIGHT 56

static void refresh_themes(WindowData *data);
static void load_current_controls(WindowData *data);

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

static GtkWidget *
make_heading(const gchar *title, const gchar *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *subtitle_label = gtk_label_new(subtitle);

    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(subtitle_label), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(title_label),
                                "title");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle_label),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), subtitle_label, FALSE, FALSE, 0);
    return box;
}

static GtkWidget *
make_preference_row(const gchar *title,
                    const gchar *subtitle,
                    GtkWidget *control)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *subtitle_label = gtk_label_new(subtitle);

    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(subtitle_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(subtitle_label), 58);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle_label),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_widget_set_valign(control, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(labels), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels), subtitle_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), labels, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), control, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    return row;
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

static gboolean
external_component_available(const AtmTheme *theme, AtmComponent component)
{
    switch (component) {
    case ATM_COMPONENT_CINNAMON:
        return theme->cinnamon_path != NULL ||
               atm_discovery_has(ATM_DISCOVERY_CINNAMON,
                                 theme->cinnamon_theme);
    case ATM_COMPONENT_GTK:
        return theme->gtk_path != NULL ||
               atm_discovery_has(ATM_DISCOVERY_GTK, theme->gtk_theme);
    case ATM_COMPONENT_ICONS:
        return theme->icon_path != NULL ||
               atm_discovery_has(ATM_DISCOVERY_ICONS, theme->icon_theme);
    case ATM_COMPONENT_CURSOR:
        return theme->cursor_path != NULL ||
               atm_discovery_has(ATM_DISCOVERY_CURSOR, theme->cursor_theme);
    default:
        return TRUE;
    }
}

static void
set_bundle_preview(WindowData *data, const AtmTheme *theme)
{
    g_autofree gchar *preview_path = NULL;
    g_autofree gchar *summary = NULL;
    g_autofree gchar *meta = NULL;
    g_autoptr(GString) missing = g_string_new(NULL);
    AtmComponent components = theme != NULL ?
                              atm_theme_get_components(theme) :
                              ATM_COMPONENT_NONE;
    guint i;

    data->selected = (AtmTheme *) theme;
    gtk_label_set_text(GTK_LABEL(data->bundle_name),
                       theme != NULL ? theme->name : "Select a theme");
    gtk_label_set_text(GTK_LABEL(data->description),
                       theme != NULL && theme->description != NULL ?
                       theme->description : "");
    gtk_widget_set_sensitive(data->apply_bundle_button, theme != NULL);

    for (i = 0; i < G_N_ELEMENTS(bundle_options); i++) {
        gboolean present = (components & bundle_options[i].component) != 0;
        gtk_widget_set_sensitive(data->bundle_checks[i], present);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->bundle_checks[i]),
                                     present);
        if (theme != NULL && present &&
            !external_component_available(theme,
                                          bundle_options[i].component)) {
            if (missing->len > 0)
                g_string_append(missing, ", ");
            g_string_append(missing, bundle_options[i].label + 3);
        }
    }

    if (theme == NULL) {
        gtk_image_clear(GTK_IMAGE(data->preview));
        gtk_label_set_text(GTK_LABEL(data->details), "");
        gtk_label_set_text(GTK_LABEL(data->bundle_status), "");
        return;
    }

    summary = component_summary(theme);
    meta = g_strdup_printf("%s%s%s%s\n%s",
                           theme->author != NULL ? "By " : "",
                           theme->author != NULL ? theme->author : "",
                           theme->author != NULL && theme->version != NULL ?
                           "  •  " : "",
                           theme->version != NULL ? theme->version : "",
                           summary);
    gtk_label_set_text(GTK_LABEL(data->details), meta);

    if (missing->len > 0) {
        g_autofree gchar *warning =
            g_strdup_printf("Missing system components: %s", missing->str);
        gtk_label_set_text(GTK_LABEL(data->bundle_status), warning);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(data->bundle_status), "error");
    } else {
        gtk_label_set_text(GTK_LABEL(data->bundle_status),
                           "All selected components are available.");
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(data->bundle_status), "error");
    }

    if (theme->preview != NULL)
        preview_path = atm_theme_resolve_path(theme, theme->preview, NULL);
    if (preview_path == NULL && theme->wallpaper != NULL)
        preview_path = atm_theme_resolve_path(theme, theme->wallpaper, NULL);

    if (preview_path != NULL) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GdkPixbuf) pixbuf =
            gdk_pixbuf_new_from_file_at_scale(preview_path,
                                              560,
                                              300,
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
        set_bundle_preview(data, NULL);
        return;
    }
    theme = g_object_get_data(G_OBJECT(row), "atm-theme");
    set_bundle_preview(data, theme);
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
    clear_list(data->bundle_list);
    g_clear_pointer(&data->themes, g_ptr_array_unref);
    data->themes = atm_store_list(&error);
    if (data->themes == NULL) {
        show_message(data,
                     GTK_MESSAGE_ERROR,
                     "Could not load themes",
                     error->message);
        return;
    }

    for (i = 0; i < data->themes->len; i++) {
        AtmTheme *theme = g_ptr_array_index(data->themes, i);
        GtkWidget *row =
            create_theme_row(theme, g_strcmp0(theme->id, current) == 0);
        gtk_container_add(GTK_CONTAINER(data->bundle_list), row);
        if (data->selected == NULL ||
            g_strcmp0(theme->id, current) == 0) {
            data->selected = theme;
            gtk_list_box_select_row(GTK_LIST_BOX(data->bundle_list),
                                    GTK_LIST_BOX_ROW(row));
        }
    }
    if (data->themes->len == 0)
        set_bundle_preview(data, NULL);
}

static void
on_apply_bundle_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    g_autoptr(GError) error = NULL;
    AtmComponent requested = ATM_COMPONENT_NONE;
    guint i;
    (void) button;

    if (data->selected == NULL)
        return;
    for (i = 0; i < G_N_ELEMENTS(bundle_options); i++) {
        if (gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(data->bundle_checks[i])))
            requested |= bundle_options[i].component;
    }
    if (requested == ATM_COMPONENT_NONE) {
        show_message(data,
                     GTK_MESSAGE_WARNING,
                     "Choose at least one component",
                     "Enable the parts of this theme that you want to apply.");
        return;
    }

    if (!atm_applier_apply(data->selected, requested, &error)) {
        show_message(data,
                     GTK_MESSAGE_ERROR,
                     "The theme could not be applied",
                     error->message);
        return;
    }
    show_message(data,
                 GTK_MESSAGE_INFO,
                 "Theme components applied",
                 "Already-running applications may need to be restarted.");
    refresh_themes(data);
    load_current_controls(data);
}

static void
on_select_all_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    guint i;
    (void) button;

    for (i = 0; i < G_N_ELEMENTS(bundle_options); i++) {
        if (gtk_widget_get_sensitive(data->bundle_checks[i]))
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(data->bundle_checks[i]), TRUE);
    }
}

static void
on_clear_selection_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    guint i;
    (void) button;

    for (i = 0; i < G_N_ELEMENTS(bundle_options); i++)
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(data->bundle_checks[i]), FALSE);
}

static void
on_restore_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    g_autoptr(GError) error = NULL;
    (void) button;

    if (!atm_applier_restore(&error)) {
        show_message(data,
                     GTK_MESSAGE_ERROR,
                     "The previous appearance could not be restored",
                     error->message);
        return;
    }
    show_message(data,
                 GTK_MESSAGE_INFO,
                 "Previous appearance restored",
                 NULL);
    refresh_themes(data);
    load_current_controls(data);
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
        g_autofree gchar *path =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        g_autoptr(GError) error = NULL;
        g_autoptr(AtmTheme) theme = atm_store_import(path, &error);

        if (theme == NULL)
            show_message(data,
                         GTK_MESSAGE_ERROR,
                         "The theme could not be imported",
                         error->message);
        else {
            show_message(data,
                         GTK_MESSAGE_INFO,
                         "Theme imported",
                         theme->name);
            refresh_themes(data);
        }
    }
    gtk_widget_destroy(dialog);
}

static gboolean
array_contains(GPtrArray *array, const gchar *value)
{
    guint i;

    for (i = 0; value != NULL && i < array->len; i++) {
        if (g_strcmp0(g_ptr_array_index(array, i), value) == 0)
            return TRUE;
    }
    return FALSE;
}

static const gchar *
fallback_icon_name(AtmDiscoveryKind kind)
{
    switch (kind) {
    case ATM_DISCOVERY_CINNAMON:
        return "preferences-desktop-theme";
    case ATM_DISCOVERY_GTK:
        return "applications-other";
    case ATM_DISCOVERY_ICONS:
        return "folder";
    case ATM_DISCOVERY_CURSOR:
        return "input-mouse";
    default:
        return "image-missing";
    }
}

static void
set_image_preview(GtkWidget *image,
                  AtmDiscoveryKind kind,
                  const gchar *name,
                  gint width,
                  gint height)
{
    g_autoptr(GdkPixbuf) pixbuf =
        atm_preview_load(kind, name, width, height);

    if (pixbuf != NULL)
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
    else {
        gtk_image_set_from_icon_name(GTK_IMAGE(image),
                                     fallback_icon_name(kind),
                                     GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(image), MIN(width, height));
    }
}

static void
theme_chooser_update_tiles(ThemeChooser *chooser)
{
    guint i;

    for (i = 0; i < chooser->tiles->len; i++) {
        GtkWidget *button = g_ptr_array_index(chooser->tiles, i);
        const gchar *name = g_object_get_data(G_OBJECT(button),
                                              "theme-name");
        GtkStyleContext *context = gtk_widget_get_style_context(button);

        if (g_strcmp0(name, chooser->selected) == 0)
            gtk_style_context_add_class(context, "suggested-action");
        else
            gtk_style_context_remove_class(context, "suggested-action");
    }
}

static void
theme_chooser_set_selected(ThemeChooser *chooser,
                           const gchar *name,
                           gboolean notify)
{
    if (g_strcmp0(chooser->selected, name) == 0 && !notify)
        return;

    g_free(chooser->selected);
    chooser->selected = g_strdup(name);
    gtk_label_set_text(GTK_LABEL(chooser->button_label),
                       name != NULL ? name : "Choose a theme");
    gtk_widget_set_tooltip_text(chooser->button, name);
    set_image_preview(chooser->button_image,
                      chooser->kind,
                      name,
                      chooser->kind == ATM_DISCOVERY_CURSOR ? 36 :
                      chooser->kind == ATM_DISCOVERY_ICONS ? 48 : 72,
                      chooser->kind == ATM_DISCOVERY_CURSOR ? 36 : 48);
    theme_chooser_update_tiles(chooser);

    if (notify && chooser->changed != NULL)
        chooser->changed(chooser, chooser->changed_data);
}

static void
on_theme_tile_clicked(GtkButton *button, gpointer user_data)
{
    ThemeChooser *chooser = user_data;
    const gchar *name = g_object_get_data(G_OBJECT(button),
                                          "theme-name");

    theme_chooser_set_selected(chooser, name, TRUE);
    gtk_popover_popdown(GTK_POPOVER(chooser->popover));
}

static gboolean
theme_chooser_load_next_preview(gpointer user_data)
{
    ThemeChooser *chooser = user_data;
    GtkWidget *button;
    GtkWidget *image;
    const gchar *name;
    gint width;
    gint height;

    if (!gtk_widget_get_visible(chooser->popover) ||
        chooser->preview_index >= chooser->tiles->len) {
        chooser->idle_id = 0;
        return G_SOURCE_REMOVE;
    }

    button = g_ptr_array_index(chooser->tiles, chooser->preview_index++);
    image = g_object_get_data(G_OBJECT(button), "preview-image");
    name = g_object_get_data(G_OBJECT(button), "theme-name");
    width = chooser->kind == ATM_DISCOVERY_CURSOR ? 48 :
            chooser->kind == ATM_DISCOVERY_ICONS ? 64 : 140;
    height = chooser->kind == ATM_DISCOVERY_CURSOR ? 48 :
             chooser->kind == ATM_DISCOVERY_ICONS ? 64 : 80;
    set_image_preview(image, chooser->kind, name, width, height);
    return G_SOURCE_CONTINUE;
}

static void
theme_chooser_clear_tiles(ThemeChooser *chooser)
{
    GList *children;
    GList *cursor;

    if (chooser->idle_id != 0) {
        g_source_remove(chooser->idle_id);
        chooser->idle_id = 0;
    }
    children = gtk_container_get_children(GTK_CONTAINER(chooser->flowbox));
    for (cursor = children; cursor != NULL; cursor = cursor->next)
        gtk_widget_destroy(GTK_WIDGET(cursor->data));
    g_list_free(children);
    g_ptr_array_set_size(chooser->tiles, 0);
    g_clear_pointer(&chooser->available, g_ptr_array_unref);
    chooser->preview_index = 0;
    chooser->loaded = FALSE;
}

static gint
theme_tile_width(AtmDiscoveryKind kind)
{
    if (kind == ATM_DISCOVERY_CURSOR)
        return 100;
    if (kind == ATM_DISCOVERY_ICONS)
        return 112;
    return 168;
}

static gint
theme_tile_height(AtmDiscoveryKind kind)
{
    return kind == ATM_DISCOVERY_CURSOR ? 104 : 118;
}

static gint
theme_tile_placeholder_size(AtmDiscoveryKind kind)
{
    return kind == ATM_DISCOVERY_CURSOR ? 48 :
           kind == ATM_DISCOVERY_ICONS ? 64 : 48;
}

static GtkWidget *
theme_tile_new(ThemeChooser *chooser, const gchar *name)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *image = gtk_image_new_from_icon_name(
        fallback_icon_name(chooser->kind), GTK_ICON_SIZE_DIALOG);
    GtkWidget *label = gtk_label_new(name);

    gtk_image_set_pixel_size(GTK_IMAGE(image),
                             theme_tile_placeholder_size(chooser->kind));
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 20);
    gtk_widget_set_tooltip_text(button, name);
    gtk_widget_set_size_request(button,
                                theme_tile_width(chooser->kind),
                                theme_tile_height(chooser->kind));
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    gtk_box_pack_start(GTK_BOX(box), image, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    g_object_set_data_full(G_OBJECT(button),
                           "theme-name",
                           g_strdup(name),
                           g_free);
    g_object_set_data(G_OBJECT(button), "preview-image", image);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(on_theme_tile_clicked),
                     chooser);
    return button;
}

static void
theme_chooser_populate(ThemeChooser *chooser)
{
    guint i;

    if (chooser->loaded)
        return;
    chooser->loaded = TRUE;
    chooser->available = atm_discovery_list(chooser->kind);
    if (chooser->selected != NULL &&
        !array_contains(chooser->available, chooser->selected))
        g_ptr_array_add(chooser->available, g_strdup(chooser->selected));

    for (i = 0; i < chooser->available->len; i++) {
        GtkWidget *button =
            theme_tile_new(chooser,
                           g_ptr_array_index(chooser->available, i));
        gtk_flow_box_insert(GTK_FLOW_BOX(chooser->flowbox), button, -1);
        g_ptr_array_add(chooser->tiles, button);
    }
    theme_chooser_update_tiles(chooser);
}

static void
on_theme_popover_show(GtkWidget *popover, gpointer user_data)
{
    ThemeChooser *chooser = user_data;
    (void) popover;

    theme_chooser_populate(chooser);
    gtk_widget_show_all(chooser->scroll);
    gtk_widget_queue_resize(chooser->popover);

    if (chooser->idle_id == 0 &&
        chooser->preview_index < chooser->tiles->len)
        chooser->idle_id =
            g_timeout_add_full(G_PRIORITY_LOW,
                               PREVIEW_LOAD_INTERVAL_MS,
                               theme_chooser_load_next_preview,
                               chooser,
                               NULL);
}

static void
on_theme_popover_closed(GtkPopover *popover, gpointer user_data)
{
    ThemeChooser *chooser = user_data;
    (void) popover;

    if (chooser->idle_id != 0) {
        g_source_remove(chooser->idle_id);
        chooser->idle_id = 0;
    }
}

static ThemeChooser *
theme_chooser_new(AtmDiscoveryKind kind)
{
    ThemeChooser *chooser = g_new0(ThemeChooser, 1);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    chooser->kind = kind;
    chooser->button = gtk_menu_button_new();
    gtk_widget_set_name(
        chooser->button,
        kind == ATM_DISCOVERY_CURSOR ?
        "cursor-theme-chooser" : "theme-preview-chooser");
    chooser->button_image = gtk_image_new();
    chooser->button_label = gtk_label_new("Choose a theme");
    chooser->popover = gtk_popover_new(chooser->button);
    chooser->scroll = gtk_scrolled_window_new(NULL, NULL);
    chooser->flowbox = gtk_flow_box_new();
    chooser->tiles = g_ptr_array_new();

    gtk_label_set_ellipsize(GTK_LABEL(chooser->button_label),
                            PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(chooser->button_label), 24);
    gtk_widget_set_size_request(
        chooser->button,
        kind == ATM_DISCOVERY_CURSOR ? CURSOR_CHOOSER_WIDTH : 270,
        kind == ATM_DISCOVERY_CURSOR ? CURSOR_CHOOSER_HEIGHT : 64);
    gtk_box_pack_start(GTK_BOX(button_box),
                       chooser->button_image,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(button_box),
                       chooser->button_label,
                       TRUE,
                       TRUE,
                       0);
    gtk_container_add(GTK_CONTAINER(chooser->button), button_box);

    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(chooser->flowbox),
                                    GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(chooser->flowbox), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(chooser->flowbox), 4);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(chooser->flowbox), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(chooser->flowbox), 8);
    gtk_container_set_border_width(GTK_CONTAINER(chooser->flowbox), 10);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chooser->scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(
        GTK_SCROLLED_WINDOW(chooser->scroll), 700);
    gtk_scrolled_window_set_min_content_height(
        GTK_SCROLLED_WINDOW(chooser->scroll), 420);
    gtk_widget_set_size_request(chooser->scroll, 700, 420);
    gtk_container_add(GTK_CONTAINER(chooser->scroll), chooser->flowbox);
    gtk_container_add(GTK_CONTAINER(chooser->popover), chooser->scroll);
    gtk_widget_show_all(chooser->scroll);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(chooser->button),
                                chooser->popover);
    g_signal_connect(chooser->popover,
                     "show",
                     G_CALLBACK(on_theme_popover_show),
                     chooser);
    g_signal_connect(chooser->popover,
                     "closed",
                     G_CALLBACK(on_theme_popover_closed),
                     chooser);
    return chooser;
}

static void
theme_chooser_reload(ThemeChooser *chooser, const gchar *selected)
{
    theme_chooser_clear_tiles(chooser);
    theme_chooser_set_selected(chooser, selected, FALSE);
}

static gchar *
theme_chooser_dup_selected(ThemeChooser *chooser)
{
    return g_strdup(chooser->selected);
}

static void
theme_chooser_set_changed(ThemeChooser *chooser,
                          ThemeChooserChanged changed,
                          gpointer user_data)
{
    chooser->changed = changed;
    chooser->changed_data = user_data;
}

static void
theme_chooser_free(ThemeChooser *chooser)
{
    if (chooser == NULL)
        return;
    if (chooser->idle_id != 0)
        g_source_remove(chooser->idle_id);
    g_clear_pointer(&chooser->available, g_ptr_array_unref);
    g_clear_pointer(&chooser->tiles, g_ptr_array_unref);
    g_free(chooser->selected);
    g_free(chooser);
}

static void
update_lock_status(WindowData *data)
{
    const gchar *gtk_theme = data->gtk_chooser->selected;
    g_autofree gchar *message = NULL;
    gpointer cached;
    gboolean has_style;

    if (gtk_theme == NULL) {
        gtk_label_set_text(GTK_LABEL(data->lock_status),
                           "No application theme is available.");
        return;
    }

    if (g_hash_table_lookup_extended(data->lock_style_cache,
                                     gtk_theme,
                                     NULL,
                                     &cached)) {
        has_style = GPOINTER_TO_UINT(cached) == 2;
    } else {
        has_style = atm_discovery_gtk_has_lock_style(gtk_theme);
        g_hash_table_insert(data->lock_style_cache,
                            g_strdup(gtk_theme),
                            GUINT_TO_POINTER(has_style ? 2 : 1));
    }

    if (has_style) {
        message = g_strdup_printf(
            "%s supplies Cinnamon screen-lock styling (.csstage detected).",
            gtk_theme);
    } else {
        message = g_strdup_printf(
            "%s does not supply .csstage rules; Cinnamon's fallback lock style will be used.",
            gtk_theme);
    }
    gtk_label_set_text(GTK_LABEL(data->lock_status), message);
}

static void
on_gtk_theme_changed(ThemeChooser *chooser, gpointer user_data)
{
    (void) chooser;
    update_lock_status(user_data);
}

static void
update_wallpaper_preview(WindowData *data)
{
    g_autofree gchar *path =
        gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(data->wallpaper_button));
    g_autoptr(GdkPixbuf) pixbuf = NULL;

    if (path != NULL)
        pixbuf = gdk_pixbuf_new_from_file_at_scale(path,
                                                   96,
                                                   60,
                                                   TRUE,
                                                   NULL);
    if (pixbuf != NULL)
        gtk_image_set_from_pixbuf(GTK_IMAGE(data->wallpaper_preview),
                                  pixbuf);
    else {
        gtk_image_set_from_icon_name(GTK_IMAGE(data->wallpaper_preview),
                                     "preferences-desktop-wallpaper",
                                     GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(data->wallpaper_preview), 48);
    }
}

static void
on_wallpaper_file_set(GtkFileChooserButton *button, gpointer user_data)
{
    (void) button;
    update_wallpaper_preview(user_data);
}

static void
on_accent_toggled(GtkSwitch *toggle,
                  GParamSpec *pspec,
                  gpointer user_data)
{
    WindowData *data = user_data;
    (void) pspec;
    gtk_widget_set_sensitive(
        data->accent_button,
        gtk_switch_get_active(toggle));
}

static void
load_current_controls(WindowData *data)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(AtmAppearanceSettings) current =
        atm_applier_read_current(&error);
    GdkRGBA color;

    if (current == NULL) {
        show_message(data,
                     GTK_MESSAGE_ERROR,
                     "Could not read the current appearance",
                     error->message);
        return;
    }

    theme_chooser_reload(data->cinnamon_chooser,
                           current->cinnamon_theme);
    theme_chooser_reload(data->gtk_chooser,
                           current->gtk_theme);
    theme_chooser_reload(data->icon_chooser,
                           current->icon_theme);
    theme_chooser_reload(data->cursor_chooser,
                           current->cursor_theme);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->cursor_size),
                              current->cursor_size);

    if (current->wallpaper_path != NULL)
        gtk_file_chooser_set_filename(
            GTK_FILE_CHOOSER(data->wallpaper_button),
            current->wallpaper_path);
    else
        gtk_file_chooser_unselect_all(
            GTK_FILE_CHOOSER(data->wallpaper_button));
    update_wallpaper_preview(data);

    if (current->color_scheme == NULL ||
        !gtk_combo_box_set_active_id(GTK_COMBO_BOX(data->mode_combo),
                                     current->color_scheme))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(data->mode_combo),
                                    "default");

    gtk_switch_set_active(
        GTK_SWITCH(data->accent_switch),
        current->accent_rgb != NULL && *current->accent_rgb != '\0');
    if (current->accent_rgb != NULL &&
        gdk_rgba_parse(&color, current->accent_rgb))
        gtk_color_chooser_set_rgba(
            GTK_COLOR_CHOOSER(data->accent_button), &color);
    gtk_widget_set_sensitive(
        data->accent_button,
        gtk_switch_get_active(GTK_SWITCH(data->accent_switch)));
    update_lock_status(data);
}

static gchar *
color_button_to_hex(GtkWidget *button)
{
    GdkRGBA color;
    guint red;
    guint green;
    guint blue;

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &color);
    red = (guint) (color.red * 255.0 + 0.5);
    green = (guint) (color.green * 255.0 + 0.5);
    blue = (guint) (color.blue * 255.0 + 0.5);
    return g_strdup_printf("#%02X%02X%02X", red, green, blue);
}

static void
on_apply_custom_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    g_autoptr(AtmAppearanceSettings) settings =
        g_new0(AtmAppearanceSettings, 1);
    g_autoptr(GError) error = NULL;
    const gchar *mode;
    (void) button;

    settings->cinnamon_theme =
        theme_chooser_dup_selected(data->cinnamon_chooser);
    settings->gtk_theme =
        theme_chooser_dup_selected(data->gtk_chooser);
    settings->icon_theme =
        theme_chooser_dup_selected(data->icon_chooser);
    settings->cursor_theme =
        theme_chooser_dup_selected(data->cursor_chooser);
    settings->cursor_size =
        gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(data->cursor_size));
    settings->wallpaper_path =
        gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(data->wallpaper_button));
    mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(data->mode_combo));
    settings->color_scheme = g_strdup(mode != NULL ? mode : "default");
    settings->accent_rgb =
        gtk_switch_get_active(GTK_SWITCH(data->accent_switch)) ?
        color_button_to_hex(data->accent_button) : g_strdup("");

    if (settings->cinnamon_theme == NULL ||
        settings->gtk_theme == NULL ||
        settings->icon_theme == NULL ||
        settings->cursor_theme == NULL) {
        show_message(data,
                     GTK_MESSAGE_ERROR,
                     "A required theme category is empty",
                     "Install at least one compatible Cinnamon, GTK, icon and cursor theme.");
        return;
    }

    if (!atm_applier_apply_custom(settings, &error)) {
        show_message(data,
                     GTK_MESSAGE_ERROR,
                     "The custom appearance could not be applied",
                     error->message);
        return;
    }
    show_message(data,
                 GTK_MESSAGE_INFO,
                 "Custom appearance applied",
                 "Already-running applications may need to be restarted.");
    refresh_themes(data);
    load_current_controls(data);
}

static void
on_reload_clicked(GtkButton *button, gpointer user_data)
{
    WindowData *data = user_data;
    (void) button;

    atm_preview_cache_clear();
    g_hash_table_remove_all(data->lock_style_cache);
    load_current_controls(data);
}

static GtkWidget *
create_bundle_page(WindowData *data)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *component_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *selection_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *import_button = gtk_button_new_with_label("Import Theme…");
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Previous");
    GtkWidget *select_all_button = gtk_button_new_with_label("Select all");
    GtkWidget *clear_button = gtk_button_new_with_label("Clear");
    guint i;

    data->bundle_list = gtk_list_box_new();
    data->preview = gtk_image_new_from_icon_name("preferences-desktop-theme",
                                                 GTK_ICON_SIZE_DIALOG);
    data->bundle_name = gtk_label_new("Select a theme");
    data->description = gtk_label_new("");
    data->details = gtk_label_new("");
    data->bundle_status = gtk_label_new("");
    data->apply_bundle_button =
        gtk_button_new_with_label("Apply Selected Components");

    gtk_container_set_border_width(GTK_CONTAINER(page), 18);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(data->bundle_list),
                                    GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(data->bundle_list),
                                              TRUE);
    gtk_widget_set_size_request(sidebar_scroll, 300, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sidebar_scroll), data->bundle_list);

    gtk_label_set_xalign(GTK_LABEL(data->bundle_name), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(data->description), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(data->details), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(data->bundle_status), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(data->description), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(data->details), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(data->bundle_status), TRUE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(data->bundle_name), "title");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(data->details),
        GTK_STYLE_CLASS_DIM_LABEL);

    gtk_box_pack_start(GTK_BOX(header), data->bundle_name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), data->description, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), data->preview, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), data->details, FALSE, FALSE, 0);

    for (i = 0; i < G_N_ELEMENTS(bundle_options); i++) {
        data->bundle_checks[i] =
            gtk_check_button_new_with_label(bundle_options[i].label);
        gtk_box_pack_start(GTK_BOX(component_box),
                           data->bundle_checks[i],
                           FALSE,
                           FALSE,
                           0);
    }
    gtk_box_pack_start(GTK_BOX(selection_buttons),
                       select_all_button,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(selection_buttons),
                       clear_button,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(component_box),
                       selection_buttons,
                       FALSE,
                       FALSE,
                       2);
    gtk_box_pack_start(GTK_BOX(content),
                       component_box,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(content),
                       data->bundle_status,
                       FALSE,
                       FALSE,
                       0);

    gtk_box_pack_start(GTK_BOX(button_box),
                       import_button,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(button_box),
                       restore_button,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_end(GTK_BOX(button_box),
                     data->apply_bundle_button,
                     FALSE,
                     FALSE,
                     0);
    gtk_box_pack_end(GTK_BOX(content), button_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), sidebar_scroll, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), content, TRUE, TRUE, 0);

    g_signal_connect(data->bundle_list,
                     "row-selected",
                     G_CALLBACK(on_row_selected),
                     data);
    g_signal_connect(data->apply_bundle_button,
                     "clicked",
                     G_CALLBACK(on_apply_bundle_clicked),
                     data);
    g_signal_connect(import_button,
                     "clicked",
                     G_CALLBACK(on_import_clicked),
                     data);
    g_signal_connect(restore_button,
                     "clicked",
                     G_CALLBACK(on_restore_clicked),
                     data);
    g_signal_connect(select_all_button,
                     "clicked",
                     G_CALLBACK(on_select_all_clicked),
                     data);
    g_signal_connect(clear_button,
                     "clicked",
                     G_CALLBACK(on_clear_selection_clicked),
                     data);
    return page;
}

static GtkWidget *
create_components_page(WindowData *data)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    GtkWidget *list = gtk_list_box_new();
    GtkWidget *cursor_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *wallpaper_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *accent_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *apply_button = gtk_button_new_with_label("Apply Appearance");
    GtkWidget *reload_button = gtk_button_new_with_label("Reload Current");
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Previous");
    GtkFileFilter *wallpaper_filter = gtk_file_filter_new();
    GtkWidget *heading;

    data->cinnamon_chooser =
        theme_chooser_new(ATM_DISCOVERY_CINNAMON);
    data->gtk_chooser =
        theme_chooser_new(ATM_DISCOVERY_GTK);
    data->icon_chooser =
        theme_chooser_new(ATM_DISCOVERY_ICONS);
    data->cursor_chooser =
        theme_chooser_new(ATM_DISCOVERY_CURSOR);
    data->cursor_size =
        gtk_spin_button_new_with_range(16.0, 128.0, 1.0);
    data->wallpaper_button =
        gtk_file_chooser_button_new("Choose a wallpaper",
                                    GTK_FILE_CHOOSER_ACTION_OPEN);
    data->wallpaper_preview =
        gtk_image_new_from_icon_name("preferences-desktop-wallpaper",
                                     GTK_ICON_SIZE_DIALOG);
    data->mode_combo = gtk_combo_box_text_new();
    data->accent_switch = gtk_switch_new();
    data->accent_button = gtk_color_button_new();
    data->lock_status = gtk_label_new("");

    gtk_widget_set_size_request(data->wallpaper_button, 210, -1);
    gtk_widget_set_size_request(data->wallpaper_preview, 96, 60);
    gtk_widget_set_size_request(data->mode_combo, 260, -1);
    gtk_label_set_line_wrap(GTK_LABEL(data->lock_status), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(data->lock_status), 38);
    gtk_label_set_xalign(GTK_LABEL(data->lock_status), 0.0f);

    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(data->mode_combo),
                              "default",
                              "Follow application default");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(data->mode_combo),
                              "prefer-dark",
                              "Prefer dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(data->mode_combo),
                              "prefer-light",
                              "Prefer light");

    gtk_box_pack_start(GTK_BOX(cursor_box),
                       data->cursor_chooser->button,
                       TRUE,
                       TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(cursor_box),
                       data->cursor_size,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(wallpaper_box),
                       data->wallpaper_preview,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(wallpaper_box),
                       data->wallpaper_button,
                       TRUE,
                       TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(accent_box),
                       data->accent_switch,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(accent_box),
                       data->accent_button,
                       FALSE,
                       FALSE,
                       0);

    gtk_file_filter_set_name(wallpaper_filter, "Images");
    gtk_file_filter_add_pixbuf_formats(wallpaper_filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(data->wallpaper_button),
                                wallpaper_filter);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(data->wallpaper_button),
                                    TRUE);

    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "2  Cinnamon desktop",
                          "Panel, menu, tray, notifications, popovers and OSD colors. Layout is never changed.",
                          data->cinnamon_chooser->button));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "3  Applications",
                          "GTK 3 and XApp controls, windows and title bars.",
                          data->gtk_chooser->button));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "4  Icons",
                          "Application, symbolic and Nemo file icons.",
                          data->icon_chooser->button));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "5  Cursor",
                          "Choose installed cursor artwork and a logical size from 16 to 128.",
                          cursor_box));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "6  Screen lock",
                          "The lock screen follows the active GTK theme; this reports whether it supplies dedicated Cinnamon lock CSS.",
                          data->lock_status));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "1  Wallpaper",
                          "Choose one local background image. Multi-monitor behavior is left to Cinnamon.",
                          wallpaper_box));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "7.1  Light/dark preference",
                          "Advertised to compatible applications; this is not display brightness.",
                          data->mode_combo));
    gtk_container_add(GTK_CONTAINER(list),
                      make_preference_row(
                          "7.2  Accent color",
                          "Enable an accent preference and choose its RGB color.",
                          accent_box));

    heading = make_heading(
        "Build your appearance",
        "Mix installed components in one place. Applying creates a custom composition and keeps a complete undo snapshot.");
    gtk_container_set_border_width(GTK_CONTAINER(content), 24);
    gtk_box_pack_start(GTK_BOX(content), heading, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), list, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box),
                       reload_button,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(button_box),
                       restore_button,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_end(GTK_BOX(button_box),
                     apply_button,
                     FALSE,
                     FALSE,
                     0);
    gtk_box_pack_start(GTK_BOX(content), button_box, FALSE, FALSE, 0);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), content);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);

    theme_chooser_set_changed(data->gtk_chooser,
                              on_gtk_theme_changed,
                              data);
    g_signal_connect(data->wallpaper_button,
                     "file-set",
                     G_CALLBACK(on_wallpaper_file_set),
                     data);
    g_signal_connect(data->accent_switch,
                     "notify::active",
                     G_CALLBACK(on_accent_toggled),
                     data);
    g_signal_connect(apply_button,
                     "clicked",
                     G_CALLBACK(on_apply_custom_clicked),
                     data);
    g_signal_connect(reload_button,
                     "clicked",
                     G_CALLBACK(on_reload_clicked),
                     data);
    g_signal_connect(restore_button,
                     "clicked",
                     G_CALLBACK(on_restore_clicked),
                     data);
    return page;
}

static void
window_data_free(WindowData *data)
{
    g_clear_pointer(&data->themes, g_ptr_array_unref);
    g_clear_pointer(&data->lock_style_cache, g_hash_table_unref);
    theme_chooser_free(data->cinnamon_chooser);
    theme_chooser_free(data->gtk_chooser);
    theme_chooser_free(data->icon_chooser);
    theme_chooser_free(data->cursor_chooser);
    g_free(data);
}

GtkWidget *
atm_window_new(GtkApplication *application)
{
    WindowData *data = g_new0(WindowData, 1);
    XAppPreferencesWindow *window = xapp_preferences_window_new();
    GtkWidget *bundle_page;
    GtkWidget *components_page;

    data->lock_style_cache =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    data->window = GTK_WIDGET(window);
    gtk_window_set_application(GTK_WINDOW(window), application);
    gtk_window_set_title(GTK_WINDOW(window), "Axionis Theme Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 980, 680);
    gtk_window_set_icon_name(GTK_WINDOW(window),
                             "io.axionis.ThemeManager");

    bundle_page = create_bundle_page(data);
    components_page = create_components_page(data);
    xapp_preferences_window_add_page(window,
                                     bundle_page,
                                     "global-themes",
                                     "Global Themes");
    xapp_preferences_window_add_page(window,
                                     components_page,
                                     "components",
                                     "Components");

    g_object_set_data_full(G_OBJECT(window),
                           "atm-window-data",
                           data,
                           (GDestroyNotify) window_data_free);
    refresh_themes(data);
    load_current_controls(data);
    gtk_widget_show_all(GTK_WIDGET(window));
    return GTK_WIDGET(window);
}
