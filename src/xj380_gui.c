/*
 * xj380_gui.c — XJ380 GUI 后端 (SDL3)
 *
 * 实现 XJ380 API 手册 Chapter 4 的图形化函数:
 *   窗口管理, 绘图, 图片, framebuffer, 控件, 消息处理
 *
 * 线程模型:
 *   模拟器主线程在 uc_emu_start() 阻塞执行, 但 SDL3 需要在主线程处理事件。
 *   方案: 在 xj380_run 中用 SDL_PollEvent 交替驱动 SDL3 和 Unicorn。
 *   当 xapi_CreateWindow 被调用时, 创建 SDL 窗口并切换到事件驱动模式。
 */

#include "xj380_emu.h"
#include "xj380_gui.h"

#include <SDL3/SDL.h>
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wconversion"
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"
#pragma GCC diagnostic pop

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <stdarg.h>
#include <strings.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ================================================================
 * GUI 窗口结构
 * ================================================================ */
#define MAX_GUI_WINDOWS 16
#define FB_MAX_WIDTH    1920
#define FB_MAX_HEIGHT   1080
#define KEY_STATE_COUNT 512

enum {
    XJ380_XKEY_ESC = 128,
    XJ380_XKEY_BACKSPACE = 129,
    XJ380_XKEY_TAB = 130,
    XJ380_XKEY_ENTER = 131,
    XJ380_XKEY_CAPS = 132,
    XJ380_XKEY_SHIFT = 133,
    XJ380_XKEY_CTRL = 134,
    XJ380_XKEY_ALT = 135,
    XJ380_XKEY_SPACE = 136,
    XJ380_XKEY_F1 = 137,
    XJ380_XKEY_F2 = 138,
    XJ380_XKEY_F3 = 139,
    XJ380_XKEY_F4 = 140,
    XJ380_XKEY_F5 = 141,
    XJ380_XKEY_F6 = 142,
    XJ380_XKEY_F7 = 143,
    XJ380_XKEY_F8 = 144,
    XJ380_XKEY_F9 = 145,
    XJ380_XKEY_F10 = 146,
    XJ380_XKEY_F11 = 147,
    XJ380_XKEY_F12 = 148,
    XJ380_XKEY_NUML = 149,
    XJ380_XKEY_SCROLL = 150,
    XJ380_XKEY_HOME = 151,
    XJ380_XKEY_UP = 152,
    XJ380_XKEY_PAGE_UP = 153,
    XJ380_XKEY_LEFT = 154,
    XJ380_XKEY_RIGHT = 155,
    XJ380_XKEY_END = 156,
    XJ380_XKEY_DOWN = 157,
    XJ380_XKEY_PAGE_DOWN = 158,
    XJ380_XKEY_INSERT = 159,
    XJ380_XKEY_DELETE = 160
};

typedef struct {
    HDLE    handle;
    SDL_Window   *win;
    SDL_Renderer *rend;
    SDL_Texture  *tex;
    uint32_t     *fb;          /* RGBA framebuffer (host memory) */
    int           width;
    int           height;
    char          title[256];
    bool          open;
    bool          fullscreen;
    bool          resizable;
    bool          dirty;
    SDL_Rect      dirty_rect;
    bool          suppress_textinput;
    uint64_t      suppress_textinput_value;
    uint8_t       key_pressed_values[KEY_STATE_COUNT];

    /* 控件 */
    struct {
        uint64_t id;
        int      x, y, w, h;
        char     text[256];
        bool     emphasis;     /* 蓝色强调按钮 */
        bool     alive;
    } buttons[64];
    int button_count;

    /* 右键菜单 */
    struct {
        bool     registered;
        uint64_t items[16];   /* CRLid */
        char     item_text[16][64];
        int      item_count;
    } rbutton_menu;

    /* 消息回调 (在模拟器内) */
    uint64_t msg_callback_addr;  /* SetMsgPrcor 设置的函数地址 */

} gui_window_t;

static gui_window_t *g_windows[MAX_GUI_WINDOWS];
static int            g_window_count;
static uint64_t       g_next_handle = 1;
static int            g_event_head;
static int            g_event_tail;
static bool           g_sdl_initialized;
static bool           g_gui_debug_enabled = true;
static bool           g_test_events_enabled;
static bool           g_test_events_pushed;

static void gui_log(const char *fmt, ...);

/* ---- TTF 字体缓存 ---- */
#define MAX_FONTS 8
typedef struct {
    char           path[512];
    stbtt_fontinfo info;
    uint8_t       *data;
    size_t         size;
    float          scale;
} cached_font_t;

static cached_font_t g_fonts[MAX_FONTS];
static int           g_font_count;

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void load_host_font_if_present(const char *vpath, const char *hpath)
{
    if (g_font_count >= MAX_FONTS)
    {
        return;
    }

    FILE *fp = fopen(hpath, "rb");
    if (!fp)
    {
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return;
    }
    long len = ftell(fp);
    if (len <= 0)
    {
        fclose(fp);
        return;
    }
    rewind(fp);

    uint8_t *data = malloc((size_t)len);
    if (!data)
    {
        fclose(fp);
        return;
    }

    size_t got = fread(data, 1, (size_t)len, fp);
    fclose(fp);
    if (got != (size_t)len)
    {
        free(data);
        return;
    }

    cached_font_t *cf = &g_fonts[g_font_count];
    memset(cf, 0, sizeof(*cf));
    copy_cstr(cf->path, sizeof(cf->path), vpath);
    cf->data = data;
    cf->size = (size_t)len;
    if (!stbtt_InitFont(&cf->info, cf->data, stbtt_GetFontOffsetForIndex(cf->data, 0)))
    {
        free(cf->data);
        memset(cf, 0, sizeof(*cf));
        return;
    }
    cf->scale = stbtt_ScaleForPixelHeight(&cf->info, 16.0f);
    g_font_count++;
    gui_log("[GUI] loaded host font: %s -> %s (%zu bytes)\n", hpath, vpath, (size_t)len);
}

static void load_default_host_fonts_once(void)
{
    static bool attempted = false;
    if (attempted)
    {
        return;
    }
    attempted = true;

    const char *root = getenv("XSWL_XJ380_ROOT");
    if (root && *root)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/font/ttf/XJ380C.ttf", root);
        load_host_font_if_present("/system/font/XJ380C.ttf", path);
        snprintf(path, sizeof(path), "%s/font/ttf/XJ380F.ttf", root);
        load_host_font_if_present("/system/font/XJ380F.ttf", path);
    }

    load_host_font_if_present("/system/font/XJ380C.ttf", "../XJ380/font/ttf/XJ380C.ttf");
    load_host_font_if_present("/system/font/XJ380F.ttf", "../XJ380/font/ttf/XJ380F.ttf");
    load_host_font_if_present("/system/font/XJ380C.ttf",
                              "/home/Bnear8273/Projects/XJ380/font/ttf/XJ380C.ttf");
    load_host_font_if_present("/system/font/XJ380F.ttf",
                              "/home/Bnear8273/Projects/XJ380/font/ttf/XJ380F.ttf");
}

static cached_font_t* find_font(const char *path)
{
    if (g_font_count == 0)
    {
        load_default_host_fonts_once();
    }

    for (int i = 0; i < g_font_count; i++)
    {
        if (strcmp(g_fonts[i].path, path) == 0)
        {
            return &g_fonts[i];
        }
    }

    return g_font_count > 0 ? &g_fonts[0] : NULL;
}

static SDL_Surface *load_image_surface(xj380_emu_t *emu, const char *path)
{
    const uint8_t *data = NULL;
    size_t size = 0;

    if (xj380_vfs_read_file(emu, path, &data, &size) == 0 && data && size > 0)
    {
        SDL_IOStream *io = SDL_IOFromConstMem(data, size);
        if (io)
        {
            SDL_Surface *surf = SDL_LoadSurface_IO(io, true);
            if (surf)
            {
                return surf;
            }
        }
    }

    SDL_Surface *surf = SDL_LoadSurface(path);
    if (!surf && path[0] == '/')
    {
        surf = SDL_LoadSurface(path + 1);
    }

    return surf;
}



/* ================================================================
 * SDL3 初始化
 * ================================================================ */
void xj380_gui_set_debug(bool enabled)
{
    g_gui_debug_enabled = enabled;
}

static void gui_log(const char *fmt, ...)
{
    if (!g_gui_debug_enabled)
    {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int xj380_gui_init(void)
{
    if (g_sdl_initialized) return 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "[GUI] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    g_test_events_enabled = getenv("XSWL_TEST_GUI_EVENTS") != NULL;
    g_test_events_pushed = false;

    g_sdl_initialized = true;
    gui_log("[GUI] SDL3 initialized\n");
    return 0;
}

void xj380_gui_cleanup(void)
{
    for (int i = 0; i < g_window_count; i++) {
        gui_window_t *gw = g_windows[i];
        if (!gw) continue;
        if (gw->fb)    free(gw->fb);
        if (gw->tex)   SDL_DestroyTexture(gw->tex);
        if (gw->rend)  SDL_DestroyRenderer(gw->rend);
        if (gw->win) {
            SDL_StopTextInput(gw->win);
            SDL_DestroyWindow(gw->win);
        }
        free(gw);
        g_windows[i] = NULL;
    }
    g_window_count = 0;
    g_event_head = 0;
    g_event_tail = 0;
    g_test_events_pushed = false;

    if (g_sdl_initialized) {
        SDL_Quit();
        g_sdl_initialized = false;
    }
}

/* ================================================================
 * 窗口管理
 * ================================================================ */

static gui_window_t* find_window(uint64_t handle)
{
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i]->handle == handle) return g_windows[i];
    }
    return NULL;
}

