# Porting to PC

## The Challenge

The Xbox dashboard source code is approximately 25,000 lines of C++ that assumes it's running on an Xbox. Every file I/O call uses `CreateFile`/`ReadFile`. Every draw call goes through Direct3D 8. Every input read polls Xbox gamepads via XInput. The filesystem uses drive letter mappings (`Q:\Xips\default.xip`) from the modded Xbox ecosystem. Types are Windows-specific (`DWORD`, `HANDLE`, `BYTE`).

The goal: make all of this run on macOS, Linux, and Windows with SDL2 and OpenGL: then strip out the Xbox scaffolding entirely.

## Strategy: One Tree, Targeted Divergence

The initial UIX Desktop port lived in a separate repo and forked aggressively to escape the `#ifdef` swamp. That made desktop a clean codebase but meant every Xbox-side fix had to be re-ported by hand. The two trees were merged into the Theseus repo on 2026-04-10 and have been converging ever since.

The current shape: one repo, four source roots that share whatever doesn't need to diverge.

```
theseus/
  engine/    Pure logic, no platform deps (VM, nodes, math, settings, dotfield, locale)
  shared/    Cross-platform with Win32 types (file I/O, titles, audio, screen, settings file)
  render/    D3D8/graphics (scene graph, textures, materials, shape rendering, hud)
  xbox/      Xbox-only (XTL init, modchip, kernel APIs, main.cpp)
  desktop/   Desktop-only (SDL/OpenGL platform layer, ImGui tools, stubs, platform_shim.h)
```

Every file that compiles cleanly on both targets lives in `engine`/`shared`/`render`. Files that genuinely diverge (joystick goes to XInput on Xbox vs. SDL2 on desktop, audio goes to DirectSound vs. SDL_mixer, etc.) live under `xbox/` or `desktop/` and are picked up by their target's build only. The Xbox build is `make` (cross-compiles via clang+lld+cxbe to a bootable XBE); the desktop build is `make desktop` (native macOS/Linux) or `make desktop-win64` (mingw cross-compile to a Windows .exe). Same Makefile.

The migration from "everything under `desktop/`" to "shared canonical source where possible" is ongoing. As of 2026-05-02 the engine, scene graph, rendering, and asset-loading code (including `xap_compile.cpp`, `xap_vm.cpp`, `tmap_system.cpp`, `asset_loader.cpp`, the node system, and the leaf nodes for screen, date, settings, hud, dotfield, fileutil, activefile, etc.) lives in `engine/`, `shared/`, and `render/` and is consumed by both builds. The remaining drift between the two builds sits in the desktop-only platform tooling (SDL window, ImGui developer tools, libmpv media player, HDD image browser) and a small set of files still being moved.

The port is a mix of techniques depending on what each subsystem required:

- **Virtualization**: The filesystem layer intercepts Win32 file I/O calls and redirects them to a local directory structure. The engine calls `CreateFile("Q:\\Xips\\default.xip")` and gets back a valid handle without knowing it's actually opening `./xboxfs/Q/Xips/default.xip` via `fopen()`.
- **Reimplementation**: D3D8 rendering was completely rebuilt on top of OpenGL. The engine calls the same `IDirect3DDevice8` interface, but every method is backed by OpenGL state and GLSL shaders. Audio was fully replaced with SDL_mixer.
- **Type compatibility**: Win32 types (`DWORD`, `HANDLE`, `BYTE`, etc.) are typedef'd to standard C types so the code compiles on non-Windows platforms.
- **Stubbing**: Xbox-only subsystems (Xbox Live, hardware management, etc.) are replaced with no-op stubs that satisfy the linker. Node types referenced by XAP scripts but irrelevant on desktop get stub classes so the script VM doesn't fail on unknown types.
- **Direct rewrite**: The input system (CJoystick) was rewritten from XInput to direct SDL2 calls. The EEPROM config system was replaced with hardcoded desktop defaults. Debug infrastructure (TRACE/ASSERT) was rewired to stderr. All dead Xbox code paths were removed.

```
Xbox Code:  CreateFile("Q:\\Xips\\default.xip", ...)
                |
                v
Platform Layer: XboxFS_CreateFileA() translates path to "./xboxfs/Q/Xips/default.xip"
                Opens with fopen(), returns a HANDLE-compatible wrapper
                |
                v
Xbox Code:  ReadFile(handle, buffer, bytesToRead, &bytesRead, NULL)
                |
                v
Platform Layer: Calls fread() on the underlying FILE*
```

