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
        int encrypted;
    };

    int payload_load_from_file(const char *path, struct Payload *out);

    int payload_from_text(const char *text, struct Payload *out);

    int payload_write_to_file(const struct Payload *payload, const char *outpath);

    void payload_free(struct Payload *p);

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_H */
