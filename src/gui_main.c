#include "../include/gui_main.h"
#include "../include/stego_core.h"
#include "../include/batch.h"
#include "../include/aes_wrapper.h"
#include "../include/gui_batch.h"
#include "../include/image_io.h"
#include "../include/payload.h"
#include "../include/metadata.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
	#include <process.h>
	#define getpid _getpid
#else
	#include <unistd.h>
#endif

static GtkWidget *window;
static GtkWidget *notebook_tabs; // encode / decode / batch

// Encode tab widgets
static GtkWidget *encode_file_chooser_input;
static GtkWidget *encode_file_chooser_output;
static GtkWidget *encode_entry_password;
static GtkWidget *encode_combo_lsb_depth;
static GtkWidget *encode_progress_bar;
static GtkWidget *button_encode;
static GtkWidget *encode_combo_payload_type;
static GtkWidget *encode_text_view_message;
static GtkWidget *encode_scroll_window_message;
static GtkWidget *encode_file_chooser_payload;
static GtkWidget *encode_payload_stack;

// Decode tab widgets
static GtkWidget *decode_file_chooser_input;
static GtkWidget *decode_file_chooser_output;
static GtkWidget *decode_entry_password;
static GtkWidget *decode_progress_bar;
static GtkWidget *button_decode;

// Store selected file paths - separate for encode and decode
static GFile *encode_selected_input_file = NULL;
static GFile *encode_selected_output_file = NULL;
static GFile *encode_selected_payload_file = NULL;
static GFile *decode_selected_input_file = NULL;
static GFile *decode_selected_output_file = NULL;

// Structure to pass data for JPEG warning callback
typedef struct {
    gboolean dismissed;
    GMainLoop *loop;
} DialogData;

/* Callback: Window close - terminate application */
static void on_window_destroy(GtkWindow *window, gpointer user_data)
{
    (void)window;
    (void)user_data;
    exit(0);  // Immediately terminate the entire process
}

/* Callback for JPEG warning dialog */
static void on_jpeg_warning_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    DialogData *data = (DialogData *)user_data;
    data->dismissed = TRUE;
    if (data->loop && g_main_loop_is_running(data->loop)) {
        g_main_loop_quit(data->loop);
    }
}


/* Callback for encode input file selection */
static void on_encode_input_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    
    if (file != NULL)
    {
        if (encode_selected_input_file != NULL)
            g_object_unref(encode_selected_input_file);
        
        encode_selected_input_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(encode_file_chooser_input), basename);
        g_free(basename);
    }
    else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
    {
        g_warning("Error selecting input file: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}


/* Callback for decode input file selection */
static void on_decode_input_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    
    if (file != NULL)
    {
        if (decode_selected_input_file != NULL)
            g_object_unref(decode_selected_input_file);
        
        decode_selected_input_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(decode_file_chooser_input), basename);
        g_free(basename);
    }
    else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
    {
        g_warning("Error selecting input file: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback for encode output directory selection */
static void on_encode_output_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
    
    if (file != NULL)
    {
        if (encode_selected_output_file != NULL)
            g_object_unref(encode_selected_output_file);
        
        encode_selected_output_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(encode_file_chooser_output), basename);
        g_free(basename);
    }
    else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
    {
        g_warning("Error selecting output directory: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback for decode output directory selection */
static void on_decode_output_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
    
    if (file != NULL)
    {
        if (decode_selected_output_file != NULL)
            g_object_unref(decode_selected_output_file);
        
        decode_selected_output_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(decode_file_chooser_output), basename);
        g_free(basename);
    }
    else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
    {
        g_warning("Error selecting output directory: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback for encode payload file selection */
static void on_encode_payload_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    
    if (file != NULL)
    {
        if (encode_selected_payload_file != NULL)
            g_object_unref(encode_selected_payload_file);
        
        encode_selected_payload_file = file;
        char *basename = g_file_get_basename(file);
        gtk_button_set_label(GTK_BUTTON(encode_file_chooser_payload), basename);
        g_free(basename);
    }
    else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
    {
        g_warning("Error selecting payload file: %s", error->message);
    }
    
    if (error != NULL)
        g_error_free(error);
}

/* Callback: Open encode payload file dialog */
static void on_encode_payload_chooser_clicked(GtkButton *button, gpointer user_data)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Payload File");
    
    gtk_file_dialog_open(dialog, GTK_WINDOW(window), NULL, on_encode_payload_file_selected, NULL);
}

/* Callback: Encode payload type changed */
static void on_encode_payload_type_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    guint selected = gtk_drop_down_get_selected(dropdown);
    
    // 0 = Text Message, 1 = File
    if (selected == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(encode_payload_stack), "text");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(encode_payload_stack), "file");
    }
}

