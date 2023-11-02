#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_UNITY
	#include <unity/unity/unity.h>
#endif

#include "callbacks.h"
#include "support.h"


GPid rsync_pid;
GIOChannel *chout, *cherr;
gint session_last = -1, session_set = -1, initial_width, initial_height, root_x, root_y, width, height;
gsize session_number = 0;
gboolean had_error, paused, first_load_groups = TRUE, dryrunning = FALSE, must_scroll = FALSE, is_set = FALSE;
gchar **groups = NULL, **argv, *error_list = NULL, *watch_oldfile = NULL;
GtkWidget *rsync_window = NULL;
gboolean config_output = FALSE, config_remember = TRUE, config_errorlist = FALSE, config_log = FALSE, config_log_overwrite = FALSE, config_fastscroll = TRUE;
gboolean config_switchbutton = FALSE, config_trayicon = FALSE;
gint64 startime, pausedtime;
gchar config_command[MAXPATH], *grsync_dir, *rsync_help = NULL, *rsync_man = NULL;
FILE *log_file = NULL;
glong scroll_previous_run;
GtkStatusIcon *trayIcon = NULL;
GtkTreePath *path_set = NULL;
GtkTreeModelFilter *filter = NULL;
gboolean more = FALSE, first = TRUE;


void dialog_set_labels_selectable(GtkWidget *dialog) {
	void _set_label_selectable(gpointer data, gpointer user_data) {
		GtkWidget *widget = GTK_WIDGET(data);
		if (GTK_IS_LABEL(widget)) gtk_label_set_selectable(GTK_LABEL(widget), TRUE);
	}

	GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
	GtkContainer *box = (GtkContainer *) area;
	GList *children = gtk_container_get_children(box);
	g_list_foreach(children, _set_label_selectable, NULL);
	g_list_free(children);
}


void on_trayicon_activate(GObject *trayIcon, gpointer window) {
	// gtk_widget_show(GTK_WIDGET(window));
	gtk_window_deiconify(GTK_WINDOW(window));
	gtk_window_set_skip_taskbar_hint((GtkWindow*) window, FALSE);
}


void set_trayicon(gboolean flag) {
	if (flag) {
		if (trayIcon == NULL) {
			trayIcon = gtk_status_icon_new_from_file(icon);
			g_signal_connect(GTK_STATUS_ICON(trayIcon), "activate", G_CALLBACK(on_trayicon_activate), main_window);
		}
	} else {
		if (trayIcon != NULL) {
			g_object_unref(trayIcon);
			trayIcon = NULL;
			gtk_window_set_skip_taskbar_hint((GtkWindow*) main_window, FALSE);
		}
	}
}


void set_trayicon_icon(gboolean busy) {
	if (trayIcon != NULL) gtk_status_icon_set_from_file(trayIcon, busy ? icon_busy : icon);
}

gboolean get_checkbox(gchar* name) {
	return gtk_toggle_button_get_active((GtkToggleButton*) gtk_builder_get_object(builder, name));
}


void set_checkbox(gchar* name, gboolean val) {
	gtk_toggle_button_set_active((GtkToggleButton*) gtk_builder_get_object(builder, name), val);
}


void ini_get_boolean(GKeyFile* settings_file, gchar* name, gboolean* var) {
	GError *error_handler = NULL;
	gboolean btmp = g_key_file_get_boolean(settings_file, CONFIG_GROUP, name, &error_handler);
	if (error_handler != NULL) g_clear_error(&error_handler);
	else *var = btmp;
}


void ini_set_entry(GKeyFile* settings_file, gchar* session, gchar* name, gchar* widget) {
	gchar* stmp = g_key_file_get_string(settings_file, session, name, NULL);
	if (stmp == NULL) stmp = g_strconcat("", NULL);
	gtk_entry_set_text((GtkEntry*) gtk_builder_get_object(builder, widget), stmp);
	g_free(stmp);
}


void set_main_title(gchar* session, gchar* extra) {
	gchar *stmp = g_strconcat("Grsync: ", session, " ", extra, NULL);
	gtk_window_set_title((GtkWindow*) main_window, stmp);
	if (trayIcon != NULL) gtk_status_icon_set_tooltip_text(trayIcon, stmp);
	g_free(stmp);
}


void set_rsync_title(gchar* session, gchar* extra) {
	gchar *stmp = g_strconcat("rsync: ", session, " ", extra, NULL);
	gtk_window_set_title((GtkWindow*) rsync_window, stmp);
	g_free(stmp);
}


