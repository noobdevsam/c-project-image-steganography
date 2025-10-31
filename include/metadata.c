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

    struct Metadata
    {
        char magic[4];
        char original_filename[256];
        uint64_t file_size;
        int lsb_depth;
        bool encrypted;
    };

    struct Metadata metadata_create_from_payload(const char *filename, size_t file_size, int lsb_depth, bool encrypted);
