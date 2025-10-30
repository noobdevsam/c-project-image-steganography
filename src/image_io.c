/* ==========================================================
 * image_io.c - Image I/O implementation for BMP, JPEG, PNG.
 *
 * Uses libpng and libjpeg for decoding. Only libpng is used
 * for saving to simplify write logic (all stego outputs are
 * saved in PNG format, ensuring lossless results).
 * ==========================================================
 */

#include "../include/image_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <png.h>
#include <jpeglib.h>

/* Helper: determine file extension */
static const char *get_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    return (dot && dot[1]) ? dot + 1 : "";
}

/* ==========================================================
 * BMP loading (simple 24-bit uncompressed reader)
 * ==========================================================
 */
#pragma pack(push, 1)
struct BMPHeader
{
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

static int load_bmp(const char *path, struct Image *out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    struct BMPHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    {
        fclose(f);
        return -2;
    }

    if (hdr.bfType != 0x4D42 || hdr.biBitCount != 24 || hdr.biCompression != 0)
    {
        fclose(f);
        return -3; /* unsupported format */
    }

    out->width = hdr.biWidth;
    out->height = hdr.biHeight;
    out->channels = 3;
    size_t row_padded = ((out->width * 3 + 3) & ~3);
    out->pixels = malloc((size_t)out->width * out->height * 3);
    if (!out->pixels)
    {
        fclose(f);
        return -4;
    }

    fseek(f, hdr.bfOffBits, SEEK_SET);

    for (int y = 0; y < out->height; ++y)
    {
        unsigned char *row = malloc(row_padded);
        fread(row, 1, row_padded, f);
        for (int x = 0; x < out->width; ++x)
        {
            /* BMP stores BGR */
            size_t idx = ((out->height - 1 - y) * out->width + x) * 3;
            out->pixels[idx + 0] = row[x * 3 + 2]; /* R */
            out->pixels[idx + 1] = row[x * 3 + 1]; /* G */
            out->pixels[idx + 2] = row[x * 3 + 0]; /* B */
        }
        free(row);
    }
    fclose(f);
    return 0;
}

/* ==========================================================
 * JPEG loading (via libjpeg)
 * ==========================================================
 */
static int load_jpeg(const char *path, struct Image *out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    out->width = cinfo.output_width;
    out->height = cinfo.output_height;
    out->channels = cinfo.output_components;

    size_t row_stride = out->width * out->channels;
    out->pixels = malloc((size_t)row_stride * out->height);
    if (!out->pixels)
    {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        return -2;
    }

    while (cinfo.output_scanline < cinfo.output_height)
    {
        unsigned char *buffer_array[1];
        buffer_array[0] = out->pixels + (size_t)cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, buffer_array, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return 0;
}

/* ==========================================================
 * PNG loading (via libpng)
 * ==========================================================
 */
static int load_png(const char *path, struct Image *out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    unsigned char header[8];
    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8))
    {
        fclose(fp);
        return -2;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        fclose(fp);
        return -3;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return -4;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return -5;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    out->width = png_get_image_width(png_ptr, info_ptr);
    out->height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    out->channels = 4; /* RGBA */
    size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    out->pixels = malloc((size_t)rowbytes * out->height);
    if (!out->pixels)
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return -6;
    }

    png_bytep *row_pointers = malloc(sizeof(png_bytep) * out->height);
    for (int y = 0; y < out->height; ++y)
        row_pointers[y] = out->pixels + y * rowbytes;

    png_read_image(png_ptr, row_pointers);
    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
    return 0;
}

/* ==========================================================
 * PNG saving (always saves RGBA or RGB -> PNG)
 * ==========================================================
 */
static int save_png(const char *path, const struct Image *img)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        fclose(fp);
        return -2;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return -3;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return -4;
    }

    png_init_io(png_ptr, fp);

    int color_type = (img->channels == 4) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;

    png_set_IHDR(png_ptr, info_ptr, img->width, img->height,
                 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    size_t rowbytes = (size_t)img->width * img->channels;
    png_bytep *row_pointers = malloc(sizeof(png_bytep) * img->height);
    for (int y = 0; y < img->height; ++y)
        row_pointers[y] = (png_bytep)(img->pixels + y * rowbytes);

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 0;
}

/* ==========================================================
 * Public API
 * ==========================================================
 */
int image_load(const char *path, struct Image *out)
{
    if (!path || !out)
        return -1;
    memset(out, 0, sizeof(*out));

    const char *ext = get_ext(path);
    char lower[8];
    for (size_t i = 0; i < sizeof(lower) - 1 && ext[i]; ++i)
        lower[i] = (char)tolower((unsigned char)ext[i]);
    lower[sizeof(lower) - 1] = '\0';

    if (strstr(lower, "bmp"))
        return load_bmp(path, out);
    if (strstr(lower, "jpg") || strstr(lower, "jpeg"))
        return load_jpeg(path, out);
    if (strstr(lower, "png"))
        return load_png(path, out);

    return -2; /* unsupported extension */
}

int image_save(const char *path, const struct Image *img)
{
    if (!path || !img)
        return -1;
    return save_png(path, img);
}

void image_free(struct Image *img)
{
    if (!img || !img->pixels)
        return;
    free(img->pixels);
    img->pixels = NULL;
    img->width = img->height = img->channels = 0;
}

int image_is_jpeg(const char *path)
{
    if (!path)
        return 0;

    const char *ext = get_ext(path);
    char lower[8];
    for (size_t i = 0; i < sizeof(lower) - 1 && ext[i]; ++i)
        lower[i] = (char)tolower((unsigned char)ext[i]);
    lower[sizeof(lower) - 1] = '\0';

    if (strstr(lower, "jpg") || strstr(lower, "jpeg"))
        return 1;

    // Also check magic bytes for JPEG (FF D8 FF)
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    unsigned char magic[3];
    if (fread(magic, 1, 3, f) == 3)
    {
        fclose(f);
        if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF)
            return 1;
    }
    else
    {
        fclose(f);
    }

    return 0;
}

int image_convert_jpeg_to_png(const char *input_path, const char *output_path)
{
    if (!input_path || !output_path)
        return -1;

    struct Image img = {0};
    int rc = load_jpeg(input_path, &img);
    if (rc != 0)
        return rc;

    rc = save_png(output_path, &img);
    image_free(&img);

    return rc;
}
