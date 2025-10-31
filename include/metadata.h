/* metadata.h - Metadata handling for the LSB Steganography project */

#ifndef METADATA_H
#define METADATA_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Metadata structure representing embedded header info. */
    struct Metadata
    {
        char magic[4]; /* "STEG" */
        char original_filename[256];
        uint64_t file_size; /* original payload size */
        int lsb_depth;      /* 1..3 */
        bool encrypted;     /* AES applied? */
    };

    /* Create metadata for a given payload and configuration. */
    struct Metadata metadata_create_from_payload(const char *filename, size_t file_size, int lsb_depth, bool encrypted);

    /* Free metadata (no dynamic members here, but for symmetry). */
    void metadata_free(struct Metadata *m);

    /* Serialize metadata to a contiguous byte buffer (caller frees). */
    int metadata_serialize(const struct Metadata *meta, unsigned char **out_buf, size_t *out_size);

    /* Parse metadata from a serialized buffer. */
    int metadata_parse(const unsigned char *buf, size_t buf_size, struct Metadata *meta_out);

    /* Get payload size stored in metadata. */
    int metadata_get_payload_size(const struct Metadata *meta, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* METADATA_H */
