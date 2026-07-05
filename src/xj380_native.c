#define _GNU_SOURCE

#include "xj380_native.h"

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SYS_BRK 12ULL
#define XAPI_PRINTLINE 7385ULL
#define XAPI_OUTPUT 7381ULL
#define XAPI_EXIT 60ULL
#define XAPI_EXIT_GROUP 231ULL

#define PT_LOAD 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

static char g_native_error[256];
static jmp_buf g_exit_jmp;
static int g_exit_code;
static bool g_debug_enabled;
static uint64_t g_brk_addr;
static uint64_t g_brk_map_end;

static uint64_t xj380_native_enter_syscall(uint64_t syscall_no, uint64_t arg1,
                                           uint64_t arg2, uint64_t arg3,
                                           uint64_t arg4, uint64_t arg5,
                                           uint64_t arg6);

static void native_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_native_error, sizeof(g_native_error), fmt, ap);
    va_end(ap);
}

const char *xj380_native_strerror(void)
{
    return g_native_error[0] ? g_native_error : "no error";
}

static uint64_t page_down(uint64_t value, uint64_t page_size)
{
    return value & ~(page_size - 1ULL);
}

static uint64_t page_up(uint64_t value, uint64_t page_size)
{
    return (value + page_size - 1ULL) & ~(page_size - 1ULL);
}

static int phdr_prot(uint32_t flags)
{
    int prot = 0;
    if (flags & PF_R) prot |= PROT_READ;
    if (flags & PF_W) prot |= PROT_WRITE;
    if (flags & PF_X) prot |= PROT_EXEC;
    return prot ? prot : PROT_READ;
}

static bool read_at(FILE *file, uint64_t offset, void *dst, size_t size)
{
    return offset <= (uint64_t)LONG_MAX
        && fseek(file, (long)offset, SEEK_SET) == 0
        && fread(dst, 1, size, file) == size;
}

static uint64_t find_symbol(FILE *file, const Elf64_Ehdr *eh, const char *name)
{
    if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize != sizeof(Elf64_Shdr))
    {
        return 0;
    }

    Elf64_Shdr *sections = calloc((size_t)eh->e_shnum, sizeof(Elf64_Shdr));
    if (!sections)
    {
        return 0;
    }
    if (!read_at(file, eh->e_shoff, sections, (size_t)eh->e_shnum * sizeof(Elf64_Shdr)))
    {
        free(sections);
        return 0;
    }

    uint64_t result = 0;
    for (uint16_t i = 0; i < eh->e_shnum && !result; i++)
    {
        Elf64_Shdr *symtab = &sections[i];
        if (symtab->sh_type != SHT_SYMTAB || symtab->sh_entsize != sizeof(Elf64_Sym)
            || symtab->sh_link >= eh->e_shnum)
        {
            continue;
        }

        Elf64_Shdr *strtab = &sections[symtab->sh_link];
        if (strtab->sh_type != SHT_STRTAB || strtab->sh_size == 0)
        {
            continue;
        }

        char *strings = malloc((size_t)strtab->sh_size);
        if (!strings)
        {
            continue;
        }
        if (!read_at(file, strtab->sh_offset, strings, (size_t)strtab->sh_size))
        {
            free(strings);
            continue;
        }

        uint64_t count = symtab->sh_size / sizeof(Elf64_Sym);
        for (uint64_t n = 0; n < count; n++)
        {
            Elf64_Sym sym;
            if (!read_at(file, symtab->sh_offset + n * sizeof(Elf64_Sym), &sym, sizeof(sym)))
            {
                break;
            }
            if (sym.st_name < strtab->sh_size && strcmp(strings + sym.st_name, name) == 0)
            {
                result = sym.st_value;
                break;
            }
        }
        free(strings);
    }

    free(sections);
    return result;
}

static uint64_t xj380_native_enter_syscall(uint64_t syscall_no, uint64_t arg1,
                                           uint64_t arg2, uint64_t arg3,
                                           uint64_t arg4, uint64_t arg5,
                                           uint64_t arg6)
{
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;

    if (syscall_no == SYS_BRK)
    {
        long page_size_long = sysconf(_SC_PAGESIZE);
        uint64_t page_size = page_size_long > 0 ? (uint64_t)page_size_long : 4096ULL;

        if (arg1 == 0)
        {
            return g_brk_addr;
        }
        if (arg1 <= g_brk_map_end)
        {
            g_brk_addr = arg1;
            return g_brk_addr;
        }

        uint64_t next_map_end = page_up(arg1, page_size);
        void *mapped = mmap((void *)(uintptr_t)g_brk_map_end,
                            (size_t)(next_map_end - g_brk_map_end),
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                            -1, 0);
        if (mapped != MAP_FAILED)
        {
            g_brk_map_end = next_map_end;
            g_brk_addr = arg1;
        }
        return g_brk_addr;
    }

    if (syscall_no == XAPI_PRINTLINE)
    {
        if (arg1)
        {
            puts((const char *)(uintptr_t)arg1);
        }
        return 0;
    }
    if (syscall_no == XAPI_OUTPUT)
    {
        if (arg1)
        {
            fputs((const char *)(uintptr_t)arg1, stdout);
            fflush(stdout);
        }
        return 0;
    }
    if (syscall_no == XAPI_EXIT || syscall_no == XAPI_EXIT_GROUP)
    {
        g_exit_code = (int)arg1;
        longjmp(g_exit_jmp, 1);
    }

    if (g_debug_enabled)
    {
        fprintf(stderr, "[native] unsupported syscall: %llu\n",
                (unsigned long long)syscall_no);
    }
    native_set_error("unsupported native syscall: %llu",
                     (unsigned long long)syscall_no);
    fprintf(stderr, "native runtime error: %s\n", xj380_native_strerror());
    fflush(stderr);
    exit(1);
}

