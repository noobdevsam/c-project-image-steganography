/* payload.h - Manage payload data for steganography (read/write/free)
 *
 * Provides a simple API to load a payload from disk into memory, write an
 * extracted payload back to disk, and free payload buffers.
 */

#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct Payload
    {
        unsigned char *data;
        size_t size;
        int encrypted; /* boolean flag: 0 = plain, 1 = encrypted */
    };

    /* Load payload from file into `out`. Caller must call payload_free when done.
     * Returns 0 on success, non-zero on error.
     */
    int payload_load_from_file(const char *path, struct Payload *out);

    /* Create payload from a text string. The text is copied internally.
     * Caller must call payload_free when done. Returns 0 on success.
     */
    int payload_from_text(const char *text, struct Payload *out);

    /* Write payload to disk at outpath. Returns 0 on success. */
    int payload_write_to_file(const struct Payload *payload, const char *outpath);

    /* Free internal buffers of payload. Safe to call on partially-initialized payloads. */
    void payload_free(struct Payload *p);

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_H */