/* Callback: Open encode input file dialog */
static void on_encode_input_chooser_clicked(GtkButton *button, gpointer user_data)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Input Image");
    
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
    
    gtk_file_dialog_open(dialog, GTK_WINDOW(window), NULL, on_encode_input_file_selected, NULL);
}


/* Callback: Open decode input file dialog */
static void on_decode_input_chooser_clicked(GtkButton *button, gpointer user_data)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Stego Image");
    
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
    
    gtk_file_dialog_open(dialog, GTK_WINDOW(window), NULL, on_decode_input_file_selected, NULL);
}

/* Callback: Open encode output directory dialog */
static void on_encode_output_chooser_clicked(GtkButton *button, gpointer user_data)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Output Directory");
    
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(window), NULL, on_encode_output_file_selected, NULL);
}

/* Callback: Open decode output directory dialog */
static void on_decode_output_chooser_clicked(GtkButton *button, gpointer user_data)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Output Directory");
    
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(window), NULL, on_decode_output_file_selected, NULL);
}

/* Callback: Encode single file */
static void on_encode_clicked(GtkButton *button, gpointer user_data)
{
    if (!encode_selected_input_file || !encode_selected_output_file)
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Please select input image and output directory.");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        return;
    }
    
    // Get payload type
    guint payload_type = gtk_drop_down_get_selected(GTK_DROP_DOWN(encode_combo_payload_type));
    
    // Check payload source
    if (payload_type == 1 && !encode_selected_payload_file) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Please select a payload file.");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        return;
    }
    
    GtkEntryBuffer *buffer = gtk_entry_get_buffer(GTK_ENTRY(encode_entry_password));
    const gchar *password = gtk_entry_buffer_get_text(buffer);
    gint lsb_depth = gtk_drop_down_get_selected(GTK_DROP_DOWN(encode_combo_lsb_depth)) + 1;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.1);
    
    // Get input path and check if it's JPEG
    char *input_path = g_file_get_path(encode_selected_input_file);
    char *actual_cover_path = NULL;
    bool jpeg_converted = false;
    
    if (image_is_jpeg(input_path)) {
        // Show warning about JPEG and wait for user to acknowledge
        char warning_msg[512];
        snprintf(warning_msg, sizeof(warning_msg),
                "The selected cover image is in JPEG format.\n\n"
                "JPEG is a lossy format and not suitable for steganography "
                "because it corrupts LSB data during compression.\n\n"
                "The image will be automatically converted to PNG format "
                "before encoding to ensure reliable extraction.\n\n"
                "Click OK to continue.");
        
        GtkAlertDialog *warning_dialog = gtk_alert_dialog_new("%s", warning_msg);
        gtk_alert_dialog_set_modal(warning_dialog, TRUE);
        gtk_alert_dialog_set_buttons(warning_dialog, (const char *[]){"OK", NULL});
        
        // Create dialog data and main loop for synchronous behavior
        DialogData dialog_data = {FALSE, g_main_loop_new(NULL, FALSE)};
        
        // Show dialog with callback
        gtk_alert_dialog_choose(warning_dialog, GTK_WINDOW(window), NULL,
                               on_jpeg_warning_response, &dialog_data);
        
        // Run nested event loop until dialog is dismissed
        g_main_loop_run(dialog_data.loop);
        
        // Cleanup
        g_main_loop_unref(dialog_data.loop);
        g_object_unref(warning_dialog);
        
        // Convert JPEG to PNG
        char temp_png_path[1024];
        snprintf(temp_png_path, sizeof(temp_png_path), "/tmp/stego_converted_%d.png", (int)getpid());
        
        if (image_convert_jpeg_to_png(input_path, temp_png_path) != 0) {
            g_free(input_path);
            GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to convert JPEG to PNG!");
            gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
            g_object_unref(dialog);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
            return;
        }
        
        actual_cover_path = strdup(temp_png_path);
        jpeg_converted = true;
    } else {
        actual_cover_path = strdup(input_path);
    }
    g_free(input_path);
    
    // Load cover image
    struct Image cover = {0};
    if (image_load(actual_cover_path, &cover) != 0) {
        if (jpeg_converted) {
            unlink(actual_cover_path);
        }
        free(actual_cover_path);
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to load cover image!");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
        return;
    }
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.3);
   
    // Create payload
    struct Payload payload = {0};
    const char *payload_filename = "message.txt";
    
    if (payload_type == 0) {
        // Text message
        GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(encode_text_view_message));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(text_buffer, &start, &end);
        char *text = gtk_text_buffer_get_text(text_buffer, &start, &end, FALSE);
        
        if (strlen(text) == 0) {
            g_free(text);
            image_free(&cover);
            if (jpeg_converted) {
                unlink(actual_cover_path);
            }
            free(actual_cover_path);
            GtkAlertDialog *dialog = gtk_alert_dialog_new("Please enter a message to encode!");
            gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
            g_object_unref(dialog);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
            return;
        }
        
        if (payload_from_text(text, &payload) != 0) {
            g_free(text);
            image_free(&cover);
            if (jpeg_converted) {
                unlink(actual_cover_path);
            }
            free(actual_cover_path);
            GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to create payload from text!");
            gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
            g_object_unref(dialog);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
            return;
        }
        g_free(text);
    } else {
        // File payload
        char *payload_path = g_file_get_path(encode_selected_payload_file);
        payload_filename = g_file_get_basename(encode_selected_payload_file);
        
        if (payload_load_from_file(payload_path, &payload) != 0) {
            g_free(payload_path);
            image_free(&cover);
            if (jpeg_converted) {
                unlink(actual_cover_path);
            }
            free(actual_cover_path);
            GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to load payload file!");
            gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
            g_object_unref(dialog);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
            return;
        }
        g_free(payload_path);
    }
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.5);
    
    // Encrypt if password is provided
    bool encrypted = false;
    if (password && strlen(password) > 0) {
        if (aes_encrypt_inplace(&payload, password) == 0) {
            encrypted = true;
        }
    }
    
    // Create metadata
    struct Metadata meta = metadata_create_from_payload(payload_filename, payload.size, lsb_depth, encrypted);
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.6);
    
    // Embed payload
    struct Image stego = {0};
    int result = stego_embed(&cover, &payload, &meta, lsb_depth, &stego);
    
    image_free(&cover);
    payload_free(&payload);
    
    if (result != 0) {
        if (jpeg_converted) {
            unlink(actual_cover_path);
        }
        free(actual_cover_path);
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to embed payload! Image may be too small.");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
        return;
    }
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.8);
    
    // Save output - generate filename in the selected directory
    char *output_dir = g_file_get_path(encode_selected_output_file);
    char *input_basename = g_file_get_basename(encode_selected_input_file);
    
    // Generate output filename: input_basename + "_stego.png"
    char *dot = strrchr(input_basename, '.');
    char output_filename[512];

    srand(time(NULL));
    int random_suffix = rand() % 10000;

    if (dot) {
        size_t base_len = dot - input_basename;
        snprintf(output_filename, sizeof(output_filename), "%.*s_stego_%d.png", (int)base_len, input_basename, random_suffix);
    } else {
        snprintf(output_filename, sizeof(output_filename), "%s_stego_%d.png", input_basename, random_suffix);
    }
    
    char output_path[1024];
    snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, output_filename);
    
    if (image_save(output_path, &stego) != 0) {
        g_free(output_dir);
        g_free(input_basename);
        image_free(&stego);
        if (jpeg_converted) {
            unlink(actual_cover_path);
        }
        free(actual_cover_path);
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to save output image!");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 0.0);
        return;
    }
    
    // Clean up temporary file
    if (jpeg_converted) {
        unlink(actual_cover_path);
    }
    free(actual_cover_path);
    
    g_free(output_dir);
    g_free(input_basename);
    image_free(&stego);
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(encode_progress_bar), 1.0);

    char success_msg[1024];
    if (jpeg_converted) {
        snprintf(success_msg, sizeof(success_msg), 
                "Encoding completed successfully!\n"
                "JPEG cover was auto-converted to PNG.\n"
                "Output saved as: %s", output_filename);
    } else {
        snprintf(success_msg, sizeof(success_msg), 
                "Encoding completed successfully!\n"
                "Output saved as: %s", output_filename);
    }
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", success_msg);
    gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
    g_object_unref(dialog);
}


