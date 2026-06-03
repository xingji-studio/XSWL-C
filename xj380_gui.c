/*
 * xj380_gui.c — XJ380 GUI 后端 (SDL2)
 *
 * 实现 XJ380 API 手册 Chapter 4 的图形化函数:
 *   窗口管理, 绘图, 图片, framebuffer, 控件, 消息处理
 *
 * 线程模型:
 *   模拟器主线程在 uc_emu_start() 阻塞执行, 但 SDL2 需要在主线程处理事件。
 *   方案: 在 xj380_run 中用 SDL_PollEvent 交替驱动 SDL2 和 Unicorn。
 *   当 xapi_CreateWindow 被调用时, 创建 SDL 窗口并切换到事件驱动模式。
 */

#include "xj380_emu.h"
#include "xj380_gui.h"

#include <SDL.h>
#include <SDL_image.h>
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

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

/* ================================================================
 * GUI 窗口结构
 * ================================================================ */
#define MAX_GUI_WINDOWS 16
#define FB_MAX_WIDTH    1920
#define FB_MAX_HEIGHT   1080

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
static bool           g_sdl_initialized;

/* ================================================================
 * SDL2 初始化
 * ================================================================ */
int xj380_gui_init(void)
{
    if (g_sdl_initialized) return 0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[GUI] SDL_Init 失败: %s\n", SDL_GetError());
        return -1;
    }

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    g_sdl_initialized = true;
    printf("[GUI] SDL2 初始化完成\n");
    return 0;
}

