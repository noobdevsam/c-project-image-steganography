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


/* Helper: Check if a task panel is ready to start */
static gboolean task_panel_is_ready(BatchTaskPanel *panel)
{
    if (!panel->input_file || !panel->output_folder)
        return FALSE;
    
    if (panel->is_encode) {
        // For encode, need payload (either text or file)
        guint payload_type = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->payload_type_combo));
        
        if (payload_type == 0) {
            // Text message - check if there's text
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(panel->payload_text_view));
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
            gboolean has_text = (strlen(text) > 0);
            g_free(text);
            if (!has_text)
                return FALSE;
        } else {
            // File payload - check if file is selected
            if (!panel->payload_file)
                return FALSE;
        }
    }
    
    return TRUE;
}

/* Update Start All Tasks button sensitivity based on task states */
static void update_start_button_sensitivity(void)
{
    gboolean has_ready_tasks = FALSE;
    
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, task_panels);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        BatchTaskPanel *panel = (BatchTaskPanel *)value;
        if (!panel->is_processing && task_panel_is_ready(panel)) {
            has_ready_tasks = TRUE;
            break;
        }
    }
    
    gtk_widget_set_sensitive(start_all_button, has_ready_tasks);
}

/* Create a task configuration panel (Encode or Decode) */
static GtkWidget *create_batch_task_panel(gboolean is_encode)
{
    // Create unique task ID
    gchar *task_id = g_strdup_printf("task_%d", ++task_counter);
    
    // Create panel structure
    BatchTaskPanel *panel = g_new0(BatchTaskPanel, 1);
    panel->task_id = g_strdup(task_id);
    panel->is_encode = is_encode;
    panel->is_processing = FALSE;
    panel->lsb_depth = 3; // Default LSB depth
    
    // Create expander for collapsible panel
    const char *label_text = is_encode ? "Encode" : "Decode";
    gchar *expander_label = g_strdup_printf("Task %d - %s", task_counter, label_text);
    
    GtkWidget *expander = gtk_expander_new(expander_label);
    g_free(expander_label);
    gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);
    panel->container = expander;
    
    // Create main frame
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_margin_top(frame, 5);
    gtk_widget_set_margin_bottom(frame, 5);
    gtk_widget_set_margin_start(frame, 5);
    gtk_widget_set_margin_end(frame, 5);
    
    // Create vertical box for frame content
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    
    // Create grid for form fields
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_hexpand(grid, TRUE);
    panel->grid = grid;
    
    int row = 0;
    
    // Input Image
    GtkWidget *label_input = gtk_label_new(is_encode ? "Input Image:" : "Stego Image:");
    gtk_widget_set_halign(label_input, GTK_ALIGN_END);
    panel->input_chooser = gtk_button_new_with_label(is_encode ? "Select input image" : "Select stego image");
    gtk_widget_set_hexpand(panel->input_chooser, TRUE);
    g_signal_connect(panel->input_chooser, "clicked", G_CALLBACK(on_task_input_chooser_clicked), panel);
    
    gtk_grid_attach(GTK_GRID(grid), label_input, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->input_chooser, 1, row, 2, 1);
    row++;
    
    // Output Directory
    GtkWidget *label_output = gtk_label_new("Output Directory:");
    gtk_widget_set_halign(label_output, GTK_ALIGN_END);
    panel->output_chooser = gtk_button_new_with_label("Select output directory");
    gtk_widget_set_hexpand(panel->output_chooser, TRUE);
    g_signal_connect(panel->output_chooser, "clicked", G_CALLBACK(on_task_output_chooser_clicked), panel);
    
    gtk_grid_attach(GTK_GRID(grid), label_output, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->output_chooser, 1, row, 2, 1);
    row++;
    
    // Encode-specific fields
    if (is_encode) {
        // Payload Type
        GtkWidget *label_payload_type = gtk_label_new("Payload Type:");
        gtk_widget_set_halign(label_payload_type, GTK_ALIGN_END);
        
        const char *payload_type_options[] = {"Text Message", "File", NULL};
        GtkStringList *payload_type_list = gtk_string_list_new(payload_type_options);
        panel->payload_type_combo = gtk_drop_down_new(G_LIST_MODEL(payload_type_list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->payload_type_combo), 0);
        g_signal_connect(panel->payload_type_combo, "notify::selected", G_CALLBACK(on_task_payload_type_changed), panel);
        
        gtk_grid_attach(GTK_GRID(grid), label_payload_type, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), panel->payload_type_combo, 1, row, 2, 1);
        row++;
        
        // Payload input area
        GtkWidget *label_payload = gtk_label_new("Payload:");
        gtk_widget_set_halign(label_payload, GTK_ALIGN_START);
        gtk_widget_set_valign(label_payload, GTK_ALIGN_START);
        
        // Create a stack to hold text message input and file chooser
        panel->payload_stack = gtk_stack_new();
        gtk_widget_set_hexpand(panel->payload_stack, TRUE);
        
        // Text message input
        panel->payload_text_view = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(panel->payload_text_view), GTK_WRAP_WORD);
        gtk_widget_set_hexpand(panel->payload_text_view, TRUE);
        
        GtkWidget *scroll_window = gtk_scrolled_window_new();
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_window), panel->payload_text_view);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window), 
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_hexpand(scroll_window, TRUE);
        gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll_window), TRUE);
        gtk_widget_set_size_request(scroll_window, -1, 100);
        
        // File payload chooser
        panel->payload_file_chooser = gtk_button_new_with_label("Select payload file");
        gtk_widget_set_hexpand(panel->payload_file_chooser, TRUE);
        gtk_widget_set_valign(panel->payload_file_chooser, GTK_ALIGN_START);
        g_signal_connect(panel->payload_file_chooser, "clicked", G_CALLBACK(on_task_payload_file_chooser_clicked), panel);
        
        // Add both widgets to the stack
        gtk_stack_add_named(GTK_STACK(panel->payload_stack), scroll_window, "text");
        gtk_stack_add_named(GTK_STACK(panel->payload_stack), panel->payload_file_chooser, "file");
        gtk_stack_set_visible_child_name(GTK_STACK(panel->payload_stack), "text");
        
        gtk_grid_attach(GTK_GRID(grid), label_payload, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), panel->payload_stack, 1, row, 2, 1);
        row++;
        
        // LSB Depth
        GtkWidget *label_lsb = gtk_label_new("LSB Depth:");
        gtk_widget_set_halign(label_lsb, GTK_ALIGN_END);
        
        const char *lsb_options[] = {"1", "2", "3", NULL};
        GtkStringList *lsb_list = gtk_string_list_new(lsb_options);
        panel->lsb_combo = gtk_drop_down_new(G_LIST_MODEL(lsb_list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->lsb_combo), 0);
        
        gtk_grid_attach(GTK_GRID(grid), label_lsb, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), panel->lsb_combo, 1, row, 1, 1);
        row++;
    }
    
    // Password (both encode and decode)
    GtkWidget *label_password = gtk_label_new("Password:");
    gtk_widget_set_halign(label_password, GTK_ALIGN_END);
    panel->password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(panel->password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(panel->password_entry), "Optional");
    gtk_widget_set_hexpand(panel->password_entry, TRUE);
    
    gtk_grid_attach(GTK_GRID(grid), label_password, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->password_entry, 1, row, 2, 1);
    row++;
    
    gtk_box_append(GTK_BOX(vbox), grid);
    
    // Progress bar
    panel->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(panel->progress_bar, TRUE);
    gtk_box_append(GTK_BOX(vbox), panel->progress_bar);
    
    // Status label and remove button in horizontal box
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    panel->status_label = gtk_label_new("Ready");
    gtk_widget_set_halign(panel->status_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(panel->status_label, TRUE);
    
    panel->remove_button = gtk_button_new_with_label("✕ Remove");
    g_signal_connect(panel->remove_button, "clicked", G_CALLBACK(on_remove_task_clicked), panel);
    
    gtk_box_append(GTK_BOX(bottom_box), panel->status_label);
    gtk_box_append(GTK_BOX(bottom_box), panel->remove_button);
    gtk_box_append(GTK_BOX(vbox), bottom_box);
    
    gtk_frame_set_child(GTK_FRAME(frame), vbox);
    gtk_expander_set_child(GTK_EXPANDER(expander), frame);
    
    // Add to hash table
    g_hash_table_insert(task_panels, g_strdup(task_id), panel);
    g_free(task_id);
    
    return expander;
}