/* Callback: Decode single file */
static void on_decode_clicked(GtkButton *button, gpointer user_data)
{
    if (!decode_selected_input_file || !decode_selected_output_file)
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Please select input image and output directory.");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        return;
    }
    
    GtkEntryBuffer *buffer = gtk_entry_get_buffer(GTK_ENTRY(decode_entry_password));
    const gchar *password = gtk_entry_buffer_get_text(buffer);

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.1);
    
    // Load stego image
    char *input_path = g_file_get_path(decode_selected_input_file);
    struct Image stego = {0};
    if (image_load(input_path, &stego) != 0) {
        g_free(input_path);
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to load stego image!");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        return;
    }
    g_free(input_path);
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.3);
    
    // Extract payload and metadata
    struct Metadata meta = {0};
    struct Payload payload = {0};
    
    int result = stego_extract(&stego, &meta, &payload);
    image_free(&stego);
    
    if (result != 0) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to extract payload! Invalid stego image or corrupted data.");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.0);
        return;
    }
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.6);
    
    // Decrypt if password is provided and payload is encrypted
    if (payload.encrypted && password && strlen(password) > 0) {
        if (aes_decrypt_inplace(&payload, password) != 0) {
            payload_free(&payload);
            GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to decrypt payload! Wrong password?");
            gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
            g_object_unref(dialog);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.0);
            return;
        }
    }
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.8);
    
    // Save extracted payload in the selected directory
    char *output_dir = g_file_get_path(decode_selected_output_file);
    
    // Use the original filename from metadata
    char output_path[1024];
    snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, meta.original_filename);
    
    if (payload_write_to_file(&payload, output_path) != 0) {
        g_free(output_dir);
        payload_free(&payload);
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to save extracted payload!");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
        g_object_unref(dialog);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 0.0);
        return;
    }
    g_free(output_dir);
    payload_free(&payload);
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(decode_progress_bar), 1.0);

    char message[1024];
    snprintf(message, sizeof(message), "Decoding completed successfully!\nExtracted file: %s\nSaved to: %s", 
             meta.original_filename, output_path);
    GtkAlertDialog *dialog = gtk_alert_dialog_new(message);
    gtk_alert_dialog_show(dialog, GTK_WINDOW(window));
    g_object_unref(dialog);
}

