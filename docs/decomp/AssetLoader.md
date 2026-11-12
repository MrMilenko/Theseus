# Asset Loader

What we found in the Xbox Dashboard about how it gets textures and meshes on screen: the
`Texture` and `ImageTexture` nodes, a hundred slot texture cache fed by a background file
loader, the XPR texture parser, and the `Mesh` node with the `.xm` reader behind it.
Everything here comes from the 5960 retail XBE. Theseus rebuilds it in
`render/asset_loader.cpp`.

## Registration

| Class | Name string | Size | Factory | vtable |
|-------|-------------|------|---------|--------|
| CTexture | "Texture" | 0x30 | `0x00060db0` | `0x0002a518` |
| CImageTexture | "ImageTexture" | 0x38 | `0x00060ef0` | `0x0002a4b8` |
| CMeshNode | "Mesh" | 0x40 | `0x0005c050` | `0x00029d70` |

Texture props (PRD at `0x00175d74`): `repeatS` (bool, 0x1c), `repeatT` (bool, 0x1d).
ImageTexture (PRD at `0x00175ee0`): `url` (string, 0x30), `alpha` (bool, 0x34).
Mesh (PRD at `0x0017504c`): `url` (string, 0x1c). Its one script function is `load`
(FND entry `0x00188418`, sig 12).

The other fields: Texture keeps its format at 0x20 (the factory writes 6, A8R8G8B8), the
surface at 0x24 and the size at 0x28/0x2c. ImageTexture adds a reload flag at 0x35 that
starts set. Mesh keeps a dirty flag at 0x20, the last render time (double) at 0x28, the
mesh at 0x30, whether it owns that mesh at 0x34, and a link to the next mesh node at 0x38
(list head `0x0017c8d4`).

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x00060990 | ParseTexture | File bytes to a texture |
| 0x00060c90 | LoadTexture | Forced `.xbx` lookup in the XIPs |
| 0x00060d40 | CreateTexture | One level managed texture, reports the size it got |
| 0x00060ec0 | CTexture::Create | Texture vtable slot 23 |
| 0x00060f90 | CImageTexture::GetTextureSurface | ImageTexture slot 22 |
| 0x00060fc0 / 0x00061030 | CBackgroundLoader ctor / dtor | |
| 0x00061090 | CBackgroundLoader::Fetch | Starts the read |
| 0x000611f0 | CBackgroundTexture dtor | Clears the slot's loading flag |
| 0x00061210 | CBackgroundTexture::OnComplete | Loader vtable `0x0002a4ac` slot 1 |
| 0x00061290 | CleanupTextureCache | Frees the oldest texture |
| 0x000614f0 | (unload by url) | Drops every slot holding a url; only the MOTD clear calls it |
| 0x000615a0 | FindTexture | The cache |
| 0x00061760 | CImageTexture::Load | Surface from the cache |
| 0x00061820 | CImageTexture::OnSetProperty | |
| 0x0005bd00 | CMesh dtor | |
| 0x0005bda0 | CMesh::GetFVF | Slot 1; the same three bytes serve `CStrObject::length` |
| 0x0005bdb0 | CMesh::Create | Reads a `.xm` file |
| 0x0005bea0 | CMesh::Render | Slot 0 |
| 0x0005c1f0 | CMeshNode::Init | Clears dirty |
| 0x0005c200 | CMeshNode::Render | |
| 0x0005c250 | CMeshNode::load | |
| 0x0005c320 | LoadMesh | New CMesh from a path |
| 0x0005c390 | MakeSphere | Used by `Sphere` (`0x00052aa0`) |

Several Ghidra labels in the project are off by one function here: `CBackgroundTexture_OnComplete`
is ParseTexture, `GetTexture` is `CImageTexture::Load`, `CreateMesh_File` is LoadMesh.

## The texture cache

A flat array of one hundred 0x20 byte slots at `0x0017da70`: url, texture, last used time
(double), a loading flag at 0x10, and the time the texture arrived (double) at 0x18. A pointer
at `0x0017da68` marks the slot being filled so a memory cleanup triggered by that very load
leaves it alone.

