#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/*
 * xj380_emu.c — XJ380 高性能二进制模拟器核心
 *
 * Unicorn C API + ELF 加载 + xapi syscall 分发 + VFS。
 * 无 Python 层, 全路径 C 级别直通。
 */

#include "xj380_emu.h"

#ifdef XJ380_GUI
#include "xj380_gui.h"
#endif

#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* ================================================================
 * 常量
 * ================================================================ */

#define TRAMP_SLOT_SIZE   16      /* 每条 trampoline 16 字节 */
#define MAX_OPEN_FILES    256
#define VFS_PATH_MAX      512
#define MAX_WINDOWS       16

/* ================================================================
 * 虚拟文件条目
 * ================================================================ */

typedef struct {
    char    path[VFS_PATH_MAX];
    uint8_t *data;
    size_t  size;
    bool    is_dir;
} vfs_entry_t;

/* ================================================================
 * 模拟器实例
 * ================================================================ */

struct xj380_emu {
    uc_engine     *uc;
    uint64_t       entry_point;
    uint64_t       brk_addr;

    /* 错误 */
    char           error[256];

    /* 运行状态 */
    bool           running;
    int            exit_code;

    /* VFS */
    vfs_entry_t   *vfs;
    int            vfs_count;
    int            vfs_capacity;

    /* 用户 */
    UserInfo       current_user;

    /* GUI 回调 */
    xj380_render_cb render_cb;
};

/* ================================================================
 * 前向声明
 * ================================================================ */

static int  dispatch_xapi(xj380_emu_t *emu, int syscall_no);
static void xj380_syscall_hook(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data);
static void h_SYSBRK(xj380_emu_t *e);

/* ================================================================
 * 辅助：寄存器读写
 * ================================================================ */

static inline uint64_t r(xj380_emu_t *emu, int reg) {
    uint64_t v = 0; (void)uc_reg_read(emu->uc, reg, &v); return v;
}
static inline void w(xj380_emu_t *emu, int reg, uint64_t val) {
    (void)uc_reg_write(emu->uc, reg, &val);
}

/* ================================================================
 * 辅助：模拟器内存读写
 * ================================================================ */

int xj380_mem_read_str(xj380_emu_t *emu, uint64_t addr, char *buf, size_t max) {
    size_t i = 0;
    while (i < max - 1) {
        uint8_t c;
        if (uc_mem_read(emu->uc, addr + i, &c, 1) != UC_ERR_OK) return -1;
        buf[i] = (char)c;
        if (c == 0) break;
        i++;
    }
    buf[i] = '\0';
    return 0;
}

static int wr_str(xj380_emu_t *emu, uint64_t addr, const char *str) {
    return (int)uc_mem_write(emu->uc, addr,
        (const uint8_t*)str, strlen(str) + 1);
}

int xj380_mem_read(xj380_emu_t *emu, uint64_t addr, void *dst, size_t len) {
    return (int)uc_mem_read(emu->uc, addr, (uint8_t*)dst, len);
}

int xj380_mem_write(xj380_emu_t *emu, uint64_t addr, const void *src, size_t len) {
    return (int)uc_mem_write(emu->uc, addr, (const uint8_t*)src, len);
}

static uint64_t emu_brk(xj380_emu_t *emu, size_t size) {
    uint64_t addr = emu->brk_addr;
    uint64_t pagesz = (size + 0xFFFULL) & ~0xFFFULL;
    /* 按需映射: 跳过已映射区域 (UC_ERR_MAP) */
    uc_err err = uc_mem_map(emu->uc, addr, pagesz, UC_PROT_READ | UC_PROT_WRITE);
    (void)err;  /* 失败通常是已映射, 无害 */
    emu->brk_addr += pagesz;
    return addr;
}

/* ================================================================
 * 辅助：VFS
 * ================================================================ */

static int vfs_find(xj380_emu_t *emu, const char *path) {
    for (int i = 0; i < emu->vfs_count; i++)
        if (strcmp(emu->vfs[i].path, path) == 0) return i;
    return -1;
}

static int vfs_ensure(xj380_emu_t *emu) {
    if (emu->vfs_count >= emu->vfs_capacity) {
        size_t new_cap = (size_t)emu->vfs_capacity * 2;
        vfs_entry_t *new_vfs = realloc(emu->vfs, new_cap * sizeof(vfs_entry_t));
        if (!new_vfs) return -1;
        emu->vfs = new_vfs;
        emu->vfs_capacity = (int)new_cap;
    }
    return 0;
}

static int vfs_import_host(xj380_emu_t *emu, const char *vpath, const char *hpath) {
    if (vfs_ensure(emu) != 0) return -1;
    FILE *fp = fopen(hpath, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = NULL;
    if (sz > 0) {
        d = malloc((size_t)sz);
        if (!d) { fclose(fp); return -1; }
        (void)fread(d, 1, (size_t)sz, fp);
    }
    if (!d) { fclose(fp); return -1; }
    (void)fread(d, 1, (size_t)sz, fp);
    fclose(fp);
    int idx = emu->vfs_count++;
    strncpy(emu->vfs[idx].path, vpath, VFS_PATH_MAX - 1);
    emu->vfs[idx].data   = d;
    emu->vfs[idx].size   = (size_t)sz;
    emu->vfs[idx].is_dir = false;
    return idx;
}

static int vfs_create(xj380_emu_t *emu, const char *path, bool is_dir) {
    if (vfs_find(emu, path) >= 0) return -1;
    if (vfs_ensure(emu) != 0) return -1;
    int idx = emu->vfs_count++;
    strncpy(emu->vfs[idx].path, path, VFS_PATH_MAX - 1);
    emu->vfs[idx].data   = NULL;
    emu->vfs[idx].size   = 0;
    emu->vfs[idx].is_dir = is_dir;
    return idx;
}

/* ================================================================
 * ELF 加载器
 * ================================================================ */

/* ELF64 类型 */
typedef struct { uint8_t e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff; uint32_t e_flags; uint16_t e_ehsize, e_phentsize,
    e_phnum, e_shentsize, e_shnum, e_shstrndx; } Elf64_Ehdr;
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr,
    p_filesz, p_memsz, p_align; } Elf64_Phdr;

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

