# 笑死我了重制版

XJ380 操作系统应用程序模拟器。基于 Unicorn Engine C API。

## 快速开始

```bash
# 安装依赖 (Ubuntu/Debian)
sudo apt install -y libunicorn-dev libsdl2-dev libsdl2-image-dev

# 编译
make          # TUI 模式 (纯终端)
make gui      # GUI 模式 (SDL2 窗口渲染)
make release  # 发布版 (g++ 链接 -O3 -flto)

# 运行
./xj380_emu program.epf
```

## 依赖

| 库 | 用途 |
|---|------|
| [Unicorn Engine](https://www.unicorn-engine.org/) | x86_64 CPU 模拟 |
| [SDL2](https://www.libsdl.org/) | 窗口创建和事件处理 |
| [SDL2_image](https://github.com/libsdl-org/SDL_image) | 图片加载(BMP/PNG/JPEG) |
| [nanosvg](https://github.com/memononen/nanosvg) | SVG 矢量图渲染 (已内置) |

## 构建目标

| 命令 | 编译器 | 优化 | 说明 |
|------|--------|------|------|
| `make` | GCC | `-O3` | TUI 模式，无 GUI 依赖 |
| `make gui` | GCC | `-O3` | 完整 GUI 支持 |
| `make dev` | Clang | `-O0 -g` | 开发调试，ASan+UBSan，指令 trace |
| `make release` | GCC 编译 + G++ 链接 | `-O3 -flto -march=native` | 最终优化版 |

### Windows (Cygwin)

```bash
make -f Makefile.Windows          # TUI
make -f Makefile.Windows gui      # GUI
make -f Makefile.Windows release  # 发布版
```

## 使用

```bash
./xj380_emu <XJ380 ELF/EPF 文件>
```

模拟器自动识别两种二进制格式：
- **xxcc 编译的 .epf** — 扫描 `.text` 段中的 `syscall` 指令进行拦截
- **测试汇编 .elf** — 通过 trampoline 页直接调用 xapi 函数

## 支持的功能

### TUI (43 个函数)

| 类别 | 函数 |
|------|------|
| 文本 I/O | Output, Input, Getch, EndLine, PrintLine, Printf, Getline, OutputSerial |
| 文件 | OpenFile, CloseFile, SearchFile, Mkdir, CreateFile, DeleteFile, RenameFile, ReadFile, WriteFile, Rmdir |
| 转换 | xcr_char2int, xcr_int2char, xcr_hex2char, toRGB, toRGBA |
| 进程 | Fork, Execve, Exit, GetTaskList, KillProcess |
| 系统信息 | GetSystemVersion, GetTime, GetCurrentUser, GetTimeX, GetCpuModel, GetMemorySize |
| 消息 | Broken, SendAppMessage, Sleep, Run, RunArgs, FlushTime |
| 内存 | AllocateMemory, FreeMemory, MapMemory |

### GUI (27 个函数)

| 类别 | 函数 |
|------|------|
| 窗口 | CreateWindow, SetWindowTitle, CloseWindow, SetIcon, GetWindowSize, SetMsgPrcor |
| 绘图 | DrawPoint, DrawLine, DrawRect, DrawText, DrawTextl, DrawSWText, CalcTextWidth, DrawSvg, DrawFA |
| 图片 | DrawBMP, DrawPNG, DrawPicture, GetPicSize |
| Framebuffer | ReadBuffer, WriteBuffer, ReadBufferA, WriteBufferA, RefreshWindow, RefreshPartWindow |
| 控件 | Button, ButtonEmp, DeleteButton, RegisterRightButtonMenu, DeleteRightButtonMenu |

## 测试

```bash
make test           # 快速回归测试
make test-tui       # TUI 全覆盖测试 (40+ 函数)
./xj380_emu test_tui.elf
```

用 xxcc 编译的完整测试：

```bash
xxcc test_all.cpp -o test_all.epf
./xj380_emu test_all.epf
```

## 架构

```
┌────────────────────────────────────────────────────┐
│                  xj380_emu                          │
├──────────────┬─────────────────┬───────────────────┤
│  ELF Loader  │  Syscall Hook   │  Trampoline Hook  │
│  (段合并)     │  (扫描 0F 05)   │  (0xFFFF0000)     │
├──────────────┴─────────────────┴───────────────────┤
│              42 TUI + 28 GUI handlers              │
├──────────────┬─────────────────────────────────────┤
│  VFS         │  SDL2 Backend (窗口/绘图/事件)       │
│  (文件模拟)   │  nanosvg (SVG 渲染)                 │
├──────────────┴─────────────────────────────────────┤
│              Unicorn Engine (x86_64)                │
└────────────────────────────────────────────────────┘
```

## Syscall ABI

XJ380 二进制通过 `enter_syscall` 函数调用内核，该函数将参数重排为标准 Linux x86_64 syscall 约定：

```
RAX = XJ380 syscall 编号 (基址 7380)
RDI = arg1, RSI = arg2, RDX = arg3
R10 = arg4, R8  = arg5, R9  = arg6
```

Syscall 编号定义见官方 SDK `libsys.h`。

## 文件结构

```
xj380_emu/
├── xj380_emu.c         核心模拟器 (ELF加载、syscall分发、TUI handlers)
├── xj380_emu.h         公共头文件
├── xj380_gui.c         GUI 后端 (SDL2窗口/绘图/事件)
├── xj380_gui.h         GUI 头文件
├── main.c              CLI 入口
├── nanosvg.h           SVG 解析库
├── nanosvgrast.h       SVG 光栅化库
├── test_xj380.S        快速回归测试
├── test_tui.S          TUI 全覆盖测试
├── test_all.cpp        API 全覆盖测试 (需 xxcc 编译)
├── Makefile            Linux 构建
├── Makefile.Windows    Windows Cygwin 构建
└── README.md           本文件
```

## 性能

- **TUI 模式**：全速 Unicorn 执行，无额外开销
- **GUI 模式**：时间分片，每 50,000 条指令切出处理 SDL2 事件，约 60fps 窗口刷新

## License

MIT
