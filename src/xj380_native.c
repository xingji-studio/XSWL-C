#define _GNU_SOURCE

#include "xj380_native.h"

#include <SDL3/SDL.h>

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
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SYS_READ 0ULL
#define SYS_WRITE 1ULL
#define SYS_OPEN 2ULL
#define SYS_CLOSE 3ULL
#define SYS_STAT 4ULL
#define SYS_FSTAT 5ULL
#define SYS_LSEEK 8ULL
#define SYS_MMAP 9ULL
#define SYS_MPROTECT 10ULL
#define SYS_MUNMAP 11ULL
#define SYS_RT_SIGACTION 13ULL
#define SYS_RT_SIGPROCMASK 14ULL
#define SYS_RT_SIGRETURN 15ULL
#define SYS_IOCTL 16ULL
#define SYS_WRITEV 20ULL
#define SYS_PIPE 22ULL
#define SYS_SELECT 23ULL
#define SYS_GETPID 39ULL
#define SYS_SOCKET 41ULL
#define SYS_FORK 57ULL
#define SYS_EXECVE 59ULL
#define SYS_WAIT4 61ULL
#define SYS_KILL 62ULL
#define SYS_GETCWD 79ULL
#define SYS_CHDIR 80ULL
#define SYS_GETDENTS 78ULL
#define SYS_UNLINK 87ULL
#define SYS_BRK 12ULL
#define SYS_ARCH_PRCTL 158ULL
#define SYS_GETUID 102ULL
#define SYS_GETGID 104ULL
#define SYS_GETEUID 107ULL
#define SYS_GETEGID 108ULL
#define SYS_GETGROUPS 115ULL
#define SYS_CLOCK_GETTIME 228ULL
#define SYS_UTIMENSAT 280ULL
#define ARCH_SET_FS 0x1002ULL
#define ARCH_GET_FS 0x1003ULL

#define NATIVE_FB_WIDTH 640U
#define NATIVE_FB_HEIGHT 480U
#define NATIVE_FB_BPP 32U
#define NATIVE_FB_BYTES (NATIVE_FB_WIDTH * NATIVE_FB_HEIGHT * 2U * 4U)
#define NATIVE_FBIOGET_VSCREENINFO 0x4600ULL
#define NATIVE_FBIOGET_FSCREENINFO 0x4602ULL
#define NATIVE_FBIOPAN_DISPLAY 0x4606ULL

#define NATIVE_MSG_KEYUP 9ULL
#define NATIVE_MSG_KEYDOWN 10ULL

#define NATIVE_XKEY_ESC 128
#define NATIVE_XKEY_BACKSPACE 129
#define NATIVE_XKEY_TAB 130
#define NATIVE_XKEY_ENTER 131
#define NATIVE_XKEY_CAPS 132
#define NATIVE_XKEY_SHIFT 133
#define NATIVE_XKEY_CTRL 134
#define NATIVE_XKEY_ALT 135
#define NATIVE_XKEY_SPACE 136
#define NATIVE_XKEY_F1 137
#define NATIVE_XKEY_F2 138
#define NATIVE_XKEY_F3 139
#define NATIVE_XKEY_F4 140
#define NATIVE_XKEY_F5 141
#define NATIVE_XKEY_F6 142
#define NATIVE_XKEY_F7 143
#define NATIVE_XKEY_F8 144
#define NATIVE_XKEY_F9 145
#define NATIVE_XKEY_F10 146
#define NATIVE_XKEY_F11 147
#define NATIVE_XKEY_F12 148
#define NATIVE_XKEY_NUML 149
#define NATIVE_XKEY_SCROLL 150
#define NATIVE_XKEY_HOME 151
#define NATIVE_XKEY_UP 152
#define NATIVE_XKEY_PAGE_UP 153
#define NATIVE_XKEY_LEFT 154
#define NATIVE_XKEY_RIGHT 155
#define NATIVE_XKEY_END 156
#define NATIVE_XKEY_DOWN 157
#define NATIVE_XKEY_PAGE_DOWN 158
#define NATIVE_XKEY_INSERT 159
#define NATIVE_XKEY_DELETE 160

#define NATIVE_SIG_BLOCK 0ULL
#define NATIVE_SIG_UNBLOCK 1ULL
#define NATIVE_SIG_IGN 1ULL
#define NATIVE_MAX_SIGNALS 64U

#define XAPI_PRINTLINE 7385ULL
#define XAPI_OUTPUT 7381ULL
#define XAPI_INPUT 7382ULL
#define XAPI_GETCH 7383ULL
#define XAPI_ENDLINE 7384ULL
#define XAPI_GETLINE 7418ULL
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
#define XAPI_CLOSE_WINDOW 7394ULL
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
#define XAPI_GET_TIME 7412ULL
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

#define SXAH_CHECK_TERMINAL_INIT_STATUS 3801ULL
#define SXAH_MARK_IS_TERMINAL 128956723895689204ULL
#define SXAH_WRITE_INPUT_BUFFER 128956723895689205ULL
#define SXAH_READ_OUTPUT_BUFFER 128956723895689206ULL
#define SXAH_CHECK_INPUT_BUFFER 128956723895689207ULL
#define SXAH_UNLOCK_OUTPUT_LOCK 128956723895689208ULL
#define SXAH_MESSAGE_ASK 128956723895689209ULL
#define SXAH_INSTALLER_ENUM_DISKS 128956723895689220ULL
#define SXAH_INSTALLER_START 128956723895689221ULL
#define SXAH_INSTALLER_PROGRESS 128956723895689222ULL
#define SXAH_INSTALLER_PRECHECK 128956723895689223ULL
#define SXAH_INSTALLER_START_EX 128956723895689224ULL
#define SXAH_INSTALLER_RESCUE 128956723895689225ULL
#define SXAH_INSTALLER_LOG 128956723895689226ULL
#define SXAH_INSTALLER_START_OPTIONS 128956723895689227ULL
#define SXAH_INSTALLER_PRECHECK_OPTIONS 128956723895689228ULL

#define XJ380_INSTALLER_MAX_DISKS 32U
#define XJ380_INSTALLER_DISK_NAME_LEN 48U
#define XJ380_INSTALLER_STAGE_LEN 96U
#define XJ380_INSTALLER_DETAIL_LEN 192U
#define XJ380_INSTALLER_QUEUE_ITEMS 16U
#define XJ380_INSTALLER_QUEUE_LEN 128U
#define XJ380_INSTALLER_CHECK_ITEMS 12U
#define XJ380_INSTALLER_RESCUE_ITEMS 16U
#define XJ380_INSTALLER_LOG_LINES 24U
#define XJ380_INSTALLER_LOG_LEN 160U

#define XJ380_INSTALLER_DISK_FLAG_WRITABLE (1ULL << 0)
#define XJ380_INSTALLER_DISK_FLAG_SECTOR_512 (1ULL << 3)

#define XJ380_INSTALLER_MODE_DEVELOPER 3U
#define XJ380_INSTALLER_COMPONENT_DEFAULT 0xFULL

#define XJ380_INSTALLER_CHECK_OK 0U
#define XJ380_INSTALLER_CHECK_WARN 1U
#define XJ380_INSTALLER_CHECK_ERROR 2U

#define XJ380_INSTALLER_RESCUE_REBUILD_BOOT 1ULL
#define XJ380_INSTALLER_RESCUE_CHECK_DISK 2ULL
#define XJ380_INSTALLER_RESCUE_VIEW_DISK 3ULL
#define XJ380_INSTALLER_RESCUE_OPEN_TERM 4ULL
#define XJ380_INSTALLER_RESCUE_VIEW_LOG 5ULL