int xj380_load_elf(xj380_emu_t *emu, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { snprintf(emu->error, sizeof(emu->error), "打开失败: %s", path); return -1; }

    Elf64_Ehdr eh;
    if (fread(&eh, sizeof(eh), 1, fp) != 1) {
        snprintf(emu->error, sizeof(emu->error), "读 ELF 头失败"); fclose(fp); return -1;
    }

    if (memcmp(eh.e_ident, "\x7F" "ELF", 4) || eh.e_ident[4] != 2) {
        snprintf(emu->error, sizeof(emu->error), "非 64 位 ELF"); fclose(fp); return -1;
    }
    if (eh.e_machine != 0x3E) {
        snprintf(emu->error, sizeof(emu->error), "非 x86_64"); fclose(fp); return -1;
    }

    emu->entry_point = eh.e_entry ? eh.e_entry : XJ380_TEXT_BASE;

    /* 映射固定区域（堆改为按需映射, 不再预分配 16MB） */
    uc_mem_map(emu->uc, XJ380_STACK_BASE, XJ380_STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(emu->uc, XJ380_TRAMP_BASE, XJ380_TRAMP_SIZE, UC_PROT_READ | UC_PROT_EXEC);
    uc_mem_map(emu->uc, 0xFFFFF000, 0x1000, UC_PROT_READ | UC_PROT_EXEC); /* 事件回调返回 */
    emu->brk_addr = XJ380_HEAP_BASE;

    /* 先读取所有程序头（不要在处理段数据时 fseek 破坏文件指针） */
    Elf64_Phdr *phdrs = calloc((size_t)eh.e_phnum, sizeof(Elf64_Phdr));
    fseek(fp, (long)eh.e_phoff, SEEK_SET);
    for (int i = 0; i < eh.e_phnum; i++) {
        if (fread(&phdrs[i], sizeof(Elf64_Phdr), 1, fp) != 1) {
            free(phdrs);
            snprintf(emu->error, sizeof(emu->error), "读取程序头 %d 失败", i);
            fclose(fp);
            return -1;
        }
    }

    /* 先合并重叠的 LOAD 段，再统一映射 (修复 Windows 下相邻段重叠问题) */
    {
        typedef struct { uint64_t s, e; int p; } seg_t;
        seg_t segs[16]; int sn = 0;

        for (int i = 0; i < eh.e_phnum; i++) {
            Elf64_Phdr ph = phdrs[i];
            if (ph.p_type != PT_LOAD) continue;
            uint64_t s = ph.p_vaddr & ~0xFFFULL;
            uint64_t e = (ph.p_vaddr + ph.p_memsz + 0xFFF) & ~0xFFFULL;
            int p = 0;
            if (ph.p_flags & PF_R) p |= UC_PROT_READ;
            if (ph.p_flags & PF_W) p |= UC_PROT_WRITE;
            if (ph.p_flags & PF_X) p |= UC_PROT_EXEC;
            if (!p) p = UC_PROT_READ;

            int merged = 0;
            for (int j = 0; j < sn; j++) {
                if (s < segs[j].e && e > segs[j].s) {
                    if (s < segs[j].s) segs[j].s = s;
                    if (e > segs[j].e) segs[j].e = e;
                    segs[j].p |= p;
                    merged = 1; break;
                }
            }
            if (!merged && sn < 16) segs[sn++] = (seg_t){s, e, p};
        }

        for (int i = sn - 1; i >= 0; i--) {
            uint64_t sz = segs[i].e - segs[i].s;
            printf("[ELF] LOAD [0x%llx-0x%llx] flags=%c%c%c\n",
                   (unsigned long long)segs[i].s, (unsigned long long)segs[i].e,
                   (segs[i].p & UC_PROT_READ)  ? 'R' : '-',
                   (segs[i].p & UC_PROT_WRITE) ? 'W' : '-',
                   (segs[i].p & UC_PROT_EXEC)  ? 'X' : '-');
            uc_mem_map(emu->uc, segs[i].s, sz, (uint32_t)segs[i].p);
        }
    }

    /* 读取段数据到已映射的内存 */
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr ph = phdrs[i];
        if (ph.p_type != PT_LOAD) continue;

        if (ph.p_filesz > 0) {
            uint8_t *buf = malloc((size_t)ph.p_filesz);
            fseek(fp, (long)ph.p_offset, SEEK_SET);
            size_t rd = fread(buf, 1, ph.p_filesz, fp);
            xj380_mem_write(emu, ph.p_vaddr, buf, rd);
            free(buf);
        }
        if (ph.p_memsz > ph.p_filesz) {
            uint8_t *z = calloc(1, ph.p_memsz - ph.p_filesz);
            xj380_mem_write(emu, ph.p_vaddr + ph.p_filesz, z, ph.p_memsz - ph.p_filesz);
            free(z);
        }
    }
    /* 重新计算 max_vaddr */
    uint64_t max_vaddr = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint64_t e = (phdrs[i].p_vaddr + phdrs[i].p_memsz + 0xFFF) & ~0xFFFULL;
            if (e > max_vaddr) max_vaddr = e;
        }
    }
    /* 扫描可执行段中的 syscall 指令并注册 hook */
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr ph = phdrs[i];
        if (ph.p_type != PT_LOAD) continue;
        int prot = 0;
        if (ph.p_flags & PF_R) prot |= UC_PROT_READ;
        if (ph.p_flags & PF_W) prot |= UC_PROT_WRITE;
        if (ph.p_flags & PF_X) prot |= UC_PROT_EXEC;
        if (!(prot & UC_PROT_EXEC)) continue;

        uint64_t scan_size = ph.p_memsz;
        if (scan_size > 0x200000) scan_size = 0x200000;
        uint8_t *scan = malloc(scan_size);
        if (!scan) continue;

        uc_mem_read(emu->uc, ph.p_vaddr, scan, scan_size);
        uc_hook h_sys;
        for (size_t off = 0; off < scan_size - 1; off++) {
            if (scan[off] == 0x0F && scan[off + 1] == 0x05) {
                uint64_t saddr = ph.p_vaddr + off;
                (void)uc_hook_add(emu->uc, &h_sys, UC_HOOK_CODE,
                                  (void*)xj380_syscall_hook, emu,
                                  saddr, saddr + 1);
                printf("[ELF] 发现 syscall @ 0x%llx, 已注册 hook\n",
                       (unsigned long long)saddr);
            }
        }
        free(scan);
    }

    free(phdrs);
    fclose(fp);

    if (max_vaddr > emu->brk_addr)
        emu->brk_addr = (max_vaddr + 0xFFF) & ~0xFFFULL;

    /* 写入 xapi trampolines
     * 每条占 16 字节:
     *   48 C7 C0 <syscall_no>    mov rax, imm32      (7 bytes)
     *   0F 05                    syscall              (2 bytes)
     *   C3                       ret                  (1 byte)
     *   填充 NOP
     */
    static const char *xapi_names[XAPI_COUNT] = {
        [XAPI_OUTPUT]="OUTPUT",[XAPI_INPUT]="INPUT",[XAPI_GETLINE]="GETLINE",
        [XAPI_GETCH]="GETCH",[XAPI_ENDLINE]="ENDLINE",[XAPI_PRINTLINE]="PRINTLINE",
        [XAPI_PRINTF]="PRINTF",[XAPI_OUTPUTSERIAL]="OUTPUTSERIAL",
        [XAPI_OPENFILE]="OPENFILE",[XAPI_CLOSEFILE]="CLOSEFILE",
        [XAPI_SEARCHFILE]="SEARCHFILE",[XAPI_MKDIR]="MKDIR",
        [XAPI_CREATEFILE]="CREATEFILE",[XAPI_DELETEFILE]="DELETEFILE",
        [XAPI_RENAMEFILE]="RENAMEFILE",[XAPI_READFILE]="READFILE",
        [XAPI_WRITEFILE]="WRITEFILE",[XAPI_RMDIR]="RMDIR",
        [XAPI_CHAR2INT]="CHAR2INT",[XAPI_INT2CHAR]="INT2CHAR",
        [XAPI_HEX2CHAR]="HEX2CHAR",[XAPI_TORGB]="TORGB",[XAPI_TORGBA]="TORGBA",
        [XAPI_FORK]="FORK",[XAPI_EXECVE]="EXECVE",[XAPI_EXIT]="EXIT",
        [XAPI_GETTASKLIST]="GETTASKLIST",[XAPI_KILLPROCESS]="KILLPROCESS",
        [XAPI_GETSYSTEMVERSION]="GETSYSTEMVERSION",[XAPI_GETTIME]="GETTIME",
        [XAPI_GETCURRENTUSER]="GETCURRENTUSER",[XAPI_GETTIMEX]="GETTIMEX",
        [XAPI_GETCPUMODEL]="GETCPUMODEL",[XAPI_GETMEMORYSIZE]="GETMEMORYSIZE",
        [XAPI_BROKEN]="BROKEN",[XAPI_SENDAPPMESSAGE]="SENDAPPMESSAGE",
        [XAPI_SLEEP]="SLEEP",[XAPI_RUN]="RUN",[XAPI_RUNARGS]="RUNARGS",
        [XAPI_FLUSHTIME]="FLUSHTIME",[XAPI_SETMSGPRCOR]="SETMSGPRCOR",
        [XAPI_ALLOCATEMEMORY]="ALLOCATEMEMORY",[XAPI_FREEMEMORY]="FREEMEMORY",
        [XAPI_MAPMEMORY]="MAPMEMORY",
    };

    uint8_t tramp[TRAMP_SLOT_SIZE];
    for (int i = 0; i < XAPI_COUNT; i++) {
        if (!xapi_names[i]) continue;
        memset(tramp, 0x90, TRAMP_SLOT_SIZE); /* NOP 填充 */
        /* mov rax, imm32 */
        tramp[0] = 0x48; tramp[1] = 0xC7; tramp[2] = 0xC0;
        memcpy(tramp + 3, &i, 4);
        /* syscall */
        tramp[7] = 0x0F; tramp[8] = 0x05;
        /* ret */
        tramp[9] = 0xC3;

        xj380_mem_write(emu, XJ380_TRAMP_BASE + (uint64_t)i * TRAMP_SLOT_SIZE, tramp, TRAMP_SLOT_SIZE);

        /* 导出符号表（仅调试模式下打印） */
#ifdef DEBUG_TRACE
        printf("[xj380] xapi_%-20s @ 0x%llx (syscall #%d)\n",
               xapi_names[i],
               (unsigned long long)(XJ380_TRAMP_BASE + (uint64_t)i * TRAMP_SLOT_SIZE), i);
#else
        (void)xapi_names;
#endif
    }

    /* 初始化栈 */
    uint64_t rsp = XJ380_STACK_BASE + XJ380_STACK_SIZE - 0x1000;
    w(emu, UC_X86_REG_RSP, rsp);
    w(emu, UC_X86_REG_RBP, rsp);

    printf("[xj380] ELF 加载完成, 入口=0x%llx\n", (unsigned long long)emu->entry_point);
    return 0;
}

