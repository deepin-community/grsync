#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include "support.h"
#include "callbacks.h"

GtkBuilder *builder;
GtkWidget *main_window;
gchar *argv_session, *argv_filename, *icon, *icon_busy;
gboolean cmdline_session, cmdline_execute, cmdline_stayopen, cmdline_import;
GtkListStore *liststore_session;

int main (int argc, char *argv[]) {
#ifdef ENABLE_NLS
	bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
#endif

	gtk_init(&argc, &argv);

	gint i = 1, result;
	GError *gerror = NULL;

	cmdline_session = FALSE;
	cmdline_execute = FALSE;
	cmdline_stayopen = FALSE;
	cmdline_import = FALSE;
	argv_filename = NULL;

	while (i < argc && argv[i] != NULL) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'e') cmdline_execute = TRUE;
			if (argv[i][1] == 's') cmdline_stayopen = TRUE;
			if (argv[i][1] == 'i') cmdline_import = TRUE;
		} else {
			if (cmdline_import) argv_filename = g_strconcat(argv[i], NULL);
			else {
				argv_session = g_strconcat(argv[i], NULL);
				cmdline_session = TRUE;
			}
		}
		i++;
	}

	if (cmdline_execute && cmdline_import) {
		printf(_("Error: conflicting arguments\n"));
		return 22;
	}
	if (!cmdline_session) argv_session = g_strconcat("default", NULL);

	if (cmdline_import && argv_filename == NULL) {
		// disabled because of grsync.desktop Exec option
		/*
		printf(_("Error: missing filename\n"));
		return 2;
		*/
		cmdline_import = FALSE;
	}

	icon = g_file_test(ICON_SOURCE, G_FILE_TEST_EXISTS) ? ICON_SOURCE : ICON_PACKAGE;
	icon_busy = g_file_test(ICON_SOURCE_BUSY, G_FILE_TEST_EXISTS) ? ICON_SOURCE_BUSY : ICON_PACKAGE_BUSY;
	gtk_window_set_default_icon_from_file(icon, NULL);

	builder = gtk_builder_new();
	result = gtk_builder_add_from_file(builder, XMLFILE, &gerror) || gtk_builder_add_from_file(builder, PACKAGE_DATA_DIR "/" PACKAGE "/" XMLFILE, NULL);
	if (result) {
		main_window = (GtkWidget*) gtk_builder_get_object(builder, "main_window");
		liststore_session = (GtkListStore*) gtk_builder_get_object(builder, "liststore_session");
		gtk_builder_connect_signals(builder, NULL);
		on_main_create((GtkWindow*) main_window, NULL);		// I run the callback now because the "show" signal seems not to be emitted with gtkbuilder
		// gtk_widget_show_all(main_window);		// disabled because it showed widgets hidden by gtk calls in the program; was needed on some systems, need to check again
		gtk_main();
	} else {
		g_printerr("Error loading GtkBuilder XML file: %s - error code %u (%s)\n", XMLFILE, gerror->code, gerror->message);
	}

	g_free(argv_session);
	g_object_unref(G_OBJECT(builder));
	return 0;
}