void load_settings(gchar* session, gchar *filename) {
	gchar settings_file_path[MAXPATH], *stmp;
	static gboolean resized = FALSE;
	GKeyFile *settings_file;

	if (filename == NULL) g_sprintf(settings_file_path, "%s/%s", grsync_dir, "grsync.ini");
	else strncpy(settings_file_path, filename, MAXPATH-1);

	// initializes config_command before other functions which may use it
	if (filename == NULL) strcpy(config_command, "rsync");

	if (!g_file_test(settings_file_path, G_FILE_TEST_EXISTS)) {
		g_printf (_("(ERROR) Can't open config file! Maybe this is the first run?\n"));
		if (session != NULL) {
			save_settings(session, NULL);
			load_groups(session);
		}
	}

	settings_file = g_key_file_new();
	g_key_file_load_from_file(settings_file, settings_file_path, G_KEY_FILE_NONE, NULL);
	if (session == NULL) {
		session = g_key_file_get_start_group(settings_file);
		save_settings(session, NULL);
		session_last = -1;		// added because of save session bug, copied from external patch
		load_groups(session);
	}

	if (filename == NULL) {
		// load configuration (preferences)
		stmp = g_key_file_get_string(settings_file, CONFIG_GROUP, "command", NULL);
		if (stmp != NULL) {
			strncpy(config_command, stmp, MAXPATH - 1);
			g_free(stmp);
		}

		ini_get_boolean(settings_file, "output", &config_output);
		ini_get_boolean(settings_file, "remember", &config_remember);
		ini_get_boolean(settings_file, "errorlist", &config_errorlist);
		ini_get_boolean(settings_file, "logging", &config_log);
		ini_get_boolean(settings_file, "logging_overwrite", &config_log_overwrite);
		ini_get_boolean(settings_file, "fastscroll", &config_fastscroll);
		ini_get_boolean(settings_file, "switchbutton", &config_switchbutton);
//		gtk_widget_set_visible((GtkWidget*) gtk_builder_get_object(builder, "button_switch"), config_switchbutton);  ONLY WORKS IN GTK 2.18
		config_switchbutton ? gtk_widget_show((GtkWidget*) gtk_builder_get_object(builder, "button_switch")) :
		gtk_widget_hide((GtkWidget*) gtk_builder_get_object(builder, "button_switch"));

		ini_get_boolean(settings_file, "trayicon", &config_trayicon);
		set_trayicon(config_trayicon);

		// load window position and size
		if (!resized) {
			root_x = g_key_file_get_integer(settings_file, CONFIG_GROUP, "root_x", NULL);
			root_y = g_key_file_get_integer(settings_file, CONFIG_GROUP, "root_y", NULL);
			width  = g_key_file_get_integer(settings_file, CONFIG_GROUP, "width", NULL);
			height = g_key_file_get_integer(settings_file, CONFIG_GROUP, "height", NULL);
			gtk_window_move((GtkWindow*) main_window, root_x, root_y);
			if (width > 0 && height > 0) gtk_window_resize((GtkWindow*) main_window, width, height);
			resized = TRUE;
		}
	}
	set_main_title(session, NULL);		// put here because it should be after set_trayicon()

	// load specific session
	if (path_set == NULL) is_set = g_key_file_get_boolean(settings_file, session, "is_set", NULL);

	if (is_set && path_set == NULL) {
		GtkTreeIter iter;
		gboolean enabled;
		gint i = 0;
		GtkTreePath *path = gtk_tree_path_new_first();

		while (gtk_tree_model_get_iter((GtkTreeModel*) liststore_session, &iter, path)) {
			stmp = g_strconcat("set_session_enabled_", groups[i], NULL);
			enabled = g_key_file_get_boolean(settings_file, session, stmp, NULL);
			g_free(stmp);

			// if (enabled) gtk_list_store_set(liststore_session, &iter, 1, TRUE, -1);
			gtk_list_store_set(liststore_session, &iter, 1, enabled, -1);
			gtk_tree_path_next(path);
			i++;
		}
		gtk_tree_path_free(path);
	} else {
		ini_set_entry(settings_file, session, "text_source", "text_source");
		ini_set_entry(settings_file, session, "text_dest", "text_dest");

		ini_set_entry(settings_file, session, "text_notes", "entry_notes");
		ini_set_entry(settings_file, session, "text_com_before", "entry_com_before");
		ini_set_entry(settings_file, session, "text_com_after", "entry_com_after");

		stmp = g_key_file_get_string(settings_file, session, "text_addit", NULL);
		if (stmp == NULL) stmp = g_strconcat("", NULL);
		gtk_text_buffer_set_text(gtk_text_view_get_buffer((GtkTextView*) gtk_builder_get_object(builder, "textview_additional")), stmp, -1);
		g_free(stmp);

		set_checkbox("check_time", g_key_file_get_boolean(settings_file, session, "check_time", NULL));
		set_checkbox("check_perm", g_key_file_get_boolean(settings_file, session, "check_perm", NULL));
		set_checkbox("check_owner", g_key_file_get_boolean(settings_file, session, "check_owner", NULL));
		set_checkbox("check_group", g_key_file_get_boolean(settings_file, session, "check_group", NULL));
		set_checkbox("check_onefs", g_key_file_get_boolean(settings_file, session, "check_onefs", NULL));
		set_checkbox("check_verbose", g_key_file_get_boolean(settings_file, session, "check_verbose", NULL));
		set_checkbox("check_progr", g_key_file_get_boolean(settings_file, session, "check_progr", NULL));
		set_checkbox("check_delete", g_key_file_get_boolean(settings_file, session, "check_delete", NULL));
		set_checkbox("check_exist", g_key_file_get_boolean(settings_file, session, "check_exist", NULL));
		set_checkbox("check_size", g_key_file_get_boolean(settings_file, session, "check_size", NULL));
		set_checkbox("check_skipnew", g_key_file_get_boolean(settings_file, session, "check_skipnew", NULL));
		set_checkbox("check_windows", g_key_file_get_boolean(settings_file, session, "check_windows", NULL));

		set_checkbox("check_sum", g_key_file_get_boolean(settings_file, session, "check_sum", NULL));
		set_checkbox("check_symlink", g_key_file_get_boolean(settings_file, session, "check_symlink", NULL));
		set_checkbox("check_hardlink", g_key_file_get_boolean(settings_file, session, "check_hardlink", NULL));
		set_checkbox("check_dev", g_key_file_get_boolean(settings_file, session, "check_dev", NULL));
		set_checkbox("check_update", g_key_file_get_boolean(settings_file, session, "check_update", NULL));
		set_checkbox("check_keepart", g_key_file_get_boolean(settings_file, session, "check_keepart", NULL));
		set_checkbox("check_mapuser", g_key_file_get_boolean(settings_file, session, "check_mapuser", NULL));
		set_checkbox("check_compr", g_key_file_get_boolean(settings_file, session, "check_compr", NULL));
		set_checkbox("check_backup", g_key_file_get_boolean(settings_file, session, "check_backup", NULL));
		set_checkbox("check_itemized", g_key_file_get_boolean(settings_file, session, "check_itemized", NULL));
		set_checkbox("check_norecur", g_key_file_get_boolean(settings_file, session, "check_norecur", NULL));
		set_checkbox("check_protectargs", g_key_file_get_boolean(settings_file, session, "check_protectargs", NULL));

		set_checkbox("check_com_before", g_key_file_get_boolean(settings_file, session, "check_com_before", NULL));
		set_checkbox("check_com_halt", g_key_file_get_boolean(settings_file, session, "check_com_halt", NULL));
		set_checkbox("check_com_after", g_key_file_get_boolean(settings_file, session, "check_com_after", NULL));
		set_checkbox("check_com_onerror", g_key_file_get_boolean(settings_file, session, "check_com_onerror", NULL));
		set_checkbox("check_browse_files", g_key_file_get_boolean(settings_file, session, "check_browse_files", NULL));
		set_checkbox("check_superuser", g_key_file_get_boolean(settings_file, session, "check_superuser", NULL));
	}

	g_key_file_free (settings_file);

	GtkWidget *widget = (GtkWidget*) gtk_builder_get_object(builder, "table_set");
	is_set ? gtk_widget_show(widget) : gtk_widget_hide(widget);
	widget = (GtkWidget*) gtk_builder_get_object(builder, "table_basic");
	is_set ? gtk_widget_hide(widget) : gtk_widget_show(widget);
	widget = (GtkWidget*) gtk_builder_get_object(builder, "table_advanced");
	is_set ? gtk_widget_hide(widget) : gtk_widget_show(widget);
	widget = (GtkWidget*) gtk_builder_get_object(builder, "table_extra");
	is_set ? gtk_widget_hide(widget) : gtk_widget_show(widget);
}


