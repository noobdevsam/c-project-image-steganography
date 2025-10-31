/* aes.h - tiny-AES-c public domain implementation header */

#ifndef _AES_H_
#define _AES_H_

#include <stdint.h>

#define AES_BLOCKLEN 16 // Block length in bytes
#define AES_KEYLEN 32   // 32 bytes = 256 bits
#define AES_keyExpSize 240

struct AES_ctx
{
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx(struct AES_ctx *ctx, const uint8_t *key);
void AES_init_ctx_iv(struct AES_ctx *ctx, const uint8_t *key, const uint8_t *iv);
void AES_ctx_set_iv(struct AES_ctx *ctx, const uint8_t *iv);

/* CBC mode */
void AES_CBC_encrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, uint32_t length);
void AES_CBC_decrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, uint32_t length);

#endif /* _AES_H_ */
