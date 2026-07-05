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
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SYS_READ 0ULL
#define SYS_WRITE 1ULL
#define SYS_OPEN 2ULL
#define SYS_CLOSE 3ULL
#define SYS_STAT 4ULL
#define SYS_RT_SIGACTION 13ULL
#define SYS_RT_SIGPROCMASK 14ULL
#define SYS_GETPID 39ULL
#define SYS_SOCKET 41ULL
#define SYS_FORK 57ULL
#define SYS_EXECVE 59ULL
#define SYS_WAIT4 61ULL
#define SYS_KILL 62ULL
#define SYS_BRK 12ULL
#define SYS_ARCH_PRCTL 158ULL
#define SYS_GETUID 102ULL
#define SYS_GETGID 104ULL
#define SYS_GETEUID 107ULL
#define SYS_GETEGID 108ULL
#define SYS_GETGROUPS 115ULL
#define SYS_CLOCK_GETTIME 228ULL
#define ARCH_SET_FS 0x1002ULL
#define ARCH_GET_FS 0x1003ULL

#define XAPI_PRINTLINE 7385ULL
#define XAPI_OUTPUT 7381ULL
#define XAPI_PRINTF 7386ULL
#define XAPI_OPEN_FILE 7387ULL
#define XAPI_CLOSE_FILE 7388ULL
#define XAPI_FORK 7389ULL
#define XAPI_EXECVE 7390ULL
#define XAPI_MAKEDIR 7425ULL
#define XAPI_SEARCH_FILE 7416ULL
#define XAPI_READ_FILE 7423ULL
#define XAPI_GET_VERSION 7391ULL
#define XAPI_CREATE_WINDOW 7392ULL
#define XAPI_SET_WINDOW_TITLE 7393ULL
#define XAPI_SET_ICON 7395ULL
#define XAPI_DRAW_POINT 7396ULL
#define XAPI_DRAW_LINE 7397ULL
#define XAPI_DRAW_TEXT 7402ULL
#define XAPI_DRAW_PNG 7404ULL
#define XAPI_BUTTON 7410ULL
#define XAPI_BUTTON_EMP 7411ULL
#define XAPI_DRAW_TEXT_SW 7415ULL
#define XAPI_DRAW_PICTURE 7419ULL
#define XAPI_GET_CURRENT_USER 7413ULL
#define XAPI_GET_WINDOW_SIZE 7426ULL
#define XAPI_CALC_TEXT_WIDTH 7431ULL
#define XAPI_DELETE_BUTTON 7432ULL
#define XAPI_GET_TIME_X 7433ULL
#define XAPI_GET_MEMORY_SIZE 7435ULL
#define XAPI_DRAW_RECT 7398ULL
#define XAPI_DRAW_RECT_FILL 7399ULL
#define XAPI_SET_MSG_PRCOR 7405ULL
#define XAPI_READ_BUFFER 7406ULL
#define XAPI_WRITE_BUFFER 7407ULL
#define XAPI_WRITE_BUFFER_A 7408ULL
#define XAPI_REFRESH_WINDOW 7409ULL
#define XAPI_READ_BUFFER_A 7417ULL
#define XAPI_SLEEP 7430ULL
#define XAPI_GET_PIC_SIZE 7440ULL
#define XAPI_REFRESH_PART_WINDOW 7438ULL
#define XAPI_REG_RB_MENU 7436ULL
#define XAPI_URG_RB_MENU 7437ULL
#define XAPI_GET_TASK_LIST 7448ULL
#define XAPI_FLUSH_TIME 7445ULL
#define XAPI_DRAW_FA 7447ULL
#define XAPI_OUTPUT_SERIAL 7427ULL
#define XAPI_USER_OOBE_REQUIRED 7452ULL
#define XAPI_USER_LIST 7453ULL
#define XAPI_PUT_TEXT_INBOX 7456ULL
#define XAPI_GET_TEXT_INBOX 7457ULL
#define XAPI_DEL_TEXT_INBOX 7458ULL
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