/* ================================================================
 * XJ380 syscall 编号 → 内部 handler 映射 (从 main.epf 逆向提取)
 * ================================================================ */

typedef struct { uint64_t xj380_no; int internal_no; } syscall_map_entry_t;

static const syscall_map_entry_t syscall_map[] = {
    /* Linux 标准 syscall (XJ380 复用) */
    {60,  XAPI_EXIT},              /* SYS_EXIT */
    {231, XAPI_EXIT},              /* SYS_EXIT_GROUP */

    /* XJ380 XAPI syscall (基址 7380, 来自 libsys.h) */
    /* P3.1 文本 I/O */
    {7381, XAPI_OUTPUT},           /* XAPI_OUTPUT */
    {7382, XAPI_INPUT},            /* XAPI_INPUT */
    {7383, XAPI_GETCH},            /* XAPI_GETCH */
    {7384, XAPI_ENDLINE},          /* XAPI_ENDLINE */
    {7385, XAPI_PRINTLINE},        /* XAPI_PRINTLINE */
    {7386, XAPI_PRINTF},           /* XAPI_PRINTF */
    {7418, XAPI_GETLINE},          /* XAPI_GETLINE */
    {7427, XAPI_OUTPUTSERIAL},     /* XAPI_OUTPUT_SERIAL */

    /* P3.2 文件操作 */
    {7387, XAPI_OPENFILE},         /* XAPI_OPEN_FILE */
    {7388, XAPI_CLOSEFILE},        /* XAPI_CLOSE_FILE */
    {7416, XAPI_SEARCHFILE},       /* XAPI_SEARCH_FILE */
    {7425, XAPI_MKDIR},            /* XAPI_MAKEDIR */
    {7420, XAPI_CREATEFILE},       /* XAPI_CREATE_FILE */
    {7421, XAPI_DELETEFILE},       /* XAPI_DELETE_FILE */
    {7422, XAPI_RENAMEFILE},       /* XAPI_RENAME_FILE */
    {7423, XAPI_READFILE},         /* XAPI_READ_FILE */
    {7424, XAPI_WRITEFILE},        /* XAPI_WRITE_FILE */
    {7444, XAPI_RMDIR},            /* XAPI_REMOVEDIR */

    /* P3.4 进程 */
    {7389, XAPI_FORK},             /* XAPI_FORK */
    {7390, XAPI_EXECVE},           /* XAPI_EXECVE */
    {7448, XAPI_GETTASKLIST},      /* XAPI_GET_TASK_LIST */
    {7449, XAPI_KILLPROCESS},      /* XAPI_KILL_PROCESS */

    /* P3.5 系统信息 */
    {7391, XAPI_GETSYSTEMVERSION}, /* XAPI_GET_VERSION */
    {7412, XAPI_GETTIME},          /* XAPI_GET_TIME */
    {7413, XAPI_GETCURRENTUSER},   /* XAPI_GET_CURRENT_USER */
    {7433, XAPI_GETTIMEX},         /* XAPI_GET_TIME_X */
    {7434, XAPI_GETCPUMODEL},      /* XAPI_GET_CPU_MODEL */
    {7435, XAPI_GETMEMORYSIZE},    /* XAPI_GET_MEMORY_SIZE */

    /* P3.6 系统消息 */
    {7428, XAPI_BROKEN},           /* XAPI_BROKEN */
    {7429, XAPI_SENDAPPMESSAGE},   /* XAPI_SEND_APP_MSG */
    {7430, XAPI_SLEEP},            /* XAPI_SLEEP */
    {7439, XAPI_RUN},              /* XAPI_RUN */
    {7450, XAPI_RUNARGS},          /* XAPI_RUN_ARGS */
    {7445, XAPI_FLUSHTIME},        /* XAPI_FLUSH_TIME */
    {7405, XAPI_SETMSGPRCOR},      /* XAPI_SET_MSH_PROCOR */

    /* P3.7 内存 */
    {7441, XAPI_ALLOCATEMEMORY},   /* XAPI_MALLOC */
    {7442, XAPI_FREEMEMORY},       /* XAPI_FREE */
    {7443, XAPI_MAPMEMORY},        /* XAPI_MAP_MEMORY */

    /* P4.1 窗口 */
    {7392, XAPI_CREATEWINDOW},     /* XAPI_CREATE_WINDOW */
    {7393, XAPI_SETWINDOWTITLE},   /* XAPI_SET_WINDOW_TITLE */
    {7394, XAPI_CLOSEWINDOW},      /* XAPI_CLOSE_WINDOW */
    {7395, XAPI_SETICON},          /* XAPI_SET_ICON */
    {7426, XAPI_GETWINDOWSIZE},    /* XAPI_GET_WIN_SZIE */

    /* P4.2 绘图 */
    {7396, XAPI_DRAWPOINT},        /* XAPI_DRAW_POINT */
    {7397, XAPI_DRAWLINE},         /* XAPI_DRAW_LINE */
    {7398, XAPI_DRAWRECT},         /* XAPI_DRAW_RECT */
    {7399, XAPI_DRAWRECT},         /* XAPI_DRAW_RECT_FILL → 同 DRAWRECT */
    {7402, XAPI_DRAWTEXT},         /* XAPI_DRAW_TEXT */
    {7414, XAPI_DRAWTEXTL},        /* XAPI_DRAW_TEXT_L */
    {7415, XAPI_DRAWSWTEXT},       /* XAPI_DRAW_TEXT_SW */
    {7431, XAPI_CALCTEXTWIDTH},    /* XAPI_CALC_TEXT_WIDTH */

    /* P4.3 图片 */
    {7403, XAPI_DRAWBMP},          /* XAPI_DRAW_BMP */
    {7404, XAPI_DRAWPNG},          /* XAPI_DRAW_PNG */
    {7419, XAPI_DRAWPICTURE},      /* XAPI_DRAW_PICTURE */
    {7440, XAPI_GETPICSIZE},       /* XAPI_GET_PIC_SIZE */
    {7446, XAPI_DRAWSVG},          /* XAPI_DRAW_SVG */
    {7447, XAPI_DRAWFA},           /* XAPI_DRAW_FA */

    /* P4.5 Framebuffer */
    {7406, XAPI_READBUFFER},       /* XAPI_READBUFFER */
    {7407, XAPI_WRITEBUFFER},      /* XAPI_WRITEBUFFER */
    {7417, XAPI_READBUFFERA},      /* XAPI_READBUFFERA */
    {7408, XAPI_WRITEBUFFERA},     /* XAPI_WRITEBUFFERA */
    {7409, XAPI_REFRESHWINDOW},    /* XAPI_REFRESH_WINDOW */
    {7438, XAPI_REFRESHPARTWINDOW},/* XAPI_REFRESH_PART_WINDOW */

    /* P4.6 控件 */
    {7410, XAPI_BUTTON},           /* XAPI_BUTTON */
    {7411, XAPI_EMPBUTTON},        /* XAPI_BUTTON_EMP */
    {7432, XAPI_DELETEBUTTON},     /* XAPI_DELETE_BUTTON */
    {7436, XAPI_REGISTERRIGHTBUTTONMENU}, /* XAPI_REG_RB_MENU */
    {7437, XAPI_DELETERIGHTBUTTONMENU},   /* XAPI_URG_RB_MENU */
};