static void write_null_handle(xj380_emu_t *emu, uint64_t handle_ptr)
{
    uint64_t zero = 0;
    if (handle_ptr)
    {
        (void)xj380_mem_write(emu, handle_ptr, &zero, sizeof(zero));
    }
}

/* xapi_CreateWindow: HDLE*, XWINDOW* → 创建 SDL 窗口 */
void xj380_gui_create_window(xj380_emu_t *emu, uint64_t handle_ptr, uint64_t xwin_ptr)
{
    if (g_window_count >= MAX_GUI_WINDOWS)
    {
        write_null_handle(emu, handle_ptr);
        return;
    }

    if (xj380_gui_init() != 0)
    {
        write_null_handle(emu, handle_ptr);
        return;
    }

    /* 读 XWINDOW 结构 */
    uint32_t width  = 0, height = 0;
    UINT8    sets   = 0;
    char     title[256] = {0};

    /* 手动从模拟器内存读取 (XWINDOW = {UINT32 width, UINT32 height, WSTR title, UINT8 sets}) */
    /* WSTR 是指针 (8 bytes in x86_64), 所以 XWINDOW 布局:
     *   +0:  width  (4 bytes)
     *   +4:  height (4 bytes)
     *   +8:  title  (8 bytes pointer)
     *   +16: sets   (1 byte)
     *   (实际可能有填充, 先按 packed 处理)
     */

    /* 简化: 直接读裸字节 */
    uint8_t xwin_raw[32] = {0};
    if (!xwin_ptr || xj380_mem_read(emu, xwin_ptr, xwin_raw, sizeof(xwin_raw)) != 0)
    {
        write_null_handle(emu, handle_ptr);
        return;
    }
    memcpy(&width,  xwin_raw,      4);
    memcpy(&height, xwin_raw + 4,  4);
    uint64_t title_ptr = 0;
    memcpy(&title_ptr, xwin_raw + 8, 8);
    sets = xwin_raw[16];

    if (title_ptr && xj380_mem_read_str(emu, title_ptr, title, sizeof(title)) != 0)
    {
        title[0] = 0;
    }

    if (width == 0)   width  = 640;
    if (height == 0)  height = 480;
    if (width > FB_MAX_WIDTH || height > FB_MAX_HEIGHT)
    {
        write_null_handle(emu, handle_ptr);
        return;
    }
    if (title[0] == 0) strcpy(title, "XJ380 Application");

    /* 创建 SDL 窗口 */
    SDL_WindowFlags sdl_flags = 0;
    if (sets & XWIN_FULL_SCR) {
        sdl_flags |= SDL_WINDOW_FULLSCREEN;
    } else if (sets & XWIN_FRAME_OFF) {
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    }
    if (sets & XWIN_SUPPORT_RESIZEABLE) {
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window *win = SDL_CreateWindow(title, (int)width, (int)height, sdl_flags);

    if (!win) {
        fprintf(stderr, "[GUI] SDL_CreateWindow failed: %s\n", SDL_GetError());
        /* 写 NULL handle */
        write_null_handle(emu, handle_ptr);
        return;
    }

    SDL_Renderer *rend = SDL_CreateRenderer(win, NULL);
    if (!rend)
    {
        fprintf(stderr, "[GUI] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        write_null_handle(emu, handle_ptr);
        return;
    }
    const char *vsync_env = getenv("XSWL_VSYNC");
    if (vsync_env && strcmp(vsync_env, "0") != 0)
    {
        SDL_SetRenderVSync(rend, 1);
    }

    SDL_Texture *tex = SDL_CreateTexture(rend,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        (int)width, (int)height);
    if (!tex)
    {
        fprintf(stderr, "[GUI] SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        write_null_handle(emu, handle_ptr);
        return;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (width != 0 && pixel_count / (size_t)width != (size_t)height)
    {
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        write_null_handle(emu, handle_ptr);
        return;
    }

    uint32_t *fb = calloc(pixel_count, sizeof(uint32_t));
    if (!fb)
    {
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        write_null_handle(emu, handle_ptr);
        return;
    }

    for (size_t i = 0; i < pixel_count; i++)
    {
        fb[i] = 0xFFFFFFFFU;
    }

    gui_window_t *gw = calloc(1, sizeof(*gw));
    if (!gw)
    {
        free(fb);
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        write_null_handle(emu, handle_ptr);
        return;
    }
    gw->handle     = g_next_handle++;
    gw->win        = win;
    gw->rend       = rend;
    gw->tex        = tex;
    gw->fb         = fb;
    gw->width      = (int)width;
    gw->height     = (int)height;
    gw->open       = true;
    gw->fullscreen = (sets & XWIN_FULL_SCR) != 0;
    gw->resizable  = (sets & XWIN_SUPPORT_RESIZEABLE) != 0;
    gw->dirty      = true;
    gw->dirty_rect = (SDL_Rect){0, 0, gw->width, gw->height};
    copy_cstr(gw->title, sizeof(gw->title), title);
    SDL_StartTextInput(win);

    g_windows[g_window_count++] = gw;

    /* 写 handle 回模拟器内存 */
    if (handle_ptr)
    {
        (void)xj380_mem_write(emu, handle_ptr, &gw->handle, 8);
    }

    gui_log("[GUI] created window #%llu: %dx%d \"%s\"\n",
           (unsigned long long)gw->handle, gw->width, gw->height, gw->title);
}

/* xapi_CloseWindow */
void xj380_gui_close_window(xj380_emu_t *emu, uint64_t handle)
{
    (void)emu;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i]->handle == handle) {
            gui_window_t *gw = g_windows[i];
            gw->open = false;
            if (gw->fb)   free(gw->fb);
            if (gw->tex)  SDL_DestroyTexture(gw->tex);
            if (gw->rend) SDL_DestroyRenderer(gw->rend);
            if (gw->win) {
                SDL_StopTextInput(gw->win);
                SDL_DestroyWindow(gw->win);
            }
            free(gw);
            /* 从数组中移除 */
            g_windows[i] = g_windows[--g_window_count];
            g_windows[g_window_count] = NULL;
            gui_log("[GUI] closed window #%llu\n", (unsigned long long)handle);
            return;
        }
    }
}

/* xapi_SetWindowTitle */
void xj380_gui_set_window_title(xj380_emu_t *emu, uint64_t handle, uint64_t title_ptr)
{
    char title[256] = {0};
    if (title_ptr && xj380_mem_read_str(emu, title_ptr, title, sizeof(title)) != 0)
    {
        return;
    }

    gui_window_t *gw = find_window(handle);
    if (gw) {
        copy_cstr(gw->title, sizeof(gw->title), title);
        SDL_SetWindowTitle(gw->win, title);
    }
}

/* ================================================================
 * 绘图函数
 * ================================================================ */

static bool clip_rect(gui_window_t *gw, uint32_t x, uint32_t y,
    uint32_t w, uint32_t h, SDL_Rect *rect)
{
    uint32_t max_w  = 0;
    uint32_t max_h  = 0;
    uint32_t copy_w = 0;
    uint32_t copy_h = 0;

    if (!gw || !rect || w == 0 || h == 0 || x >= (uint32_t)gw->width
        || y >= (uint32_t)gw->height)
    {
        return false;
    }

    max_w  = (uint32_t)gw->width - x;
    max_h  = (uint32_t)gw->height - y;
    copy_w = (w < max_w) ? w : max_w;
    copy_h = (h < max_h) ? h : max_h;

    if (copy_w == 0 || copy_h == 0)
    {
        return false;
    }

    *rect = (SDL_Rect){(int)x, (int)y, (int)copy_w, (int)copy_h};
    return true;
}

static uint32_t rgba_to_argb(uint32_t rgba)
{
    return ((rgba & 0x000000FFU) << 24) | ((rgba & 0xFFFFFF00U) >> 8);
}

static uint32_t argb_to_rgba(uint32_t argb)
{
    return ((argb & 0x00FFFFFFU) << 8) | ((argb & 0xFF000000U) >> 24);
}

static bool utf8_next_codepoint(const char *text, const char *p, int *codepoint, int *len)
{
    size_t remaining = strlen(p);
    unsigned char c0 = (unsigned char)p[0];

    (void)text;
    if (!codepoint || !len || remaining == 0)
    {
        return false;
    }

    if ((c0 & 0x80U) == 0)
    {
        *codepoint = c0;
        *len = 1;
        return true;
    }

    if ((c0 & 0xE0U) == 0xC0U)
    {
        if (remaining < 2 || (((unsigned char)p[1] & 0xC0U) != 0x80U)) return false;
        *codepoint = ((int)(c0 & 0x1FU) << 6) | (int)((unsigned char)p[1] & 0x3FU);
        *len = 2;
        return true;
    }

    if ((c0 & 0xF0U) == 0xE0U)
    {
        if (remaining < 3 || (((unsigned char)p[1] & 0xC0U) != 0x80U)
            || (((unsigned char)p[2] & 0xC0U) != 0x80U)) return false;
        *codepoint = ((int)(c0 & 0x0FU) << 12)
                   | ((int)((unsigned char)p[1] & 0x3FU) << 6)
                   |  (int)((unsigned char)p[2] & 0x3FU);
        *len = 3;
        return true;
    }

    if ((c0 & 0xF8U) == 0xF0U)
    {
        if (remaining < 4 || (((unsigned char)p[1] & 0xC0U) != 0x80U)
            || (((unsigned char)p[2] & 0xC0U) != 0x80U)
            || (((unsigned char)p[3] & 0xC0U) != 0x80U)) return false;
        *codepoint = ((int)(c0 & 0x07U) << 18)
                   | ((int)((unsigned char)p[1] & 0x3FU) << 12)
                   | ((int)((unsigned char)p[2] & 0x3FU) << 6)
                   |  (int)((unsigned char)p[3] & 0x3FU);
        *len = 4;
        return true;
    }

    return false;
}

static void mark_dirty(gui_window_t *gw, int x, int y, int w, int h)
{
    SDL_Rect clipped;

    if (!clip_rect(gw, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, &clipped))
    {
        return;
    }

    if (!gw->dirty)
    {
        gw->dirty      = true;
        gw->dirty_rect = clipped;
        return;
    }

    SDL_GetRectUnion(&gw->dirty_rect, &clipped, &gw->dirty_rect);
}

static void set_pixel_raw(gui_window_t *gw, int x, int y, uint32_t rgba)
{
    if (x < 0 || y < 0 || x >= gw->width || y >= gw->height)
    {
        return;
    }

    gw->fb[y * gw->width + x] = rgba;
}

static void set_pixel(gui_window_t *gw, int x, int y, uint32_t rgba)
{
    set_pixel_raw(gw, x, y, rgba);
    mark_dirty(gw, x, y, 1, 1);
}

void xj380_gui_draw_point(xj380_emu_t *emu, uint64_t handle,
                          uint32_t x, uint32_t y, uint32_t rgba)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (gw) set_pixel(gw, (int)x, (int)y, rgba_to_argb(rgba));
}