typedef struct {
    uint32_t width;
    uint32_t height;
    char *title;
    uint8_t sets;
} NativeXWindow;

typedef struct {
    bool in_use;
    uint64_t handle;
    uint32_t width;
    uint32_t height;
    uint8_t sets;
    const char *title;
    uint64_t msg_proc;
    bool dirty;
    uint64_t draw_count;
} NativeWindow;

typedef struct {
    uint64_t length;
    void *buffer;
} NativeXFile;

typedef struct {
    bool in_use;
    FILE *file;
} NativeFd;

typedef struct {
    char filename[256];
    uint64_t length;
    uint64_t filetype;
} NativeDirNode;

typedef struct {
    char name[64];
    uint32_t user_type;
} NativeUserInfo;

typedef struct {
    int32_t tm_sec;
    int32_t tm_min;
    int32_t tm_hour;
    int32_t tm_mday;
    int32_t tm_mon;
    int32_t tm_year;
    int32_t tm_wday;
    int32_t tm_yday;
    int32_t tm_isdst;
} NativeTimeType;

#define NATIVE_MAX_WINDOWS 64U
#define NATIVE_MAX_FDS 64U

static char g_native_error[256];
static char g_native_app_dir[PATH_MAX];
static jmp_buf g_exit_jmp;
static int g_exit_code;
static bool g_debug_enabled;
static bool g_native_is_fork_child;
static uint64_t g_brk_addr;
static uint64_t g_brk_map_end;
static uint64_t g_fs_base;
static NativeWindow g_native_windows[NATIVE_MAX_WINDOWS];
static uint64_t g_next_window_handle;
static NativeFd g_native_fds[NATIVE_MAX_FDS];

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

static void native_set_app_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
    {
        snprintf(g_native_app_dir, sizeof(g_native_app_dir), ".");
        return;
    }

    size_t len = (size_t)(slash - path);
    if (len >= sizeof(g_native_app_dir))
    {
        len = sizeof(g_native_app_dir) - 1U;
    }
    memcpy(g_native_app_dir, path, len);
    g_native_app_dir[len] = '\0';
}

