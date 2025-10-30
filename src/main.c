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
