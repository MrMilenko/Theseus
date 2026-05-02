# D3D8 to OpenGL Translation

## The Problem

The Xbox dashboard renders everything through Direct3D 8, Microsoft's graphics API from 2000. D3D8 predates programmable shaders (it has vertex shaders but no pixel shaders in the modern sense), uses a fixed-function texture blending pipeline, and has a completely different state model from modern OpenGL.

The desktop port doesn't use a general-purpose D3D8-to-OpenGL translator. Instead, `d3d8_sdl.h` implements just enough of the D3D8 interface to render the dashboard, approximately 2,600 lines of purpose-built translation code.

## Architecture

The translation layer provides stub classes that mirror the D3D8 COM interfaces:

```
D3D8 Interface              Desktop Implementation
─────────────────           ──────────────────────
IDirect3D8                  → Returns a single "SDL OpenGL" adapter
IDirect3DDevice8            → OpenGL state machine + GLSL shaders
IDirect3DTexture8           → GL texture objects
IDirect3DVertexBuffer8      → CPU-side vertex arrays with transform caching
IDirect3DIndexBuffer8       → CPU-side index arrays
IDirect3DSurface8           → GL framebuffer attachments
```

## IDirect3DDevice8: The Core

Every D3D8 call in the dashboard goes through `CDashApp` wrapper functions that call into `IDirect3DDevice8`. On desktop, this class manages:

### Render State Tracking

D3D8 uses `SetRenderState()` with ~200 possible state flags. The translation layer tracks the ones the dashboard actually uses:

| D3D8 Render State | OpenGL Equivalent |
|-------------------|-------------------|
| `D3DRS_ZENABLE` | `glEnable/glDisable(GL_DEPTH_TEST)` |
| `D3DRS_ALPHABLENDENABLE` | `glEnable/glDisable(GL_BLEND)` |
| `D3DRS_SRCBLEND` / `D3DRS_DESTBLEND` | `glBlendFunc()` |
| `D3DRS_CULLMODE` | `glEnable(GL_CULL_FACE)` + `glCullFace()` |
| `D3DRS_LIGHTING` | Tracked as a flag, applied in vertex shader |
| `D3DRS_FILLMODE` | `glPolygonMode()` |
| `D3DRS_ALPHATESTENABLE` / `D3DRS_ALPHAREF` | Fragment shader `discard` |
| `D3DRS_TEXTUREFACTOR` | Uniform passed to fragment shader |

The dashboard sets ~40 different render states during initialization. Most are Xbox-specific display modes (flicker filter, soft display filter, swath width) or features not needed on desktop (stencil, fog, point sprites). These are accepted by `SetRenderState` without error but have no GPU-side effect.

### Texture Stage States

D3D8 uses a multi-stage texture blending model. Each stage can have a texture bound and configure how its color and alpha are combined with previous stages:

```cpp
// Dashboard typically does:
SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
```

The translation layer tracks stage 0 and stage 1 configurations and generates GLSL fragment shader logic to match. The most common patterns:
- **MODULATE**: `texture_color * vertex_color` (the default)
- **SELECTARG1**: Use texture color only
- **SELECTARG2**: Use vertex color only
- **ADD**: `texture_color + vertex_color`

### Transform Pipeline

D3D8 uses separate World, View, and Projection matrices set via `SetTransform()`. The translation layer captures these and combines them into an MVP (Model-View-Projection) matrix for the GLSL vertex shader:

```cpp
// D3D8 code sets:
SetTransform(D3DTS_WORLD, &matWorld);
SetTransform(D3DTS_VIEW, &matView);
SetTransform(D3DTS_PROJECTION, &matProjection);

// Translation layer combines to:
mat4 MVP = Projection * View * World;
// Passed to vertex shader as uniform
```

## GLSL Shaders

The dashboard's rendering is handled by two GLSL shaders:

### Vertex Shader

