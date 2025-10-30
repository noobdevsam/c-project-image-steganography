/* ==========================================================
 * stego_core.c - implementation of the simple LSB embedding/extraction
 *
 * This implementation assumes an 8-bit-per-channel RGB(A) pixel buffer.
 * It works at the byte level and manipulates the least-significant bits
 * of each color channel according to the chosen lsb_depth (1..3).
 *
 * Important notes / TODOs:
 * - Metadata serialization/parsing is delegated to metadata.c via the
 *   functions metadata_serialize() and metadata_parse(). Those must be
 *   implemented to match the format expected here. We expect the
 *   serialized metadata to start with a 4-byte magic 'STEG' followed by
 *   a 4-byte metadata length (big-endian or little-endian consistently).
 * - Payload memory layout: struct Payload must expose .data and .size.
 * - Batch processing should use GTask in future modules (as requested by
 *   the user). This file is single-threaded and does not depend on pthread.
 *
 * Security: This code does *not* perform any encryption. Use aes_wrapper.c
 * to encrypt/decrypt payload->data before/after calling these APIs.
 *
 * Build integration: compile alongside image_io.c, metadata.c, aes_wrapper.c
 *
 * ==========================================================
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../include/stego_core.h"
#include "../include/metadata.h"
#include "../include/payload.h"
#include "../include/image_io.h"

/* Forward-declared helper APIs that must be provided in other modules:
 * - metadata_serialize(const Metadata*, unsigned char**, size_t*)
 * - metadata_parse(const unsigned char*, size_t, Metadata*)
 * The implementations will live in metadata.c
 */

int metadata_serialize(const struct Metadata *meta, unsigned char **out_buf, size_t *out_size);
int metadata_parse(const unsigned char *buf, size_t buf_size, struct Metadata *meta_out);

/* Expectation for Image struct; image_io.c must follow this layout */
static size_t compute_capacity_bytes(const struct Image *img, int lsb_depth)
{
    if (!img || img->channels < 3)
        return 0;
    size_t total_pixels = (size_t)img->width * (size_t)img->height;
    size_t total_bits = total_pixels * (size_t)img->channels * (size_t)lsb_depth;
    return total_bits / 8;
}

/* Helper: write a single bit into pixel channel LSBs
 * - dst_byte points to the pixel channel byte
 * - bit_val is 0 or 1
 * - bit_pos is which LSB (0 is least significant within the group of lsb_depth)
 */
static inline void set_lsb_bit(unsigned char *dst_byte, int bit_val, int bit_pos)
{
    unsigned char mask = (1u << bit_pos);
    if (bit_val)
        *dst_byte |= mask;
    else
        *dst_byte &= ~mask;
}

/* Write sequential bits from buf (big-endian within each byte: msb first)
 * into the image LSBs. We consume bits starting from buf[0] MSB -> LSB.
 */
static int embed_bytes_into_image(const struct Image *cover, const unsigned char *buf, size_t buf_size, int lsb_depth, struct Image *out)
{
    if (!cover || !buf || !out)
        return -1;
    size_t capacity = compute_capacity_bytes(cover, lsb_depth);
    if (buf_size > capacity)
        return -2; /* not enough capacity */

    /* Prepare output image as a copy of cover */
    size_t pixel_bytes = (size_t)cover->width * cover->height * cover->channels;
    out->pixels = malloc(pixel_bytes);
    if (!out->pixels)
        return -3;
    memcpy(out->pixels, cover->pixels, pixel_bytes);
    out->width = cover->width;
    out->height = cover->height;
    out->channels = cover->channels;

    size_t total_bits_to_write = buf_size * 8;
    size_t bit_index = 0; /* 0..total_bits_to_write-1 */

    for (size_t px = 0; px < (size_t)cover->width * cover->height && bit_index < total_bits_to_write; ++px)
    {
        size_t base = px * cover->channels;
        for (int ch = 0; ch < cover->channels && bit_index < total_bits_to_write; ++ch)
        {
            unsigned char *pbyte = &out->pixels[base + ch];
            /* For each channel embed up to lsb_depth bits */
            for (int b = 0; b < lsb_depth && bit_index < total_bits_to_write; ++b)
            {
                size_t byte_idx = bit_index / 8;
                int bit_in_byte = 7 - (bit_index % 8); /* msb-first */
                int bit_val = (buf[byte_idx] >> bit_in_byte) & 1;
                /* Set the b-th LSB of pbyte to bit_val */
                if (bit_val)
                    *pbyte |= (1u << b);
                else
                    *pbyte &= ~(1u << b);
                ++bit_index;
            }
        }
    }

    if (bit_index < total_bits_to_write)
    {
        /* Unexpected: couldn't write all bits */
        free(out->pixels);
        out->pixels = NULL;
        return -4;
    }

    return 0;
}

