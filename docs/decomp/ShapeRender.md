# Shape Rendering & Vertex Shaders

Shape rendering and vertex shader system in the 4920-5960 retail binary.

## Nodes

| Class | Address | FND String | Address |
|-------|---------|------------|---------|
| CShape | 0x000287dc | "Shape" | 0x000287ec |
| CAppearance | 0x00028780 | "Appearance" | 0x00028798 |
| CMaterial | 0x00028734 | "Material" | 0x00028748 |
| CSphere | 0x0002864c | "Sphere" | 0x0002865c |

Box is registered too: vtable `0x000285c0`, class object `0x001880f0`, prop map `0x00175130`.

| Class | vtable | Factory | Size |
|-------|--------|---------|------|
| Shape | 0x000284a0 | 0x00050cf0 | 0x30 |
| Appearance | 0x00028500 | 0x00050f50 | 0x24 |
| Material | 0x00028560 | 0x00051300 | 0x90 |
| Sphere | 0x00028440 | 0x000529d0 | 0x50 |
| Box | 0x000285c0 | 0x000526c0 | 0x37c |

Virtual slots used below: 4 Release, 6 GetNodeClass, 15 Advance, 16 Render, 17 GetBBox,
18 GetRadius, 22 GetTextureSurface, 23 (MeshNode) Init.

## CShape

Geometry + appearance container. Binds a geometry child (Sphere, mesh, etc.) to an Appearance node for rendering.

| Offset | Field |
|--------|-------|
| 0x1c | appearance |
| 0x20 | geometry |
| 0x24..0x2c | world position after the last draw |

The destructor (`0x00050d60`) releases appearance, then geometry.

**Render (`0x00050dd0`).**

```c
if (geometry == NULL) return;
for (cls = geometry->GetNodeClass(); cls; cls = cls->base)     // class chain walk
    if (cls == &MeshNode_class /* 0x001883e8 */) {
        mesh = geometry;
        if (mesh->dirty) {
            if (mesh->url && *mesh->url) Xap_load(mesh->url);
            mesh->Init();                                       // slot 23
            if (mesh->core == NULL) return;
        }
        if (mesh->core == NULL || mesh->core->GetFVF() == 0) return;
        g_pRenderMeshNode /* 0x0017e758 */ = mesh;
        SetVertexShader(GetFixedFunctionShader(fvf));           // 0x0005c740
        break;
    }
if (appearance) appearance->Render();
geometry->Render();
copy 16 dwords of the device's world matrix (pDevice + 0xde0);
position = row 3 (x, y, z); g_pRenderMeshNode = NULL;
```

**GetBBox (`0x00050eb0`)** tail calls the geometry's GetBBox, or zeroes all six floats.
**GetRadius (`0x00050ef0`)** returns the geometry's radius, or 0.0.
**Advance (`0x00050f10`)** runs the CNode advance, then geometry, then appearance.

## CAppearance

Material + texture binding. Links a CMaterial and texture references for a given shape.

Material at `0x1c`, texture at `0x20`. The destructor (`0x00050fc0`) releases them in that order.

**Render (`0x00051030`).** Every device call is inlined into shadow state writes and push
buffer packets. Decoded, in order:

| State | Value |
|-------|-------|
| CULLMODE | CCW (`0x901` in the Xbox encoding) |
| ZWRITEENABLE | 0 |
| ALPHABLENDENABLE | 1 |
| SRCBLEND | SRCALPHA (`0x302`) |
| DESTBLEND | INVSRCALPHA (`0x303`) |
| stage 0 COLOROP, ALPHAOP | DISABLE |
| stage 0 TEXCOORDINDEX | 0 |
| stage 0 TEXTURETRANSFORMFLAGS | 0 |
| stage 1 COLOROP, ALPHAOP | DISABLE |
| texture stages 0 and 1 | NULL |

