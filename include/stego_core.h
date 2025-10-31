/* stego_core.h - public API for embedding/extracting LSB steganography */


#ifndef STEGO_CORE_H
#define STEGO_CORE_H


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

struct Image;
struct Payload;
struct Metadata;

int stego_embed(
const struct Image *cover,
const struct Payload *payload,
const struct Metadata *meta,
int lsb_depth,
struct Image *out
);

int stego_extract(
const struct Image *stego,
struct Metadata *meta_out,
struct Payload *payload_out
);


#ifdef __cplusplus
}
#endif


#endif /* STEGO_CORE_H */
