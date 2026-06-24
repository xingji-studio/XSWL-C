#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

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
#include <limits.h>
#include <stdarg.h>

/* ================================================================
 * 常量
 * ================================================================ */

#define TRAMP_SLOT_SIZE   16      /* 每条 trampoline 16 字节 */
#define MAX_OPEN_FILES    256
#define VFS_PATH_MAX      512
#define MAX_WINDOWS       16
#define VFS_ALLOC_SIZE    0x1000000ULL
#define MAX_ELF_PHDRS    128
#define MAX_ELF_SEGMENTS 64
#define MAX_ELF_LOAD_SIZE 0x40000000ULL

#define XJ380_SYS_ARCH_PRCTL 158ULL
#define XJ380_ARCH_SET_FS    0x1002ULL
#define XJ380_ARCH_GET_FS    0x1003ULL
#define XJ380_ARCH_SET_GS    0x1004ULL
#define XJ380_ARCH_GET_GS    0x1005ULL

#ifndef PATH_MAX
#define PATH_MAX          4096
#endif

/* ================================================================
 * 虚拟文件条目
 * ================================================================ */

typedef struct {
    char    path[VFS_PATH_MAX];
    uint8_t *data;       /* 宿主内存 (源数据) */
    size_t  size;
    bool    is_dir;
    uint64_t emu_addr;   /* 模拟器内存缓存 (首次Open时分配) */
    bool    emu_cached;  /* 是否已缓存到模拟器内存 */
} vfs_entry_t;

/* ================================================================
 * 模拟器实例
 * ================================================================ */

struct xj380_emu {
    uc_engine     *uc;
    uint64_t       entry_point;
    uint64_t       brk_addr;
    uint64_t       heap_map_end;
    uint64_t       vfs_alloc_addr;
    uint64_t       vfs_map_end;

    /* 错误 */
    char           error[256];
    char           app_dir[PATH_MAX];