void save_settings(gchar *session, gchar *filename) {
	gchar settings_file_name[MAXPATH], *key_data, *stmp;
	FILE *key_file;
	GError *error_handler = NULL;
	GKeyFile *settings_file;

	if (filename == NULL) g_sprintf(settings_file_name, "%s/%s", grsync_dir, "grsync.ini");
	else strncpy(settings_file_name, filename, MAXPATH-1);
	g_mkdir(grsync_dir, 0700);
	settings_file = g_key_file_new();

	if (filename == NULL) {
		g_key_file_load_from_file(settings_file, settings_file_name, G_KEY_FILE_NONE, NULL);

		// save configuration (preferences)
		g_key_file_set_string(settings_file, CONFIG_GROUP, "command", config_command);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "output", config_output);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "remember", config_remember);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "errorlist", config_errorlist);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "logging", config_log);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "logging_overwrite", config_log_overwrite);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "fastscroll", config_fastscroll);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "switchbutton", config_switchbutton);
		g_key_file_set_boolean(settings_file, CONFIG_GROUP, "trayicon", config_trayicon);
		if (session_last != -1 && config_remember) g_key_file_set_string(settings_file, CONFIG_GROUP, "last_session", groups[session_last]);

		// window position and size
		g_key_file_set_integer(settings_file, CONFIG_GROUP, "root_x", root_x);
		g_key_file_set_integer(settings_file, CONFIG_GROUP, "root_y", root_y);
		g_key_file_set_integer(settings_file, CONFIG_GROUP, "width", width);
		g_key_file_set_integer(settings_file, CONFIG_GROUP, "height", height);
	}

	// save specific session

	if (is_set) {
		GtkTreeIter iter;
		gboolean enabled, cur_is_set;
		gint i = 0;
		GtkTreePath *path = gtk_tree_path_new_first();
		g_key_file_remove_group(settings_file, session, NULL);		// empties the group before adding
		g_key_file_set_boolean(settings_file, session, "is_set", TRUE);

		while (gtk_tree_model_get_iter((GtkTreeModel*) liststore_session, &iter, path)) {
			gtk_tree_model_get((GtkTreeModel*) liststore_session, &iter, 2, &cur_is_set, -1);
			if (cur_is_set) {
				gtk_tree_model_get((GtkTreeModel*) liststore_session, &iter, 1, &enabled, -1);
				if (enabled) {
					stmp = g_strconcat("set_session_enabled_", groups[i], NULL);
					g_key_file_set_boolean(settings_file, session, stmp, TRUE);
					g_free(stmp);
				}
			}
			gtk_tree_path_next(path);
			i++;
		}
		gtk_tree_path_free(path);
	} else {
		g_key_file_set_boolean(settings_file, session, "is_set", FALSE);
		g_key_file_set_string(settings_file, session, "text_source", gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_source")));
		g_key_file_set_string(settings_file, session, "text_dest", gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_dest")));
		g_key_file_set_string(settings_file, session, "text_notes", gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_notes")));
		g_key_file_set_string(settings_file, session, "text_com_before", gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_com_before")));
		g_key_file_set_string(settings_file, session, "text_com_after", gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_com_after")));

		GtkTextIter iter1, iter2;
		GtkTextBuffer *buf = gtk_text_view_get_buffer((GtkTextView*) gtk_builder_get_object(builder, "textview_additional"));
		gtk_text_buffer_get_start_iter(buf, &iter1);
		gtk_text_buffer_get_end_iter(buf, &iter2);
		stmp = gtk_text_buffer_get_text(buf, &iter1, &iter2, FALSE);
		g_key_file_set_string(settings_file, session, "text_addit", stmp);
		g_free(stmp);

		g_key_file_set_boolean(settings_file, session, "check_time", get_checkbox("check_time"));
		g_key_file_set_boolean(settings_file, session, "check_perm", get_checkbox("check_perm"));
		g_key_file_set_boolean(settings_file, session, "check_owner", get_checkbox("check_owner"));
		g_key_file_set_boolean(settings_file, session, "check_group", get_checkbox("check_group"));
		g_key_file_set_boolean(settings_file, session, "check_onefs", get_checkbox("check_onefs"));
		g_key_file_set_boolean(settings_file, session, "check_verbose", get_checkbox("check_verbose"));
		g_key_file_set_boolean(settings_file, session, "check_progr", get_checkbox("check_progr"));
		g_key_file_set_boolean(settings_file, session, "check_delete", get_checkbox("check_delete"));
		g_key_file_set_boolean(settings_file, session, "check_exist", get_checkbox("check_exist"));
		g_key_file_set_boolean(settings_file, session, "check_size", get_checkbox("check_size"));
		g_key_file_set_boolean(settings_file, session, "check_skipnew", get_checkbox("check_skipnew"));
		g_key_file_set_boolean(settings_file, session, "check_windows", get_checkbox("check_windows"));

		g_key_file_set_boolean(settings_file, session, "check_sum", get_checkbox("check_sum"));
		g_key_file_set_boolean(settings_file, session, "check_symlink", get_checkbox("check_symlink"));
		g_key_file_set_boolean(settings_file, session, "check_hardlink", get_checkbox("check_hardlink"));
		g_key_file_set_boolean(settings_file, session, "check_dev", get_checkbox("check_dev"));
		g_key_file_set_boolean(settings_file, session, "check_update", get_checkbox("check_update"));
		g_key_file_set_boolean(settings_file, session, "check_keepart", get_checkbox("check_keepart"));
		g_key_file_set_boolean(settings_file, session, "check_mapuser", get_checkbox("check_mapuser"));
		g_key_file_set_boolean(settings_file, session, "check_compr", get_checkbox("check_compr"));
		g_key_file_set_boolean(settings_file, session, "check_backup", get_checkbox("check_backup"));
		g_key_file_set_boolean(settings_file, session, "check_itemized", get_checkbox("check_itemized"));
		g_key_file_set_boolean(settings_file, session, "check_norecur", get_checkbox("check_norecur"));
		g_key_file_set_boolean(settings_file, session, "check_protectargs", get_checkbox("check_protectargs"));

		g_key_file_set_boolean(settings_file, session, "check_com_before", get_checkbox("check_com_before"));
		g_key_file_set_boolean(settings_file, session, "check_com_halt", get_checkbox("check_com_halt"));
		g_key_file_set_boolean(settings_file, session, "check_com_after", get_checkbox("check_com_after"));
		g_key_file_set_boolean(settings_file, session, "check_com_onerror", get_checkbox("check_com_onerror"));
		g_key_file_set_boolean(settings_file, session, "check_browse_files", get_checkbox("check_browse_files"));
		g_key_file_set_boolean(settings_file, session, "check_superuser", get_checkbox("check_superuser"));
	}

	key_data = g_key_file_to_data(settings_file, NULL, &error_handler);
	key_file = fopen(settings_file_name, "w");
	if (key_file != NULL) {
		fputs(key_data, key_file);
		fclose(key_file);
	} else {
		g_printf(_("\tUnable to save settings!\n"));
	}

	g_key_file_free(settings_file);
	g_clear_error(&error_handler);
}


int compare_strings(const void *p1, const void *p2) {
	return strcoll(*((char**) p1), *((char**) p2));
}


gboolean load_groups(gchar *session) {
	gchar settings_file_path[MAXPATH], *stmp;
	g_sprintf(settings_file_path, "%s/%s", grsync_dir, "grsync.ini");

	if (g_file_test(settings_file_path, G_FILE_TEST_EXISTS)) {
		GError *error_handler = NULL;
		GKeyFile *settings_file;
		GtkComboBox *combo;
		GtkTreeIter iter;
		gint i;
		gboolean flag = FALSE;

		combo = (GtkComboBox*) gtk_builder_get_object(builder, "combo_session");
		gtk_list_store_clear(liststore_session);
		// for (i = 0; i < session_number; i++) gtk_combo_box_remove_text(combo, 0);

		settings_file = g_key_file_new();
		g_key_file_load_from_file(settings_file, settings_file_path, 	G_KEY_FILE_NONE, &error_handler);
		groups = g_key_file_get_groups(settings_file, &session_number);

		if (!cmdline_session && first_load_groups && g_key_file_get_boolean(settings_file, CONFIG_GROUP, "remember", NULL)) {
			stmp = g_key_file_get_string(settings_file, CONFIG_GROUP, "last_session", NULL);
			if (stmp != NULL) {
				session = stmp;
				g_free(argv_session);
				argv_session = g_strconcat(session, NULL);
			}
			first_load_groups = FALSE;
		}

		i = 0;
		while (groups[i] != NULL) {
			if (strcmp(groups[i], CONFIG_GROUP) == 0) {
				session_number--;
				g_free(groups[i]);
				flag = TRUE;
			} else {
				if (i > 0 && flag) groups[i - 1] = groups[i];
			}
			i++;
		}
		groups[session_number] = NULL;
		qsort(groups, session_number, sizeof(gchar*), compare_strings);

		i = 0;
		while (groups[i] != NULL) {
			// gtk_combo_box_append_text(combo, groups[i]);
			gtk_list_store_append(liststore_session, &iter);
			gtk_list_store_set(liststore_session, &iter, 0, groups[i], 2, !g_key_file_get_boolean(settings_file, groups[i], "is_set", NULL), -1);

			if (strcmp(groups[i], session) == 0) {
				gtk_combo_box_set_active(combo, i);
				session_last = i;
			}
			i++;
		}

		if (!filter) {
			filter = (GtkTreeModelFilter*) gtk_builder_get_object(builder, "treemodelfilter_set");
			gtk_tree_model_filter_set_visible_column(filter, 2);
			gtk_tree_model_filter_refilter(filter);
		}

		g_key_file_free(settings_file);
		g_clear_error(&error_handler);
		if (session_last == -1) return FALSE;
	}
	return TRUE;
}


void show_browse_source(GtkButton *button, gpointer user_data) {
	GtkWidget *dialog;
	gint retval;
	gchar *filename;
	gboolean browse_files = get_checkbox("check_browse_files");
	GtkFileChooserAction action = browse_files ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	gchar *curr_path = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_source"));
	gchar *curr_folder = g_path_get_dirname(curr_path);

	dialog = gtk_file_chooser_dialog_new(_("Browse"), (GtkWindow*) main_window, action, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), curr_folder);
	if (browse_files) gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(dialog), curr_path);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_ACCEPT || retval == GTK_RESPONSE_OK) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		// tmp = g_strconcat(filename, browse_files ? "" : "/", NULL);
		gtk_entry_set_text((GtkEntry*) gtk_builder_get_object(builder, "text_source"), filename);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
	g_free(curr_folder);
}


void show_browse_dest(GtkButton *button, gpointer user_data) {
	GtkWidget *dialog;
	gint retval;
	gchar *filename;
	gboolean browse_files = get_checkbox("check_browse_files");
	GtkFileChooserAction action = browse_files ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	gchar *curr_path = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_dest"));
	gchar *curr_folder = g_path_get_dirname(curr_path);

	dialog = gtk_file_chooser_dialog_new(_("Browse"), (GtkWindow*) main_window, action, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), curr_folder);
	if (browse_files) gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(dialog), curr_path);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_ACCEPT || retval == GTK_RESPONSE_OK) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		gtk_entry_set_text((GtkEntry*) gtk_builder_get_object(builder, "text_dest"), filename);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
	g_free(curr_folder);
}