#define XJ380_INSTALLER_IDLE 0U
#define XJ380_INSTALLER_FAILED 3U

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
    bool in_use;
    uint64_t handle;
    uint32_t width;
    uint32_t height;
    uint8_t sets;
    const char *title;
    uint64_t msg_proc;
    bool dirty;
    uint64_t draw_count;
    SDL_Window *sdl_window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
} NativeWindow;

typedef struct {
    uint64_t length;
    void *buffer;
} NativeXFile;

typedef enum {
    NATIVE_FD_UNUSED = 0,
    NATIVE_FD_HOST,
    NATIVE_FD_DIR_ROOT,
    NATIVE_FD_FB,
} NativeFdKind;

typedef struct {
    NativeFdKind kind;
    int host_fd;
    bool root_dir_sent;
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

typedef struct {
    uint64_t base;
    uint64_t length;
} NativeIovec;

typedef struct {
    int64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} NativeDirent;

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
} NativeFbBitfield;

typedef struct {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
} NativeFbFixScreeninfo;

typedef struct {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    NativeFbBitfield red;
    NativeFbBitfield green;
    NativeFbBitfield blue;
    NativeFbBitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
} NativeFbVarScreeninfo;

typedef struct {
    uint32_t id;
    uint32_t sector_size;
    uint64_t size_bytes;
    uint64_t flags;
    char name[XJ380_INSTALLER_DISK_NAME_LEN];
} NativeInstallerDisk;

typedef struct {
    uint32_t count;
    uint32_t reserved;
    NativeInstallerDisk disks[XJ380_INSTALLER_MAX_DISKS];
} NativeInstallerDiskList;

typedef struct {
    uint32_t status;
    uint32_t code;
    char title[64];
    char detail[XJ380_INSTALLER_DETAIL_LEN];
} NativeInstallerCheckItem;

typedef struct {
    uint32_t disk_id;
    uint32_t mode;
    uint64_t components;
} NativeInstallerStartOptions;

typedef struct {
    uint32_t disk_id;
    uint32_t mode;
    uint32_t item_count;
    uint32_t can_continue;
    uint64_t components;
    uint64_t payload_bytes;
    uint64_t required_bytes;
    uint64_t target_bytes;
    uint64_t efi_first_lba;
    uint64_t efi_last_lba;
    NativeInstallerCheckItem items[XJ380_INSTALLER_CHECK_ITEMS];
} NativeInstallerPrecheck;

typedef struct {
    uint32_t state;
    uint32_t percent;
    int64_t result;
    char stage[XJ380_INSTALLER_STAGE_LEN];
    char detail[XJ380_INSTALLER_DETAIL_LEN];
    uint32_t queue_index;
    uint32_t queue_total;
    uint32_t queue_count;
    uint32_t queue_reserved;
    char queue_items[XJ380_INSTALLER_QUEUE_ITEMS][XJ380_INSTALLER_QUEUE_LEN];
    uint32_t mode;
    uint32_t stage_percent;
    uint32_t total_percent;
    uint32_t small_file_count;
    uint32_t large_file_count;
    uint32_t copied_small_file_count;
    uint32_t copied_large_file_count;
    uint64_t bytes_per_second;
    uint64_t eta_seconds;
    uint64_t copied_bytes;
    uint64_t total_bytes;
    uint64_t current_file_bytes;
    uint64_t current_file_size;
} NativeInstallerProgress;

typedef struct {
    uint32_t status;
    uint32_t code;
    char title[64];
    char detail[XJ380_INSTALLER_DETAIL_LEN];
} NativeInstallerRescueItem;

typedef struct {
    int64_t result;
    uint32_t item_count;
    uint32_t reserved;
    NativeInstallerRescueItem items[XJ380_INSTALLER_RESCUE_ITEMS];
} NativeInstallerRescueResult;

typedef struct {
    uint32_t count;
    uint32_t reserved;
    char lines[XJ380_INSTALLER_LOG_LINES][XJ380_INSTALLER_LOG_LEN];
} NativeInstallerLog;

typedef struct {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} NativeSigAction;

#define NATIVE_MAX_WINDOWS 64U
#define NATIVE_MAX_FDS 64U

static char g_native_error[256];
static char g_native_app_dir[PATH_MAX];
static jmp_buf g_exit_jmp;
static int g_exit_code;
static bool g_debug_enabled;
static bool g_native_is_fork_child;
static pid_t g_last_native_child_pid;
static uint64_t g_brk_addr;
static uint64_t g_brk_map_end;
static uint64_t g_fs_base;
static NativeWindow g_native_windows[NATIVE_MAX_WINDOWS];
static uint64_t g_next_window_handle;
static NativeFd g_native_fds[NATIVE_MAX_FDS];
static NativeInstallerProgress g_native_installer_progress;
static NativeSigAction g_native_sigactions[NATIVE_MAX_SIGNALS];
static uint64_t g_native_signal_mask;
static uint64_t g_native_pending_signals;
static bool g_native_sdl_initialized;