void xj380_gui_draw_line(xj380_emu_t *emu, uint64_t handle,
    uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t rgba)
{
    rgba = rgba_to_argb(rgba);
    (void)emu;
    gui_window_t *gw = find_window(handle);

    if (!gw)
    {
        return;
    }

    int dx  = abs((int)x2 - (int)x1);
    int dy  = -abs((int)y2 - (int)y1);
    int sx  = x1 < x2 ? 1 : -1;
    int sy  = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int cx  = (int)x1;
    int cy  = (int)y1;

    for (;;)
    {
        set_pixel_raw(gw, cx, cy, rgba);
        if (cx == (int)x2 && cy == (int)y2)
        {
            break;
        }

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            cx += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            cy += sy;
        }
    }

    uint32_t lx = x1 < x2 ? x1 : x2;
    uint32_t rx = x1 > x2 ? x1 : x2;
    uint32_t ty = y1 < y2 ? y1 : y2;
    uint32_t by = y1 > y2 ? y1 : y2;
    mark_dirty(gw, (int)lx, (int)ty, (int)(rx - lx + 1U), (int)(by - ty + 1U));
}

void xj380_gui_draw_rect(xj380_emu_t *emu, uint64_t handle,
    uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
    uint32_t rgba, int fill)
{
    rgba = rgba_to_argb(rgba);
    (void)emu;
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!gw)
    {
        return;
    }

    uint32_t lx = x1 < x2 ? x1 : x2;
    uint32_t rx = x1 > x2 ? x1 : x2;
    uint32_t ty = y1 < y2 ? y1 : y2;
    uint32_t by = y1 > y2 ? y1 : y2;

    if (!clip_rect(gw, lx, ty, rx - lx + 1U, by - ty + 1U, &rect))
    {
        return;
    }

    if (fill)
    {
        for (int row = 0; row < rect.h; row++)
        {
            uint32_t *dst = gw->fb + (rect.y + row) * gw->width + rect.x;
            for (int col = 0; col < rect.w; col++)
            {
                dst[col] = rgba;
            }
        }
    }
    else
    {
        int left   = rect.x;
        int right  = rect.x + rect.w - 1;
        int top    = rect.y;
        int bottom = rect.y + rect.h - 1;

        for (int col = left; col <= right; col++)
        {
            set_pixel_raw(gw, col, top, rgba);
            set_pixel_raw(gw, col, bottom, rgba);
        }
        for (int row = top; row <= bottom; row++)
        {
            set_pixel_raw(gw, left, row, rgba);
            set_pixel_raw(gw, right, row, rgba);
        }
    }

    mark_dirty(gw, rect.x, rect.y, rect.w, rect.h);
}

void xj380_gui_draw_text(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint64_t str_ptr, uint32_t size, uint32_t rgba)
{
    rgba = rgba_to_argb(rgba);
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw || !str_ptr) return;

    char text[4096];
    if (xj380_mem_read_str(emu, str_ptr, text, sizeof(text)) != 0)
    {
        return;
    }

    /* 尝试用缓存字体渲染 */
    cached_font_t *cf = find_font("/system/font/XJ380C.ttf");
    if (!cf) cf = find_font("/system/font/XJ380F.ttf");
    if (!cf && g_font_count > 0) cf = &g_fonts[0];

    if (cf) {
        float scale = stbtt_ScaleForPixelHeight(&cf->info, (float)(size ? size : 12));
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&cf->info, &ascent, &descent, &line_gap);
        int baseline = (int)((float)ascent * scale);
        int line_h   = (int)((float)(ascent - descent + line_gap) * scale);

        float cx = (float)x;
        float cy = (float)y + (float)baseline;
        char *p = text;
        while (*p) {
            if (*p == '\n') { cx = (float)x; cy += (float)line_h; p++; continue; }

            /* 解码 UTF-8 码点。非法或截断序列按单字节跳过。 */
            int codepoint = 0;
            int utf8_len = 0;
            if (!utf8_next_codepoint(text, p, &codepoint, &utf8_len)) {
                p++;
                continue;
            }

            int adv, lsb, x0, y0, x1, y1;
            /* 获取字符度量 (使用真实码点) */
            stbtt_GetCodepointHMetrics(&cf->info, codepoint, &adv, &lsb);
            stbtt_GetCodepointBitmapBox(&cf->info, codepoint, scale, scale,
                                        &x0, &y0, &x1, &y1);

            int gw_w = x1 - x0, gw_h = y1 - y0;
            if (gw_w > 0 && gw_h > 0) {
                uint8_t *glyph = malloc((size_t)(gw_w * gw_h));
                if (glyph) {
                    stbtt_MakeCodepointBitmap(&cf->info, glyph, gw_w, gw_h, gw_w,
                                              scale, scale, codepoint);

                    uint8_t ar = (uint8_t)((rgba >> 16) & 0xFF);
                    uint8_t ag = (uint8_t)((rgba >>  8) & 0xFF);
                    uint8_t ab = (uint8_t)( rgba        & 0xFF);
                    uint8_t aa_scale = (uint8_t)((rgba >> 24) & 0xFF);

                    /* Alpha 混合到 framebuffer */
                    int dx = (int)cx + x0;
                    int dy = (int)cy + y0;
                    for (int gy = 0; gy < gw_h; gy++) {
                        for (int gx = 0; gx < gw_w; gx++) {
                            int px = dx + gx, py = dy + gy;
                            if (px < 0 || py < 0 || px >= gw->width || py >= gw->height)
                                continue;
                            uint8_t a = (uint8_t)((unsigned)glyph[gy * gw_w + gx] *
                                         (unsigned)aa_scale / 255);
                            if (a == 0) continue;
                            if (a == 255) {
                                gw->fb[py * gw->width + px] = rgba;
                            } else {
                                uint32_t bg = gw->fb[py * gw->width + px];
                                uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
                                uint8_t bg2 = (uint8_t)((bg >>  8) & 0xFF);
                                uint8_t bb = (uint8_t)( bg        & 0xFF);
                                uint8_t fr = (uint8_t)(((unsigned)ar * a + (unsigned)br * (255-a)) / 255);
                                uint8_t fg = (uint8_t)(((unsigned)ag * a + (unsigned)bg2* (255-a)) / 255);
                                uint8_t fb2= (uint8_t)(((unsigned)ab * a + (unsigned)bb * (255-a)) / 255);
                                gw->fb[py * gw->width + px] = 0xFF000000U |
                                    ((uint32_t)fr << 16) | ((uint32_t)fg << 8) | fb2;
                            }
                        }
                    }
                    free(glyph);
                }
            }
            cx += (float)adv * scale;
            if (cx > (float)gw->width) { cx = (float)x; cy += (float)line_h; }
            p += utf8_len;
        }
    } else {
        /* 无字体回退: 色块占位 */
        int cw = 8, ch = 16;
        int px = (int)x, py = (int)y;
        for (char *p = text; *p; p++) {
            if (*p == '\n') { px = (int)x; py += ch; continue; }
            for (int dy = 0; dy < ch && py+dy < gw->height; dy++)
                for (int dx = 0; dx < cw && px+dx < gw->width; dx++)
                    set_pixel(gw, px+dx, py+dy, rgba);
            px += cw;
            if (px + cw > gw->width) { px = (int)x; py += ch; }
        }
    }
}

