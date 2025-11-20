noobdevsam: provide step by step working flow of the encryption-decryption procedure by analyzing provided files. provide flow of data while passing through these codes.

GitHub Copilot: Summary
- Files analyzed: aes_wrapper.c (uses ../third_party/tiny-aes/aes.h/.c).
- Encryption produces payload: [16 bytes salt][16 bytes IV][ciphertext (AES‑CBC, PKCS#7, multiple of 16)].
- Decryption parses that layout, derives same key, AES‑CBC decrypts and PKCS#7 unpads.

Encryption: step‑by‑step data flow
1. Input
   - struct Payload *payload:
     - payload->data -> pointer to plaintext bytes
     - payload->size -> plaintext length (bytes)
     - payload->encrypted -> flag (should be 0)
   - const char *password

2. Sanity checks
   - payload and password non-NULL
   - payload->size != 0 and payload->data != NULL

3. Random material
   - secure_random_bytes(salt, 16) reads 16 bytes from /dev/urandom -> salt[]
   - secure_random_bytes(iv, 16) reads 16 bytes from /dev/urandom -> iv[]

4. Key derivation (PBKDF2-HMAC-SHA256)
   - pbkdf2_hmac_sha256(password, strlen(password), salt, 16, 100000, key, 32)
   - Internals:
     - HMAC-SHA256 uses internal sha256_* functions (sha256_init/update/final)
     - PBKDF2 computes blocks: U1 = HMAC(password, salt||INT(i)), then iterates 100k times XORing into T
   - Result: 32-byte AES-256 key -> key[]

5. PKCS#7 padding
   - pkcs7_pad(payload->data, payload->size, 16, &padded_len)
   - produces new buffer padded[] of length padded_len (multiple of 16)
   - pad value = number of padding bytes (1..16) appended

6. Prepare ciphertext buffer
   - cipher = malloc(padded_len)
   - memcpy(cipher, padded, padded_len)
   - Note: tiny-AES-c encrypts in-place so cipher holds data to be encrypted

7. AES-CBC encryption (tiny-AES-c)
   - AES_init_ctx_iv(&ctx, key, iv)
   - AES_CBC_encrypt_buffer(&ctx, cipher, padded_len)
   - cipher now contains ciphertext (same length as padded_len)

8. Build final payload buffer
   - final_len = 16 (salt) + 16 (iv) + padded_len
   - final_buf = malloc(final_len)
   - memcpy(final_buf, salt, 16)
   - memcpy(final_buf + 16, iv, 16)
   - memcpy(final_buf + 32, cipher, padded_len)
   - Data layout: [salt (16)] [iv (16)] [ciphertext (padded_len, multiple of 16)]

9. Replace payload content
   - Zero and free old plaintext payload->data
   - free(padded), free(cipher)
   - payload->data = final_buf
   - payload->size = final_len
   - payload->encrypted = 1

10. Cleanup
   - memset(key,0), memset(salt,0), memset(iv,0)
   - Return 0 on success

Errors returned at each failure point (examples)
- secure_random_bytes() failure -> return -3 / -4
- pbkdf2 failure -> -5
- malloc failure for padding/cipher/final -> -6..-8

Decryption: step‑by‑step data flow
1. Input
   - struct Payload *payload:
     - payload->data -> pointer to encrypted payload bytes
     - payload->size -> length (must be >= 32)
   - const char *password

2. Sanity checks
   - payload and password non-NULL
   - payload->size >= 32 (16 salt + 16 iv)
   - cipher_len = payload->size - 32
   - cipher_len must be multiple of 16 -> else return error

3. Parse layout
   - salt = payload->data (first 16 bytes)
   - iv = payload->data + 16 (next 16 bytes)
   - cipher = payload->data + 32 (rest, cipher_len bytes)

4. Key derivation (same as encryption)
   - pbkdf2_hmac_sha256(password, strlen(password), salt, 16, 100000, key, 32)
   - Produces identical key[] if password and salt match

5. Copy ciphertext and decrypt in-place
   - plain = malloc(cipher_len)
   - memcpy(plain, cipher, cipher_len)
   - AES_init_ctx_iv(&ctx, key, iv)
   - AES_CBC_decrypt_buffer(&ctx, plain, cipher_len)
   - plain now contains padded plaintext bytes

6. PKCS#7 unpad and validate
   - unpadded_len = pkcs7_unpad(plain, cipher_len, 16)
   - pkcs7_unpad checks:
     - buf_len multiple of block_size
     - last byte (pad) between 1 and block_size
     - last pad bytes all equal to pad value
     - returns buf_len - pad or -1 on invalid padding

7. Replace payload content
   - free(payload->data) (old encrypted buffer)
   - payload->data = malloc(unpadded_len)
   - memcpy(payload->data, plain, unpadded_len)
   - payload->size = unpadded_len
   - payload->encrypted = 0

8. Cleanup
   - memset(plain, 0, cipher_len); free(plain)
   - memset(key, 0, sizeof(key))
   - Return 0 on success

Key buffer / memory lifecycle and zeroing
- Key, salt, iv are explicitly zeroed after use via memset in both encrypt/decrypt to reduce residual sensitive data in memory.
- Old plaintext buffer is zeroed before free during encryption.
- Temporary buffers (padded, cipher, plain) are freed; plain is zeroed before freeing in decrypt.

Important byte / buffer names and sizes (quick reference)
- SALT_LEN = 16
- IV_LEN = 16
- KEY_LEN = 32
- PBKDF2_ITERS = 100000
- Layout: [salt(16)] [iv(16)] [ciphertext(padded multiple of 16)]
- tiny-AES-c requires data length multiple of 16 and encrypts/decrypts in-place.

Where code components live
- AES functions: third_party/tiny-aes/aes.h/.c (AES_init_ctx_iv, AES_CBC_encrypt_buffer, AES_CBC_decrypt_buffer)
- SHA256 / HMAC / PBKDF2: implemented inside aes_wrapper.c (sha256_* / hmac_sha256 / pbkdf2_hmac_sha256)
- Randomness: secure_random_bytes() reads /dev/urandom

Sanity/security notes
- PBKDF2 iterations = 100000: adjustable for cost.
- Salt and IV are random per-encryption; salt is stored in output so same password can recreate key.
- PKCS#7 padding checked during decrypt; invalid padding returns error.
- No authentication (no HMAC/AEAD) — ciphertext integrity not verified. If attackers can tamper ciphertext, decryption may succeed with wrong plaintext or padding oracle risks. Consider adding HMAC or switching to authenticated AES-GCM.

If you want, I can produce a simple diagram or annotated sequence using actual variable names and memory offsets.
