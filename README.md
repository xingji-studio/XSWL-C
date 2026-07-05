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

```bash
./build/xswl <XJ380 ELF/EPF file>
```

Use `nodebug` or `--nodebug` before the program path to suppress debug logs:

```bash
./build/xswl nodebug ./app.elf
```

Experimental native fast path:

```bash
./build/xswl --native ./app.elf
```

Native mode maps static x86_64 XJ380 ELF programs into the host process and
patches their `enter_syscall` symbol to call XSWL-C directly. It is still a
compatibility subset, but now covers the native smoke path plus common GUI
startup calls: `brk`, `Output`, `PrintLine`, `Exit`, `OpenFile`, `ReadFile`,
`CloseFile`, `CreateWindow`, `GetWindowSize`, `SetMsgPrcor`, `DrawRect`,
`ReadBuffer`, `WriteBuffer`, `RefreshWindow`, `FlushTime`, and `Sleep`.
Unsupported XAPI calls fail fast with a diagnostic instead of falling back
inside the same process. Use the default Unicorn path for full compatibility.

## Test

```bash
cmake --build build --target xswl_run_gui_events
cmake --build build --target xswl_run_native_smoke
```

The GUI event test uses SDL's dummy video driver, so it can run without a
display server. The native smoke test verifies fixed-address ELF loading,
`enter_syscall` patching, `brk`, basic file access, minimal GUI/window calls,
framebuffer no-op compatibility, `Sleep`, `PrintLine`, and `Exit`.

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
