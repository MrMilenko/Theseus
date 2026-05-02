// xip_archive.c: XIP archive parser implementation.
//
// Reads the format described in xip_archive.h: loads the header,
// FILEDATA / FILENAME / names tables at open time, then services payload
// reads on demand from the open file handle. Win32 file API throughout
// (CreateFileA / ReadFile / SetFilePointer / CloseHandle) so the same
// code path runs on Xbox via xtl.h, on Windows desktop via the real Win32
// headers, and on the SDL desktop targets via the sdl_platform.h shim.
//
// Strictly format-only: no D3D, no scene graph, no skin override, no
// buffered chunked reader, no threading, no caching. Consumers layer
// those concerns on top.

#include <xtl.h>

// Angle-bracket form so the include path search skips -iquote (which
// the cross-compile Makefile points at theseus/, where there is a
// C++ xip_archive.h with the dashboard's CXipFile class) and finds
// the lib's own header via -I$(THESEUSLIB_DIR)/include.
#include <xip_archive.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// Internal archive layout
// =============================================================================

// XipArchive owns the parsed metadata tables and the open file handle.
// Pointer ownership is straightforward: every malloc'd block is freed
// in XipClose, the file handle is closed in XipClose or
// XipReleaseFileHandle, and there are no shared references.
struct XipArchive {
    HANDLE       hFile;       // file handle, INVALID_HANDLE_VALUE after release
    XipHeader    header;      // copy of the on-disk header
    XipFileData* fileData;    // header.fileCount entries
    XipFileName* fileName;    // header.nameCount entries
    char*        names;       // raw name string pool
    uint32_t     namesSize;   // size of the name string pool in bytes
};

// =============================================================================
// Win32 read helpers
// =============================================================================

// Read `size` bytes from the current file position into `buf`.
// Returns true only if the full request was satisfied.
static BOOL XipReadExact(HANDLE h, void* buf, DWORD size)
{
    DWORD bytesRead = 0;
    if (!ReadFile(h, buf, size, &bytesRead, NULL)) return FALSE;
    return bytesRead == size;
}

// Seek to a 32-bit byte offset from the start of the file. XIPs are
// small (a few MB at most) so the high dword is always zero, but we
// still go through SetFilePointer for consistency with the Xbox CRT
// (which has no fseek with 64-bit offsets).
static BOOL XipSeek(HANDLE h, DWORD offset)
{
    DWORD result = SetFilePointer(h, (LONG)offset, NULL, FILE_BEGIN);
    return result != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR;
}

// =============================================================================
// XipOpen / XipClose
// =============================================================================