static uint64_t xj380_native_enter_syscall(uint64_t syscall_no, uint64_t arg1,
                                           uint64_t arg2, uint64_t arg3,
                                           uint64_t arg4, uint64_t arg5,
                                           uint64_t arg6)
    __attribute__((force_align_arg_pointer));

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
    if (!vpath || !host_path || host_path_size == 0)
    {
        return -1;
    }

    const char *rel_path = vpath[0] == '/' ? vpath + 1 : vpath;
    if (rel_path[0] == '\0' || strstr(rel_path, ".."))
    {
        return -1;
    }

    if (native_try_host_path(host_path, host_path_size, g_native_app_dir, rel_path) == 0
        || native_try_host_path(host_path, host_path_size, ".", rel_path) == 0
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

static int native_alloc_fd(NativeFdKind kind, int host_fd)
{
    for (size_t i = 0; i < NATIVE_MAX_FDS; i++)
    {
        if (g_native_fds[i].kind == NATIVE_FD_UNUSED)
        {
            g_native_fds[i].kind = kind;
            g_native_fds[i].host_fd = host_fd;
            g_native_fds[i].root_dir_sent = false;
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
    return native_fd->kind != NATIVE_FD_UNUSED ? native_fd : NULL;
}

static int native_resolve_writable_host_path(const char *vpath, char *host_path,
                                             size_t host_path_size)
{
    if (!vpath || vpath[0] != '/' || strstr(vpath, ".."))
    {
        return -1;
    }

    const char *name = strrchr(vpath, '/');
    name = name ? name + 1 : vpath + 1;
    if (name[0] == '\0' || strchr(name, '/'))
    {
        return -1;
    }

    int written = snprintf(host_path, host_path_size, "/tmp/xswl-native-%s", name);
    return written >= 0 && (size_t)written < host_path_size ? 0 : -1;
}

static uint64_t native_sys_open(uint64_t path_ptr, uint64_t flags_arg,
                                uint64_t mode_arg)
{
    if (!path_ptr)
    {
        return (uint64_t)-1;
    }

    const char *vpath = (const char *)(uintptr_t)path_ptr;
    int flags = (int)flags_arg;
    mode_t mode = (mode_t)mode_arg;
    if (strcmp(vpath, "/dev/fb0") == 0)
    {
        return (uint64_t)native_alloc_fd(NATIVE_FD_FB, -1);
    }
    if (strcmp(vpath, "/") == 0 && (flags & O_DIRECTORY))
    {
        return (uint64_t)native_alloc_fd(NATIVE_FD_DIR_ROOT, -1);
    }

    char host_path[PATH_MAX];
    if (native_resolve_host_path(vpath, host_path, sizeof(host_path)) != 0
        && (!(flags & O_CREAT)
            || native_resolve_writable_host_path(vpath, host_path, sizeof(host_path)) != 0))
    {
        return (uint64_t)-1;
    }

    int host_fd = open(host_path, flags, mode);
    if (host_fd < 0)
    {
        return (uint64_t)-1;
    }

    int fd = native_alloc_fd(NATIVE_FD_HOST, host_fd);
    if (fd < 0)
    {
        close(host_fd);
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
    if (native_fd->kind != NATIVE_FD_HOST)
    {
        return (uint64_t)-1;
    }

    ssize_t ret = read(native_fd->host_fd, (void *)(uintptr_t)buffer_ptr,
                       (size_t)size);
    return ret >= 0 ? (uint64_t)ret : (uint64_t)-1;
}

static uint64_t native_sys_write(uint64_t fd, uint64_t buffer_ptr, uint64_t size)
{
    if (!buffer_ptr || size > (uint64_t)SIZE_MAX)
    {
        return (uint64_t)-1;
    }
    if (fd == 1 || fd == 2)
    {
        FILE *out = fd == 2 ? stderr : stdout;
        return (uint64_t)fwrite((const void *)(uintptr_t)buffer_ptr, 1,
                                (size_t)size, out);
    }

    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd || native_fd->kind != NATIVE_FD_HOST)
    {
        return (uint64_t)-1;
    }
    ssize_t ret = write(native_fd->host_fd, (const void *)(uintptr_t)buffer_ptr,
                        (size_t)size);
    return ret >= 0 ? (uint64_t)ret : (uint64_t)-1;
}

static uint64_t native_sys_close(uint64_t fd)
{
    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd)
    {
        return (uint64_t)-1;
    }

    if (native_fd->kind == NATIVE_FD_HOST)
    {
        close(native_fd->host_fd);
    }
    native_fd->host_fd = -1;
    native_fd->kind = NATIVE_FD_UNUSED;
    native_fd->root_dir_sent = false;
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

static uint64_t native_sys_fstat(uint64_t fd, uint64_t stat_ptr)
{
    if (!stat_ptr)
    {
        return (uint64_t)-1;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd)
    {
        return (uint64_t)-1;
    }
    if (native_fd->kind == NATIVE_FD_HOST)
    {
        if (fstat(native_fd->host_fd, &st) != 0)
        {
            return (uint64_t)-1;
        }
    }
    else if (native_fd->kind == NATIVE_FD_DIR_ROOT)
    {
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
    }
    else if (native_fd->kind == NATIVE_FD_FB)
    {
        st.st_mode = S_IFCHR | 0600;
        st.st_size = NATIVE_FB_BYTES;
    }
    memcpy((void *)(uintptr_t)stat_ptr, &st, sizeof(st));
    return 0;
}

static uint64_t native_sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence)
{
    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd || native_fd->kind != NATIVE_FD_HOST)
    {
        return (uint64_t)-1;
    }

    off_t ret = lseek(native_fd->host_fd, (off_t)(int64_t)offset, (int)whence);
    return ret >= 0 ? (uint64_t)ret : (uint64_t)-1;
}

static uint64_t native_sys_writev(uint64_t fd, uint64_t iov_ptr,
                                  uint64_t iovcnt)
{
    if (!iov_ptr || iovcnt > 1024)
    {
        return (uint64_t)-1;
    }

    uint64_t total = 0;
    NativeIovec *iov = (NativeIovec *)(uintptr_t)iov_ptr;
    for (uint64_t i = 0; i < iovcnt; i++)
    {
        uint64_t written = native_sys_write(fd, iov[i].base, iov[i].length);
        if (written == (uint64_t)-1)
        {
            return total ? total : (uint64_t)-1;
        }
        total += written;
        if (written != iov[i].length)
        {
            break;
        }
    }
    return total;
}

static uint64_t native_sys_pipe(uint64_t pipefd_ptr)
{
    if (!pipefd_ptr)
    {
        return (uint64_t)-1;
    }

    int host_pipe[2];
    if (pipe(host_pipe) != 0)
    {
        return (uint64_t)-1;
    }

    int read_fd = native_alloc_fd(NATIVE_FD_HOST, host_pipe[0]);
    int write_fd = native_alloc_fd(NATIVE_FD_HOST, host_pipe[1]);
    if (read_fd < 0 || write_fd < 0)
    {
        if (read_fd >= 0)
        {
            native_sys_close((uint64_t)read_fd);
        }
        else
        {
            close(host_pipe[0]);
        }
        close(host_pipe[1]);
        return (uint64_t)-1;
    }

    int *guest_pipe = (int *)(uintptr_t)pipefd_ptr;
    guest_pipe[0] = read_fd;
    guest_pipe[1] = write_fd;
    return 0;
}

static bool native_fdset_isset(uint64_t set_ptr, int fd)
{
    uint64_t *bits = (uint64_t *)(uintptr_t)set_ptr;
    return (bits[(unsigned)fd / 64U] & (1ULL << ((unsigned)fd % 64U))) != 0;
}

static void native_fdset_zero(uint64_t set_ptr)
{
    memset((void *)(uintptr_t)set_ptr, 0, 128);
}

static void native_fdset_set(uint64_t set_ptr, int fd)
{
    uint64_t *bits = (uint64_t *)(uintptr_t)set_ptr;
    bits[(unsigned)fd / 64U] |= 1ULL << ((unsigned)fd % 64U);
}

static uint64_t native_sys_select(uint64_t nfds, uint64_t readfds_ptr,
                                  uint64_t writefds_ptr, uint64_t exceptfds_ptr,
                                  uint64_t timeout_ptr)
{
    (void)timeout_ptr;
    uint64_t ready = 0;
    uint8_t read_ready[128];
    uint8_t write_ready[128];
    memset(read_ready, 0, sizeof(read_ready));
    memset(write_ready, 0, sizeof(write_ready));

    for (uint64_t fd = 0; fd < nfds && fd < 1024; fd++)
    {
        NativeFd *native_fd = native_get_fd(fd);
        if (!native_fd)
        {
            continue;
        }
        if (readfds_ptr && native_fdset_isset(readfds_ptr, (int)fd))
        {
            read_ready[fd / 8U] |= (uint8_t)(1U << (fd % 8U));
            ready++;
        }
        if (writefds_ptr && native_fdset_isset(writefds_ptr, (int)fd))
        {
            write_ready[fd / 8U] |= (uint8_t)(1U << (fd % 8U));
            ready++;
        }
    }

    if (readfds_ptr)
    {
        native_fdset_zero(readfds_ptr);
    }
    if (writefds_ptr)
    {
        native_fdset_zero(writefds_ptr);
    }
    if (exceptfds_ptr)
    {
        native_fdset_zero(exceptfds_ptr);
    }
    for (uint64_t fd = 0; fd < nfds && fd < 1024; fd++)
    {
        if (readfds_ptr && (read_ready[fd / 8U] & (uint8_t)(1U << (fd % 8U))))
        {
            native_fdset_set(readfds_ptr, (int)fd);
        }
        if (writefds_ptr && (write_ready[fd / 8U] & (uint8_t)(1U << (fd % 8U))))
        {
            native_fdset_set(writefds_ptr, (int)fd);
        }
    }
    return ready;
}

static uint64_t native_sys_getdents(uint64_t fd, uint64_t dents_ptr,
                                    uint64_t size)
{
    if (!dents_ptr || size < sizeof(NativeDirent))
    {
        return (uint64_t)-1;
    }

    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd || native_fd->kind != NATIVE_FD_DIR_ROOT)
    {
        return (uint64_t)-1;
    }
    if (native_fd->root_dir_sent)
    {
        return 0;
    }

    NativeDirent *dent = (NativeDirent *)(uintptr_t)dents_ptr;
    memset(dent, 0, sizeof(*dent));
    dent->d_ino = 1;
    dent->d_off = 1;
    dent->d_reclen = (uint16_t)sizeof(*dent);
    dent->d_type = 4;
    snprintf(dent->d_name, sizeof(dent->d_name), "%s", "apps");
    native_fd->root_dir_sent = true;
    return sizeof(*dent);
}

