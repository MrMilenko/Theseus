# D3D8 to bgfx

## The problem

The Xbox dashboard draws everything through the Xbox flavor of Direct3D 8, on an NV2A GPU with a fixed function texture combiner and a handful of custom vertex shaders. The scene graph code calls `SetRenderState`, `SetTextureStageState`, `SetTransform` and `DrawIndexedPrimitive` like any 2001 game, and it expects Xbox texture formats and Xbox vertex layouts.

The desktop build doesn't carry a general D3D8 emulator. `theseus/desktop/d3d8_sdl.h` implements the slice of the D3D8 interface the dashboard actually uses and backs it with [bgfx](https://github.com/bkaradzic/bgfx), which picks Metal on macOS, Direct3D 11 on Windows and Vulkan on Linux (overridable in Settings > Display > Renderer). The dashboard code above it doesn't know the difference.

## Shape of the shim

```
D3D8 interface              Desktop implementation
IDirect3D8                  One adapter, one device
IDirect3DDevice8            Render state mirror, packed into bgfx state and shader uniforms
IDirect3DTexture8           CPU side RGBA pixels, uploaded to a bgfx texture on first use
IDirect3DVertexBuffer8      Vertex data plus a bgfx layout built from the FVF on first draw
IDirect3DIndexBuffer8       WORD indices, expanded to triangle lists at draw time
D3DX math                   Matrices, vectors, quaternions, inline
```

Everything the dashboard draws in a frame lands in bgfx view 0. The CRT effect, video and the ImGui tools layer on after.

## Render state

D3D8 has a couple hundred render states. The shim tracks the ones the dashboard sets and folds them into a `BGFX_STATE_*` bitmask that rides along with each draw:

| D3D8 | bgfx |
|---|---|
| `D3DRS_ZENABLE`, `D3DRS_ZFUNC`, `D3DRS_ZWRITEENABLE` | depth test bits and `BGFX_STATE_WRITE_Z` |
| `D3DRS_ALPHABLENDENABLE`, `D3DRS_SRCBLEND`, `D3DRS_DESTBLEND` | blend function bits |
| `D3DRS_CULLMODE` | cull bits (bgfx's default winding is clockwise, same as D3D) |
| `D3DRS_ALPHATESTENABLE`, `D3DRS_ALPHAREF` | a shader uniform, `discard` in the fragment shader |
| `D3DRS_TEXTUREFACTOR` | a shader uniform |

The rest are accepted and dropped: NV2A display tricks (flicker filter, soft display, swath width), fog, stencil, point sprites, vertex blending. The dashboard sets them during init and never relies on them.

Sampler state (`D3DTSS_ADDRESSU/V`, `MINFILTER`, `MAGFILTER`) becomes `BGFX_SAMPLER_*` flags passed with the texture on each draw.

F8 dumps a count of how often each render and texture stage state is set, and how many of those sets changed nothing. Shift+F8 resets the counters.

## The fixed function emulator

Two bgfx shaders stand in for the whole fixed function pipeline: `vs_ff.sc` and `fs_ff.sc` in `theseus/desktop/shaders/`. Rather than compiling a shader per state combination, the shim packs the current state into a few `vec4` flag uniforms and the shaders branch on them.

**Vertex side** (`vs_ff`):

- Transforms by the world view projection matrix, or passes pretransformed `XYZRHW` vertices straight through for 2D overlays
- Picks the vertex color from falloff, vertex diffuse, texture factor, material diffuse or plain white
- Reads Xbox `NORMPACKED3` normals (11:11:10 in four bytes) as well as float normals
- Does spherical environment mapping for the materials that use it

**Falloff** is the dashboard's signature look: the faint glow along the edges of the green shell and the brighter faces pointing at you. On the Xbox it's `effect.vsh`. The desktop shader does the same math on the GPU: side color plus (front color minus side color) times the absolute dot product of the view direction and the normal. Faces turned toward the camera get the front color and edges fade to the side color. Both colors come from the material, through the same vertex shader constants the Xbox code sets.

**Fragment side** (`fs_ff`) covers the texture stage setups the dashboard uses: modulate, select texture, select diffuse and add for color; modulate or select for alpha; an optional second stage that adds color or multiplies alpha; and the alpha test. With no texture bound, a 1x1 white texture keeps the sample well defined.

## Draw calls

- `DrawIndexedPrimitive` submits through a dynamic vertex buffer and a transient index buffer.
- `DrawPrimitiveUP` and `DrawIndexedPrimitiveUP` copy the caller's data into transient buffers each draw.
- Strips, fans and the Xbox only `D3DPT_QUADLIST` are expanded to triangle lists, so bgfx only ever sees triangles. The scratch buffer for that lives for the whole run.
- Vertex layouts come from the FVF on first use. Float normals map to bgfx's normal attribute; packed normals ride in a color attribute and get unpacked in the shader.

## Textures

Dashboard textures are `.xbx` files: Xbox XPR0 resources holding DXT1, DXT3, DXT5 or linear data, usually swizzled into Morton (Z) order for the NV2A. `xbx_texture.h` parses the header, decodes DXT blocks on the CPU and unswizzles to plain RGBA. PNG, JPG and BMP (skins, Steam icons, box art) go through `stb_image`. Either way the result is RGBA pixels on the CPU side of `IDirect3DTexture8`.

Upload to bgfx happens the first time a texture is drawn. Textures the dashboard rewrites every frame (the music visualizer, dynamic textures) are created with `BGFX_TEXTURE_BLIT_DST`. Without it, Metal can put the texture in GPU only memory and silently ignore later CPU updates, and the visualizer freezes on its first frame.

## Things drawn outside the scene

A few things skip the D3D8 shim entirely:

- **CRT effect**: when it's on, view 0 renders into an offscreen framebuffer and `fs_crt.sc` draws that to the backbuffer with scanlines, curvature, phosphor mask, vignette, bloom, flicker and color bleed.
- **Video**: libmpv renders each frame into a CPU buffer that's uploaded to a bgfx texture and drawn fullscreen with `vs_blit` and `fs_blit` (see [media-player-design.md](media-player-design.md)). It goes through the CRT pass too.
- **Boot animation and xCloud streams**: the same blit path.
- **ImGui tools**: their own bgfx backend (`imgui_impl_bgfx.cpp`) with `vs_imgui` and `fs_imgui`.

## Shaders on disk

Shader sources are `theseus/desktop/shaders/*.sc`. The compiled binaries for every backend are checked in under `Data/shaders/` (`metal`, `spirv`, `dx11`, `glsl`, `essl`), and the app loads the folder matching the renderer bgfx picked. They're the one part of `Data/` that stays with the app instead of moving to the user data folder. If that folder is missing, the log says so near the top, because otherwise the only symptom is a black window.