Then, with a texture node: its Render, then GetTextureSurface. With a surface it goes on stage 0
and COLOROP/ALPHAOP become SELECTARG1 of TEXTURE. Without one ALPHAOP, ALPHAARG1, COLOROP,
COLORARG1 become SELECTARG1 of TFACTOR and TEXTUREFACTOR is set to 0, so the shape draws clear
until its image arrives. The material node renders last.

**Advance (`0x000512c0`)**: CNode advance, material, texture.

## CMaterial

VRML97 material properties: diffuse color, emissive color, specular color, transparency. Standard Phong-style lighting parameters.

| Offset | Field | Default |
|--------|-------|---------|
| 0x1c | ambientIntensity | 0.2 |
| 0x20 | diffuseColor | 0.8, 0.8, 0.8 |
| 0x2c | emissiveColor | 0, 0, 0 |
| 0x38 | shininess | 0.2 |
| 0x3c | specularColor | 0, 0, 0 |
| 0x48 | transparency | 0 |
| 0x4c | D3DMATERIAL8 | |

**Render (`0x000513a0`).**

```c
a = 1.0 - transparency;
Diffuse  = (diffuseColor, a);
Ambient  = (ambientIntensity, ambientIntensity, ambientIntensity, a);
Specular = (specularColor, a);
Emissive = (emissiveColor, a);
Power    = shininess;
copy 0x11 dwords to pDevice + 0xf00; dirty flags |= 0x9000;   // SetMaterial, inlined
```

Material has no destructor of its own; its vtable points at the shared scalar deleting
destructor.

## Box

None of these are defined functions in the project database; they read straight from the
disassembly.

| Offset | Field |
|--------|-------|
| 0x1c | size, default (1, 1, 1) |
| 0x28 | vertex buffer |
| 0x2c | index buffer |
| 0x30 | 24 vertices, 32 bytes each (position, normal, uv) |
| 0x330 | 36 WORD indices |
| 0x378 | dirty, starts 1 |

**Render (`0x00052820`).**

1. If dirty, build the cube into the node's own arrays (`0x00051450`, outside facing) and clear dirty.
2. With no vertex buffer, create a 0x300 byte one through the shared create helper
   (`0x0003d410`, which frees mesh and texture caches and retries until it gets memory) and copy
   the vertices in with `rep movsd`.
3. Same for the index buffer, 0x48 bytes (`0x0003d390`).
4. With both: SetStreamSource(0, vb, 0x20), SetIndices(ib, 0), SetVertexShader(0x112), then
   DrawIndexedVertices(TRIANGLELIST, 36, indices).

**GetBBox (`0x00052960`)**: center (0, 0, 0), size as set.
**GetRadius (`0x00052990`)**: `sqrt(x*x + y*y + z*z) * 0.5`, in x87.
**Destructor (`0x00052750`)**: releases the vertex buffer, then the index buffer.

### The cube builder (`0x00051450`)

Fastcall with the inside flag in CL, vertices in EDX and indices in EAX, size by value on the
stack. Fully unrolled: each vertex is assembled in a 32 byte stack record and copied out with
`rep movsd`. Constants: `0.5` (`0x0002bed4`), `-0.5` (`0x0002bedc`), `1.0` (`0x0002bed8`),
`-1.0` (`0x0002bef0`), `0.01` (`0x0002bfe8`), `0.99` (`0x0002bfa8`).

Every position is `+-0.5 * size` per axis, texture coordinates are inset to 0.01 and 0.99, and the
faces come in the order -z, +z, +y, -y, +x, -x with their outward normals. The outside facing
corners (sign per axis, then u and v as low 0 / high 1):

| Face | Corner 0 | Corner 1 | Corner 2 | Corner 3 |
|------|----------|----------|----------|----------|
| -z | - - - / 0 0 | + - - / 1 0 | + + - / 1 1 | - + - / 0 1 |
| +z | + + + / 0 1 | + - + / 0 0 | - - + / 1 0 | - + + / 1 1 |
| +y | - + - / 0 0 | + + - / 1 0 | + + + / 1 1 | - + + / 0 1 |
| -y | + - + / 1 1 | + - - / 1 0 | - - - / 0 0 | - - + / 0 1 |
| +x | + - - / 0 0 | + - + / 1 0 | + + + / 1 1 | + + - / 0 1 |
| -x | - + + / 0 1 | - - + / 0 0 | - - - / 1 0 | - + - / 1 1 |

