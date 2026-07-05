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
| SDL3 | 窗口管理、事件、BMP/PNG 图片加载 |
| nanosvg | SVG 光栅化，已放在仓库内 |

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

XSWL-C 默认使用 native 快速路径：

```bash
./build/xswl <XJ380 ELF/EPF 文件>
```

如果不想输出调试日志，可以在程序路径前加 `--nodebug`：

```bash
./build/xswl --nodebug ./app.elf
```

旧的 Unicorn 模拟路径仍然保留，用于兼容性检查：

```bash
./build/xswl --unicorn ./app.elf
```

`--emu` 是 `--unicorn` 的别名。`--native` 仍然可以显式传入，但它已经是默认值。

Native 模式只支持 Linux。Windows 上如果要用 native 模式，请通过 WSL2 运行
XSWL-C；如果要原生 Windows 运行，请使用 Unicorn 路径作为可移植模拟后端。

Native 模式会把静态 x86_64 XJ380 ELF 程序映射进宿主进程，并 patch 它们的
`enter_syscall` 符号，让程序直接调用 XSWL-C。它仍然只是兼容性子集，但已经覆盖
native smoke 路径和常见 GUI 启动调用：`brk`、`Output`、`PrintLine`、`Exit`、
`OpenFile`、`ReadFile`、`CloseFile`、`SearchFile`、部分 POSIX 文件调用、终端输入
stub、`CreateWindow`、`GetWindowSize`、`SetMsgPrcor`、`SetWindowTitle`、基础文本/
绘图 no-op、`ReadBuffer`、`WriteBuffer`、`RefreshWindow`、`FlushTime`、`Sleep`、
`rt_sigaction`/`rt_sigprocmask`/`kill` 的自发信号投递，以及常见系统查询。POSIX
覆盖了当前 XJ380 demo 会用到的小文件、`pipe`、`select`、`writev`、`mmap`、
`mprotect`、`munmap`、`getdents` 和 `/dev/fb0` 路径。guest 创建的文件会映射到
私有的 `/tmp/xswl-native-*` 宿主文件；相对 guest 路径会优先按 XJ380 程序所在目录
解析。XJ380 控制台
启动使用的私有终端服务调用会被模拟。私有安装器服务调用也会被模拟，但安装和
启动修复总是安全失败，所以 native 模式不会写宿主磁盘。

Native `fork` 支持非 GUI 程序。`SYS_FORK` 和 `XAPI_FORK` 会在父进程返回子 PID，
在子进程返回 `0`；`SYS_WAIT4` 可以回收子进程并报告正常退出状态。如果 native
程序已经创建窗口，`fork` 会返回失败，以避免复制 GUI/window 状态。
`Execve("/apps/system/shell.elf")` 为 XJ380 终端启动器父进程提供了窄兼容路径。
其他 exec 调用和不支持的 XAPI 调用会快速失败并输出诊断信息，不会在同一进程里
伪装继续执行。兼容性检查请使用 Unicorn 路径。

## 测试

```bash
cmake --build build --target xswl_run_gui_events
cmake --build build --target xswl_run_native_smoke
cmake --build build --target xswl_run_native_fork
cmake --build build --target xswl_run_native_posix
```

GUI 事件测试使用 SDL dummy video driver，所以可以在没有显示服务器的环境里跑。
Native smoke 测试验证固定地址 ELF 加载、`enter_syscall` patch、`brk`、基础文件访问、
最小 GUI/window 调用、framebuffer no-op 兼容、`Sleep`、`PrintLine` 和 `Exit`。
Native fork 测试验证终端 stub、安装器私有 syscall stub、`SYS_GETGROUPS`、
`SYS_FORK`、`XAPI_FORK`、`SYS_WAIT4`、子进程退出状态和非 GUI fork 路径。
Native POSIX 测试验证可写临时文件、`fstat`、`lseek`、`pipe`、`select`、匿名
`mmap`、内存保护调用，以及 `/dev/fb0` 的 `ioctl`/`mmap` 兼容路径。

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

模拟器有自己的内存 VFS。程序打开文件时，XSWL-C 也可以按路径从宿主系统导入文件。

## 许可证

MIT