/* Create Encode tab content */
static GtkWidget* create_encode_tab(void)
{
    GtkWidget *tab_encode = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(tab_encode, 10);
    gtk_widget_set_margin_bottom(tab_encode, 10);
    gtk_widget_set_margin_start(tab_encode, 10);
    gtk_widget_set_margin_end(tab_encode, 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_box_append(GTK_BOX(tab_encode), grid);

    // Input Image
    GtkWidget *label_in = gtk_label_new("Input Image:");
    gtk_widget_set_halign(label_in, GTK_ALIGN_END);
    
    encode_file_chooser_input = gtk_button_new_with_label("Select input image");
    gtk_widget_set_hexpand(encode_file_chooser_input, TRUE);

    // Output Directory
    GtkWidget *label_out = gtk_label_new("Output Directory:");
    gtk_widget_set_halign(label_out, GTK_ALIGN_END);
    
    encode_file_chooser_output = gtk_button_new_with_label("Select output directory");
    gtk_widget_set_hexpand(encode_file_chooser_output, TRUE);

    // Payload Type
    GtkWidget *label_payload_type = gtk_label_new("Payload Type:");
    gtk_widget_set_halign(label_payload_type, GTK_ALIGN_END);
    
    const char *payload_type_options[] = {"Text Message", "File", NULL};
    GtkStringList *payload_type_list = gtk_string_list_new(payload_type_options);
    encode_combo_payload_type = gtk_drop_down_new(G_LIST_MODEL(payload_type_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(encode_combo_payload_type), 0);

    // Payload input area
    GtkWidget *label_payload = gtk_label_new("Payload:");
    gtk_widget_set_halign(label_payload, GTK_ALIGN_START);
    gtk_widget_set_valign(label_payload, GTK_ALIGN_START);
    
    // Create a stack to hold text message input and file chooser
    encode_payload_stack = gtk_stack_new();
    gtk_widget_set_vexpand(encode_payload_stack, TRUE);
    gtk_widget_set_hexpand(encode_payload_stack, TRUE);
    
    // Text message input with improved visibility
    encode_text_view_message = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(encode_text_view_message), GTK_WRAP_WORD);
    gtk_widget_set_vexpand(encode_text_view_message, TRUE);
    gtk_widget_set_hexpand(encode_text_view_message, TRUE);
    
    // Create scrolled window with improved styling
    encode_scroll_window_message = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(encode_scroll_window_message), encode_text_view_message);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(encode_scroll_window_message), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(encode_scroll_window_message, TRUE);
    gtk_widget_set_hexpand(encode_scroll_window_message, TRUE);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(encode_scroll_window_message), TRUE);
    
    // Set minimum height for the text view to make it more visible
    gtk_widget_set_size_request(encode_scroll_window_message, -1, 180);
    
    // File payload chooser
    encode_file_chooser_payload = gtk_button_new_with_label("Select payload file");
    gtk_widget_set_hexpand(encode_file_chooser_payload, TRUE);
    gtk_widget_set_valign(encode_file_chooser_payload, GTK_ALIGN_START);
    
    // Add both widgets to the stack
    gtk_stack_add_named(GTK_STACK(encode_payload_stack), encode_scroll_window_message, "text");
    gtk_stack_add_named(GTK_STACK(encode_payload_stack), encode_file_chooser_payload, "file");
    
    // Set default to text
    gtk_stack_set_visible_child_name(GTK_STACK(encode_payload_stack), "text");

    // Password
    GtkWidget *label_pass = gtk_label_new("Password:");
    gtk_widget_set_halign(label_pass, GTK_ALIGN_END);
    
    encode_entry_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(encode_entry_password), FALSE);
    gtk_widget_set_hexpand(encode_entry_password, TRUE);

    // LSB Depth
    GtkWidget *label_lsb = gtk_label_new("LSB Depth:");
    gtk_widget_set_halign(label_lsb, GTK_ALIGN_END);
    
    const char *lsb_options[] = {"1", "2", "3", NULL};
    GtkStringList *string_list = gtk_string_list_new(lsb_options);
    encode_combo_lsb_depth = gtk_drop_down_new(G_LIST_MODEL(string_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(encode_combo_lsb_depth), 0);

    // Progress bar
    encode_progress_bar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(encode_progress_bar, TRUE);

    // Encode button
    button_encode = gtk_button_new_with_label("Encode");
    gtk_widget_set_hexpand(button_encode, TRUE);

    // Attach widgets to grid
    gtk_grid_attach(GTK_GRID(grid), label_in, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_file_chooser_input, 1, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_out, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_file_chooser_output, 1, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_payload_type, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_combo_payload_type, 1, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_payload, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_payload_stack, 1, 3, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_pass, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_entry_password, 1, 4, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_lsb, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_combo_lsb_depth, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), encode_progress_bar, 0, 6, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), button_encode, 0, 7, 3, 1);

    // Connect signals
    g_signal_connect(encode_file_chooser_input, "clicked", G_CALLBACK(on_encode_input_chooser_clicked), NULL);
    g_signal_connect(encode_file_chooser_output, "clicked", G_CALLBACK(on_encode_output_chooser_clicked), NULL);
    g_signal_connect(encode_file_chooser_payload, "clicked", G_CALLBACK(on_encode_payload_chooser_clicked), NULL);
    g_signal_connect(encode_combo_payload_type, "notify::selected", G_CALLBACK(on_encode_payload_type_changed), NULL);
    g_signal_connect(button_encode, "clicked", G_CALLBACK(on_encode_clicked), NULL);

    return tab_encode;
}