static int native_try_host_path(char *host_path, size_t host_path_size,
                                const char *prefix, const char *rel_path)
{
    int written = 0;

    if (!prefix || prefix[0] == '\0' || strcmp(prefix, ".") == 0)
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

static int native_try_system_font(char *host_path, size_t host_path_size,
                                  const char *rel_path)
{
    const char prefix[] = "system/font/";
    size_t prefix_len = sizeof(prefix) - 1U;
    if (strncmp(rel_path, prefix, prefix_len) != 0)
    {
        return -1;
    }

    char remapped[PATH_MAX];
    int written = snprintf(remapped, sizeof(remapped), "font/ttf/%s",
                           rel_path + prefix_len);
    if (written < 0 || (size_t)written >= sizeof(remapped))
    {
        return -1;
    }

    if (native_try_host_path(host_path, host_path_size, g_native_app_dir, remapped) == 0)
    {
        return 0;
    }
    if (native_try_host_path(host_path, host_path_size, "../XJ380", remapped) == 0)
    {
        return 0;
    }
    return native_try_host_path(host_path, host_path_size, "../../XJ380", remapped);
}

static int native_resolve_host_path(const char *vpath, char *host_path,
                                    size_t host_path_size)
{
    if (!vpath || vpath[0] != '/' || !host_path || host_path_size == 0)
    {
        return -1;
    }

    const char *rel_path = vpath + 1;
    if (rel_path[0] == '\0' || strstr(rel_path, ".."))
    {
        return -1;
    }

    if (native_try_host_path(host_path, host_path_size, ".", rel_path) == 0
        || native_try_host_path(host_path, host_path_size, g_native_app_dir, rel_path) == 0
        || native_try_host_path(host_path, host_path_size, "../XJ380", rel_path) == 0
        || native_try_host_path(host_path, host_path_size, "../../XJ380", rel_path) == 0
        || native_try_system_font(host_path, host_path_size, rel_path) == 0)
    {
        return 0;
    }

    return -1;
}

static uint64_t native_open_file(uint64_t path_ptr)
{
    if (!path_ptr)
    {
        return 0;
    }

    const char *vpath = (const char *)(uintptr_t)path_ptr;
    char host_path[PATH_MAX];
    if (native_resolve_host_path(vpath, host_path, sizeof(host_path)) != 0)
    {
        if (g_debug_enabled)
        {
            fprintf(stderr, "[native] OpenFile failed: %s\n", vpath);
        }
        return 0;
    }

    FILE *file = fopen(host_path, "rb");
    if (!file)
    {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }
    long length = ftell(file);
    if (length < 0)
    {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    NativeXFile *xfile = calloc(1, sizeof(*xfile));
    if (!xfile)
    {
        fclose(file);
        return 0;
    }

    if (length > 0)
    {
        xfile->buffer = malloc((size_t)length);
        if (!xfile->buffer)
        {
            free(xfile);
            fclose(file);
            return 0;
        }
        if (fread(xfile->buffer, 1, (size_t)length, file) != (size_t)length)
        {
            free(xfile->buffer);
            free(xfile);
            fclose(file);
            return 0;
        }
    }
    fclose(file);

    xfile->length = (uint64_t)length;
    if (g_debug_enabled)
    {
        fprintf(stderr, "[native] OpenFile: %s -> %s (%llu bytes)\n", vpath,
                host_path, (unsigned long long)xfile->length);
    }
    return (uint64_t)(uintptr_t)xfile;
}

static uint64_t native_close_file(uint64_t file_ptr)
{
    if (!file_ptr)
    {
        return 0;
    }

    NativeXFile *xfile = (NativeXFile *)(uintptr_t)file_ptr;
    free(xfile->buffer);
    xfile->buffer = NULL;
    xfile->length = 0;
    free(xfile);
    return 0;
}

static uint64_t native_read_file(uint64_t path_ptr, uint64_t buffer_ptr,
                                 uint64_t size, uint64_t offset)
{
    if (!path_ptr || !buffer_ptr || size == 0 || size > (uint64_t)SIZE_MAX
        || offset > (uint64_t)LONG_MAX)
    {
        return 0;
    }

    const char *vpath = (const char *)(uintptr_t)path_ptr;
    char host_path[PATH_MAX];
    if (native_resolve_host_path(vpath, host_path, sizeof(host_path)) != 0)
    {
        return 0;
    }

    FILE *file = fopen(host_path, "rb");
    if (!file)
    {
        return 0;
    }
    if (fseek(file, (long)offset, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    void *buffer = (void *)(uintptr_t)buffer_ptr;
    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    return (uint64_t)bytes_read;
}

static uint64_t native_search_file(uint64_t path_ptr, uint64_t count_ptr,
                                   uint64_t dir_ptr)
{
    if (!path_ptr || !count_ptr)
    {
        return 0;
    }

    const char *vpath = (const char *)(uintptr_t)path_ptr;
    char host_path[PATH_MAX];
    uint32_t count = 0;
    *(uint32_t *)(uintptr_t)count_ptr = 0;
    if (native_resolve_host_path(vpath, host_path, sizeof(host_path)) != 0)
    {
        return 0;
    }

    DIR *dir = opendir(host_path);
    if (!dir)
    {
        return 0;
    }

    NativeDirNode *out = dir_ptr ? (NativeDirNode *)(uintptr_t)dir_ptr : NULL;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && count < 255U)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        if (out)
        {
            NativeDirNode *node = &out[count];
            memset(node, 0, sizeof(*node));
            snprintf(node->filename, sizeof(node->filename), "%s", entry->d_name);

            char child_path[PATH_MAX];
            int written = snprintf(child_path, sizeof(child_path), "%s/%s", host_path,
                                   entry->d_name);
            if (written >= 0 && (size_t)written < sizeof(child_path))
            {
                struct stat st;
                if (stat(child_path, &st) == 0)
                {
                    node->length = S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
                    node->filetype = S_ISDIR(st.st_mode) ? 1ULL : 0ULL;
                }
            }
        }
        count++;
    }
    closedir(dir);
    *(uint32_t *)(uintptr_t)count_ptr = count;
    return count;
}

static int native_alloc_fd(FILE *file)
{
    for (size_t i = 0; i < NATIVE_MAX_FDS; i++)
    {
        if (!g_native_fds[i].in_use)
        {
            g_native_fds[i].in_use = true;
            g_native_fds[i].file = file;
            return (int)i + 3;
        }
    }
    return -1;
}

static NativeFd *native_get_fd(uint64_t fd)
{
    if (fd < 3 || fd - 3 >= NATIVE_MAX_FDS)
    {
        return NULL;
    }

    NativeFd *native_fd = &g_native_fds[fd - 3];
    return native_fd->in_use ? native_fd : NULL;
}

static uint64_t native_sys_open(uint64_t path_ptr)
{
    if (!path_ptr)
    {
        return (uint64_t)-1;
    }

    const char *vpath = (const char *)(uintptr_t)path_ptr;
    char host_path[PATH_MAX];
    if (native_resolve_host_path(vpath, host_path, sizeof(host_path)) != 0)
    {
        return (uint64_t)-1;
    }

    FILE *file = fopen(host_path, "rb");
    if (!file)
    {
        return (uint64_t)-1;
    }

    int fd = native_alloc_fd(file);
    if (fd < 0)
    {
        fclose(file);
        return (uint64_t)-1;
    }
    return (uint64_t)fd;
}

static uint64_t native_sys_read(uint64_t fd, uint64_t buffer_ptr, uint64_t size)
{
    if (!buffer_ptr || size > (uint64_t)SIZE_MAX)
    {
        return (uint64_t)-1;
    }

    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd)
    {
        return (uint64_t)-1;
    }
    return (uint64_t)fread((void *)(uintptr_t)buffer_ptr, 1, (size_t)size,
                           native_fd->file);
}

static uint64_t native_sys_close(uint64_t fd)
{
    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd)
    {
        return (uint64_t)-1;
    }

    fclose(native_fd->file);
    native_fd->file = NULL;
    native_fd->in_use = false;
    return 0;
}

