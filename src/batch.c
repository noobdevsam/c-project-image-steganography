/*
 * batch.c - Batch encode/decode using GTask (GLib) for async background work.
 *
 * This module depends on the following project APIs:
 *  - image_load / image_save / image_free   (image_io.h)
 *  - payload_load_from_file / payload_write_to_file / payload_free (payload.h)
 *  - metadata_create_from_payload / metadata_free / metadata_get_payload_size (metadata.h)
 *  - aes_encrypt_inplace / aes_decrypt_inplace (aes_wrapper.h)
 *  - stego_embed / stego_extract (stego_core.h)
 *
 * The callbacks (progress and finished) are always invoked on the main thread
 * via g_main_context_invoke() so GUI code can safely manipulate widgets.
 */

#include "../include/batch.h"
#include "../include/image_io.h"
#include "../include/payload.h"
#include "../include/metadata.h"
#include "../include/aes_wrapper.h"
#include "../include/stego_core.h"

#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h> // For basename()

/* Internal struct used to pass parameters to worker */
typedef struct
{
    char *cover_path;
    char *payload_path;
    char *out_path;
    char *stego_path;
    char *out_dir;
    char *password;
    int lsb_depth;

    BatchProgressCb progress_cb;
    BatchFinishedCb finished_cb;
    gpointer user_data;
} BatchParams;

/* Utility: duplicate string safely (returns malloc'd pointer) */
static char *dupstr_safe(const char *s)
{
    if (!s)
        return NULL;
    return g_strdup(s);
}

/* Helper: invoke progress callback on main loop with given fraction */
typedef struct
{
    BatchProgressCb cb;
    gpointer user_data;
    double fraction;
} ProgressInvokeData;

