# Porting to PC

## The challenge

The dashboard was written to run on exactly one machine. Files open through `CreateFile` on drive letters (`Q:\Xips\default.xip`, `C:\UIX Configs\config.ini`). Every pixel goes through Xbox Direct3D 8. Input comes from four controller ports and a DVD remote. Sound goes through DirectSound and the console's audio DSP. Types are Win32 types (`DWORD`, `HANDLE`, `TCHAR`), and the kernel answers questions no PC has an answer for, like which memory units are plugged in.

UIX Desktop keeps the dashboard's engine code and its scripts as they are, and gives them an Xbox shaped world to live in on macOS, Linux and Windows.

## One tree, two builds

```
theseus/
  engine/    Pure logic: script compiler, math, locale, dotfield, settings nodes
  shared/    Both builds: script VM, file and title handling, settings files, keyboard, screen
  render/    Both builds: scene graph, materials, shapes, text, HUD, dynamic textures
  xbox/      Xbox only: XTL init, kernel and hardware glue, DirectSound, XInput
  desktop/   Desktop only: SDL main loop, D3D8 to bgfx shim, host layer, tools, media, launchers
  toolbox/   Xbox only: FTP, drive and network tools from the PrometheOS lineage
```

`build/Makefile` builds the desktop app from `desktop/` plus the shared roots. `build/Makefile.xbox` builds the XBE from `xbox/` plus the same shared roots. A file lives in a shared root when one copy can serve both builds; where the two genuinely differ (input, audio, file I/O, a handful of nodes whose Xbox version talks to hardware), each build has its own. `shared/platform.h` says which build is compiling: exactly one of `_XBOX` or `THESEUS_DESKTOP`, and on desktop exactly one host OS. `_WIN32` is true on the Xbox too, so nothing outside the host layer is allowed to test it.

## The platform layer

### platform_shim.h: Xbox names, no OS headers

Dashboard code expects Win32 and XDK names: `HANDLE`, `ReadFile`, `GetTickCount`, `FILETIME`, `FindFirstFile`, `OutputDebugString` and friends. `platform_shim.h` provides them on every desktop OS, backed by SDL, the C runtime and the host layer. It's the same file on macOS, Linux and Windows, and it never includes an OS header. That last part matters most on Windows, where the real `CreateFile` exists and would happily accept `Q:\Xips\default.xip`.

### host.h and host_*: the only code that talks to the OS

`host.h` is a narrow door with plain types: make a folder, stat a file, list a directory, flush to disk, rename over, spawn a process, open a URL, find the executable, show a folder in the file manager, read the CD table of contents. `host_posix.cpp` and `host_posix_app.cpp` implement it for macOS and Linux, `host_win.cpp` and `host_win_app.cpp` for Windows. Those four files are the only ones that include OS headers or test OS macros.

Two build targets keep it that way:

- `make lint-platform` fails on a Windows header, an OS macro or a host test anywhere else. CI runs it on every push.
- `make win64-import-guard` compiles the Windows objects and fails if anything outside `host_win*` imports kernel32's file, clock or sleep calls, which would mean real Win32 answered where the shim was supposed to.

### xbox_path.h: drive letters

Every Xbox path goes through one translator. Drive letters map onto the per user data folder:

| Xbox | Desktop |
|---|---|
| `C:` (`C:\UIX Configs\`) | `Configs/` |
| `Q:` (the dashboard's own data) | `Data/` |
| `E:` | `Library/` |
| `X:` | `Library/` too: a desktop only drive that can never collide with a real Windows `C:\` path |
| `F:`, `G:`, `R:` and the rest | nothing |

Scripts that walk every partition just find the extra ones empty. Linux cares about case and the Xbox didn't, so `path_ci.h` resolves paths case insensitively when the exact spelling isn't on disk; skins and XAPs written on one OS keep working on another.

### app_paths: the user data folder

The data lives in a per user folder (`~/Library/Application Support/Theseus` on macOS, `$XDG_DATA_HOME/theseus` or `~/.local/share/theseus` on Linux, `%APPDATA%\Theseus` on Windows). On first run it's seeded from the `Configs/`, `Data/` and `Library/` that ship beside the binary (inside a macOS `.app`, from `Contents/Resources`). Only `Data/shaders/` stays with the app, so a read only install works.

Updates to shipped `Data/` reach existing installs. `Data/.shipped` remembers the size and timestamp of every file it copied, both as shipped and as it looked right after. A copy that still matches is replaced when the shipped file changes; a copy the user edited is theirs and stays. For installs from before the manifest existed, the engine content (`Xips`, `System`, `Fonts`, `Language`) is replaced when it differs and the old copy is kept as `.old`; skins, music and the rest are left alone.

Settings writes go through `safe_save.h`: write `<file>.new`, flush it to disk, keep the current file as `<file>.old`, then rename the new one into place. A crash or power cut mid save leaves either the old file or the new one, never half of one.

### xbox_file.cpp: files that only exist in games.ini

The dashboard finds games by listing folders under `E:\Games` and reading each `default.xbe`. On desktop most entries aren't folders at all: they're lines in `Configs/games.ini`, or live entries from ES-DE and Xbox Cloud Gaming. `xbox_file.cpp` makes them list like folders anyway, and their `default.xbe` opens as a synthesized header carrying the title name and ID. The dashboard's game grid never knows.

## Rendering

The dashboard's D3D8 calls land on a purpose built shim over bgfx: Metal on macOS, Direct3D 11 on Windows, Vulkan on Linux. That's its own document: [d3d8-translation.md](d3d8-translation.md).

## Audio

`audio_sdl.cpp` replaces DirectSound with SDL_mixer. Sound effects play as mixer chunks and background music as mixer music. Many dashboard sounds are Xbox IMA ADPCM WAVs (format tag `0x0069`), which SDL can't read, so they're decoded to 16 bit PCM at load time. A post mix hook feeds the PCM to the audio visualizer and projectM. Master volume and mute sit on top of all of it, and dashboard audio goes quiet while a video or stream plays (those have their own audio output) and while the window is in the background.

## Input

`desktop/joystick.cpp` implements the dashboard's `Joystick` node on SDL2's GameController API, so Xbox, PlayStation, Switch Pro and other mapped pads all work. The keyboard is merged into the same snapshot: arrows for the D-pad, WASD for the left stick, Enter for A, Backspace for B, Tab and backtick for White and Black. The DVD remote's buttons (play, pause, skip, digits) map to keys and fire the same script callbacks the IR receiver did. Typematic repeat, the screen saver reset and the secret button sequences behave like the Xbox.

## Nodes

The scripts reference node types by name, and an unknown node type fails the whole scene, so every node the XIPs use has to exist on desktop.

- **Shared with the Xbox build**: the scene graph, animation, materials and shapes, text, the keyboard, settings, screen, date, and the script built ins.
- **Desktop versions of Xbox nodes**: `AudioClip`, `MusicCollection`, `SavedGameGrid` (reads real Xbox saves from `Library/UDATA` and from qcow2 images), `HardDrive`, `MemoryMonitor`, `DiscDrive` (audio CDs), `DVDPlayer` (libmpv), `Joystick`, `Config` and `TheseusLauncher`.
- **Kept so scripts load**: `XboxLive`, `LiveAccounts` and `CopyDestination` answer with harmless defaults; there's no Xbox Live or memory unit on a PC.
- **Desktop additions**: `MediaCollection`, `PlaylistCollection`, `PlexLibrary`, `JellyfinLibrary`, `XcloudLibrary`, `GamesCollection` and `Display`. They're new names alongside the originals, never replacements; see [xap-contract.md](../xap-contract.md).

## Logs and crashes

Everything the app prints goes to the terminal and to `theseus.log` in the user data folder, with the previous run kept as `theseus.prev.log`, so a launch from Finder, a desktop entry or Explorer still leaves a log someone can send. On Windows a launch from a terminal prints there instead of the file. Near the top, the log names the shipped folder, the user data folder and the `games.ini` it's reading, and warns if the shader folder is missing. A crash handler prints a stack trace on the way down.

## Building

See [building.md](building.md) and [dependencies.md](dependencies.md).