static uint64_t native_sys_stat(uint64_t path_ptr, uint64_t stat_ptr)
{
    if (!path_ptr || !stat_ptr)
    {
        return (uint64_t)-1;
    }

    const char *vpath = (const char *)(uintptr_t)path_ptr;
    char host_path[PATH_MAX];
    if (native_resolve_host_path(vpath, host_path, sizeof(host_path)) != 0)
    {
        return (uint64_t)-1;
    }

    struct stat st;
    if (stat(host_path, &st) != 0)
    {
        return (uint64_t)-1;
    }
    memcpy((void *)(uintptr_t)stat_ptr, &st, sizeof(st));
    return 0;
}

static NativeWindow *native_find_window(uint64_t handle)
{
    if (!handle)
    {
        return NULL;
    }
    for (size_t i = 0; i < NATIVE_MAX_WINDOWS; i++)
    {
        NativeWindow *window = &g_native_windows[i];
        if (window->in_use && window->handle == handle)
        {
            return window;
        }
    }
    return NULL;
}

static bool native_has_windows(void)
{
    for (size_t i = 0; i < NATIVE_MAX_WINDOWS; i++)
    {
        if (g_native_windows[i].in_use)
        {
            return true;
        }
    }
    return false;
}

static uint64_t native_fork_process(void)
{
    if (native_has_windows())
    {
        return (uint64_t)-1;
    }

    pid_t pid = (pid_t)syscall(SYS_fork);
    if (pid < 0)
    {
        return (uint64_t)-1;
    }
    if (pid == 0)
    {
        g_native_is_fork_child = true;
        if (g_debug_enabled)
        {
            static const char child_msg[] = "[native] fork child reached\n";
            (void)write(2, child_msg, sizeof(child_msg) - 1U);
        }
        return 0;
    }
    if (g_debug_enabled)
    {
        fprintf(stderr, "[native] fork parent child=%d\n", (int)pid);
    }
    return (uint64_t)pid;
}