/* ================================================================
 * 图片加载与绘制
 * ================================================================ */

void xj380_gui_draw_bmp(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t path_ptr)
{
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!gw || !path_ptr || w == 0 || h == 0 || w > FB_MAX_WIDTH || h > FB_MAX_HEIGHT
        || !clip_rect(gw, x, y, w, h, &rect))
    {
        return;
    }

    char path[512];
    if (xj380_mem_read_str(emu, path_ptr, path, sizeof(path)) != 0)
    {
        return;
    }

    SDL_Surface *surf = load_image_surface(emu, path);
    if (!surf)
    {
        return;
    }

    SDL_Surface *scaled = SDL_CreateSurface((int)w, (int)h, SDL_PIXELFORMAT_ARGB8888);
    if (!scaled)
    {
        SDL_DestroySurface(surf);
        return;
    }

    SDL_BlitSurfaceScaled(surf, NULL, scaled, NULL, SDL_SCALEMODE_LINEAR);

    uint8_t *pixels = (uint8_t *)scaled->pixels;
    for (int row = 0; row < rect.h; row++)
    {
        uint32_t *src = (uint32_t *)(pixels
            + (size_t)(row + rect.y - (int)y) * (size_t)scaled->pitch)
            + rect.x - (int)x;
        uint32_t *dst = gw->fb + (rect.y + row) * gw->width + rect.x;
        memcpy(dst, src, (size_t)rect.w * sizeof(uint32_t));
    }

    mark_dirty(gw, rect.x, rect.y, rect.w, rect.h);
    SDL_DestroySurface(scaled);
    SDL_DestroySurface(surf);
}

void xj380_gui_draw_png(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t path_ptr)
{
    /* SDL3 内置加载器支持 BMP/PNG。 */
    xj380_gui_draw_bmp(emu, handle, x, y, w, h, path_ptr);
}

void xj380_gui_draw_picture(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t path_ptr)
{
    /* SDL3 内置加载器支持 BMP/PNG；其它格式需要额外解码库。 */
    xj380_gui_draw_bmp(emu, handle, x, y, w, h, path_ptr);
}

/* ================================================================
 * Framebuffer 操作
 * ================================================================ */

void xj380_gui_read_buffer(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!buf_ptr || !clip_rect(gw, x, y, w, h, &rect))
    {
        return;
    }

    size_t row_bytes = (size_t)rect.w * 3U;
    uint8_t *row     = malloc(row_bytes);
    if (!row)
    {
        return;
    }

    for (int dy = 0; dy < rect.h; dy++)
    {
        uint32_t *src = gw->fb + (rect.y + dy) * gw->width + rect.x;

        for (int dx = 0; dx < rect.w; dx++)
        {
            uint32_t pixel = src[dx];

            row[(size_t)dx * 3U + 0U] = (uint8_t)((pixel >> 16) & 0xFFU);
            row[(size_t)dx * 3U + 1U] = (uint8_t)((pixel >>  8) & 0xFFU);
            row[(size_t)dx * 3U + 2U] = (uint8_t)( pixel        & 0xFFU);
        }

        if (xj380_mem_write(
                emu,
                buf_ptr + (uint64_t)dy * (uint64_t)w * 3ULL,
                row,
                row_bytes) != 0)
        {
            break;
        }
    }

    free(row);
}

void xj380_gui_write_buffer(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!buf_ptr || !clip_rect(gw, x, y, w, h, &rect))
    {
        return;
    }

    size_t row_bytes = (size_t)rect.w * 3U;
    uint8_t *row     = malloc(row_bytes);
    if (!row)
    {
        return;
    }

    for (int dy = 0; dy < rect.h; dy++)
    {
        if (xj380_mem_read(
                emu,
                buf_ptr + (uint64_t)dy * (uint64_t)w * 3ULL,
                row,
                row_bytes) != 0)
        {
            break;
        }

        uint32_t *dst = gw->fb + (rect.y + dy) * gw->width + rect.x;
        for (int dx = 0; dx < rect.w; dx++)
        {
            dst[dx] = 0xFF000000U
                | ((uint32_t)row[(size_t)dx * 3U + 0U] << 16)
                | ((uint32_t)row[(size_t)dx * 3U + 1U] << 8)
                |  (uint32_t)row[(size_t)dx * 3U + 2U];
        }
    }

    mark_dirty(gw, rect.x, rect.y, rect.w, rect.h);
    free(row);
}

/* ================================================================
 * 窗口刷新
 * ================================================================ */

void xj380_gui_refresh_window(xj380_emu_t *emu, uint64_t handle)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!gw || !gw->open)
    {
        return;
    }

    rect = gw->dirty ? gw->dirty_rect : (SDL_Rect){0, 0, gw->width, gw->height};
    if (!clip_rect(gw, (uint32_t)rect.x, (uint32_t)rect.y,
            (uint32_t)rect.w, (uint32_t)rect.h, &rect))
    {
        return;
    }

    SDL_UpdateTexture(
        gw->tex,
        &rect,
        gw->fb + rect.y * gw->width + rect.x,
        gw->width * (int)sizeof(uint32_t));
    SDL_RenderClear(gw->rend);
    SDL_RenderTexture(gw->rend, gw->tex, NULL, NULL);
    SDL_RenderPresent(gw->rend);
    gw->dirty = false;
}

void xj380_gui_refresh_part(xj380_emu_t *emu, uint64_t handle,
    uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!gw || !gw->open || x1 >= x2 || y1 >= y2)
    {
        return;
    }

    if (!clip_rect(gw, x1, y1, x2 - x1, y2 - y1, &rect))
    {
        return;
    }

    SDL_UpdateTexture(
        gw->tex,
        &rect,
        gw->fb + rect.y * gw->width + rect.x,
        gw->width * (int)sizeof(uint32_t));
    SDL_RenderClear(gw->rend);
    SDL_RenderTexture(gw->rend, gw->tex, NULL, NULL);
    SDL_RenderPresent(gw->rend);
    gw->dirty = false;
}

/* ================================================================
 * 事件轮询 (在主循环中调用)
 * ================================================================ */

/* XJ380 消息类型 (来自 libsys.h) */
#define XJ380_MSG_CHAR    0
#define XJ380_MSG_MOVE    1
#define XJ380_MSG_LBUTTON 2
#define XJ380_MSG_RBUTTON 3
#define XJ380_MSG_MBUTTON 4
#define XJ380_MSG_ROLLER  5
#define XJ380_MSG_CRL     6
#define XJ380_MSG_SPCHAR  7
#define XJ380_MSG_RESIZE  8
#define XJ380_MSG_KEYUP   9
#define XJ380_MSG_KEYDOWN 10
#define XJ380_GUI_EVENT_RETURN 0x70000000ULL
#define XJ380_GUI_EVENT_STACK_TOP 0x70002000ULL

typedef struct {
    bool active;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t eflags;
} gui_callback_context_t;

static gui_callback_context_t g_gui_callback_context;

static bool save_gui_callback_context(struct uc_struct *uc, uint64_t return_addr)
{
    if (g_gui_callback_context.active)
    {
        return false;
    }

    g_gui_callback_context.rip = return_addr;
    if (uc_reg_read(uc, UC_X86_REG_RAX, &g_gui_callback_context.rax) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RBX, &g_gui_callback_context.rbx) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RCX, &g_gui_callback_context.rcx) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RDX, &g_gui_callback_context.rdx) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RSI, &g_gui_callback_context.rsi) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RDI, &g_gui_callback_context.rdi) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RBP, &g_gui_callback_context.rbp) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_RSP, &g_gui_callback_context.rsp) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R8, &g_gui_callback_context.r8) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R9, &g_gui_callback_context.r9) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R10, &g_gui_callback_context.r10) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R11, &g_gui_callback_context.r11) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R12, &g_gui_callback_context.r12) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R13, &g_gui_callback_context.r13) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R14, &g_gui_callback_context.r14) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_R15, &g_gui_callback_context.r15) != UC_ERR_OK
        || uc_reg_read(uc, UC_X86_REG_EFLAGS, &g_gui_callback_context.eflags) != UC_ERR_OK)
    {
        memset(&g_gui_callback_context, 0, sizeof(g_gui_callback_context));
        return false;
    }

    g_gui_callback_context.active = true;
    return true;
}

/* 事件队列 (最多缓存 64 个事件) */
typedef struct {
    uint64_t type, hData, lData;
    uint64_t target_handle;
} gui_event_t;

