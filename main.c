/*
 * main.c — XJ380 模拟器 CLI 入口
 *
 * 用法: ./xj380_emu <ELF文件>
 */

#include "xj380_emu.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: %s <XJ380 ELF 文件>\n", argv[0]);
        return 1;
    }

    xj380_emu_t *emu = xj380_create();
    if (!emu) {
        fprintf(stderr, "创建模拟器失败\n");
        return 1;
    }

    if (xj380_load_elf(emu, argv[1]) != 0) {
        fprintf(stderr, "加载失败: %s\n", xj380_strerror(emu));
        xj380_destroy(emu);
        return 1;
    }

    int ret = xj380_run(emu, argc - 1, argv + 1);
    if (ret != 0) {
        fprintf(stderr, "运行错误: %s\n", xj380_strerror(emu));
    }

    xj380_destroy(emu);
    return ret;
}
