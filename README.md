# Theseus

[![build](https://github.com/MrMilenko/Theseus/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/MrMilenko/Theseus/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Xbox%20%7C%20macOS%20%7C%20Linux%20%7C%20Windows-lightgrey.svg)](#)

Theseus is a reverse-engineered reconstruction of the original Xbox 5960 dashboard. It runs on Xbox hardware, and on desktop (macOS, Linux, Windows) where the same engine is being grown into a 3D launcher for PC games, Steam titles, and emulator-hosted Xbox ISOs (via [xemu](https://xemu.app)). Authoring tools for skin and content creators (XAP editor, scene inspector, Title Maker) ship alongside it.

The reconstruction is built on binary analysis of the retail Xbox dashboard binaries (4920-5960) in [Ghidra](https://ghidra-sre.org/), backed by the TeamUIX lineage's two-decades-plus history with the dashboard (see [Heritage](#heritage) below). The methodology is documented in [Decomp Methodology](#decomp-methodology).

Theseus has two builds from the same source tree:

- **Xbox**: a native dashboard XBE that boots on modded Xbox hardware. The shipped `default.xbe` is from-scratch native code, not a modified Microsoft binary.
- **UIX Desktop**: the same engine compiled natively for macOS, Linux, and Windows via SDL2 + OpenGL. The Xbox D3D8 calls are translated to OpenGL, kernel APIs are replaced with desktop equivalents, the Xbox filesystem is virtualized, and the same scene graph and script VM that runs on Xbox hardware runs natively on your desktop. Desktop source lives in `theseus/desktop/` and shares code with the Xbox build via `theseus/shared/`, `theseus/engine/`, and `theseus/render/`. Documentation in [`docs/desktop/`](docs/desktop/).

[**UIX Lite**](https://github.com/OfficialTeamUIX/UIX-Lite) is a sibling project in the TeamUIX lineage: a heavily patched version of the original Microsoft retail dashboard XBE, authored and maintained by TeamUIX. The XIPs (XAP scenes, scene archives, skins) shipped with a dashboard install are UIX Lite's work. The `uixdata` directory layout is a convention established by JbOnE's earlier source-level modification, *User.Interface.X* (UIX), that TeamUIX has used ever since. Theseus is structured to follow that same convention and to consume UIX Lite's XIPs unchanged, so dropping UIX Lite's data onto a Theseus-powered Xbox works out of the box.

## Current State (Public Beta v0.3)

Working on Xbox hardware:

- **Launcher**: XBE / ISO / CCI launching from the harddrive menu, backed by the native title scanner. Hundreds of titles scan in single-digit milliseconds. Title icons auto-populate from each game's XBE certificate. UDATA TitleImage.xbx written from the embedded `$$XTIMAGE` section if missing.
- **Overlay** (L Trigger + B): quick ISO / CCI loader, file manager, live FTP / drive widgets. Independent of the main scene graph, so it stays usable when the dashboard scene is mid-transition.
- **Hot-swappable skins** from the settings panel (no reboot).
- **FTP server** auto-starts at boot, auto-detects available partitions, stays up even in panic mode.
- **Recovery / panic screen** if `default.xip` or `default.xap` fails to load, or if an unhandled exception escapes the dashboard. D3D-direct UI with no scene-graph dependency, dumps a crash log with register state and stack walk to `Q:\Logs\panic-*.txt`. FTP still listens, so you can push fixed XIPs over the wire and retry without reflashing.
- **MP3 audio playback**, scanned from `E:\Music\<Soundtrack>\<song>.mp3`.

Not yet on Xbox: Xbox Live account creation, soundtrack rip-CD-to-disk. DVD playback ships on the desktop build (libmpv-backed) and is intentionally absent on Xbox; see [Known Issues](#known-issues) for the full picture.

## How It Works

Theseus is approximately 50 source files reconstructed from the retail XBE, organized into the same subsystems the original dashboard had:

- **Script VM** (`xap_compile.cpp`, `xap_vm.cpp`): A JS-like bytecode compiler and virtual machine that executes XAP scene scripts. Lexer, parser, compiler, and interpreter.
- **Scene graph** (`node_system.cpp`, `node.h`): VRML97-inspired node system with runtime reflection via FND/PRD property tables. Every scene node exposes its properties and methods to the VM through a single macro-driven table.
- **Rendering** (`scene_groups.cpp`, `shape_render.cpp`, `tmap_system.cpp`, `materials.cpp`, `text.cpp`, `camera.cpp`, `dotfield_node.cpp`): D3D8 screen management, material system inherited from the 3ds Max export pipeline, mesh rendering, XTF font rendering, particle fields, dynamic textures, falloff shading.
- **Asset loading** (`asset_loader.cpp`, `xip_archive.cpp`): XIP archive parser, XBX texture loader, .xm mesh buffer loader.
- **UI framework** (`scene_groups.cpp`, `animation_nodes.cpp`, `keyboard_node.cpp`, `hud_node.cpp`): Panels, layers, viewpoints, cameras, on-screen keyboard, HUD overlay, interpolation/animation drivers.
- **System integration** (`audio_system.cpp`, `dsound_manager.cpp`, `disc_management.cpp`, `harddrive.cpp`, `savegame_grid.cpp`): DirectSound pump, disc detection, hard drive enumeration, save game grid, soundtrack collection.
- **Title launcher** (`theseus_launcher.cpp`, `title_scanner.cpp`): Native filesystem scanner that walks all mounted partitions, parses XBE certs for titleId, recognises XBE/ISO/CCI launchables, and writes a UIX-Lite-compatible `cache.ini` plus `Icons.ini` plus per-title `UDATA\<id>\TitleImage.xbx`. Launch path dispatches by extension and uses Hermes's `IOCTL_VIRTUAL_ATTACH` for ISO/CCI, then soft-launches via `XLaunchNewImage` so the disc image mount survives the handoff.
- **Recovery** (`panic_screen.cpp`): D3D-direct fallback UI when init fails or an unhandled exception escapes the dashboard. No scene-graph dependency. Writes timestamped crash logs to `Q:\Logs\panic-*.txt` with full exception record + register dump + EBP-chain stack walk.
- **Xbox Live** (`live_accounts.cpp`, `xbox_live.cpp`): 5960 Xbox Live account loading via `_XOnlineGetUsersFromHD`, profile verification, gamertag display.
- **Toolbox** (`theseus/toolbox/`): FTP server, drive manager, network stack. Forked from PrometheOS via the UIX Lite Toolbox. FTP keeps running in panic mode so missing/broken XIPs can be pushed over the wire without reflashing.

The XAP script API contract (every node, function, property, and callback the dashboard XIPs reference) is documented in [`docs/xap-contract.md`](docs/xap-contract.md). That contract is the "do not break" surface; the C++ implementation behind it can be rewritten freely.

## The Xbox Build

### Project Structure

```
theseus/
  engine/         Pure logic, no platform deps (VM, nodes, math, settings)
  shared/         Cross-platform with Win32 types (file I/O, titles, audio, theseus.h)
  render/         D3D8/graphics (scene graph, textures, materials, shape rendering)
  xbox/           Xbox-only (XTL init, modchip, kernel APIs, main.cpp)
  desktop/        Desktop-only (SDL/OpenGL platform layer, ImGui tools, stubs)
  toolbox/        PrometheOS-derived FTP/drive/network toolbox
theseuslib/       Shared C library for the dashboard's link-time ABI (xiso parser, xip parser, xapi extensions)
build/            Unified build system (Xbox cross-compile + native desktop + Windows cross)
Configs/          Desktop runtime: dashboard configs, virtual game DB, version stamp (Xbox C:\UIX Configs\)
Data/             Desktop runtime: shipped assets -- XIPs, skins, fonts, audio, language (Xbox Q:\)
Library/          Desktop runtime: game library + save data (Xbox E:\)
docs/             XAP contract reference, decomp notes, desktop port docs
```

### Building

Builds a bootable Xbox XBE using clang, lld-link, and cxbe. Works on macOS and Linux.

**Requirements:**
- clang and lld-link (`brew install llvm` on macOS, `apt install clang lld` on Linux)
- [OXDK](https://github.com/MrMilenko/OXDK) cloned and built
- An Xbox SDK source tree (headers and libraries the Xbox dashboard ABI requires at link time)

**Setup:**
1. Clone and build OXDK:
   ```
   git clone https://github.com/MrMilenko/OXDK ~/OXDK
   cd ~/OXDK/tools/cxbe && make
   ```
2. Drop your XDK tree somewhere the build can reach. The Makefile takes a `XDK_BASE` path; the simplest convention is to extract it into `theseus/xdk/` in the repo (gitignored, so it won't get committed). The build expects this layout:
   ```
   theseus/xdk/
     include/                 # public headers
     lib/                     # public libs
     private/inc/             # plus crypto/
     private/ntos/inc/
     private/ntos/xapi/inc/
     public/sdk/inc/          # plus crt/
     public/ddk/inc/
   ```
   If your XDK lives elsewhere, pass that path as `XDK_BASE` instead (see Build below).

**Build:**
```
cd build

# If you put the XDK at theseus/xdk/, this is enough:
make XDK_BASE=$(pwd)/../theseus/xdk

# Otherwise pass your own path:
make XDK_BASE=/path/to/xbox

# Retail variant:
make CONFIG=retail XDK_BASE=$(pwd)/../theseus/xdk

# Clean
make clean
```

Override `OXDK_DIR` if OXDK is not at `~/OXDK`.

Output lands under `~/builds/theseus/` by default (override with `BUILDS_ROOT`):
- `~/builds/theseus/xbox-debug/default.xbe`
- `~/builds/theseus/xbox-retail/default.xbe`

### Deploying to Xbox Hardware

The Xbox runs the retail build. The dashboard expects two things to be in place: the data folder next to the XBE, and a config folder on `C:\`.

1. **Drop the XBE somewhere on the Xbox HDD**, for example `E:\Dashboards\Theseus\default.xbe`. Anywhere works as long as the data folder lives next to it.
2. **Copy `Data/` from this repo to that same folder, renamed to `uixdata`.** Final layout:
   ```
   E:\Dashboards\Theseus\
     default.xbe
     uixdata\
       Audio\
       Background\
       Fonts\
       Language\
       Music\
       Orbs\
       Screenshots\
       Skins\
       System\
       Xips\
   ```
   On Xbox the data folder is named `uixdata`. The desktop build calls the same content `Data/` (it's what `Q:\` resolves to via the path translator).
3. **Copy `Configs/` to `C:\UIX Configs\` on the Xbox HDD.** This holds the launcher cache and INI configuration the dashboard reads at boot.

If the XBE boots without `uixdata/` next to it or without `C:\UIX Configs\` on the HDD, the dashboard drops into the panic / recovery screen with a crash log explaining what's missing. FTP comes up either way, so you can push the missing folders over the wire and reboot.

`Library/` mirrors what a stock Xbox `E:\` looks like (Applications, Dashboards, Emulators, Games, TDATA). It exists so the desktop build has a sane filesystem to enumerate against. On real hardware your Xbox already has `E:\`; nothing to copy.

### Status

**Complete:**
- XAP script VM (tokenizer, compiler, bytecode interpreter)
- Node/scene graph infrastructure (CObject, CNode, CNodeClass)
- XIP archive loader
- Scene groups (Group, Transform, Inline, Spinner, Waver, Layout, Switch, Billboard, Layer, Background, Level)
- TMAP system (dynamic textures, delta field warping, HSV palettes, audio visualization, FFT)
- Shape rendering (Shape, Appearance, Material, Box, Sphere, vertex shaders, falloff)
- Animation nodes (TimeSensor, interpolators, Viewpoint, NavigationInfo)
- Settings, recovery, keyboard, dotfield, autosmooth
- File operations (game copying, folder/file nodes)
- Text/font rendering, camera system
- Core utilities, string built-ins, settings file parser
- DirectSound manager, CD-ROM IOCTL service
- Title/saved game enumeration
- Network stack, FTP server, drive manager (PrometheOS toolbox)
- Overlay system, modchip detection, Discord relay
- MP3 streaming playback (CMP3Pump + minimp3) for the music system

**In Progress:**
- The desktop and Xbox builds are converging on a single shared source tree. Most engine, scene-graph, and rendering code already builds for both targets; a small set of desktop-only copies still need to migrate.

**Media playback:**
- DVD playback and the retail soundtrack manager (rip-to-disk, in-dashboard playback) depend on licensed media technology (DVD-Video, WMA, etc.) that's outside the scope of this project. The dashboard's existing media XAP scenes are wired to open-source backends instead: [libmpv](https://mpv.io/) on the desktop build for video, and [minimp3](https://github.com/lieff/minimp3) on both builds for an `E:\Music\` directory of MP3 files. UI flows are unchanged; the backend changed.

### Known Issues

- **Save game delete is non-functional.** The confirmation submenu opens but the delete action is a no-op. Under investigation.
- **Xbox Live account transfer between memory units and the HDD is not supported.** `StartXboxLiveAccountCopy` is a stub that fires `OnCopyError` for XAP compatibility.
- **Dev mode loose-load is incomplete.** Script hot-reload works; texture, mesh, and missing-XIP fallback paths still go through .xip archives.
- **Retail Xbox builds compile at `-O0`** to work around a clang `-O2` optimizer bug. Larger binaries, no behavior change.

The Xbox build links clean via the macOS / Linux cross-compile and boots on original hardware.

## The Desktop Port

UIX Desktop takes the Theseus engine and runs it natively on macOS, Linux, and (in-progress) Windows. Same scene graph, same script VM, same XAP scenes from the XIP archives, no emulation. The Xbox D3D8 interface is reimplemented on top of OpenGL 3.2 and GLSL, the Win32 type system is reimplemented in `sdl_platform.h`, and Xbox drive letters are virtualized to three top-level folders next to the binary (`Configs/`, `Data/`, `Library/`).

### Features

- **3D game launcher**: Browse and launch PC games, Steam titles, and Xbox ISOs (via [xemu](https://xemu.app)) through the dashboard's 3D interface.
- **Steam library import**: Discovers installed Steam games and creates launcher entries automatically.
- **xemu integration**: Launch Xbox disc images directly through xemu.
- **Community skins**: UIX Lite skins work out of the box, switchable from dashboard settings.
- **XAP script editor (F2)**: Edit dashboard scripts live with syntax highlighting and hot reload.
- **Title Maker (F3)**: Create game shortcuts with custom icons, categories, and launch commands.
- **Scene inspector (F1)**: Full scene graph browser with node properties, visibility toggles, and 3D click-to-select.
- **Xbox HDD browser (F5)**: Mount and browse qcow2/FATX Xbox hard drive images.
- **Media library + player** *(beta)*: Three-scene browser (movies / TV shows / season-episode) backed by your filesystem and a TMDB-enriched local cache. Configurable library roots in Settings. Fullscreen video via libmpv with custom Xbox-themed OSD; optional CRT post-process applied to the picture. Expect bugs — the lifecycle around mpv state, scene transitions, and CRT compositing is still settling.
- **CRT post-processing**: Scanline, curvature, phosphor, and bloom filters.

### Building

The desktop source lives in `theseus/desktop/` and shares code with the Xbox build via `theseus/shared/`, `engine/`, and `render/`. Same Makefile, same repo.

**Requirements:** C++17 compiler, OpenGL 3.2, SDL2, SDL2_mixer, libmpv, libcurl, pkg-config.

`libmpv` powers fullscreen video playback (Movies/TV browser) and CD audio. `libcurl` powers the in-process TMDB metadata enrichment that populates the media library. Both are mandatory; the Makefile errors out with install hints if pkg-config can't find them.

**macOS:**
```
brew install sdl2 sdl2_mixer mpv curl pkg-config
cd build && make desktop
~/builds/theseus/desktop/theseus
```

If `make` reports libcurl missing, prepend `PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig:${PKG_CONFIG_PATH}"` (Homebrew's curl is keg-only).

**Linux (Debian/Ubuntu):**
```
sudo apt install build-essential pkg-config \
                 libsdl2-dev libsdl2-mixer-dev libgl-dev \
                 libmpv-dev libcurl4-openssl-dev
cd build && make desktop
~/builds/theseus/desktop/theseus
```

**Windows (MSYS2 / MinGW64):**

Install [MSYS2](https://www.msys2.org/), then in the **MSYS2 MinGW64** shell:

```
pacman -S make pkg-config \
          mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-SDL2 \
          mingw-w64-x86_64-SDL2_mixer \
          mingw-w64-x86_64-mpv \
          mingw-w64-x86_64-curl
cd build && make desktop
~/builds/theseus/desktop/theseus.exe
```

The output `.exe` is a real PE32+ x86-64 binary. Ship it alongside the MSYS2-runtime DLLs (SDL2.dll, SDL2_mixer.dll, libmpv-2.dll, libcurl-4.dll, etc. — `ldd theseus.exe` from inside MSYS2 lists them) and the `Configs/`, `Data/`, `Library/` folders.

**Windows (cross-compile from macOS/Linux):**

Less polished than the MSYS2 path but useful if you don't want to launch a Windows VM. Requires SDL2 / SDL2_mixer / libmpv / libcurl mingw devel libs unpacked under `~/cross/`.

```
sudo apt install mingw-w64                    # macOS: brew install mingw-w64
# One-time: SDL2 + SDL2_mixer mingw devel  -> ~/cross/sdl2-mingw/
# One-time: GLEW from source (mingw)       -> ~/cross/glew-src/glew-2.2.0/
# One-time: libmpv mingw devel             -> ~/cross/mpv-mingw/      (shinchiro/mpv-winbuild-cmake)
# One-time: libcurl mingw devel            -> ~/cross/curl-mingw/     (curl.se/windows)
cd build && make desktop-win64
~/builds/theseus/desktop-win64/theseus.exe + SDL2.dll + SDL2_mixer.dll + libmpv-2.dll + libcurl-x64.dll
```

The CI workflow (`.github/workflows/build.yml`, job `desktop-win64-cross`) runs this path on every push and is the closest thing to executable docs for the one-time setup.

**Linux ARM64 (cross-compile from x86_64 Linux):**
```
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install g++-aarch64-linux-gnu pkg-config \
                 libsdl2-dev:arm64 libsdl2-mixer-dev:arm64 libgl-dev:arm64 \
                 libmpv-dev:arm64 libcurl4-openssl-dev:arm64
PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
    make -C build desktop-linux-arm64
# Output: ~/builds/theseus/desktop-linux-arm64/theseus
```

For testers running aarch64 Linux on devices like jailbroken Switches, Pi 4/5 boxes, or ARM cloud VMs. The output is a dynamically linked aarch64 ELF; runtime deps on the target are `libsdl2-2.0-0`, `libsdl2-mixer-2.0-0`, `libgl1`, `libmpv2` (or `libmpv1`), and `libcurl4`. Build host glibc must be <= target glibc; if your tester is on an older Ubuntu, build on a Debian Bookworm host or directly on the target. See the `desktop-linux-arm64` target preamble in `build/Makefile` for details.

### CLI flags (desktop)

| Flag             | What it does                                                                  |
|------------------|-------------------------------------------------------------------------------|
| `--scale N`      | UI scale factor for the ImGui debug overlay (1.5, 2.0 for high-DPI displays) |
| `--fullscreen`   | Borderless fullscreen at native resolution                                    |
| `--4k`           | Convenience: `--fullscreen` + `--scale 2.0`                                   |
| `--no-toolbar`   | Start with the menu bar hidden (toggle back with F10)                        |
| `--dashboard`    | Start in dashboard mode (skip the preloader picker)                           |
| `--development`  | Start in development mode (enables Inspector, XAP Editor)                     |
| `--muted`        | Start with audio muted                                                        |
| `--preview`      | Legacy alias for `--dashboard`                                                |

### Data Files

The `Configs/`, `Data/`, and `Library/` directories hold dashboard data (configs, XIP archives, skins, fonts, audio, language files, virtual game library) for the desktop build to consume. The build system copies them into the output directory automatically so each build drop is self-contained. The Xbox build does not need them; it uses the same data shipped on the Xbox HDD via real drive letters.

### Controls (desktop)

**Dashboard navigation:**

| Key | Xbox button | Action |
|---|---|---|
| Arrow keys | D-pad | Navigate |
| Enter / Space | A | Select |
| Backspace | B | Back |
| X / Y | X / Y | Context actions |
| Tab | White | Play / Pause |
| ` (backtick) | Black | Stop |
| Q / E | LT / RT | Page or frame step |
| WASD | Left stick | Analog navigation |

**Media playback:**

| Key | Action |
|---|---|
| `[` / `]` | Scan backward / forward |
| `;` / `'` | Play / Pause |
| `i` | Toggle OSD |
| 0-9 | Chapter digit entry |

**Developer tools:**

| Key | Action |
|---|---|
| F1 | Scene inspector |
| F2 | XAP script editor |
| F3 | Title Maker |
| F4 | Settings |
| F5 | Xbox HDD browser |
| F10 | Toggle menu bar |
| Ctrl+M | Mute |
| Ctrl+R | Restart dashboard |

Xbox and PlayStation controllers also work via the SDL2 GameController API, merged with keyboard input.

### Desktop Platform Layer

The desktop port wraps the Theseus engine with a platform layer that handles everything Xbox-specific:

| Component | Responsibility |
|---|---|
| `d3d8_sdl.h` | Translates D3D8 to OpenGL 3.2: device, textures, vertex buffers, GLSL shaders, falloff lighting |
| `sdl_platform.h` | Reimplements Win32 types and APIs (DWORD, HANDLE, ReadFile, FindFirstFile, GetTickCount, etc.) |
| `xboxfs.h` | Maps Xbox drive paths (`Q:\Xips\...`) to local filesystem with case-insensitive lookup |
| `sdl_main.cpp` | SDL2 window, OpenGL context, main loop, CRT post-processing |
| `desktop_nodes.cpp` | Desktop implementations of XAP nodes (AudioClip, SavedGameGrid, DVDPlayer, MusicCollection) |
| `inspector.cpp` | Scene graph browser with 3D selection |
| `xap_editor.cpp` | Live script editor with syntax highlighting |
| `title_maker.cpp` | Game shortcut creator with Steam import |
| `hdd_browser.cpp` | Xbox HDD image browser (qcow2 + FATX) |
| `media_player.cpp` | libmpv wrapper for the DVD player node |
| `audio_sdl.cpp` | SDL_mixer backend with Xbox IMA ADPCM decoder |
| `xbx_texture.h` | XBX texture reader (DXT1/3/5, Xbox swizzle) |

For deeper notes on the desktop port, see [`docs/desktop/`](docs/desktop/).

## Documentation

- **[`docs/README.md`](docs/README.md)**: Documentation index
- **[`docs/xap-contract.md`](docs/xap-contract.md)**: Complete catalog of XAP node types, functions, properties, and callbacks. The "do not break" API surface.
- **[`docs/decomp/`](docs/decomp/)**: Per-subsystem reverse engineering notes derived from binary analysis of the retail XBE
- **[`docs/desktop/`](docs/desktop/)**: Desktop port documentation (porting notes, D3D8-to-OpenGL translation, features, media player design)

For Xbox dashboard history and the broader UIX project narrative, see [UIX History](https://github.com/MrMilenko/UIX-History).

## Decomp Methodology

Each source file was produced by analyzing the corresponding code in the retail XBE:

- **FND/PRD tables**: Function and property descriptors identified by scanning .bss init patterns
- **XAP script verification**: Every exposed function/property cross-checked against dashboard XIP scripts
- **Ghidra analysis**: ~2,300 of the binary's 4,559 functions labeled and decompiled via custom Ghidra scripts and pattern-matching (~50% of the binary)

See [`docs/decomp/`](docs/decomp/) for per-subsystem binary analysis notes.

## Heritage

Theseus is part of the TeamUIX lineage. JbOnE created *User.Interface.X* (UIX), a source-level modification of the original Microsoft Xbox Dashboard, with the original teamUIX collective. Modern TeamUIX continues that tradition with UIX Lite and Theseus.

There is a circularity to this. UIX modified the dashboard at the source level. Theseus reaches the same destination from the other side of the river, reconstructing that codebase from the retail binary using Ghidra and analysis of the XAP scripts.

## Credits

### Team UIX

- **Milenko**: primary RE and development, UIX Desktop port
- **BigJx**: maintainer of the UIX Lite XIPs (XAP scripts, skins, scene assets) shipped with the modern dashboard, plus testing and bug reports
- **Rocky5**: skin presets and the modern Colourizer XBE color patcher. The technique itself descends from **ZogoChieftan**'s pioneering in-dashboard color patching work in BlackStormX (BSX), circa 2004; Rocky5's tool brings that idea to the retail 5960 dashboard.
- **JbOnE**: original author of User.Interface.X (UIX) and the original teamUIX collective; the lineage Theseus continues.

### Related Projects

- **[Team Resurgent](https://github.com/Team-Resurgent)**: [PrometheOS](https://github.com/Team-Resurgent/PrometheOS-Firmware) firmware (the toolbox, including FTP server, drive manager, and utilities, was forked from PrometheOS via the [UIX Lite Toolbox](https://github.com/OfficialTeamUIX/UIX-Lite-Toolbox)) and [Hermes](https://github.com/Team-Resurgent/Hermes) (ISO/CCI mount support).
- **[xemu](https://xemu.app)**: Original Xbox emulator. The desktop port's launcher integrates xemu for booting Xbox ISOs.

### Third-Party Libraries

**Used by the Xbox build:**

| Library | License | Purpose |
|---|---|---|
| [minimp3](https://github.com/lieff/minimp3) | CC0 (public domain) | Single-header MP3 decoder used by the music system for streaming playback |

**Used by the desktop port:**

| Library | License | Purpose |
|---|---|---|
| [SDL2](https://www.libsdl.org/) | zlib | Window, input, audio |
| [SDL2_mixer](https://github.com/libsdl-org/SDL_mixer) | zlib | Sound playback |
| [libmpv](https://mpv.io/) | LGPL 2.1+ | Video playback for the DVD player node |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | Developer tool UI (scene inspector, XAP editor, Title Maker, HDD browser) |
| [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) | MIT | XAP script editor with syntax highlighting |
| [stb_image](https://github.com/nothings/stb) | Public Domain | Image loading on the desktop side |
| [GLEW](https://glew.sourceforge.net/) | Modified BSD / MIT | OpenGL extension loader (Windows desktop build only) |

## License

Theseus is licensed under the **GNU General Public License, version 3 or later** (`GPL-3.0-or-later`). The full license text is in [`LICENSE`](LICENSE).

Inherited code keeps its origin license intact:

- **`theseus/toolbox/`**: forked from PrometheOS via UIXLiteFTP and UIX-Lite-Toolbox, GPL-3.0 throughout.
- **ISO/CCI mount support**: integrates with [Hermes](https://github.com/Team-Resurgent/Hermes), GPL-3.0.

A complete catalog of inherited code, linked libraries, and their respective licenses is in [`LICENSE-THIRD-PARTY.md`](LICENSE-THIRD-PARTY.md).

The XIPs and skin assets shipped in `Data/` are authored by UIX Lite and represent TeamUIX modifications of dashboard scripts present on every retail Xbox console. UIX Lite's modifications ship under GPL-3.0-or-later; the underlying script formats are referenced for compatibility and preservation.
