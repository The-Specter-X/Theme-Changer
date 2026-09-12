#include "config.h"
#include "atm-window.h"

#include <locale.h>
#include <libintl.h>

#define GETTEXT_PACKAGE "axionis-theme-manager"

static void
on_activate(GtkApplication *application, gpointer user_data)
{
    GtkWidget *window = atm_window_new(application);
    (void) user_data;
    gtk_window_present(GTK_WINDOW(window));
}

int
main(int argc, char **argv)
{
    g_autoptr(GtkApplication) application = NULL;

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, ATM_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    application = gtk_application_new("io.axionis.ThemeManager",
                                      G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), NULL);
    return g_application_run(G_APPLICATION(application), argc, argv);
}