/* Start a single task panel */
static void start_task_panel(BatchTaskPanel *panel)
{
    // Mark as processing
    panel->is_processing = TRUE;
    
    // Disable remove button
    gtk_widget_set_sensitive(panel->remove_button, FALSE);
    
    // Disable input fields
    gtk_widget_set_sensitive(panel->input_chooser, FALSE);
    gtk_widget_set_sensitive(panel->output_chooser, FALSE);
    gtk_widget_set_sensitive(panel->password_entry, FALSE);
    
    if (panel->is_encode) {
        gtk_widget_set_sensitive(panel->payload_type_combo, FALSE);
        gtk_widget_set_sensitive(panel->payload_text_view, FALSE);
        gtk_widget_set_sensitive(panel->payload_file_chooser, FALSE);
        gtk_widget_set_sensitive(panel->lsb_combo, FALSE);
    }
    
    // Update status
    gtk_label_set_text(GTK_LABEL(panel->status_label), panel->is_encode ? "Encoding..." : "Decoding...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(panel->progress_bar), 0.0);
    
    // Get password
    GtkEntryBuffer *buffer = gtk_entry_get_buffer(GTK_ENTRY(panel->password_entry));
    const gchar *password = gtk_entry_buffer_get_text(buffer);
    if (password && strlen(password) > 0) {
        panel->password = g_strdup(password);
    }
    
    // Prepare user data for callbacks
    GuiBatchUserData *ud = g_new0(GuiBatchUserData, 1);
    ud->task_id = g_strdup(panel->task_id);
    ud->panel = panel;
    
    if (!panel->is_encode) {
        // Decode task
        char *stego_path = g_file_get_path(panel->input_file);
        char *output_dir = g_file_get_path(panel->output_folder);
        
        panel->running_task = batch_decode_async(stego_path, output_dir, panel->password,
                                                  gui_batch_progress_cb, gui_batch_finished_cb, ud);
        
        g_free(stego_path);
        g_free(output_dir);
    } else {
        // Encode task
        char *cover_path = g_file_get_path(panel->input_file);
        char *output_dir = g_file_get_path(panel->output_folder);
        
        // Get LSB depth
        guint lsb_selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->lsb_combo));
        panel->lsb_depth = lsb_selected + 1; // 0,1,2 -> 1,2,3
        
        // Get payload
        guint payload_type = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->payload_type_combo));

        srand(time(NULL));
        int rand_suffix = rand() % 10000;
        
        if (payload_type == 0) {
            // Text message - save to temp file
            GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(panel->payload_text_view));
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(text_buffer, &start, &end);
            char *text = gtk_text_buffer_get_text(text_buffer, &start, &end, FALSE);
            
            // Create temp file for text payload
            char temp_path[] = "/tmp/batch_payload_XXXXXX";
            int fd = mkstemp(temp_path);
            if (fd != -1) {
                write(fd, text, strlen(text));
                close(fd);
                
                // Generate output filename
                char *input_basename = g_file_get_basename(panel->input_file);
                char *dot = strrchr(input_basename, '.');
                char output_filename[512];

                if (dot) {
                    size_t base_len = dot - input_basename;
                    snprintf(output_filename, sizeof(output_filename), "%.*s_stego_%d.png", (int)base_len, input_basename, rand_suffix);
                } else {
                    snprintf(output_filename, sizeof(output_filename), "%s_stego_%d.png", input_basename, rand_suffix);
                }
                
                char output_path[1024];
                snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, output_filename);
                
                panel->running_task = batch_encode_async(cover_path, temp_path, output_path, 
                                                         panel->lsb_depth, panel->password,
                                                         gui_batch_progress_cb, gui_batch_finished_cb, ud);
                
                // Clean up temp file after a delay (will be done in callback)
                g_free(input_basename);
            }
            
            g_free(text);
        } else {
            // File payload
            char *payload_path = g_file_get_path(panel->payload_file);
            
            // Generate output filename
            char *input_basename = g_file_get_basename(panel->input_file);
            char *dot = strrchr(input_basename, '.');
            char output_filename[512];
            if (dot) {
                size_t base_len = dot - input_basename;
                snprintf(output_filename, sizeof(output_filename), "%.*s_stego_%d.png", (int)base_len, input_basename, rand_suffix);
            } else {
                snprintf(output_filename, sizeof(output_filename), "%s_stego_%d.png", input_basename, rand_suffix);
            }
            
            char output_path[1024];
            snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, output_filename);
            
            panel->running_task = batch_encode_async(cover_path, payload_path, output_path,
                                                     panel->lsb_depth, panel->password,
                                                     gui_batch_progress_cb, gui_batch_finished_cb, ud);
            
            g_free(payload_path);
            g_free(input_basename);
        }
        
        g_free(cover_path);
        g_free(output_dir);
    }
}


