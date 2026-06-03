/*
 * xj380_emu.h — XJ380 高性能二进制模拟器 (Unicorn C API + SDL2)
 *
 * 模拟 XJ380 操作系统的核心环境：ELF 加载、xapi 系统调用、
 * 虚拟文件系统、GUI 渲染（通过 SDL2）。
 *
 * 无 Python 层, 全路径 C 级别直通。
 */

#ifndef XJ380_EMU_H
#define XJ380_EMU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ================================================================
 * 内存布局常量 (参考 XJ380 API 手册 1-2)
 * ================================================================ */
#define XJ380_TEXT_BASE      0x200000ULL   /* 代码段基址 (-Ttext=0x200000)    */
#define XJ380_STACK_BASE     0x800000ULL   /* 栈底                            */
#define XJ380_STACK_SIZE     0x100000ULL   /* 1MB 栈                          */
#define XJ380_HEAP_BASE      0x900000ULL   /* 堆基址                          */
#define XJ380_HEAP_SIZE      0x1000000ULL  /* 16MB 堆                         */
#define XJ380_TRAMP_BASE     0xFFFF0000ULL /* xapi trampoline 页              */
#define XJ380_TRAMP_SIZE     0x10000ULL    /* 64KB，每个函数占 8 字节          */
#define XJ380_VFS_BASE       0xFF000000ULL /* 虚拟文件系统映射区              */

/* XJ380 专有类型 (手册 1-3) */
typedef int8_t   INT8;
typedef uint8_t  UINT8;
typedef int16_t  INT16;
typedef uint16_t UINT16;
typedef int32_t  INT32;
typedef uint32_t UINT32;
typedef int64_t  INT64;
typedef uint64_t UINT64;
typedef char*    WSTR;                     /* UTF-8 字符串 */

/* 窗口句柄 */
typedef uint64_t HDLE;

/* 窗口类型 (手册 4-1-1) */
typedef struct {
    UINT32  width;
    UINT32  height;
    WSTR    title;
    UINT8   sets;
} XWINDOW;

#define XWIN_NORMAL           0x00
#define XWIN_FRAME_OFF        0x01
#define XWIN_FULL_SCR         0x02
#define XWIN_SUPPORT_RESIZEABLE 0x04

/* 文件类型 (手册 3-2-1) */
typedef struct {
    UINT64  length;
    void*   buffer;
} XFILE;

/* 目录条目 (手册 3-2-4) */
typedef struct {
    char    filename[256];
    UINT64  length;
    UINT64  filetype;   /* 0=文件, 1=文件夹 */
} DirNode;

/* 用户信息 (手册 1-4) — 必须与 xtuiapi.h SDK 定义一致 */
typedef struct {
    char     name[64];
    UINT32   user_type;    /* 0=Root, 1=System, 2=Admin, 3=Visitor, 4=Custom */
} UserInfo;

/* 任务信息 (手册 3-4-4) — 必须与 libsys.h SDK 定义一致 */
#define XJ380_TASK_NAME_LEN 32
typedef struct {
    UINT64 pid, ppid, tid, cpu_id, task_level, thread_count, window_count, memory_bytes;
    UINT32 process_status, thread_status;
    char    process_name[XJ380_TASK_NAME_LEN];
    char    thread_name[XJ380_TASK_NAME_LEN];
} XapiTaskInfo;

/* 时间类型 (手册 3-5-4) — 必须与 xtuiapi.h SDK 定义一致 */
typedef struct {
    INT32  tm_sec;
    INT32  tm_min;
    INT32  tm_hour;
    INT32  tm_mday;
    INT32  tm_mon;
    INT32  tm_year;
    INT32  tm_wday;
    INT32  tm_yday;
    INT32  tm_isdst;
} TimeType;

/* 右键菜单项 (手册 4-7-1) */
typedef struct {
    UINT64  CRLid;
    WSTR    text;
} RightMenuItem;

/* ================================================================
 * 模拟器主结构
 * ================================================================ */
typedef struct xj380_emu xj380_emu_t;

/* ================================================================
 * 公共 API
 * ================================================================ */

/* 创建/销毁模拟器实例 */
xj380_emu_t* xj380_create(void);
void         xj380_destroy(xj380_emu_t *emu);

/* 加载 ELF/EPF 二进制 */
int  xj380_load_elf(xj380_emu_t *emu, const char *path);

/* 运行（阻塞直到程序退出） */
int  xj380_run(xj380_emu_t *emu, int argc, char **argv);