static int map_xj380_to_internal(uint64_t xj380_no)
{
    for (size_t i = 0; i < sizeof(syscall_map) / sizeof(syscall_map[0]); i++) {
        if (syscall_map[i].xj380_no == xj380_no)
            return syscall_map[i].internal_no;
    }
    return -1;
}

/* 前向声明: handlers 数组在文件后面定义 */
typedef void (*xapi_fn)(xj380_emu_t*);
static const xapi_fn handlers[XAPI_COUNT];

/*
 * xj380_syscall_hook — 截获真实的 syscall 指令 (针对 xxcc 编译的 XJ380 二进制)
 *
 * 当 Unicorn 执行到 syscall 时触发。
 * Linux syscall ABI: RAX=syscall#, RDI=arg1, RSI=arg2, RDX=arg3,
 *                    R10=arg4, R8=arg5, R9=arg6
 *
 * 注意: enter_syscall 已经把 XJ380 的函数参数重排到了 Linux syscall 寄存器。
 * 我们的 handler 读取 x86_64 ABI (RCX=arg4), 所以需要 R10→RCX。
 */
static void xj380_syscall_hook(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    (void)uc;
    (void)size;
    xj380_emu_t *emu = (xj380_emu_t*)user_data;
    uint64_t xj380_no = r(emu, UC_X86_REG_RAX);

    /* Linux syscall ABI: arg4 在 R10, 我们 handler 期望在 RCX */
    uint64_t r10_val = r(emu, UC_X86_REG_R10);
    w(emu, UC_X86_REG_RCX, r10_val);

    /* 特殊 syscall: SYS_BRK (12) */
    if (xj380_no == 12) {
        h_SYSBRK(emu);
    } else {
        int internal_no = map_xj380_to_internal(xj380_no);
        if (internal_no >= 0 && internal_no < XAPI_COUNT && handlers[internal_no]) {
            handlers[internal_no](emu);
        } else {
            fprintf(stderr, "[xj380] 未知 XJ380 syscall: 0x%llx (%llu)\n",
                    (unsigned long long)xj380_no, (unsigned long long)xj380_no);
            w(emu, UC_X86_REG_RAX, UINT64_MAX);
        }
    }

    /* 跳过 syscall 指令 (2 字节), 返回 enter_syscall 的收尾代码 */
    w(emu, UC_X86_REG_RIP, addr + 2);
}

#ifdef DEBUG_TRACE
/* 调试：代码 hook，打印执行轨迹 */
static void debug_code_hook(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    (void)uc;
    (void)user_data;
    fprintf(stderr, "  [TRACE] 0x%llx (%u bytes)\n", (unsigned long long)addr, size);
}
#endif

/*
 * xapi trampoline hook — 截获对 xapi 函数的调用
 *
 * trampoline 布局 (每条 16 字节):
 *   48 C7 C0 <syscall_no>    mov rax, imm32      (7 bytes)
 *   0F 05                    syscall              (2 bytes)
 *   C3                       ret                  (1 byte)
 *
 * 当程序 call 到 trampoline 地址时：
 *   1. 根据 addr 计算 syscall_no
 *   2. 从 x86_64 ABI 寄存器 (RDI,RSI,RDX,RCX,R8,R9) 读参数
 *   3. 调用 C handler
 *   4. RAX = 返回值
 *   5. 从栈顶弹出返回地址 → RIP, RSP += 8
 */
static void tramp_hook(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    (void)size;
    xj380_emu_t *emu = (xj380_emu_t*)user_data;

    if (addr < XJ380_TRAMP_BASE || addr >= XJ380_TRAMP_BASE + XJ380_TRAMP_SIZE)
        return;

    int syscall_no = (int)((addr - XJ380_TRAMP_BASE) / TRAMP_SLOT_SIZE);
    if (syscall_no < 0 || syscall_no >= XAPI_COUNT) return;

    /* 先让 handler 读取调用者设置的 RDI..R9 */
    dispatch_xapi(emu, syscall_no);

    /* 模拟 ret: 从栈顶弹返回地址 */
    uint64_t rsp = r(emu, UC_X86_REG_RSP);
    uint64_t ret_addr = 0;
    uc_mem_read(uc, rsp, (uint8_t*)&ret_addr, 8);
    rsp += 8;
    w(emu, UC_X86_REG_RSP, rsp);
    w(emu, UC_X86_REG_RIP, ret_addr);
}

static void on_syscall(uc_engine *uc, uint32_t intno, void *user_data)
{
    (void)uc;
    (void)intno;
    (void)user_data;
    /* 备用：如果 syscall 指令真的触发了 */
    fprintf(stderr, "  [SYSCALL FALLBACK]\n");
}

/* ================================================================
 * xapi handler 注册表
 * ================================================================ */