```
FindTexture(url, async, width, height, inXIP):        // 0x000615a0
    for each slot:
        if (slot.url && !wcsicmp(slot.url, url)) { slot.lastUsed = now; return slot; }
        if (!free && !slot.loading)
            if (!slot.url) free = slot;
            else if (!old || slot.lastUsed < old.lastUsed) old = slot;
    if (!free && old) { free(old.url); Release(old.texture); zero old; free = old; }
    locked = free; free.url = copy of url; free.lastUsed = now;
    if (!async || inXIP) { free.texture = LoadTexture(url); free.timeLoaded = now; }
    else { free.loading = 1; loader = new CBackgroundTexture(free, width, height);
           if (!loader->Fetch(url)) delete loader; }
    locked = NULL; return free;
```

`CleanupTextureCache` runs the same oldest search but skips the locked slot and anything
loading, frees one, and says whether it did. It's the first thing every out of memory retry
in the dashboard tries; `CleanupMeshCache` is second, and a third failure prints
`xdash: out of memory (failed to allocate %d bytes)` and reboots to the firmware. The ten
slot unrolled loop is the compiler's.

The title image material (`CIconMatInfo_Setup`, `0x000572d0`) calls FindTexture with async
set and fades the image in from `timeLoaded`. `ImageTexture` goes through `CImageTexture::Load`
(`0x00061760`): release the old surface, `FindTexture(url, 0, 0, 0, 0)`, `AddRef` the cached
texture and read its level 0 size.

`0x000614f0` walks all hundred slots, frees and zeroes every one whose url matches, and
returns whether it found any. Its only caller is the MOTD clear (`0x0006a590`), which
deletes the cached `t:\motd` files and drops the `.xbx` one from the cache.

## The background loader

`CBackgroundLoader` is 0x2c bytes: vtable, next, file handle, an `OVERLAPPED` at 0x0c, the
buffer at 0x20, its size at 0x24, the absolute url at 0x28. The constructor appends it to
the tail of the list at `0x0017e6f0`.

```
Fetch(url):                                           // 0x00061090
    MakeAbsoluteURL(abs, url); this->url = copy of abs;
    flags = OVERLAPPED | SEQUENTIAL_SCAN, plus NO_BUFFERING when url starts c: or y:;
    file = CreateFile(abs, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, flags, 0);
    if (file == INVALID) return 0;
    size = (GetFileSize(file) + 511) & ~511;          // whole sectors for unbuffered I/O
    data = new BYTE[size]; overlapped = 0;
    if (ReadFile(file, data, size, 0, &overlapped)) { OnComplete(); delete this; }
    else if (GetLastError() != ERROR_IO_PENDING) return 0;
    return 1;
```

Nothing polls inside the loader. The frame loop (`0x0002da00`), right after the lerpers
advance, walks the list and for every loader whose `overlapped.Internal` is no longer
`STATUS_PENDING` (0x103) calls `OnComplete` and deletes it.

`CBackgroundTexture` adds the cache slot (0x2c) and the size the image must be (0x30/0x34).
OnComplete runs ParseTexture on the buffer; when that fails and the width asked for was 64
it loads `xboxlogo64.xbx` instead, for 128 `xboxlogo128.xbx`. Then it clears the slot's
loading flag and stamps `timeLoaded`. The destructor clears the loading flag too, so a
failed Fetch leaves the slot usable.

## ParseTexture (`0x00060990`)

Decides by the url's extension:

- `xt`: the first 0x14 bytes are a texture header, the rest is pixel data.
- `xbx` with `XPR0` magic: the header sits after the 12 byte XPR header, the pixels at
  `dwHeaderSize`. It refuses a header region under 0x14 bytes, a resource that isn't a
  texture (`Common & 0x70000 != 0x40000`), an empty pixel block, or one that runs past the
  file. When the caller gave a width and height, the format's USIZE and VSIZE fields
  (`>> 20 & 0xf`, `>> 24 & 0xf`) must encode exactly those, through a table of powers of two
  that reads 0 past 4096.
- Anything else, or an `.xbx` without the magic: `D3DXCreateTextureFromFileInMemoryEx` with
  default size, one mip level, unknown format, managed pool, retrying through the cache
  cleanups on `E_OUTOFMEMORY`.

