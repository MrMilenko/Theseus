# Desktop Port Documentation

Technical documentation for the desktop port of Theseus: the Xbox dashboard engine running natively on macOS, Linux, and Windows via SDL2 + OpenGL.

For the engine itself (Script VM, scene graph, XIP archives), see [`docs/decomp/`](../decomp/). For the XAP script API contract that both Xbox and desktop builds must honor, see [`docs/xap-contract.md`](../xap-contract.md).

## Contents

- **[Porting to PC](porting-to-pc.md)**: How Xbox D3D8, kernel APIs, and filesystem calls were replaced with SDL2/OpenGL for macOS, Linux, and Windows
- **[D3D8 to OpenGL Translation](d3d8-translation.md)**: The rendering layer that makes Xbox Direct3D 8 calls work on modern OpenGL 3.2
- **[Desktop Features](desktop-features.md)**: Game launching, Steam integration, Title Maker, development tools, media player
- **[Media Player Design](media-player-design.md)**: libmpv integration with the Xbox DVD player XAP scene