static gboolean progress_invoke_cb(gpointer user_data)
{
    ProgressInvokeData *d = (ProgressInvokeData *)user_data;
    if (d->cb)
        d->cb(d->user_data, d->fraction);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void report_progress_main(BatchProgressCb cb, gpointer user_data, double fraction)
{
    if (!cb)
        return;
    ProgressInvokeData *d = g_new0(ProgressInvokeData, 1);
    d->cb = cb;
    d->user_data = user_data;
    d->fraction = fraction;
    g_main_context_invoke(NULL, progress_invoke_cb, d);
}

/* Helper: invoke finished callback on main loop with message */
typedef struct
{
    BatchFinishedCb cb;
    gpointer user_data;
    gboolean success;
    char *message; /* allocated by caller or NULL; freed after call */
} FinishedInvokeData;

static gboolean finished_invoke_cb(gpointer user_data)
{
    FinishedInvokeData *d = (FinishedInvokeData *)user_data;
    if (d->cb)
        d->cb(d->user_data, d->success, d->message);
    if (d->message)
        g_free(d->message);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void report_finished_main(BatchFinishedCb cb, gpointer user_data, gboolean success, const char *message)
{
    FinishedInvokeData *d = g_new0(FinishedInvokeData, 1);
    d->cb = cb;
    d->user_data = user_data;
    d->success = success;
    d->message = message ? g_strdup(message) : NULL;
    g_main_context_invoke(NULL, finished_invoke_cb, d);
}

/* Worker for encode (runs in thread) */
static void encode_task_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    BatchParams *p = (BatchParams *)task_data;
    (void)source_object;
    (void)cancellable;

    char *actual_cover_path = NULL;
    gboolean jpeg_converted = FALSE;

    /* Step 0: Check if cover is JPEG and convert if needed */
    if (image_is_jpeg(p->cover_path))
    {
        report_progress_main(p->progress_cb, p->user_data, 0.02);

        // Generate temporary PNG path
        char temp_png_path[4096];
        snprintf(temp_png_path, sizeof(temp_png_path), "/tmp/stego_batch_converted_%p.png", (void *)p);

        int rc = image_convert_jpeg_to_png(p->cover_path, temp_png_path);
        if (rc != 0)
        {
            report_finished_main(p->finished_cb, p->user_data, FALSE, "Failed to convert JPEG to PNG");
            return;
        }

        actual_cover_path = g_strdup(temp_png_path);
        jpeg_converted = TRUE;
    }
    else
    {
        actual_cover_path = g_strdup(p->cover_path);
    }

    /* Step 1: load cover image */
    report_progress_main(p->progress_cb, p->user_data, 0.05);
    struct Image cover = {0};
    int rc = image_load(actual_cover_path, &cover);
    if (rc != 0)
    {
        if (jpeg_converted)
            unlink(actual_cover_path);
        g_free(actual_cover_path);
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Failed to load cover image");
        return;
    }

    /* Step 2: load payload */
    report_progress_main(p->progress_cb, p->user_data, 0.15);
    struct Payload payload = {0};
    rc = payload_load_from_file(p->payload_path, &payload);
    if (rc != 0)
    {
        image_free(&cover);
        if (jpeg_converted)
            unlink(actual_cover_path);
        g_free(actual_cover_path);
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Failed to load payload file");
        return;
    }

    /* Step 3: optional encryption */
    if (p->password && p->password[0] != '\0')
    {
        report_progress_main(p->progress_cb, p->user_data, 0.30);
        rc = aes_encrypt_inplace(&payload, p->password);
        if (rc != 0)
        {
            payload_free(&payload);
            image_free(&cover);
            if (jpeg_converted)
                unlink(actual_cover_path);
            g_free(actual_cover_path);
            report_finished_main(p->finished_cb, p->user_data, FALSE, "AES encryption failed");
            return;
        }
    }

    /* Step 4: create metadata */
    report_progress_main(p->progress_cb, p->user_data, 0.45);
    char *payload_path_copy = g_strdup(p->payload_path);
    const char *payload_basename = basename(payload_path_copy);
    struct Metadata meta = metadata_create_from_payload(payload_basename, payload.size, p->lsb_depth, payload.encrypted);
    g_free(payload_path_copy);

    /* Step 5: embed */
    report_progress_main(p->progress_cb, p->user_data, 0.60);
    struct Image outimg = {0};
    rc = stego_embed(&cover, &payload, &meta, p->lsb_depth, &outimg);
    if (rc != 0)
    {
        metadata_free(&meta);
        payload_free(&payload);
        image_free(&cover);
        if (jpeg_converted)
            unlink(actual_cover_path);
        g_free(actual_cover_path);
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Embedding failed (maybe insufficient capacity)");
        return;
    }

    /* Step 6: save PNG */
    report_progress_main(p->progress_cb, p->user_data, 0.85);
    rc = image_save(p->out_path, &outimg);
    if (rc != 0)
    {
        image_free(&outimg);
        metadata_free(&meta);
        payload_free(&payload);
        image_free(&cover);
        if (jpeg_converted)
            unlink(actual_cover_path);
        g_free(actual_cover_path);
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Failed to save output PNG");
        return;
    }

    /* Cleanup and finish */
    image_free(&outimg);
    metadata_free(&meta);
    payload_free(&payload);
    image_free(&cover);

    // Clean up temporary file
    if (jpeg_converted)
        unlink(actual_cover_path);
    g_free(actual_cover_path);

    report_progress_main(p->progress_cb, p->user_data, 1.0);

    const char *finish_msg = jpeg_converted ? "Encode complete (JPEG auto-converted to PNG)" : "Encode complete";
    report_finished_main(p->finished_cb, p->user_data, TRUE, finish_msg);
}

/* Worker for decode (runs in thread) */
static void decode_task_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    BatchParams *p = (BatchParams *)task_data;
    (void)source_object;
    (void)cancellable;

    report_progress_main(p->progress_cb, p->user_data, 0.05);
    struct Image img = {0};
    int rc = image_load(p->stego_path, &img);
    if (rc != 0)
    {
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Failed to load stego image");
        return;
    }

    report_progress_main(p->progress_cb, p->user_data, 0.20);
    struct Metadata meta = {0};
    struct Payload payload = {0};
    rc = stego_extract(&img, &meta, &payload);
    if (rc != 0)
    {
        image_free(&img);
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Extraction failed (not a stego image?)");
        return;
    }

    /* If encrypted, decrypt */
    if (meta.encrypted)
    {
        if (!p->password || p->password[0] == '\0')
        {
            metadata_free(&meta);
            payload_free(&payload);
            image_free(&img);
            report_finished_main(p->finished_cb, p->user_data, FALSE, "Payload is encrypted but no password provided");
            return;
        }
        report_progress_main(p->progress_cb, p->user_data, 0.5);
        rc = aes_decrypt_inplace(&payload, p->password);
        if (rc != 0)
        {
            metadata_free(&meta);
            payload_free(&payload);
            image_free(&img);
            report_finished_main(p->finished_cb, p->user_data, FALSE, "AES decryption failed (wrong password?)");
            return;
        }
    }

    /* Write extracted payload to out_dir using original_filename from metadata */
    report_progress_main(p->progress_cb, p->user_data, 0.75);
    char outpath[4096];
    snprintf(outpath, sizeof(outpath), "%s/%s", p->out_dir, meta.original_filename);
    rc = payload_write_to_file(&payload, outpath);
    if (rc != 0)
    {
        metadata_free(&meta);
        payload_free(&payload);
        image_free(&img);
        report_finished_main(p->finished_cb, p->user_data, FALSE, "Failed to write extracted payload to disk");
        return;
    }

    metadata_free(&meta);
    payload_free(&payload);
    image_free(&img);

    report_progress_main(p->progress_cb, p->user_data, 1.0);
    report_finished_main(p->finished_cb, p->user_data, TRUE, "Decode complete");
}