void set_global_progress(gdouble fraction) {
	gint64 curtime;
	glong elapsed, remaining;
	static glong lastupdate = 0;
	gchar tmps[50];
	GtkProgressBar* progr = (GtkProgressBar*) gtk_builder_get_object(builder, "progress_global");

	curtime = g_get_monotonic_time() / 1000000;
	if (fraction > (gdouble)0 && fraction < (gdouble)1 && curtime == lastupdate) return;
	lastupdate = curtime;

	#ifdef HAVE_UNITY
		UnityLauncherEntry *launcher_entry = unity_launcher_entry_get_for_desktop_id("grsync.desktop");
		unity_launcher_entry_set_progress(launcher_entry, fraction);
		unity_launcher_entry_set_progress_visible(launcher_entry, fraction > (gdouble)0);
 	#endif

	gtk_progress_bar_set_fraction(progr, fraction);
	elapsed = curtime - startime;
	remaining = elapsed / fraction - elapsed;
	if (fraction) snprintf(tmps, 49, _("%.f%% (%li:%02li elapsed, %li:%02li remaining)"), round(fraction * 100), elapsed / 60, elapsed % 60, remaining / 60, remaining % 60);
	else strcpy(tmps, "0%");
	gtk_progress_bar_set_text(progr, tmps);
	set_main_title(groups[session_last], tmps);
}


void set_file_progress(gdouble fraction) {
	gint64 curtime;
	static glong lastupdate = 0;
	gchar tmps[50];
	GtkProgressBar* progr = (GtkProgressBar*) gtk_builder_get_object(builder, "progress_file");

	curtime = g_get_monotonic_time() / 1000000;
	if (fraction < (gdouble)1 && curtime == lastupdate) return;
	lastupdate = curtime;

	gtk_progress_bar_set_fraction(progr, fraction);
	snprintf(tmps, 5, "%.f%%", round(fraction * 100));
	gtk_progress_bar_set_text(progr, tmps);
}


void build_rsync_cmd() {
	gint i = 0, j, argc_tmp;
	gchar **argv_tmp, *gtmp;
	GtkTextIter iter1, iter2;
	GtkTextBuffer *buf;

	argv = g_new(gchar *, MAXOPT);
	argv_tmp = g_new(gchar *, MAXOPT);
	if (get_checkbox("check_superuser")) argv[i++] = "pkexec";
	argv[i++] = config_command;

	argv[i++] = get_checkbox("check_norecur") ? "-d" : "-r";
	if (dryrunning) argv[i++] = "-n";
	if (get_checkbox("check_time")) argv[i++] = "-t";
	if (get_checkbox("check_perm")) argv[i++] = "-p";
	if (get_checkbox("check_owner")) argv[i++] = "-o";
	if (get_checkbox("check_group")) argv[i++] = "-g";
	if (get_checkbox("check_onefs")) argv[i++] = "-x";
	if (get_checkbox("check_verbose")) argv[i++] = "-v";
	if (get_checkbox("check_progr")) argv[i++] = "--progress";
	if (get_checkbox("check_delete")) argv[i++] = "--delete";
	if (get_checkbox("check_exist")) argv[i++] = "--ignore-existing";
	if (get_checkbox("check_size")) argv[i++] = "--size-only";
	if (get_checkbox("check_skipnew")) argv[i++] = "-u";
	if (get_checkbox("check_windows")) argv[i++] = "--modify-window=1";

	if (get_checkbox("check_sum")) argv[i++] = "-c";
	if (get_checkbox("check_symlink")) argv[i++] = "-l";
	if (get_checkbox("check_hardlink")) argv[i++] = "-H";
	if (get_checkbox("check_dev")) argv[i++] = "-D";
	if (get_checkbox("check_update")) argv[i++] = "--existing";
	if (get_checkbox("check_keepart")) argv[i++] = "--partial";
	if (get_checkbox("check_mapuser")) argv[i++] = "--numeric-ids";
	if (get_checkbox("check_compr")) argv[i++] = "-z";
	if (get_checkbox("check_backup")) argv[i++] = "-b";
	if (get_checkbox("check_itemized")) argv[i++] = "-i";
	if (get_checkbox("check_protectargs")) argv[i++] = "-s";

	buf = gtk_text_view_get_buffer((GtkTextView*) gtk_builder_get_object(builder, "textview_additional"));
	gtk_text_buffer_get_start_iter(buf, &iter1);
	gtk_text_buffer_get_end_iter(buf, &iter2);
	gtmp = gtk_text_buffer_get_text(buf, &iter1, &iter2, FALSE);
	if (gtmp[0]) {
		g_shell_parse_argv(gtmp, &argc_tmp, &argv_tmp, NULL);
		for (j = 0; j < argc_tmp && i + 3 < MAXOPT; j++) argv[i++] = g_strconcat(argv_tmp[j], NULL);
		g_strfreev(argv_tmp);
	}
	g_free(gtmp);
	// source, dest and NULL must follow only, or change the previous for cicle ending condition.

	argv[i++] = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_source"));
	argv[i++] = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_dest"));
	argv[i++] = NULL;
}


gboolean set_next() {
	static gint i = -1;
	gboolean enabled, more;
	GtkTreeIter iter;

	if (i == -1) {
		session_set = session_last;
		path_set = gtk_tree_path_new_first();
		i = 0;
		first = TRUE;
	} else {
		first = FALSE;
	}

	while ((more = gtk_tree_model_get_iter((GtkTreeModel*) liststore_session, &iter, path_set))) {
		gtk_tree_model_get((GtkTreeModel*) liststore_session, &iter, 1, &enabled, -1);
		gtk_tree_path_next(path_set);
		i++;
		if (enabled) {
			session_last = i - 1;
			load_settings(groups[session_last], NULL);
			break;
		}
	}

	if (!more) {
		session_last = session_set;
		session_set = -1;
		gtk_tree_path_free(path_set);
		path_set = NULL;
		i = -1;
	}
	return more;
}


void on_play_clicked(GtkButton *button, gpointer user_data) {
	gchar *gtmp;

	if (is_set) {
		more = set_next();
		if (!more) return;
	}
	build_rsync_cmd();
	paused = FALSE;
	// scroll_previous_run = 0;

	gtk_expander_set_expanded((GtkExpander*) gtk_builder_get_object(builder, "expander_rsync"), config_output || dryrunning);
	// with expander initialized, let's resize the window:
	on_expander_rsync_activate((GtkExpander*) gtk_builder_get_object(builder, "expander_rsync"), NULL);

	if (!is_set || first) {
		if (error_list != NULL) g_free(error_list);
		error_list = g_strconcat("", NULL);
	}

	if (config_log) {
		gtmp = g_strconcat(grsync_dir, "/", groups[session_last], ".log", NULL);
		log_file = fopen(gtmp, (config_log_overwrite ? "w" : "a"));
		g_free(gtmp);
	}

	set_rsync_title(groups[session_last], "running");
	gtk_button_set_label((GtkButton*) gtk_builder_get_object(builder, "close"), "gtk-stop");
	gtk_button_set_label((GtkButton*) gtk_builder_get_object(builder, "button_pause"), "gtk-media-pause");
	gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "button_pause"), TRUE);
	gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "button_show_errors"), FALSE);
	gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "textview_output"), FALSE);
	gtk_window_set_urgency_hint((GtkWindow*) rsync_window, FALSE);
	set_file_progress(0);
	set_global_progress(0);
	gtk_label_set_text((GtkLabel*) gtk_builder_get_object(builder, "label_file"), dryrunning ? _("Simulation in progress") : "");
	if (!is_set || first) gtk_widget_show(rsync_window);
	else on_rsync_show(NULL, NULL);
}


void on_dryrun_clicked(GtkButton *button, gpointer user_data) {
	dryrunning = TRUE;
	on_play_clicked(button, NULL);
}


