# XIP

XIP ("XIP0") is the archive format used by the Xbox Dashboard to bundle scene scripts (.xap), textures (.xbx), meshes (.xm), and other assets into a single file. The dashboard loads XIPs at startup and on skin changes, caching mesh data in GPU-accessible memory. Backed by `xip_archive.cpp` and `xip_archive.h` in the active source tree.

## File Format

Derived from hex analysis of .xip files.

### Header (16 bytes)

```
Offset  Size  Field
0x00    4     Magic ("XIP0" = 0x30504958)
0x04    4     DataStart (byte offset where file data begins)
0x08    2     FileCount (number of FILEDATA entries)
0x0A    2     NameCount (number of FILENAME entries)
0x0C    4     DataSize (total size of all file data)
```

### FILEDATA entries (16 bytes each)

```
Offset  Size  Field
0x00    4     DataOffset (from DataStart, or packed mesh ID for type 4)
0x04    4     Size (byte count, or primitive count for type 4)
0x08    4     Type (0=generic, 1=mesh[obsolete], 2=texture, 3=wave, 4=meshref, 5=IB, 6=VB)
0x0C    4     Timestamp
```

### FILENAME entries (4 bytes each)

```
Offset  Size  Field
0x00    2     FileDataIndex (index into FILEDATA array)
0x02    2     NameOffset (offset into name string pool)
```

### Layout

```
[XIPHEADER][FILEDATA * FileCount][FILENAME * NameCount][name strings][file data...]
```

Directory is sorted by name for binary search (bsearch with _stricmp).

## Binary Analysis (4920-5960 retail XBE)

### Globals

- `c_nXipFileCount` at `0x0001c028`: count of loaded archives
- `c_rgXipFile[20]`: static pool of CXipFile instances

### CMeshRef vtable

- `___7CMeshRef__6B_` at `0x000249fc`

### File Protection

Retail binary calls `HalReturnToFirmware(HalFatalErrorRebootRoutine)` on XIP read failure. Debug builds issue an ALERT and `int 3` breakpoint.

### Texture Loading (XPR format)

Textures inside XIPs are wrapped in XPR containers (Xbox Packed Resource). The loader checks the XPR magic, allocates GPU-contiguous memory via `TheseusD3D_AllocContiguousMemory`, copies texture data, and registers it with D3D.

### Mesh Buffer Layout

Vertex buffers are prefixed with an 8-byte header: `[int vertexCount][DWORD fvf]`, followed by raw vertex data. Index buffers are raw WORD arrays.

Mesh references pack the buffer index and first-index into a single DWORD:

```
DataOffset = (bufferIndex << 24) | firstIndex
```

### Skin Override

`FindObjectInXIP` checks the skin directory for loose texture files before searching loaded archives. This allows skins to override individual textures without repacking the XIP.

### Cache Eviction

`CleanupMeshCache` finds the oldest loaded XIP (by `m_cacheTime`) that has mesh buffers allocated and frees them. Mesh buffers are lazily reloaded on next render via `CMeshRef::Render`.