static uint64_t native_wait4_process(uint64_t pid_arg, uint64_t status_ptr,
                                     uint64_t options)
{
    int status = 0;
    int wait_options = 0;
    if (options & 1ULL)
    {
        wait_options |= WNOHANG;
    }

    pid_t waited = waitpid((pid_t)(int64_t)pid_arg, &status, wait_options);
    if (g_debug_enabled)
    {
        fprintf(stderr, "[native] wait4 pid=%lld options=0x%x -> %d status=0x%x\n",
                (long long)(int64_t)pid_arg, wait_options, (int)waited, status);
    }
    if (waited < 0)
    {
        return (uint64_t)-1;
    }
    if (status_ptr && waited > 0)
    {
        *(int *)(uintptr_t)status_ptr = status;
    }
    return (uint64_t)waited;
}

static uint64_t native_create_window(uint64_t handle_ptr, uint64_t xwindow_ptr)
{
    if (!handle_ptr || !xwindow_ptr)
    {
        return 0;
    }

    NativeXWindow *window_info = (NativeXWindow *)(uintptr_t)xwindow_ptr;
    uint64_t *out_handle = (uint64_t *)(uintptr_t)handle_ptr;

    for (size_t i = 0; i < NATIVE_MAX_WINDOWS; i++)
    {
        NativeWindow *window = &g_native_windows[i];
        if (window->in_use)
        {
            continue;
        }

        uint64_t handle = g_next_window_handle++;
        if (handle == 0)
        {
            handle = g_next_window_handle++;
        }
        window->in_use = true;
        window->handle = handle;
        window->width = window_info->width;
        window->height = window_info->height;
        window->sets = window_info->sets;
        window->title = window_info->title ? window_info->title : "";
        *out_handle = handle;

        if (g_debug_enabled)
        {
            fprintf(stderr, "[native] create window handle=%llu size=%ux%u title=%s\n",
                    (unsigned long long)handle, window->width, window->height,
                    window->title);
        }
        return 1;
    }

    return 0;
}

static uint64_t native_set_msg_prcor(uint64_t handle, uint64_t msg_proc)
{
    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    window->msg_proc = msg_proc;
    return 1;
}

static uint64_t native_get_window_size(uint64_t handle, uint64_t width_ptr,
                                       uint64_t height_ptr)
{
    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    if (width_ptr)
    {
        *(uint64_t *)(uintptr_t)width_ptr = window->width;
    }
    if (height_ptr)
    {
        *(uint64_t *)(uintptr_t)height_ptr = window->height;
    }
    return 1;
}

static uint64_t native_set_window_title(uint64_t handle, uint64_t title_ptr)
{
    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    window->title = title_ptr ? (const char *)(uintptr_t)title_ptr : "";
    return 1;
}

static uint64_t native_draw_rect(uint64_t handle, uint64_t x1, uint64_t y1,
                                 uint64_t x2, uint64_t y2, uint64_t color,
                                 bool fill)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
    (void)fill;

    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    window->dirty = true;
    window->draw_count++;
    return 1;
}

static uint64_t native_draw_noop(uint64_t handle)
{
    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    window->dirty = true;
    window->draw_count++;
    return 1;
}

static uint64_t native_calc_text_width(uint64_t text_ptr, uint64_t size)
{
    if (!text_ptr)
    {
        return 0;
    }

    const char *text = (const char *)(uintptr_t)text_ptr;
    uint64_t glyph_width = size > 0 ? size / 2U : 8U;
    if (glyph_width == 0)
    {
        glyph_width = 1;
    }
    return (uint64_t)strlen(text) * glyph_width;
}