void set_tooltip(gchar *long_opt, gchar *widget_str) {
	gchar *tmpc, *tmpc2, *out, *opt, *oldtip;
	GtkWidget *widget;

	opt = rsync_man != NULL ? g_strconcat(long_opt, "\n", NULL) : g_strconcat(long_opt, " ", NULL);
	out = rsync_man != NULL ? g_strconcat(rsync_man, NULL) : g_strconcat(rsync_help, NULL);
	tmpc = g_strrstr(out, opt);
//	tmpc = g_strstr_len(out, strlen(out), opt);

	if (tmpc != NULL && *tmpc) {
		while (*tmpc != '\n') tmpc--;
		tmpc++;
		tmpc2 = rsync_man != NULL ? strstr(tmpc, "\n\n\n") : strchr(tmpc, '\n');
		*tmpc2 = '\0';
		g_strstrip(tmpc);

		widget = (GtkWidget*) gtk_builder_get_object(builder, widget_str);
		oldtip = gtk_widget_get_tooltip_text(widget);
		if (oldtip != NULL) {
			tmpc2 = g_strconcat(tmpc, "\n\n", oldtip, NULL);
			g_free(oldtip);
		}
		else tmpc2 = g_strconcat(tmpc, NULL);

		gtk_widget_set_tooltip_text(widget, tmpc2);
		g_free(tmpc2);
	}
	g_free(opt);
	g_free(out);
}


void on_main_create(GtkWindow *window, gpointer user_data) {
	GtkWidget *error;
	gchar *stmp;

	grsync_dir = g_strconcat(g_get_home_dir(), "/.grsync", NULL);
	if (!load_groups(argv_session)) {
		error = gtk_message_dialog_new(window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CANCEL,
			_("The session you specified on the command line doesn't exists"));
		gtk_dialog_run(GTK_DIALOG(error));
		gtk_widget_destroy(error);
		g_free(argv_session);
		argv_session = g_strconcat("default", NULL);
		cmdline_execute = FALSE;
		load_groups(argv_session);
	}
	load_settings(argv_session, NULL);

	stmp = g_strconcat(config_command, " --help", NULL);
	if (!g_spawn_command_line_sync(stmp, &rsync_help, NULL, NULL, NULL)) {
		error = gtk_message_dialog_new(window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CANCEL,
			_("Unable to execute the rsync command line tool. It is needed by this program. You may need to install the rsync package or make it available in the default executable search PATH. See file/preferences to change the default rsync executable command."));
		gtk_dialog_run(GTK_DIALOG(error));
		gtk_widget_destroy(error);
	} else {
//		g_spawn_command_line_sync("man rsync", &rsync_man, NULL, NULL, NULL);
//		g_spawn_command_line_sync("sh -c 'info rsync | egrep -A9999 \"OPTIONS$\" | grep -B9999 \"DAEMON OPTIONS\"'", &rsync_man, NULL, NULL, NULL);
		set_tooltip("--times", "check_time");
		set_tooltip("--perms", "check_perm");
		set_tooltip("--owner", "check_owner");
		set_tooltip("--group", "check_group");
		set_tooltip("--one-file-system", "check_onefs");
		set_tooltip("--verbose", "check_verbose");
		set_tooltip("--progress", "check_progr");
		set_tooltip("--delete", "check_delete");
		set_tooltip("--ignore-existing", "check_exist");
		set_tooltip("--size-only", "check_size");
		set_tooltip("--update", "check_skipnew");
		set_tooltip("--modify-window=NUM", "check_windows");

		set_tooltip("--checksum", "check_sum");
		set_tooltip("--hard-links", "check_hardlink");
		set_tooltip("-D", "check_dev");
		set_tooltip("--existing", "check_update");
		set_tooltip("--partial", "check_keepart");
		set_tooltip("--numeric-ids", "check_mapuser");
		set_tooltip("--compress", "check_compr");
		set_tooltip("--backup", "check_backup");
		set_tooltip("--itemize-changes", "check_itemized");
		set_tooltip("--protect-args", "check_protectargs");
	}
	g_free(stmp);
	rsync_window = (GtkWidget*) gtk_builder_get_object(builder, "dialog_rsync");
	gtk_window_get_size((GtkWindow*) rsync_window, &initial_width, &initial_height);
	if (cmdline_execute) on_play_clicked((GtkButton*) window, NULL);
	if (cmdline_import) on_import1_activate((GtkMenuItem*) window, argv_filename);
}


void on_main_destroy(GtkWidget *object, gpointer user_data) {
	gint sel = gtk_combo_box_get_active((GtkComboBox*) gtk_builder_get_object(builder, "combo_session"));
	save_settings(groups[sel], NULL);
	g_strfreev(groups);
	if (rsync_help != NULL) g_free(rsync_help);
	if (rsync_man != NULL) g_free(rsync_man);
	gtk_main_quit();
}


gboolean on_main_configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data) {
	gtk_window_get_position((GtkWindow*) main_window, &root_x, &root_y);
	gtk_window_get_size((GtkWindow*) main_window, &width, &height);
	return FALSE;
}


void superkill(int sig) {
	if (get_checkbox("check_superuser")) {
		gchar *cmd;
		gchar pidbuf[6], sigbuf[6];

		sprintf(pidbuf, "%i", rsync_pid);
		sprintf(sigbuf, "%i", sig);
		cmd = g_strconcat("pkexec kill -", sigbuf, " ", pidbuf, NULL);
		g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
		g_free(cmd);
	} else {
		kill(rsync_pid, sig);
	}
}


void on_close_clicked(GtkButton *button, gpointer user_data) {
	if (rsync_pid) {
		if (paused) superkill(SIGCONT);
		superkill(SIGTERM);
		gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "button_pause"), FALSE);
	} else {
		if (more) on_play_clicked(NULL, NULL);		//on_rsync_show(NULL, NULL);
		else {
			set_global_progress(0);
			gtk_widget_hide(rsync_window);
		}
	}
	if (cmdline_execute && !more && !cmdline_stayopen) on_main_destroy((GtkWidget*) main_window, NULL);
}


void scroll_to_end(GtkTextView* view, gboolean final) {
	gint64 curtime;

//	if (config_fastscroll && !final) g_get_current_time(&curtime);
	curtime = g_get_monotonic_time() / 1000000;
	if (!config_fastscroll || final || curtime - scroll_previous_run >= 1) {
		GtkTextBuffer *buffer;
		GtkTextIter iter;
		GtkTextMark *mark;

		scroll_previous_run = curtime;
		buffer = gtk_text_view_get_buffer(view);
		gtk_text_buffer_get_end_iter(buffer, &iter);
		mark = gtk_text_buffer_create_mark(buffer, "end_mark", &iter, TRUE);
		gtk_text_view_scroll_mark_onscreen(view, mark);
		gtk_text_buffer_delete_mark(buffer, mark);
	}
}


void rsync_cleanup(gpointer data) {
	GtkTextBuffer *buffer;
	GtkTextView *view;
	gchar *comm_str, *comm_out, *comm_err, *comm_after;

	view = (GtkTextView*) gtk_builder_get_object(builder, "textview_output");
	if (get_checkbox("check_com_after")) {
		if (had_error || !get_checkbox("check_com_onerror")) {
			comm_str = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_com_after"));
			if (!g_spawn_command_line_sync(comm_str, &comm_out, &comm_err, NULL, NULL)) {
				comm_out = g_strconcat("", NULL);
				comm_err = g_strconcat("Error launching command!\n", NULL);
			}
			comm_after = g_strconcat("\n", _("** Launching AFTER command:\n"), comm_str, "\n", comm_out, comm_err, NULL);
			g_free(comm_out);
			g_free(comm_err);

			buffer = gtk_text_view_get_buffer(view);
			gtk_text_buffer_insert_at_cursor(buffer, comm_after, -1);
			scroll_to_end(view, FALSE);
			if (log_file != NULL) fputs(comm_after, log_file);
			g_free(comm_after);
		}
	}

	gtk_button_set_label((GtkButton*) gtk_builder_get_object(builder, "close"), "gtk-close");
	GtkLabel *tmpl = (GtkLabel*) gtk_builder_get_object(builder, "label_file");
	if (had_error) {
		gtk_label_set_markup(tmpl, _("<span foreground=\"red\" weight=\"bold\">Completed with errors!</span>"));
		gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "button_show_errors"), TRUE);
	} else {
		gtk_label_set_markup(tmpl, _("<span foreground=\"darkgreen\" weight=\"bold\">Completed successfully!</span>"));
	}
	set_global_progress(1);
	gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "button_pause"), FALSE);
	gtk_widget_set_sensitive((GtkWidget*) view, TRUE);
	set_rsync_title(groups[session_last], "stopped");

	g_io_channel_unref(chout);
	g_io_channel_unref(cherr);
	g_spawn_close_pid(rsync_pid);
	scroll_to_end(view, TRUE);
	rsync_pid = 0;
	g_free(argv);
	if (watch_oldfile != NULL) g_free(watch_oldfile);
	watch_oldfile = NULL;

	if (config_errorlist && had_error) on_button_show_errors_clicked(NULL, NULL);
	else gtk_window_set_urgency_hint((GtkWindow*) rsync_window, TRUE);
	set_trayicon_icon(FALSE);
	if (more || (cmdline_execute && !(config_errorlist && had_error) && !cmdline_stayopen)) on_close_clicked(NULL, NULL);

	if (log_file != NULL) {
		fputs("\n", log_file);
		fclose(log_file);
		log_file = NULL;
	}
	set_main_title(groups[session_last], NULL);
}