static uint64_t native_sys_unlink(uint64_t path_ptr)
{
    if (!path_ptr)
    {
        return (uint64_t)-1;
    }

    char host_path[PATH_MAX];
    if (native_resolve_writable_host_path((const char *)(uintptr_t)path_ptr,
                                          host_path, sizeof(host_path)) != 0)
    {
        return (uint64_t)-1;
    }
    return unlink(host_path) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t native_sys_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                                uint64_t flags, uint64_t fd_arg,
                                uint64_t offset)
{
    if (length == 0 || length > (uint64_t)SIZE_MAX)
    {
        return (uint64_t)-1;
    }

    int host_fd = -1;
    NativeFd *native_fd = native_get_fd(fd_arg);
    if (native_fd && native_fd->kind == NATIVE_FD_HOST)
    {
        host_fd = native_fd->host_fd;
    }
    else if (native_fd && native_fd->kind == NATIVE_FD_FB)
    {
        flags = MAP_PRIVATE | MAP_ANONYMOUS;
        host_fd = -1;
        offset = 0;
    }
    else if ((int64_t)fd_arg < 0)
    {
        host_fd = -1;
    }
    else
    {
        return (uint64_t)-1;
    }

    void *mapped = mmap(addr ? (void *)(uintptr_t)addr : NULL, (size_t)length,
                        (int)prot, (int)flags, host_fd, (off_t)offset);
    return mapped != MAP_FAILED ? (uint64_t)(uintptr_t)mapped : (uint64_t)-1;
}

static uint64_t native_sys_ioctl(uint64_t fd, uint64_t request, uint64_t arg_ptr)
{
    NativeFd *native_fd = native_get_fd(fd);
    if (!native_fd || native_fd->kind != NATIVE_FD_FB || !arg_ptr)
    {
        return (uint64_t)-1;
    }

    if (request == NATIVE_FBIOGET_FSCREENINFO)
    {
        NativeFbFixScreeninfo *fix = (NativeFbFixScreeninfo *)(uintptr_t)arg_ptr;
        memset(fix, 0, sizeof(*fix));
        snprintf(fix->id, sizeof(fix->id), "%s", "xswlfb");
        fix->smem_len = NATIVE_FB_BYTES;
        fix->line_length = NATIVE_FB_WIDTH * 4U;
        return 0;
    }
    if (request == NATIVE_FBIOGET_VSCREENINFO || request == NATIVE_FBIOPAN_DISPLAY)
    {
        NativeFbVarScreeninfo *var = (NativeFbVarScreeninfo *)(uintptr_t)arg_ptr;
        if (request == NATIVE_FBIOGET_VSCREENINFO)
        {
            memset(var, 0, sizeof(*var));
            var->xres = NATIVE_FB_WIDTH;
            var->yres = NATIVE_FB_HEIGHT;
            var->xres_virtual = NATIVE_FB_WIDTH;
            var->yres_virtual = NATIVE_FB_HEIGHT * 2U;
            var->bits_per_pixel = NATIVE_FB_BPP;
            var->red.offset = 16;
            var->red.length = 8;
            var->green.offset = 8;
            var->green.length = 8;
            var->blue.offset = 0;
            var->blue.length = 8;
            var->transp.offset = 24;
            var->transp.length = 8;
        }
        return 0;
    }
    return (uint64_t)-1;
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

static void native_destroy_window(NativeWindow *window)
{
    if (!window || !window->in_use)
    {
        return;
    }

    free(window->pixels);
    window->pixels = NULL;
    if (window->texture)
    {
        SDL_DestroyTexture(window->texture);
        window->texture = NULL;
    }
    if (window->renderer)
    {
        SDL_DestroyRenderer(window->renderer);
        window->renderer = NULL;
    }
    if (window->sdl_window)
    {
        SDL_DestroyWindow(window->sdl_window);
        window->sdl_window = NULL;
    }
    memset(window, 0, sizeof(*window));
}

static void native_cleanup_windows(void)
{
    for (size_t i = 0; i < NATIVE_MAX_WINDOWS; i++)
    {
        native_destroy_window(&g_native_windows[i]);
    }
    if (g_native_sdl_initialized)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        g_native_sdl_initialized = false;
    }
}

static bool native_init_sdl(void)
{
    if (g_native_sdl_initialized)
    {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "[native] SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return false;
    }
    g_native_sdl_initialized = true;
    return true;
}

static int native_sdl_key_to_ascii(SDL_Keycode sym, SDL_Keymod mod)
{
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    bool caps = (mod & SDL_KMOD_CAPS) != 0;

    if (sym >= SDLK_A && sym <= SDLK_Z)
    {
        int ch = 'a' + (int)(sym - SDLK_A);
        if (shift != caps)
        {
            ch = ch - 'a' + 'A';
        }
        return ch;
    }
    if (sym >= SDLK_0 && sym <= SDLK_9)
    {
        static const char shifted[] = ")!@#$%^&*(";
        return shift ? shifted[sym - SDLK_0] : (int)('0' + (sym - SDLK_0));
    }

    switch (sym)
    {
    case SDLK_SPACE: return ' ';
    case SDLK_MINUS: return shift ? '_' : '-';
    case SDLK_EQUALS: return shift ? '+' : '=';
    case SDLK_LEFTBRACKET: return shift ? '{' : '[';
    case SDLK_RIGHTBRACKET: return shift ? '}' : ']';
    case SDLK_BACKSLASH: return shift ? '|' : '\\';
    case SDLK_SEMICOLON: return shift ? ':' : ';';
    case SDLK_APOSTROPHE: return shift ? '"' : '\'';
    case SDLK_COMMA: return shift ? '<' : ',';
    case SDLK_PERIOD: return shift ? '>' : '.';
    case SDLK_SLASH: return shift ? '?' : '/';
    case SDLK_GRAVE: return shift ? '~' : '`';
    default: return -1;
    }
}