/* Common helper to free BatchParams */
static void free_batch_params(BatchParams *p)
{
    if (!p)
        return;
    g_free(p->cover_path);
    g_free(p->payload_path);
    g_free(p->out_path);
    g_free(p->stego_path);
    g_free(p->out_dir);
    g_free(p->password);
    g_free(p);
}

/* Public API: batch_encode_async */
GTask *batch_encode_async(const char *cover_path,
                          const char *payload_path,
                          const char *out_path,
                          int lsb_depth,
                          const char *password,
                          BatchProgressCb progress_cb,
                          BatchFinishedCb finished_cb,
                          gpointer user_data)
{
    if (!cover_path || !payload_path || !out_path)
        return NULL;
    BatchParams *p = g_new0(BatchParams, 1);
    p->cover_path = dupstr_safe(cover_path);
    p->payload_path = dupstr_safe(payload_path);
    p->out_path = dupstr_safe(out_path);
    p->password = dupstr_safe(password);
    p->lsb_depth = lsb_depth;
    p->progress_cb = progress_cb;
    p->finished_cb = finished_cb;
    p->user_data = user_data;

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, p, (GDestroyNotify)free_batch_params);
    g_task_run_in_thread(task, encode_task_func);
    return task;
}

/* Public API: batch_decode_async */
GTask *batch_decode_async(const char *stego_path,
                          const char *out_dir,
                          const char *password,
                          BatchProgressCb progress_cb,
                          BatchFinishedCb finished_cb,
                          gpointer user_data)
{
    if (!stego_path || !out_dir)
        return NULL;
    BatchParams *p = g_new0(BatchParams, 1);
    p->stego_path = dupstr_safe(stego_path);
    p->out_dir = dupstr_safe(out_dir);
    p->password = dupstr_safe(password);
    p->progress_cb = progress_cb;
    p->finished_cb = finished_cb;
    p->user_data = user_data;

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, p, (GDestroyNotify)free_batch_params);
    g_task_run_in_thread(task, decode_task_func);
    return task;
}

void batch_task_cancel(GTask *task)
{
    if (!task)
        return;
    GCancellable *c = g_task_get_cancellable(task);
    if (!c)
    {
        /* create a cancellable and set it on the task for future use? just request cancel on a new one */
        g_cancellable_cancel(g_cancellable_new());
    }
    else
    {
        g_cancellable_cancel(c);
    }
}
