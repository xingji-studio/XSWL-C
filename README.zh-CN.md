# XSWL-C

[English](README.md)

XSWL-C 是一个用 C 写的 XJ380 应用程序模拟器。它通过 Unicorn Engine 执行
XJ380 ELF/EPF 程序，并在 Linux 上用 SDL3 实现常用的 XAPI TUI 和 GUI 调用。

## 依赖

Ubuntu/Debian：

```bash
sudo apt install -y cmake ninja-build pkg-config libunicorn-dev libsdl3-dev
```

| 库 | 用途 |
|---|---|
| Unicorn Engine | x86_64 CPU 模拟 |
| SDL3 | 窗口、事件和 BMP/PNG 图片加载 |
| nanosvg | SVG 渲染，已放在仓库内 |

## 编译

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

常用选项：

| 选项 | 默认 | 说明 |
|---|---:|---|
| `XSWL_ENABLE_DEV_SANITIZERS` | `OFF` | Debug 构建启用 ASan/UBSan |
| `XSWL_ENABLE_TRACE` | `OFF` | 启用指令/syscall 跟踪日志 |
| `XSWL_ENABLE_NATIVE_RELEASE` | `OFF` | Release 构建添加 `-march=native` |

调试构建：

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DXSWL_ENABLE_DEV_SANITIZERS=ON -DXSWL_ENABLE_TRACE=ON
cmake --build build-dev
```

## 运行

```bash
./build/xswl <XJ380 ELF/EPF 文件>
```

如果不想输出调试日志，可以在程序路径前加 `nodebug` 或 `--nodebug`：

```bash
./build/xswl nodebug ./app.elf
```

## 测试

```bash
cmake --build build --target xswl_run_gui_events
```

GUI 事件测试使用 SDL dummy video driver，可以在没有显示器的环境里跑。

## 目前覆盖的 XAPI

| 范围 | 示例 |
|---|---|
| TUI | `Output`, `Input`, `Getch`, `Printf`, `OutputSerial` |
| 文件 | `OpenFile`, `ReadFile`, `WriteFile`, `CreateFile`, `DeleteFile` |
| 进程 | `Fork`, `Execve`, `Exit`, `GetTaskList`, `KillProcess` |
| 系统 | `GetSystemVersion`, `GetTime`, `GetCurrentUser`, `Sleep` |
| 内存 | `AllocateMemory`, `FreeMemory`, `MapMemory` |
| GUI 窗口 | `CreateWindow`, `SetWindowTitle`, `SetMsgPrcor` |
| 绘图 | `DrawPoint`, `DrawLine`, `DrawRect`, `DrawText`, `DrawSvg` |
| 图片 | `DrawBMP`, `DrawPNG`, `DrawPicture`, `GetPicSize` |
| Framebuffer | `ReadBuffer`, `WriteBuffer`, `WriteBufferA`, `RefreshWindow` |

## 说明

XSWL-C 支持两种当前 XJ380 程序会用到的执行方式：

- 扫描普通 XJ380 ELF/EPF 程序里的 syscall 指令并挂钩；
- 为小型汇编测试提供 trampoline 页，直接调用 XAPI。

模拟器有自己的内存 VFS。程序打开文件时，也可以按路径从宿主系统导入文件。

## 许可证

MIT