The inside facing branch writes the same four corners of each face in reverse order and keeps
the normals. The indices are `0, 1, 2, 2, 3, 0` per face, offset by 4 each face. Box only ever
calls it outside facing.

## CSphere

Procedural sphere mesh generator. Builds vertex/index buffers at runtime from a radius parameter.

A MeshNode (its constructor is inlined) with radius 1.0 at `0x40`, slices 32 at `0x44` and stacks
32 at `0x48`, created dirty. Its destructor (`0x00052a90`) is only the MeshNode one.

**Init (`0x00052aa0`, MeshNode slot 23).**

```c
core = MakeSphere(radius, slices, stacks);     // 0x0005c390: D3DXCreateSphere + D3DXComputeNormals
if (core) dirty = 0;
```

## Vertex Shader System

The binary contains vertex shader management for both effect shaders and a fixed-function fallback path. Compressed normals are supported (packed into a single DWORD, expanded in the vertex shader).

Both lookups use 0x14 byte entries: effect number, microcode pointer, FVF, shader handle (0 until
created), and a spare dword.

**Effect table (`0x00175bb0`, 12 entries).**

| Effect | Microcode | FVF |
|--------|-----------|-----|
| 1 | 0x001754f0 | 0x20000042 (XYZ, packed normal, diffuse) |
| 1 | 0x001754f0 | 0x20000002 |
| 1 | 0x00175648 | 0x20000102 (with one texture coordinate) |
| 2 | 0x00175ad0 | 0x20000002 |
| 3 | 0x001757a0 | 0x20000102 |
| 4 | 0x001758f8 | 0x20000002 |
| 1 | 0x001754f0 | 0x52 (XYZ, float normal, diffuse) |
| 1 | 0x001754f0 | 0x12 |
| 1 | 0x00175648 | 0x112 |
| 2 | 0x00175ad0 | 0x12 |
| 3 | 0x001757a0 | 0x112 |
| 4 | 0x001758f8 | 0x12 |

**Fixed function table (`0x00175b74`, 3 entries)**: FVFs `0x20000042`, `0x20000002`,
`0x20000102`, all pointing at the `0x001754f0` microcode, which is never used for them.

**Microcode.** Five NV2A programs. The first word is `0x2078` with the instruction count in the
high half, then four words per instruction.

| Address | Instructions | Used for |
|---------|--------------|----------|
| 0x001754f0 | 21 | effect 1 |
| 0x00175648 | 21 | effect 1, textured |
| 0x001757a0 | 21 | effect 3 |
| 0x001758f8 | 29 | effect 4 |
| 0x00175ad0 | 10 | effect 2 |

**Declarations.**

| Address | Tokens |
|---------|--------|
| 0x00176660 | stream 0; v0 FLOAT3; v2 NORMPACKED3; v3 D3DCOLOR; end |
| 0x00176674 | stream 0; v0 FLOAT3; v2 NORMPACKED3; end |
| 0x00176688 | stream 0; v0 FLOAT3; v2 NORMPACKED3; v9 FLOAT2; end |
| 0x0017669c | stream 0; v0 FLOAT3; v3 FLOAT3; v6 FLOAT2; end |
| 0x001766b0 | stream 0; v0 FLOAT3; v3 FLOAT3; end |
| 0x001766c0 | stream 0; v0 FLOAT3; v3 NORMPACKED3; v6 FLOAT2; end |
| 0x001766d4 | stream 0; v0 FLOAT3; v3 NORMPACKED3; end |

The first three are the fixed function ones, 0x14 bytes apart and indexed by table position.

**GetEffectShader (`0x0005c6c0`).** Effect in ECX, FVF in EAX.