/* Start all ready tasks */
static void start_all_tasks(void)
{
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, task_panels);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        BatchTaskPanel *panel = (BatchTaskPanel *)value;
        if (!panel->is_processing && task_panel_is_ready(panel)) {
            start_task_panel(panel);
        }
    }
    
    // Disable start button while tasks are running
    gtk_widget_set_sensitive(start_all_button, FALSE);
}

/* Callback: When user clicks "+ Add Task" */
static void on_add_task_clicked(GtkButton *button, gpointer user_data)
{
    // Get selected mode
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(mode_combo));
    gboolean is_encode = (selected == 0); // 0 = Encode, 1 = Decode
    
    // Create new task panel
    GtkWidget *panel_widget = create_batch_task_panel(is_encode);
    
    // Add to task list
    gtk_box_append(GTK_BOX(task_list_box), panel_widget);
    
    // Update button sensitivity
    update_start_button_sensitivity();
}

/* Callback: Start All Tasks button clicked */
static void on_start_all_clicked(GtkButton *button, gpointer user_data)
{
    start_all_tasks();
}

/* Public function: create the entire batch tab */
GtkWidget *gui_batch_create_tab(void)
{
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(main_vbox, 10);
    gtk_widget_set_margin_bottom(main_vbox, 10);
    gtk_widget_set_margin_start(main_vbox, 10);
    gtk_widget_set_margin_end(main_vbox, 10);

    // Initialize task panels hash table
    task_panels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, 
                                        (GDestroyNotify)batch_task_panel_free);

    // Top controls box: Mode selector and Add Task button
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    // Mode selection: Encode / Decode
    GtkWidget *label_mode = gtk_label_new("Mode:");
    const char *mode_options[] = {"Encode", "Decode", NULL};
    GtkStringList *mode_list = gtk_string_list_new(mode_options);
    mode_combo = gtk_drop_down_new(G_LIST_MODEL(mode_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(mode_combo), 0); // Default = Encode
    
    // Add Task button
    GtkWidget *button_add = gtk_button_new_with_label("+ Add Task");
    
    gtk_box_append(GTK_BOX(top_box), label_mode);
    gtk_box_append(GTK_BOX(top_box), mode_combo);
    gtk_widget_set_hexpand(mode_combo, FALSE);
    gtk_box_append(GTK_BOX(top_box), button_add);
    gtk_widget_set_halign(button_add, GTK_ALIGN_END);
    gtk_widget_set_hexpand(button_add, TRUE);
    
    gtk_box_append(GTK_BOX(main_vbox), top_box);

    // Scrollable task list
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    
    // Task list container
    task_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), task_list_box);
    
    gtk_box_append(GTK_BOX(main_vbox), scrolled_window);

    // Start All Tasks button at the bottom
    start_all_button = gtk_button_new_with_label("Start All Tasks");
    gtk_widget_set_hexpand(start_all_button, TRUE);
    gtk_widget_set_sensitive(start_all_button, FALSE); // Disabled by default
    
    gtk_box_append(GTK_BOX(main_vbox), start_all_button);

    // Connect signals
    g_signal_connect(button_add, "clicked", G_CALLBACK(on_add_task_clicked), NULL);
    g_signal_connect(start_all_button, "clicked", G_CALLBACK(on_start_all_clicked), NULL);

    return main_vbox;
}