/* Read sequential bits from image LSBs into buffer (reads buf_size bytes)
 * The reading order mirrors the embedding order used above.
 */
static int extract_bytes_from_image(const struct Image *img, unsigned char *out_buf, size_t out_size, int lsb_depth)
{
    if (!img || !out_buf)
        return -1;
    size_t capacity = compute_capacity_bytes(img, lsb_depth);
    if (out_size > capacity)
        return -2;

    size_t total_bits_to_read = out_size * 8;
    size_t bit_index = 0;
    memset(out_buf, 0, out_size);

    for (size_t px = 0; px < (size_t)img->width * img->height && bit_index < total_bits_to_read; ++px)
    {
        size_t base = px * img->channels;
        for (int ch = 0; ch < img->channels && bit_index < total_bits_to_read; ++ch)
        {
            unsigned char pbyte = img->pixels[base + ch];
            for (int b = 0; b < lsb_depth && bit_index < total_bits_to_read; ++b)
            {
                int bit_val = (pbyte >> b) & 1;
                size_t byte_idx = bit_index / 8;
                int bit_in_byte = 7 - (bit_index % 8);
                out_buf[byte_idx] |= (bit_val << bit_in_byte);
                ++bit_index;
            }
        }
    }

    if (bit_index < total_bits_to_read)
        return -3;
    return 0;
}
/* Public API: stego_embed */
int stego_embed(const struct Image *cover,
                const struct Payload *payload,
                const struct Metadata *meta,
                int lsb_depth,
                struct Image *out)
{
    if (!cover || !payload || !meta || !out)
        return -1;
    if (lsb_depth < 1 || lsb_depth > 3)
        return -2;

    /* Serialize metadata */
    unsigned char *meta_buf = NULL;
    size_t meta_size = 0;
    int rc = metadata_serialize(meta, &meta_buf, &meta_size);
    if (rc != 0)
    {
        return -3;
    }

    /* Build combined buffer: [meta_size(4 bytes LE)] [meta_buf] [payload->data]
     * We prefix metadata length (uint32 LE) to help decoder know how many
     * metadata bytes to read. This convention must be matched in metadata_parse.
     */
    size_t total_size = 4 + meta_size + payload->size;
    unsigned char *combined = malloc(total_size);
    if (!combined)
    {
        free(meta_buf);
        return -4;
    }

    /* Little-endian 32-bit length */
    combined[0] = (unsigned char)(meta_size & 0xFF);
    combined[1] = (unsigned char)((meta_size >> 8) & 0xFF);
    combined[2] = (unsigned char)((meta_size >> 16) & 0xFF);
    combined[3] = (unsigned char)((meta_size >> 24) & 0xFF);

    memcpy(combined + 4, meta_buf, meta_size);
    memcpy(combined + 4 + meta_size, payload->data, payload->size);

    /* Check capacity */
    size_t capacity = compute_capacity_bytes(cover, lsb_depth);
    if (total_size > capacity)
    {
        free(meta_buf);
        free(combined);
        return -5; /* overflow */
    }