void xj380_gui_cleanup(void)
{
    for (int i = 0; i < g_window_count; i++) {
        gui_window_t *gw = g_windows[i];
        if (gw->fb)    free(gw->fb);
        if (gw->tex)   SDL_DestroyTexture(gw->tex);
        if (gw->rend)  SDL_DestroyRenderer(gw->rend);
        if (gw->win)   SDL_DestroyWindow(gw->win);
        free(gw);
    }
    g_window_count = 0;

    if (g_sdl_initialized) {
        IMG_Quit();
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

/* xapi_CreateWindow: HDLE*, XWINDOW* → 创建 SDL 窗口 */
void xj380_gui_create_window(xj380_emu_t *emu, uint64_t handle_ptr, uint64_t xwin_ptr)
{
    if (g_window_count >= MAX_GUI_WINDOWS) return;

    xj380_gui_init();

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
    uint8_t xwin_raw[32];
    xj380_mem_read(emu, xwin_ptr, xwin_raw, sizeof(xwin_raw));
    memcpy(&width,  xwin_raw,      4);
    memcpy(&height, xwin_raw + 4,  4);
    uint64_t title_ptr;
    memcpy(&title_ptr, xwin_raw + 8, 8);
    sets = xwin_raw[16];

    if (title_ptr) xj380_mem_read_str(emu, title_ptr, title, sizeof(title));

    if (width == 0)   width  = 640;
    if (height == 0)  height = 480;
    if (title[0] == 0) strcpy(title, "XJ380 Application");

    /* 创建 SDL 窗口 */
    uint32_t sdl_flags = SDL_WINDOW_SHOWN;
    if (sets & XWIN_FULL_SCR) {
        sdl_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    } else if (sets & XWIN_FRAME_OFF) {
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    }
    if (sets & XWIN_SUPPORT_RESIZEABLE) {
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window *win = SDL_CreateWindow(title,
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        (int)width, (int)height, sdl_flags);

    if (!win) {
        fprintf(stderr, "[GUI] SDL_CreateWindow 失败: %s\n", SDL_GetError());
        /* 写 NULL handle */
        uint64_t zero = 0;
        xj380_mem_write(emu, handle_ptr, &zero, 8);
        return;
    }

    SDL_Renderer *rend = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture  *tex  = SDL_CreateTexture(rend,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        (int)width, (int)height);

    /* 分配 framebuffer */
    uint32_t *fb = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    /* 填充白色 */
    for (int i = 0; i < (int)(width * height); i++) fb[i] = 0xFFFFFFFF;

    /* 注册 */
    gui_window_t *gw = calloc(1, sizeof(*gw));
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
    strncpy(gw->title, title, 255);

    g_windows[g_window_count++] = gw;

    /* 写 handle 回模拟器内存 */
    xj380_mem_write(emu, handle_ptr, &gw->handle, 8);

    printf("[GUI] 创建窗口 #%llu: %dx%d \"%s\"\n",
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
            if (gw->win)  SDL_DestroyWindow(gw->win);
            free(gw);
            /* 从数组中移除 */
            g_windows[i] = g_windows[--g_window_count];
            g_windows[g_window_count] = NULL;
            printf("[GUI] 关闭窗口 #%llu\n", (unsigned long long)handle);
            return;
        }
    }
}

/* xapi_SetWindowTitle */
void xj380_gui_set_window_title(xj380_emu_t *emu, uint64_t handle, uint64_t title_ptr)
{
    char title[256];
    if (title_ptr) xj380_mem_read_str(emu, title_ptr, title, sizeof(title));
    else title[0] = 0;

    gui_window_t *gw = find_window(handle);
    if (gw) {
        strncpy(gw->title, title, 255);
        SDL_SetWindowTitle(gw->win, title);
    }
}

/* ================================================================
 * 绘图函数
 * ================================================================ */

static void set_pixel(gui_window_t *gw, int x, int y, uint32_t rgba)
{
    if (x < 0 || y < 0 || x >= gw->width || y >= gw->height) return;
    gw->fb[y * gw->width + x] = rgba;
}

void xj380_gui_draw_point(xj380_emu_t *emu, uint64_t handle,
                          uint32_t x, uint32_t y, uint32_t rgba)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (gw) set_pixel(gw, (int)x, (int)y, rgba);
}

void xj380_gui_draw_line(xj380_emu_t *emu, uint64_t handle,
    uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t rgba)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw) return;

    /* Bresenham 画线 */
    int dx  = abs((int)x2 - (int)x1);
    int dy  = -abs((int)y2 - (int)y1);
    int sx  = x1 < x2 ? 1 : -1;
    int sy  = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int cx  = (int)x1;
    int cy  = (int)y1;

    for (;;) {
        set_pixel(gw, cx, cy, rgba);
        if (cx == (int)x2 && cy == (int)y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
}

void xj380_gui_draw_rect(xj380_emu_t *emu, uint64_t handle,
    uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
    uint32_t rgba, int fill)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw) return;

    int lx = (int)(x1 < x2 ? x1 : x2);
    int rx = (int)(x1 > x2 ? x1 : x2);
    int ty = (int)(y1 < y2 ? y1 : y2);
    int by = (int)(y1 > y2 ? y1 : y2);

    if (fill) {
        for (int y = ty; y <= by; y++)
            for (int x = lx; x <= rx; x++)
                set_pixel(gw, x, y, rgba);
    } else {
        for (int x = lx; x <= rx; x++) {
            set_pixel(gw, x, ty, rgba);
            set_pixel(gw, x, by, rgba);
        }
        for (int y = ty; y <= by; y++) {
            set_pixel(gw, lx, y, rgba);
            set_pixel(gw, rx, y, rgba);
        }
    }
}