static gui_event_t g_event_queue[64];

static bool is_keyboard_state_event(uint64_t type)
{
    return type == XJ380_MSG_KEYDOWN || type == XJ380_MSG_KEYUP;
}

static void queue_event(uint64_t handle, uint64_t type, uint64_t hData, uint64_t lData)
{
    int next = (g_event_tail + 1) % 64;
    if (type == XJ380_MSG_MOVE)
    {
        int i = g_event_head;
        while (i != g_event_tail)
        {
            if (g_event_queue[i].target_handle == handle
                && g_event_queue[i].type == XJ380_MSG_MOVE)
            {
                g_event_queue[i].hData = hData;
                g_event_queue[i].lData = lData;
                return;
            }
            i = (i + 1) % 64;
        }
    }

    if (next == g_event_head)
    {
        int i = g_event_head;
        while (i != g_event_tail)
        {
            if (g_event_queue[i].type == XJ380_MSG_MOVE)
            {
                g_event_queue[i].target_handle = handle;
                g_event_queue[i].type = type;
                g_event_queue[i].hData = hData;
                g_event_queue[i].lData = lData;
                return;
            }
            i = (i + 1) % 64;
        }
        if (!is_keyboard_state_event(type))
        {
            return;
        }
        g_event_head = (g_event_head + 1) % 64;
        next = (g_event_tail + 1) % 64;
    }
    g_event_queue[g_event_tail].type   = type;
    g_event_queue[g_event_tail].hData  = hData;
    g_event_queue[g_event_tail].lData  = lData;
    g_event_queue[g_event_tail].target_handle = handle;
    g_event_tail = next;
}

static int sdl_key_to_ascii(SDL_Keycode sym, SDL_Keymod mod)
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

static int sdl_key_to_xj380_key(SDL_Keycode sym, SDL_Keymod mod)
{
    switch (sym)
    {
    case SDLK_ESCAPE: return XJ380_XKEY_ESC;
    case SDLK_BACKSPACE: return '\b';
    case SDLK_TAB: return XJ380_XKEY_TAB;
    case SDLK_RETURN: return '\n';
    case SDLK_CAPSLOCK: return XJ380_XKEY_CAPS;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return XJ380_XKEY_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return XJ380_XKEY_CTRL;
    case SDLK_LALT:
    case SDLK_RALT: return XJ380_XKEY_ALT;
    case SDLK_SPACE: return XJ380_XKEY_SPACE;
    case SDLK_F1: return XJ380_XKEY_F1;
    case SDLK_F2: return XJ380_XKEY_F2;
    case SDLK_F3: return XJ380_XKEY_F3;
    case SDLK_F4: return XJ380_XKEY_F4;
    case SDLK_F5: return XJ380_XKEY_F5;
    case SDLK_F6: return XJ380_XKEY_F6;
    case SDLK_F7: return XJ380_XKEY_F7;
    case SDLK_F8: return XJ380_XKEY_F8;
    case SDLK_F9: return XJ380_XKEY_F9;
    case SDLK_F10: return XJ380_XKEY_F10;
    case SDLK_F11: return XJ380_XKEY_F11;
    case SDLK_F12: return XJ380_XKEY_F12;
    case SDLK_NUMLOCKCLEAR: return XJ380_XKEY_NUML;
    case SDLK_SCROLLLOCK: return XJ380_XKEY_SCROLL;
    case SDLK_HOME: return XJ380_XKEY_HOME;
    case SDLK_UP: return XJ380_XKEY_UP;
    case SDLK_PAGEUP: return XJ380_XKEY_PAGE_UP;
    case SDLK_LEFT: return XJ380_XKEY_LEFT;
    case SDLK_RIGHT: return XJ380_XKEY_RIGHT;
    case SDLK_END: return XJ380_XKEY_END;
    case SDLK_DOWN: return XJ380_XKEY_DOWN;
    case SDLK_PAGEDOWN: return XJ380_XKEY_PAGE_DOWN;
    case SDLK_INSERT: return XJ380_XKEY_INSERT;
    case SDLK_DELETE: return XJ380_XKEY_DELETE;
    default: break;
    }

    return sdl_key_to_ascii(sym, mod);
}

static int sdl_scancode_index(SDL_Scancode scancode)
{
    int index = (int)scancode;
    if (index < 0 || index >= KEY_STATE_COUNT)
    {
        return -1;
    }
    return index;
}

static uint64_t pack_utf8_bytes(const char *text, int len)
{
    uint64_t value = 0;
    int n = len > 8 ? 8 : len;

    for (int i = 0; i < n; i++)
    {
        value |= (uint64_t)(uint8_t)text[i] << (unsigned)(i * 8);
    }

    return value;
}

static void push_test_sdl_events(gui_window_t *gw)
{
    if (!g_test_events_enabled || g_test_events_pushed || !gw || !gw->win
        || !gw->msg_callback_addr)
    {
        return;
    }

    queue_event(gw->handle, XJ380_MSG_CHAR, 0, 0x41);
    queue_event(gw->handle, XJ380_MSG_SPCHAR, 0, 128);
    queue_event(gw->handle, XJ380_MSG_KEYDOWN, 0, 0x41);
    queue_event(gw->handle, XJ380_MSG_KEYUP, 0, 0x41);
    queue_event(gw->handle, XJ380_MSG_MOVE, 11, 22);
    queue_event(gw->handle, XJ380_MSG_LBUTTON, 33, 44);
    queue_event(gw->handle, XJ380_MSG_RBUTTON, 45, 46);
    queue_event(gw->handle, XJ380_MSG_MBUTTON, 47, 48);
    queue_event(gw->handle, XJ380_MSG_ROLLER, ((uint64_t)55 << 32) | 66, 1);
    queue_event(gw->handle, XJ380_MSG_CRL, 3001, 0);
    queue_event(gw->handle, XJ380_MSG_RESIZE, 0, 0);

    g_test_events_pushed = true;
}