/* 注册 GUI 渲染回调（可选，不设置则用内置 SDL2 后端） */
typedef void (*xj380_render_cb)(void *window, int w, int h);
void xj380_set_render_callback(xj380_emu_t *emu, xj380_render_cb cb);

/* 获取错误信息 */
const char* xj380_strerror(xj380_emu_t *emu);

/* 获取 Unicorn 引擎句柄 (GUI 事件注入需要) */
struct uc_struct;
struct uc_struct* xj380_get_uc(xj380_emu_t *emu);

/* 模拟器内存访问 (GUI 后端也需要) */
int  xj380_mem_read(xj380_emu_t *emu, uint64_t addr, void *dst, size_t len);
int  xj380_mem_write(xj380_emu_t *emu, uint64_t addr, const void *src, size_t len);
int  xj380_mem_read_str(xj380_emu_t *emu, uint64_t addr, char *buf, size_t max);

/* ================================================================
 * 内部 — xapi 系统调用号
 * ================================================================ */
typedef enum {
    /* 文本 I/O */
    XAPI_OUTPUT = 0,
    XAPI_INPUT,
    XAPI_GETLINE,
    XAPI_GETCH,
    XAPI_ENDLINE,
    XAPI_PRINTLINE,
    XAPI_PRINTF,
    XAPI_OUTPUTSERIAL,

    /* 文件操作 */
    XAPI_OPENFILE,
    XAPI_CLOSEFILE,
    XAPI_SEARCHFILE,
    XAPI_MKDIR,
    XAPI_CREATEFILE,
    XAPI_DELETEFILE,
    XAPI_RENAMEFILE,
    XAPI_READFILE,
    XAPI_WRITEFILE,
    XAPI_RMDIR,

    /* 类型转换 */
    XAPI_CHAR2INT,
    XAPI_INT2CHAR,
    XAPI_HEX2CHAR,
    XAPI_TORGB,
    XAPI_TORGBA,

    /* 进程与线程 */
    XAPI_FORK,
    XAPI_EXECVE,
    XAPI_EXIT,
    XAPI_GETTASKLIST,
    XAPI_KILLPROCESS,

    /* 系统信息 */
    XAPI_GETSYSTEMVERSION,
    XAPI_GETTIME,
    XAPI_GETCURRENTUSER,
    XAPI_GETTIMEX,
    XAPI_GETCPUMODEL,
    XAPI_GETMEMORYSIZE,

    /* 系统消息及服务 */
    XAPI_BROKEN,
    XAPI_SENDAPPMESSAGE,
    XAPI_SLEEP,
    XAPI_RUN,
    XAPI_RUNARGS,
    XAPI_FLUSHTIME,
    XAPI_SETMSGPRCOR,

    /* 内存 */
    XAPI_ALLOCATEMEMORY,
    XAPI_FREEMEMORY,
    XAPI_MAPMEMORY,

    /* GUI — 窗口 */
    XAPI_CREATEWINDOW,
    XAPI_SETWINDOWTITLE,
    XAPI_CLOSEWINDOW,
    XAPI_SETICON,
    XAPI_GETWINDOWSIZE,

    /* GUI — 绘图 */
    XAPI_DRAWPOINT,
    XAPI_DRAWLINE,
    XAPI_DRAWRECT,
    XAPI_DRAWTEXT,
    XAPI_DRAWTEXTL,
    XAPI_DRAWSWTEXT,
    XAPI_CALCTEXTWIDTH,
    XAPI_DRAWSVG,
    XAPI_DRAWFA,

    /* GUI — 图片 */
    XAPI_DRAWBMP,
    XAPI_DRAWPNG,
    XAPI_DRAWPICTURE,
    XAPI_GETPICSIZE,

    /* GUI — framebuffer */
    XAPI_READBUFFER,
    XAPI_WRITEBUFFER,
    XAPI_READBUFFERA,
    XAPI_WRITEBUFFERA,
    XAPI_REFRESHWINDOW,
    XAPI_REFRESHPARTWINDOW,

    /* GUI — 控件 */
    XAPI_BUTTON,
    XAPI_EMPBUTTON,
    XAPI_DELETEBUTTON,
    XAPI_REGISTERRIGHTBUTTONMENU,
    XAPI_DELETERIGHTBUTTONMENU,

    XAPI_COUNT
} xapi_syscall_t;

#ifdef __cplusplus
}
#endif

#endif /* XJ380_EMU_H */
