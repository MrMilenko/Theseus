# Shape Rendering & Vertex Shaders

Shape rendering and vertex shader system in the 4920-5960 retail binary.

## Nodes

| Class | Address | FND String | Address |
|-------|---------|------------|---------|
| CShape | 0x000287dc | "Shape" | 0x000287ec |
| CAppearance | 0x00028780 | "Appearance" | 0x00028798 |
| CMaterial | 0x00028734 | "Material" | 0x00028748 |
| CSphere | 0x0002864c | "Sphere" | 0x0002865c |

## CShape

Geometry + appearance container. Binds a geometry child (Sphere, mesh, etc.) to an Appearance node for rendering.

## CAppearance

Material + texture binding. Links a CMaterial and texture references for a given shape.

## CMaterial

VRML97 material properties: diffuse color, emissive color, specular color, transparency. Standard Phong-style lighting parameters.

## CSphere

Procedural sphere mesh generator. Builds vertex/index buffers at runtime from a radius parameter.

## Vertex Shader System

The binary contains vertex shader management for both effect shaders and a fixed-function fallback path. Compressed normals are supported (packed into a single DWORD, expanded in the vertex shader).

Falloff and reflection shader setup is present for environment-mapped surfaces.

The global `g_nEffectAlpha` controls scene-wide transparency for fade transitions.

## Mesh Normal Smoothing

Mesh normal smoothing is a standalone function, not a script-callable node. Called during mesh loading from XIP archives.

Purpose: computes smooth vertex normals by averaging face normals across shared vertices. Vertices that share a position but exceed the threshold angle between their face normals are treated as separate, which preserves hard edges on boxy geometry while smoothing curved surfaces.

Algorithm:

1. Build face normals for all triangles in the mesh.
2. For each vertex, collect all faces that reference that position.
3. Average the face normals for faces within the smoothing angle threshold.
4. Write the resulting normal back to the vertex buffer.

The output normals use `D3DFVF_NORMPACKED3`, the Xbox-specific compressed normal format that packs a unit normal into a single DWORD. This saves 8 bytes per vertex compared to a full float3 normal, meaningful savings on meshes with thousands of vertices in a 64MB system.