int xj380_gui_poll_events(xj380_emu_t *emu)
{
    (void)emu;
    if (!g_sdl_initialized || g_window_count == 0) return 0;

    for (int i = 0; i < g_window_count; i++)
    {
        push_test_sdl_events(g_windows[i]);
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        gui_window_t *gw = NULL;

        /* 找到事件对应的窗口 */
        Uint32 winID = 0;
        switch (ev.type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_RESIZED:
            winID = ev.window.windowID; break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            winID = ev.button.windowID; break;
        case SDL_EVENT_MOUSE_MOTION: winID = ev.motion.windowID; break;
        case SDL_EVENT_MOUSE_WHEEL:  winID = ev.wheel.windowID; break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:       winID = ev.key.windowID; break;
        case SDL_EVENT_TEXT_INPUT:   winID = ev.text.windowID; break;
        default: break;
        }

        for (int i = 0; i < g_window_count; i++) {
            SDL_Window *w = g_windows[i]->win;
            if (w && SDL_GetWindowID(w) == winID) {
                gw = g_windows[i];
                break;
            }
        }
        if (!gw && ev.type != SDL_EVENT_QUIT) continue;

        switch (ev.type) {

        /* ---- 窗口事件 ---- */
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                memset(gw->key_pressed_values, 0, sizeof(gw->key_pressed_values));
                gw->open = false;
                if (gw->fb)   { free(gw->fb);   gw->fb   = NULL; }
                if (gw->tex)  { SDL_DestroyTexture(gw->tex);  gw->tex  = NULL; }
                if (gw->rend) { SDL_DestroyRenderer(gw->rend); gw->rend = NULL; }
                if (gw->win)  {
                    SDL_StopTextInput(gw->win);
                    SDL_DestroyWindow(gw->win);
                    gw->win  = NULL;
                }
                return 2;  /* 通知调用者: 窗口被用户关闭 */
        case SDL_EVENT_WINDOW_RESIZED:
            {
                int new_width  = ev.window.data1;
                int new_height = ev.window.data2;

                if (new_width <= 0 || new_height <= 0
                    || new_width > FB_MAX_WIDTH || new_height > FB_MAX_HEIGHT)
                {
                    break;
                }

                uint32_t *new_fb = calloc(
                    (size_t)new_width * (size_t)new_height,
                    sizeof(uint32_t));
                SDL_Texture *new_tex = SDL_CreateTexture(
                    gw->rend,
                    SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    new_width,
                    new_height);

                if (!new_fb || !new_tex)
                {
                    free(new_fb);
                    if (new_tex)
                    {
                        SDL_DestroyTexture(new_tex);
                    }
                    break;
                }

                free(gw->fb);
                if (gw->tex)
                {
                    SDL_DestroyTexture(gw->tex);
                }

                gw->width      = new_width;
                gw->height     = new_height;
                gw->fb         = new_fb;
                gw->tex        = new_tex;
                gw->dirty      = true;
                gw->dirty_rect = (SDL_Rect){0, 0, gw->width, gw->height};
                queue_event(gw->handle, XJ380_MSG_RESIZE, 0, 0);
                break;
            }

        /* ---- 鼠标移动 ---- */
        case SDL_EVENT_MOUSE_MOTION:
            queue_event(gw->handle, XJ380_MSG_MOVE,
                        (uint64_t)(uint32_t)ev.motion.x, (uint64_t)(uint32_t)ev.motion.y);
            break;

        /* ---- 鼠标按键: XJ380 在释放时发送一次点击手势 ---- */
        case SDL_EVENT_MOUSE_BUTTON_UP:
            switch (ev.button.button) {
            case SDL_BUTTON_LEFT:
                queue_event(gw->handle, XJ380_MSG_LBUTTON,
                    (uint64_t)(uint32_t)ev.button.x, (uint64_t)(uint32_t)ev.button.y);
                /* 检查按钮点击 */
                for (int i = 0; i < gw->button_count; i++) {
                    if (!gw->buttons[i].alive) continue;
                    int bx = gw->buttons[i].x, by = gw->buttons[i].y;
                    int tw = (int)strlen(gw->buttons[i].text) * 8 + 22;
                    if ((int)ev.button.x >= bx && (int)ev.button.x <= bx + tw &&
                        (int)ev.button.y >= by && (int)ev.button.y <= by + 24) {
                        queue_event(gw->handle, XJ380_MSG_CRL,
                            gw->buttons[i].id, 0);
                    }
                }
                break;
            case SDL_BUTTON_RIGHT:
                queue_event(gw->handle, XJ380_MSG_RBUTTON,
                    (uint64_t)(uint32_t)ev.button.x, (uint64_t)(uint32_t)ev.button.y);
                break;
            case SDL_BUTTON_MIDDLE:
                queue_event(gw->handle, XJ380_MSG_MBUTTON,
                    (uint64_t)(uint32_t)ev.button.x, (uint64_t)(uint32_t)ev.button.y);
                break;
            }
            break;

        /* ---- 鼠标滚轮 ---- */
        case SDL_EVENT_MOUSE_WHEEL:
            queue_event(gw->handle, XJ380_MSG_ROLLER,
                ((uint64_t)(uint32_t)ev.wheel.mouse_x << 32) |
                ((uint64_t)(uint32_t)ev.wheel.mouse_y),
                (uint64_t)(int64_t)ev.wheel.y);
            break;

        /* ---- 键盘: 字符输入 ---- */
        case SDL_EVENT_TEXT_INPUT:
        {
            const char *p = ev.text.text;
            while (*p)
            {
                int codepoint = 0;
                int utf8_len = 0;
                (void)codepoint;
                if (!utf8_next_codepoint(ev.text.text, p, &codepoint, &utf8_len))
                {
                    p++;
                    continue;
                }
                uint64_t packed = pack_utf8_bytes(p, utf8_len);
                if (gw->suppress_textinput
                    && gw->suppress_textinput_value == packed)
                {
                    gw->suppress_textinput = false;
                    p += utf8_len;
                    continue;
                }
                gw->suppress_textinput = false;
                queue_event(gw->handle, XJ380_MSG_CHAR, 0, packed);
                p += utf8_len;
            }
            break;
        }

        /* ---- 键盘: 特殊键 ---- */
        case SDL_EVENT_KEY_DOWN: {
            if (ev.key.repeat)
            {
                break;
            }
            int key = sdl_key_to_xj380_key(ev.key.key, ev.key.mod);
            if (key >= 0)
            {
                int scancode_index = sdl_scancode_index(ev.key.scancode);
                if (scancode_index >= 0)
                {
                    if (gw->key_pressed_values[scancode_index] != 0)
                    {
                        break;
                    }
                    gw->key_pressed_values[scancode_index] = (uint8_t)key;
                }
                queue_event(gw->handle, XJ380_MSG_KEYDOWN, 0, (uint64_t)(uint8_t)key);
                if (key == '\n' || key == '\b' || key >= 128)
                {
                    queue_event(gw->handle, XJ380_MSG_SPCHAR, 0, (uint64_t)(uint8_t)key);
                }
                else
                {
                    queue_event(gw->handle, XJ380_MSG_CHAR, 0, (uint64_t)(uint8_t)key);
                    gw->suppress_textinput = true;
                    gw->suppress_textinput_value = (uint64_t)(uint8_t)key;
                }
            }
            break;
        }

        case SDL_EVENT_KEY_UP: {
            int key = sdl_key_to_xj380_key(ev.key.key, ev.key.mod);
            int scancode_index = sdl_scancode_index(ev.key.scancode);
            if (scancode_index >= 0 && gw->key_pressed_values[scancode_index] != 0)
            {
                key = gw->key_pressed_values[scancode_index];
                gw->key_pressed_values[scancode_index] = 0;
            }
            if (key >= 0)
            {
                queue_event(gw->handle, XJ380_MSG_KEYUP, 0, (uint64_t)(uint8_t)key);
            }
            break;
        }

        case SDL_EVENT_QUIT:
            return 1;
        }
    }

    return 0;
}

void xj380_gui_load_font(const char *vpath, const uint8_t *data, size_t size)
{
    if (!data || !size || g_font_count >= MAX_FONTS) return;
    /* 只缓存 TTF/OTF */
    const char *ext = strrchr(vpath, '.');
    if (!ext || (strcasecmp(ext, ".ttf") && strcasecmp(ext, ".otf"))) return;

    /* 检查是否已缓存 */
    for (int i = 0; i < g_font_count; i++)
        if (strcmp(g_fonts[i].path, vpath) == 0) return;

    /* 解析字体 */
    cached_font_t *cf = &g_fonts[g_font_count];
    memset(cf, 0, sizeof(*cf));
    copy_cstr(cf->path, sizeof(cf->path), vpath);
    cf->data = malloc(size);
    if (!cf->data) return;
    memcpy(cf->data, data, size);
    cf->size = size;

    if (!stbtt_InitFont(&cf->info, cf->data, stbtt_GetFontOffsetForIndex(cf->data, 0))) {
        free(cf->data);
        cf->data = NULL;
        return;
    }
    cf->scale = stbtt_ScaleForPixelHeight(&cf->info, 16.0f);
    g_font_count++;
    gui_log("[GUI] loaded font: %s (%zu bytes)\n", vpath, size);
}

void xj380_gui_store_callback(xj380_emu_t *emu, uint64_t handle, uint64_t func)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (gw) {
        gw->msg_callback_addr = func;
        gui_log("[GUI] SetMsgPrcor: handle=0x%llx func=0x%llx\n",
               (unsigned long long)handle, (unsigned long long)func);
    }
}

bool xj380_gui_dispatch_queued_event(xj380_emu_t *emu, uint64_t return_addr)
{
    if (g_event_head == g_event_tail)
    {
        return false;
    }

    struct uc_struct *uc = xj380_get_uc(emu);

    while (g_event_head != g_event_tail)
    {
        gui_event_t ev = g_event_queue[g_event_head];
        g_event_head = (g_event_head + 1) % 64;

        gui_window_t *gw = NULL;
        for (int i = 0; i < g_window_count; i++)
        {
            if (g_windows[i]->handle == ev.target_handle)
            {
                gw = g_windows[i];
                break;
            }
        }

        if (!gw || !gw->msg_callback_addr)
        {
            continue;
        }

        if (!save_gui_callback_context(uc, return_addr))
        {
            return false;
        }

        uint64_t rsp = XJ380_GUI_EVENT_STACK_TOP - 8U;
        uint64_t event_return = XJ380_GUI_EVENT_RETURN;
        if (uc_mem_write(uc, rsp, &event_return, sizeof(event_return)) != UC_ERR_OK)
        {
            memset(&g_gui_callback_context, 0, sizeof(g_gui_callback_context));
            return false;
        }

        uint64_t type = ev.type;
        uint64_t hd   = ev.hData;
        uint64_t ld   = ev.lData;
        uint64_t cb   = gw->msg_callback_addr;

        uc_reg_write(uc, UC_X86_REG_RDI, &type);
        uc_reg_write(uc, UC_X86_REG_RSI, &hd);
        uc_reg_write(uc, UC_X86_REG_RDX, &ld);
        uc_reg_write(uc, UC_X86_REG_RSP, &rsp);
        uc_reg_write(uc, UC_X86_REG_RIP, &cb);
        return true;
    }

    return false;
}

void xj380_gui_finish_dispatched_event(xj380_emu_t *emu)
{
    if (!g_gui_callback_context.active)
    {
        return;
    }

    struct uc_struct *uc = xj380_get_uc(emu);
    uc_reg_write(uc, UC_X86_REG_RAX, &g_gui_callback_context.rax);
    uc_reg_write(uc, UC_X86_REG_RBX, &g_gui_callback_context.rbx);
    uc_reg_write(uc, UC_X86_REG_RCX, &g_gui_callback_context.rcx);
    uc_reg_write(uc, UC_X86_REG_RDX, &g_gui_callback_context.rdx);
    uc_reg_write(uc, UC_X86_REG_RSI, &g_gui_callback_context.rsi);
    uc_reg_write(uc, UC_X86_REG_RDI, &g_gui_callback_context.rdi);
    uc_reg_write(uc, UC_X86_REG_RBP, &g_gui_callback_context.rbp);
    uc_reg_write(uc, UC_X86_REG_RSP, &g_gui_callback_context.rsp);
    uc_reg_write(uc, UC_X86_REG_R8, &g_gui_callback_context.r8);
    uc_reg_write(uc, UC_X86_REG_R9, &g_gui_callback_context.r9);
    uc_reg_write(uc, UC_X86_REG_R10, &g_gui_callback_context.r10);
    uc_reg_write(uc, UC_X86_REG_R11, &g_gui_callback_context.r11);
    uc_reg_write(uc, UC_X86_REG_R12, &g_gui_callback_context.r12);
    uc_reg_write(uc, UC_X86_REG_R13, &g_gui_callback_context.r13);
    uc_reg_write(uc, UC_X86_REG_R14, &g_gui_callback_context.r14);
    uc_reg_write(uc, UC_X86_REG_R15, &g_gui_callback_context.r15);
    uc_reg_write(uc, UC_X86_REG_RIP, &g_gui_callback_context.rip);
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &g_gui_callback_context.eflags);
    memset(&g_gui_callback_context, 0, sizeof(g_gui_callback_context));
}

