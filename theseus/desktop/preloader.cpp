// preloader.cpp: startup mode and XIP extraction. Development mode unpacks
// the bundled XIP archives into folders the first time. Desktop-only.

#include "display.h"
#include "app_paths.h"
#include "preloader.h"
#include "std.h"
#include <SDL.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "host.h"


// ============================================================================
// XIP Extraction (ported from xiptool.c)
// ============================================================================

#pragma pack(push, 1)
#define XTOOL_XIP_MAGIC 0x30504958  /* "XIP0" */

typedef struct { DWORD magic; DWORD dataStart; WORD fileCount; WORD nameCount; DWORD dataSize; } XTOOL_XIPHEADER;
typedef struct { DWORD offset; DWORD size; DWORD type; DWORD timestamp; } XTOOL_FILEDATA;
typedef struct { WORD fileDataIndex; WORD nameOffset; } XTOOL_FILENAME;
typedef struct { DWORD primitiveType; DWORD faceCount; DWORD fvf; DWORD vertexStride; DWORD vertexCount; DWORD indexCount; } XTOOL_MESHFILEHEADER;

#pragma pack(pop)

#define XTOOL_TYPE_MESH_REFERENCE 4
#define XTOOL_TYPE_INDEXBUFFER    5
#define XTOOL_TYPE_VERTEXBUFFER   6
#define XTOOL_MAX_MESHBUF 10

static void Preloader_Mkdirp(const char* path) {
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            Host_MakeDir(tmp);
            *p = '/';
        }
    }
    Host_MakeDir(tmp);
}

static const char* Preloader_FindName(WORD idx, XTOOL_FILENAME* filenames, int nameCount,
                                       const char* names, long namesSize) {
    for (int i = 0; i < nameCount; i++) {
        if (filenames[i].fileDataIndex == idx && filenames[i].nameOffset < namesSize)
            return &names[filenames[i].nameOffset];
    }
    return NULL;
}

static bool Preloader_EndsWith(const char* str, const char* suffix) {
    size_t slen = strlen(str), xlen = strlen(suffix);
    if (xlen > slen) return false;
    return strcasecmp(str + slen - xlen, suffix) == 0;
}

