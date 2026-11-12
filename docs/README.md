# Theseus Documentation

Technical documentation for Theseus, the Xbox dashboard engine rebuilt from binary analysis of the retail dashboard, and for UIX Desktop, the build of it that runs on macOS, Linux and Windows. If you just want to run it, the main [README](../README.md) is the place to start.

## Reference

- **[XAP Script Contract](xap-contract.md)**: every node type, function, property and callback the dashboard's XAP scripts use. The "do not break" surface any implementation (Xbox, desktop, future ports) has to honor.
- **[xips-source](../xips-source/README.md)**: the editable source for the scene archives Theseus adds or changes.

## What we found in the dashboard binary (`decomp/`)

Per subsystem notes from analyzing the 5960 retail dashboard XBE in Ghidra. Each file documents the structures, function tables, properties and behavior identified for one part of the dashboard. [decomp/README.md](decomp/README.md) is the address by address index.

- **[VM](decomp/VM.md)**: XAP bytecode VM: opcode set, operator table, built in functions, variable resolution
- **[Compiler](decomp/Compiler.md)**: how XAP text becomes bytecode and scene nodes
- **[Node](decomp/Node.md)**: scene graph reflection, FND and PRD struct layouts, how the structures were identified in the binary
- **[XIP](decomp/XIP.md)**: archive format, file layout, texture loading, mesh buffer layout
- **[AudioSystem](decomp/AudioSystem.md)**: DirectSound pipeline, CAudioBuf, CAudioPump, CFilePump, CAudioClip, CMusicCollection
- **[SceneGroups](decomp/SceneGroups.md)**: Group, Layer, Background and Level grouping nodes
- **[ShapeRender](decomp/ShapeRender.md)**: Shape, Appearance, Material, Box, Sphere, falloff shading, mesh normal smoothing
- **[HudNode](decomp/HudNode.md)**: the HUD and Picture screen space overlay nodes
- **[TmapSystem](decomp/TmapSystem.md)**: dynamic textures, delta field warping, audio visualizer, FFT, particle dot field
- **[AnimationNodes](decomp/AnimationNodes.md)**: TimeSensor, interpolators, Viewpoint, NavigationInfo
- **[FileOps](decomp/FileOps.md)**: file and directory copy, save game grid backend
- **[Keyboard](decomp/Keyboard.md)**: on screen keyboard layout and input handling
- **[Camera](decomp/Camera.md)**: view matrix construction
- **[Text](decomp/Text.md)**: font rendering, glyph layout, color formatting
- **[NtIoSvc](decomp/NtIoSvc.md)**: CD-ROM IOCTL service
- **[TitleCollection](decomp/TitleCollection.md)**: saved game and title enumeration backend
- **[Settings](decomp/Settings.md)**: system settings, screen saver, INI style config file parser
- **[Date](decomp/Date.md)** and **[Math](decomp/Math.md)**: built in date and math objects
- **[StringObject](decomp/StringObject.md)**: string built in
- **[Util](decomp/Util.md)**: utility helpers

## Desktop (`desktop/`)

How the same engine runs on macOS, Linux and Windows. The Xbox build doesn't need any of this.

- **[Building](desktop/building.md)**: every desktop target, the one time library steps, Windows cross builds, packaging
- **[Dependencies](desktop/dependencies.md)**: packages per OS, minimum library versions, Windows cross deps
- **[Desktop Features](desktop/desktop-features.md)**: games grid and launching, Title Maker, media, music, xCloud, skins, CRT, tools
- **[Porting to PC](desktop/porting-to-pc.md)**: the platform layer, drive letters and the user data folder, audio, input, nodes
- **[D3D8 to bgfx](desktop/d3d8-translation.md)**: drawing the dashboard's Direct3D 8 calls through bgfx
- **[Media Player](desktop/media-player-design.md)**: the media library, libmpv playback, and the DVD player node

## The Xbox build

Built with OXDK through `build/Makefile.xbox` (`make -f Makefile.xbox CONFIG=retail` or `CONFIG=debug` from `build/`). It needs an Xbox SDK tree of your own at `theseus/xdk`. Console work currently ships as [UIX Lite](https://github.com/OfficialTeamUIX/UIX-Lite); the Theseus XBE is a work in progress.