void xj380_gui_draw_text(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint64_t str_ptr, uint32_t size, uint32_t rgba)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw || !str_ptr) return;

    char text[4096];
    xj380_mem_read_str(emu, str_ptr, text, sizeof(text));

    /* 用 SDL2 内置的简单文本渲染 (通过 SDL2_ttf 更专业, 这里偷懒画点) */
    /* 简单等宽字体: 8x16 per char, 按 size 缩放 */
    int char_w = 8  * (int)(size ? size : 2);
    int char_h = 16 * (int)(size ? size : 2);
    if (char_w < 4) char_w = 4;
    if (char_h < 8) char_h = 8;

    int cx = (int)x, cy = (int)y;
    for (char *p = text; *p; p++) {
        if (*p == '\n') { cx = (int)x; cy += char_h; continue; }
        /* 简单占位: 画一个色块表示字符 */
        uint8_t ch = (uint8_t)*p;
        uint8_t r = (uint8_t)((ch * 37)  & 0xFF);
        uint8_t g = (uint8_t)((ch * 73)  & 0xFF);
        uint8_t b = (uint8_t)((ch * 137) & 0xFF);
        uint32_t c = (rgba & 0xFF000000) | (r << 16) | (g << 8) | b;
        for (int dy = 0; dy < char_h - 1 && cy + dy < gw->height; dy++)
            for (int dx = 0; dx < char_w - 1 && cx + dx < gw->width; dx++)
                set_pixel(gw, cx + dx, cy + dy, c);
        cx += char_w;
        if (cx + char_w > gw->width) { cx = (int)x; cy += char_h; }
    }
}

/* ================================================================
 * 图片加载与绘制
 * ================================================================ */

void xj380_gui_draw_bmp(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t path_ptr)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw || !path_ptr) return;

    char path[512];
    xj380_mem_read_str(emu, path_ptr, path, sizeof(path));

    /* 从 VFS 或宿主文件系统加载 */
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        /* 尝试去掉前导 / */
        if (path[0] == '/') surf = IMG_Load(path + 1);
        if (!surf) return;
    }

    /* 拉伸/填充到目标区域 */
    SDL_Surface *scaled = SDL_CreateRGBSurfaceWithFormat(0,
        (int)w, (int)h, 32, SDL_PIXELFORMAT_RGBA8888);
    SDL_BlitScaled(surf, NULL, scaled, NULL);

    /* 逐像素写入 framebuffer */
    uint32_t *pixels = (uint32_t*)scaled->pixels;
    for (int dy = 0; dy < (int)h; dy++) {
        for (int dx = 0; dx < (int)w; dx++) {
            if ((int)x + dx < gw->width && (int)y + dy < gw->height) {
                set_pixel(gw, (int)x + dx, (int)y + dy, pixels[dy * (int)w + dx]);
            }
        }
    }

    SDL_FreeSurface(scaled);
    SDL_FreeSurface(surf);
}

void xj380_gui_draw_png(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t path_ptr)
{
    /* IMG_Load 同样支持 PNG */
    xj380_gui_draw_bmp(emu, handle, x, y, w, h, path_ptr);
}

void xj380_gui_draw_picture(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t path_ptr)
{
    /* IMG_Load 支持 PNG/BMP/JPEG/GIF */
    xj380_gui_draw_bmp(emu, handle, x, y, w, h, path_ptr);
}

/* ================================================================
 * Framebuffer 操作
 * ================================================================ */

void xj380_gui_read_buffer(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || !buf_ptr) return;

    /* 逐行复制 RGBA 到模拟器内存 */
    for (uint32_t dy = 0; dy < h && (y + dy) < (uint32_t)gw->height; dy++) {
        uint64_t row_addr = buf_ptr + (uint64_t)dy * (uint64_t)w * 4;
        for (uint32_t dx = 0; dx < w && (x + dx) < (uint32_t)gw->width; dx++) {
            uint32_t pixel = gw->fb[(y + dy) * (uint32_t)gw->width + (x + dx)];
            xj380_mem_write(emu, row_addr + (uint64_t)dx * 4, &pixel, 4);
        }
    }
}

void xj380_gui_write_buffer(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || !buf_ptr) return;

    for (uint32_t dy = 0; dy < h && (y + dy) < (uint32_t)gw->height; dy++) {
        for (uint32_t dx = 0; dx < w && (x + dx) < (uint32_t)gw->width; dx++) {
            uint32_t pixel = 0;
            uint64_t addr = buf_ptr + (uint64_t)(dy * w + dx) * 4;
            xj380_mem_read(emu, addr, &pixel, 4);
            gw->fb[(y + dy) * (uint32_t)gw->width + (x + dx)] = pixel;
        }
    }
}