    /* 运行状态 */
    bool           running;
    bool           debug_enabled;
    int            exit_code;
    uint64_t       arg7_value;
    bool           arg7_valid;

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
static void h_ARCHPRCTL(xj380_emu_t *e);
static void h_CLOCKGETTIME(xj380_emu_t *e);
static void h_WRITE(xj380_emu_t *e);
static void h_OPEN(xj380_emu_t *e);

/* ================================================================
 * 辅助：寄存器读写
 * ================================================================ */

static inline uint64_t page_align_up(uint64_t value)
{
    return (value + 0xFFFULL) & ~0xFFFULL;
}

static inline uint64_t align_up_u64(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static inline uint64_t r(xj380_emu_t *emu, int reg)
{
    uint64_t v = 0;

    (void)uc_reg_read(emu->uc, reg, &v);
    return v;
}

static inline void w(xj380_emu_t *emu, int reg, uint64_t val)
{
    (void)uc_reg_write(emu->uc, reg, &val);
}


bool xj380_debug_enabled(const xj380_emu_t *emu)
{
    return !emu || emu->debug_enabled;
}

void xj380_log(xj380_emu_t *emu, const char *fmt, ...)
{
    if (!xj380_debug_enabled(emu))
    {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

typedef struct
{
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
}
guest_regs_t;

static void save_guest_regs(xj380_emu_t *emu, guest_regs_t *regs)
{
    regs->rdi = r(emu, UC_X86_REG_RDI);
    regs->rsi = r(emu, UC_X86_REG_RSI);
    regs->rdx = r(emu, UC_X86_REG_RDX);
    regs->rcx = r(emu, UC_X86_REG_RCX);
    regs->r8  = r(emu, UC_X86_REG_R8);
    regs->r9  = r(emu, UC_X86_REG_R9);
    regs->r10 = r(emu, UC_X86_REG_R10);
    regs->r11 = r(emu, UC_X86_REG_R11);
}

static void restore_guest_regs(xj380_emu_t *emu, const guest_regs_t *regs)
{
    w(emu, UC_X86_REG_RDI, regs->rdi);
    w(emu, UC_X86_REG_RSI, regs->rsi);
    w(emu, UC_X86_REG_RDX, regs->rdx);
    w(emu, UC_X86_REG_RCX, regs->rcx);
    w(emu, UC_X86_REG_R8,  regs->r8);
    w(emu, UC_X86_REG_R9,  regs->r9);
    w(emu, UC_X86_REG_R10, regs->r10);
    w(emu, UC_X86_REG_R11, regs->r11);
}

/* ================================================================
 * 辅助：模拟器内存读写
 * ================================================================ */

int xj380_mem_read_str(xj380_emu_t *emu, uint64_t addr, char *buf, size_t max)
{
    size_t i = 0;

    if (!buf || max == 0)
    {
        return -1;
    }

    while (i < max - 1)
    {
        uint8_t c;

        if (uc_mem_read(emu->uc, addr + i, &c, 1) != UC_ERR_OK)
        {
            return -1;
        }

        buf[i] = (char)c;
        if (c == 0)
        {
            break;
        }

        i++;
    }

    buf[i] = 0;
    return 0;
}

static int wr_str(xj380_emu_t *emu, uint64_t addr, const char *str)
{
    return (int)uc_mem_write(emu->uc, addr,
        (const uint8_t *)str, strlen(str) + 1);
}

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
    {
        return;
    }

    snprintf(dst, dst_size, "%s", src ? src : "");
}

int xj380_mem_read(xj380_emu_t *emu, uint64_t addr, void *dst, size_t len)
{
    return (int)uc_mem_read(emu->uc, addr, (uint8_t *)dst, len);
}

int xj380_mem_write(xj380_emu_t *emu, uint64_t addr, const void *src, size_t len)
{
    return (int)uc_mem_write(emu->uc, addr, (const uint8_t *)src, len);
}

static int map_heap_until(xj380_emu_t *emu, uint64_t end_addr)
{
    uint64_t map_end = page_align_up(end_addr);

    if (map_end <= emu->heap_map_end)
    {
        return 0;
    }

    if (map_end < emu->heap_map_end || map_end > XJ380_HEAP_BASE + XJ380_HEAP_SIZE)
    {
        return -1;
    }

    uc_err err = uc_mem_map(
        emu->uc,
        emu->heap_map_end,
        map_end - emu->heap_map_end,
        UC_PROT_READ | UC_PROT_WRITE);
    if (err != UC_ERR_OK)
    {
        snprintf(emu->error, sizeof(emu->error),
            "heap map failed at 0x%llx: %s",
            (unsigned long long)emu->heap_map_end,
            uc_strerror(err));
        return -1;
    }

    emu->heap_map_end = map_end;
    return 0;
}

static uint64_t emu_brk(xj380_emu_t *emu, size_t size)
{
    uint64_t addr = align_up_u64(emu->brk_addr, 8);
    uint64_t end  = 0;

    if (size == 0)
    {
        return addr;
    }

    if ((uint64_t)size > UINT64_MAX - addr)
    {
        return 0;
    }

    end = align_up_u64(addr + (uint64_t)size, 8);
    if (map_heap_until(emu, end) != 0)
    {
        return 0;
    }

    emu->brk_addr = end;
    return addr;
}

/* ================================================================
 * 辅助：模拟器内部分配区
 * ================================================================ */

static int map_vfs_until(xj380_emu_t *emu, uint64_t end_addr)
{
    uint64_t map_end = page_align_up(end_addr);

    if (map_end <= emu->vfs_map_end)
    {
        return 0;
    }

    if (map_end < emu->vfs_map_end || map_end > XJ380_VFS_BASE + VFS_ALLOC_SIZE)
    {
        return -1;
    }

    uc_err err = uc_mem_map(
        emu->uc,
        emu->vfs_map_end,
        map_end - emu->vfs_map_end,
        UC_PROT_READ | UC_PROT_WRITE);
    if (err != UC_ERR_OK)
    {
        snprintf(emu->error, sizeof(emu->error),
            "vfs map failed at 0x%llx: %s",
            (unsigned long long)emu->vfs_map_end,
            uc_strerror(err));
        return -1;
    }

    emu->vfs_map_end = map_end;
    return 0;
}

static uint64_t emu_vfs_alloc(xj380_emu_t *emu, size_t size)
{
    uint64_t addr = align_up_u64(emu->vfs_alloc_addr, 8);
    uint64_t end  = 0;

    if (size == 0)
    {
        return addr;
    }

    if ((uint64_t)size > UINT64_MAX - addr)
    {
        return 0;
    }

    end = align_up_u64(addr + (uint64_t)size, 8);
    if (map_vfs_until(emu, end) != 0)
    {
        return 0;
    }

    emu->vfs_alloc_addr = end;
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

static int vfs_remove(xj380_emu_t *emu, const char *path)
{
    int idx = vfs_find(emu, path);
    if (idx < 0)
    {
        return -1;
    }

    free(emu->vfs[idx].data);
    if (idx + 1 < emu->vfs_count)
    {
        memmove(&emu->vfs[idx], &emu->vfs[idx + 1],
            (size_t)(emu->vfs_count - idx - 1) * sizeof(emu->vfs[0]));
    }
    emu->vfs_count--;
    return 0;
}

int xj380_vfs_read_file(xj380_emu_t *emu, const char *path, const uint8_t **data, size_t *size)
{
    int idx = -1;

    if (!emu || !path || !data || !size)
    {
        return -1;
    }

    idx = vfs_find(emu, path);
    if (idx < 0 && path[0] == '/')
    {
        idx = vfs_find(emu, path + 1);
    }

    if (idx < 0 || emu->vfs[idx].is_dir || !emu->vfs[idx].data)
    {
        return -1;
    }

    *data = emu->vfs[idx].data;
    *size = emu->vfs[idx].size;
    return 0;
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

static int try_host_path(char *host_path, size_t host_path_size,
    const char *prefix, const char *rel_path)
{
    int written = 0;

    if (!prefix || prefix[0] == 0 || strcmp(prefix, ".") == 0)
    {
        written = snprintf(host_path, host_path_size, "%s", rel_path);
    }
    else
    {
        written = snprintf(host_path, host_path_size, "%s/%s", prefix, rel_path);
    }

    if (written < 0 || (size_t)written >= host_path_size)
    {
        return -1;
    }

    return access(host_path, R_OK) == 0 ? 0 : -1;
}

static int vfs_resolve_host_path(xj380_emu_t *emu, const char *vpath,
    char *host_path, size_t host_path_size)
{
    const char *rel_path = (vpath && *vpath == '/') ? vpath + 1 : vpath;

    if (!rel_path || !host_path || host_path_size == 0 || strstr(rel_path, ".."))
    {
        return -1;
    }

    if (try_host_path(host_path, host_path_size, ".", rel_path) == 0
        || try_host_path(host_path, host_path_size, emu->app_dir, rel_path) == 0
        || try_host_path(host_path, host_path_size, "..", rel_path) == 0
        || try_host_path(host_path, host_path_size, "../XJ380 System", rel_path) == 0
        || try_host_path(host_path, host_path_size, "XJ380 System", rel_path) == 0)
    {
        return 0;
    }

    return -1;
}

static int vfs_import_host(xj380_emu_t *emu, const char *vpath, const char *hpath)
{
    if (vfs_ensure(emu) != 0) return -1;
    FILE *fp = fopen(hpath, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size_t sz = (fsz > 0) ? (size_t)fsz : 0;
    uint8_t *d = NULL;
    if (sz > 0) {
        d = malloc(sz);
        if (!d) { fclose(fp); return -1; }
        if (fread(d, 1, sz, fp) != sz) {
            free(d);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    int idx = emu->vfs_count++;
    copy_cstr(emu->vfs[idx].path, sizeof(emu->vfs[idx].path), vpath);
#ifdef XJ380_GUI
    xj380_gui_load_font(vpath, d, sz);
#endif
    emu->vfs[idx].data   = d;
    emu->vfs[idx].size   = sz;
    emu->vfs[idx].is_dir = false;
    emu->vfs[idx].emu_addr   = 0;
    emu->vfs[idx].emu_cached = false;
    return idx;
}

static int vfs_create(xj380_emu_t *emu, const char *path, bool is_dir) {
    if (vfs_find(emu, path) >= 0) return -1;
    if (vfs_ensure(emu) != 0) return -1;
    int idx = emu->vfs_count++;
    copy_cstr(emu->vfs[idx].path, sizeof(emu->vfs[idx].path), path);
    emu->vfs[idx].data   = NULL;
    emu->vfs[idx].size   = 0;
    emu->vfs[idx].is_dir = is_dir;
    emu->vfs[idx].emu_addr   = 0;
    emu->vfs[idx].emu_cached = false;
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
    if (!fp) { snprintf(emu->error, sizeof(emu->error), "open failed: %s", path); return -1; }

    Elf64_Ehdr eh;
    if (fread(&eh, sizeof(eh), 1, fp) != 1) {
        snprintf(emu->error, sizeof(emu->error), "failed to read ELF header"); fclose(fp); return -1;
    }

    if (memcmp(eh.e_ident, "\x7F" "ELF", 4) || eh.e_ident[4] != 2) {
        snprintf(emu->error, sizeof(emu->error), "not a 64-bit ELF"); fclose(fp); return -1;
    }
    if (eh.e_machine != 0x3E) {
        snprintf(emu->error, sizeof(emu->error), "not x86_64"); fclose(fp); return -1;
    }
    if (eh.e_phentsize != sizeof(Elf64_Phdr) || eh.e_phnum == 0 || eh.e_phnum > MAX_ELF_PHDRS) {
        snprintf(emu->error, sizeof(emu->error), "invalid ELF program header count or size: %u", eh.e_phnum);
        fclose(fp);
        return -1;
    }
    if (eh.e_phoff > (uint64_t)LONG_MAX) {
        snprintf(emu->error, sizeof(emu->error), "ELF program header offset is too large");
        fclose(fp);
        return -1;
    }

    emu->entry_point = eh.e_entry ? eh.e_entry : XJ380_TEXT_BASE;

    const char *slash = strrchr(path, '/');
    if (slash)
    {
        size_t dir_len = (size_t)(slash - path);
        if (dir_len >= sizeof(emu->app_dir))
        {
            dir_len = sizeof(emu->app_dir) - 1U;
        }
        memcpy(emu->app_dir, path, dir_len);
        emu->app_dir[dir_len] = 0;
    }
    else
    {
        copy_cstr(emu->app_dir, sizeof(emu->app_dir), ".");
    }

    /* 映射固定区域（堆改为按需映射, 不再预分配 16MB） */
    uc_mem_map(emu->uc, XJ380_STACK_BASE, XJ380_STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(emu->uc, XJ380_TRAMP_BASE, XJ380_TRAMP_SIZE, UC_PROT_READ | UC_PROT_EXEC);
    uc_mem_map(emu->uc, 0x70000000, 0x1000, UC_PROT_READ | UC_PROT_EXEC);  /* GUI 事件返回 */
    uc_mem_map(emu->uc, 0x70001000, 0x1000, UC_PROT_READ | UC_PROT_WRITE); /* GUI 事件栈 */
    emu->brk_addr     = XJ380_HEAP_BASE;
    emu->heap_map_end = XJ380_HEAP_BASE;
    emu->vfs_alloc_addr = XJ380_VFS_BASE;
    emu->vfs_map_end    = XJ380_VFS_BASE;

    /* 先读取所有程序头（不要在处理段数据时 fseek 破坏文件指针） */
    Elf64_Phdr *phdrs = calloc((size_t)eh.e_phnum, sizeof(Elf64_Phdr));
    if (!phdrs) {
        snprintf(emu->error, sizeof(emu->error), "failed to allocate program headers");
        fclose(fp);
        return -1;
    }
    if (fseek(fp, (long)eh.e_phoff, SEEK_SET) != 0) {
        free(phdrs);
        snprintf(emu->error, sizeof(emu->error), "failed to seek program headers");
        fclose(fp);
        return -1;
    }
    for (int i = 0; i < eh.e_phnum; i++) {
        if (fread(&phdrs[i], sizeof(Elf64_Phdr), 1, fp) != 1) {
            free(phdrs);
            snprintf(emu->error, sizeof(emu->error), "failed to read program header %d", i);
            fclose(fp);
            return -1;
        }
    }

    /* 先合并重叠的 LOAD 段，再统一映射 (修复 Windows 下相邻段重叠问题) */
    {
        typedef struct { uint64_t s, e; int p; } seg_t;
        seg_t segs[MAX_ELF_SEGMENTS]; int sn = 0;

        for (int i = 0; i < eh.e_phnum; i++) {
            Elf64_Phdr ph = phdrs[i];
            if (ph.p_type != PT_LOAD) continue;
            if (ph.p_memsz == 0) continue;
            if (ph.p_filesz > ph.p_memsz || ph.p_memsz > MAX_ELF_LOAD_SIZE
                || ph.p_vaddr > UINT64_MAX - ph.p_memsz - 0xFFFULL) {
                free(phdrs);
                snprintf(emu->error, sizeof(emu->error), "invalid ELF LOAD segment size");
                fclose(fp);
                return -1;
            }
            uint64_t s = ph.p_vaddr & ~0xFFFULL;
            uint64_t e = (ph.p_vaddr + ph.p_memsz + 0xFFFULL) & ~0xFFFULL;
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
            if (!merged) {
                if (sn >= MAX_ELF_SEGMENTS) {
                    free(phdrs);
                    snprintf(emu->error, sizeof(emu->error), "too many ELF LOAD segments");
                    fclose(fp);
                    return -1;
                }
                segs[sn++] = (seg_t){s, e, p};
            }
        }

        for (int i = sn - 1; i >= 0; i--) {
            uint64_t sz = segs[i].e - segs[i].s;
            xj380_log(emu, "[ELF] LOAD [0x%llx-0x%llx] flags=%c%c%c\n",
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

        if (ph.p_memsz == 0) continue;
        if (ph.p_filesz > ph.p_memsz || ph.p_memsz > MAX_ELF_LOAD_SIZE
            || ph.p_vaddr > UINT64_MAX - ph.p_memsz
            || ph.p_offset > (uint64_t)LONG_MAX) {
            free(phdrs);
            snprintf(emu->error, sizeof(emu->error), "invalid ELF LOAD segment data");
            fclose(fp);
            return -1;
        }

        if (ph.p_filesz > 0) {
            uint8_t *buf = malloc((size_t)ph.p_filesz);
            if (!buf) {
                free(phdrs);
                snprintf(emu->error, sizeof(emu->error), "failed to allocate ELF segment buffer");
                fclose(fp);
                return -1;
            }
            if (fseek(fp, (long)ph.p_offset, SEEK_SET) != 0
                || fread(buf, 1, (size_t)ph.p_filesz, fp) != (size_t)ph.p_filesz
                || xj380_mem_write(emu, ph.p_vaddr, buf, (size_t)ph.p_filesz) != 0) {
                free(buf);
                free(phdrs);
                snprintf(emu->error, sizeof(emu->error), "failed to read ELF segment");
                fclose(fp);
                return -1;
            }
            free(buf);
        }
        if (ph.p_memsz > ph.p_filesz) {
            uint64_t zero_size = ph.p_memsz - ph.p_filesz;
            uint8_t *z = calloc(1, (size_t)zero_size);
            if (!z || xj380_mem_write(emu, ph.p_vaddr + ph.p_filesz, z, (size_t)zero_size) != 0) {
                free(z);
                free(phdrs);
                snprintf(emu->error, sizeof(emu->error), "failed to zero ELF BSS");
                fclose(fp);
                return -1;
            }
            free(z);
        }
    }
    /* 重新计算 max_vaddr */
    uint64_t max_vaddr = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (phdrs[i].p_memsz == 0
                || phdrs[i].p_vaddr > UINT64_MAX - phdrs[i].p_memsz - 0xFFFULL) {
                continue;
            }
            uint64_t e = (phdrs[i].p_vaddr + phdrs[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
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
        if (scan_size > 0x800000) {  /* 8MB 上限, 足够覆盖大型二进制 */
            xj380_log(emu, "[ELF] warning: segment 0x%llx exceeds scan limit (%llu > 8MB); "
                    "some syscalls may not be hooked\n",
                    (unsigned long long)ph.p_vaddr,
                    (unsigned long long)scan_size);
            scan_size = 0x800000;
        }
        if (scan_size < 2) continue;
        uint8_t *scan = malloc((size_t)scan_size);
        if (!scan) continue;

        if (uc_mem_read(emu->uc, ph.p_vaddr, scan, (size_t)scan_size) != UC_ERR_OK) {
            free(scan);
            continue;
        }
        uc_hook h_sys;
        for (size_t off = 0; off + 1 < (size_t)scan_size; off++) {
            if (scan[off] == 0x0F && scan[off + 1] == 0x05) {
                uint64_t saddr = ph.p_vaddr + off;
                _Pragma("GCC diagnostic push")
                _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
                (void)uc_hook_add(emu->uc, &h_sys, UC_HOOK_CODE,
                                  (void*)xj380_syscall_hook, emu,
                                  saddr, saddr + 1);
                _Pragma("GCC diagnostic pop")
                xj380_log(emu, "[ELF] found syscall @ 0x%llx; hook registered\n",
                       (unsigned long long)saddr);
            }
        }
        free(scan);
    }

    free(phdrs);
    fclose(fp);

    if (max_vaddr > emu->brk_addr)
    {
        emu->brk_addr     = page_align_up(max_vaddr);
        emu->heap_map_end = emu->brk_addr;
    }

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
        xj380_log(emu, "[xj380] xapi_%-20s @ 0x%llx (syscall #%d)\n",
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

    /* 补丁: 如果 CRT 中 main 地址为 0, 从 ELF 符号表读取并填入 */
    {
        FILE *fp2 = fopen(path, "rb");
        if (fp2) {
            Elf64_Ehdr eh2;
            uint64_t main_addr = 0;

            if (fread(&eh2, sizeof(eh2), 1, fp2) == 1) {
                /* 找 .symtab 和对应的 .strtab (通过 sh_link) */
                uint64_t sym_off = 0, sym_sz = 0, str_off = 0, str_sz = 0;
                uint32_t sym_link = 0;
                for (int i = 0; i < eh2.e_shnum; i++) {
                    fseek(fp2, (long)(eh2.e_shoff + (uint64_t)i * eh2.e_shentsize), SEEK_SET);
                    uint32_t sh_type = 0, sh_link = 0;
                    uint64_t sh_offset = 0, sh_size = 0;
                    fseek(fp2, 4, SEEK_CUR);              /* sh_name */
                    if (fread(&sh_type,   4, 1, fp2) != 1) break;
                    fseek(fp2, 16, SEEK_CUR);              /* flags(8)+addr(8) */
                    if (fread(&sh_offset, 8, 1, fp2) != 1) break;
                    if (fread(&sh_size,   8, 1, fp2) != 1) break;
                    if (fread(&sh_link,   4, 1, fp2) != 1) break;
                    if (sh_type == 2) { sym_off = sh_offset; sym_sz = sh_size; sym_link = sh_link; }
                    if (sh_type == 3 && (uint32_t)i == sym_link) { str_off = sh_offset; str_sz = sh_size; }
                }

                if (sym_off && sym_sz && str_off && str_sz) {
                    char *strtab = malloc((size_t)str_sz);
                    if (strtab) {
                        fseek(fp2, (long)str_off, SEEK_SET);
                        if (fread(strtab, 1, (size_t)str_sz, fp2) == str_sz) {
                            fseek(fp2, (long)sym_off, SEEK_SET);
                            for (uint64_t j = 0; j < sym_sz / 24; j++) { /* Elf64_Sym = 24 bytes */
                                uint32_t st_name = 0;
                                uint64_t st_value = 0;
                                fseek(fp2, (long)(sym_off + j * 24), SEEK_SET);
                                if (fread(&st_name,  4, 1, fp2) != 1) continue;
                                fseek(fp2, 4, SEEK_CUR);  /* st_info+st_other+st_shndx */
                                if (fread(&st_value, 8, 1, fp2) != 1) continue;
                                if (st_value > 0 && st_name < str_sz) {
                                    if (strcmp(strtab + st_name, "main") == 0 ||
                                        strncmp(strtab + st_name, "_Z4main", 7) == 0) {
                                        main_addr = st_value;
                                        break;
                                    }
                                }
                            }
                        }
                        free(strtab);
                    }
                }

                if (main_addr) {
                    uint64_t patch_off = 0x20e7d8;
                    uint8_t mov_rax[7];
                    xj380_mem_read(emu, patch_off, mov_rax, 7);
                    if (mov_rax[0] == 0x48 && mov_rax[1] == 0xC7 && mov_rax[2] == 0xC0) {
                        uint32_t existing = 0;
                        memcpy(&existing, mov_rax + 3, 4);
                        if (existing == 0) {
                            uint32_t addr32 = (uint32_t)main_addr;
                            xj380_mem_write(emu, patch_off + 3, &addr32, 4);
                            xj380_log(emu, "[ELF] patched main address: 0x%x\n", addr32);
                        }
                    }
                }
            }

            fclose(fp2);
        }
    }

    xj380_log(emu, "[xj380] ELF loaded, entry=0x%llx\n", (unsigned long long)emu->entry_point);
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
    {228, -2},                    /* SYS_CLOCK_GETTIME → 特殊处理 */

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
    guest_regs_t saved_regs;
    save_guest_regs(emu, &saved_regs);

    uint64_t xj380_no = r(emu, UC_X86_REG_RAX);
#ifdef DEBUG_TRACE
    xj380_log(emu, "  [HOOK] syscall @ 0x%llx RAX=0x%llx (%llu)\n",
            (unsigned long long)addr, (unsigned long long)xj380_no,
            (unsigned long long)xj380_no);
#endif

    /* Linux syscall ABI: arg4 在 R10, handler 期望在 RCX */
    uint64_t r10_val = r(emu, UC_X86_REG_R10);
    w(emu, UC_X86_REG_RCX, r10_val);

    /*
     * enter_syscall 已经把原始第 7 参数暂存在 R9。
     * 这里不能改 guest RSP, 否则会破坏真实调用者的栈帧。
     */
    emu->arg7_value = r(emu, UC_X86_REG_R9);
    emu->arg7_valid = true;

    bool dispatch_gui_event = false;

    /* 特殊 syscall: SYS_BRK (12) */
    if (xj380_no == 12) {
        h_SYSBRK(emu);
    } else if (xj380_no == XJ380_SYS_ARCH_PRCTL) {
        h_ARCHPRCTL(emu);
    } else if (xj380_no == 228) {
        h_CLOCKGETTIME(emu);
    } else if (xj380_no == 1) {
        h_WRITE(emu);
    } else if (xj380_no == 2 || xj380_no == 257) {
        h_OPEN(emu);   /* SYS_OPEN / SYS_OPENAT */
    } else if (xj380_no == 3) {
        w(emu, UC_X86_REG_RAX, 0);  /* SYS_CLOSE: no-op return 0 */
    } else {
        int internal_no = map_xj380_to_internal(xj380_no);
        if (internal_no >= 0 && internal_no < XAPI_COUNT && handlers[internal_no]) {
            handlers[internal_no](emu);
            dispatch_gui_event = (internal_no == XAPI_FLUSHTIME
                               || internal_no == XAPI_SLEEP);
        } else {
            fprintf(stderr, "[xj380] unknown XJ380 syscall: 0x%llx (%llu)\n",
                    (unsigned long long)xj380_no, (unsigned long long)xj380_no);
            w(emu, UC_X86_REG_RAX, UINT64_MAX);
        }
    }

    uint64_t result = r(emu, UC_X86_REG_RAX);

    emu->arg7_value = 0;
    emu->arg7_valid = false;

    restore_guest_regs(emu, &saved_regs);
    w(emu, UC_X86_REG_RAX, result);

    /* 跳过 syscall 指令 (2 字节), 返回 enter_syscall 的收尾代码。 */
    w(emu, UC_X86_REG_RIP, addr + 2);
#ifdef XJ380_GUI
    if (dispatch_gui_event) {
        (void)xj380_gui_dispatch_queued_event(emu, addr + 2);
    }
#endif
}

#ifdef DEBUG_TRACE
/* 调试：代码 hook，打印执行轨迹 */
static void debug_code_hook(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    (void)uc;
    (void)user_data;
    xj380_log((xj380_emu_t *)user_data, "  [TRACE] 0x%llx (%u bytes)\n", (unsigned long long)addr, size);
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

    /* 7-arg 兼容: SysV 调用下第 7 参数在返回地址之后。 */
    uint64_t my_sp = r(emu, UC_X86_REG_RSP);
    uint64_t stack_arg7 = 0;
    (void)uc_mem_read(uc, my_sp + 8, (uint8_t *)&stack_arg7, 8);
    emu->arg7_value = stack_arg7;
    emu->arg7_valid = true;

    dispatch_xapi(emu, syscall_no);

    emu->arg7_value = 0;
    emu->arg7_valid = false;

    /* 模拟 ret: 从栈顶弹返回地址 */
    uint64_t ret_addr = 0;
    uc_mem_read(uc, my_sp, (uint8_t*)&ret_addr, 8);
    my_sp += 8;
    w(emu, UC_X86_REG_RSP, my_sp);
    w(emu, UC_X86_REG_RIP, ret_addr);
#ifdef XJ380_GUI
    if (syscall_no == XAPI_FLUSHTIME || syscall_no == XAPI_SLEEP) {
        (void)xj380_gui_dispatch_queued_event(emu, ret_addr);
    }
#endif
}

static void on_syscall(uc_engine *uc, uint32_t intno, void *user_data)
{
    (void)uc;
    (void)intno;
    (void)user_data;
#ifdef DEBUG_TRACE
    xj380_log((xj380_emu_t *)user_data, "  [SYSCALL FALLBACK]\n");
#endif
}

static bool invalid_mem_hook(uc_engine *uc, uc_mem_type type, uint64_t address,
    int size, int64_t value, void *user_data)
{
    xj380_emu_t *emu = (xj380_emu_t *)user_data;
    uint64_t     rip = 0;
    uint64_t     rax = 0;
    uint64_t     rcx = 0;
    uint64_t     rdx = 0;
    uint64_t     rdi = 0;
    uint64_t     rsi = 0;
    uint64_t     rsp = 0;
    uint64_t     rbp = 0;
    const char  *kind = "UNKNOWN";

    (void)value;
    (void)uc_reg_read(uc, UC_X86_REG_RIP, &rip);
    (void)uc_reg_read(uc, UC_X86_REG_RAX, &rax);
    (void)uc_reg_read(uc, UC_X86_REG_RCX, &rcx);
    (void)uc_reg_read(uc, UC_X86_REG_RDX, &rdx);
    (void)uc_reg_read(uc, UC_X86_REG_RDI, &rdi);
    (void)uc_reg_read(uc, UC_X86_REG_RSI, &rsi);
    (void)uc_reg_read(uc, UC_X86_REG_RSP, &rsp);
    (void)uc_reg_read(uc, UC_X86_REG_RBP, &rbp);

    if (type == UC_MEM_READ_UNMAPPED)
    {
        kind = "READ_UNMAPPED";
    }
    else if (type == UC_MEM_WRITE_UNMAPPED)
    {
        kind = "WRITE_UNMAPPED";
    }
    else if (type == UC_MEM_FETCH_UNMAPPED)
    {
        kind = "FETCH_UNMAPPED";
    }

    fprintf(stderr,
        "[xj380] invalid memory: %s addr=0x%llx size=%d rip=0x%llx "
        "rax=0x%llx rcx=0x%llx rdx=0x%llx rdi=0x%llx rsi=0x%llx "
        "rsp=0x%llx rbp=0x%llx brk=0x%llx map_end=0x%llx\n",
        kind,
        (unsigned long long)address,
        size,
        (unsigned long long)rip,
        (unsigned long long)rax,
        (unsigned long long)rcx,
        (unsigned long long)rdx,
        (unsigned long long)rdi,
        (unsigned long long)rsi,
        (unsigned long long)rsp,
        (unsigned long long)rbp,
        (unsigned long long)emu->brk_addr,
        (unsigned long long)emu->heap_map_end);
    return false;
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
        /* 去除换行和首段空格后的内容 (API: 读取到第一个空格) */
        char *n = strchr(b, '\n'); if (n) *n = 0;
        char *s = strchr(b, ' ');  if (s) *s = 0;
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
                if (*p == 0) break;
                if (*p == 's') {
                    char ab[4096];
                    if (xj380_mem_read_str(e, a1, ab, sizeof(ab)) == 0) printf("%s", ab);
                } else if (*p == 'd') {
                    printf("%lld", (long long)(int64_t)a1);
                } else if (*p == 'u') {
                    printf("%llu", (unsigned long long)a1);
                } else if (*p == 'x') {
                    printf("%llx", (unsigned long long)a1);
                } else if (*p == 'c') {
                    putchar((char)a1);
                } else if (*p == 'l') {
                    p++;
                    if (*p == 'l') {
                        p++;
                        if (*p == 'd') printf("%lld", (long long)(int64_t)a1);
                        else if (*p == 'u') printf("%llu", (unsigned long long)a1);
                        else if (*p == 'x') printf("%llx", (unsigned long long)a1);
                        else { putchar('%'); putchar('l'); putchar('l'); putchar(*p); }
                    } else {
                        putchar('%'); putchar('l'); putchar(*p);
                    }
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
static void h_OPENFILE(xj380_emu_t *e)
{
    uint64_t a = r(e, UC_X86_REG_RDI);
    char     p[512];

    w(e, UC_X86_REG_RAX, 0);
    if (xj380_mem_read_str(e, a, p, sizeof(p)) != 0)
    {
        return;
    }

    xj380_log(e, "[xj380] OpenFile: %s\n", p);

    int idx = vfs_find(e, p);
    if (idx < 0)
    {
        char hp[PATH_MAX];

        idx = (vfs_resolve_host_path(e, p, hp, sizeof(hp)) == 0)
            ? vfs_import_host(e, p, hp)
            : -1;
        if (idx < 0)
        {
            /* 对于 /system/ 下的路径，只允许已知的目录结构 */
            if (strncmp(p, "/system/", 8) == 0)
            {
                /* 只允许 /system/font/ 和 /system/ 根下的文件 */
                if (strcmp(p, "/system") == 0
                    || strncmp(p, "/system/font", 12) == 0)
                {
                    idx = vfs_create(e, p, false);
                }
                else
                {
                    fprintf(stderr, "[xj380] OpenFile failed: '%s' does not exist\n", p);
                    return;
                }
            }
            else
            {
                fprintf(stderr, "[xj380] OpenFile failed: '%s' does not exist\n", p);
                return;
            }
        }

        if (idx < 0)
        {
            return;
        }
    }

    vfs_entry_t *ent = &e->vfs[idx];
    uint64_t     xf  = emu_vfs_alloc(e, 16);
    uint64_t     len = (uint64_t)ent->size;
    uint64_t     buf = 0;

    if (!xf)
    {
        return;
    }

    if (ent->size > 0 && ent->data)
    {
        if (!ent->emu_cached)
        {
            buf = emu_vfs_alloc(e, ent->size);
            if (!buf || xj380_mem_write(e, buf, ent->data, ent->size) != 0)
            {
                return;
            }

            ent->emu_addr   = buf;
            ent->emu_cached = true;
        }
        else
        {
            buf = ent->emu_addr;
        }
    }

    if (xj380_mem_write(e, xf, &len, 8) != 0
        || xj380_mem_write(e, xf + 8, &buf, 8) != 0)
    {
        return;
    }

    w(e, UC_X86_REG_RAX, xf);
}
static void h_CLOSEFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); if (!a) return;
    /* XFILE = { UINT64 length; void *buffer; } 共 16 字节 */
    uint64_t len = 0, buf_ptr = 0, zero = 0;
    xj380_mem_read(e, a,      &len,     8);
    xj380_mem_read(e, a + 8,  &buf_ptr, 8);
    xj380_log(e, "[xj380] CloseFile: len=%llu buf=0x%llx\n",
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
        /* 精确前缀匹配: 路径前缀必须后跟 '/' 或 '\0' */
        if (strncmp(e->vfs[i].path, path, pl) == 0) {
            char next = e->vfs[i].path[pl];
            if (next != '/' && next != '\0') continue;
            const char *nm = e->vfs[i].path + pl; if (*nm == '/') nm++;
            if (!*nm) continue;  /* 跳过目录本身 */
            copy_cstr(db[cnt].filename, sizeof(db[cnt].filename), nm);
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
    xj380_log(e, "[xj380] Mkdir: %s\n", p);
}
static void h_CREATEFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p)); vfs_create(e, p, false);
    xj380_log(e, "[xj380] CreateFile: %s\n", p);
}
static void h_DELETEFILE(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p));
    w(e, UC_X86_REG_RAX, vfs_remove(e, p) == 0 ? 0 : UINT64_MAX);
}
static void h_RENAMEFILE(xj380_emu_t *e) {
    uint64_t oa = r(e, UC_X86_REG_RDI), na = r(e, UC_X86_REG_RSI);
    char op[512], np[512];
    xj380_mem_read_str(e, oa, op, sizeof(op)); xj380_mem_read_str(e, na, np, sizeof(np));
    int idx = vfs_find(e, op);
    if (idx >= 0) copy_cstr(e->vfs[idx].path, sizeof(e->vfs[idx].path), np);
}
static void h_READFILE(xj380_emu_t *e) {
    uint64_t pa = r(e, UC_X86_REG_RDI), ba = r(e, UC_X86_REG_RSI);
    uint64_t sz = r(e, UC_X86_REG_RDX),  off = r(e, UC_X86_REG_RCX);
    w(e, UC_X86_REG_RAX, 0);
    if (!ba || !sz) return;
    char p[512];
    if (xj380_mem_read_str(e, pa, p, sizeof(p)) != 0) return;
    int idx = vfs_find(e, p);
    if (idx < 0 || !e->vfs[idx].data || off >= e->vfs[idx].size) {
        uint8_t zero = 0;
        (void)xj380_mem_write(e, ba, &zero, 1);
        return;
    }
    size_t avail = e->vfs[idx].size - (size_t)off;
    size_t cp = ((uint64_t)avail < sz) ? avail : (size_t)sz;
    if (xj380_mem_write(e, ba, e->vfs[idx].data + off, cp) != 0) return;
    if (cp < (size_t)sz) {
        size_t rest = (size_t)sz - cp;
        uint8_t *zeros = calloc(1, rest);
        if (zeros) {
            (void)xj380_mem_write(e, ba + cp, zeros, rest);
            free(zeros);
        }
    }
    w(e, UC_X86_REG_RAX, (uint64_t)cp);
}
static void h_WRITEFILE(xj380_emu_t *e) {
    uint64_t pa = r(e, UC_X86_REG_RDI), ba = r(e, UC_X86_REG_RSI);
    uint64_t sz = r(e, UC_X86_REG_RDX),  off = r(e, UC_X86_REG_RCX);
    w(e, UC_X86_REG_RAX, 0);
    /* 检查: 空缓冲/空大小/偏移溢出 */
    if (!ba || !sz) return;
    if (off > (uint64_t)SIZE_MAX || sz > (uint64_t)SIZE_MAX) return;
    if (off > (uint64_t)SIZE_MAX - sz) return;
    char p[512];
    if (xj380_mem_read_str(e, pa, p, sizeof(p)) != 0) return;
    int idx = vfs_find(e, p);
    if (idx < 0) idx = vfs_create(e, p, false);
    if (idx >= 0) {
        vfs_entry_t *et = &e->vfs[idx];
        size_t needed = (size_t)off + (size_t)sz;
        /* 检测需要的空间是否溢出 */
        if (needed < (size_t)off) return;
        size_t old_size = et->size;
        if (needed > et->size) {
            uint8_t *nd = realloc(et->data, needed);
            if (!nd) return;
            et->data = nd;
            if ((size_t)off > old_size) memset(et->data + old_size, 0, (size_t)off - old_size);
            et->size = needed;
        }
        if (xj380_mem_read(e, ba, et->data + off, (size_t)sz) != 0) return;
        et->emu_cached = false;  /* 内容变了, 缓存失效 */
        w(e, UC_X86_REG_RAX, sz);
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
    /* XJ380 ARGB (0xAARRGGBB) → XJ380 RGBA (0xRRGGBBAA):
     * 在 little-endian 内存中 ARGB 是 [B][G][R][A], RGBA 是 [R][G][B][A],
     * 即交换字节 0 和字节 2 */
    uint32_t argb = (uint32_t)r(e, UC_X86_REG_RDI);
    uint32_t rgba = (argb & 0xFF00FF00U)
                  | ((argb & 0x000000FFU) << 16)
                  | ((argb & 0x00FF0000U) >> 16);
    w(e, UC_X86_REG_RAX, rgba);
}
static void h_FORK(xj380_emu_t *e) {
    /* 单进程模拟器无法复制进程映像；父进程返回稳定的伪子进程 PID。 */
    w(e, UC_X86_REG_RAX, 2);
}
static void h_EXECVE(xj380_emu_t *e) {
    /* 不支持 execve */
    w(e, UC_X86_REG_RAX, UINT64_MAX);
}
static void h_EXIT(xj380_emu_t *e) {
    uint64_t v = r(e, UC_X86_REG_RDI);
    e->running  = false;
    e->exit_code = (int)v;
    (void)uc_emu_stop(e->uc);
}
static void h_GETTASKLIST(xj380_emu_t *e) {
    uint64_t ba = r(e, UC_X86_REG_RDI); uint64_t mc = r(e, UC_X86_REG_RSI);
    if (ba && mc > 0) {
        XapiTaskInfo ti;
        memset(&ti, 0, sizeof(ti));
        ti.pid  = 1;
        ti.ppid = 0;
        ti.process_status = 0;  /* running */
        copy_cstr(ti.process_name, sizeof(ti.process_name), "xswl-app");
        copy_cstr(ti.thread_name,  sizeof(ti.thread_name),  "main");
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
    if (a) {
        UserInfo ui;
        memset(&ui, 0, sizeof(ui));
        copy_cstr(ui.name, sizeof(ui.name), "Admin");
        ui.user_type = 2;
        xj380_mem_write(e, a, &ui, sizeof(ui));
    }
}
static void h_GETTIMEX(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI);
    if (a) {
        time_t n = time(NULL); struct tm *tm = localtime(&n);
        TimeType xt;
        memset(&xt, 0, sizeof(xt));
        xt.tm_sec   = tm->tm_sec;
        xt.tm_min   = tm->tm_min;
        xt.tm_hour  = tm->tm_hour;
        xt.tm_mday  = tm->tm_mday;
        xt.tm_mon   = tm->tm_mon + 1;
        xt.tm_year  = tm->tm_year + 1900;
        xt.tm_wday  = tm->tm_wday;     /* 0=Sunday */
        xt.tm_yday  = tm->tm_yday;     /* 0-based day of year */
        xt.tm_isdst = tm->tm_isdst;
        xj380_mem_write(e, a, &xt, sizeof(xt));
    }
}
static void h_GETCPUMODEL(xj380_emu_t *e) {
    wr_str(e, r(e, UC_X86_REG_RDI), "Host CPU (Unicorn x86_64)");
}
static void h_GETMEMORYSIZE(xj380_emu_t *e) { w(e, UC_X86_REG_RAX, 4096); }
static void h_BROKEN(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char b[4096];
    fprintf(stderr, "\n[crash] %s\n", xj380_mem_read_str(e, a, b, sizeof(b)) == 0 ? b : "(no message)");
    e->running = false;
    (void)uc_emu_stop(e->uc);
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
    /* GUI 模式下不能真睡: 必须边等边轮询 SDL2 事件。
     * 使用基于时间的循环避免长时间 ms 的 O(n) 迭代 */
    {
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        uint64_t elapsed = 0;
        int render_tick = 0;
        while (elapsed < ms) {
            /* 每次休眠最多 16ms (~60fps)，保持响应性 */
            uint64_t remaining = ms - elapsed;
            uint32_t chunk = (remaining > 16) ? 16U : (uint32_t)remaining;
            usleep((unsigned int)(chunk * 1000U));
            int ev_ret = xj380_gui_poll_events(e);
            if (ev_ret != 0) {
                if (ev_ret == 2) {  /* 用户关闭窗口 */
                    e->running = false;
                }
                break;
            }
            if (++render_tick % 4 == 0) xj380_gui_render_all(e);  /* ≈60fps */
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (uint64_t)(now.tv_sec - start.tv_sec) * 1000ULL
                    + (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000000ULL;
        }
    }
#else
    while (ms > 0) {
        uint64_t chunk = ms > 1000ULL ? 1000ULL : ms;
        usleep((unsigned int)(chunk * 1000ULL));
        ms -= chunk;
    }
#endif
}
static void h_RUN(xj380_emu_t *e) {
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p));
    xj380_log(e, "[xj380] Run: %s (unsupported)\n", p);
}
static void h_RUNARGS(xj380_emu_t *e) {
    /* xapi_RunArgs(WSTR path, WSTR argv[]) — 同 h_RUN, 忽略参数 */
    uint64_t a = r(e, UC_X86_REG_RDI); char p[512];
    xj380_mem_read_str(e, a, p, sizeof(p));
    xj380_log(e, "[xj380] RunArgs: %s (unsupported)\n", p);
    w(e, UC_X86_REG_RAX, UINT64_MAX);
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
static void h_FLUSHTIME(xj380_emu_t *e) {
#ifdef XJ380_GUI
    xj380_gui_poll_events(e);
    xj380_gui_render_all(e);
#else
    (void)e;
#endif
}
static void h_ALLOCATEMEMORY(xj380_emu_t *e) {
    w(e, UC_X86_REG_RAX, emu_brk(e, (size_t)r(e, UC_X86_REG_RDI)));
}
static void h_FREEMEMORY(xj380_emu_t *e) { (void)e; /* 无法部分 unmap */ }
/* SYS_BRK handler: Linux brk() 语义 */
static void h_SYSBRK(xj380_emu_t *e)
{
    uint64_t new_brk = r(e, UC_X86_REG_RDI);

    if (new_brk == 0)
    {
        w(e, UC_X86_REG_RAX, e->brk_addr);
        return;
    }

    if (new_brk > XJ380_HEAP_BASE + XJ380_HEAP_SIZE)
    {
        w(e, UC_X86_REG_RAX, e->brk_addr);
        return;
    }

    if (new_brk > e->brk_addr)
    {
        if (map_heap_until(e, new_brk) != 0)
        {
            w(e, UC_X86_REG_RAX, e->brk_addr);
            return;
        }

        e->brk_addr = new_brk;
    }
    else if (new_brk >= XJ380_HEAP_BASE)
    {
        e->brk_addr = new_brk;
    }

    w(e, UC_X86_REG_RAX, e->brk_addr);
}

static void h_ARCHPRCTL(xj380_emu_t *e)
{
    uint64_t code = r(e, UC_X86_REG_RDI);
    uint64_t addr = r(e, UC_X86_REG_RSI);
    uint64_t base = 0;

    switch (code)
    {
        case XJ380_ARCH_SET_FS:
            w(e, UC_X86_REG_FS_BASE, addr);
            w(e, UC_X86_REG_RAX, 0);
            return;

        case XJ380_ARCH_GET_FS:
            base = r(e, UC_X86_REG_FS_BASE);
            if (addr && xj380_mem_write(e, addr, &base, sizeof(base)) == 0)
            {
                w(e, UC_X86_REG_RAX, 0);
                return;
            }
            break;

        case XJ380_ARCH_SET_GS:
            w(e, UC_X86_REG_GS_BASE, addr);
            w(e, UC_X86_REG_RAX, 0);
            return;

        case XJ380_ARCH_GET_GS:
            base = r(e, UC_X86_REG_GS_BASE);
            if (addr && xj380_mem_write(e, addr, &base, sizeof(base)) == 0)
            {
                w(e, UC_X86_REG_RAX, 0);
                return;
            }
            break;

        default:
            break;
    }

    w(e, UC_X86_REG_RAX, UINT64_MAX);
}

/* SYS_CLOCK_GETTIME handler: clock_gettime(clockid, *timespec) */
static void h_CLOCKGETTIME(xj380_emu_t *e) {
    uint64_t clk_id = r(e, UC_X86_REG_RDI);  /* arg1 = clockid */
    uint64_t tp     = r(e, UC_X86_REG_RSI);  /* arg2 = timespec ptr */
    if (tp) {
        struct timespec ts;
        clockid_t cid;
        switch (clk_id) {
            case 0:  cid = CLOCK_REALTIME;  break;
            case 1:  cid = CLOCK_MONOTONIC; break;
            case 2:  cid = CLOCK_PROCESS_CPUTIME_ID; break;
            case 3:  cid = CLOCK_THREAD_CPUTIME_ID;  break;
            default: cid = CLOCK_MONOTONIC; break;
        }
        clock_gettime(cid, &ts);
        /* 写 timespec: { tv_sec(8), tv_nsec(8) } = 16 bytes */
        uint64_t sec  = (uint64_t)ts.tv_sec;
        uint64_t nsec = (uint64_t)ts.tv_nsec;
        xj380_mem_write(e, tp,      &sec,  8);
        xj380_mem_write(e, tp + 8,  &nsec, 8);
    }
    w(e, UC_X86_REG_RAX, 0);  /* 成功 */
}

/* SYS_WRITE: write(fd, buf, count) → stdout */
static void h_WRITE(xj380_emu_t *e) {
    uint64_t buf  = r(e, UC_X86_REG_RSI);  /* arg2 */
    uint64_t cnt  = r(e, UC_X86_REG_RDX);  /* arg3 */
    char tmp[4096];
    size_t n = cnt < sizeof(tmp)-1 ? (size_t)cnt : sizeof(tmp)-1;
    xj380_mem_read(e, buf, tmp, n);
    tmp[n] = 0;
    fwrite(tmp, 1, n, stdout);
    fflush(stdout);
    w(e, UC_X86_REG_RAX, (uint64_t)n);
}

/* SYS_OPEN(2): path=RDI, flags=RSI, mode=RDX
   SYS_OPENAT(257): dirfd=RDI, path=RSI, flags=RDX, mode=RCX */
static void h_OPEN(xj380_emu_t *e) {
    uint64_t rax = r(e, UC_X86_REG_RAX);
    uint64_t path = (rax == 2) ? r(e, UC_X86_REG_RDI) : r(e, UC_X86_REG_RSI);
    char p[512];
    xj380_mem_read_str(e, path, p, sizeof(p));
    int idx = vfs_find(e, p);
    if (idx < 0) {
        char hp[PATH_MAX];
        idx = (vfs_resolve_host_path(e, p, hp, sizeof(hp)) == 0)
            ? vfs_import_host(e, p, hp)
            : -1;
        if (idx < 0) {
            fprintf(stderr, "[xj380] Open failed: '%s' does not exist\n", p);
            w(e, UC_X86_REG_RAX, UINT64_MAX);
            return;
        }
    }
    w(e, UC_X86_REG_RAX, (uint64_t)(idx + 3));
}

static void h_MAPMEMORY(xj380_emu_t *e)
{
    uint64_t a  = r(e, UC_X86_REG_RDI);
    uint64_t sz = r(e, UC_X86_REG_RSI);
    uint32_t fl = (uint32_t)r(e, UC_X86_REG_RDX);
    int      prot = 0;

    if (sz == 0 || sz > UINT64_MAX - 0xFFFULL)
    {
        w(e, UC_X86_REG_RAX, 0);
        return;
    }

    if (fl & 1U)
    {
        prot |= UC_PROT_READ;
    }
    if (fl & 2U)
    {
        prot |= UC_PROT_WRITE;
    }
    /* bit 8 (0x100) = PTE_NO_EXECUTE, 与 XJ380 API 手册一致 */
    if (!(fl & 0x100U))
    {
        prot |= UC_PROT_EXEC;
    }
    if (!prot)
    {
        prot = UC_PROT_READ;
    }

    sz = page_align_up(sz);
    if (!a)
    {
        a = emu_brk(e, (size_t)sz);
        w(e, UC_X86_REG_RAX, a);
        return;
    }

    if ((a & 0xFFFULL) != 0)
    {
        w(e, UC_X86_REG_RAX, 0);
        return;
    }

    if (uc_mem_map(e->uc, a, sz, (uint32_t)prot) != UC_ERR_OK)
    {
        w(e, UC_X86_REG_RAX, 0);
        return;
    }

    w(e, UC_X86_REG_RAX, a);
}

/* handler 表 */
#ifdef XJ380_GUI
/* ---- GUI handler stubs (调用 SDL2 后端) ---- */

static uint64_t current_arg7(xj380_emu_t *emu)
{
    if (emu->arg7_valid)
    {
        return emu->arg7_value;
    }

    return 0;
}


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
    uint64_t fill = current_arg7(e);
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
    uint64_t width_ptr = current_arg7(e);
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
    uint64_t width = xj380_gui_calc_text_width(e, r(e,UC_X86_REG_RDI),
        (uint32_t)r(e,UC_X86_REG_RSI));
    w(e, UC_X86_REG_RAX, width);
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
    xj380_gui_get_pic_size(e, r(e,UC_X86_REG_RDX),
        r(e,UC_X86_REG_RDI), r(e,UC_X86_REG_RSI));
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
        fprintf(stderr, "[xj380] unimplemented syscall #%d\n", no);
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
    emu->debug_enabled = true;

    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &emu->uc);
    if (err != UC_ERR_OK) {
        snprintf(emu->error, sizeof(emu->error), "uc_open: %s", uc_strerror(err));
        free(emu); return NULL;
    }

    /* Admin 用户 */
    memset(&emu->current_user, 0, sizeof(emu->current_user));
    copy_cstr(emu->current_user.name, sizeof(emu->current_user.name), "Admin");
    emu->current_user.user_type = 2;

    /* VFS 根 + 系统目录 (防止程序找不到系统文件死循环) */
    emu->vfs_capacity = 64;
    emu->vfs = calloc((size_t)emu->vfs_capacity, sizeof(vfs_entry_t));
    emu->vfs_count = 3;
    strcpy(emu->vfs[0].path, "/");
    emu->vfs[0].is_dir = true;
    strcpy(emu->vfs[1].path, "/system");
    emu->vfs[1].is_dir = true;
    strcpy(emu->vfs[2].path, "/system/font");
    emu->vfs[2].is_dir = true;

    /* trampoline 范围 hook: 截获所有 xapi 调用 */
    uc_hook h_tramp;
    _Pragma("GCC diagnostic push")
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
    (void)uc_hook_add(emu->uc, &h_tramp, UC_HOOK_CODE,
                      (void*)tramp_hook, emu,
                      XJ380_TRAMP_BASE, XJ380_TRAMP_BASE + XJ380_TRAMP_SIZE);

    /* 备用 syscall hook */
    uc_hook h_sys;
    (void)uc_hook_add(emu->uc, &h_sys, UC_HOOK_INTR,
                      (void*)on_syscall, emu,
                      UC_X86_INS_SYSCALL, (uint64_t)-1, (uint64_t)0);

    uc_hook h_invalid;
    (void)uc_hook_add(emu->uc, &h_invalid,
                      UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                      | UC_HOOK_MEM_FETCH_UNMAPPED,
                      (void*)invalid_mem_hook, emu,
                      (uint64_t)1, (uint64_t)0);
    _Pragma("GCC diagnostic pop")

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

void xj380_set_debug(xj380_emu_t *emu, bool enabled)
{
    if (!emu)
    {
        return;
    }

    emu->debug_enabled = enabled;
#ifdef XJ380_GUI
    xj380_gui_set_debug(enabled);
#endif
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

int xj380_run(xj380_emu_t *emu, int host_argc, char **host_argv)
{
    /* 设置模拟程序的 argc/argv/envp。字符串和指针表分开，避免长路径覆盖 argv。 */
    uint64_t rsp = r(emu, UC_X86_REG_RSP);
    (void)host_argc;

    const char *prog_name = (host_argv && host_argv[0]) ? host_argv[0] : "xswl-app";
    size_t      prog_len  = strlen(prog_name) + 1U;
    uint64_t    zero      = 0;

    rsp -= align_up_u64((uint64_t)prog_len, 16);
    uint64_t argv0_str = rsp;
    xj380_mem_write(emu, argv0_str, prog_name, prog_len);

    rsp -= 32;
    rsp &= ~0xFULL;
    uint64_t argv_array = rsp;
    uint64_t envp_array = argv_array + 16;

    xj380_mem_write(emu, argv_array, &argv0_str, 8);
    xj380_mem_write(emu, argv_array + 8, &zero, 8);
    xj380_mem_write(emu, envp_array, &zero, 8);

    w(emu, UC_X86_REG_RDI, 1);            /* argc = 1 */
    w(emu, UC_X86_REG_RSI, argv_array);    /* argv */
    w(emu, UC_X86_REG_RDX, envp_array);    /* envp */
    w(emu, UC_X86_REG_RIP, emu->entry_point);
    w(emu, UC_X86_REG_EFLAGS, 0x202);
    emu->running = true;

    xj380_log(emu, "[xj380] starting execution @ 0x%llx\n", (unsigned long long)emu->entry_point);

#ifdef XJ380_GUI
    /* GUI 模式: 时间分片执行，每 50000 条指令切出来处理事件 */
    while (emu->running) {
        uc_err err = uc_emu_start(emu->uc, emu->entry_point,
                                  0xFFFFFFFFFFFFFFFFULL, 0, 50000);
        if (!emu->running) break;
        if (err == UC_ERR_OK) {
            int ev = xj380_gui_poll_events(emu);
            if (ev == 2) { emu->running = false; break; }
            xj380_gui_render_all(emu);
        } else {
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
