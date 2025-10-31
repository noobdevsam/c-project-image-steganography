/* image_io.h - Image loading/saving utilities for the LSB Steganography project */

#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Image {
    unsigned char *pixels;
    int width;
    int height;
    int channels;
};

int image_load(const char *path, struct Image *out);

int image_save(const char *path, const struct Image *img);

void image_free(struct Image *img);

int image_is_jpeg(const char *path);

int image_convert_jpeg_to_png(const char *input_path, const char *output_path);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_IO_H */