/* ================================================================
 * 窗口刷新
 * ================================================================ */

void xj380_gui_refresh_window(xj380_emu_t *emu, uint64_t handle)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw || !gw->open) return;

    SDL_UpdateTexture(gw->tex, NULL, gw->fb,
        gw->width * (int)sizeof(uint32_t));
    SDL_RenderClear(gw->rend);
    SDL_RenderCopy(gw->rend, gw->tex, NULL, NULL);
    SDL_RenderPresent(gw->rend);
}

void xj380_gui_refresh_part(xj380_emu_t *emu, uint64_t handle,
    uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (!gw || !gw->open) return;

    int w = (int)(x2 - x1);
    int h = (int)(y2 - y1);
    if (w <= 0 || h <= 0) return;

    SDL_Rect rect = { (int)x1, (int)y1, w, h };
    SDL_UpdateTexture(gw->tex, &rect,
        gw->fb + (int)y1 * gw->width + (int)x1,
        gw->width * (int)sizeof(uint32_t));
    SDL_RenderClear(gw->rend);
    SDL_RenderCopy(gw->rend, gw->tex, NULL, NULL);
    SDL_RenderPresent(gw->rend);
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

/* 事件队列 (最多缓存 64 个事件) */
typedef struct {
    uint64_t type, hData, lData;
    uint64_t target_handle;
} gui_event_t;

static gui_event_t g_event_queue[64];
static int         g_event_head;
static int         g_event_tail;

static void queue_event(uint64_t handle, uint64_t type, uint64_t hData, uint64_t lData)
{
    int next = (g_event_tail + 1) % 64;
    if (next == g_event_head) return; /* 队列满 */
    g_event_queue[g_event_tail].type   = type;
    g_event_queue[g_event_tail].hData  = hData;
    g_event_queue[g_event_tail].lData  = lData;
    g_event_queue[g_event_tail].target_handle = handle;
    g_event_tail = next;
}

int xj380_gui_poll_events(xj380_emu_t *emu)
{
    (void)emu;
    if (!g_sdl_initialized || g_window_count == 0) return 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        gui_window_t *gw = NULL;

        /* 找到事件对应的窗口 */
        Uint32 winID = 0;
        switch (ev.type) {
        case SDL_WINDOWEVENT: winID = ev.window.windowID; break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            winID = ev.button.windowID; break;
        case SDL_MOUSEMOTION: winID = ev.motion.windowID; break;
        case SDL_MOUSEWHEEL:  winID = ev.wheel.windowID; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:       winID = ev.key.windowID; break;
        case SDL_TEXTINPUT:   winID = ev.text.windowID; break;
        default: break;
        }

        for (int i = 0; i < g_window_count; i++) {
            SDL_Window *w = g_windows[i]->win;
            if (w && SDL_GetWindowID(w) == winID) {
                gw = g_windows[i];
                break;
            }
        }
        if (!gw && ev.type != SDL_QUIT) continue;

        switch (ev.type) {

        /* ---- 窗口事件 ---- */
        case SDL_WINDOWEVENT:
            switch (ev.window.event) {
            case SDL_WINDOWEVENT_CLOSE:
                gw->open = false;
                if (gw->fb)   { free(gw->fb);   gw->fb   = NULL; }
                if (gw->tex)  { SDL_DestroyTexture(gw->tex);  gw->tex  = NULL; }
                if (gw->rend) { SDL_DestroyRenderer(gw->rend); gw->rend = NULL; }
                if (gw->win)  { SDL_DestroyWindow(gw->win);   gw->win  = NULL; }
                return 2;  /* 通知调用者: 窗口被用户关闭 */
            case SDL_WINDOWEVENT_RESIZED:
                gw->width  = ev.window.data1;
                gw->height = ev.window.data2;
                free(gw->fb);
                gw->fb = calloc((size_t)gw->width * (size_t)gw->height, sizeof(uint32_t));
                if (gw->tex) SDL_DestroyTexture(gw->tex);
                gw->tex = SDL_CreateTexture(gw->rend,
                    SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                    gw->width, gw->height);
                queue_event(gw->handle, XJ380_MSG_RESIZE, 0, 0);
                break;
            }
            break;

        /* ---- 鼠标移动 ---- */
        case SDL_MOUSEMOTION:
            queue_event(gw->handle, XJ380_MSG_MOVE,
                        (uint64_t)ev.motion.x, (uint64_t)ev.motion.y);
            break;

        /* ---- 鼠标按键 ---- */
        case SDL_MOUSEBUTTONDOWN:
            /* 只处理按下, XJ380 在释放时发送消息 (看4-4-3)...
               但我们没有 down/up 配对, 简化: down 就发消息 */
            switch (ev.button.button) {
            case SDL_BUTTON_LEFT:
                queue_event(gw->handle, XJ380_MSG_LBUTTON,
                    (uint64_t)ev.button.x, (uint64_t)ev.button.y);
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
                    (uint64_t)ev.button.x, (uint64_t)ev.button.y);
                break;
            case SDL_BUTTON_MIDDLE:
                queue_event(gw->handle, XJ380_MSG_MBUTTON,
                    (uint64_t)ev.button.x, (uint64_t)ev.button.y);
                break;
            }
            break;

        /* ---- 鼠标滚轮 ---- */
        case SDL_MOUSEWHEEL:
            queue_event(gw->handle, XJ380_MSG_ROLLER,
                ((uint64_t)(uint32_t)ev.wheel.mouseX << 32) |
                ((uint64_t)(uint32_t)ev.wheel.mouseY),
                (uint64_t)(int64_t)ev.wheel.y);
            break;

        /* ---- 键盘: 字符输入 ---- */
        case SDL_TEXTINPUT:
            if (ev.text.text[0]) {
                queue_event(gw->handle, XJ380_MSG_CHAR,
                    0, (uint64_t)(uint8_t)ev.text.text[0]);
            }
            break;

        /* ---- 键盘: 特殊键 ---- */
        case SDL_KEYDOWN: {
            int sp = -1;
            switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:    sp = 128; break;
            case SDLK_BACKSPACE: sp = '\b'; break;
            case SDLK_TAB:       sp = 130; break;
            case SDLK_RETURN:    sp = '\n'; break;
            case SDLK_CAPSLOCK:  sp = 132; break;
            case SDLK_LSHIFT: case SDLK_RSHIFT: sp = 133; break;
            case SDLK_LCTRL: case SDLK_RCTRL:   sp = 134; break;
            case SDLK_LALT: case SDLK_RALT:     sp = 135; break;
            case SDLK_F1:  sp = 136; break; case SDLK_F2:  sp = 137; break;
            case SDLK_F3:  sp = 138; break; case SDLK_F4:  sp = 139; break;
            case SDLK_F5:  sp = 140; break; case SDLK_F6:  sp = 141; break;
            case SDLK_F7:  sp = 142; break; case SDLK_F8:  sp = 143; break;
            case SDLK_F9:  sp = 144; break; case SDLK_F10: sp = 145; break;
            case SDLK_F11: sp = 146; break; case SDLK_F12: sp = 147; break;
            case SDLK_NUMLOCKCLEAR: sp = 149; break;
            case SDLK_SCROLLLOCK:   sp = 150; break;
            default: break;
            }
            if (sp >= 0) {
                queue_event(gw->handle, XJ380_MSG_SPCHAR, 0, (uint64_t)sp);
            }
            break;
        }

        case SDL_QUIT:
            return 1;
        }
    }

    return 0;
}

