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
