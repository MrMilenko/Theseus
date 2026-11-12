# XIP Archives

What we found in the Xbox Dashboard about its XIP archives: one `XIP0` file per scene, holding
its scripts, textures, raw data and meshes. Everything here comes from the 5960 retail XBE.
Theseus rebuilds it in `render/xip_archive.cpp`, one file for both builds.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x0003d390 | (index buffer alloc) | Builds an index buffer resource by hand, with the out of memory retry |
| 0x0003d410 | (vertex buffer alloc) | Same for a vertex buffer |
| 0x0003d4a0 | CFileBuffer::SetFile | Keeps the handle, finds the archive in the signed `XIPS` section |
| 0x0003d570 | (block hash check) | Hashes a 64 KB block and compares it with the signed table |
| 0x0003d5e0 | CFileBuffer::Seek | Loads the block holding an offset |
| 0x0003d710 | CFileBuffer::Read | Copies out of the block buffer, refilling it as it goes |
| 0x0003d900 | LoadXIP | Finds or opens an archive in the pool and starts its load |
| 0x0003dab0 | FindInXIPAndDetach | Hands a raw entry's bytes to the caller |
| 0x0003dba0 | FindObjectInXIP | Looks an entry up across every loaded archive |
| 0x0003dd20 | CXipFile::~CXipFile | Frees tables, mesh buffers and the block buffer |
| 0x0003ddf0 | CXipFile::Open | Stores the file name and the lookup key |
| 0x0003dec0 | CXipFile::StartLoadThread | Thread entry: load, or reload the mesh buffers |
| 0x0003def0 | CXipFile::Load | Reads the header, the tables and every entry |
| 0x0003e030 | (texture read) | XPR0 entry to a texture |
| 0x0003e110 | CXipFile::CreateObjects | Reads the entries in file order |
| 0x0003e270 | (name compare) | `bsearch` compare, `_stricmp` |
| 0x0003e290 | CXipFile::Find | Path to entry index |
| 0x0003e320 | CXipFile::FindObject | Path to object, with a type check and an `AddRef` for textures |
| 0x0003e390 | CXipFile::DeleteMeshBuffers | Releases all ten buffer pairs |
| 0x0003e440 | CleanupMeshCache | Frees the mesh buffers of the archive drawn longest ago |
| 0x0003e5f0 | CXipFile::ReadIndexBuffer | Index buffer entry to a buffer |
| 0x0003e650 | CXipFile::ReadVertexBuffer | Vertex buffer entry to a buffer |
| 0x0003e710 | CXipFile::ReloadMeshBuffers | Reads every mesh buffer again after an eviction |
| 0x0003e800 | CXipFile::Reload | Flags a reload and starts the loader (labeled `CMeshRef_Render` in the project) |
| 0x0003e850 | CMeshRef::Render | CMeshRef vtable (`0x00024dbc`) slot 0 |
| 0x0003ead0 | CMeshRef::GetFVF | Slot 1 |

Globals: the pool `c_rgXipFile` at `0x001804e0` (twenty 0x150 byte slots, torn down by the
`atexit` thunk at `0x000ce600`), the count at `0x0017e734`, the frame clock the cache times come
from at `0x0017e808`, the XIP folder string at `0x0017e828`, the out of memory stage at
`0x0017c028`, and the disk lock every `ReadFile` holds shared at `0x0017aeb0`.

## The file

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic `XIP0` (`0x30504958`) |
| 4 | 4 | data start |
| 8 | 2 | entry count |
| 0xa | 2 | name count |
| 0xc | 4 | data size |

Then one 16 byte record per entry (data offset, size, type, timestamp), one 4 byte directory record
per name (entry index, name offset), the name pool, and the data. `Load` sizes the name pool as
`dataStart - 16 - entries * 16 - names * 4` and never checks it.

Entry types, from the `CreateObjects` switch:

| Type | What `CreateObjects` makes |
|------|----------------------------|
| 1 | nothing, and nothing is read |
| 2 | a texture (`0x0003e030`) |
| 4 | a 0x14 byte CMeshRef: archive, `offset >> 24` as the buffer, `offset & 0xffffff` as the first index, `size` as the primitive count |
| 5 | an index buffer, in the slot of the vertex buffer count at that moment |
| 6 | a vertex buffer: a 4 byte vertex count and a 4 byte FVF lead the vertices |
| anything else | `size` raw bytes from `XAppAllocMemory` |

Entries are read back to back with no seeking, so the data has to be in table order. The
directory is sorted, because `Find` binary searches it.

## Reading the file

`Load` opens the archive with `NO_BUFFERING | SEQUENTIAL_SCAN` (`0x28000080`), after turning a
leading `y:` into `x:`. Unbuffered handles only read whole aligned blocks, so every read goes
through `CFileBuffer`, one 64 KB `VirtualAlloc` block at a time. The object is six dwords: the
handle, the signed section, the buffer, the bytes the last block gave, the read index, and one past
the block in the buffer.

```
Read(pv, cb):                                   // 0x0003d710
    if (!buffer) buffer = VirtualAlloc(0x10000);  // retry: texture cache, mesh cache, then reboot
    for (;;) {
        if (cb < 1) return total;
        if (index < size) { copy min(size - index, cb); advance; }
        if (index != size) continue;
        lock shared 0x17aeb0; ok = ReadFile(h, buffer, 0x10000, &got); unlock;
        if (!ok) HalReturnToFirmware(4);
        if (got == 0) return -1;
        hash check the block; current++; size = got; index = 0;
    }
```

The buffer refills the moment it runs dry, even when the copy just finished, so a read ending on
the file's last byte returns -1. `Seek` reads the block an offset falls in unless `current` already
names it; since `current` is one past the block in the buffer, seeking back into that block reads
it again.

Retail archives are signed. `SetFile` walks the `XIPS` section for the archive's file name and
keeps its record, and every block `Read` or `Seek` loads is run through `XCCalcDigest` and
compared with the record's next 20 byte hash (`0x0003d570`). A missing record or a wrong hash
calls `HalReturnToFirmware(4)`, the fatal error reboot.

## The pool

`LoadXIP` puts relative names under `Q:/Xips/`, cleans the path, drops the extension, and compares
that with every slot's key. A hit stamps the slot's cache time and, if its mesh buffers were
evicted, reads them back before returning it. Otherwise it opens the next slot, counts it, and
loads it on a new thread; if no thread starts, it runs the loader inline. There is no bound check:
a twenty first archive would write past the pool. Slots are never freed. Nothing in the binary
unloads an archive; only its mesh buffers come and go.

`Open` keeps the path without its extension as the key, except that an archive named `default`
gives up its own name too, so `Q:\Xips\default.xip` answers for `Q:\Xips`. `LoadXIP`'s own
comparison keeps `default`, so a second `LoadXIP` of the default archive would open a new slot.

`FindObjectInXIP` and `FindInXIPAndDetach` search newest archive first and skip any that is
locked or not loaded. For a texture asked of slot 0, `FindObjectInXIP` swaps the file name's
extension for `xbx` and looks it up under the XIP folder instead, since the default archive packs
its textures as `.xbx`. `FindInXIPAndDetach` hands the raw bytes and size to the caller and clears
the archive's pointer, so the caller frees them.

```
Find(url):                                      // 0x0003e290
    n = strlen(dirPath);
    if (_strnicmp(url, dirPath, n) || url[n] != '\\') return -1;
    hit = bsearch({names, url + n + 1}, directory, nameCount, 4, compare);
    return hit ? hit->entry : -1;

FindObject(url, type):                          // 0x0003e320
    n = Find(url);
    if (n == -1 || (type != -1 && filedata[n].type != type)) return 0;
    if (type == 2) objects[n]->AddRef();
    return objects[n];
```

## Textures