XipArchive* XipOpen(const char* xipPath)
{
    if (!xipPath) return NULL;

    HANDLE hFile = CreateFileA(xipPath,
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    XipArchive* arch = (XipArchive*)calloc(1, sizeof(XipArchive));
    if (!arch) {
        CloseHandle(hFile);
        return NULL;
    }
    arch->hFile = hFile;

    // Header.
    if (!XipReadExact(hFile, &arch->header, sizeof(XipHeader)) ||
        arch->header.magic != XIP_MAGIC)
    {
        XipClose(arch);
        return NULL;
    }

    // FileData table.
    DWORD cbFileData = (DWORD)arch->header.fileCount * (DWORD)sizeof(XipFileData);
    arch->fileData = (XipFileData*)malloc(cbFileData);
    if (!arch->fileData || !XipReadExact(hFile, arch->fileData, cbFileData)) {
        XipClose(arch);
        return NULL;
    }

    // FileName table.
    DWORD cbFileName = (DWORD)arch->header.nameCount * (DWORD)sizeof(XipFileName);
    arch->fileName = (XipFileName*)malloc(cbFileName);
    if (!arch->fileName || !XipReadExact(hFile, arch->fileName, cbFileName)) {
        XipClose(arch);
        return NULL;
    }

    // Name string pool. Its size is whatever space is left between the
    // end of the FileName table and the start of the data section.
    DWORD metadataEnd = (DWORD)sizeof(XipHeader) + cbFileData + cbFileName;
    if (arch->header.dataStart < metadataEnd) {
        XipClose(arch);
        return NULL;
    }
    arch->namesSize = arch->header.dataStart - metadataEnd;
    arch->names = (char*)malloc(arch->namesSize > 0 ? arch->namesSize : 1);
    if (!arch->names) {
        XipClose(arch);
        return NULL;
    }
    if (arch->namesSize > 0 &&
        !XipReadExact(hFile, arch->names, arch->namesSize))
    {
        XipClose(arch);
        return NULL;
    }

    return arch;
}

void XipReleaseFileHandle(XipArchive* arch)
{
    if (!arch) return;
    if (arch->hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(arch->hFile);
        arch->hFile = INVALID_HANDLE_VALUE;
    }
}

void XipClose(XipArchive* arch)
{
    if (!arch) return;
    XipReleaseFileHandle(arch);
    free(arch->fileData);
    free(arch->fileName);
    free(arch->names);
    free(arch);
}

// =============================================================================
// Metadata accessors
// =============================================================================

const XipHeader* XipGetHeader(const XipArchive* arch)
{
    return arch ? &arch->header : NULL;
}

const XipFileData* XipGetFileData(const XipArchive* arch, unsigned fileIdx)
{
    if (!arch || fileIdx >= arch->header.fileCount) return NULL;
    return &arch->fileData[fileIdx];
}

const XipFileName* XipGetFileName(const XipArchive* arch, unsigned entryIdx)
{
    if (!arch || entryIdx >= arch->header.nameCount) return NULL;
    return &arch->fileName[entryIdx];
}

unsigned XipGetFileCount(const XipArchive* arch)
{
    return arch ? arch->header.fileCount : 0;
}

unsigned XipGetNameCount(const XipArchive* arch)
{
    return arch ? arch->header.nameCount : 0;
}

uint32_t XipGetFileType(const XipArchive* arch, unsigned fileIdx)
{
    const XipFileData* fd = XipGetFileData(arch, fileIdx);
    return fd ? fd->type : 0;
}

uint32_t XipGetFileSize(const XipArchive* arch, unsigned fileIdx)
{
    const XipFileData* fd = XipGetFileData(arch, fileIdx);
    return fd ? fd->size : 0;
}

uint32_t XipGetFileOffset(const XipArchive* arch, unsigned fileIdx)
{
    const XipFileData* fd = XipGetFileData(arch, fileIdx);
    return fd ? fd->offset : 0;
}

const char* XipGetEntryName(const XipArchive* arch, unsigned entryIdx)
{
    const XipFileName* fn = XipGetFileName(arch, entryIdx);
    if (!fn || fn->nameOffset >= arch->namesSize) return NULL;
    return arch->names + fn->nameOffset;
}

unsigned XipGetEntryFileIndex(const XipArchive* arch, unsigned entryIdx)
{
    const XipFileName* fn = XipGetFileName(arch, entryIdx);
    return fn ? fn->fileDataIndex : 0;
}

// =============================================================================
// Name lookup
// =============================================================================

// bsearch comparator: looks the search key up against a directory
// entry by following its nameOffset into the names pool. Case
// insensitive to match the dashboard's _stricmp behavior.
typedef struct {
    const char* names;
    const char* find;
} XipFindCtx;

// __cdecl is required because the lib is compiled with
// -fdefault-calling-conv=stdcall but the CRT's bsearch expects its
// comparator to be cdecl, matching the public stdlib.h prototype.
static int __cdecl XipFindCompare(const void* key, const void* elem)
{
    const XipFindCtx* ctx = (const XipFindCtx*)key;
    const XipFileName* entry = (const XipFileName*)elem;
    const char* a = ctx->find;
    const char* b = ctx->names + entry->nameOffset;

    // Local case-insensitive compare so we don't have to depend on
    // _stricmp / strcasecmp being available on every target. The XIP
    // tooling builds names from ASCII filenames so a tolower walk
    // matches the on-disk sort order exactly.
    for (;;) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        int la = tolower(ca);
        int lb = tolower(cb);
        if (la != lb) return la - lb;
        if (la == 0) return 0;
    }
}

int XipFindByName(const XipArchive* arch, const char* name)
{
    if (!arch || !name || arch->header.nameCount == 0) return -1;

    XipFindCtx ctx;
    ctx.names = arch->names;
    ctx.find  = name;

    XipFileName* hit = (XipFileName*)bsearch(&ctx,
                                              arch->fileName,
                                              arch->header.nameCount,
                                              sizeof(XipFileName),
                                              XipFindCompare);
    return hit ? (int)hit->fileDataIndex : -1;
}

// =============================================================================
// Payload reads
// =============================================================================

bool XipReadFile(XipArchive* arch, unsigned fileIdx, void* buf, unsigned bufSize)
{
    if (!arch || !buf) return false;
    if (arch->hFile == INVALID_HANDLE_VALUE) return false;
    if (fileIdx >= arch->header.fileCount) return false;

    const XipFileData* fd = &arch->fileData[fileIdx];
    if (bufSize < fd->size) return false;

    DWORD absOffset = arch->header.dataStart + fd->offset;
    if (!XipSeek(arch->hFile, absOffset)) return false;

    return XipReadExact(arch->hFile, buf, fd->size) ? true : false;
}
