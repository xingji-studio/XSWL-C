# XSWL-C

XSWL-C is a C implementation of an XJ380 application emulator. It loads XJ380
ELF/EPF programs, runs x86_64 code through Unicorn Engine, and implements a
practical subset of the XAPI TUI and GUI calls on Linux with SDL2.

Chinese documentation: [README.zh-CN.md](README.zh-CN.md)

## Requirements

Ubuntu/Debian:

```bash
sudo apt install -y cmake ninja-build pkg-config libunicorn-dev libsdl2-dev libsdl2-image-dev
```

Main dependencies:

| Library | Use |
|---|---|
| Unicorn Engine | x86_64 CPU emulation |
| SDL2 | GUI windows and events |
| SDL2_image | BMP/PNG/JPEG image loading |
| nanosvg | SVG rasterization, vendored |

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful build options:

| Option | Default | Description |
|---|---:|---|
| `XSWL_ENABLE_DEV_SANITIZERS` | `OFF` | Enable ASan/UBSan in Debug builds |
| `XSWL_ENABLE_TRACE` | `OFF` | Enable instruction/syscall trace logging |
| `XSWL_ENABLE_NATIVE_RELEASE` | `OFF` | Add `-march=native` to Release builds |

Development build:

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DXSWL_ENABLE_DEV_SANITIZERS=ON -DXSWL_ENABLE_TRACE=ON
cmake --build build-dev
```

## Run

```bash
./build/xswl <XJ380 ELF/EPF file>
```

`nodebug` or `--nodebug` can be passed before the program path to reduce log
output:

```bash
./build/xswl nodebug ./app.elf
```

## Tests

```bash
cmake --build build --target xswl_run_test
cmake --build build --target xswl_run_gui_events
```

The GUI event test uses SDL's dummy video driver and is suitable for CI or
headless shells.

## Supported XAPI Areas

Current coverage includes:

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

XSWL-C supports two execution styles used by current XJ380 binaries:

- syscall scanning for normal XJ380 ELF/EPF programs;
- a trampoline page for small assembly tests that call XAPI directly.

The emulator keeps its own in-memory VFS for guest files and can import host
files on demand when a guest opens a path.

## License

MIT