```
texture(file, cb):                              // 0x0003e030
    xpr = new(cb); file.Read(xpr, cb);
    if (xpr->magic != 'XPR0') return 0;
    tex = AllocNoncontiguous(0x14); copy 0x14 bytes from xpr + 0xc;
    bits = AllocContiguous(xpr->total - xpr->headerSize, 0x80);
    if (!bits) return 0;
    copy the pixels from xpr + headerSize;
    tex->Data = 0; Register(bits); tex->Common |= 0x01000000;
    return tex;
```

`Register` is inlined: the address is masked to physical unless the resource is a push buffer.
The entry's bytes are never freed, and a failed pixel allocation leaks the header.

## Mesh buffers

A CXipFile carries ten 24 byte buffer pairs at `+0x2c` (FVF, stride, vertex count, index count,
vertex buffer, index buffer), then the vertex buffer count `+0x11c`, the index buffer count
`+0x120`, the cache time `+0x128`, the lock flag `+0x130`, the `CFileBuffer` `+0x134` and the
reload flag `+0x14c`. The loaded flag is `+0x28`.

The buffers are built without the device: `0x0003d390` and `0x0003d410` `XMemAlloc` a resource
header and the data, and the readers write straight into it. `ReadVertexBuffer` first waits for
the GPU if the buffer is still in use. The stride is `(size - 8) / count`, with no check on the
count. `ReloadMeshBuffers` seeks to each buffer entry in table order and reads it again, numbering
index buffers and vertex buffers separately.

Under memory pressure `XAppAllocMemory` and the block allocator call `CleanupTextureCache` first,
then `CleanupMeshCache`, which frees the buffers of the unlocked, loaded archive with the oldest
cache time that still has some. The next `CMeshRef::Render` of that archive sees a NULL vertex
buffer and calls `Reload`; `CInline_Render` (`0x0004e5d0`) checks the same thing inline
(`loaded && !reloading && vbCount && !meshBuffers[0].vb`) and reloads before drawing.

```
CMeshRef::Render(bSetFVF):                      // 0x0003e850
    xip->cacheTime = now; buf = &xip->meshBuffers[index];
    if (xip->reloading) return;
    if (!buf->vb) { xip->Reload(); return; }
    if (bSetFVF) SetVertexShader(GetFixedFunctionShader(buf->fvf));
    if (count > 800 && !g_bEdgeAntialiasOverride) { EDGEANTIALIAS = 0; MULTISAMPLEANTIALIAS = 1; }
    if (g_uMeshRef++ == 0) {
        view = inverse(affine(rotation 0x00187fe0, position 0x00187fd4)), third row negated;
        wv = matrix stack top * view;
        c0..3 = transpose(wv * projection); c10..13 = transpose(wv);
        c5..8 = inverse(wv); c4 = the light direction in object space;
    }
    SetStreamSource(0, buf->vb, buf->stride); SetIndices(buf->ib, 0);
    DrawIndexedVertices(TRIANGLELIST, count * 3, indices + first);
```

The constant numbers above are the binary's minus its bias of 96. Theseus sets the same constants
on every world matrix change instead (see `SetFalloffShaderFrameValues`, `0x0005ca00`).

## Where Theseus differs

- No `XIPS` signature check and no `y:` to `x:` swap; files open through Theseus's own path layer.
- A twenty first archive is refused instead of overrunning the pool; a sync `LoadXIP` that fails
  returns NULL.
- `Load` checks every read, refuses a header whose name pool would be empty or whose
  `dataStart + dataSize` isn't the file size, and closes a refused file.
- `CreateObjects` refuses mesh references past the last slot and mesh buffers past ten, and reads
  past what it refuses so the entries after stay in line. Textures must hold an XPR header and a
  resource header, and their sizes must fall inside the entry.
- Reload refuses buffers `CreateObjects` refused, and `CMeshRef` refuses an index past the buffers.
- Buffers are made through the device (bgfx on desktop) and locked to write.
- Desktop: loads and reloads run inline, textures are decoded in software, raw entries get a
  terminating zero, a refused read is refused instead of rebooting, and there is no edge AA swap.