// Each mesh reference becomes one standalone .xm. We slice the shared IB by
// [first_index, first_index + prim_count*3), find the contiguous vertex range
// it touches, copy those vertices, and rebase the indices to start at zero.
// Mirrors the meshref-keyed extraction in tools/xiptool.c.
int Preloader_ExtractXIP(const char* xipPath, const char* outDir) {
    FILE* f = fopen(xipPath, "rb");
    if (!f) return -1;

    XTOOL_XIPHEADER hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != XTOOL_XIP_MAGIC) {
        fclose(f); return -1;
    }

    XTOOL_FILEDATA* filedata = (XTOOL_FILEDATA*)calloc(hdr.fileCount, sizeof(XTOOL_FILEDATA));
    fread(filedata, sizeof(XTOOL_FILEDATA), hdr.fileCount, f);

    XTOOL_FILENAME* filenames = (XTOOL_FILENAME*)calloc(hdr.nameCount, sizeof(XTOOL_FILENAME));
    fread(filenames, sizeof(XTOOL_FILENAME), hdr.nameCount, f);

    long namesStart = (long)ftell(f);
    long namesSize = (long)hdr.dataStart - namesStart;
    if (namesSize < 0) namesSize = 0;
    char* names = (char*)calloc(1, namesSize + 1);
    if (namesSize > 0) fread(names, 1, namesSize, f);

    Preloader_Mkdirp(outDir);

    struct LoadedBuf {
        BYTE* ib;
        DWORD ibSize;
        BYTE* vb;
        DWORD vbSize;
        DWORD vertexCount;
        DWORD fvf;
        DWORD vertexStride;
    };
    LoadedBuf bufs[XTOOL_MAX_MESHBUF];
    memset(bufs, 0, sizeof(bufs));
    int nIBs = 0, nVBs = 0;

    for (int i = 0; i < hdr.fileCount; i++) {
        long dataPos = (long)hdr.dataStart + (long)filedata[i].offset;
        if (filedata[i].type == XTOOL_TYPE_INDEXBUFFER && nIBs < XTOOL_MAX_MESHBUF) {
            fseek(f, dataPos, SEEK_SET);
            bufs[nIBs].ib = (BYTE*)malloc(filedata[i].size);
            bufs[nIBs].ibSize = filedata[i].size;
            fread(bufs[nIBs].ib, 1, filedata[i].size, f);
            nIBs++;
        } else if (filedata[i].type == XTOOL_TYPE_VERTEXBUFFER && nVBs < XTOOL_MAX_MESHBUF) {
            fseek(f, dataPos, SEEK_SET);
            bufs[nVBs].vb = (BYTE*)malloc(filedata[i].size);
            bufs[nVBs].vbSize = filedata[i].size;
            fread(bufs[nVBs].vb, 1, filedata[i].size, f);
            if (filedata[i].size >= 8) {
                memcpy(&bufs[nVBs].vertexCount, bufs[nVBs].vb,     sizeof(DWORD));
                memcpy(&bufs[nVBs].fvf,         bufs[nVBs].vb + 4, sizeof(DWORD));
                if (bufs[nVBs].vertexCount > 0)
                    bufs[nVBs].vertexStride = (filedata[i].size - 8) / bufs[nVBs].vertexCount;
            }
            nVBs++;
        }
    }

    int nBufs = (nIBs < nVBs) ? nIBs : nVBs;

    int xmCount = 0;
    for (int i = 0; i < hdr.fileCount; i++) {
        if (filedata[i].type != XTOOL_TYPE_MESH_REFERENCE) continue;

        DWORD bufIdx   = filedata[i].offset >> 24;
        DWORD firstIdx = filedata[i].offset & 0x00FFFFFF;
        DWORD primCount = filedata[i].size;
        DWORD indexCount = primCount * 3;

        const char* name = Preloader_FindName((WORD)i, filenames, hdr.nameCount, names, namesSize);
        if (!name) continue;
        if ((int)bufIdx >= nBufs) continue;

        LoadedBuf* mb = &bufs[bufIdx];
        if (mb->vertexStride == 0 || mb->ibSize == 0) continue;

        DWORD totalIndices = mb->ibSize / sizeof(WORD);
        if (firstIdx + indexCount > totalIndices) continue;

        WORD* sliceIndices = ((WORD*)mb->ib) + firstIdx;

        WORD minIdx = 0xFFFF, maxIdx = 0;
        if (indexCount == 0) { minIdx = 0; maxIdx = 0; }
        else {
            for (DWORD k = 0; k < indexCount; k++) {
                WORD v = sliceIndices[k];
                if (v < minIdx) minIdx = v;
                if (v > maxIdx) maxIdx = v;
            }
        }
        if (maxIdx >= mb->vertexCount) continue;

        DWORD vertCount = (DWORD)(maxIdx - minIdx + 1);

        char xmPath[1024];
        snprintf(xmPath, sizeof(xmPath), "%s/%s", outDir, name);

        FILE* xmf = fopen(xmPath, "wb");
        if (!xmf) continue;

        XTOOL_MESHFILEHEADER mfh = { 4, primCount, mb->fvf, mb->vertexStride, vertCount, indexCount };
        fwrite(&mfh, sizeof(mfh), 1, xmf);

        BYTE* vertexData = mb->vb + 8 + (DWORD)minIdx * mb->vertexStride;
        fwrite(vertexData, 1, vertCount * mb->vertexStride, xmf);

        if (minIdx == 0) {
            fwrite(sliceIndices, sizeof(WORD), indexCount, xmf);
        } else {
            WORD* remapped = (WORD*)malloc(indexCount * sizeof(WORD));
            for (DWORD k = 0; k < indexCount; k++)
                remapped[k] = (WORD)(sliceIndices[k] - minIdx);
            fwrite(remapped, sizeof(WORD), indexCount, xmf);
            free(remapped);
        }

        fclose(xmf);
        xmCount++;
    }

    int extracted = 0;
    for (int i = 0; i < hdr.nameCount; i++) {
        WORD idx = filenames[i].fileDataIndex;
        if (idx >= hdr.fileCount) continue;
        XTOOL_FILEDATA* fd = &filedata[idx];
        if (fd->type == XTOOL_TYPE_MESH_REFERENCE || fd->type == XTOOL_TYPE_INDEXBUFFER || fd->type == XTOOL_TYPE_VERTEXBUFFER)
            continue;
        const char* name = (filenames[i].nameOffset < namesSize) ? &names[filenames[i].nameOffset] : "unknown";
        if (name[0] == '~') continue;
        fseek(f, (long)hdr.dataStart + (long)fd->offset, SEEK_SET);
        BYTE* data = (BYTE*)malloc(fd->size);
        fread(data, 1, fd->size, f);
        char outPath[1024];
        snprintf(outPath, sizeof(outPath), "%s/%s", outDir, name);
        FILE* out = fopen(outPath, "wb");
        if (out) { fwrite(data, 1, fd->size, out); fclose(out); extracted++; }
        free(data);
    }

    for (int b = 0; b < XTOOL_MAX_MESHBUF; b++) { free(bufs[b].ib); free(bufs[b].vb); }
    free(names); free(filenames); free(filedata);
    fclose(f);
    return extracted + xmCount;
}

