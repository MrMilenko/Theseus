# Theseus Documentation

This directory contains the technical documentation for Theseus: the Xbox dashboard engine reverse-engineered from the 5960 retail XBE.

## Contents

### Reference

- **[XAP Script Contract](xap-contract.md)**: Complete catalog of every node type, function, property, and callback that XAP scripts in the dashboard XIPs reference. The "do not break" API surface that any implementation (Xbox, desktop, future ports) must honor.

### Binary Analysis (`decomp/`)

Per-subsystem reverse engineering notes derived from analysis of the retail 5960 XBE in Ghidra. Each file documents the structures, function tables, properties, and behavior identified for one subsystem of the dashboard.

- **[VM](decomp/VM.md)**: XAP bytecode VM: opcode set, operator table, built-in functions, variable resolution
- **[Node](decomp/Node.md)**: Scene graph reflection, FND and PRD struct layouts, how the structures were identified from the binary
- **[XIP](decomp/XIP.md)**: Archive format, file layout, texture loading, mesh buffer layout
- **[AudioSystem](decomp/AudioSystem.md)**: DirectSound pipeline, CAudioBuf, CAudioPump, CFilePump, CAudioClip, CMusicCollection
- **[SceneGroups](decomp/SceneGroups.md)**: Group, Layer, Background, Level grouping nodes
- **[ShapeRender](decomp/ShapeRender.md)**: Shape, Appearance, Material, Box, Sphere, falloff shading, mesh normal smoothing
- **[TmapSystem](decomp/TmapSystem.md)**: Dynamic textures, delta field warping, audio visualizer, FFT, particle dot field
- **[AnimationNodes](decomp/AnimationNodes.md)**: TimeSensor, interpolators, Viewpoint, NavigationInfo
- **[FileOps](decomp/FileOps.md)**: File and directory copy, save game grid backend
- **[Keyboard](decomp/Keyboard.md)**: On-screen keyboard layout and input handling
- **[Camera](decomp/Camera.md)**: View matrix construction
- **[Text](decomp/Text.md)**: Font rendering, glyph layout, color formatting
- **[NtIoSvc](decomp/NtIoSvc.md)**: CD-ROM IOCTL service
- **[TitleCollection](decomp/TitleCollection.md)**: Saved game and title enumeration backend
- **[Settings](decomp/Settings.md)**: System settings, screen saver, INI-style config file parser
- **[Date](decomp/Date.md)** and **[Math](decomp/Math.md)**: Built-in date and math objects
- **[StringObject](decomp/StringObject.md)**: String built-in
- **[Util](decomp/Util.md)**: Utility helpers

### Desktop Port (`desktop/`)

Documentation specific to building Theseus for desktop platforms (macOS, Linux, in-progress Windows). The Xbox build does not need any of this.

- **[Porting to PC](desktop/porting-to-pc.md)**: How Xbox APIs were replaced with SDL2 and OpenGL
- **[D3D8 to OpenGL Translation](desktop/d3d8-translation.md)**: Rendering layer translation
- **[Desktop Features](desktop/desktop-features.md)**: Game launching, Steam, Title Maker, tools
- **[Media Player Design](desktop/media-player-design.md)**: libmpv integration with the DVD player XAP scene
