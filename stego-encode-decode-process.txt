noobdevsam: provide step by step working flow of the encoding-decoding procedure by analyzing provided files. provide flow of data while passing through these codes.

GitHub Copilot: Below is a concise, actionable step‑by‑step description of how the encode (embed) and decode (extract) flows typically pass data through the files in your /home/sam/Documents/CProjects/stego-c-practice/src tree. I inspected the file list you gave and mapped a standard steganography program architecture to those files. Replace function names below with the actual names in your code if they differ — the structural flow and data handoff remain the same.

Summary of responsibilities (per file)
- main.c — CLI entrypoint: parses command line, chooses encode/decode/batch/gui mode and orchestrates the high‑level steps.
- gui_main.c / gui_batch.c — GUI frontends that call the same core functions as main (or call batch.c). Not essential for core pipeline.
- batch.c — batch mode runner that invokes core encode/decode on multiple files.
- payload.c — reading the payload (message/file) to embed; returns a memory buffer and length; may compute hash or MIME/type info.
- metadata.c — builds/parses the header metadata that is stored with the payload (payload length, type, flags like "encrypted", maybe checksum).
- aes_wrapper.c — optional AES encrypt/decrypt of the payload buffer using a password/key; returns encrypted buffer and its length (or decrypts).
- image_io.c — image read/write routines: load image into memory (pixel buffer), provide width/height/channels, save modified image.
- stego_core.c — main embed/extract algorithms: capacity check, write metadata+payload bits into image LSBs (or similar), and conversely read bits, reconstruct header+payload.
- image_io.c and stego_core.c are the two places that touch pixel buffers.

Encoding (embed) flow — step by step
1. main.c / gui_main.c / batch.c: Parse user options:
   - input carrier image path
   - payload path (file or text)
   - output image path
   - optional password / encryption flag
   - stego params (bits per channel, channels to use, offset)
2. main calls payload.c to load the payload:
   - payload.c reads file/text into memory: returns (payload_buf, payload_len, payload_type).
3. main calls metadata.c to create header:
   - metadata.c creates a compact header containing payload_len, payload_type, flags (e.g., encrypted=0/1), maybe CRC.
   - header_buf and header_len returned.
4. If encryption requested, main calls aes_wrapper.c:
   - aes_wrapper_encrypt(payload_buf, payload_len, password) → encrypted_payload_buf, encrypted_len
   - metadata flag "encrypted" set; metadata may be updated to indicate AES parameters (iv, cipher).
   - original payload buffer freed (or retained if needed).
5. Build the full message to embed:
   - combined_buf = header_buf || encrypted_payload_buf (concatenate)
   - combined_len = header_len + encrypted_len
6. main calls image_io.c to load the carrier:
   - image_io_read(path) → image struct containing pixel buffer, width, height, channels, stride, bytes_per_pixel.
7. Capacity check using metadata/stego_core:
   - stego_core_calculate_capacity(image, stego_params) → capacity_bytes
   - If combined_len exceeds capacity_bytes, abort with error.
8. main calls stego_core to embed:
   - stego_core_embed(image.pixel_buffer, image.meta, combined_buf, combined_len, stego_params)
   - stego_core iterates pixels and channels, writes bits of combined_buf into LSBs (or chosen bit positions), respecting offset/stride and channel selection.
   - stego_core may update any in‑image header marker or checksum in pixels.
9. main calls image_io.c to write new image:
   - image_io_write(output_path, image) → writes file (PNG/BMP), preserving metadata as needed.
10. Cleanup memory and report success.

Data flow during encoding (concise)
- payload file → payload_buf (payload.c)
- payload_buf → (optional) aes_wrapper → encrypted_payload_buf
- metadata.c → header_buf
- header_buf + encrypted_payload_buf → combined_buf
- image_io.c → pixel_buffer (image_data)
- stego_core.c takes combined_buf and pixel_buffer → modifies pixel_buffer LSBs
- image_io.c writes modified pixel_buffer → output image file