static void h_OUTPUT(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    if (xj380_mem_read_str(e, a, b, sizeof(b)) == 0) { printf("%s", b); fflush(stdout); }
}
static void h_INPUT(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    if (fgets(b, (int)sizeof(b), stdin)) {
        char *s = strchr(b, ' '); if (s) *s = 0;
        char *n = strchr(b, '\n'); if (n) *n = 0;
        wr_str(e, a, b); w(e, UC_X86_REG_RAX, a);
    } else w(e, UC_X86_REG_RAX, 0);
}
static void h_GETLINE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    if (fgets(b, (int)sizeof(b), stdin)) {
        char *n = strchr(b, '\n'); if (n) *n = 0;
        wr_str(e, a, b);
    }
}
static void h_GETCH(xj380_emu_t *e) { w(e, UC_X86_REG_RAX, (uint64_t)getchar()); }
static void h_ENDLINE(xj380_emu_t *e) { (void)e; printf("\n"); fflush(stdout); }
static void h_PRINTLINE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    if (xj380_mem_read_str(e, a, b, sizeof(b)) == 0) { printf("%s\n", b); fflush(stdout); }
}
static void h_PRINTF(xj380_emu_t *e) {
    uint64_t f = r(e, UC_X86_REG_RDI), a1 = r(e, UC_X86_REG_RSI);
    char fmt[4096];
    if (xj380_mem_read_str(e, f, fmt, sizeof(fmt)) == 0) {
        for (char *p = fmt; *p; p++) {
            if (*p == '%') {
                p++;
                if (*p == 's') {
                    char ab[4096];
                    if (xj380_mem_read_str(e, a1, ab, sizeof(ab)) == 0) printf("%s", ab);
                } else if (*p == 'd' || *p == 'x' || *p == 'u') {
                    printf("%lld", (long long)a1);
                } else if (*p == 'l' && (p[1]=='l'||p[1]=='x')) {
                    printf("%lld", (long long)a1); p++;
                } else if (*p == '%') putchar('%');
                else { putchar('%'); putchar(*p); }
            } else putchar(*p);
        }
        fflush(stdout);
    }
}
static void h_OUTPUTSERIAL(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    if (xj380_mem_read_str(e, a, b, sizeof(b)) == 0) fprintf(stderr, "[serial] %s\n", b);
}
static void h_OPENFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    if (xj380_mem_read_str(e, a, p, sizeof(p)) != 0) { w(e, UC_X86_REG_RAX, 0); return; }
    printf("[xj380] OpenFile: %s\n", p);
    int idx = vfs_find(e, p);
    if (idx < 0) {
        const char *hp = (*p == '/') ? p + 1 : p;
        idx = vfs_import_host(e, p, hp);
        if (idx < 0) idx = vfs_create(e, p, false);
        if (idx < 0) { w(e, UC_X86_REG_RAX, 0); return; }
    }
    vfs_entry_t *ent = &e->vfs[idx];
    uint64_t xf = emu_brk(e, 16);
    uint64_t len = ent->size, buf = 0;
    if (ent->size > 0 && ent->data) {
        buf = emu_brk(e, ent->size);
        xj380_mem_write(e, buf, ent->data, ent->size);
    }
    xj380_mem_write(e, xf,     &len, 8);
    xj380_mem_write(e, xf + 8, &buf, 8);
    w(e, UC_X86_REG_RAX, xf);
}
static void h_CLOSEFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); if (!a) return;
    /* XFILE = { UINT64 length; void *buffer; } 共 16 字节 */
    uint64_t len = 0, buf_ptr = 0, zero = 0;
    xj380_mem_read(e, a,      &len,     8);
    xj380_mem_read(e, a + 8,  &buf_ptr, 8);
    printf("[xj380] CloseFile: len=%llu buf=0x%llx\n",
           (unsigned long long)len, (unsigned long long)buf_ptr);
    /* 清零 XFILE 结构 */
    xj380_mem_write(e, a,      &zero, 8);
    xj380_mem_write(e, a + 8,  &zero, 8);
}
static void h_SEARCHFILE(xj380_emu_t *e) {
    uint64_t pa = r(e, UC_X86_REG_RDI), ca = r(e, UC_X86_REG_RSI), da = r(e, UC_X86_REG_RDX);
    char path[512]; xj380_mem_read_str(e, pa, path, sizeof(path));
    size_t pl = strlen(path);
    UINT32 cnt = 0; DirNode db[256]; memset(db, 0, sizeof(db));
    for (int i = 0; i < e->vfs_count && cnt < 255; i++) {
        if (strncmp(e->vfs[i].path, path, pl) == 0) {
            const char *nm = e->vfs[i].path + pl; if (*nm == '/') nm++;
            if (!*nm) continue;
            strncpy(db[cnt].filename, nm, 255);
            db[cnt].length   = e->vfs[i].size;
            db[cnt].filetype = e->vfs[i].is_dir ? 1 : 0;
            cnt++;
        }
    }
    if (cnt > 255) cnt = 256;
    xj380_mem_write(e, ca, &cnt, 4);
    if (da && cnt <= 255) xj380_mem_write(e, da, db, cnt * sizeof(DirNode));
}
static void h_MKDIR(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p)); vfs_create(e, p, true);
    printf("[xj380] Mkdir: %s\n", p);
}
static void h_CREATEFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p)); vfs_create(e, p, false);
    printf("[xj380] CreateFile: %s\n", p);
}
static void h_DELETEFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p));
    int idx = vfs_find(e, p);
    if (idx >= 0) { free(e->vfs[idx].data); e->vfs[idx].data = NULL; e->vfs[idx].size = 0; }
}
static void h_RENAMEFILE(xj380_emu_t *e) {
    uint64_t oa = r(e, UC_X86_REG_RDI), na = r(e, UC_X86_REG_RSI);
    char op[512], np[512];
    xj380_mem_read_str(e, oa, op, sizeof(op)); xj380_mem_read_str(e, na, np, sizeof(np));
    int idx = vfs_find(e, op);
    if (idx >= 0) strncpy(e->vfs[idx].path, np, VFS_PATH_MAX - 1);
}
static void h_READFILE(xj380_emu_t *e) {
    uint64_t pa = r(e, UC_X86_REG_RDI), ba = r(e, UC_X86_REG_RSI);
    uint64_t sz = r(e, UC_X86_REG_RDX),  off = r(e, UC_X86_REG_RCX);
    char p[512]; xj380_mem_read_str(e, pa, p, sizeof(p));
    int idx = vfs_find(e, p);
    if (idx >= 0 && e->vfs[idx].data && off < e->vfs[idx].size) {
        size_t cp = (off + sz > e->vfs[idx].size) ? (e->vfs[idx].size - off) : (size_t)sz;
        xj380_mem_write(e, ba, e->vfs[idx].data + off, cp);
    }
}
static void h_WRITEFILE(xj380_emu_t *e) {
    uint64_t pa = r(e, UC_X86_REG_RDI), ba = r(e, UC_X86_REG_RSI);
    uint64_t sz = r(e, UC_X86_REG_RDX),  off = r(e, UC_X86_REG_RCX);
    char p[512]; xj380_mem_read_str(e, pa, p, sizeof(p));
    int idx = vfs_find(e, p);
    if (idx < 0) idx = vfs_create(e, p, false);
    if (idx >= 0) {
        vfs_entry_t *et = &e->vfs[idx];
        size_t ns = (size_t)(off + sz);
        if (ns > et->size) { et->data = realloc(et->data, ns); et->size = ns; }
        xj380_mem_read(e, ba, et->data + off, (size_t)sz);
    }
}
static void h_RMDIR(xj380_emu_t *e) { h_DELETEFILE(e); }
static void h_CHAR2INT(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[256];
    xj380_mem_read_str(e, a, b, sizeof(b)); w(e, UC_X86_REG_RAX, strtoull(b, NULL, 10));
}
static void h_INT2CHAR(xj380_emu_t *e) {
    uint64_t d = r(e, UC_X86_REG_RDI); char b[32];
    snprintf(b, sizeof(b), "%llu", (unsigned long long)d);
    uint64_t ra = emu_brk(e, strlen(b) + 1);
    wr_str(e, ra, b); w(e, UC_X86_REG_RAX, ra);
}
static void h_HEX2CHAR(xj380_emu_t *e) {
    uint64_t d = r(e, UC_X86_REG_RDI); char b[32];
    snprintf(b, sizeof(b), "%llx", (unsigned long long)d);
    uint64_t ra = emu_brk(e, strlen(b) + 1);
    wr_str(e, ra, b); w(e, UC_X86_REG_RAX, ra);
}
static void h_TORGB(xj380_emu_t *e) {
    uint8_t rv = (uint8_t)r(e, UC_X86_REG_RDI), gv = (uint8_t)r(e, UC_X86_REG_RSI),
            bv = (uint8_t)r(e, UC_X86_REG_RDX);
    w(e, UC_X86_REG_RAX, ((uint64_t)rv << 24) | ((uint64_t)gv << 16) | ((uint64_t)bv << 8) | 0xFFULL);
}
static void h_TORGBA(xj380_emu_t *e) {
    uint32_t argb = (uint32_t)r(e, UC_X86_REG_RDI);
    w(e, UC_X86_REG_RAX, ((argb & 0xFF000000)) | ((argb & 0x000000FF) << 16) |
                         ((argb & 0x0000FF00)) | ((argb & 0x00FF0000) >> 16));
}
static void h_FORK(xj380_emu_t *e) {
    /* 不支持 fork */
    w(e, UC_X86_REG_RAX, UINT64_MAX);
}
static void h_EXECVE(xj380_emu_t *e) {
    /* 不支持 execve */
    w(e, UC_X86_REG_RAX, UINT64_MAX);
}
static void h_EXIT(xj380_emu_t *e) {
    uint64_t v = r(e, UC_X86_REG_RDI);
    e->running = false;
    e->exit_code = (int)v;
    uc_emu_stop(e->uc);
}
static void h_GETTASKLIST(xj380_emu_t *e) {
    uint64_t ba = r(e, UC_X86_REG_RDI); uint64_t mc = r(e, UC_X86_REG_RSI);
    if (ba && mc > 0) {
        XapiTaskInfo ti = { .pid = 1, .name = "xj380-app", .state = 0 };
        xj380_mem_write(e, ba, &ti, sizeof(ti));
    }
    w(e, UC_X86_REG_RAX, 1);
}
static void h_KILLPROCESS(xj380_emu_t *e) { w(e, UC_X86_REG_RAX, 0); }
static void h_GETSYSTEMVERSION(xj380_emu_t *e) {
    wr_str(e, r(e, UC_X86_REG_RDI), "XJ380 OS Emulator v1.0");
}
static void h_GETTIME(xj380_emu_t *e) {
    w(e, UC_X86_REG_RAX, (uint64_t)((uint64_t)time(NULL) - 315532800ULL));
}
static void h_GETCURRENTUSER(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI);
    if (a) xj380_mem_write(e, a, &e->current_user, sizeof(UserInfo));
}
static void h_GETTIMEX(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI);
    if (a) {
        time_t n = time(NULL); struct tm *tm = localtime(&n);
        TimeType xt = { .year=(UINT32)(tm->tm_year+1900), .month=(UINT32)(tm->tm_mon+1),
            .day=(UINT32)tm->tm_mday, .hour=(UINT32)tm->tm_hour,
            .minute=(UINT32)tm->tm_min, .second=(UINT32)tm->tm_sec };
        xj380_mem_write(e, a, &xt, sizeof(xt));
    }
}
static void h_GETCPUMODEL(xj380_emu_t *e) {
    wr_str(e, r(e, UC_X86_REG_RDI), "Host CPU (Unicorn x86_64)");
}
static void h_GETMEMORYSIZE(xj380_emu_t *e) { w(e, UC_X86_REG_RAX, 4096); }
static void h_BROKEN(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    fprintf(stderr, "\n[崩溃] %s\n", xj380_mem_read_str(e, a, b, sizeof(b)) == 0 ? b : "(无信息)");
    e->running = false; uc_emu_stop(e->uc);
}
static void h_SENDAPPMESSAGE(xj380_emu_t *e) {
    uint64_t ta = r(e, UC_X86_REG_RDI), tx = r(e, UC_X86_REG_RSI);
    char t[256], x[1024];
    xj380_mem_read_str(e, ta, t, sizeof(t)); xj380_mem_read_str(e, tx, x, sizeof(x));
    printf("[%s] %s\n", t, x);
}
static void h_SLEEP(xj380_emu_t *e) {
    uint64_t ms = r(e, UC_X86_REG_RDI);
#ifdef XJ380_GUI
    /* GUI 模式下不能真睡: 必须边等边轮询 SDL2 事件 */
    for (uint64_t i = 0; i < ms; i++) {
        usleep(1000);
        int ev_ret = xj380_gui_poll_events(e);
        if (ev_ret != 0) {
            if (ev_ret == 2) {  /* 用户关闭窗口 */
                e->running = false;
            }
            uc_emu_stop(e->uc);
            break;
        }
        if (i % 16 == 0) xj380_gui_render_all(e);  /* ≈60fps */
        /* 注: flush_events 不能在 uc_emu_start 内调用 (嵌套) — 由 xj380_run 外层分发 */
    }
#else
    usleep((unsigned int)(ms * 1000));
#endif
}
static void h_RUN(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p));
    printf("[xj380] Run: %s (不支持)\n", p);
}
static void h_RUNARGS(xj380_emu_t *e) {
    /* xapi_RunArgs(WSTR path, WSTR argv[]) — 同 h_RUN, 忽略参数 */
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p));
    printf("[xj380] RunArgs: %s (不支持)\n", p);
}
static void h_SETMSGPRCOR(xj380_emu_t *e) {
    uint64_t handle = r(e, UC_X86_REG_RDI);
    uint64_t func   = r(e, UC_X86_REG_RSI);
#ifdef XJ380_GUI
    xj380_gui_store_callback(e, handle, func);
#else
    (void)handle; (void)func;
#endif
}
static void h_FLUSHTIME(xj380_emu_t *e) { (void)e; }
static void h_ALLOCATEMEMORY(xj380_emu_t *e) {
    w(e, UC_X86_REG_RAX, emu_brk(e, (size_t)r(e, UC_X86_REG_RDI)));
}
static void h_FREEMEMORY(xj380_emu_t *e) { (void)e; /* 无法部分 unmap */ }
/* SYS_BRK handler: Linux brk() 语义 */
static void h_SYSBRK(xj380_emu_t *e) {
    uint64_t new_brk = r(e, UC_X86_REG_RDI);  /* arg1 = new brk address */
    if (new_brk == 0) {
        /* brk(0): 返回当前 brk */
        w(e, UC_X86_REG_RAX, e->brk_addr);
        return;
    }
    if (new_brk > e->brk_addr) {
        /* 扩展堆: 映射新页 (堆已预映射, 失败也无害) */
        uint64_t need = (new_brk - e->brk_addr + 0xFFFULL) & ~0xFFFULL;
        (void)uc_mem_map(e->uc, e->brk_addr, need, UC_PROT_READ | UC_PROT_WRITE);
        e->brk_addr = new_brk;
    } else if (new_brk < e->brk_addr) {
        /* 收缩堆: 允许但不实际释放 */
        e->brk_addr = new_brk;
    }
    w(e, UC_X86_REG_RAX, e->brk_addr);
}

