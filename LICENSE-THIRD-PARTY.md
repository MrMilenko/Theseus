# Third-Party Components

Theseus itself is licensed under GPL-3.0-or-later (see [LICENSE](LICENSE)). This document catalogs every third-party component the project incorporates, links against, or inherits code from, and the license each component carries.

## Inherited / Forked Code

These directories or files contain code originally authored by other projects. The upstream license travels with the code; Theseus's GPL-3.0-or-later license layers on top compatibly.

### `theseus/toolbox/` (PrometheOS lineage)

The toolbox (FTP server, drive manager, network utilities, file system helpers) is forked from a chain of three upstream repos:

1. [Team-Resurgent/PrometheOS-Firmware](https://github.com/Team-Resurgent/PrometheOS-Firmware): the original PrometheOS firmware, licensed **GPL-3.0**.
2. [Team-Resurgent/UIXLiteFTP](https://github.com/Team-Resurgent/UIXLiteFTP): a single-FTP-app fork of PrometheOS, gifted by Team Resurgent to TeamUIX.
3. [OfficialTeamUIX/UIX-Lite-Toolbox](https://github.com/OfficialTeamUIX/UIX-Lite-Toolbox): TeamUIX's evolution of the gifted FTP app into a full toolbox.

The intermediate repos do not carry explicit `LICENSE` files but inherit GPL-3.0 transitively from PrometheOS. See [`theseus/toolbox/LICENSE`](theseus/toolbox/LICENSE) for the directory-local notice.

### ISO and CCI mount support (Hermes)

ISO and CCI launchable handling integrates with [Team-Resurgent/Hermes](https://github.com/Team-Resurgent/Hermes), licensed **GPL-3.0**. Theseus uses Hermes's `IOCTL_VIRTUAL_ATTACH` mechanism to mount disc images before soft-launching titles via `XLaunchNewImage`.

## Linked / Embedded Libraries

Used by the Xbox build:

| Library | License | Purpose |
|---|---|---|
| [minimp3](https://github.com/lieff/minimp3) | CC0 (Public Domain) | Single-header MP3 decoder for streaming music playback |

Used by the desktop port:

| Library | License | Purpose |
|---|---|---|
| [SDL2](https://www.libsdl.org/) | zlib | Window, input, audio platform layer |
| [SDL2_mixer](https://github.com/libsdl-org/SDL_mixer) | zlib | Sound playback |
| [libmpv](https://mpv.io/) | LGPL-2.1+ | Video playback for the DVD player node |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | Developer tool UI |
| [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) | MIT | XAP script editor with syntax highlighting |
| [stb_image](https://github.com/nothings/stb) | Public Domain (or MIT, dual-licensed) | Image loading on the desktop side |
| [GLEW](https://glew.sourceforge.net/) | Modified BSD / MIT | OpenGL extension loader (Windows desktop build only) |

All of the above are GPL-3.0-compatible, either by being permissive (MIT, BSD, zlib, public domain) or by being LGPL (linkable from GPL).

## Xbox Dashboard Assets (`xboxfs/`)

The XIPs and skin assets in `xboxfs/` are authored by [UIX Lite](https://github.com/OfficialTeamUIX/UIX-Lite) and contain TeamUIX modifications of dashboard scripts present on every retail Xbox console. The `uixdata` directory layout is a convention established by JbOnE's earlier source-level modification, *User.Interface.X* (UIX); Theseus follows that convention so the same content runs on both projects. UIX Lite's modifications are licensed under GPL-3.0-or-later. The underlying script formats and any unmodified original components are referenced for compatibility and preservation; Microsoft retains its rights to its original work. Every Xbox owner already possesses the equivalent original files on their console.

## Microsoft XDK Headers (`theseus/xdk/`)

This directory contains Microsoft Xbox Development Kit headers and is **not part of the public release**. It is excluded before any public source distribution and is not licensed for redistribution under the GPL or otherwise. If you obtained a source archive that contains this directory, that archive is not a public release.