/* Called on the main thread by batch.c (safe to update GTK widgets directly) */
static void gui_batch_progress_cb(gpointer user_data, double fraction)
{
    GuiBatchUserData *ud = (GuiBatchUserData *)user_data;
    if (!ud || !ud->task_id || !ud->panel)
        return;

    BatchTaskPanel *panel = ud->panel;
    
    // Update progress bar
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(panel->progress_bar), fraction);
    
    // Update status label
    if (fraction > 0.0 && fraction < 1.0) {
        gchar *status_text = g_strdup_printf("%s... %.0f%%", 
                                             panel->is_encode ? "Encoding" : "Decoding",
                                             fraction * 100);
        gtk_label_set_text(GTK_LABEL(panel->status_label), status_text);
        g_free(status_text);
    }
}

/* Called on the main thread when a task finishes */
static void gui_batch_finished_cb(gpointer user_data, gboolean success, const char *message)
{
    GuiBatchUserData *ud = (GuiBatchUserData *)user_data;
    if (!ud || !ud->task_id || !ud->panel)
        goto cleanup;

    BatchTaskPanel *panel = ud->panel;
    
    // Update progress bar to 100%
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(panel->progress_bar), 1.0);
    
    // Update status label with color coding
    if (success) {
        gtk_label_set_markup(GTK_LABEL(panel->status_label), "<span foreground='green'>Complete ✓</span>");
    } else {
        gchar *error_markup = g_markup_printf_escaped("<span foreground='red'>Failed: %s</span>", 
                                                       message ? message : "Unknown error");
        gtk_label_set_markup(GTK_LABEL(panel->status_label), error_markup);
        g_free(error_markup);
    }
    
    // Mark task as no longer processing
    panel->is_processing = FALSE;
    
    // Re-enable remove button
    gtk_widget_set_sensitive(panel->remove_button, TRUE);
    
    // Check if all tasks are done, then re-enable start button if there are ready tasks
    update_start_button_sensitivity();

cleanup:
    // Free the user_data we allocated when submitting the task
    if (ud) {
        g_free(ud->task_id);
        g_free(ud);
    }
}

/* Old function kept for backward compatibility if needed elsewhere */
void gui_batch_update_progress(const gchar *task_id, double progress, gboolean done)
{
    // This function may be called from other parts of the code
    // Look up the task panel and update it
    BatchTaskPanel *panel = g_hash_table_lookup(task_panels, task_id);
    if (panel) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(panel->progress_bar), progress);
        if (done) {
            gtk_label_set_text(GTK_LABEL(panel->status_label), "Complete");
        }
    }
}