void xj380_gui_flush_events(xj380_emu_t *emu)
{
    uint64_t return_addr = 0;
    struct uc_struct *uc = xj380_get_uc(emu);

    if (uc_reg_read(uc, UC_X86_REG_RIP, &return_addr) != UC_ERR_OK)
    {
        return;
    }

    (void)xj380_gui_dispatch_queued_event(emu, return_addr);
}

void xj380_gui_render_all(xj380_emu_t *emu)
{
    (void)emu;
    for (int i = 0; i < g_window_count; i++) {
        gui_window_t *gw = g_windows[i];
        if (!gw->open || !gw->dirty) continue;
        xj380_gui_refresh_window(emu, gw->handle);
    }
}

int xj380_gui_window_count(void)
{
    return g_window_count;
}

/* ---- 剩余 stub 实现 ---- */

void xj380_gui_set_icon(xj380_emu_t *emu, uint64_t handle, uint64_t path_ptr)
{
    (void)emu; (void)handle; (void)path_ptr;
    /* SDL3 设置窗口图标需要 SDL_Surface, 暂略 */
}

void xj380_gui_get_window_size(xj380_emu_t *emu, uint64_t handle,
                               uint64_t width_ptr, uint64_t height_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw) return;
    uint64_t w = (uint64_t)gw->width;
    uint64_t h = (uint64_t)gw->height;
    if (width_ptr)  xj380_mem_write(emu, width_ptr,  &w, 8);
    if (height_ptr) xj380_mem_write(emu, height_ptr, &h, 8);
}

void xj380_gui_draw_text_l(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint64_t str_ptr,
    uint32_t size, uint32_t rgba, uint64_t width_ptr)
{
    /* 先画文本, 然后计算宽度写入 width_ptr */
    xj380_gui_draw_text(emu, handle, x, y, str_ptr, size, rgba);
    if (width_ptr) {
        char text[4096] = {0};
        if (!str_ptr || xj380_mem_read_str(emu, str_ptr, text, sizeof(text)) != 0)
        {
            uint32_t zero = 0;
            xj380_mem_write(emu, width_ptr, &zero, 4);
            return;
        }
        uint32_t char_w = 8 * (size ? size : 2);
        if (char_w < 4) char_w = 4;
        uint32_t w = char_w * (uint32_t)strlen(text);
        xj380_mem_write(emu, width_ptr, &w, 4);
    }
}

void xj380_gui_draw_sw_text(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint64_t str_ptr, uint32_t rgba)
{
    /* 等宽字体: 英 9px, 中 18px, 高 16px — 简化: 用 draw_text 替 */
    xj380_gui_draw_text(emu, handle, x, y, str_ptr, 1, rgba);
}

uint64_t xj380_gui_calc_text_width(xj380_emu_t *emu, uint64_t str_ptr,
                                  uint32_t size)
{
    char text[4096] = {0};
    if (!str_ptr || xj380_mem_read_str(emu, str_ptr, text, sizeof(text)) != 0)
    {
        return 0;
    }
    uint32_t char_w = 8 * (size ? size : 2);
    if (char_w < 4) char_w = 4;
    return (uint64_t)char_w * (uint64_t)strlen(text);
}

void xj380_gui_get_pic_size(xj380_emu_t *emu, uint64_t path_ptr,
                            uint64_t width_ptr, uint64_t height_ptr)
{
    char path[512] = {0};
    if (!path_ptr || xj380_mem_read_str(emu, path_ptr, path, sizeof(path)) != 0)
    {
        return;
    }
    SDL_Surface *s = load_image_surface(emu, path);
    if (s) {
        uint32_t w = (uint32_t)s->w, h = (uint32_t)s->h;
        if (width_ptr)  xj380_mem_write(emu, width_ptr,  &w, 4);
        if (height_ptr) xj380_mem_write(emu, height_ptr, &h, 4);
        SDL_DestroySurface(s);
    }
}

void xj380_gui_read_buffer_a(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!buf_ptr || !clip_rect(gw, x, y, w, h, &rect))
    {
        return;
    }

    size_t row_bytes = (size_t)rect.w * 4U;
    uint8_t *row     = malloc(row_bytes);
    if (!row)
    {
        return;
    }

    for (int dy = 0; dy < rect.h; dy++)
    {
        uint32_t *src = gw->fb + (rect.y + dy) * gw->width + rect.x;
        for (int dx = 0; dx < rect.w; dx++)
        {
            uint32_t rgba = argb_to_rgba(src[dx]);
            row[(size_t)dx * 4U + 0U] = (uint8_t)((rgba >> 24) & 0xFFU);
            row[(size_t)dx * 4U + 1U] = (uint8_t)((rgba >> 16) & 0xFFU);
            row[(size_t)dx * 4U + 2U] = (uint8_t)((rgba >>  8) & 0xFFU);
            row[(size_t)dx * 4U + 3U] = (uint8_t)( rgba        & 0xFFU);
        }

        if (xj380_mem_write(
                emu,
                buf_ptr + (uint64_t)dy * (uint64_t)w * 4ULL,
                row,
                row_bytes) != 0)
        {
            break;
        }
    }

    free(row);
}

void xj380_gui_write_buffer_a(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    gui_window_t *gw = find_window(handle);
    SDL_Rect      rect;

    if (!buf_ptr || !clip_rect(gw, x, y, w, h, &rect))
    {
        return;
    }

    size_t row_bytes = (size_t)rect.w * 4U;
    uint8_t *row     = malloc(row_bytes);
    if (!row)
    {
        return;
    }

    for (int dy = 0; dy < rect.h; dy++)
    {
        if (xj380_mem_read(
                emu,
                buf_ptr + (uint64_t)dy * (uint64_t)w * 4ULL,
                row,
                row_bytes) != 0)
        {
            break;
        }

        uint32_t *dst = gw->fb + (rect.y + dy) * gw->width + rect.x;
        for (int dx = 0; dx < rect.w; dx++)
        {
            uint8_t red   = row[(size_t)dx * 4U + 0U];
            uint8_t green = row[(size_t)dx * 4U + 1U];
            uint8_t blue  = row[(size_t)dx * 4U + 2U];
            uint8_t alpha = row[(size_t)dx * 4U + 3U];

            if (alpha == 255U)
            {
                dst[dx] = 0xFF000000U
                    | ((uint32_t)red << 16)
                    | ((uint32_t)green << 8)
                    |  (uint32_t)blue;
            }
            else if (alpha != 0U)
            {
                uint32_t old   = dst[dx];
                uint8_t old_r  = (uint8_t)((old >> 16) & 0xFFU);
                uint8_t old_g  = (uint8_t)((old >>  8) & 0xFFU);
                uint8_t old_b  = (uint8_t)( old        & 0xFFU);
                uint8_t out_r  = (uint8_t)(((uint32_t)red * alpha
                    + (uint32_t)old_r * (255U - alpha)) / 255U);
                uint8_t out_g  = (uint8_t)(((uint32_t)green * alpha
                    + (uint32_t)old_g * (255U - alpha)) / 255U);
                uint8_t out_b  = (uint8_t)(((uint32_t)blue * alpha
                    + (uint32_t)old_b * (255U - alpha)) / 255U);

                dst[dx] = 0xFF000000U
                    | ((uint32_t)out_r << 16)
                    | ((uint32_t)out_g << 8)
                    |  (uint32_t)out_b;
            }
        }
    }

    mark_dirty(gw, rect.x, rect.y, rect.w, rect.h);
    free(row);
}

void xj380_gui_button(xj380_emu_t *emu, uint64_t handle,
    uint64_t crl_id, uint64_t x, uint64_t y, uint64_t text_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || gw->button_count >= 64) return;
    int index = gw->button_count;
    gw->buttons[index].text[0] = '\0';
    if (text_ptr && xj380_mem_read_str(emu, text_ptr, gw->buttons[index].text,
        sizeof(gw->buttons[index].text)) != 0)
    {
        return;
    }
    gw->buttons[index].id    = crl_id;
    gw->buttons[index].x     = (int)x;
    gw->buttons[index].y     = (int)y;
    gw->buttons[index].alive = true;
    gw->buttons[index].emphasis = false;
    gw->button_count++;
    /* 在 framebuffer 上画简单矩形按钮 */
    int tw = (int)strlen(gw->buttons[gw->button_count-1].text) * 8 + 22;
    xj380_gui_draw_rect(emu, handle, (uint32_t)x, (uint32_t)y,
        (uint32_t)((int)x + tw), (uint32_t)((int)y + 24),
        0xCCCCCCFF, 1 /* fill */);
    xj380_gui_draw_text(emu, handle, (uint32_t)((int)x + 11),
        (uint32_t)((int)y + 4), text_ptr, 1, 0x000000FF);
}

