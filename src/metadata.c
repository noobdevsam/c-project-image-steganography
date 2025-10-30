
/* ==========================================================
 * metadata.c - Implementation of metadata serialization and parsing.
 * ==========================================================
 */

#include "../include/metadata.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define METADATA_MAGIC "STEG"

struct Metadata metadata_create_from_payload(const char *filename, size_t file_size, int lsb_depth, bool encrypted)
{
    struct Metadata m;
    memcpy(m.magic, METADATA_MAGIC, 4);
    strncpy(m.original_filename, filename ? filename : "payload.bin", sizeof(m.original_filename) - 1);
    m.original_filename[sizeof(m.original_filename) - 1] = '\0';
    m.file_size = file_size;
    m.lsb_depth = lsb_depth;
    m.encrypted = encrypted;
    return m;
}

void metadata_free(struct Metadata *m)
{
    (void)m; /* nothing dynamic for now */
}

int metadata_serialize(const struct Metadata *meta, unsigned char **out_buf, size_t *out_size)
{
    if (!meta || !out_buf || !out_size)
        return -1;

    size_t total = 4 + 256 + 8 + 4 + 1; /* magic + filename + size + lsb_depth + encrypted */
    unsigned char *buf = malloc(total);
    if (!buf)
        return -2;

    size_t offset = 0;
    memcpy(buf + offset, meta->magic, 4);
    offset += 4;
    memcpy(buf + offset, meta->original_filename, 256);
    offset += 256;

    /* store 64-bit size (little-endian) */
    uint64_t size = meta->file_size;
    for (int i = 0; i < 8; ++i)
        buf[offset++] = (unsigned char)((size >> (8 * i)) & 0xFF);

    /* store 32-bit lsb_depth */
    uint32_t depth = (uint32_t)meta->lsb_depth;
    for (int i = 0; i < 4; ++i)
        buf[offset++] = (unsigned char)((depth >> (8 * i)) & 0xFF);

    buf[offset++] = meta->encrypted ? 1 : 0;

    *out_buf = buf;
    *out_size = total;
    return 0;
}

int metadata_parse(const unsigned char *buf, size_t buf_size, struct Metadata *meta_out)
{
    if (!buf || !meta_out)
        return -1;
    if (buf_size < 4 + 256 + 8 + 4 + 1)
        return -2;

    size_t offset = 0;
    memcpy(meta_out->magic, buf + offset, 4);
    offset += 4;
    if (memcmp(meta_out->magic, METADATA_MAGIC, 4) != 0)
        return -3;

    memcpy(meta_out->original_filename, buf + offset, 256);
    offset += 256;

    uint64_t size = 0;
    for (int i = 0; i < 8; ++i)
        size |= ((uint64_t)buf[offset++]) << (8 * i);
    meta_out->file_size = size;

    uint32_t depth = 0;
    for (int i = 0; i < 4; ++i)
        depth |= ((uint32_t)buf[offset++]) << (8 * i);
    meta_out->lsb_depth = (int)depth;

    meta_out->encrypted = buf[offset++] != 0;

    return 0;
}

int metadata_get_payload_size(const struct Metadata *meta, size_t *out_size)
{
    if (!meta || !out_size)
        return -1;
    *out_size = (size_t)meta->file_size;
    return 0;
}
