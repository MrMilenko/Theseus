# Theseus

[![build](https://github.com/MrMilenko/Theseus/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/MrMilenko/Theseus/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows%20%7C%20Xbox-lightgrey.svg)](#get-it)
[![Discord](https://img.shields.io/discord/1391105896179957830)](https://discord.gg/qfVHTYD4xX)

The original Xbox shipped with a dashboard nobody has ever quite gotten over: the green glow, the orbs, the hum. Theseus is that dashboard rebuilt plank by plank from years of reverse engineering the retail binaries, and it runs on your computer.

On macOS, Linux and Windows it goes by **UIX Desktop**: a launcher and media center that looks and feels like the 2002 dashboard. Your Steam library, emulators, xemu, Xbox Cloud Gaming, movies, TV and music all live in the games grid and the orbs, driven by the same scene scripts the Xbox ran.

<p align="center">
  <img src="docs/images/desktop-launcher.png" width="48%" alt="UIX Desktop games grid">
  <img src="docs/images/media-library.png" width="48%" alt="Media library">
</p>
<p align="center">
  <img src="docs/images/custom-skin.png" width="48%" alt="A custom skin">
  <img src="docs/images/steam-tab.png" width="48%" alt="Steam import in Title Maker">
</p>

**Jump to:** [Get it](#get-it) · [Set it up](#set-it-up) · [Controls](#controls) · [Getting help](#getting-help) · [Build from source](#build-from-source) · [How it works](#how-it-works) · [The Xbox build](#the-xbox-build) · [Heritage](#heritage) · [Credits](#credits) · [License](#license)

## What's in it

- **Games grid** for anything you can launch: native PC games, scripts, Steam, RetroArch, [ES-DE](https://es-de.org/) collections, Xbox ISOs through [xemu](https://xemu.app), and your Xbox Cloud Gaming library
- **Title Maker** (F3) for adding and editing entries by hand, with import tabs for Steam, RetroArch and ES-DE
- **Media library** for your Movies and TV folders, with posters from [TMDB](https://www.themoviedb.org/), plus [Plex](https://www.plex.tv/) and [Jellyfin](https://jellyfin.org/) servers. Playback runs through libmpv
- **Music** from your own folders, with a [projectM](https://github.com/projectM-visualizer/projectm) (MilkDrop) visualizer
- **Xbox Cloud Gaming and remote play**: sign in with your Xbox account and your cloud games and consoles show up in the grid
- **Skins**: UIX Lite community skins drop in as is, and the Skin Editor helps you make your own
- **CRT effect** for that living room TV look: scanlines, curvature, phosphor mask, bloom, all tunable
- **Controllers**: Xbox, PlayStation and other SDL2 gamepads, plus full keyboard control
- **Xbox HDD browser** (F5) for xemu qcow2 images and their FATX partitions

## Get it

Grab the build for your OS from the [Releases page](https://github.com/MrMilenko/Theseus/releases).

| OS | Download | Notes |
|---|---|---|
| macOS (Apple Silicon) | `Theseus-<version>-macos-app.zip` | Self contained `Theseus.app`. It's ad hoc signed, not notarized, so the first launch needs right click > Open, or **System Settings > Privacy & Security > Open Anyway** on newer macOS. |
| macOS (Apple Silicon) | `Theseus-<version>-macos.zip` | Bare binary for people who already live in Homebrew. Needs `brew install sdl2 sdl2_mixer mpv curl ffmpeg opus`. |
| Linux x64 | `UIX-Desktop-<version>-x86_64.AppImage` | One file with its libraries bundled. `chmod +x` it and run. This is the one for a Steam Deck. |
| Linux x64 / ARM64 | `Theseus-<version>-linux-x64.tar.gz`, `Theseus-<version>-linux-arm64.tar.gz` | Binary plus data. Needs the runtime packages listed in [docs/desktop/dependencies.md](docs/desktop/dependencies.md). |
| Windows x64 | `Theseus-<version>-windows-x64.zip` | Unzip anywhere and run `theseus.exe`. Every DLL it needs is in the zip. The exe isn't code signed, so SmartScreen may ask first (More info > Run anyway). |

Rendering goes through [bgfx](https://github.com/bkaradzic/bgfx): Metal on macOS, Direct3D 11 on Windows, Vulkan on Linux. If the default misbehaves on your machine, pick another backend under **Settings > Display > Renderer** and restart.

## Set it up

### First run

Launch it and you land on the dashboard. On first run UIX Desktop creates your **user data folder** and copies the starter Configs, Data and Library into it. Everything you change lives there, not next to the app, so updating means replacing the app and nothing else.

| OS | User data folder |
|---|---|
| macOS | `~/Library/Application Support/Theseus/` |
| Linux | `$XDG_DATA_HOME/theseus/`, which is usually `~/.local/share/theseus/` |
| Windows | `%APPDATA%\Theseus\` |

**Settings (F4) > General > Open User Directory** takes you straight there. Inside:

| Path | What's there |
|---|---|
| `Configs/games.ini` | Every launcher entry you add: name, title ID, launch command, category |
| `Configs/Icons.ini` | Which icon each entry uses |
| `Configs/icons/` | The icon images, named by title ID (`.jpg` or `.png`) |
| `Configs/desktop.ini` | Desktop settings: display, audio, CRT, library folders, sign ins |
| `Configs/config.ini` | Dashboard settings shared with the Xbox build (skin, clock, language) |
| `Data/` | Skins, orbs, music, fonts, screenshots and the dashboard's scene archives |
| `Library/` | The virtual Xbox `E:` drive: `Games`, `Applications`, `Emulators`, plus title and save data in `TDATA` and `UDATA` |
| `theseus.log`, `theseus.prev.log` | This run's log and the one before it |

When an update ships new Data (scene archives, fonts, skins), it reaches your copy on the next launch, and anything you've edited is left alone. Only the compiled shaders stay with the app itself. Settings files are written atomically, with the previous version kept beside them as `.old`, so a crash mid save can't eat your games list.

### Adding games and apps

Open **Title Maker** with F3 (or Tools > Title Maker).

- **Main** lists every entry, whatever added it. Create one by hand with a name and a launch line: a path to an executable, a script, a `steam://` link, or any shell command. Quote paths with spaces. **Test Launch** tries it without leaving the dashboard. **Add ISO** turns an Xbox ISO into an xemu entry once the xemu path is set (Find checks the usual install spots).
- **Steam** finds your Steam install, and **Import Steam Library** brings in every installed game with its icon. Add by App ID covers betas and demos.
- **RetroArch** finds your install, and **Import Recent Titles** pulls in what you've played lately with the core and box art already sorted. Or pick a ROM and a core by hand.
- **ES-DE** points at your ES-DE data folder (the one holding `gamelists/`). Your systems and games show up under an ES-DE category, read live on every launch.

Entries from Main, Steam and RetroArch are stored in **`Configs/games.ini`** inside your user data folder, with their icons in `Configs/icons/` and the icon mapping in `Configs/Icons.ini`. Back those up and you've backed up your launcher. ES-DE and Xbox Cloud Gaming entries are never written there; they're rebuilt each launch from their source.

The Steam, RetroArch and ES-DE tabs can be hidden under Optional Tabs at the top of Main. Hiding a tab keeps its entries. When you launch something, the dashboard fades to black and minimizes so the game gets the screen; switch back to it when you're done. Cloud and remote play streams run inside the dashboard window instead.

### Xbox Cloud Gaming

**Settings > Media > Xbox / xCloud > Sign in to Xbox**, then enter the code it shows on any device. From then on your cloud games and your consoles (for remote play) appear in the games grid with their box art.

### Movies, TV and music

**Settings > Media > Local Library** takes three folders:

- **Movies**: one folder per movie, or video files straight in the folder
- **TV Shows**: `Show Name/Season 1/episodes` (or `Specials`), or episodes straight in the show folder. `S01E02` style names set the order
- **Music**: each subfolder becomes a soundtrack, `.m3u` playlists in the root become soundtracks too, and loose files land in one called Music

A free [TMDB API key](https://www.themoviedb.org/settings/api) is optional; with one, posters and synopses fill in. Hit **Refresh Media Library** after changing folders.

Plex signs in with a PIN code. Jellyfin takes your server URL and signs in with Quick Connect. Both live under **Settings > Media**.

### Skins

Skins live in `Data/Skins/<SkinName>/` in your user data folder. Pick one from the dashboard's own settings and it swaps live, no restart. UIX Lite community skins work without conversion. A skin's `<SkinName>.xbx` only lists the materials it changes; anything it leaves out keeps the retail 5960 color. **Tools > Skin Editor** can start a new skin from Stock and tweak colors as you watch.

### Handy settings

- **Display**: fullscreen (F11), borderless (F12), resolution, vsync, FPS cap, MSAA, renderer, hardware video decode, and Emulate Xbox Resolution, which frames scenes in the original 720x480 design space
- **CRT Effect**: Settings > Display, or View > CRT Effect. Four presets or eight sliders
- **Audio**: master volume, mute (Ctrl+M), and the projectM visualizer. With it on, X + Y (keys or pad) opens and closes the visualizer window

Command line flags: `--fullscreen`, `--4k` (fullscreen plus 2x UI scale), `--scale N`, `--no-toolbar`, `--muted`, `--minimized`, `--dashboard`, `--development`.

## Controls

The dashboard drives like the Xbox did: D-pad or stick to move, A to select, B to back out.

<details>
<summary><b>Keyboard</b></summary>

| Key | Xbox button |
|---|---|
| Arrow keys | D-pad |
| WASD | Left stick |
| Enter / Space | A |
| Backspace / Esc / B | B |
| X / Y | X / Y |
| Tab | White |
| ` (backtick) | Black |
| Q / E | Left / right trigger |

</details>

<details>
<summary><b>Video playback</b></summary>

| Key | Action |
|---|---|
| Esc / Q / Backspace | Stop and return to the dashboard |
| Space | Pause / resume |
| Left / Right | Seek 5 seconds |
| `[` / `]` | Previous / next in playlist |
| T | Audio and subtitle tracks |

</details>

<details>
<summary><b>Tools and windows</b></summary>

| Key | Action |
|---|---|
| F1 | Scene inspector |
| F2 | XAP script editor |
| F3 | Title Maker |
| F4 | Settings |
| F5 | Xbox HDD browser |
| F6 | Playlist Maker |
| F10 | Show or hide the menu bar |
| F11 | Fullscreen |
| F12 | Borderless window |
| Ctrl+M | Mute |
| Ctrl+R | Restart the dashboard |

On a Mac, Cmd works in place of Ctrl.

</details>

<details>
<summary><b>Controller in the tool windows</b></summary>

**LT + B** (the same chord as the Xbox overlay) hands the pad to the tool windows. A prompt bar along the bottom shows what each button does right now.

| Input | Action |
|---|---|
| Stick / D-pad | Move |
| A | Select |
| B | Back out one layer, then leave pad mode |
| Y | Open a text field with the on screen keyboard |
| Hold X | Move and resize windows |

Turn it off with `PadMode=0` in `Configs/desktop.ini`. The Skin Editor stays mouse and keyboard only.

</details>

## Getting help

Found a bug? Open an [issue](https://github.com/MrMilenko/Theseus/issues) or find us on the TeamUIX [Discord](https://discord.gg/qfVHTYD4xX). Please include:

1. **Your OS and version**, and your GPU if it's a display problem
2. **Which download you're running**: the file name carries the version and package. Help > About UIX Desktop shows it too
3. **What you did, what you expected, and what happened instead**
4. **`theseus.log`** from your user data folder (see [First run](#first-run)). If it crashed on the previous launch, attach `theseus.prev.log` as well. A crash also leaves a report in the `crashes` folder there; attach the newest one

On Windows, starting `theseus.exe` from a terminal sends the log to that terminal instead of the file.

Please don't post `Configs/desktop.ini` as is: it holds your Plex, Jellyfin and Xbox sign in tokens.

## Build from source

The full walkthrough is in [docs/desktop/building.md](docs/desktop/building.md), and the libraries per OS are in [docs/desktop/dependencies.md](docs/desktop/dependencies.md). The shape of it on macOS or Linux:

1. `git clone --recursive https://github.com/MrMilenko/Theseus`
2. Install the build packages for your OS. Everything is found through `pkg-config`, so any package manager works
3. Build the bgfx runtime libraries and libdatachannel once. Skip libdatachannel and xCloud quietly drops out of the build
4. `cd build && make desktop`, then run `~/builds/theseus/desktop/theseus`

Windows builds cross compile with mingw (`make desktop-win64`), and there's a Linux ARM64 cross target (`make desktop-linux-arm64`). Scripts for the macOS `.app` and the Linux AppImage live in `build/`.

For contributors: only `theseus/desktop/host_*` talks to the operating system. `make lint-platform` enforces that, and `make win64-import-guard` catches stray Win32 calls. CI runs the lint and builds macOS, Linux and Windows on every push.

## How it works

The dashboard is a scene graph driven by XAP, a small scripting language compiled to bytecode, with scenes, meshes and textures packed into `.xip` archives. Theseus reimplements the engine underneath: the script VM, the VRML flavored scene graph with its FND/PRD reflection tables, asset loading, text, materials, audio and input. The scripts and archives run unmodified, which is why the scene node interface is treated as a contract ([docs/xap-contract.md](docs/xap-contract.md)).

On desktop, the dashboard's Direct3D 8 calls land on a thin shim that drives bgfx, Xbox drive paths map onto your user data folder, and SDL2 handles windows, input and sound.

```
theseus/
  engine/    Pure logic: script compiler, math, locale
  shared/    Both builds: script VM, files and titles, settings, keyboard
  render/    Both builds: scene graph, materials, shapes, text
  desktop/   Desktop only: SDL main loop, bgfx shim, host_* OS layer, tools, media, launchers
  xbox/      Xbox only: kernel and hardware glue
  toolbox/   Xbox only: FTP and drive tools from the PrometheOS lineage
theseuslib/  C library: XISO and XIP parsers
xips-source/ Editable source for the scene archives Theseus adds or changes
```

Documentation index: [docs/README.md](docs/README.md). What we found in the dashboard binary, subsystem by subsystem: [docs/decomp/](docs/decomp/).

## The Xbox build

The same engine still compiles for the original Xbox as a replacement dashboard, built with [OXDK](https://github.com/MrMilenko/OXDK) through `build/Makefile.xbox`. It's a work in progress and isn't part of the desktop releases. If you want a custom dashboard on your console today, use [UIX Lite](https://github.com/OfficialTeamUIX/UIX-Lite), where TeamUIX's console work currently lives.

<p align="center">
  <img src="docs/images/xbox-dashboard.png" width="48%" alt="Theseus on the Xbox">
</p>

Building it needs clang and lld-link, OXDK, and an Xbox SDK tree of your own linked at `theseus/xdk` (it isn't in this repo):

```
cd build
make -f Makefile.xbox CONFIG=retail     # or CONFIG=debug
```

The XBE lands in `~/builds/theseus/xbox-retail/` (or `xbox-debug/`). On the console it expects `uixdata\` beside the XBE, mounted as `Q:`, and its configs in `C:\UIX Configs\`. If something's missing at boot, the panic screen tells you what.

## Heritage

Theseus is part of the TeamUIX lineage. JbOnE created *User.Interface.X* (UIX), a source level modification of the original Xbox dashboard, and a lot of what we know about how the dashboard behaves came from years of living with it. Modern TeamUIX carries that on with [UIX Lite](https://github.com/OfficialTeamUIX/UIX-Lite), a heavily patched retail dashboard, and with Theseus.

UIX changed the dashboard from the inside. Theseus reaches the same place from the other side of the river: Ghidra analysis of the retail dashboard binaries from 4920 through 5960, custom Ghidra scripts that pull the dashboard's own FND/PRD descriptor tables out of the binary, disassembly where the decompiler gets it wrong, every name checked against the XAP scripts that call it, and two decades of community changes to the XIPs untangled along the way. The findings are written up per subsystem in [docs/decomp/](docs/decomp/).

For the wider UIX story, see [UIX History](https://github.com/MrMilenko/UIX-History).

## Credits

**TeamUIX**
- **Milenko**: reverse engineering and development, UIX Desktop
- **BigJx**: UIX Lite XIPs (XAP scripts, skins, scene assets), testing, bug reports
- **Rocky5**: skin presets and the Colourizer XBE color patcher (the technique descends from **ZogoChieftan**'s in dashboard color patching in BlackStormX, circa 2004)
- **JbOnE**: the original UIX, the lineage Theseus continues

**Upstream code**
- [Team Resurgent](https://github.com/Team-Resurgent): [PrometheOS](https://github.com/Team-Resurgent/PrometheOS-Firmware) (the Xbox toolbox is forked from it via [UIX Lite Toolbox](https://github.com/OfficialTeamUIX/UIX-Lite-Toolbox)) and [Hermes](https://github.com/Team-Resurgent/Hermes) (ISO/CCI mounting on Xbox)

**Libraries**: SDL2, SDL2_mixer, libmpv, libcurl, FFmpeg, Opus, OpenSSL, libdatachannel, bgfx, bx, bimg, projectM, Dear ImGui, ImGuiColorTextEdit, imgui-filebrowser, stb and minimp3. Licenses and links are in [LICENSE-THIRD-PARTY.md](LICENSE-THIRD-PARTY.md).

## License

Theseus is licensed under the **GNU General Public License, version 3 or later** (`GPL-3.0-or-later`). Full text in [LICENSE](LICENSE).

Inherited code keeps its own license (`theseus/toolbox/` from PrometheOS via UIX Lite Toolbox is GPL-3.0; Hermes is GPL-3.0). The XIPs and skin assets shipped in `Data/` are TeamUIX's UIX Lite work and ship under GPL-3.0-or-later.