```c
if (effect == 0) return fvf;
for (i = 0, e = table; e->effect != effect || e->fvf != fvf; i++, e++)
    if (i >= 11) return fvf;
if (e->shader == 0) {
    if (fvf & 0x20000000) decl = (fvf & 0x100) ? 0x1766c0 : 0x1766d4;
    else                  decl = (fvf & 0x100) ? 0x17669c : 0x1766b0;
    CreateVertexShader(decl, e->microcode, &e->shader, 0);     // 0x00147cc0
}
return e->shader;
```

**GetFixedFunctionShader (`0x0005c740`).** FVF in EAX. Walks the three entries for a matching FVF
and returns the FVF itself when none matches. On first use it builds the shader inline from the
declaration at `0x00176660 + i * 0x14` with no microcode: size the declaration's constants,
XMemAlloc `0x118` plus that, zero it, mark 16 attribute slots, parse the declaration in, and
store the pointer with bit 0 set as the handle.

## CompressNormal (`0x0005c820`)

The vector comes in EAX. The length is computed in x87 (`fsqrt`), its reciprocal in SSE, and each
component multiplied by it. Then `ftol2` of `x * 1023`, `y * 1023` (`0x0002c014`) and `z * 511`
(`0x0002c010`):

```c
return (ftol(x * 1023) & 0x7ff) | ((ftol(y * 1023) & 0x7ff) << 11) | (ftol(z * 511) << 22);
```

`ftol2` truncates through a signed 64-bit integer, so negative components land in the packed
fields as two's complement.

## Shader Constants

Constant registers are passed to `D3DDevice_SetVertexShaderConstant1/4` biased by 96, so `0x88`
is c40 and `0x60` is c0. The world matrix is the top of the matrix stack (`0x0017e7fc`, vtable
slot `0x44`), view is `0x0017f468`, projection `0x0017f4e8`.

Both setters end with the same light direction: `(1, 1, -1, 0)` normalized, transformed by the
transpose of world * view with D3DXVec3TransformNormal, normalized again.

**SetReflectShaderFrameValues (`0x0005c8d0`).**

| Register | Value |
|----------|-------|
| c40..c43 | transpose(world * view) |
| c44..c47 | transpose(projection) |
| c48 | (0, 0, 1, 0.5) |
| c49 | light direction |

Then LIGHTING goes to FALSE (inlined state write).

**Falloff frame values (`0x0005ca00`).** Runs whenever the world matrix changes.

| Register | Value |
|----------|-------|
| c0..c3 | transpose(world * view * projection) |
| c10..c13 | transpose(world * view) |
| c5..c8 | inverse(world * view) |
| c9 | (0, 0.5, 1, -1) |
| c4 | light direction |

Then texture stage 0 is set to NULL and LIGHTING to FALSE, both inlined.

**Falloff colors.** Not a function of its own: every falloff family material Setup inlines it,
for example CFalloffMatInfo_Setup (`0x00057790`):

```c
c15 = (side.r, side.g, side.b, side.a * effectAlpha);
c16 = (front.r - side.r, front.g - side.g, front.b - side.b, (front.a - side.a) * effectAlpha);
```

The scene-wide fade `effectAlpha` lives at `0x00175ca0` and starts at 1.0.

## Mesh Normal Smoothing

Mesh normal smoothing is a standalone function, not a script-callable node. Called during mesh loading from XIP archives.

Purpose: computes smooth vertex normals by averaging face normals across shared vertices. Vertices that share a position but exceed the threshold angle between their face normals are treated as separate, which preserves hard edges on boxy geometry while smoothing curved surfaces.

Algorithm:

1. Build face normals for all triangles in the mesh.
2. For each vertex, collect all faces that reference that position.
3. Average the face normals for faces within the smoothing angle threshold.
4. Write the resulting normal back to the vertex buffer.

The output normals use `D3DFVF_NORMPACKED3`, the Xbox-specific compressed normal format that packs a unit normal into a single DWORD. This saves 8 bytes per vertex compared to a full float3 normal, meaningful savings on meshes with thousands of vertices in a 64MB system.