static void h_MAPMEMORY(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI), sz = r(e, UC_X86_REG_RSI);
    uint32_t fl = (uint32_t)r(e, UC_X86_REG_RDX);
    int prot = 0;
    if (fl & 1) prot |= UC_PROT_READ;
    if (fl & 2) prot |= UC_PROT_WRITE;
    if (!(fl & 64)) prot |= UC_PROT_EXEC;
    sz = (sz + 0xFFF) & ~0xFFFULL;
    if (!a) { a = e->brk_addr; e->brk_addr += sz; }
    (void)uc_mem_map(e->uc, a, sz, (uint32_t)prot);
    w(e, UC_X86_REG_RAX, a);
}

/* handler 表 */
#ifdef XJ380_GUI
/* ---- GUI handler stubs (调用 SDL2 后端) ---- */

#define GETH  r(e, UC_X86_REG_RDI)  /* handle = arg1 (RDI) */

static void gh_CREATEWINDOW(xj380_emu_t *e) {
    xj380_gui_create_window(e, r(e,UC_X86_REG_RDI), r(e,UC_X86_REG_RSI));
}
static void gh_SETWINDOWTITLE(xj380_emu_t *e) {
    xj380_gui_set_window_title(e, r(e,UC_X86_REG_RDI), r(e,UC_X86_REG_RSI));
}
static void gh_CLOSEWINDOW(xj380_emu_t *e) {
    xj380_gui_close_window(e, r(e,UC_X86_REG_RDI));
}
static void gh_SETICON(xj380_emu_t *e) {
    xj380_gui_set_icon(e, r(e,UC_X86_REG_RDI), r(e,UC_X86_REG_RSI));
}
static void gh_GETWINDOWSIZE(xj380_emu_t *e) {
    xj380_gui_get_window_size(e, r(e,UC_X86_REG_RDI),
                              r(e,UC_X86_REG_RSI), r(e,UC_X86_REG_RDX));
}
static void gh_DRAWPOINT(xj380_emu_t *e) {
    xj380_gui_draw_point(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX));
}
static void gh_DRAWLINE(xj380_emu_t *e) {
    xj380_gui_draw_line(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), (uint32_t)r(e,UC_X86_REG_R9));
}
static void gh_DRAWRECT(xj380_emu_t *e) {
    uint64_t fill = r(e, UC_X86_REG_R9);
    xj380_gui_draw_rect(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), (uint32_t)r(e,UC_X86_REG_R9), (int)fill);
}
static void gh_DRAWTEXT(xj380_emu_t *e) {
    xj380_gui_draw_text(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), (uint32_t)r(e,UC_X86_REG_R9));
}
static void gh_DRAWTEXTL(xj380_emu_t *e) {
    /* 7 参数: 前6个在寄存器, width_ptr 在栈上 [RSP+8] (call 压入了返回地址) */
    uint64_t rsp = r(e, UC_X86_REG_RSP);
    uint64_t width_ptr = 0;
    xj380_mem_read(e, rsp + 8, &width_ptr, 8);
    xj380_gui_draw_text_l(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), (uint32_t)r(e,UC_X86_REG_R9), width_ptr);
}
static void gh_DRAWSWTEXT(xj380_emu_t *e) {
    xj380_gui_draw_sw_text(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8));
}
static void gh_CALCTEXTWIDTH(xj380_emu_t *e) {
    xj380_gui_calc_text_width(e, r(e,UC_X86_REG_RDI),
        (uint32_t)r(e,UC_X86_REG_RSI), r(e,UC_X86_REG_RDX));
}
static void gh_DRAWBMP(xj380_emu_t *e) {
    xj380_gui_draw_bmp(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_DRAWPNG(xj380_emu_t *e) {
    xj380_gui_draw_png(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_DRAWPICTURE(xj380_emu_t *e) {
    xj380_gui_draw_picture(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_GETPICSIZE(xj380_emu_t *e) {
    xj380_gui_get_pic_size(e, r(e,UC_X86_REG_RDI),
        r(e,UC_X86_REG_RSI), r(e,UC_X86_REG_RDX));
}
static void gh_READBUFFER(xj380_emu_t *e) {
    xj380_gui_read_buffer(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_WRITEBUFFER(xj380_emu_t *e) {
    xj380_gui_write_buffer(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_READBUFFERA(xj380_emu_t *e) {
    xj380_gui_read_buffer_a(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_WRITEBUFFERA(xj380_emu_t *e) {
    xj380_gui_write_buffer_a(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8), r(e,UC_X86_REG_R9));
}
static void gh_REFRESHWINDOW(xj380_emu_t *e) {
    xj380_gui_refresh_window(e, GETH);
}
static void gh_REFRESHPART(xj380_emu_t *e) {
    xj380_gui_refresh_part(e, GETH, (uint32_t)r(e,UC_X86_REG_RSI),
        (uint32_t)r(e,UC_X86_REG_RDX), (uint32_t)r(e,UC_X86_REG_RCX),
        (uint32_t)r(e,UC_X86_REG_R8));
}
static void gh_BUTTON(xj380_emu_t *e) {
    xj380_gui_button(e, GETH, r(e,UC_X86_REG_RSI), r(e,UC_X86_REG_RDX),
                     r(e,UC_X86_REG_RCX), r(e,UC_X86_REG_R8));
}
static void gh_EMPBUTTON(xj380_emu_t *e) {
    xj380_gui_emp_button(e, GETH, r(e,UC_X86_REG_RSI), r(e,UC_X86_REG_RDX),
                         r(e,UC_X86_REG_RCX), r(e,UC_X86_REG_R8));
}
static void gh_DELETEBUTTON(xj380_emu_t *e) {
    xj380_gui_delete_button(e, GETH, r(e,UC_X86_REG_RSI));
}
static void gh_REGRBUTTON(xj380_emu_t *e) {
    xj380_gui_register_rbutton_menu(e, GETH, r(e,UC_X86_REG_RSI),
                                    r(e,UC_X86_REG_RDX));
}
static void gh_DELRBUTTON(xj380_emu_t *e) {
    xj380_gui_delete_rbutton_menu(e, GETH);
}
static void gh_DRAWSVG(xj380_emu_t *e) {
    uint64_t h     = GETH;
    uint32_t x     = (uint32_t)r(e, UC_X86_REG_RSI);
    uint32_t y     = (uint32_t)r(e, UC_X86_REG_RDX);
    uint32_t w     = (uint32_t)r(e, UC_X86_REG_RCX);
    uint64_t svg   = r(e, UC_X86_REG_R8);
    int      trans = (int)r(e, UC_X86_REG_R9);
    xj380_gui_draw_svg(e, h, x, y, w, svg, trans);
}
static void gh_DRAWFA(xj380_emu_t *e) {
    uint64_t h     = GETH;
    uint32_t x     = (uint32_t)r(e, UC_X86_REG_RSI);
    uint32_t y     = (uint32_t)r(e, UC_X86_REG_RDX);
    uint32_t w     = (uint32_t)r(e, UC_X86_REG_RCX);
    uint64_t name  = r(e, UC_X86_REG_R8);
    int      trans = (int)r(e, UC_X86_REG_R9);
    xj380_gui_draw_fa(e, h, x, y, w, name, trans);
}
#undef GETH
#endif /* XJ380_GUI */

static const xapi_fn handlers[XAPI_COUNT] = {
    [XAPI_OUTPUT]=h_OUTPUT,[XAPI_INPUT]=h_INPUT,[XAPI_GETLINE]=h_GETLINE,
    [XAPI_GETCH]=h_GETCH,[XAPI_ENDLINE]=h_ENDLINE,[XAPI_PRINTLINE]=h_PRINTLINE,
    [XAPI_PRINTF]=h_PRINTF,[XAPI_OUTPUTSERIAL]=h_OUTPUTSERIAL,
    [XAPI_OPENFILE]=h_OPENFILE,[XAPI_CLOSEFILE]=h_CLOSEFILE,
    [XAPI_SEARCHFILE]=h_SEARCHFILE,[XAPI_MKDIR]=h_MKDIR,
    [XAPI_CREATEFILE]=h_CREATEFILE,[XAPI_DELETEFILE]=h_DELETEFILE,
    [XAPI_RENAMEFILE]=h_RENAMEFILE,[XAPI_READFILE]=h_READFILE,
    [XAPI_WRITEFILE]=h_WRITEFILE,[XAPI_RMDIR]=h_RMDIR,
    [XAPI_CHAR2INT]=h_CHAR2INT,[XAPI_INT2CHAR]=h_INT2CHAR,
    [XAPI_HEX2CHAR]=h_HEX2CHAR,[XAPI_TORGB]=h_TORGB,[XAPI_TORGBA]=h_TORGBA,
    [XAPI_FORK]=h_FORK,[XAPI_EXECVE]=h_EXECVE,[XAPI_EXIT]=h_EXIT,
    [XAPI_GETTASKLIST]=h_GETTASKLIST,[XAPI_KILLPROCESS]=h_KILLPROCESS,
    [XAPI_GETSYSTEMVERSION]=h_GETSYSTEMVERSION,[XAPI_GETTIME]=h_GETTIME,
    [XAPI_GETCURRENTUSER]=h_GETCURRENTUSER,[XAPI_GETTIMEX]=h_GETTIMEX,
    [XAPI_GETCPUMODEL]=h_GETCPUMODEL,[XAPI_GETMEMORYSIZE]=h_GETMEMORYSIZE,
    [XAPI_BROKEN]=h_BROKEN,[XAPI_SENDAPPMESSAGE]=h_SENDAPPMESSAGE,
    [XAPI_SLEEP]=h_SLEEP,[XAPI_RUN]=h_RUN,[XAPI_RUNARGS]=h_RUNARGS,
    [XAPI_FLUSHTIME]=h_FLUSHTIME,[XAPI_SETMSGPRCOR]=h_SETMSGPRCOR,
    [XAPI_ALLOCATEMEMORY]=h_ALLOCATEMEMORY,[XAPI_FREEMEMORY]=h_FREEMEMORY,
    [XAPI_MAPMEMORY]=h_MAPMEMORY,
#ifdef XJ380_GUI
    [XAPI_CREATEWINDOW]=gh_CREATEWINDOW,
    [XAPI_SETWINDOWTITLE]=gh_SETWINDOWTITLE,
    [XAPI_CLOSEWINDOW]=gh_CLOSEWINDOW,
    [XAPI_SETICON]=gh_SETICON,
    [XAPI_GETWINDOWSIZE]=gh_GETWINDOWSIZE,
    [XAPI_DRAWPOINT]=gh_DRAWPOINT,
    [XAPI_DRAWLINE]=gh_DRAWLINE,
    [XAPI_DRAWRECT]=gh_DRAWRECT,
    [XAPI_DRAWTEXT]=gh_DRAWTEXT,
    [XAPI_DRAWTEXTL]=gh_DRAWTEXTL,
    [XAPI_DRAWSWTEXT]=gh_DRAWSWTEXT,
    [XAPI_CALCTEXTWIDTH]=gh_CALCTEXTWIDTH,
    [XAPI_DRAWSVG]=gh_DRAWSVG,
    [XAPI_DRAWFA]=gh_DRAWFA,
    [XAPI_DRAWBMP]=gh_DRAWBMP,
    [XAPI_DRAWPNG]=gh_DRAWPNG,
    [XAPI_DRAWPICTURE]=gh_DRAWPICTURE,
    [XAPI_GETPICSIZE]=gh_GETPICSIZE,
    [XAPI_READBUFFER]=gh_READBUFFER,
    [XAPI_WRITEBUFFER]=gh_WRITEBUFFER,
    [XAPI_READBUFFERA]=gh_READBUFFERA,
    [XAPI_WRITEBUFFERA]=gh_WRITEBUFFERA,
    [XAPI_REFRESHWINDOW]=gh_REFRESHWINDOW,
    [XAPI_REFRESHPARTWINDOW]=gh_REFRESHPART,
    [XAPI_BUTTON]=gh_BUTTON,
    [XAPI_EMPBUTTON]=gh_EMPBUTTON,
    [XAPI_DELETEBUTTON]=gh_DELETEBUTTON,
    [XAPI_REGISTERRIGHTBUTTONMENU]=gh_REGRBUTTON,
    [XAPI_DELETERIGHTBUTTONMENU]=gh_DELRBUTTON,
#endif
};

static int dispatch_xapi(xj380_emu_t *emu, int no)
{
    if (no < 0 || no >= XAPI_COUNT || !handlers[no]) {
        fprintf(stderr, "[xj380] 未实现: syscall #%d\n", no);
        return -1;
    }
    handlers[no](emu);
    return 0;
}

/* ================================================================
 * 公共 API
 * ================================================================ */

xj380_emu_t* xj380_create(void)
{
    xj380_emu_t *emu = calloc(1, sizeof(*emu));
    if (!emu) return NULL;

    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &emu->uc);
    if (err != UC_ERR_OK) {
        snprintf(emu->error, sizeof(emu->error), "uc_open: %s", uc_strerror(err));
        free(emu); return NULL;
    }

    /* Admin 用户 */
    emu->current_user = (UserInfo){
        .user_type = 2, .perm_r = true, .perm_w = true,
        .perm_cs = true, .perm_nv = true,
    };

    /* VFS 根 */
    emu->vfs_capacity = 64;
    emu->vfs = calloc((size_t)emu->vfs_capacity, sizeof(vfs_entry_t));
    emu->vfs_count = 1;
    strcpy(emu->vfs[0].path, "/");
    emu->vfs[0].is_dir = true;

    /* trampoline 范围 hook: 截获所有 xapi 调用 */
    uc_hook h_tramp;
    (void)uc_hook_add(emu->uc, &h_tramp, UC_HOOK_CODE,
                      (void*)tramp_hook, emu,
                      XJ380_TRAMP_BASE, XJ380_TRAMP_BASE + XJ380_TRAMP_SIZE);

    /* 备用 syscall hook */
    uc_hook h_sys;
    (void)uc_hook_add(emu->uc, &h_sys, UC_HOOK_INTR,
                      (void*)on_syscall, emu,
                      UC_X86_INS_SYSCALL, (uint64_t)-1, (uint64_t)0);

#ifdef DEBUG_TRACE
    /* 调试：全范围指令 trace */
    uc_hook h_tr;
    (void)uc_hook_add(emu->uc, &h_tr, UC_HOOK_CODE,
                      (void*)debug_code_hook, emu, (uint64_t)1, (uint64_t)0);
#else
    (void)emu;
#endif

    return emu;
}

void xj380_destroy(xj380_emu_t *emu)
{
    if (!emu) return;
    if (emu->uc) uc_close(emu->uc);
    if (emu->vfs) {
        for (int i = 0; i < emu->vfs_count; i++) free(emu->vfs[i].data);
        free(emu->vfs);
    }
    free(emu);
}

int xj380_run(xj380_emu_t *emu, int argc, char **argv)
{
    (void)argc; (void)argv;
    w(emu, UC_X86_REG_RIP, emu->entry_point);
    w(emu, UC_X86_REG_EFLAGS, 0x202);
    emu->running = true;

    printf("[xj380] ▶ 开始执行 @ 0x%llx\n", (unsigned long long)emu->entry_point);

#ifdef XJ380_GUI
    /* GUI 模式: 时间分片执行，每 50000 条指令切出来处理事件 */
    while (emu->running) {
        uc_err err = uc_emu_start(emu->uc, emu->entry_point,
                                  0xFFFFFFFFFFFFFFFFULL, 0, 50000);
        if (err == UC_ERR_OK) {
            int ev = xj380_gui_poll_events(emu);
            if (ev == 2) { emu->running = false; break; }
            xj380_gui_render_all(emu);
            xj380_gui_flush_events(emu);
        } else if (err == UC_ERR_EXCEPTION) {
            snprintf(emu->error, sizeof(emu->error), "uc_emu_start: %s", uc_strerror(err));
            return -1;
        }
        emu->entry_point = r(emu, UC_X86_REG_RIP);
    }
    return emu->exit_code;
#else
    uc_err err = uc_emu_start(emu->uc, emu->entry_point,
                              0xFFFFFFFFFFFFFFFFULL, 0, 0);
    if (err != UC_ERR_OK && emu->running) {
        snprintf(emu->error, sizeof(emu->error), "uc_emu_start: %s", uc_strerror(err));
        return -1;
    }
    return emu->exit_code;
#endif
}

const char* xj380_strerror(xj380_emu_t *emu) { return emu->error; }
void xj380_set_render_callback(xj380_emu_t *emu, xj380_render_cb cb) { emu->render_cb = cb; }
struct uc_struct* xj380_get_uc(xj380_emu_t *emu) { return emu->uc; }
