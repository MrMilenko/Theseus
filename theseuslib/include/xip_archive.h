// xip_archive.h: Xbox Information Package (XIP) archive format reader.
//
// XIP is the asset archive used by every dashboard XAP scene: a flat
// container of textures, meshes, audio clips, scripts and arbitrary blobs
// identified by name. Both the dashboard (which loads them during boot to
// populate the scene graph) and the desktop port (which either loads them
// directly or extracts them to disk for live editing) walk the same
// on-disk layout.
//
// On-disk layout, starting at the head of the file:
//
//   XIPHEADER     16 bytes  magic / data offset / counts
//   FILEDATA[N]   16 each   per-file: payload offset, size, type, mtime
//   FILENAME[M]    4 each   per-name: file index, name pool offset
//   names[]       cbNames   null-separated string pool, sorted for bsearch
//   data[]        dataSize  raw payload bytes, indexed via FILEDATA[i].offset
//
// This header exposes only the format: opening, walking, and reading raw
// payload bytes. Consumer-side concerns (D3D texture creation, vertex /
// index buffer construction, mesh-buffer caching, threaded loaders, the
// dashboard's skin-override hook, file-protection / tamper handling, the
// desktop port's extract-to-disk flow) all live in the consumers, layered
// on top of these calls.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// On-disk format
// =============================================================================

// "XIP0" little-endian. The first DWORD of every valid archive.
#define XIP_MAGIC  0x30504958u

// File type identifiers stored in FILEDATA::type.
#define XIP_TYPE_GENERIC         0  // arbitrary blob
#define XIP_TYPE_MESH            1  // obsolete (legacy mesh format)
#define XIP_TYPE_TEXTURE         2  // XPR-wrapped texture (.xbx payload)
#define XIP_TYPE_WAVE            3  // PCM/ADPCM audio data
#define XIP_TYPE_MESH_REFERENCE  4  // packed: high byte = mesh buffer index,
                                    //         low 24 bits = first index in IB
#define XIP_TYPE_INDEXBUFFER     5  // raw D3D index buffer
#define XIP_TYPE_VERTEXBUFFER    6  // raw D3D VB, prefixed with vertex count + FVF

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;       // must equal XIP_MAGIC
    uint32_t dataStart;   // byte offset where the data section begins
    uint16_t fileCount;   // number of FILEDATA entries
    uint16_t nameCount;   // number of FILENAME entries
    uint32_t dataSize;    // total size of all file data
} XipHeader;

typedef struct {
    uint32_t offset;      // byte offset within the data section
                          // (or packed mesh-ref payload for type=MESH_REFERENCE)
    uint32_t size;        // size in bytes
                          // (or primitive count for type=MESH_REFERENCE)
    uint32_t type;        // one of XIP_TYPE_*
    uint32_t timestamp;   // file modification time
} XipFileData;

typedef struct {
    uint16_t fileDataIndex;  // index into the FileData array
    uint16_t nameOffset;     // byte offset into the names string pool
} XipFileName;

#pragma pack(pop)

// =============================================================================
// Reader API
// =============================================================================

// Opaque archive handle. Constructed by XipOpen, freed by XipClose.
typedef struct XipArchive XipArchive;

// Open an archive at `xipPath`. On success returns a handle and the
// header / FileData / FileName / names tables are loaded into memory;
// the file handle stays open so subsequent XipReadFile calls can pull
// payload bytes on demand. Returns NULL if the file does not exist,
// is unreadable, or does not start with XIP_MAGIC.
XipArchive* XipOpen(const char* xipPath);

// Release everything: metadata tables, the open file handle, and the
// archive struct itself. Safe to call with a NULL handle.
void XipClose(XipArchive* arch);

// Drop just the file handle once the consumer is done reading payload
// bytes (typically after the consumer has converted every file to its
// in-memory form). Metadata stays loaded so name lookups still work.
// XipReadFile will fail after this is called. Safe to call repeatedly
// or on a NULL handle.
void XipReleaseFileHandle(XipArchive* arch);

// Direct access to the parsed header and tables. The pointers are
// owned by the archive and remain valid until XipClose.
const XipHeader*   XipGetHeader  (const XipArchive* arch);
const XipFileData* XipGetFileData(const XipArchive* arch, unsigned fileIdx);
const XipFileName* XipGetFileName(const XipArchive* arch, unsigned entryIdx);

// Convenience accessors for the most common metadata reads.
unsigned    XipGetFileCount(const XipArchive* arch);
unsigned    XipGetNameCount(const XipArchive* arch);
uint32_t    XipGetFileType (const XipArchive* arch, unsigned fileIdx);
uint32_t    XipGetFileSize (const XipArchive* arch, unsigned fileIdx);
uint32_t    XipGetFileOffset(const XipArchive* arch, unsigned fileIdx);

// Resolve an entry by index into its name and the file it points at.
// `entryIdx` is in the range [0, XipGetNameCount). Returns NULL or 0
// if entryIdx is out of range.
const char* XipGetEntryName     (const XipArchive* arch, unsigned entryIdx);
unsigned    XipGetEntryFileIndex(const XipArchive* arch, unsigned entryIdx);

// Look up a file by name (case-insensitive bsearch over the directory,
// matching the dashboard's _stricmp / desktop's strcasecmp behavior).
// Returns the file data index, or -1 on miss. The path-prefix
// stripping that CXipFile::Find does is the consumer's job; pass just
// the filename here.
int XipFindByName(const XipArchive* arch, const char* name);

// Read a file's raw payload bytes from the data section into the
// caller's buffer. `bufSize` must be at least XipGetFileSize(...).
// Returns true on success. Fails after XipReleaseFileHandle has been
// called or if fileIdx is out of range.
bool XipReadFile(XipArchive* arch, unsigned fileIdx, void* buf, unsigned bufSize);

#ifdef __cplusplus
}
#endif
