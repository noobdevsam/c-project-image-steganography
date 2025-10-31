/* aes_wrapper.c
 *
 * AES-256-CBC encryption/decryption wrapper using:
 *  - tiny-AES-c for AES operations (https://github.com/kokke/tiny-AES-c)
 *  - internal PBKDF2-HMAC-SHA256 implementation (no OpenSSL dependency)
 *
 * Encrypted payload layout:
 *   [16 bytes salt][16 bytes IV][ciphertext (multiple of 16 bytes, PKCS#7)]
 *
 * Notes:
 *  - Requires tiny-AES-c's aes.h / aes.c being available and compiled into the project.
 *  - Uses /dev/urandom for randomness.
 */

#include "../include/aes_wrapper.h"
#include "../include/payload.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* tiny-AES-c header (must be present in project) */
#include "../third_party/tiny-aes/aes.h"

/* ---------- SHA-256 implementation (compact public-domain style) ---------- */
/* This is a small SHA-256 implementation adapted for embedding in this file.
 * It's intentionally compact; it's sufficient for HMAC and PBKDF2 usage.
 *
 * (If you prefer, you can replace these functions with your project's SHA256)
 */

typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} sha256_ctx;

/* SHA256 constants */
static const uint32_t k_sha256[64] = {
    0x428a2f98ul, 0x71374491ul, 0xb5c0fbcful, 0xe9b5dba5ul, 0x3956c25bul, 0x59f111f1ul, 0x923f82a4ul, 0xab1c5ed5ul,
    0xd807aa98ul, 0x12835b01ul, 0x243185beul, 0x550c7dc3ul, 0x72be5d74ul, 0x80deb1feul, 0x9bdc06a7ul, 0xc19bf174ul,
    0xe49b69c1ul, 0xefbe4786ul, 0x0fc19dc6ul, 0x240ca1ccul, 0x2de92c6ful, 0x4a7484aaul, 0x5cb0a9dcul, 0x76f988daul,
    0x983e5152ul, 0xa831c66dul, 0xb00327c8ul, 0xbf597fc7ul, 0xc6e00bf3ul, 0xd5a79147ul, 0x06ca6351ul, 0x14292967ul,
    0x27b70a85ul, 0x2e1b2138ul, 0x4d2c6dfcul, 0x53380d13ul, 0x650a7354ul, 0x766a0abbul, 0x81c2c92eul, 0x92722c85ul,
    0xa2bfe8a1ul, 0xa81a664bul, 0xc24b8b70ul, 0xc76c51a3ul, 0xd192e819ul, 0xd6990624ul, 0xf40e3585ul, 0x106aa070ul,
    0x19a4c116ul, 0x1e376c08ul, 0x2748774cul, 0x34b0bcb5ul, 0x391c0cb3ul, 0x4ed8aa4aul, 0x5b9cca4ful, 0x682e6ff3ul,
    0x748f82eeul, 0x78a5636ful, 0x84c87814ul, 0x8cc70208ul, 0x90befffaul, 0xa4506cebul, 0xbef9a3f7ul, 0xc67178f2ul};