The filesystem example above is genuine virtualization: the engine doesn't know it's not on an Xbox. But other parts of the port are more hands-on. The D3D8 translation layer, for instance, isn't just intercepting calls. It's a from-scratch OpenGL renderer that happens to expose a D3D8-shaped interface.

## The Platform Layer

### sdl_platform.h: The Type Foundation

This header makes non-Windows platforms speak Windows. It defines every Win32 type the Xbox code uses:

```cpp
typedef uint32_t    DWORD;
typedef uint8_t     BYTE;
typedef uint16_t    WORD;
typedef int32_t     LONG;
typedef int         BOOL;
typedef void*       HANDLE;
typedef void*       LPVOID;
typedef const char* LPCSTR;
// ~100 more type definitions
```

It also implements Win32 API functions using POSIX/SDL equivalents:
- `CreateFileA()` / `ReadFile()` / `WriteFile()` / `CloseHandle()`: file I/O via `fopen`/`fread`/`fwrite`/`fclose`
- `FindFirstFileA()` / `FindNextFileA()` / `FindClose()`: directory enumeration via `opendir`/`readdir`, with virtual game injection from `games.ini`
- `GetFileAttributesA()` / `GetFileAttributesExA()`: file stat via `stat()`
- `GetTickCount()`: millisecond timer via `SDL_GetTicks()`
- `Sleep()`: via `SDL_Delay()`
- `GlobalMemoryStatus()`: real host memory via `sysctl` (macOS) or `sysinfo` (Linux)
- `OutputDebugString()`: via `fprintf(stderr, ...)`

Xbox-specific APIs that originally lived here have been removed entirely. The Xbox EEPROM configuration system (`XQueryValue`/`XSetValue`) was replaced by hardcoding sensible defaults directly in `config.cpp` (stereo audio, no parental controls, English language). The XInput gamepad bridge was replaced by direct SDL2 calls in `joystick.cpp`. Hardware monitoring (SMBus), memory unit management, soundtrack database, power management, and DirectSound were all removed after confirming zero callers.

