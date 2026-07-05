/*
 * main.c — XJ380 模拟器 CLI 入口
 *
 * Usage: ./xswl <ELF file>
 */

#include "xj380_emu.h"
#include "xj380_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

int main(int argc, char **argv)
{
    bool debug_enabled = true;
    bool native_enabled = false;
    const char *elf_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "nodebug") == 0 || strcmp(argv[i], "--nodebug") == 0) {
            debug_enabled = false;
        } else if (strcmp(argv[i], "--native") == 0) {
            native_enabled = true;
        } else if (!elf_path) {
            elf_path = argv[i];
        }
    }

    if (!elf_path) {
        fprintf(stderr, "Usage: %s [--native] [nodebug|--nodebug] <XJ380 ELF File>\n", argv[0]);
        return 1;
    }

    if (native_enabled) {
        int ret = xj380_native_run(elf_path, argc, argv, debug_enabled);
        if (ret != 0) {
            fprintf(stderr, "native runtime error: %s\n", xj380_native_strerror());
        }
        return ret;
    }

    xj380_emu_t *emu = xj380_create();
    if (!emu) {
        fprintf(stderr, "failed to create emulator\n");
        return 1;
    }

    xj380_set_debug(emu, debug_enabled);

    if (xj380_load_elf(emu, elf_path) != 0) {
        fprintf(stderr, "load failed: %s\n", xj380_strerror(emu));
        xj380_destroy(emu);
        return 1;
    }

    char *guest_argv[] = {(char *)elf_path, NULL};
    int ret = xj380_run(emu, 1, guest_argv);
    if (ret != 0) {
        fprintf(stderr, "runtime error: %s\n", xj380_strerror(emu));
    }

    xj380_destroy(emu);
    return ret;
}