Both XPR branches copy the header into a 0x14 byte noncontiguous block and the pixels into
128 byte aligned contiguous memory, point the header at the pixels (physical address unless
it's a vertex buffer, what `Register` does) and set `D3DCOMMON_D3DCREATED`.

LoadTexture (`0x00060c90`) is only the XIP half: make the url absolute, replace whatever
extension it has with `xbx`, `FindObjectInXIP(path, TEXTURE)`.

## Meshes

A `.xm` file is a 0x18 byte header (primitive type, faces, FVF, stride, vertices, indices),
the vertices, then 16 bit indices. The types are the PC D3D numbers: 4 becomes the Xbox
triangle list, 5 the strip, anything else makes `Create` fail. None of the three reads is
checked. The buffers are built by hand (`0x0003d410`, `0x0003d390`, see [XIP](XIP.md)).

`LoadMesh` (`0x0005c320`) is `new CMesh`, `CreateFile(path, GENERIC_READ, 0, 0, OPEN_EXISTING,
SEQUENTIAL_SCAN)`, and `Create` if it opened. It returns the mesh either way.

```
CMesh::Render(bSetFVF):                               // 0x0005bea0
    if (!vb || !ib) return;
    if (bSetFVF) SetVertexShader(GetFixedFunctionShader(fvf));
    if (faces > 800 && !g_bEdgeAntialiasOverride) { EDGEANTIALIAS = 0; MULTISAMPLEANTIALIAS = 1; }
    if (g_uMesh == 0) {                               // 0x00179f5c, zeroed every frame
        save the device view (pDevice + 0xc60);
        view = inverse(affine(camera rotation 0x00187fe0, position 0x00187fd4)), third row negated;
    }
    SetStreamSource(0, vb, stride); SetIndices(ib, 0);
    DrawIndexedVertices(type, the index count for that many faces, indices);
    if (g_uMesh == 0) put the saved view back;
    g_uMesh++;
```

So the first loose mesh drawn each frame gets the camera's own view, without the noisy
wobble, and every later one the device's current view.

```
CMeshNode::load(file):                                // 0x0005c250
    MakeAbsoluteURL(abs, file);
    mesh = FindObjectInXIP(abs, any);
    if (mesh) { ownMesh = 0; return; }
    ownMesh = 1; mesh = LoadMesh(abs);
    if (!mesh->GetFVF()) { delete mesh; MakePath(abs, xipDir, file); mesh = LoadMesh(abs); }
```

Render stamps the time, loads on first use (the inlined `Initialize`: load if the url is
set, then `Init`), and returns if there's still no mesh. Otherwise it draws with
`bSetFVF = (g_pRenderMeshNode != this)`: the shape renderer sets the FVF itself for the
node it's drawing. The destructor `free`s an owned mesh without running its destructor,
because the base class has no virtual one.

MakeSphere (`0x0005c390`) asks D3DX for a sphere, clones it to XYZ, normal and diffuse
(stride 0x1c), recomputes normals and takes the buffers. On any failure it releases the
buffers, deletes the mesh and carries on with a NULL mesh into the repack below. Then it
repacks each vertex into 0x14 bytes, position, `CompressNormal` (`0x0005c820`) and diffuse,
and switches the FVF to XYZ, NORMPACKED3, DIFFUSE.

## Where Theseus differs

- `LoadTexture` first tries the active skin folder under every name in the asset's
  equivalence group (`render/skin_assets.h`), then the XIPs, then the file itself
  (`.tga`, `.bmp`, `.dds` through D3DX on Xbox). A miss raises an alert.
- `CMesh::Load` and `CMeshNode::load` try the skin folder before the XIPs the same way.
- Texture has a `titleImage` prop and ImageTexture a `fromfile` prop. Every live
  ImageTexture is on a list so a skin switch can drop and reload them.
- `FlushTextureCache` and `FlushMeshCache` throw everything out for a skin switch, and
  `PreloadSkinTextures` loads a skin's `.xbx` files ahead of time.
- The background loader also turns off buffering for the e:, f:, g:, r: and s: partitions.
- The first mesh of a frame is drawn with the device view like any other; Theseus's camera
  loads the device view every frame.
- A loose mesh that isn't beside the scene is looked for beside the app, not in the XIP folder.
- A texture header whose pixel allocation fails is freed and the load returns NULL.
- No out of memory reboot: allocations retry through `NewFailed` and give up.