static int native_sdl_key_to_xj380_key(SDL_Keycode sym, SDL_Keymod mod)
{
    switch (sym)
    {
    case SDLK_ESCAPE: return NATIVE_XKEY_ESC;
    case SDLK_BACKSPACE: return '\b';
    case SDLK_TAB: return NATIVE_XKEY_TAB;
    case SDLK_RETURN: return '\n';
    case SDLK_CAPSLOCK: return NATIVE_XKEY_CAPS;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return NATIVE_XKEY_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return NATIVE_XKEY_CTRL;
    case SDLK_LALT:
    case SDLK_RALT: return NATIVE_XKEY_ALT;
    case SDLK_SPACE: return NATIVE_XKEY_SPACE;
    case SDLK_F1: return NATIVE_XKEY_F1;
    case SDLK_F2: return NATIVE_XKEY_F2;
    case SDLK_F3: return NATIVE_XKEY_F3;
    case SDLK_F4: return NATIVE_XKEY_F4;
    case SDLK_F5: return NATIVE_XKEY_F5;
    case SDLK_F6: return NATIVE_XKEY_F6;
    case SDLK_F7: return NATIVE_XKEY_F7;
    case SDLK_F8: return NATIVE_XKEY_F8;
    case SDLK_F9: return NATIVE_XKEY_F9;
    case SDLK_F10: return NATIVE_XKEY_F10;
    case SDLK_F11: return NATIVE_XKEY_F11;
    case SDLK_F12: return NATIVE_XKEY_F12;
    case SDLK_NUMLOCKCLEAR: return NATIVE_XKEY_NUML;
    case SDLK_SCROLLLOCK: return NATIVE_XKEY_SCROLL;
    case SDLK_HOME: return NATIVE_XKEY_HOME;
    case SDLK_UP: return NATIVE_XKEY_UP;
    case SDLK_PAGEUP: return NATIVE_XKEY_PAGE_UP;
    case SDLK_LEFT: return NATIVE_XKEY_LEFT;
    case SDLK_RIGHT: return NATIVE_XKEY_RIGHT;
    case SDLK_END: return NATIVE_XKEY_END;
    case SDLK_DOWN: return NATIVE_XKEY_DOWN;
    case SDLK_PAGEDOWN: return NATIVE_XKEY_PAGE_DOWN;
    case SDLK_INSERT: return NATIVE_XKEY_INSERT;
    case SDLK_DELETE: return NATIVE_XKEY_DELETE;
    default: break;
    }

    return native_sdl_key_to_ascii(sym, mod);
}

static NativeWindow *native_find_window_by_sdl_id(SDL_WindowID window_id)
{
    for (size_t i = 0; i < NATIVE_MAX_WINDOWS; i++)
    {
        NativeWindow *window = &g_native_windows[i];
        if (window->in_use && window->sdl_window
            && SDL_GetWindowID(window->sdl_window) == window_id)
        {
            return window;
        }
    }
    return NULL;
}

static void native_dispatch_window_msg(NativeWindow *window, uint64_t type,
                                       uint64_t hdata, uint64_t ldata)
{
    if (!window || !window->msg_proc)
    {
        return;
    }

    void (*callback)(uint64_t, uint64_t, uint64_t) =
        (void (*)(uint64_t, uint64_t, uint64_t))(uintptr_t)window->msg_proc;
    callback(type, hdata, ldata);
}

static void native_poll_sdl_events(void)
{
    if (!g_native_sdl_initialized)
    {
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            g_exit_code = 0;
            longjmp(g_exit_jmp, 1);
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            NativeWindow *window = native_find_window_by_sdl_id(event.window.windowID);
            if (window)
            {
                g_exit_code = 0;
                longjmp(g_exit_jmp, 1);
            }
        }
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        {
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
            {
                continue;
            }
            NativeWindow *window = native_find_window_by_sdl_id(event.key.windowID);
            int key = native_sdl_key_to_xj380_key(event.key.key, event.key.mod);
            if (window && key >= 0)
            {
                native_dispatch_window_msg(window,
                                           event.type == SDL_EVENT_KEY_DOWN
                                               ? NATIVE_MSG_KEYDOWN
                                               : NATIVE_MSG_KEYUP,
                                           0,
                                           (uint64_t)(uint8_t)key);
            }
        }
    }
}