static uint64_t native_get_current_user(uint64_t user_info_ptr)
{
    if (!user_info_ptr)
    {
        return 0;
    }

    NativeUserInfo *info = (NativeUserInfo *)(uintptr_t)user_info_ptr;
    memset(info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", "Root");
    info->user_type = 0;
    return 1;
}

static uint64_t native_get_pic_size(uint64_t width_ptr, uint64_t height_ptr)
{
    if (width_ptr)
    {
        *(uint32_t *)(uintptr_t)width_ptr = 0;
    }
    if (height_ptr)
    {
        *(uint32_t *)(uintptr_t)height_ptr = 0;
    }
    return 0;
}

static uint64_t native_get_time_x(uint64_t time_ptr)
{
    if (!time_ptr)
    {
        return 0;
    }

    time_t now = time(NULL);
    struct tm tm_value;
    memset(&tm_value, 0, sizeof(tm_value));
    if (!localtime_r(&now, &tm_value))
    {
        return 0;
    }

    NativeTimeType *out = (NativeTimeType *)(uintptr_t)time_ptr;
    out->tm_sec = tm_value.tm_sec;
    out->tm_min = tm_value.tm_min;
    out->tm_hour = tm_value.tm_hour;
    out->tm_mday = tm_value.tm_mday;
    out->tm_mon = tm_value.tm_mon;
    out->tm_year = tm_value.tm_year;
    out->tm_wday = tm_value.tm_wday;
    out->tm_yday = tm_value.tm_yday;
    out->tm_isdst = tm_value.tm_isdst;
    return 1;
}

static uint64_t native_refresh_window(uint64_t handle)
{
    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    window->dirty = false;
    return 1;
}

static uint64_t native_read_buffer(uint64_t handle, uint64_t width,
                                   uint64_t height, uint64_t buffer_ptr,
                                   size_t bytes_per_pixel)
{
    if (!native_find_window(handle) || !buffer_ptr || width == 0 || height == 0
        || width > (uint64_t)SIZE_MAX || height > (uint64_t)SIZE_MAX)
    {
        return 0;
    }

    if ((size_t)width > SIZE_MAX / (size_t)height
        || (size_t)width * (size_t)height > SIZE_MAX / bytes_per_pixel)
    {
        return 0;
    }

    size_t byte_count = (size_t)width * (size_t)height * bytes_per_pixel;
    memset((void *)(uintptr_t)buffer_ptr, 0, byte_count);
    return 1;
}

static uint64_t native_write_buffer(uint64_t handle)
{
    return native_find_window(handle) ? 1 : 0;
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
    if (syscall_no == SYS_READ)
    {
        return native_sys_read(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_WRITE)
    {
        if ((arg1 == 1 || arg1 == 2) && arg2 && arg3 <= (uint64_t)SIZE_MAX)
        {
            FILE *out = arg1 == 2 ? stderr : stdout;
            return (uint64_t)fwrite((const void *)(uintptr_t)arg2, 1, (size_t)arg3, out);
        }
        return (uint64_t)-1;
    }
    if (syscall_no == SYS_OPEN)
    {
        return native_sys_open(arg1);
    }
    if (syscall_no == SYS_CLOSE)
    {
        return native_sys_close(arg1);
    }
    if (syscall_no == SYS_STAT)
    {
        return native_sys_stat(arg1, arg2);
    }
    if (syscall_no == SYS_RT_SIGACTION || syscall_no == SYS_RT_SIGPROCMASK)
    {
        return 0;
    }
    if (syscall_no == SYS_FORK)
    {
        return native_fork_process();
    }
    if (syscall_no == SYS_WAIT4)
    {
        return native_wait4_process(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_GETPID)
    {
        return g_native_is_fork_child ? (uint64_t)getpid() : 1;
    }
    if (syscall_no == SYS_SOCKET || syscall_no == SYS_EXECVE || syscall_no == SYS_KILL)
    {
        return (uint64_t)-1;
    }
    if (syscall_no == SYS_GETUID || syscall_no == SYS_GETGID
        || syscall_no == SYS_GETEUID || syscall_no == SYS_GETEGID)
    {
        return 0;
    }
    if (syscall_no == SYS_GETGROUPS)
    {
        return 0;
    }
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
    if (syscall_no == SYS_ARCH_PRCTL)
    {
        if (arg1 == ARCH_SET_FS)
        {
            g_fs_base = arg2;
            return 0;
        }
        if (arg1 == ARCH_GET_FS && arg2)
        {
            *(uint64_t *)(uintptr_t)arg2 = g_fs_base;
            return 0;
        }
        return (uint64_t)-1;
    }
    if (syscall_no == SYS_CLOCK_GETTIME)
    {
        if (!arg2)
        {
            return (uint64_t)-1;
        }
        return clock_gettime((clockid_t)arg1, (struct timespec *)(uintptr_t)arg2) == 0
            ? 0
            : (uint64_t)-1;
    }

    if (syscall_no == XAPI_PRINTLINE)
    {
        if (arg1)
        {
            puts((const char *)(uintptr_t)arg1);
        }
        return 0;
    }
    if (syscall_no == XAPI_PRINTF)
    {
        if (arg1)
        {
            fputs((const char *)(uintptr_t)arg1, stdout);
            fflush(stdout);
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
    if (syscall_no == XAPI_OUTPUT_SERIAL)
    {
        if (arg1)
        {
            fprintf(stderr, "[serial] %s\n", (const char *)(uintptr_t)arg1);
        }
        return 0;
    }
    if (syscall_no == XAPI_OPEN_FILE)
    {
        return native_open_file(arg1);
    }
    if (syscall_no == XAPI_CLOSE_FILE)
    {
        return native_close_file(arg1);
    }
    if (syscall_no == XAPI_FORK)
    {
        return native_fork_process();
    }
    if (syscall_no == XAPI_EXECVE)
    {
        return (uint64_t)-1;
    }
    if (syscall_no == XAPI_READ_FILE)
    {
        return native_read_file(arg1, arg2, arg3, arg4);
    }
    if (syscall_no == XAPI_SEARCH_FILE)
    {
        return native_search_file(arg1, arg2, arg3);
    }
    if (syscall_no == XAPI_GET_VERSION)
    {
        if (arg1)
        {
            snprintf((char *)(uintptr_t)arg1, 64, "%s", "XJ380 native");
        }
        return 0;
    }
    if (syscall_no == XAPI_MAKEDIR)
    {
        return 0;
    }
    if (syscall_no == XAPI_CREATE_WINDOW)
    {
        return native_create_window(arg1, arg2);
    }
    if (syscall_no == XAPI_SET_WINDOW_TITLE)
    {
        return native_set_window_title(arg1, arg2);
    }
    if (syscall_no == XAPI_SET_ICON)
    {
        return native_find_window(arg1) ? 1 : 0;
    }
    if (syscall_no == XAPI_SET_MSG_PRCOR)
    {
        return native_set_msg_prcor(arg1, arg2);
    }
    if (syscall_no == XAPI_GET_WINDOW_SIZE)
    {
        return native_get_window_size(arg1, arg2, arg3);
    }
    if (syscall_no == XAPI_GET_CURRENT_USER)
    {
        return native_get_current_user(arg1);
    }
    if (syscall_no == XAPI_USER_LIST)
    {
        if (arg1 && arg2 > 0)
        {
            native_get_current_user(arg1);
            return 1;
        }
        return 0;
    }
    if (syscall_no == XAPI_GET_TASK_LIST)
    {
        return 0;
    }
    if (syscall_no == XAPI_GET_MEMORY_SIZE)
    {
        return 512ULL * 1024ULL * 1024ULL;
    }
    if (syscall_no == XAPI_GET_TIME_X)
    {
        return native_get_time_x(arg1);
    }
    if (syscall_no == XAPI_DRAW_POINT)
    {
        return native_draw_rect(arg1, arg2, arg3, arg2, arg3, arg4, true);
    }
    if (syscall_no == XAPI_DRAW_LINE)
    {
        return native_draw_noop(arg1);
    }
    if (syscall_no == XAPI_DRAW_TEXT || syscall_no == XAPI_DRAW_TEXT_SW)
    {
        return native_draw_noop(arg1);
    }
    if (syscall_no == XAPI_CALC_TEXT_WIDTH)
    {
        return native_calc_text_width(arg1, arg2);
    }
    if (syscall_no == XAPI_DRAW_RECT || syscall_no == XAPI_DRAW_RECT_FILL)
    {
        return native_draw_rect(arg1, arg2, arg3, arg4, arg5, arg6,
                                syscall_no == XAPI_DRAW_RECT_FILL);
    }
    if (syscall_no == XAPI_DRAW_PNG || syscall_no == XAPI_DRAW_PICTURE
        || syscall_no == XAPI_DRAW_FA)
    {
        return native_draw_noop(arg1);
    }
    if (syscall_no == XAPI_GET_PIC_SIZE)
    {
        return native_get_pic_size(arg1, arg2);
    }
    if (syscall_no == XAPI_READ_BUFFER)
    {
        return native_read_buffer(arg1, arg4, arg5, arg6, 3U);
    }
    if (syscall_no == XAPI_READ_BUFFER_A)
    {
        return native_read_buffer(arg1, arg4, arg5, arg6, 4U);
    }
    if (syscall_no == XAPI_WRITE_BUFFER || syscall_no == XAPI_WRITE_BUFFER_A)
    {
        return native_write_buffer(arg1);
    }
    if (syscall_no == XAPI_BUTTON || syscall_no == XAPI_BUTTON_EMP
        || syscall_no == XAPI_DELETE_BUTTON)
    {
        return native_find_window(arg1) ? 1 : 0;
    }
    if (syscall_no == XAPI_REG_RB_MENU || syscall_no == XAPI_URG_RB_MENU)
    {
        return native_find_window(arg1) ? 1 : 0;
    }
    if (syscall_no == XAPI_PUT_TEXT_INBOX || syscall_no == XAPI_DEL_TEXT_INBOX)
    {
        return native_find_window(arg1) ? 1 : 0;
    }
    if (syscall_no == XAPI_GET_TEXT_INBOX)
    {
        if (arg3 && arg4 > 0)
        {
            ((char *)(uintptr_t)arg3)[0] = '\0';
        }
        return native_find_window(arg1) ? 1 : 0;
    }
    if (syscall_no == XAPI_REFRESH_WINDOW)
    {
        return native_refresh_window(arg1);
    }
    if (syscall_no == XAPI_REFRESH_PART_WINDOW)
    {
        return native_refresh_window(arg1);
    }
    if (syscall_no == XAPI_FLUSH_TIME)
    {
        return 0;
    }
    if (syscall_no == XAPI_SLEEP)
    {
        uint64_t usec = arg1 > UINT64_MAX / 1000ULL ? UINT64_MAX : arg1 * 1000ULL;
        if (usec > 1000000ULL)
        {
            usec = 1000000ULL;
        }
        usleep((useconds_t)usec);
        return 0;
    }
    if (syscall_no == XAPI_USER_OOBE_REQUIRED)
    {
        return 0;
    }
    if (syscall_no == XAPI_EXIT || syscall_no == XAPI_EXIT_GROUP)
    {
        if (g_native_is_fork_child)
        {
            _exit((int)arg1);
        }
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
    g_native_is_fork_child = false;
    native_set_app_dir(path);
    g_brk_addr = 0;
    g_brk_map_end = 0;
    memset(g_native_windows, 0, sizeof(g_native_windows));
    g_next_window_handle = 1;
    memset(g_native_fds, 0, sizeof(g_native_fds));
    g_fs_base = 0;

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