void xj380_gui_emp_button(xj380_emu_t *emu, uint64_t handle,
    uint64_t crl_id, uint64_t x, uint64_t y, uint64_t text_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || gw->button_count >= 64) return;
    int index = gw->button_count;
    gw->buttons[index].text[0] = '\0';
    if (text_ptr && xj380_mem_read_str(emu, text_ptr, gw->buttons[index].text,
        sizeof(gw->buttons[index].text)) != 0)
    {
        return;
    }
    gw->buttons[index].id    = crl_id;
    gw->buttons[index].x     = (int)x;
    gw->buttons[index].y     = (int)y;
    gw->buttons[index].alive = true;
    gw->buttons[index].emphasis = true;
    gw->button_count++;
    int tw = (int)strlen(gw->buttons[gw->button_count-1].text) * 8 + 22;
    xj380_gui_draw_rect(emu, handle, (uint32_t)x, (uint32_t)y,
        (uint32_t)((int)x + tw), (uint32_t)((int)y + 24),
        0x4488FFFF, 1);  /* 蓝色强调 */
    xj380_gui_draw_text(emu, handle, (uint32_t)((int)x + 11),
        (uint32_t)((int)y + 4), text_ptr, 1, 0xFFFFFFFF);
}

void xj380_gui_delete_button(xj380_emu_t *emu, uint64_t handle, uint64_t crl_id)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw) return;
    for (int i = 0; i < gw->button_count; i++) {
        if (gw->buttons[i].id == crl_id) {
            gw->buttons[i].alive = false;
            return;
        }
    }
}

void xj380_gui_register_rbutton_menu(xj380_emu_t *emu, uint64_t handle,
                                     uint64_t items_ptr, uint64_t count)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || count > 16 || !items_ptr) return;

    uint64_t items[16] = {0};
    char item_text[16][64] = {{0}};

    /* RightMenuItem = { UINT64 CRLid, WSTR text } 每项 16 字节 */
    for (uint64_t i = 0; i < count; i++) {
        uint64_t item_addr = items_ptr + i * 16;
        uint64_t crl_id = 0, text_ptr = 0;
        if (xj380_mem_read(emu, item_addr, &crl_id, 8) != 0
            || xj380_mem_read(emu, item_addr + 8, &text_ptr, 8) != 0)
        {
            return;
        }
        items[i] = crl_id;
        if (text_ptr) {
            if (xj380_mem_read_str(emu, text_ptr, item_text[i],
                sizeof(item_text[i])) != 0)
            {
                return;
            }
        }
    }

    memset(&gw->rbutton_menu, 0, sizeof(gw->rbutton_menu));
    gw->rbutton_menu.registered = true;
    gw->rbutton_menu.item_count = (int)count;
    memcpy(gw->rbutton_menu.items, items, sizeof(items));
    memcpy(gw->rbutton_menu.item_text, item_text, sizeof(item_text));
}

/* ---- DrawSvg / DrawFA ---- */

void xj380_gui_draw_svg(xj380_emu_t *emu,
    uint64_t handle, uint32_t x, uint32_t y,
    uint32_t width, uint64_t svg_ptr, int enable_trans)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || !svg_ptr || width == 0 || width > FB_MAX_WIDTH) return;

    char *svg_text = malloc(65536);
    if (!svg_text) return;
    svg_text[0] = '\0';
    if (xj380_mem_read_str(emu, svg_ptr, svg_text, 65535) != 0)
    {
        free(svg_text);
        return;
    }

    /* 解析 SVG */
    NSVGimage *img = nsvgParse(svg_text, "px", 96.0f);
    free(svg_text);
    if (!img || img->width <= 0.0f || img->height <= 0.0f) {
        if (img) nsvgDelete(img);
        return;
    }

    /* 按宽度计算缩放 */
    float scale = (float)width / img->width;
    int   h     = (int)(img->height * scale);
    if (h < 1) h = 1;
    if (h > FB_MAX_HEIGHT) h = FB_MAX_HEIGHT;
    if ((size_t)width > SIZE_MAX / (size_t)h / 4U) { nsvgDelete(img); return; }

    /* 光栅化到临时缓冲区 */
    size_t buf_size = (size_t)width * (size_t)h * 4U;
    unsigned char *rast = malloc(buf_size);
    if (!rast) { nsvgDelete(img); return; }
    memset(rast, 0, buf_size);

    NSVGrasterizer *nsvg_r = nsvgCreateRasterizer();
    if (!nsvg_r) { free(rast); nsvgDelete(img); return; }

    nsvgRasterize(nsvg_r, img, 0, 0, scale, rast, (int)width, h, (int)width * 4);

    /* 写入 framebuffer */
    for (int dy = 0; dy < h && (int)y + dy < gw->height; dy++) {
        for (uint32_t dx = 0; dx < width && (int)x + (int)dx < gw->width; dx++) {
            int off = (dy * (int)width + (int)dx) * 4;
            uint8_t r = rast[off];
            uint8_t g = rast[off + 1];
            uint8_t b = rast[off + 2];
            uint8_t a = rast[off + 3];

            if (enable_trans) {
                /* 反转颜色 */
                r = (uint8_t)(255 - r);
                g = (uint8_t)(255 - g);
                b = (uint8_t)(255 - b);
            }

            uint32_t rgba = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                           ((uint32_t)g << 8)  | (uint32_t)b;
            set_pixel_raw(gw, (int)x + (int)dx, (int)y + dy, rgba);
        }
    }

    mark_dirty(gw, (int)x, (int)y, (int)width, h);
    nsvgDeleteRasterizer(nsvg_r);
    nsvgDelete(img);
    free(rast);
}

void xj380_gui_draw_fa(xj380_emu_t *emu,
    uint64_t handle, uint32_t x, uint32_t y,
    uint32_t width, uint64_t name_ptr, int enable_trans)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || !name_ptr || width == 0 || width > FB_MAX_WIDTH) return;

    /* 从 VFS 或文件系统读 SVG 文件内容 */
    char path[512] = {0};
    if (xj380_mem_read_str(emu, name_ptr, path, sizeof(path)) != 0)
    {
        return;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp && path[0] == '/') fp = fopen(path + 1, "rb");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1048576) { fclose(fp); return; }  /* 最大 1MB */

    char *svg = malloc((size_t)sz + 1);
    if (!svg) { fclose(fp); return; }
    if (fread(svg, 1, (size_t)sz, fp) != (size_t)sz) {
        free(svg);
        fclose(fp);
        return;
    }
    svg[sz] = '\0';
    fclose(fp);

    /* 复用 DrawSvg 逻辑 */
    NSVGimage *img = nsvgParse(svg, "px", 96.0f);
    free(svg);
    if (!img || img->width <= 0.0f || img->height <= 0.0f) {
        if (img) nsvgDelete(img);
        return;
    }

    float scale = (float)width / img->width;
    int h = (int)(img->height * scale);
    if (h < 1) h = 1;
    if (h > FB_MAX_HEIGHT) h = FB_MAX_HEIGHT;
    if ((size_t)width > SIZE_MAX / (size_t)h / 4U) { nsvgDelete(img); return; }

    size_t buf_size = (size_t)width * (size_t)h * 4U;
    unsigned char *rast = malloc(buf_size);
    if (!rast) { nsvgDelete(img); return; }
    memset(rast, 0, buf_size);

    NSVGrasterizer *nsvg_r = nsvgCreateRasterizer();
    if (!nsvg_r) { free(rast); nsvgDelete(img); return; }

    nsvgRasterize(nsvg_r, img, 0, 0, scale, rast, (int)width, h, (int)width * 4);

    for (int dy = 0; dy < h && (int)y + dy < gw->height; dy++) {
        for (uint32_t dx = 0; dx < width && (int)x + (int)dx < gw->width; dx++) {
            int off = (dy * (int)width + (int)dx) * 4;
            uint8_t r = rast[off], g = rast[off+1], b = rast[off+2], a = rast[off+3];
            if (enable_trans) { r ^= 0xFF; g ^= 0xFF; b ^= 0xFF; }
            set_pixel_raw(gw, (int)x + (int)dx, (int)y + dy,
                ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
        }
    }

    mark_dirty(gw, (int)x, (int)y, (int)width, h);
    nsvgDeleteRasterizer(nsvg_r);
    nsvgDelete(img);
    free(rast);
}

void xj380_gui_delete_rbutton_menu(xj380_emu_t *emu, uint64_t handle)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (gw) gw->rbutton_menu.registered = false;
}
