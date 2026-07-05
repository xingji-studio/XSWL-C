# XSWL-C

[中文](README.zh-CN.md)

XSWL-C is an XJ380 application emulator written in C. It runs XJ380 ELF/EPF
programs through Unicorn Engine and implements the common XAPI TUI and GUI
calls on Linux with SDL3.

## Dependencies

Ubuntu/Debian:

```bash
sudo apt install -y cmake ninja-build pkg-config libunicorn-dev libsdl3-dev
```

| Library | Purpose |
|---|---|
| Unicorn Engine | x86_64 CPU emulation |
| SDL3 | Window management, events, BMP/PNG image loading |
| nanosvg | SVG rasterization, vendored in this repository |

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Common options:

| Option | Default | Description |
|---|---:|---|
| `XSWL_ENABLE_DEV_SANITIZERS` | `OFF` | Enable ASan/UBSan for Debug builds |
| `XSWL_ENABLE_TRACE` | `OFF` | Enable instruction/syscall trace logs |
| `XSWL_ENABLE_NATIVE_RELEASE` | `OFF` | Add `-march=native` for Release builds |

Debug build:

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DXSWL_ENABLE_DEV_SANITIZERS=ON -DXSWL_ENABLE_TRACE=ON
cmake --build build-dev
```

## Run

By default, XSWL-C uses the native fast path:

```bash
./build/xswl <XJ380 ELF/EPF file>
```

Use `--nodebug` before the program path to suppress debug logs:

```bash
./build/xswl --nodebug ./app.elf
```

The old Unicorn emulation path is still available for compatibility:

```bash
./build/xswl --unicorn ./app.elf
```

`--emu` is accepted as an alias for `--unicorn`. `--native` can still be passed
explicitly, but it is the default.

Native mode is Linux-only. On Windows, run XSWL-C through WSL2 for native mode
or use the Unicorn path for a portable emulator backend.

Native mode maps static x86_64 XJ380 ELF programs into the host process and
patches their `enter_syscall` symbol to call XSWL-C directly. It is still a
compatibility subset, but now covers the native smoke path plus common GUI
startup calls: `brk`, `Output`, `PrintLine`, `Exit`, `OpenFile`, `ReadFile`,
`CloseFile`, `SearchFile`, selected POSIX file calls, terminal input stubs,
`CreateWindow`, `GetWindowSize`, `SetMsgPrcor`, `SetWindowTitle`, basic
text/drawing no-ops, `ReadBuffer`, `WriteBuffer`, `RefreshWindow`, `FlushTime`,
`Sleep`, self-signal delivery for `rt_sigaction`/`rt_sigprocmask`/`kill`, and
common system queries. POSIX coverage includes the small file, `pipe`,
`select`, `writev`, `mmap`, `mprotect`, `munmap`, `getdents`, and `/dev/fb0`
paths used by the current XJ380 demos. Guest-created files are mapped to
private `/tmp/xswl-native-*` host files; relative guest paths are resolved
relative to the XJ380 program directory when possible. Private terminal service calls used by XJ380's console startup are
simulated. Private installer service calls are also simulated, but installation
and boot repair always fail safely so native mode never writes host disks.

Native `fork` is supported for non-GUI programs. `SYS_FORK` and `XAPI_FORK`
return a child PID in the parent and `0` in the child; `SYS_WAIT4` can reap
children and reports normal exit status. If a native program has created a
window, `fork` returns failure to avoid duplicating GUI/window state.
`Execve("/apps/system/shell.elf")` has a narrow compatibility path for XJ380's
terminal launcher parent. Other exec calls and unsupported XAPI calls fail fast
with a diagnostic instead of falling back inside the same process. Use the
Unicorn path for compatibility checks.

## Test

```bash
cmake --build build --target xswl_run_gui_events
cmake --build build --target xswl_run_native_smoke
cmake --build build --target xswl_run_native_fork
cmake --build build --target xswl_run_native_posix
```

The GUI event test uses SDL's dummy video driver, so it can run without a
display server. The native smoke test verifies fixed-address ELF loading,
`enter_syscall` patching, `brk`, basic file access, minimal GUI/window calls,
framebuffer no-op compatibility, `Sleep`, `PrintLine`, and `Exit`.
The native fork test verifies terminal stubs, installer private syscall stubs,
`SYS_GETGROUPS`, `SYS_FORK`, `XAPI_FORK`, `SYS_WAIT4`, child exit status, and
the non-GUI fork path.
The native POSIX test verifies writable temp files, `fstat`, `lseek`, `pipe`,
`select`, anonymous `mmap`, memory protection calls, and the `/dev/fb0`
`ioctl`/`mmap` compatibility path.

## Covered XAPI Areas

| Area | Examples |
|---|---|
| TUI | `Output`, `Input`, `Getch`, `Printf`, `OutputSerial` |
| Files | `OpenFile`, `ReadFile`, `WriteFile`, `CreateFile`, `DeleteFile` |
| Processes | `Fork`, `Execve`, `Exit`, `GetTaskList`, `KillProcess` |
| System | `GetSystemVersion`, `GetTime`, `GetCurrentUser`, `Sleep` |
| Memory | `AllocateMemory`, `FreeMemory`, `MapMemory` |
| GUI windows | `CreateWindow`, `SetWindowTitle`, `SetMsgPrcor` |
| Drawing | `DrawPoint`, `DrawLine`, `DrawRect`, `DrawText`, `DrawSvg` |
| Images | `DrawBMP`, `DrawPNG`, `DrawPicture`, `GetPicSize` |
| Framebuffer | `ReadBuffer`, `WriteBuffer`, `WriteBufferA`, `RefreshWindow` |

## Notes

XSWL-C supports the two execution paths currently used by XJ380 programs:

- scanning normal XJ380 ELF/EPF programs for syscall instructions and hooking them;
- providing a trampoline page for small assembly tests that call XAPI directly.

The emulator has its own in-memory VFS. When a program opens a file, XSWL-C can
also import it from the host filesystem by path.

## License

MIT