static inline uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(sha256_ctx *ctx)
{
    uint32_t m[64];
    for (int i = 0; i < 16; ++i)
    {
        m[i] = ((uint32_t)ctx->data[i * 4] << 24) | ((uint32_t)ctx->data[i * 4 + 1] << 16) | ((uint32_t)ctx->data[i * 4 + 2] << 8) | ((uint32_t)ctx->data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i)
    {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; ++i)
    {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k_sha256[i] + m[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}


static void sha256_init(sha256_ctx *ctx)
{
    ctx->state[0] = 0x6a09e667ul;
    ctx->state[1] = 0xbb67ae85ul;
    ctx->state[2] = 0x3c6ef372ul;
    ctx->state[3] = 0xa54ff53aul;
    ctx->state[4] = 0x510e527ful;
    ctx->state[5] = 0x9b05688cul;
    ctx->state[6] = 0x1f83d9abul;
    ctx->state[7] = 0x5be0cd19ul;
    ctx->bitlen = 0;
    ctx->datalen = 0;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64)
        {
            sha256_transform(ctx);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32])
{
    size_t i = ctx->datalen;

    /* Pad whatever data is left in the buffer. */
    if (ctx->datalen < 56)
    {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    }
    else
    {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx);

    for (i = 0; i < 4; ++i)
    {
        hash[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0x000000ff);
    }
}


/* ---------- HMAC-SHA256 ---------- */

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *msg, size_t msg_len,
                        uint8_t out[32])
{
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t tk[32];

    if (key_len > 64)
    {
        sha256_ctx tctx;
        sha256_init(&tctx);
        sha256_update(&tctx, key, key_len);
        sha256_final(&tctx, tk);
        key = tk;
        key_len = 32;
    }

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (int i = 0; i < 64; ++i)
    {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, msg, msg_len);
    uint8_t inner[32];
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}


/* ---------- PBKDF2-HMAC-SHA256 ---------- */
/* Implements PBKDF2 as defined in RFC 2898 using HMAC-SHA256 */
static int pbkdf2_hmac_sha256(const uint8_t *password, size_t password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint32_t iterations,
                              uint8_t *out, size_t out_len)
{
    if (!password || !salt || !out)
        return -1;
    uint32_t block_count = (out_len + 31) / 32;
    uint8_t U[32];
    uint8_t T[32];
    uint8_t *asalt = malloc(salt_len + 4);
    if (!asalt)
        return -2;
    memcpy(asalt, salt, salt_len);

    size_t produced = 0;
    for (uint32_t block = 1; block <= block_count; ++block)
    {
        /* asalt = salt || INT(block) (big-endian) */
        asalt[salt_len + 0] = (uint8_t)((block >> 24) & 0xFF);
        asalt[salt_len + 1] = (uint8_t)((block >> 16) & 0xFF);
        asalt[salt_len + 2] = (uint8_t)((block >> 8) & 0xFF);
        asalt[salt_len + 3] = (uint8_t)(block & 0xFF);

        hmac_sha256(password, password_len, asalt, salt_len + 4, U);
        memcpy(T, U, 32);

        for (uint32_t i = 1; i < iterations; ++i)
        {
            hmac_sha256(password, password_len, U, 32, U);
            for (int j = 0; j < 32; ++j)
                T[j] ^= U[j];
        }

        size_t to_copy = (produced + 32 > out_len) ? (out_len - produced) : 32;
        memcpy(out + produced, T, to_copy);
        produced += to_copy;
    }

    free(asalt);
    return 0;
}

/* ---------- Utility functions ---------- */

static int secure_random_bytes(uint8_t *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, len);
    close(fd);
    if ((size_t)r != len)
        return -2;
    return 0;
}

/* PKCS#7 padding: pad data buffer and return new buffer (caller must free).
 * - block_size typically 16.
 * - out_len will be multiple of block_size.
 */
static unsigned char *pkcs7_pad(const unsigned char *in, size_t in_len, size_t block_size, size_t *out_len)
{
    size_t pad = block_size - (in_len % block_size);
    size_t total = in_len + pad;
    unsigned char *out = malloc(total);
    if (!out)
        return NULL;
    memcpy(out, in, in_len);
    memset(out + in_len, (unsigned char)pad, pad);
    *out_len = total;
    return out;
}

/* PKCS#7 unpad in-place; returns new length or -1 on error */
static ssize_t pkcs7_unpad(unsigned char *buf, size_t buf_len, size_t block_size)
{
    if (buf_len == 0 || (buf_len % block_size) != 0)
        return -1;
    unsigned char pad = buf[buf_len - 1];
    if (pad == 0 || pad > block_size)
        return -1;
    for (size_t i = 0; i < pad; ++i)
    {
        if (buf[buf_len - 1 - i] != pad)
            return -1;
    }
    return (ssize_t)(buf_len - pad);
}

/* ---------- Public API Implementation ---------- */

