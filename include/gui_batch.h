#ifndef GUI_BATCH_H
#define GUI_BATCH_H

#include <gtk/gtk.h>

/* Initialize and return the batch tab widget */
GtkWidget* gui_batch_create_tab(void);

/* Called by backend when a batch task reports progress or completion */
void gui_batch_update_progress(const gchar *task_id, double progress, gboolean done);

#endif // GUI_BATCH_H