#define EVT_RETURN_TRAMP 0xFFFFF000ULL  /* 事件回调返回 trampoline */

/*
 * 将排队的事件注入到模拟程序的消息回调中。
 * 在 xapi_Sleep 循环中被调用。
 */
void xj380_gui_store_callback(xj380_emu_t *emu, uint64_t handle, uint64_t func)
{
    (void)emu;
    gui_window_t *gw = find_window(handle);
    if (gw) {
        gw->msg_callback_addr = func;
        printf("[GUI] SetMsgPrcor: handle=0x%llx func=0x%llx\n",
               (unsigned long long)handle, (unsigned long long)func);
    }
}

void xj380_gui_flush_events(xj380_emu_t *emu)
{
    if (g_event_head == g_event_tail) return;

    struct uc_struct *uc = xj380_get_uc(emu);

    while (g_event_head != g_event_tail) {
        gui_event_t *ev = &g_event_queue[g_event_head];
        g_event_head = (g_event_head + 1) % 64;

        /* 找目标窗口 */
        gui_window_t *gw = NULL;
        for (int i = 0; i < g_window_count; i++) {
            if (g_windows[i]->handle == ev->target_handle) {
                gw = g_windows[i];
                break;
            }
        }
        if (!gw || !gw->msg_callback_addr) continue;

        /* 保存当前 CPU 状态 */
        uint64_t saved_rip = 0, saved_rsp = 0, saved_rflags = 0;
        uc_reg_read(uc, UC_X86_REG_RIP, &saved_rip);
        uc_reg_read(uc, UC_X86_REG_RSP, &saved_rsp);
        uc_reg_read(uc, UC_X86_REG_EFLAGS, &saved_rflags);

        /* 压入返回地址 (trampoline) */
        uint64_t new_rsp = saved_rsp - 8;
        uint64_t ret_addr = EVT_RETURN_TRAMP;
        uc_mem_write(uc, new_rsp, &ret_addr, 8);

        /* 设置回调参数: RDI=Type, RSI=hData, RDX=lData */
        uint64_t type = ev->type, hd = ev->hData, ld = ev->lData;
        uc_reg_write(uc, UC_X86_REG_RDI, &type);
        uc_reg_write(uc, UC_X86_REG_RSI, &hd);
        uc_reg_write(uc, UC_X86_REG_RDX, &ld);
        uc_reg_write(uc, UC_X86_REG_RSP, &new_rsp);

        /* 跳转到回调 */
        uint64_t cb = gw->msg_callback_addr;
        uc_reg_write(uc, UC_X86_REG_RIP, &cb);

        /* 运行直到返回 trampoline */
        uc_err ev_err = uc_emu_start(uc, cb, EVT_RETURN_TRAMP, 0, 0);

        /* 恢复状态 (即使执行失败也要尽量恢复) */
        uc_reg_write(uc, UC_X86_REG_RIP, &saved_rip);
        uc_reg_write(uc, UC_X86_REG_RSP, &saved_rsp);
        uc_reg_write(uc, UC_X86_REG_EFLAGS, &saved_rflags);

        if (ev_err != UC_ERR_OK) {
            fprintf(stderr, "[GUI] 事件注入失败: %s\n", uc_strerror(ev_err));
        }
    }
}