gboolean out_watch(GIOChannel *source, GIOCondition condition, gpointer data) {
	GtkTextBuffer *buffer;
	GtkTextIter iter, iter2;
	GString *str;
	static gchar *carriage = NULL;
	gchar *file;
	gdouble fraction, done, total;

	if (condition & G_IO_IN) {
		buffer = gtk_text_view_get_buffer((GtkTextView*)data);

		if (carriage != NULL) {
			gtk_text_buffer_get_iter_at_line(buffer, &iter, gtk_text_buffer_get_line_count(buffer) - 2);
			gtk_text_buffer_get_end_iter(buffer, &iter2);
			gtk_text_buffer_delete(buffer, &iter, &iter2);
		}

		str = g_string_new("");
		g_io_channel_read_line_string(source, str, NULL, NULL);
		carriage = strchr(str->str, '\r');

		gtk_text_buffer_insert_at_cursor(buffer, (gchar*)str->str, -1);
		scroll_to_end((GtkTextView*)data, FALSE);
		if (log_file != NULL) fputs((gchar*)str->str, log_file);

		if (!dryrunning) {
			if (strchr(str->str, '%') && str->str[0] == ' ') {
				if (watch_oldfile != NULL) file = g_strconcat(watch_oldfile, NULL);
				else file = g_strconcat(str->str, NULL);

				fraction = g_ascii_strtod(strchr(strchr(str->str, '%') - 4, ' '), NULL) / 100;
				set_file_progress(fraction);

				fraction = 0;
				if (strchr(str->str, '%') != strrchr(str->str, '%')) fraction = g_ascii_strtod(strchr(strrchr(str->str, '%') - 6, ' '), NULL) / 100;
				else if (strchr(str->str, '=')) {
					total = g_ascii_strtod(strrchr(str->str, '/') + 1, NULL);
					done = total - g_ascii_strtod(strrchr(str->str, '=') + 1, NULL);
					fraction = done / total;
				}

				if (fraction) set_global_progress(fraction);
			} else {
				file = g_strconcat(str->str, NULL);
				if (watch_oldfile != NULL) g_free(watch_oldfile);
				watch_oldfile = g_strconcat(str->str, NULL);
			}
			g_strchomp(file);
			if (file[0] != 0) gtk_label_set_text((GtkLabel*) gtk_builder_get_object(builder, "label_file"), file);
			g_free(file);
		}

		g_string_free(str, TRUE);
		must_scroll = TRUE;
	} else {
//	if (condition & G_IO_HUP || condition & G_IO_ERR) {
		// g_io_channel_shutdown(source, FALSE, NULL);
		// rsync_cleanup(data);
		return FALSE;
	}
	return TRUE;
}


gboolean err_watch(GIOChannel *source, GIOCondition condition, gpointer data) {
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GString *str;
	gchar *tmpc;

	if (condition & G_IO_IN) {
		buffer = gtk_text_view_get_buffer((GtkTextView*)data);
		str = g_string_new("");

		g_io_channel_read_line_string(source, str, NULL, NULL);
		if (str->len > 2) had_error = TRUE;

		gtk_text_buffer_get_end_iter(buffer, &iter);
		gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, (gchar*)str->str, -1, "fore-red", NULL);
		scroll_to_end((GtkTextView*)data, FALSE);

		if (log_file != NULL) fputs((gchar*)str->str, log_file);
		tmpc = g_strconcat(error_list, str->str, NULL);
		g_free(error_list);
		error_list = tmpc;
		g_string_free(str, TRUE);
		must_scroll = TRUE;
	} else {
//	if (condition & G_IO_HUP || condition & G_IO_ERR) {
		// g_io_channel_shutdown(source, FALSE, NULL);
		// rsync_cleanup(data);
		return FALSE;
	}
	return TRUE;
}


void child_watch(GPid pid, gint status, gpointer data) {
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	gchar *tmpc, *str, buf[5];

	sprintf(buf, "%i", WEXITSTATUS(status));
	str = g_strconcat(_("Rsync process exit status: "), buf, "\n", NULL);

	buffer = gtk_text_view_get_buffer((GtkTextView*)data);
	if (status == 0) {
		gtk_text_buffer_insert_at_cursor(buffer, str, -1);
	} else {
		gtk_text_buffer_get_end_iter(buffer, &iter);
		gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, str, -1, "fore-red", NULL);
		tmpc = g_strconcat(error_list, str, NULL);
		g_free(error_list);
		error_list = tmpc;
		had_error = TRUE;
	}
	scroll_to_end((GtkTextView*)data, FALSE);

	if (log_file != NULL) fputs(str, log_file);
	g_free(str);
	rsync_cleanup(NULL);
	must_scroll = TRUE;
}


#if defined(__FreeBSD__) || defined(__APPLE__)
gboolean child_timeout(gpointer data) {
	gint status;
	if (waitpid(rsync_pid, &status, WNOHANG) > 0) {
		child_watch(rsync_pid, status, data);
		return FALSE;
	}
	return TRUE;
}
#endif


gboolean scroll_idle(gpointer data) {
	scroll_to_end(data, TRUE);
	return FALSE;
}


gchar* escape_cmd(gchar **inv) {
	gchar *tmp, **outv;
	gint i = 0;
	outv = g_new(gchar *, MAXOPT);
	while (inv[i] != NULL) {
		outv[i] = ((g_strstr_len(inv[i], -1, " ") || inv[i][0] == 0)? g_shell_quote(inv[i]) : g_strdup(inv[i]));
		i++;
	}
	outv[i] = NULL;
	tmp = g_strjoinv(" ", outv);
	g_free(outv);
	return tmp;
}