On Windows, most of these types already exist (they're the real Win32 types). The challenge there is that `DWORD` on MSVC is `unsigned long` while our typedef makes it `uint32_t` (`unsigned int`). Same size, but MSVC considers them different types for pointer compatibility. This required a `LPDW()` macro to bridge the gap at API boundaries.

### xboxfs.h: The Virtual Filesystem

The Theseus codebase uses drive letter mappings from the modded Xbox ecosystem: `C:\`, `E:\`, `Q:\Xips\`, etc. (These are scene conventions, not how retail Xbox addresses partitions internally.) On desktop, these map to a local directory structure:

```
xboxfs/
  C/
    version
    UIX Configs/
      config.ini
      games.ini
  Q/
    Xips/
      default.xip
      games.xip
      settings.xip
      ...
    Skins/
      Stock/
    Fonts/
      xbox.xtf
      tahoma.ttf
    System/
      config.ini
      desktop.ini
  E/
    Games/
    UDATA/
```

`xboxfs.h` provides:

1. **Path translation**: `XboxFS_TranslatePath("Q:\\Xips\\default.xip")` returns `"xboxfs/Q/Xips/default.xip"`
2. **Case-insensitive lookup**: Xbox filenames are case-insensitive (NTFS). macOS is too (by default), but Linux is case-sensitive. The filesystem layer does case-insensitive directory scanning when a direct match fails.
3. **API interception**: `#define CreateFile CreateFileA` and then `#define CreateFileA XboxFS_CreateFileA`, so every file open in the Xbox code goes through the virtual filesystem without changing a single line of Xbox source.
4. **FATX fallback**: For UDATA/TDATA paths, the filesystem can read directly from a qcow2 Xbox HDD image via the FATX reader.

On Windows, this required extra care: Win32's `CreateFile` is a real function, so the wrapper must strip Xbox-specific flags (`FILE_FLAG_NO_BUFFERING`, `FILE_FLAG_OVERLAPPED`) and convert forward slashes back to backslashes before calling the real Win32 API.

### d3d8_sdl.h: D3D8 to OpenGL

This is the largest and most complex part of the platform layer (~2,600 lines). It provides full implementations of:

- `IDirect3D8`: device enumeration (returns one "SDL OpenGL" adapter)
- `IDirect3DDevice8`: the main rendering interface with GLSL shaders, render state tracking, vertex transforms, lighting, fog
- `IDirect3DTexture8`: texture objects backed by OpenGL textures
- `IDirect3DVertexBuffer8`: vertex data with CPU-side falloff computation
- `IDirect3DIndexBuffer8`: index buffers for mesh rendering
- D3DX math library: matrix, vector, and quaternion operations

See [D3D8 to OpenGL Translation](d3d8-translation.md) for the full technical details.

### desktop_stubs.cpp: The Missing Modules

Many Xbox modules are irrelevant on desktop and are excluded from compilation entirely. But the remaining compiled code still references their symbols. `desktop_stubs.cpp` provides no-op implementations for: title collection management (`CTitleArray`), disc management (`DiscManager`), the dashboard's `net::` networking wrapper, Discord RPC configuration, and the XBE executable reader (which serves virtual game metadata from `games.ini` on desktop).

### desktop_nodes.cpp: Script-Visible Nodes

The XAP scripts reference node types that don't exist natively on desktop. If the script VM encounters an unknown node type, it fails. `desktop_nodes.cpp` provides implementations using the same `DECLARE_NODE`/`IMPLEMENT_NODE` pattern as the Xbox source.

**Fully functional nodes** (real native implementations):
- `CAudioClip`: sound effects and music via SDL_mixer, with Xbox IMA ADPCM decoding
- `CSavedGameGrid`: enumerates real Xbox save data from xboxfs and qcow2 FATX images, renders the memory browser with scrolling title pods and save icons
- `CDVDPlayer`: full media playback via libmpv with transport controls, chapter navigation, A-B loop, frame stepping, zoom, and CRT post-processing
- `CMusicCollection`: soundtrack browsing from `xboxfs/Q/Music/`
- `CAudioVisualizer`: FFT visualization from PCM audio capture
- `CMemoryMonitor`: reports real host memory
- `CHardDrive`: drive info with virtual game library integration
- `CTheseusLauncher`: game launching via `system()` / `execv()`

**Stub nodes** (registered so XAP scripts don't crash, return defaults):
- `CXboxLive`, `CLiveAccounts`: Xbox Live account management UI
- `CRecovery`: Xbox system recovery
- `CCopyDestination`: memory unit save data copy
- `CDiscDrive`: physical disc detection

### audio_sdl.cpp: Sound Without DirectSound

The Xbox used DirectSound with a custom DSP for audio. The desktop port replaces this entirely with SDL_mixer:

- `.wav` files are loaded directly via `Mix_LoadWAV()`
- Xbox IMA ADPCM audio (the format used in XIP archives) is decoded in software to PCM before playback
- Music playback uses `Mix_PlayMusic()` for background tracks
- Sound effects use `Mix_PlayChannel()` with mixing
- A PostMix callback captures PCM samples for the audio visualizer

The IMA ADPCM decoder was written from the format specification. It's a standard codec, just packaged in Xbox's custom container format.

## Input: From XInput to SDL2

The Xbox dashboard used Microsoft's XInput API to handle gamepads (4-port, pressure-sensitive face buttons, analog triggers) and the DVD IR remote control kit. `CJoystick::Advance()` looped over all 4 ports every frame, polling device state and firing XAP callbacks (OnADown, OnMoveUp, OnReverse, OnPlay, etc.).

On desktop, `CJoystick` talks directly to SDL2. A single `JoySnapshot` struct captures the merged state of any connected SDL_GameController plus the keyboard. The keyboard mapping (arrows, enter, backspace, WASD, etc.) is applied with OR logic so both input sources work simultaneously. Media keys (`[`, `]`, `I`, `;`, `'`, `0-9`) use poll-based edge detection to fire the same callbacks the IR remote did on Xbox.

All XAP-visible callbacks, the secret key sequence system, screen saver reset, and typomatic repeat logic are preserved identically.

## What Works vs What's Stubbed

**Fully functional on desktop:**
- XIP/XAP engine (scripting VM, scene graph, asset loading)
- 3D rendering (D3D8 reimplemented on OpenGL)
- Audio (SDL_mixer with Xbox IMA ADPCM decoder)
- Media playback (libmpv through the Xbox DVD player UI)
- Skin system (switchable from dashboard settings)
- Filesystem (virtualized to local directory with FATX fallback)
- Input (keyboard + controller via SDL2 GameController)
- Configuration (same config.ini as Xbox, desktop defaults for hardware settings)
- Game launching, Steam import, Title Maker, XAP editor, scene inspector, HDD browser
- Debug infrastructure (TRACE/ASSERT always active, no gating)

**Stubbed (exists so XAP scripts don't crash, returns defaults):**
- Xbox Live / Live Accounts, Recovery, Copy Destination, Disc Drive

## The Windows Problem

On macOS and Linux, the platform layer works cleanly because none of the Win32 APIs exist; everything goes through the stubs. On Windows, it's a different story.

Windows actually has `CreateFile`, `ReadFile`, `DWORD`, `HANDLE`, and all the other Win32 types. The Xbox code compiles against the *real* Win32 implementations instead of going through the virtualization layer. This causes:

1. **Type mismatches**: `DWORD` is `unsigned long` on MSVC but `uint32_t` (unsigned int) in our typedefs. Same size, but MSVC treats them as incompatible types for pointers and references.

2. **Path translation bypass**: `CreateFile("Q:\\Xips\\default.xip")` calls the real Win32 `CreateFile`, which doesn't know about the xboxfs directory mapping.

3. **Flag incompatibility**: Xbox-specific `CreateFile` flags (`FILE_FLAG_NO_BUFFERING`, `FILE_FLAG_OVERLAPPED`) get passed to the real Win32 API and cause unexpected behavior.

4. **Shell behavior**: `system("program.exe \"arg with spaces\"")` goes through `cmd.exe` on Windows, which strips the inner quotes. Replaced with `CreateProcess` for game launching.

The fix was to intercept Win32 APIs on Windows too (`#define CreateFileA XboxFS_CreateFileA`), so even on Windows, file operations go through the virtual filesystem. A `LPDW()` macro bridges `uint32_t*` to `DWORD*` at Win32 API call sites.

## Debugging

The original Xbox dashboard had debug infrastructure (`TRACE`, `ASSERT`, `DumpStack`) that relied on MSVC's CRT debug runtime (`_CrtDbgBreak`, `_CrtSetAllocHook`, `_heapchk`, etc.). The desktop port replaces these with portable equivalents:

- `TRACE()` outputs to stderr (stripping the Xbox `\001` severity prefix)
- `ASSERT()` prints file and line on failure
- `DumpHex()` prints hex dumps
- The script VM stores function names for stack traces

Debug output is always active: no build-type gating. The `_DEBUG` preprocessor gates that used to control this throughout the codebase have been removed. Console output has been cleaned up to only show errors on startup (~8 lines vs the original ~100).

## Build System

A single Makefile at `build/Makefile` drives every target. Different invocations select different output:

- **`make`**: Xbox debug XBE via clang + lld-link + cxbe (cross-compile from macOS/Linux). Reads from `theseus/xbox/` + the shared roots.
- **`make retail`**: same shape but `CONFIG=retail`.
- **`make desktop`**: native macOS or Linux desktop binary, GCC or Clang. Reads from `theseus/desktop/` + the shared roots. Uses system SDL2/SDL2_mixer/OpenGL.
- **`make desktop-win64`**: Windows .exe via mingw-w64 cross-compile from macOS/Linux. Same source list as `make desktop`. Pulls SDL2 + SDL2_mixer + GLEW from `~/cross/` (one-time setup is in the target preamble).
- **Native Windows / Visual Studio 2019/2022**: open `build/theseus-desktop.sln` and build x64 / Release. Mirrors the cross-compile output but built with `cl.exe`. Setup in [`build/README-vs.md`](../../build/README-vs.md).

The codebase compiles with permissive flags (`-fpermissive`, `-fms-extensions` on Clang/GCC; `/permissive-` and a comprehensive `DisableSpecificWarnings` list on MSVC) because the original Xbox code uses legacy C++ patterns that modern compilers reject.

All source filenames are lowercase for cross-platform compatibility on case-sensitive filesystems (Linux).

## Code Organization

The dashboard engine lives in four roots that share whatever doesn't need to diverge per platform:

- `theseus/engine/`: pure logic, no platform dependencies (script VM internals, math, locale, dotfield, settings nodes)
- `theseus/shared/`: cross-platform with Win32 types (file I/O, titles, audio, screen, settings file parser, savegame grid)
- `theseus/render/`: D3D8/graphics nodes (scene groups, shape rendering, dynamic textures, materials, text, hud)
- `theseus/desktop/`: desktop-only code: SDL main loop, D3D8-to-OpenGL translation, ImGui tools (inspector, XAP editor, Title Maker, HDD browser, menu bar, media player), SDL audio backend, filesystem virtualization, the unified launch dispatcher

Within each root, every major tool is its own `.cpp`/`.h` pair. Shared state between ImGui panels is declared in `theseus/desktop/panel_shared.h`.

The Xbox build (`make`) reads from `xbox/` + the shared roots; the desktop build (`make desktop`) reads from `desktop/` + the shared roots. Files migrate from `desktop/` into the shared roots whenever a desktop file converges with the canonical decompiled form: one source serves both targets where the platform divergence allows it.