int aes_encrypt_inplace(struct Payload *payload, const char *password)
{
    if (!payload || !password)
        return -1;
    if (payload->size == 0 || payload->data == NULL)
        return -2;

    const size_t SALT_LEN = 16;
    const size_t IV_LEN = 16;
    const uint32_t PBKDF2_ITERS = 100000;
    const size_t KEY_LEN = 32; /* AES-256 */

    uint8_t salt[SALT_LEN];
    uint8_t iv[IV_LEN];
    if (secure_random_bytes(salt, SALT_LEN) != 0)
        return -3;
    if (secure_random_bytes(iv, IV_LEN) != 0)
        return -4;

    uint8_t key[KEY_LEN];
    if (pbkdf2_hmac_sha256((const uint8_t *)password, strlen(password), salt, SALT_LEN, PBKDF2_ITERS, key, KEY_LEN) != 0)
    {
        return -5;
    }

    /* PKCS7 pad plaintext */
    size_t padded_len;
    unsigned char *padded = pkcs7_pad(payload->data, payload->size, 16, &padded_len);
    if (!padded)
        return -6;

    /* Allocate ciphertext buffer (same length as padded) */
    unsigned char *cipher = malloc(padded_len);
    if (!cipher)
    {
        free(padded);
        return -7;
    }
    memcpy(cipher, padded, padded_len); /* tiny-AES-c encrypts in-place */

    /* Setup AES context and encrypt in-place using tiny-AES-c */
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_encrypt_buffer(&ctx, cipher, (uint32_t)padded_len);

    /* Build final payload: salt||iv||ciphertext */
    size_t final_len = SALT_LEN + IV_LEN + padded_len;
    unsigned char *final_buf = malloc(final_len);
    if (!final_buf)
    {
        free(padded);
        free(cipher);
        return -8;
    }
    memcpy(final_buf, salt, SALT_LEN);
    memcpy(final_buf + SALT_LEN, iv, IV_LEN);
    memcpy(final_buf + SALT_LEN + IV_LEN, cipher, padded_len);

    /* Replace payload buffer */
    memset(payload->data, 0, payload->size); /* zero old plaintext */
    free(payload->data);
    free(padded);
    free(cipher);

    payload->data = final_buf;
    payload->size = final_len;
    payload->encrypted = 1;

    /* zero sensitive material */
    memset(key, 0, sizeof(key));
    memset(salt, 0, sizeof(salt));
    memset(iv, 0, sizeof(iv));

    return 0;
}

int aes_decrypt_inplace(struct Payload *payload, const char *password)
{
    if (!payload || !password)
        return -1;
    if (payload->size < 32)
        return -2; /* must be at least salt+iv */

    const size_t SALT_LEN = 16;
    const size_t IV_LEN = 16;
    const uint32_t PBKDF2_ITERS = 100000;
    const size_t KEY_LEN = 32;

    const unsigned char *buf = payload->data;
    size_t buf_len = payload->size;

    const unsigned char *salt = buf;
    const unsigned char *iv = buf + SALT_LEN;
    const unsigned char *cipher = buf + SALT_LEN + IV_LEN;
    size_t cipher_len = buf_len - (SALT_LEN + IV_LEN);
    if ((cipher_len % 16) != 0)
        return -3;

    uint8_t key[KEY_LEN];
    if (pbkdf2_hmac_sha256((const uint8_t *)password, strlen(password), salt, SALT_LEN, PBKDF2_ITERS, key, KEY_LEN) != 0)
    {
        return -4;
    }

    unsigned char *plain = malloc(cipher_len);
    if (!plain)
    {
        memset(key, 0, sizeof(key));
        return -5;
    }
    memcpy(plain, cipher, cipher_len);

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, plain, (uint32_t)cipher_len);

    /* Unpad PKCS7 */
    ssize_t unpadded_len = pkcs7_unpad(plain, cipher_len, 16);
    if (unpadded_len < 0)
    {
        free(plain);
        memset(key, 0, sizeof(key));
        return -6;
    }

    /* Replace payload buffer with plaintext */
    free(payload->data);
    payload->data = malloc((size_t)unpadded_len);
    if (!payload->data)
    {
        free(plain);
        memset(key, 0, sizeof(key));
        return -7;
    }
    memcpy(payload->data, plain, (size_t)unpadded_len);
    payload->size = (size_t)unpadded_len;
    payload->encrypted = 0;

    /* cleanup */
    memset(plain, 0, cipher_len);
    free(plain);
    memset(key, 0, sizeof(key));

    return 0;
}