static bool patch_enter_syscall(uint64_t address)
{
    uint8_t patch[16] = {
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0,
        0x90, 0x90, 0x90, 0x90
    };
    uint64_t target = (uint64_t)(uintptr_t)&xj380_native_enter_syscall;
    long page_size_long = sysconf(_SC_PAGESIZE);
    uint64_t page_size = page_size_long > 0 ? (uint64_t)page_size_long : 4096ULL;
    uint64_t page = page_down(address, page_size);

    memcpy(patch + 2, &target, sizeof(target));
    if (mprotect((void *)(uintptr_t)page, (size_t)page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        native_set_error("mprotect enter_syscall page failed: %s", strerror(errno));
        return false;
    }
    memcpy((void *)(uintptr_t)address, patch, sizeof(patch));
    __builtin___clear_cache((char *)(uintptr_t)address,
                            (char *)(uintptr_t)(address + sizeof(patch)));
    if (mprotect((void *)(uintptr_t)page, (size_t)page_size, PROT_READ | PROT_EXEC) != 0)
    {
        native_set_error("restore enter_syscall page protection failed: %s", strerror(errno));
        return false;
    }
    return true;
}

static bool map_load_segments(FILE *file, const Elf64_Ehdr *eh)
{
    long page_size_long = sysconf(_SC_PAGESIZE);
    uint64_t page_size = page_size_long > 0 ? (uint64_t)page_size_long : 4096ULL;

    for (uint16_t i = 0; i < eh->e_phnum; i++)
    {
        Elf64_Phdr ph;
        if (!read_at(file, eh->e_phoff + (uint64_t)i * sizeof(ph), &ph, sizeof(ph)))
        {
            native_set_error("failed to read program header");
            return false;
        }
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0)
        {
            continue;
        }
        if (ph.p_filesz > ph.p_memsz)
        {
            native_set_error("invalid LOAD segment sizes");
            return false;
        }

        uint64_t map_start = page_down(ph.p_vaddr, page_size);
        uint64_t map_end = page_up(ph.p_vaddr + ph.p_memsz, page_size);
        uint64_t map_size = map_end - map_start;
        void *mapped = mmap((void *)(uintptr_t)map_start, (size_t)map_size,
                            PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                            -1, 0);
        if (mapped == MAP_FAILED)
        {
            native_set_error("mmap LOAD 0x%llx-0x%llx failed: %s",
                             (unsigned long long)map_start,
                             (unsigned long long)map_end,
                             strerror(errno));
            return false;
        }
        if (ph.p_filesz > 0
            && !read_at(file, ph.p_offset, (void *)(uintptr_t)ph.p_vaddr, (size_t)ph.p_filesz))
        {
            native_set_error("failed to read LOAD segment data");
            return false;
        }
        if (ph.p_memsz > ph.p_filesz)
        {
            memset((void *)(uintptr_t)(ph.p_vaddr + ph.p_filesz), 0,
                   (size_t)(ph.p_memsz - ph.p_filesz));
        }
        if (mprotect(mapped, (size_t)map_size, phdr_prot(ph.p_flags)) != 0)
        {
            native_set_error("mprotect LOAD segment failed: %s", strerror(errno));
            return false;
        }
        if (ph.p_vaddr + ph.p_memsz > g_brk_addr)
        {
            g_brk_addr = ph.p_vaddr + ph.p_memsz;
        }
    }
    g_brk_addr = page_up(g_brk_addr, page_size);
    g_brk_map_end = g_brk_addr;
    return true;
}

int xj380_native_run(const char *path, int argc, char **argv, bool debug_enabled)
{
    (void)argc;
    (void)argv;
    g_native_error[0] = '\0';
    g_exit_code = 0;
    g_debug_enabled = debug_enabled;
    g_brk_addr = 0;
    g_brk_map_end = 0;

    FILE *file = fopen(path, "rb");
    if (!file)
    {
        native_set_error("open failed: %s", path);
        return 1;
    }

    Elf64_Ehdr eh;
    if (!read_at(file, 0, &eh, sizeof(eh)))
    {
        fclose(file);
        native_set_error("failed to read ELF header");
        return 1;
    }
    if (memcmp(eh.e_ident, "\x7F" "ELF", 4) != 0 || eh.e_ident[4] != 2 || eh.e_machine != 0x3E)
    {
        fclose(file);
        native_set_error("not an x86_64 ELF");
        return 1;
    }
    if (eh.e_phentsize != sizeof(Elf64_Phdr) || eh.e_phnum == 0)
    {
        fclose(file);
        native_set_error("invalid ELF program headers");
        return 1;
    }

    uint64_t enter_syscall = find_symbol(file, &eh, "enter_syscall");
    if (!enter_syscall)
    {
        fclose(file);
        native_set_error("enter_syscall symbol not found");
        return 1;
    }
    if (!map_load_segments(file, &eh))
    {
        fclose(file);
        return 1;
    }
    fclose(file);

    if (!patch_enter_syscall(enter_syscall))
    {
        return 1;
    }

    if (g_debug_enabled)
    {
        fprintf(stderr, "[native] entry=0x%llx enter_syscall=0x%llx\n",
                (unsigned long long)eh.e_entry,
                (unsigned long long)enter_syscall);
    }

    if (setjmp(g_exit_jmp) == 0)
    {
        typedef int (*entry_fn)(int, char **, char **);
        char *guest_argv[] = {(char *)path, NULL};
        char *guest_envp[] = {NULL};
        entry_fn entry = (entry_fn)(uintptr_t)eh.e_entry;
        int returned = entry(1, guest_argv, guest_envp);
        g_exit_code = returned;
    }

    return g_exit_code;
}