```glsl
// Simplified. Actual shader handles more cases.
uniform mat4 u_MVP;
uniform vec4 u_falloffCenter;
uniform float u_falloffRadius;
uniform float u_falloffIntensity;

in vec3 a_position;
in vec4 a_color;
in vec2 a_texcoord;

out vec4 v_color;
out vec2 v_texcoord;

void main() {
    gl_Position = u_MVP * vec4(a_position, 1.0);
    v_color = a_color;
    v_texcoord = a_texcoord;
}
```

### Fragment Shader

```glsl
// Simplified. Actual shader handles TSS state combinations.
uniform sampler2D u_texture;
uniform bool u_hasTexture;
uniform float u_alphaRef;

in vec4 v_color;
in vec2 v_texcoord;

out vec4 fragColor;

void main() {
    vec4 texColor = u_hasTexture ? texture(u_texture, v_texcoord) : vec4(1.0);
    fragColor = texColor * v_color;
    if (fragColor.a < u_alphaRef) discard;
}
```

The actual shaders are more complex, handling independent color and alpha operations, multiple texture stages, and various blend modes configured by the TSS tracking.

## Per-Vertex Falloff Lighting

The dashboard's signature visual effect is a radial falloff, a spotlight-like effect where objects near the camera's focus are bright and objects further away fade to dark. On Xbox, this was implemented as a custom vertex shader (`effect.vsh`) running on the NV2A GPU.

On desktop, this is computed on the CPU during vertex transformation:

```
For each vertex:
  1. Transform position by World-View-Projection matrix
  2. Compute distance from vertex to falloff center (in view space)
  3. Calculate attenuation: brightness = 1.0 - saturate(distance / radius)
  4. Multiply vertex color by attenuation
  5. Store result in vertex diffuse color
```

The falloff parameters (center, radius, intensity) are extracted from the D3D8 vertex shader constants that the dashboard sets. Multiple shader variants exist for different numbers of lights and different vertex formats.

## Matrix Caching

The device caches the combined World-View-Projection (`m_matWVP`) and World-View (`m_matWV`) matrices, recomputing them only when a dirty flag is set by `SetTransform()`. This avoids redundant matrix multiplications when the same camera/world state is used across multiple draw calls in a frame.

## Texture Handling

### XBX Textures (Xbox native)

Xbox textures use DXT compression with swizzled pixel layout:

1. **Read XBX header**: Extract width, height, format (DXT1/DXT3/DXT5), mipmap count
2. **Deswizzle**: Convert from Morton order to linear scanline order
3. **Upload to OpenGL**: `glCompressedTexImage2D()` with the appropriate GL format:
   - DXT1 → `GL_COMPRESSED_RGBA_S3TC_DXT1_EXT`
   - DXT3 → `GL_COMPRESSED_RGBA_S3TC_DXT3_EXT`
   - DXT5 → `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`

### Standard Textures (Desktop)

PNG, JPG, and BMP textures (used by skins and the Title Maker) are loaded via `stb_image` and uploaded as uncompressed RGBA.

### Texture Cache

The dashboard loads textures frequently as scenes change. A texture cache (`TXTCACHE[100]`) stores recently-used GL textures by name. On skin changes, the cache is flushed and all `CImageTexture` nodes are marked dirty for reload.

## What's NOT Translated

The translation layer deliberately skips D3D8 features the dashboard doesn't use:

- **Pixel shaders**: D3D8 pixel shaders aren't used by the dashboard (it uses TSS instead)
- **Stencil buffer**: Not used
- **Multi-render-targets**: Not used
- **Fog**: Set during init but not rendered (states accepted, no GL calls)
- **Point sprites**: Not used
- **Vertex declarations** (beyond FVF): The dashboard uses FVF exclusively

This keeps the translation layer small and focused. A general-purpose D3D8 wrapper would be 10x larger; this is purpose-built for one application.

## Coordinate System Differences

D3D8 uses a left-handed coordinate system (Y up, Z into screen). OpenGL uses right-handed (Y up, Z out of screen). The translation layer handles this by negating the Z component in the projection matrix, which flips the winding order. `glFrontFace(GL_CW)` compensates for the reversed triangle winding.