    rc = embed_bytes_into_image(cover, combined, total_size, lsb_depth, out);

    free(meta_buf);
    free(combined);
    return rc;
}
/* Public API: stego_extract */
int stego_extract(const struct Image *stego,
                  struct Metadata *meta_out,
                  struct Payload *payload_out)
{
    if (!stego || !meta_out || !payload_out)
        return -1;

    unsigned char len_buf[4];
    int lsb_depth = 0;
    size_t meta_len = 0;
    unsigned char *meta_buf = NULL;
    int found = 0;

    // Probe for LSB depth by trying to find valid metadata
    for (int d = 3; d >= 1 && !found; --d)
    {
        // 1. Read the 4-byte metadata length
        if (extract_bytes_from_image(stego, len_buf, 4, d) != 0)
        {
            continue; // Not enough capacity even for length, try smaller depth
        }

        uint32_t mlen = (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8) | ((uint32_t)len_buf[2] << 16) | ((uint32_t)len_buf[3] << 24);

        // Sanity check on metadata length
        if (mlen == 0 || mlen > 1024) // Metadata shouldn't be huge
        {
            continue;
        }

        // 2. Read the metadata block itself
        unsigned char *mbuf = malloc(mlen);
        if (!mbuf)
        {
            return -2; // Out of memory
        }

        // To extract the metadata, we need to read a total of (4 + mlen) bytes
        // from the beginning of the data stream, then take the part after the first 4 bytes.
        size_t total_prefix_size = 4 + mlen;
        unsigned char *prefix_buf = malloc(total_prefix_size);
        if (!prefix_buf)
        {
            free(mbuf);
            return -2;
        }

        if (extract_bytes_from_image(stego, prefix_buf, total_prefix_size, d) != 0)
        {
            free(mbuf);
            free(prefix_buf);
            continue; // Not enough data for this depth
        }

        memcpy(mbuf, prefix_buf + 4, mlen);
        free(prefix_buf);

        // 3. Try to parse the metadata
        struct Metadata test_meta;
        if (metadata_parse(mbuf, mlen, &test_meta) == 0)
        {
            // Success! We found valid metadata
            found = 1;
            lsb_depth = d;
            meta_len = mlen;
            meta_buf = mbuf; // Keep the buffer
            memcpy(meta_out, &test_meta, sizeof(struct Metadata));
        }
        else
        {
            free(mbuf); // Failed, free the buffer and try next depth
        }
    }

    if (!found)
    {
        return -4; // Failed to find valid metadata at any LSB depth
    }

    // 4. Extract the payload
    size_t payload_size = 0;
    if (metadata_get_payload_size(meta_out, &payload_size) != 0)
    {
        free(meta_buf);
        return -5;
    }

    // If payload size is 0, nothing more to do
    if (payload_size == 0)
    {
        payload_out->data = NULL;
        payload_out->size = 0;
        free(meta_buf);
        return 0;
    }

    // Total embedded data size is (4 bytes for len + metadata + payload)
    size_t total_embedded_size = 4 + meta_len + payload_size;
    unsigned char *full_data = malloc(total_embedded_size);
    if (!full_data)
    {
        free(meta_buf);
        return -6;
    }

    if (extract_bytes_from_image(stego, full_data, total_embedded_size, lsb_depth) != 0)
    {
        free(meta_buf);
        free(full_data);
        return -7;
    }

    // Copy the payload part into the output struct
    payload_out->data = malloc(payload_size);
    if (!payload_out->data)
    {
        free(meta_buf);
        free(full_data);
        return -8;
    }
    memcpy(payload_out->data, full_data + 4 + meta_len, payload_size);
    payload_out->size = payload_size;
    payload_out->encrypted = meta_out->encrypted;

    // Cleanup
    free(meta_buf);
    free(full_data);

    return 0;
}