void on_rsync_show(GtkWidget *widget, gpointer user_data) {
	gint out, err;
	GtkTextBuffer *buffer;
	GtkTextView *view;
	static GtkTextTag *tag = NULL;
	GtkWidget *error;
	gchar *comm_str, *comm_text, *comm_out, *comm_err, *comm_before, *tmpc;
	gboolean halt = FALSE;
	time_t rawtime;

	rsync_pid = 0;
	if (!is_set || first) had_error = FALSE;
	gtk_label_set_ellipsize((GtkLabel*) gtk_builder_get_object(builder, "label_file"), PANGO_ELLIPSIZE_MIDDLE);

	if (get_checkbox("check_com_before")) {
		comm_str = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_com_before"));
		if (!g_spawn_command_line_sync(comm_str, &comm_out, &comm_err, &err, NULL) || err) {
			comm_out = g_strconcat("", NULL);
			comm_err = g_strconcat("Error launching command!", NULL);
			gtk_label_set_text((GtkLabel*) gtk_builder_get_object(builder, "label_file"), comm_err);
			if (get_checkbox("check_com_halt")) halt = TRUE;
		}
		comm_before = g_strconcat(_("** Launching BEFORE command:\n"), comm_str, "\n", comm_out, comm_err, "\n\n", NULL);
		g_free(comm_out);
		g_free(comm_err);
	} else {
		comm_before = g_strconcat("", NULL);
	}

	comm_str = escape_cmd(argv);
	if (halt) tmpc = "";
	else {
		if (dryrunning) tmpc =_("** Launching RSYNC command (simulation mode):\n");
		else tmpc = _("** Launching RSYNC command:\n");
	}
	time(&rawtime);
	comm_text = g_strconcat("**** ", groups[session_last], " - ", ctime(&rawtime), "\n", comm_before, tmpc, comm_str, "\n\n", NULL);
	g_free(comm_before);
	g_free(comm_str);

	view = (GtkTextView*)gtk_builder_get_object(builder, "textview_output");
	PangoFontDescription *font_desc = pango_font_description_from_string("monospace 10");
	gtk_widget_modify_font((GtkWidget*) view, font_desc);
	pango_font_description_free(font_desc);

	buffer = gtk_text_view_get_buffer(view);
	if (is_set && !first) gtk_text_buffer_insert_at_cursor(buffer, "\n", -1);
	else gtk_text_buffer_set_text(buffer, "", -1);
	gtk_text_buffer_insert_at_cursor(buffer, comm_text, -1);
	if (log_file != NULL) fputs(comm_text, log_file);
	g_free(comm_text);
	startime = g_get_monotonic_time() / 1000000;

	if (!halt && g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &rsync_pid, NULL, &out, &err, NULL)) {
		set_trayicon_icon(TRUE);
		chout = g_io_channel_unix_new(out);
		cherr = g_io_channel_unix_new(err);
		g_io_channel_set_flags(chout, G_IO_FLAG_NONBLOCK, NULL);
		g_io_channel_set_flags(cherr, G_IO_FLAG_NONBLOCK, NULL);
//	g_io_channel_set_buffer_size(chout, 32); g_io_channel_set_encoding(chout, NULL, NULL); g_io_channel_set_buffered(chout, FALSE);

		if (tag == NULL) tag = gtk_text_buffer_create_tag(buffer, "fore-red", "foreground", "red", NULL);
		g_io_add_watch_full(chout, G_PRIORITY_DEFAULT_IDLE - 10, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_PRI | G_IO_NVAL, out_watch, view, NULL);
		g_io_add_watch_full(cherr, G_PRIORITY_DEFAULT_IDLE - 10, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_PRI | G_IO_NVAL, err_watch, view, NULL);
		g_io_channel_set_close_on_unref(chout, TRUE);
		g_io_channel_set_close_on_unref(cherr, TRUE);
#if defined(__FreeBSD__) || defined(__APPLE__)
		g_timeout_add_seconds(1, child_timeout, view);
#else
		g_child_watch_add_full(G_PRIORITY_DEFAULT_IDLE, rsync_pid, child_watch, view, NULL);
#endif
	} else {
		gtk_button_set_label((GtkButton*) gtk_builder_get_object(builder, "close"), "gtk-close");
		gtk_widget_set_sensitive((GtkWidget*) gtk_builder_get_object(builder, "button_pause"), FALSE);
		gtk_widget_set_sensitive((GtkWidget*) view, TRUE);
		if (!halt) {
			error = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CANCEL, _("Cannot execute rsync"));
			gtk_dialog_run(GTK_DIALOG(error));
			gtk_widget_destroy(error);
		}
	}
}


void on_rsync_hide(GtkWidget *object, gpointer user_data) {
	if (rsync_pid) {
		if (paused) superkill(SIGCONT);
		superkill(SIGTERM);
	}
	dryrunning = FALSE;
}


void on_combo_session_changed(GtkComboBox *combobox, gpointer user_data) {
	gint sel = gtk_combo_box_get_active((GtkComboBox*) gtk_builder_get_object(builder, "combo_session"));
	if (session_last != -1 && sel != session_last) {
		save_settings(groups[session_last], NULL);
		session_last = sel;
		load_settings(groups[session_last], NULL);
	}
}


void on_session_add_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *add, *confirm;
	gint result, i = 0;

	add = (GtkWidget*) gtk_builder_get_object(builder, "dialog_new_session");
	gtk_editable_select_region((GtkEditable*) gtk_builder_get_object(builder, "entry_session_name"), 0, -1);
	gtk_widget_grab_focus((GtkWidget*) gtk_builder_get_object(builder, "entry_session_name"));
	result = gtk_dialog_run ((GtkDialog*) add);

	if (result == GTK_RESPONSE_OK) {
		save_settings(groups[session_last], NULL);

		gchar *newses;
		newses = (gchar*) gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_session_name"));
		is_set = get_checkbox("checkbutton_isset");

		if (strcmp(CONFIG_GROUP, newses) == 0 || strcmp("", newses) == 0 || strchr(newses, '/') != NULL) {
			confirm = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CANCEL, _("This session name is reserved, you cannot call it like this"));
			gtk_dialog_run(GTK_DIALOG(confirm));
			gtk_widget_destroy(confirm);
			gtk_widget_hide(add);
			return;
		}

		while (groups[i] != NULL && strcmp(groups[i], newses)) i++;
		if (groups[i] != NULL) {
			confirm = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CANCEL, _("A session named like this already exists"));
			gtk_dialog_run(GTK_DIALOG(confirm));
			gtk_widget_destroy(confirm);
			gtk_widget_hide(add);
			return;
		}

		save_settings(newses, NULL);
		session_number++;
		session_last = -1;		// added because of save session bug, copied from external patch
		load_groups(newses);
		load_settings(newses, NULL);
	}
	gtk_widget_hide(add);
}


void on_session_del_clicked(GtkButton *button, gpointer user_data) {
	gchar settings_file_name[MAXPATH], *key_data;
	FILE *key_file;
	GError *error_handler = NULL;
	GKeyFile *settings_file;
	gint i, result;
	GtkWidget *confirm;

	i = gtk_combo_box_get_active((GtkComboBox*) gtk_builder_get_object(builder, "combo_session"));
	if (strcmp(groups[i], "default") == 0) {
		confirm = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CANCEL, _("You cannot delete the default session"));
		gtk_dialog_run(GTK_DIALOG(confirm));
		gtk_widget_destroy(confirm);
		return;
	}

	confirm = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_OK_CANCEL, _("Are you sure you want to delete the '%s' session?"), groups[i]);
	result = gtk_dialog_run(GTK_DIALOG(confirm));
	gtk_widget_destroy(confirm);

	if (result == GTK_RESPONSE_OK) {
		g_sprintf(settings_file_name, "%s/%s", grsync_dir, "grsync.ini");
		settings_file = g_key_file_new();
		g_key_file_load_from_file(settings_file, settings_file_name, G_KEY_FILE_NONE, NULL);
		g_key_file_remove_group(settings_file, groups[i], &error_handler);

		key_data = g_key_file_to_data(settings_file, NULL, &error_handler);
		key_file = fopen(settings_file_name, "w");
		if (key_file != NULL) {
			fputs(key_data, key_file);
			fclose(key_file);
		} else {
			g_printf(_("\tUnable to save settings!\n"));
		}

		g_key_file_free(settings_file);
		g_clear_error(&error_handler);
		session_last = -1;
		load_groups("default");
		load_settings("default", NULL);
	}
}


void on_button_about_clicked(GtkButton *button, gpointer user_data) {
	gchar *artists[] = {
		_("Application icons by Roberto Gadotti <info@kuama.net>"),
		NULL
	};

	gchar *authors[] = {
		_("Written by Piero Orsoni <orsoni@gmail.com>"),
		_("Additional bug-fixing by Luca Marturana <lucamarturana@gmail.com>"),

		_("Dutch translation by Frank de Bruijn <f.d.de.bruijn@gmail.com>"),
		_("Simplified Chinese translation by Xie Yanbo <xieyanbo@gmail.com>"),
		_("French translation by xavier <ersplus@free.fr>"),
		_("Swedish translation by Daniel Nylander <info@danielnylander.se>"),
		_("Turkish translation by Doruk Fisek <dfisek@fisek.com.tr>"),
		_("Russian translation by Evgenii Terechkov <evg@altlinux.ru>"),
		_("German translation by Martin Lettner <m.lettner@gmail.com>"),
		_("Spanish translation by Ibon Castilla Varela <estri@euskalnet.net>"),
		_("Czech translation by Martin Balajka <m.balajka@gmail.com>"),
		_("Galician translation by Daniel Espinheira <daniel.espinheira@gmail.com>"),
		_("Catalan translation by Josep Sànchez <josep.sanchez@ubuntu.cat>"),
		_("Brazilian Portuguese translation by Fábio Nogueira <deb-user-ba@ubuntu.com>"),
		_("Indonesian translation by Waluyo Adi Siswanto <was.uthm@gmail.com>"),
		_("Croatian translation by Bojan Vondra <bojanvondra@hotmail.com>"),
		NULL
	};

	gtk_show_about_dialog((GtkWindow*) main_window, "name", PACKAGE, "version", VERSION, "comments", _("Synchronize files and folders (a GTK GUI for rsync)."),
		"copyright", _("(C) Piero Orsoni and others. Released under the GPL.\nSee COPYING for details"),
		"website", "http://www.opbyte.it/grsync/", "artists", artists, "authors", authors, NULL);
}