void xj380_gui_render_all(xj380_emu_t *emu)
{
    (void)emu;
    for (int i = 0; i < g_window_count; i++) {
        gui_window_t *gw = g_windows[i];
        if (!gw->open) continue;
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
    /* SDL2 设置窗口图标需要 SDL_Surface, 暂略 */
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
        char text[4096];
        xj380_mem_read_str(emu, str_ptr, text, sizeof(text));
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

void xj380_gui_calc_text_width(xj380_emu_t *emu, uint64_t str_ptr,
                               uint32_t size, uint64_t width_ptr)
{
    char text[4096];
    xj380_mem_read_str(emu, str_ptr, text, sizeof(text));
    uint32_t char_w = 8 * (size ? size : 2);
    if (char_w < 4) char_w = 4;
    uint32_t w = char_w * (uint32_t)strlen(text);
    if (width_ptr) xj380_mem_write(emu, width_ptr, &w, 4);
}

void xj380_gui_get_pic_size(xj380_emu_t *emu, uint64_t path_ptr,
                            uint64_t width_ptr, uint64_t height_ptr)
{
    char path[512] = {0};
    xj380_mem_read_str(emu, path_ptr, path, sizeof(path));
    SDL_Surface *s = IMG_Load(path);
    if (!s && path[0] == '/') s = IMG_Load(path + 1);
    if (s) {
        uint32_t w = (uint32_t)s->w, h = (uint32_t)s->h;
        if (width_ptr)  xj380_mem_write(emu, width_ptr,  &w, 4);
        if (height_ptr) xj380_mem_write(emu, height_ptr, &h, 4);
        SDL_FreeSurface(s);
    }
}

void xj380_gui_read_buffer_a(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    /* RGBA → XCOLORA: 格式相同, 直接复 */
    xj380_gui_read_buffer(emu, handle, x, y, w, h, buf_ptr);
}

void xj380_gui_write_buffer_a(xj380_emu_t *emu, uint64_t handle,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t buf_ptr)
{
    xj380_gui_write_buffer(emu, handle, x, y, w, h, buf_ptr);
}

void xj380_gui_button(xj380_emu_t *emu, uint64_t handle,
    uint64_t crl_id, uint64_t x, uint64_t y, uint64_t text_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || gw->button_count >= 64) return;
    if (text_ptr) xj380_mem_read_str(emu, text_ptr,
        gw->buttons[gw->button_count].text, 255);
    gw->buttons[gw->button_count].id    = crl_id;
    gw->buttons[gw->button_count].x     = (int)x;
    gw->buttons[gw->button_count].y     = (int)y;
    gw->buttons[gw->button_count].alive = true;
    gw->buttons[gw->button_count].emphasis = false;
    gw->button_count++;
    /* 在 framebuffer 上画简单矩形按钮 */
    int tw = (int)strlen(gw->buttons[gw->button_count-1].text) * 8 + 22;
    xj380_gui_draw_rect(emu, handle, (uint32_t)x, (uint32_t)y,
        (uint32_t)((int)x + tw), (uint32_t)((int)y + 24),
        0xFFCCCCCC, 1 /* fill */);
    xj380_gui_draw_text(emu, handle, (uint32_t)((int)x + 11),
        (uint32_t)((int)y + 4), text_ptr, 1, 0xFF000000);
}

void xj380_gui_emp_button(xj380_emu_t *emu, uint64_t handle,
    uint64_t crl_id, uint64_t x, uint64_t y, uint64_t text_ptr)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || gw->button_count >= 64) return;
    if (text_ptr) xj380_mem_read_str(emu, text_ptr,
        gw->buttons[gw->button_count].text, 255);
    gw->buttons[gw->button_count].id    = crl_id;
    gw->buttons[gw->button_count].x     = (int)x;
    gw->buttons[gw->button_count].y     = (int)y;
    gw->buttons[gw->button_count].alive = true;
    gw->buttons[gw->button_count].emphasis = true;
    gw->button_count++;
    int tw = (int)strlen(gw->buttons[gw->button_count-1].text) * 8 + 22;
    xj380_gui_draw_rect(emu, handle, (uint32_t)x, (uint32_t)y,
        (uint32_t)((int)x + tw), (uint32_t)((int)y + 24),
        0xFF4488FF, 1);  /* 蓝色强调 */
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

    gw->rbutton_menu.registered = true;
    gw->rbutton_menu.item_count = (int)count;

    /* RightMenuItem = { UINT64 CRLid, WSTR text } 每项 16 字节 */
    for (uint64_t i = 0; i < count; i++) {
        uint64_t item_addr = items_ptr + i * 16;
        uint64_t crl_id = 0, text_ptr = 0;
        xj380_mem_read(emu, item_addr,      &crl_id,   8);
        xj380_mem_read(emu, item_addr + 8,  &text_ptr, 8);
        gw->rbutton_menu.items[i] = crl_id;
        if (text_ptr) {
            xj380_mem_read_str(emu, text_ptr,
                gw->rbutton_menu.item_text[i], 64);
        } else {
            gw->rbutton_menu.item_text[i][0] = '\0';
        }
    }
}