static uint64_t native_present_window(NativeWindow *window)
{
    if (!window)
    {
        return 0;
    }
    native_poll_sdl_events();
    if (!window->renderer || !window->texture || !window->pixels)
    {
        window->dirty = false;
        return 1;
    }
    if (window->dirty)
    {
        SDL_UpdateTexture(window->texture, NULL, window->pixels,
                          (int)window->width * (int)sizeof(uint32_t));
        window->dirty = false;
    }
    SDL_RenderClear(window->renderer);
    SDL_RenderTexture(window->renderer, window->texture, NULL, NULL);
    SDL_RenderPresent(window->renderer);
    return 1;
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
    g_last_native_child_pid = pid;
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

static uint64_t native_execve_process(uint64_t path_ptr)
{
    if (!path_ptr)
    {
        return (uint64_t)-1;
    }

    const char *path = (const char *)(uintptr_t)path_ptr;
    if (strcmp(path, "/apps/system/shell.elf") != 0 || g_last_native_child_pid <= 0)
    {
        return (uint64_t)-1;
    }

    int status = 0;
    pid_t waited = waitpid(g_last_native_child_pid, &status, 0);
    if (waited == g_last_native_child_pid)
    {
        g_last_native_child_pid = 0;
        if (WIFEXITED(status))
        {
            g_exit_code = WEXITSTATUS(status);
        }
        else
        {
            g_exit_code = 1;
        }
        longjmp(g_exit_jmp, 1);
    }

    return (uint64_t)-1;
}

static uint64_t native_current_pid(void)
{
    return g_native_is_fork_child ? (uint64_t)getpid() : 1;
}

static void native_deliver_signal(uint32_t sig)
{
    if (sig >= NATIVE_MAX_SIGNALS)
    {
        return;
    }

    NativeSigAction *action = &g_native_sigactions[sig];
    if (action->handler == NATIVE_SIG_IGN)
    {
        return;
    }
    if (action->handler == 0)
    {
        if (g_native_is_fork_child)
        {
            _exit(128 + (int)sig);
        }
        g_exit_code = 128 + (int)sig;
        longjmp(g_exit_jmp, 1);
    }

    void (*handler)(int) = (void (*)(int))(uintptr_t)action->handler;
    handler((int)sig);
}

static void native_deliver_unblocked_pending(void)
{
    uint64_t deliverable = g_native_pending_signals & ~g_native_signal_mask;
    while (deliverable)
    {
        uint32_t sig = (uint32_t)__builtin_ctzll(deliverable);
        uint64_t bit = 1ULL << sig;
        g_native_pending_signals &= ~bit;
        native_deliver_signal(sig);
        deliverable = g_native_pending_signals & ~g_native_signal_mask;
    }
}

static uint64_t native_rt_sigaction(uint64_t sig_arg, uint64_t act_ptr,
                                    uint64_t oldact_ptr)
{
    if (sig_arg >= NATIVE_MAX_SIGNALS)
    {
        return (uint64_t)-EINVAL;
    }

    NativeSigAction *slot = &g_native_sigactions[sig_arg];
    if (oldact_ptr)
    {
        memcpy((void *)(uintptr_t)oldact_ptr, slot, sizeof(*slot));
    }
    if (act_ptr)
    {
        memcpy(slot, (const void *)(uintptr_t)act_ptr, sizeof(*slot));
    }
    return 0;
}

static uint64_t native_rt_sigprocmask(uint64_t how, uint64_t set_ptr,
                                      uint64_t oldset_ptr)
{
    if (oldset_ptr)
    {
        *(uint64_t *)(uintptr_t)oldset_ptr = g_native_signal_mask;
    }
    if (!set_ptr)
    {
        return 0;
    }

    uint64_t set = *(const uint64_t *)(uintptr_t)set_ptr;
    if (how == NATIVE_SIG_BLOCK)
    {
        g_native_signal_mask |= set;
    }
    else if (how == NATIVE_SIG_UNBLOCK)
    {
        g_native_signal_mask &= ~set;
        native_deliver_unblocked_pending();
    }
    else
    {
        return (uint64_t)-EINVAL;
    }
    return 0;
}

static uint64_t native_kill_process(uint64_t pid_arg, uint64_t sig_arg)
{
    if (sig_arg >= NATIVE_MAX_SIGNALS || pid_arg != native_current_pid())
    {
        return (uint64_t)-1;
    }

    uint64_t bit = 1ULL << sig_arg;
    if (g_native_signal_mask & bit)
    {
        g_native_pending_signals |= bit;
        return 0;
    }

    native_deliver_signal((uint32_t)sig_arg);
    return 0;
}

static uint64_t native_create_window(uint64_t handle_ptr, uint64_t xwindow_ptr)
{
    if (!handle_ptr || !xwindow_ptr)
    {
        return 0;
    }

    const uint8_t *xwindow = (const uint8_t *)(uintptr_t)xwindow_ptr;
    uint64_t *out_handle = (uint64_t *)(uintptr_t)handle_ptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t title_ptr = 0;
    uint8_t sets = 0;
    memcpy(&width, xwindow, sizeof(width));
    memcpy(&height, xwindow + 4, sizeof(height));
    memcpy(&title_ptr, xwindow + 8, sizeof(title_ptr));
    sets = xwindow[16];

    width = width ? width : 640U;
    height = height ? height : 480U;
    const char *title = title_ptr ? (const char *)(uintptr_t)title_ptr : "";
    if (width > 4096U || height > 4096U || (size_t)width > SIZE_MAX / (size_t)height
        || (size_t)width * (size_t)height > SIZE_MAX / sizeof(uint32_t))
    {
        *out_handle = 0;
        return 0;
    }
    bool sdl_available = native_init_sdl();

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
        window->width = width;
        window->height = height;
        window->sets = sets;
        window->title = title;
        SDL_WindowFlags sdl_flags = 0;
        if (window->sets & 1U)
        {
            sdl_flags |= SDL_WINDOW_FULLSCREEN;
        }
        if (window->sets & 2U)
        {
            sdl_flags |= SDL_WINDOW_BORDERLESS;
        }
        if (window->sets & 4U)
        {
            sdl_flags |= SDL_WINDOW_RESIZABLE;
        }
        window->pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
        if (!window->pixels)
        {
            native_destroy_window(window);
            *out_handle = 0;
            return 0;
        }
        if (sdl_available)
        {
            window->sdl_window = SDL_CreateWindow(window->title && window->title[0]
                                                      ? window->title
                                                      : "XJ380 Native",
                                                  (int)width, (int)height, sdl_flags);
            if (!window->sdl_window)
            {
                fprintf(stderr, "[native] SDL_CreateWindow failed: %s\n", SDL_GetError());
            }
            else
            {
                window->renderer = SDL_CreateRenderer(window->sdl_window, NULL);
                if (!window->renderer)
                {
                    fprintf(stderr, "[native] SDL_CreateRenderer failed: %s\n",
                            SDL_GetError());
                }
                else
                {
                    window->texture = SDL_CreateTexture(window->renderer,
                                                        SDL_PIXELFORMAT_ARGB8888,
                                                        SDL_TEXTUREACCESS_STREAMING,
                                                        (int)width, (int)height);
                    if (!window->texture)
                    {
                        fprintf(stderr, "[native] SDL_CreateTexture failed: %s\n",
                                SDL_GetError());
                    }
                }
            }
        }
        for (size_t p = 0; p < (size_t)width * (size_t)height; p++)
        {
            window->pixels[p] = 0xFF000000U;
        }
        window->dirty = true;
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

static uint64_t native_close_window(uint64_t handle)
{
    NativeWindow *window = native_find_window(handle);
    if (!window)
    {
        return 0;
    }
    native_destroy_window(window);
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
    if (window->sdl_window)
    {
        SDL_SetWindowTitle(window->sdl_window, window->title);
    }
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

static void native_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
    {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static uint64_t native_installer_enum_disks(uint64_t list_ptr)
{
    if (!list_ptr)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerDiskList *list = (NativeInstallerDiskList *)(uintptr_t)list_ptr;
    memset(list, 0, sizeof(*list));
    list->count = 1;
    list->disks[0].id = 0;
    list->disks[0].sector_size = 512;
    list->disks[0].size_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    list->disks[0].flags = XJ380_INSTALLER_DISK_FLAG_WRITABLE
        | XJ380_INSTALLER_DISK_FLAG_SECTOR_512;
    native_copy_text(list->disks[0].name, sizeof(list->disks[0].name),
                     "XSWL native simulated disk");
    return list->count;
}

static void native_installer_add_check(NativeInstallerPrecheck *check,
                                       uint32_t status, uint32_t code,
                                       const char *title, const char *detail)
{
    if (!check || check->item_count >= XJ380_INSTALLER_CHECK_ITEMS)
    {
        return;
    }

    NativeInstallerCheckItem *item = &check->items[check->item_count++];
    memset(item, 0, sizeof(*item));
    item->status = status;
    item->code = code;
    native_copy_text(item->title, sizeof(item->title), title);
    native_copy_text(item->detail, sizeof(item->detail), detail);
}

static int64_t native_installer_fill_precheck(uint32_t disk_id, uint32_t mode,
                                              uint64_t components,
                                              NativeInstallerPrecheck *check)
{
    if (!check)
    {
        return -EINVAL;
    }

    memset(check, 0, sizeof(*check));
    check->disk_id = disk_id;
    check->mode = mode;
    check->components = components ? components : XJ380_INSTALLER_COMPONENT_DEFAULT;
    check->payload_bytes = 0;
    check->required_bytes = 0;
    check->target_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    native_installer_add_check(check, XJ380_INSTALLER_CHECK_OK, 0,
                               "Native runtime",
                               "XSWL-C is running this installer UI outside XJ380.");
    native_installer_add_check(check, XJ380_INSTALLER_CHECK_ERROR, EROFS,
                               "Installation disabled",
                               "Native simulation never writes host disks.");
    check->can_continue = 0;
    return -EROFS;
}

static uint64_t native_installer_precheck(uint64_t disk_id, uint64_t mode,
                                          uint64_t out_ptr)
{
    if (!out_ptr || disk_id >= 256 || mode > XJ380_INSTALLER_MODE_DEVELOPER)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerPrecheck *out = (NativeInstallerPrecheck *)(uintptr_t)out_ptr;
    int64_t result = native_installer_fill_precheck((uint32_t)disk_id,
                                                    (uint32_t)mode,
                                                    XJ380_INSTALLER_COMPONENT_DEFAULT,
                                                    out);
    return (uint64_t)result;
}

static uint64_t native_installer_precheck_options(uint64_t options_ptr,
                                                  uint64_t out_ptr)
{
    if (!options_ptr || !out_ptr)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerStartOptions *options =
        (NativeInstallerStartOptions *)(uintptr_t)options_ptr;
    if (options->disk_id >= 256 || options->mode > XJ380_INSTALLER_MODE_DEVELOPER)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerPrecheck *out = (NativeInstallerPrecheck *)(uintptr_t)out_ptr;
    int64_t result = native_installer_fill_precheck(options->disk_id,
                                                    options->mode,
                                                    options->components,
                                                    out);
    return (uint64_t)result;
}

static void native_installer_set_failed_progress(int64_t result,
                                                 const char *stage,
                                                 const char *detail)
{
    memset(&g_native_installer_progress, 0, sizeof(g_native_installer_progress));
    g_native_installer_progress.state = XJ380_INSTALLER_FAILED;
    g_native_installer_progress.percent = 0;
    g_native_installer_progress.result = result;
    native_copy_text(g_native_installer_progress.stage,
                     sizeof(g_native_installer_progress.stage), stage);
    native_copy_text(g_native_installer_progress.detail,
                     sizeof(g_native_installer_progress.detail), detail);
}

static uint64_t native_installer_start(uint64_t disk_id, uint64_t mode,
                                       uint64_t components)
{
    (void)components;
    if (disk_id >= 256 || mode > XJ380_INSTALLER_MODE_DEVELOPER)
    {
        return (uint64_t)-EINVAL;
    }

    native_installer_set_failed_progress(-EROFS, "Installation disabled",
                                         "XSWL-C native mode does not write host disks.");
    return (uint64_t)-EROFS;
}

static uint64_t native_installer_start_options(uint64_t options_ptr)
{
    if (!options_ptr)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerStartOptions *options =
        (NativeInstallerStartOptions *)(uintptr_t)options_ptr;
    return native_installer_start(options->disk_id, options->mode,
                                  options->components);
}

static uint64_t native_installer_progress(uint64_t progress_ptr)
{
    if (!progress_ptr)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerProgress *progress =
        (NativeInstallerProgress *)(uintptr_t)progress_ptr;
    if (g_native_installer_progress.state == 0
        && g_native_installer_progress.result == 0
        && g_native_installer_progress.stage[0] == '\0')
    {
        memset(progress, 0, sizeof(*progress));
        progress->state = XJ380_INSTALLER_IDLE;
        native_copy_text(progress->stage, sizeof(progress->stage), "Idle");
        native_copy_text(progress->detail, sizeof(progress->detail),
                         "No native installer task is running.");
        return 0;
    }

    memcpy(progress, &g_native_installer_progress, sizeof(*progress));
    return 0;
}

static void native_installer_add_rescue(NativeInstallerRescueResult *result,
                                        uint32_t status, uint32_t code,
                                        const char *title, const char *detail)
{
    if (!result || result->item_count >= XJ380_INSTALLER_RESCUE_ITEMS)
    {
        return;
    }

    NativeInstallerRescueItem *item = &result->items[result->item_count++];
    memset(item, 0, sizeof(*item));
    item->status = status;
    item->code = code;
    native_copy_text(item->title, sizeof(item->title), title);
    native_copy_text(item->detail, sizeof(item->detail), detail);
}

static uint64_t native_installer_rescue(uint64_t action, uint64_t disk_id,
                                        uint64_t out_ptr)
{
    if (!out_ptr || disk_id >= 256)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerRescueResult *result =
        (NativeInstallerRescueResult *)(uintptr_t)out_ptr;
    memset(result, 0, sizeof(*result));

    if (action == XJ380_INSTALLER_RESCUE_VIEW_LOG)
    {
        native_installer_add_rescue(result, XJ380_INSTALLER_CHECK_OK, 0,
                                    "Native installer log",
                                    "Use xapi_InstallerLog for the simulated log.");
        return 0;
    }
    if (action == XJ380_INSTALLER_RESCUE_VIEW_DISK)
    {
        native_installer_add_rescue(result, XJ380_INSTALLER_CHECK_WARN, 0,
                                    "Simulated disk",
                                    "The disk exists only for UI compatibility.");
        return 0;
    }
    if (action == XJ380_INSTALLER_RESCUE_CHECK_DISK)
    {
        native_installer_add_rescue(result, XJ380_INSTALLER_CHECK_ERROR, EROFS,
                                    "Installation disabled",
                                    "Native simulation never writes host disks.");
        result->result = -EROFS;
        return (uint64_t)result->result;
    }
    if (action == XJ380_INSTALLER_RESCUE_OPEN_TERM)
    {
        native_installer_add_rescue(result, XJ380_INSTALLER_CHECK_WARN, ENOSYS,
                                    "Terminal unavailable",
                                    "Native installer rescue does not spawn a shell.");
        result->result = -ENOSYS;
        return (uint64_t)result->result;
    }
    if (action == XJ380_INSTALLER_RESCUE_REBUILD_BOOT)
    {
        native_installer_add_rescue(result, XJ380_INSTALLER_CHECK_ERROR, EROFS,
                                    "Boot repair disabled",
                                    "Native simulation never writes boot files.");
        result->result = -EROFS;
        return (uint64_t)result->result;
    }

    native_installer_add_rescue(result, XJ380_INSTALLER_CHECK_ERROR, EINVAL,
                                "Unknown rescue action",
                                "The native runtime does not support this action.");
    result->result = -EINVAL;
    return (uint64_t)result->result;
}

static uint64_t native_installer_log(uint64_t log_ptr)
{
    if (!log_ptr)
    {
        return (uint64_t)-EINVAL;
    }

    NativeInstallerLog *log = (NativeInstallerLog *)(uintptr_t)log_ptr;
    memset(log, 0, sizeof(*log));
    log->count = 3;
    native_copy_text(log->lines[0], sizeof(log->lines[0]),
                     "XSWL-C native installer compatibility mode.");
    native_copy_text(log->lines[1], sizeof(log->lines[1]),
                     "Disk writes are intentionally disabled.");
    native_copy_text(log->lines[2], sizeof(log->lines[2]),
                     "Run under XJ380 for real installation behavior.");
    return 0;
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
    return native_present_window(window);
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

static uint64_t native_write_buffer(uint64_t handle, uint64_t x, uint64_t y,
                                    uint64_t width, uint64_t height,
                                    uint64_t buffer_ptr,
                                    size_t bytes_per_pixel)
{
    NativeWindow *window = native_find_window(handle);
    if (!window || !buffer_ptr || !window->pixels || width == 0 || height == 0
        || x >= window->width || y >= window->height)
    {
        return 0;
    }

    uint64_t copy_width = width;
    uint64_t copy_height = height;
    if (copy_width > (uint64_t)window->width - x)
    {
        copy_width = (uint64_t)window->width - x;
    }
    if (copy_height > (uint64_t)window->height - y)
    {
        copy_height = (uint64_t)window->height - y;
    }
    if (copy_width > (uint64_t)SIZE_MAX || copy_height > (uint64_t)SIZE_MAX
        || (size_t)width > SIZE_MAX / bytes_per_pixel)
    {
        return 0;
    }

    const uint8_t *src = (const uint8_t *)(uintptr_t)buffer_ptr;
    size_t src_stride = (size_t)width * bytes_per_pixel;
    for (uint64_t row = 0; row < copy_height; row++)
    {
        uint32_t *dst = window->pixels
            + ((size_t)y + (size_t)row) * (size_t)window->width
            + (size_t)x;
        const uint8_t *src_row = src + (size_t)row * src_stride;
        for (uint64_t col = 0; col < copy_width; col++)
        {
            const uint8_t *px = src_row + (size_t)col * bytes_per_pixel;
            uint8_t red = px[0];
            uint8_t green = px[1];
            uint8_t blue = px[2];
            uint8_t alpha = bytes_per_pixel == 4U ? px[3] : 255U;
            if (alpha == 255U)
            {
                dst[col] = 0xFF000000U
                    | ((uint32_t)red << 16)
                    | ((uint32_t)green << 8)
                    | (uint32_t)blue;
            }
            else if (alpha != 0U)
            {
                uint32_t old = dst[col];
                uint8_t old_r = (uint8_t)((old >> 16) & 0xFFU);
                uint8_t old_g = (uint8_t)((old >> 8) & 0xFFU);
                uint8_t old_b = (uint8_t)(old & 0xFFU);
                uint8_t out_r = (uint8_t)(((uint32_t)red * alpha
                    + (uint32_t)old_r * (255U - alpha)) / 255U);
                uint8_t out_g = (uint8_t)(((uint32_t)green * alpha
                    + (uint32_t)old_g * (255U - alpha)) / 255U);
                uint8_t out_b = (uint8_t)(((uint32_t)blue * alpha
                    + (uint32_t)old_b * (255U - alpha)) / 255U);
                dst[col] = 0xFF000000U
                    | ((uint32_t)out_r << 16)
                    | ((uint32_t)out_g << 8)
                    | (uint32_t)out_b;
            }
        }
    }
    window->dirty = true;
    window->draw_count++;
    return 1;
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
        return native_sys_write(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_OPEN)
    {
        return native_sys_open(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_CLOSE)
    {
        return native_sys_close(arg1);
    }
    if (syscall_no == SYS_STAT)
    {
        return native_sys_stat(arg1, arg2);
    }
    if (syscall_no == SYS_FSTAT)
    {
        return native_sys_fstat(arg1, arg2);
    }
    if (syscall_no == SYS_LSEEK)
    {
        return native_sys_lseek(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_MMAP)
    {
        return native_sys_mmap(arg1, arg2, arg3, arg4, arg5, arg6);
    }
    if (syscall_no == SYS_MPROTECT)
    {
        return mprotect((void *)(uintptr_t)arg1, (size_t)arg2, (int)arg3) == 0
            ? 0
            : (uint64_t)-1;
    }
    if (syscall_no == SYS_MUNMAP)
    {
        return munmap((void *)(uintptr_t)arg1, (size_t)arg2) == 0
            ? 0
            : (uint64_t)-1;
    }
    if (syscall_no == SYS_RT_SIGACTION)
    {
        return native_rt_sigaction(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_RT_SIGPROCMASK)
    {
        return native_rt_sigprocmask(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_RT_SIGRETURN)
    {
        return 0;
    }
    if (syscall_no == SYS_IOCTL)
    {
        return native_sys_ioctl(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_WRITEV)
    {
        return native_sys_writev(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_PIPE)
    {
        return native_sys_pipe(arg1);
    }
    if (syscall_no == SYS_SELECT)
    {
        return native_sys_select(arg1, arg2, arg3, arg4, arg5);
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
        return native_current_pid();
    }
    if (syscall_no == SYS_EXECVE)
    {
        return native_execve_process(arg1);
    }
    if (syscall_no == SYS_KILL)
    {
        return native_kill_process(arg1, arg2);
    }
    if (syscall_no == SYS_SOCKET)
    {
        return (uint64_t)-1;
    }
    if (syscall_no == SYS_GETCWD)
    {
        const char cwd[] = "/";
        if (!arg1 || arg2 < sizeof(cwd))
        {
            return (uint64_t)-1;
        }
        memcpy((void *)(uintptr_t)arg1, cwd, sizeof(cwd));
        return arg1;
    }
    if (syscall_no == SYS_CHDIR)
    {
        return 0;
    }
    if (syscall_no == SYS_GETDENTS)
    {
        return native_sys_getdents(arg1, arg2, arg3);
    }
    if (syscall_no == SYS_UNLINK)
    {
        return native_sys_unlink(arg1);
    }
    if (syscall_no == SYS_UTIMENSAT)
    {
        return 0;
    }
    if (syscall_no == SYS_GETUID || syscall_no == SYS_GETGID
        || syscall_no == SYS_GETEUID || syscall_no == SYS_GETEGID)
    {
        return 0;
    }
    if (syscall_no == SYS_GETGROUPS)
    {
        if (arg1 > 0 && arg2)
        {
            *(uint32_t *)(uintptr_t)arg2 = 0;
            return 1;
        }
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
    if (syscall_no == XAPI_INPUT || syscall_no == XAPI_GETLINE)
    {
        if (arg1)
        {
            ((char *)(uintptr_t)arg1)[0] = '\0';
        }
        return 0;
    }
    if (syscall_no == XAPI_GETCH)
    {
        return 0;
    }
    if (syscall_no == XAPI_ENDLINE)
    {
        fputc('\n', stdout);
        fflush(stdout);
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
        return native_execve_process(arg1);
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
    if (syscall_no == XAPI_CLOSE_WINDOW)
    {
        return native_close_window(arg1);
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
    if (syscall_no == XAPI_GET_TIME)
    {
        return (uint64_t)time(NULL);
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
        return native_write_buffer(arg1, arg2, arg3, arg4, arg5, arg6,
                                   syscall_no == XAPI_WRITE_BUFFER_A ? 4U : 3U);
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
        native_poll_sdl_events();
        return 0;
    }
    if (syscall_no == XAPI_SLEEP)
    {
        native_poll_sdl_events();
        uint64_t usec = arg1 > UINT64_MAX / 1000ULL ? UINT64_MAX : arg1 * 1000ULL;
        if (usec > 1000000ULL)
        {
            usec = 1000000ULL;
        }
        usleep((useconds_t)usec);
        native_poll_sdl_events();
        return 0;
    }
    if (syscall_no == XAPI_USER_OOBE_REQUIRED)
    {
        return 0;
    }
    if (syscall_no == SXAH_CHECK_TERMINAL_INIT_STATUS)
    {
        return 1;
    }
    if (syscall_no == SXAH_MARK_IS_TERMINAL)
    {
        return 0;
    }
    if (syscall_no == SXAH_READ_OUTPUT_BUFFER)
    {
        if (arg1)
        {
            ((char *)(uintptr_t)arg1)[0] = '\0';
        }
        return 0;
    }
    if (syscall_no == SXAH_WRITE_INPUT_BUFFER
        || syscall_no == SXAH_UNLOCK_OUTPUT_LOCK)
    {
        return 0;
    }
    if (syscall_no == SXAH_CHECK_INPUT_BUFFER || syscall_no == SXAH_MESSAGE_ASK)
    {
        return 0;
    }
    if (syscall_no == SXAH_INSTALLER_ENUM_DISKS)
    {
        return native_installer_enum_disks(arg1);
    }
    if (syscall_no == SXAH_INSTALLER_START)
    {
        return native_installer_start(arg1, 0, XJ380_INSTALLER_COMPONENT_DEFAULT);
    }
    if (syscall_no == SXAH_INSTALLER_PROGRESS)
    {
        return native_installer_progress(arg1);
    }
    if (syscall_no == SXAH_INSTALLER_PRECHECK)
    {
        return native_installer_precheck(arg1, arg2, arg3);
    }
    if (syscall_no == SXAH_INSTALLER_START_EX)
    {
        return native_installer_start(arg1, arg2, XJ380_INSTALLER_COMPONENT_DEFAULT);
    }
    if (syscall_no == SXAH_INSTALLER_RESCUE)
    {
        return native_installer_rescue(arg1, arg2, arg3);
    }
    if (syscall_no == SXAH_INSTALLER_LOG)
    {
        return native_installer_log(arg1);
    }
    if (syscall_no == SXAH_INSTALLER_START_OPTIONS)
    {
        return native_installer_start_options(arg1);
    }
    if (syscall_no == SXAH_INSTALLER_PRECHECK_OPTIONS)
    {
        return native_installer_precheck_options(arg1, arg2);
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
    g_last_native_child_pid = 0;
    native_set_app_dir(path);
    g_brk_addr = 0;
    g_brk_map_end = 0;
    memset(g_native_windows, 0, sizeof(g_native_windows));
    g_next_window_handle = 1;
    memset(g_native_fds, 0, sizeof(g_native_fds));
    memset(&g_native_installer_progress, 0, sizeof(g_native_installer_progress));
    memset(g_native_sigactions, 0, sizeof(g_native_sigactions));
    g_native_signal_mask = 0;
    g_native_pending_signals = 0;
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

    native_cleanup_windows();
    return g_exit_code;
}
