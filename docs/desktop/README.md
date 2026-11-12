# Desktop Documentation

How the Xbox dashboard engine runs on macOS, Linux and Windows as UIX Desktop: SDL2 for windows, input and sound, bgfx for rendering (Metal, Direct3D 11, Vulkan), libmpv for video.

For the engine itself (script VM, scene graph, XIP archives) see [`docs/decomp/`](../decomp/). For the script API both builds have to honor, see [`docs/xap-contract.md`](../xap-contract.md).

- **[Building](building.md)**: every desktop target, the one time bgfx and libdatachannel steps, Windows cross builds, packaging
- **[Dependencies](dependencies.md)**: packages per OS, minimum library versions, the Windows cross deps
- **[Desktop Features](desktop-features.md)**: the games grid and launching, Title Maker, media, music, xCloud, skins, CRT, the tool windows, settings files
- **[Porting to PC](porting-to-pc.md)**: the platform layer, drive letters and the user data folder, audio, input, nodes, logs
- **[D3D8 to bgfx](d3d8-translation.md)**: how the dashboard's Direct3D 8 calls are drawn through bgfx
- **[Media Player](media-player-design.md)**: the media library, libmpv playback, and what became of the DVD player