/* ---- DrawSvg / DrawFA ---- */

void xj380_gui_draw_svg(xj380_emu_t *emu,
    uint64_t handle, uint32_t x, uint32_t y,
    uint32_t width, uint64_t svg_ptr, int enable_trans)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || !svg_ptr || width == 0) return;

    char *svg_text = malloc(65536);
    if (!svg_text) return;
    svg_text[0] = '\0';
    xj380_mem_read_str(emu, svg_ptr, svg_text, 65535);

    /* 解析 SVG */
    NSVGimage *img = nsvgParse(svg_text, "px", 96.0f);
    free(svg_text);
    if (!img) return;

    /* 按宽度计算缩放 */
    float scale = (float)width / img->width;
    int   h     = (int)(img->height * scale);
    if (h < 1) h = 1;
    if (h > 4096) h = 4096;

    /* 光栅化到临时缓冲区 */
    int buf_size = width * (uint32_t)h * 4;
    unsigned char *rast = malloc((size_t)buf_size);
    if (!rast) { nsvgDelete(img); return; }
    memset(rast, 0, (size_t)buf_size);

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
            set_pixel(gw, (int)x + (int)dx, (int)y + dy, rgba);
        }
    }

    nsvgDeleteRasterizer(nsvg_r);
    nsvgDelete(img);
    free(rast);
}