static int Preloader_ExtractAllXIPs(const char* xipsDir, char statusBuf[], int statusBufSize) {
    int totalXips = 0, count = 0;

    char name[260];
    HostDir* d = Host_OpenDir(xipsDir);
    if (!d) { snprintf(statusBuf, statusBufSize, "Cannot open: %s", xipsDir); return -1; }
    while (Host_ReadDir(d, name, sizeof(name), NULL))
        if (Preloader_EndsWith(name, ".xip")) totalXips++;
    Host_CloseDir(d);

    d = Host_OpenDir(xipsDir);
    while (d && Host_ReadDir(d, name, sizeof(name), NULL)) {
        if (!Preloader_EndsWith(name, ".xip")) continue;

        char xipPath[1024], outDir[1024];
        snprintf(xipPath, sizeof(xipPath), "%s%c%s", xipsDir, Host_PathSep, name);
        snprintf(outDir, sizeof(outDir), "%s%c%s", xipsDir, Host_PathSep, name);
        char* dot = strrchr(outDir + strlen(xipsDir) + 1, '.');
        if (dot) *dot = '\0';

        snprintf(statusBuf, statusBufSize, "Extracting %s... (%d/%d)", name, count + 1, totalXips);
        fprintf(stdout, "[Extract] %s\n", name);
        Preloader_ExtractXIP(xipPath, outDir);
        count++;
    }
    Host_CloseDir(d);

    snprintf(statusBuf, statusBufSize, "Extracted %d XIP file(s)", count);
    return count;
}

// Check if extracted XIPs already exist (look for default/default.xap)
static bool Preloader_HasExtractedXIPs() {
    struct stat st;
    return (stat(AppPath("Data/Xips/default/default.xap"), &st) == 0 && S_ISREG(st.st_mode));
}

// StartupMode is stored in desktop.ini via g_startupMode (loaded by sdl_main.cpp)
extern int g_startupMode;

// Development mode runs the scenes from unpacked folders; unpack them the
// first time it's picked.
bool Preloader_EnsureExtracted() {
    if (Preloader_HasExtractedXIPs()) return true;
    char status[256] = "";
    Preloader_ExtractAllXIPs(AppPath("Data/Xips"), status, sizeof(status));
    fprintf(stderr, "[Preloader] %s\n", status);
    return Preloader_HasExtractedXIPs();
}

// The mode question itself is asked by the guide once the dashboard is up.
bool RunPreloader(SDL_Window*) {
    if (g_startupMode != 2) return false;
    Preloader_EnsureExtracted();
    return true;
}
