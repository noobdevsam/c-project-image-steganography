#ifndef BATCH_H
#define BATCH_H

#include <glib.h>
#include <gio/gio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*BatchProgressCallback)(const char *task_id, double progress);
    typedef void (*BatchCompleteCallback)(const char *task_id, gboolean success);

    typedef void (*BatchProgressCb)(gpointer user_data, double fraction);

    typedef void (*BatchFinishedCb)(gpointer user_data, gboolean success, const char *message);

    GTask *batch_encode_async(const char *cover_path,
                              const char *payload_path,
                              const char *out_path,
                              int lsb_depth,
                              const char *password,
                              BatchProgressCb progress_cb,
                              BatchFinishedCb finished_cb,
                              gpointer user_data);

    GTask *batch_decode_async(const char *stego_path,
                              const char *out_dir,
                              const char *password,
                              BatchProgressCb progress_cb,
                              BatchFinishedCb finished_cb,
                              gpointer user_data);

    void batch_task_cancel(GTask *task);

#ifdef __cplusplus
}
#endif

#endif /* BATCH_H */
