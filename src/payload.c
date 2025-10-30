/* ==========================================================
 * payload.c
 * Implementation of payload helpers.
 * ==========================================================
 */

#include "../include/payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int payload_load_from_file(const char *path, struct Payload *out)
{
    if (!path || !out)
        return -1;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -2;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return -3;
    }
    long sz = ftell(f);
    if (sz < 0)
    {
        fclose(f);
        return -4;
    }
    rewind(f);

    unsigned char *buf = malloc((size_t)sz);
    if (!buf)
    {
        fclose(f);
        return -5;
    }

    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (r != (size_t)sz)
    {
        free(buf);
        return -6;
    }

    out->data = buf;
    out->size = (size_t)sz;
    out->encrypted = 0;
    return 0;
}

int payload_from_text(const char *text, struct Payload *out)
{
    if (!text || !out)
        return -1;

    size_t len = strlen(text);
    unsigned char *buf = malloc(len);
    if (!buf)
        return -2;

    memcpy(buf, text, len);
    out->data = buf;
    out->size = len;
    out->encrypted = 0;
    return 0;
}

int payload_write_to_file(const struct Payload *payload, const char *outpath)
{
    if (!payload || !outpath)
        return -1;
    FILE *f = fopen(outpath, "wb");
    if (!f)
        return -2;
    size_t w = fwrite(payload->data, 1, payload->size, f);
    fclose(f);
    if (w != payload->size)
        return -3;
    return 0;
}

void payload_free(struct Payload *p)
{
    if (!p)
        return;
    if (p->data)
    {
        /* optionally zero memory before free for security */
        memset(p->data, 0, p->size);
        free(p->data);
        p->data = NULL;
    }
    p->size = 0;
    p->encrypted = 0;
}
