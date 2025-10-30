static void print_usage(const char *prog)
{
    fprintf(
        stderr,
        "Usage: %s [options]\n"
        "-------------------------------------------------------------------------------------------------------\n"
        "  -e --encode <cover-image> <payload-file> <output-dir>    [Mandetory] Embed payload into cover image\n"
        "-------------------------------------------------------------------------------------------------------\n"
        "  -d --decode <stego-image> <output-dir>                   [Mandetory] Extract payload from stego image\n"
        "-------------------------------------------------------------------------------------------------------\n"
        "  -l --lsb <1|2|3>                                         [Mandetory] LSB depth to use (default: 3)\n"
        "-------------------------------------------------------------------------------------------------------\n"
        "  -p --password <password>                                 [Optional] Password to use for AES encryption\n"
        "---------------------------------------------------------------------------------------------------------\n"
        "  -a --auto-convert                                        [Optional] Automatically convert JPEG to PNG (for encode)\n"
        "---------------------------------------------------------------------------------------------------------\n"
        "  --gui                                                    Launch GTK GUI\n"
        "---------------------------------------------------------------------------------------------------------\n"
        "  -h --help                                                Show this help\n"
        "---------------------------------------------------------------------------------------------------------\n"
        "\n"
        "Note: JPEG is a lossy format and not suitable for steganography as it\n"
        "      corrupts LSB data. Use PNG for reliable results. The --auto-convert\n"
        "      option will automatically convert JPEG covers to PNG before encoding.\n",
        prog);
}
static int cli_encode(
    const char *cover_path,
    const char *payload_path,
    const char *out_path,
    int lsb_depth,
    const char *password,
    bool auto_convert)
{
    struct Payload payload = {0};
    struct Image cover = {0};
    struct Image out = {0};
    int rc = 0; // Return code
    char *actual_cover_path = NULL;
    bool converted = false;

    // Check if cover is JPEG
    if (image_is_jpeg(cover_path))
    {
        fprintf(stderr, "Warning: Cover image is JPEG format.\n");
        fprintf(stderr, "JPEG is a lossy format and not suitable for steganography.\n");
        fprintf(stderr, "LSB data will be corrupted during JPEG compression.\n");

        if (auto_convert)
        {
            fprintf(stderr, "Auto-converting JPEG to PNG...\n");

            // Generate temporary PNG path
            char temp_png_path[4096];
            snprintf(temp_png_path, sizeof(temp_png_path), "/tmp/stego_converted_%d.png", (int)getpid());

            rc = image_convert_jpeg_to_png(cover_path, temp_png_path);
            if (rc != 0)
            {
                fprintf(stderr, "Error: Failed to convert JPEG to PNG\n");
                return rc;
            }

            actual_cover_path = strdup(temp_png_path);
            converted = true;
            fprintf(stderr, "Conversion successful. Using PNG for encoding.\n");
        }
        else
        {
            fprintf(stderr, "Error: Use --auto-convert flag to automatically convert to PNG.\n");
            fprintf(stderr, "Or manually convert to PNG before encoding.\n");
            return -1;
        }
    }
    else
    {
        actual_cover_path = strdup(cover_path);
    }

    rc = image_load(actual_cover_path, &cover);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to load cover image '%s'\n", actual_cover_path);
        if (converted)
        {
            unlink(actual_cover_path);
        }
        free(actual_cover_path);
        return rc;
    }

    rc = payload_load_from_file(payload_path, &payload);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to load payload file '%s'\n", payload_path);
        image_free(&cover);
        if (converted)
        {
            unlink(actual_cover_path);
        }
        free(actual_cover_path);
        return rc;
    }

    if (password && strlen(password) > 0)
    {
        rc = aes_encrypt_inplace(&payload, password);
        if (rc)
        {
            fprintf(stderr, "Error: Failed to encrypt payload with AES\n");
            payload_free(&payload);
            image_free(&cover);
            if (converted)
            {
                unlink(actual_cover_path);
            }
            free(actual_cover_path);
            return rc;
        }
    }

    // Use basename of payload_path for metadata
    char *payload_path_copy = strdup(payload_path);
    const char *payload_basename = basename(payload_path_copy);
    struct Metadata meta = metadata_create_from_payload(payload_basename, payload.size, lsb_depth, (password && strlen(password) > 0));
    free(payload_path_copy);

    rc = stego_embed(
        &cover,
        &payload,
        &meta,
        lsb_depth,
        &out);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to embed payload into cover image\n");
        metadata_free(&meta);
        payload_free(&payload);
        image_free(&cover);
        if (converted)
        {
            unlink(actual_cover_path);
        }
        free(actual_cover_path);
        return rc;
    }

    rc = image_save(out_path, &out);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to save stego image to '%s'\n", out_path);
    }
    else if (converted)
    {
        fprintf(stderr, "Successfully encoded using auto-converted PNG cover.\n");
    }

    metadata_free(&meta);
    payload_free(&payload);
    image_free(&cover);
    image_free(&out);

    // Clean up temporary file
    if (converted)
    {
        unlink(actual_cover_path);
    }
    free(actual_cover_path);

    return rc;
}
static int cli_decode(
    const char *stego_path,
    const char *out_dir,
    const char *password)
{
    struct Image img = {0};
    struct Metadata meta = {0};
    struct Payload payload = {0};
    int rc = 0; // Return code

    rc = image_load(stego_path, &img);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to load stego image '%s'\n", stego_path);
        return rc;
    }

    rc = stego_extract(&img, &meta, &payload);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to extract (maybe not a stego image)\n");
        image_free(&img);
        return rc;
    }
    fprintf(stderr, "[DEBUG] Decoded metadata: original filename='%s', size=%lu, lsb_depth=%d, encrypted=%d\n",
            meta.original_filename,
            (unsigned long)meta.file_size,
            meta.lsb_depth,
            meta.encrypted);

    fprintf(stderr, "Extracted payload size: %lu bytes\n", (unsigned long)payload.size);

    if (meta.encrypted && password && strlen(password) > 0)
    {
        rc = aes_decrypt_inplace(&payload, password);
        if (rc)
        {
            fprintf(stderr, "Error: Failed to decrypt payload with AES (maybe wrong password)\n");
            metadata_free(&meta);
            payload_free(&payload);
            image_free(&img);
            return rc;
        }
    }

    // Save extracted payload using original filename from metadata
    char out_path[4096];
    snprintf(
        out_path,
        sizeof(out_path),
        "%s/%s",
        out_dir,
        meta.original_filename);
    rc = payload_write_to_file(&payload, out_path);
    if (rc)
    {
        fprintf(stderr, "Error: Failed to save extracted payload to '%s'\n", out_path);
    }

    metadata_free(&meta);
    payload_free(&payload);
    image_free(&img);
    return rc;
}
static void launch_gui(int argc, char **argv)
{
    gui_init(&argc, &argv);
    gui_show_main_window();
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    /* Basic CLI parsing (not comprehensive) */
    bool use_gui = false;
    const char *cover = NULL;
    const char *payload = NULL;
    const char *out = NULL;
    const char *stego = NULL;
    const char *outdir = NULL;
    const char *password = NULL;
    int lsb_depth = 3;
    bool do_encode = false;
    bool do_decode = false;
    bool auto_convert = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encode") == 0)
        {
            if (i + 3 >= argc)
            {
                print_usage(argv[0]);
                return 1;
            }
            do_encode = true;
            cover = argv[++i];
            payload = argv[++i];
            out = argv[++i];
        }
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decode") == 0)
        {
            if (i + 2 >= argc)
            {
                print_usage(argv[0]);
                return 1;
            }
            do_decode = true;
            stego = argv[++i];
            outdir = argv[++i];
        }
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--lsb") == 0)
        {
            if (i + 1 >= argc)
            {
                print_usage(argv[0]);
                return 1;
            }
            lsb_depth = atoi(argv[++i]);
            if (lsb_depth < 1 || lsb_depth > 3)
            {
                fprintf(stderr, "Error: invalid LSB depth (must be 1,2,3)\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--password") == 0)
        {
            if (i + 1 >= argc)
            {
                print_usage(argv[0]);
                return 1;
            }
            password = argv[++i];
        }
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto-convert") == 0)
        {
            auto_convert = true;
        }
        else if (strcmp(argv[i], "--gui") == 0)
        {
            use_gui = true;
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (use_gui)
    {
        launch_gui(argc, argv);
        return 0;
    }

    if (do_encode)
    {
        return cli_encode(cover, payload, out, lsb_depth, password, auto_convert);
    }

    if (do_decode)
    {
        return cli_decode(stego, outdir, password);
    }

    print_usage(argv[0]);
    return 1;
}
