// The badge links data/out/bundle.bin into the app with board_build.embed_files,
// which defines these two symbols. The host build does the same with .incbin.
#ifndef BUNDLE_BIN_PATH
#error "BUNDLE_BIN_PATH is set by [env:host] in firmware/platformio.ini"
#endif

__asm__(
    ".section .rodata\n"
    ".balign 16\n"
    ".global _binary_bundle_bin_start\n"
    "_binary_bundle_bin_start:\n"
    ".incbin \"" BUNDLE_BIN_PATH "\"\n"
    ".global _binary_bundle_bin_end\n"
    "_binary_bundle_bin_end:\n"
    ".byte 0\n"
    ".previous\n");
