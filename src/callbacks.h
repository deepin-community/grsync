#define MAXPATH 2048
#define MAXOPT 100
#define CONFIG_GROUP "__CONFIG"

#define ICON_SOURCE "pixmaps/grsync.png"
#define ICON_SOURCE_BUSY "pixmaps/grsync-busy.png"
#define ICON_PACKAGE PACKAGE_DATA_DIR "/" ICON_SOURCE
#define ICON_PACKAGE_BUSY PACKAGE_DATA_DIR "/" ICON_SOURCE_BUSY

extern GtkBuilder *builder;
extern GtkWidget *main_window;
extern gchar *argv_session, *argv_filename, *icon, *icon_busy;
extern gboolean cmdline_session, cmdline_execute, cmdline_stayopen, cmdline_import;
extern GtkListStore *liststore_session;

void save_settings(gchar *session, gchar *filename);
gboolean load_groups(gchar *session);
void on_main_create(GtkWindow *window, gpointer user_data);
void on_expander_rsync_activate(GtkExpander *expander, gpointer user_data);
void on_button_show_errors_clicked(GtkButton *button, gpointer user_data);
void on_import1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_rsync_show(GtkWidget *widget, gpointer user_data);
