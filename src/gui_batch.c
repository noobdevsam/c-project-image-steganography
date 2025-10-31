#include "../include/gui_batch.h"
#include "../include/batch.h"
#include "../include/stego_core.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>

/* Structure representing a batch task configuration panel */
typedef struct {
    gchar *task_id;                     // Unique task identifier
    GtkWidget *container;               // Main expander widget
    GtkWidget *grid;                    // Grid layout for form fields
    GtkWidget *input_chooser;           // Button for input file
    GtkWidget *output_chooser;          // Button for output dir
    GtkWidget *payload_type_combo;      // Encode only: Text/File dropdown
    GtkWidget *payload_text_view;       // Encode only: Text input
    GtkWidget *payload_file_chooser;    // Encode only: File button
    GtkWidget *payload_stack;           // Encode only: Switch between text/file
    GtkWidget *password_entry;          // Password field
    GtkWidget *lsb_combo;               // Encode only: LSB depth
    GtkWidget *progress_bar;            // Progress indicator
    GtkWidget *status_label;            // Status text
    GtkWidget *remove_button;           // Delete task button
    
    // Selected values
    GFile *input_file;
    GFile *output_folder;
    GFile *payload_file;
    gchar *password;
    gint lsb_depth;
    gboolean is_encode;
    gboolean is_processing;
    
    // Task execution
    GTask *running_task;                // Reference to running GTask
} BatchTaskPanel;

/* helper user data passed to batch callbacks */
typedef struct
{
    gchar *task_id; /* owned */
    BatchTaskPanel *panel; /* weak reference */
} GuiBatchUserData;

static GtkWidget *task_list_box;          // Container for task panels
static GtkWidget *start_all_button;       // Button to start all tasks
static GtkWidget *mode_combo;             // Mode dropdown
static GHashTable *task_panels;           // task_id -> BatchTaskPanel*
static gint task_counter = 0;             // Counter for generating unique task IDs

/* Forward declarations */
static void batch_task_panel_free(BatchTaskPanel *panel);
static void gui_batch_progress_cb(gpointer user_data, double fraction);
static void gui_batch_finished_cb(gpointer user_data, gboolean success, const char *message);
static void on_remove_task_clicked(GtkButton *button, gpointer user_data);
static void update_start_button_sensitivity(void);
static GtkWidget *create_batch_task_panel(gboolean is_encode);
static void start_all_tasks(void);

/* Helper: Free a BatchTaskPanel */
static void batch_task_panel_free(BatchTaskPanel *panel)
{
    if (!panel) return;
    
    g_free(panel->task_id);
    g_free(panel->password);
    
    if (panel->input_file)
        g_object_unref(panel->input_file);
    if (panel->output_folder)
        g_object_unref(panel->output_folder);
    if (panel->payload_file)
        g_object_unref(panel->payload_file);
    if (panel->running_task)
        g_object_unref(panel->running_task);
    
    g_free(panel);
}

/* Callback: Input file selection for task panel */
static void on_task_input_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    
    if (file != NULL) {
        if (panel->input_file)
            g_object_unref(panel->input_file);
        
        panel->input_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(panel->input_chooser), basename);
        g_free(basename);
        update_start_button_sensitivity();
    } else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        g_warning("Error selecting input file: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback: Output directory selection for task panel */
static void on_task_output_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
    
    if (file != NULL) {
        if (panel->output_folder)
            g_object_unref(panel->output_folder);
        
        panel->output_folder = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(panel->output_chooser), basename);
        g_free(basename);
        update_start_button_sensitivity();
    } else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        g_warning("Error selecting output directory: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback: Payload file selection for encode task panel */
static void on_task_payload_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    
    if (file != NULL) {
        if (panel->payload_file)
            g_object_unref(panel->payload_file);
        
        panel->payload_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(panel->payload_file_chooser), basename);
        g_free(basename);
        update_start_button_sensitivity();
    } else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        g_warning("Error selecting payload file: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback: Input file chooser button clicked */
static void on_task_input_chooser_clicked(GtkButton *button, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    
    GtkFileDialog *dialog = gtk_file_dialog_new();
    const char *title = panel->is_encode ? "Select Input Image" : "Select Stego Image";
    gtk_file_dialog_set_title(dialog, title);
    
    // Create a file filter for image files
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Image Files");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filter);
    g_object_unref(filters);
    
    gtk_file_dialog_open(dialog, NULL, NULL, on_task_input_selected, panel);
}

/* Callback: Output directory chooser button clicked */
static void on_task_output_chooser_clicked(GtkButton *button, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Output Directory");
    
    gtk_file_dialog_select_folder(dialog, NULL, NULL, on_task_output_selected, panel);
}

/* Callback: Payload file chooser button clicked (encode only) */
static void on_task_payload_file_chooser_clicked(GtkButton *button, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Payload File");
    
    gtk_file_dialog_open(dialog, NULL, NULL, on_task_payload_file_selected, panel);
}

/* Callback: Payload type changed (encode only) */
static void on_task_payload_type_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    guint selected = gtk_drop_down_get_selected(dropdown);
    
    // 0 = Text Message, 1 = File
    if (selected == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(panel->payload_stack), "text");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(panel->payload_stack), "file");
    }
    update_start_button_sensitivity();
}

/* Callback: Remove task button clicked */
static void on_remove_task_clicked(GtkButton *button, gpointer user_data)
{
    BatchTaskPanel *panel = (BatchTaskPanel *)user_data;
    
    // Don't allow removal if task is processing
    if (panel->is_processing) {
        return;
    }
    
    // Save container widget pointer before removing from hash table
    // (hash table removal will free the panel structure)
    GtkWidget *container = panel->container;
    GtkWidget *parent = gtk_widget_get_parent(container);
    
    // Remove from hash table (this frees the panel via batch_task_panel_free)
    g_hash_table_remove(task_panels, panel->task_id);
    
    // Remove from UI using saved container pointer
    if (parent) {
        gtk_box_remove(GTK_BOX(parent), container);
    }
    
    // Update button sensitivity
    update_start_button_sensitivity();
}