void on_button_switch_clicked(GtkButton *button, gpointer user_data) {
	const gchar *tmps, *tmpd;
	gchar *tmp;
	tmps = gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_source"));
	tmpd = gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "text_dest"));
	tmp = g_strconcat(tmps, NULL);
	gtk_entry_set_text((GtkEntry*) gtk_builder_get_object(builder, "text_source"), tmpd);
	gtk_entry_set_text((GtkEntry*) gtk_builder_get_object(builder, "text_dest"), tmp);
	g_free(tmp);
}


void on_button_pause_clicked(GtkButton *button, gpointer user_data) {
	GtkButton *tmpbutt = (GtkButton*) gtk_builder_get_object(builder, "button_pause");
	gint64 tmptime;
	if (paused) {
		if (rsync_pid) superkill(SIGCONT);
		gtk_button_set_label(tmpbutt, "gtk-media-pause");
		set_rsync_title(groups[session_last], "running");
		tmptime = g_get_monotonic_time() / 1000000;
		startime += tmptime - pausedtime;
	} else {
		if (rsync_pid) superkill(SIGSTOP);
		gtk_button_set_label(tmpbutt, "gtk-media-play");
		set_rsync_title(groups[session_last], "paused");
		pausedtime = g_get_monotonic_time() / 1000000;
		g_idle_add(scroll_idle, gtk_builder_get_object(builder, "textview_output"));
	}
	paused = !paused;
}


void on_preferences1_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget *config;
	gint result;

	config = (GtkWidget*) gtk_builder_get_object(builder, "dialog_config");
	gtk_entry_set_text((GtkEntry*) gtk_builder_get_object(builder, "entry_command"), config_command);
	set_checkbox("check_output", config_output);
	set_checkbox("check_remember", config_remember);
	set_checkbox("check_errorlist", config_errorlist);
	set_checkbox("check_log", config_log);
	set_checkbox("check_log_overwrite", config_log_overwrite);
	set_checkbox("check_fastscroll", config_fastscroll);
	set_checkbox("check_switchbutton", config_switchbutton);
	set_checkbox("check_trayicon", config_trayicon);

	result = gtk_dialog_run((GtkDialog*) config);
	if (result == GTK_RESPONSE_OK) {
		strncpy(config_command, gtk_entry_get_text((GtkEntry*) gtk_builder_get_object(builder, "entry_command")), MAXPATH - 1);
		config_output = get_checkbox("check_output");
		config_remember = get_checkbox("check_remember");
		config_errorlist = get_checkbox("check_errorlist");
		config_log = get_checkbox("check_log");
		config_log_overwrite = get_checkbox("check_log_overwrite");
		config_fastscroll = get_checkbox("check_fastscroll");
		config_switchbutton = get_checkbox("check_switchbutton");
//		gtk_widget_set_visible((GtkWidget*) gtk_builder_get_object(builder, "button_switch"), config_switchbutton);  ONLY WORKS IN GTK 2.18
		config_switchbutton ? gtk_widget_show((GtkWidget*) gtk_builder_get_object(builder, "button_switch")) :
			gtk_widget_hide((GtkWidget*) gtk_builder_get_object(builder, "button_switch"));

		config_trayicon = get_checkbox("check_trayicon");
		set_trayicon(config_trayicon);
	}
	gtk_widget_hide(config);
}


void on_rsync_info_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget *dialog;
	gchar *tmps, *result;

	tmps = g_strconcat(config_command, " --version", NULL);
	g_spawn_command_line_sync(tmps, &result, NULL, NULL, NULL);
	dialog = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", result);
	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);
	g_free(tmps);
	g_free(result);
}


void on_commandline_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget *dialog;
	gchar *tmps;

	build_rsync_cmd();
	tmps = escape_cmd(argv);
	dialog = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", tmps);
	dialog_set_labels_selectable(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);
	g_free(tmps);
	g_free(argv);
}


void on_import1_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget *dialog, *dialog2;
	gint retval;

	dialog = gtk_file_chooser_dialog_new (_("Browse"), (GtkWindow*) main_window, GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	if (user_data != NULL) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), user_data);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_ACCEPT || retval == GTK_RESPONSE_OK) {
		gchar *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		load_settings(NULL, filename);

		dialog2 = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, _("Session imported"));
		gtk_dialog_run(GTK_DIALOG(dialog2));
		gtk_widget_destroy(dialog2);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}


void on_export1_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget *dialog, *dialog2;
	gchar *filename;
	gint retval, i;

	i = gtk_combo_box_get_active((GtkComboBox*) gtk_builder_get_object(builder, "combo_session"));
	dialog = gtk_file_chooser_dialog_new (_("Browse"), (GtkWindow*) main_window, GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	filename = g_strconcat(groups[i], ".grsync", NULL);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), filename);
	g_free(filename);

	retval = gtk_dialog_run (GTK_DIALOG (dialog));
	if (retval == GTK_RESPONSE_ACCEPT || retval == GTK_RESPONSE_OK) {
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		save_settings(groups[i], filename);

		dialog2 = gtk_message_dialog_new((GtkWindow*) main_window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, _("Session exported"));
		gtk_dialog_run(GTK_DIALOG(dialog2));
		gtk_widget_destroy(dialog2);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}


void on_expander_rsync_activate(GtkExpander *expander, gpointer user_data) {
	if (!gtk_expander_get_expanded(expander)) {
		gtk_window_resize((GtkWindow*) rsync_window, initial_width, initial_height);
		gtk_widget_set_size_request(rsync_window, initial_width, initial_height);
	} else {
		gtk_widget_set_size_request(rsync_window, -1, -1);
	}
}


void on_button_show_errors_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *errors;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	static GtkTextTag *tag = NULL;

	errors = (GtkWidget*) gtk_builder_get_object(builder, "dialog_errors");
	buffer = gtk_text_view_get_buffer((GtkTextView*) gtk_builder_get_object(builder, "textview_errors"));
	gtk_text_buffer_set_text(buffer, "", -1);
	if (tag == NULL) tag = gtk_text_buffer_create_tag(buffer, "fore-red", "foreground", "red", NULL);
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_insert_with_tags(buffer, &iter, error_list, -1, tag, NULL);
	gtk_widget_show(errors);
}


gboolean on_main_window_window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data) {
	if (trayIcon != NULL && event->changed_mask == GDK_WINDOW_STATE_ICONIFIED && (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) == GDK_WINDOW_STATE_ICONIFIED) {
//		gtk_widget_hide(GTK_WIDGET(widget));
		gtk_window_set_skip_taskbar_hint((GtkWindow*) widget, TRUE);
	}
	// else if (event->changed_mask == GDK_WINDOW_STATE_WITHDRAWN && (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) != GDK_WINDOW_STATE_ICONIFIED) {
	return TRUE;
}


void on_contribute_activate(GtkMenuItem *menuitem, gpointer user_data) {
	g_spawn_command_line_async("xdg-open http://www.opbyte.it/contribute.html", NULL);
}


void on_cellrenderertoggle1_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
	GtkTreeIter iter;
	GtkTreePath *path_filter = gtk_tree_path_new_from_string(path_str);
	GtkTreePath *path = gtk_tree_model_filter_convert_path_to_child_path(filter, path_filter);
	gboolean enabled;

	gtk_tree_model_get_iter((GtkTreeModel*) liststore_session, &iter, path);
	gtk_tree_model_get((GtkTreeModel*) liststore_session, &iter, 1, &enabled, -1);
	enabled = !enabled;
	gtk_list_store_set(liststore_session, &iter, 1, enabled, -1);
	gtk_tree_path_free(path_filter);
	gtk_tree_path_free(path);
}