/* Create Decode tab content */
static GtkWidget* create_decode_tab(void)
{
    GtkWidget *tab_decode = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(tab_decode, 10);
    gtk_widget_set_margin_bottom(tab_decode, 10);
    gtk_widget_set_margin_start(tab_decode, 10);
    gtk_widget_set_margin_end(tab_decode, 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_box_append(GTK_BOX(tab_decode), grid);

    // Input Image (Stego Image)
    GtkWidget *label_in = gtk_label_new("Stego Image:");
    gtk_widget_set_halign(label_in, GTK_ALIGN_END);
    
    decode_file_chooser_input = gtk_button_new_with_label("Select stego image");
    gtk_widget_set_hexpand(decode_file_chooser_input, TRUE);

    // Output Directory
    GtkWidget *label_out = gtk_label_new("Output Directory:");
    gtk_widget_set_halign(label_out, GTK_ALIGN_END);
    
    decode_file_chooser_output = gtk_button_new_with_label("Select output directory");
    gtk_widget_set_hexpand(decode_file_chooser_output, TRUE);

    // Password
    GtkWidget *label_pass = gtk_label_new("Password:");
    gtk_widget_set_halign(label_pass, GTK_ALIGN_END);
    
    decode_entry_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(decode_entry_password), FALSE);
    gtk_widget_set_hexpand(decode_entry_password, TRUE);

    // Progress bar
    decode_progress_bar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(decode_progress_bar, TRUE);

    // Decode button
    button_decode = gtk_button_new_with_label("Decode");
    gtk_widget_set_hexpand(button_decode, TRUE);

    // Attach widgets to grid
    gtk_grid_attach(GTK_GRID(grid), label_in, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), decode_file_chooser_input, 1, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_out, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), decode_file_chooser_output, 1, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), label_pass, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), decode_entry_password, 1, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), decode_progress_bar, 0, 3, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), button_decode, 0, 4, 3, 1);

    // Connect signals
    g_signal_connect(decode_file_chooser_input, "clicked", G_CALLBACK(on_decode_input_chooser_clicked), NULL);
    g_signal_connect(decode_file_chooser_output, "clicked", G_CALLBACK(on_decode_output_chooser_clicked), NULL);
    g_signal_connect(button_decode, "clicked", G_CALLBACK(on_decode_clicked), NULL);

    return tab_decode;
}

/* Build the interface */
static void build_main_ui(void)
{
    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "C-Stego");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    // Create notebook for tabs
    notebook_tabs = gtk_notebook_new();
    gtk_window_set_child(GTK_WINDOW(window), notebook_tabs);

    // Create Encode tab
    GtkWidget *tab_encode = create_encode_tab();

    // Create Decode tab
    GtkWidget *tab_decode = create_decode_tab();

    // Create Batch tab
    GtkWidget *tab_batch = gui_batch_create_tab();

    // Add tabs to notebook
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_tabs), tab_encode, gtk_label_new("Encode"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_tabs), tab_decode, gtk_label_new("Decode"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_tabs), tab_batch, gtk_label_new("Batch"));

    // Connect window destroy signal
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    gtk_window_present(GTK_WINDOW(window));
}

/* Public entrypoints */
void gui_init(int *argc, char ***argv)
{
    gtk_init();
}

void gui_show_main_window(void)
{
    build_main_ui();
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}