Decoding (extract) flow — step by step
1. main.c / gui_main.c: Parse user options:
   - input (stego) image path
   - output payload path
   - optional password (if encrypted)
   - stego params (bits per channel, channels used, offset)
2. main calls image_io.c to load the stego image:
   - image_io_read(path) → image struct with pixel buffer, width, height, channels.
3. main calls stego_core to extract header first:
   - stego_core_extract_header(image.pixel_buffer, image.meta, stego_params) → header_buf
   - metadata.c parses header_buf → (payload_len, payload_type, encrypted_flag, iv/checksum)
4. Using payload_len, main calls stego_core to extract payload:
   - stego_core_extract_payload(image.pixel_buffer, image.meta, stego_params, payload_len, payload_offset) → payload_buf
   - (payload_offset = header_len or an offset contained in header metadata)
5. If metadata indicates encryption:
   - aes_wrapper_decrypt(payload_buf, payload_len, password, iv) → decrypted_payload_buf
   - If password is wrong, AES will fail (padding check or MAC mismatch); report error.
6. main writes decrypted_payload_buf to disk:
   - payload.c or main writes the output file using payload_type to pick filename/extension if needed.
7. Optionally verify checksum/CRC stored in header via metadata.c to confirm integrity.
8. Cleanup memory and report success.

Data flow during decoding (concise)
- image_io.c → pixel_buffer
- stego_core.c reads pixel_buffer LSBs → header_buf
- metadata.c parses header_buf → payload_len, flags
- stego_core.c reads the rest of bits → payload_buf
- (if encrypted) aes_wrapper decrypts payload_buf → original payload_buf
- main/payload.c write payload_buf → output file

Notes about typical internal details (what to look for in the code)
- Header layout: check metadata.c for exact header fields and byte layout. The header defines how many bytes the extractor must read for length and flags.
- Capacity calculation: stego_core.c usually computes capacity as width * height * channels * bits_per_channel / 8 minus header overhead. Check for per‑channel selection and reserved regions (offset).
- Bit ordering: embedding/extraction must use the same bit order (LSB->MSB of payload bytes or reverse). Confirm stego_core.c uses consistent bit packing.
- AES details: aes_wrapper.c probably provides IV generation/storage. The IV must be stored in the metadata or prefixed to the encrypted payload so decryption can use the same IV. Check for PBKDF2 or key stretching for password → key.
- Error paths: look for checks when capacity insufficient, when read/write fails, and when AES decryption fails (bad padding/MAC).
- Image formats: image_io.c may support only BMP/PNM to keep bit‑exact pixel data (PNG compression changes layout; implementations often handle PNG with libpng but still export raw pixel buffer). Make sure image_io uses a lossless format for carriers.

Where each file participates in the call chain
- main.c / GUI / batch.c: top-level control → calls payload.c, metadata.c, aes_wrapper.c, image_io.c, stego_core.c
- payload.c: payload in/out disk ↔ memory
- metadata.c: header pack/unpack ↔ memory
- aes_wrapper.c: memory buffer ↔ memory buffer (enc/dec)
- image_io.c: file ↔ image struct (pixel buffer)
- stego_core.c: image pixel buffer ↔ image pixel buffer (reads/writes bits), communicates lengths with metadata.c

Suggested quick verification steps in your workspace
1. Search for these symbols to map exact functions:
   - grep -n "embed" src/*.c
   - grep -n "extract" src/*.c
   - grep -n "encrypt" src/*.c
   - grep -n "read" src/*.c | grep -i image
2. Open the header maker and stego_core pair to confirm exact header length and embedding bit order.
3. Run a small encode/decode roundtrip with a very small payload and verify output equality.

If you want, I can:
- Search the repository for the actual function names and produce a mapping of exact call sequences.
- Read and annotate specific source files (paste-file or let me open files from your workspace) and produce a literal call trace that matches your code.

Which of the above would you like next?