void xj380_gui_draw_fa(xj380_emu_t *emu,
    uint64_t handle, uint32_t x, uint32_t y,
    uint32_t width, uint64_t name_ptr, int enable_trans)
{
    gui_window_t *gw = find_window(handle);
    if (!gw || !name_ptr || width == 0) return;

    /* 从 VFS 或文件系统读 SVG 文件内容 */
    char path[512] = {0};
    xj380_mem_read_str(emu, name_ptr, path, sizeof(path));

    FILE *fp = fopen(path, "rb");
    if (!fp && path[0] == '/') fp = fopen(path + 1, "rb");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1048576) { fclose(fp); return; }  /* 最大 1MB */

    char *svg = malloc((size_t)sz + 1);
    if (!svg) { fclose(fp); return; }
    (void)fread(svg, 1, (size_t)sz, fp);
    svg[sz] = '\0';
    fclose(fp);

    /* 复用 DrawSvg 逻辑 */
    NSVGimage *img = nsvgParse(svg, "px", 96.0f);
    free(svg);
    if (!img) return;

    float scale = (float)width / img->width;
    int h = (int)(img->height * scale);
    if (h < 1) h = 1;
    if (h > 4096) h = 4096;

    int buf_size = width * (uint32_t)h * 4;
    unsigned char *rast = malloc((size_t)buf_size);
    if (!rast) { nsvgDelete(img); return; }
    memset(rast, 0, (size_t)buf_size);

    NSVGrasterizer *nsvg_r = nsvgCreateRasterizer();
    if (!nsvg_r) { free(rast); nsvgDelete(img); return; }

    nsvgRasterize(nsvg_r, img, 0, 0, scale, rast, (int)width, h, (int)width * 4);

    for (int dy = 0; dy < h && (int)y + dy < gw->height; dy++) {
        for (uint32_t dx = 0; dx < width && (int)x + (int)dx < gw->width; dx++) {
            int off = (dy * (int)width + (int)dx) * 4;
            uint8_t r = rast[off], g = rast[off+1], b = rast[off+2], a = rast[off+3];
            if (enable_trans) { r ^= 0xFF; g ^= 0xFF; b ^= 0xFF; }
            set_pixel(gw, (int)x+(int)dx, (int)y+dy,
                ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b);
        }
    }

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
