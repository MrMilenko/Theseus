// xboxfs.h: desktop drive-letter to host-path translation. Maps
// Xbox-style paths (Q:\Xips\default.xip) onto the local xboxfs/
// staging directory (xboxfs/Q/Xips/default.xip).

#pragma once
//   C:\...               -> ./xboxfs/C/...
//   E:\...               -> ./xboxfs/E/...
//   MUSIC:\...           -> ./xboxfs/MUSIC/...

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#ifndef _WIN32
#include <dirent.h>
#endif
#include <sys/stat.h>
#include "virtual_games.h"

// Base directory for the virtual Xbox filesystem
#ifndef XBOXFS_BASE
#define XBOXFS_BASE "xboxfs"
#endif

// Case-insensitive path resolution (Xbox FATX is case-insensitive)
// Walks each path component and matches against actual directory entries
inline bool XboxFS_ResolveCaseInsensitive(char* resolvedPath, size_t maxLen) {
    // Start from the first component; try the path as-is first.
    struct stat st;
    if (stat(resolvedPath, &st) == 0)
        return true;

    // Walk path components and match case-insensitively
    char working[512];
    working[0] = '\0';

    char* path = resolvedPath;
    char* components[32];
    int nComponents = 0;

    // Split path into components
    char tempPath[512];
    strncpy(tempPath, resolvedPath, sizeof(tempPath) - 1);
    tempPath[sizeof(tempPath) - 1] = '\0';

    char* tok = strtok(tempPath, "/");
    while (tok && nComponents < 32) {
        components[nComponents++] = tok;
        tok = strtok(NULL, "/");
    }

    // Rebuild path, matching each component case-insensitively
    char rebuilt[512];
    rebuilt[0] = '\0';

    for (int i = 0; i < nComponents; i++) {
        const char* wanted = components[i];

        // Try exact match first
        char candidate[512];
        if (rebuilt[0])
            snprintf(candidate, sizeof(candidate), "%s/%s", rebuilt, wanted);
        else
            snprintf(candidate, sizeof(candidate), "%s", wanted);

        if (stat(candidate, &st) == 0) {
            strcpy(rebuilt, candidate);
            continue;
        }

        // Scan directory for case-insensitive match
        const char* dirToScan = rebuilt[0] ? rebuilt : ".";
        bool found = false;
#ifdef _WIN32
        char searchBuf[512];
        snprintf(searchBuf, sizeof(searchBuf), "%s/*", dirToScan);
        struct _finddata_t fd;
        intptr_t hFind = _findfirst(searchBuf, &fd);
        if (hFind != -1) {
            do {
                if (_stricmp(fd.name, wanted) == 0) {
                    if (rebuilt[0])
                        snprintf(candidate, sizeof(candidate), "%s/%s", rebuilt, fd.name);
                    else
                        snprintf(candidate, sizeof(candidate), "%s", fd.name);
                    strcpy(rebuilt, candidate);
                    found = true;
                    break;
                }
            } while (_findnext(hFind, &fd) == 0);
            _findclose(hFind);
        }
#else
        DIR* dir = opendir(dirToScan);
        if (!dir)
            return false;

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcasecmp(entry->d_name, wanted) == 0) {
                if (rebuilt[0])
                    snprintf(candidate, sizeof(candidate), "%s/%s", rebuilt, entry->d_name);
                else
                    snprintf(candidate, sizeof(candidate), "%s", entry->d_name);
                strcpy(rebuilt, candidate);
                found = true;
                break;
            }
        }
        closedir(dir);
#endif

        if (!found)
            return false;
    }

    strncpy(resolvedPath, rebuilt, maxLen - 1);
    resolvedPath[maxLen - 1] = '\0';
    return true;
}

// Translate an Xbox-style path to a local path
// Returns a pointer to a static buffer (NOT thread-safe, but matches Xbox single-threaded model)
inline const char* XboxFS_TranslatePath(const char* xboxPath) {
    static char s_buf[512];

    if (!xboxPath || !*xboxPath) {
        s_buf[0] = '\0';
        return s_buf;
    }

    // Look for drive letter pattern: "X:\" or "X:/"  or "WORD:\"
    const char* colon = strchr(xboxPath, ':');
    if (colon && (colon[1] == '\\' || colon[1] == '/')) {
        // Extract drive name (everything before the colon)
        int driveLen = (int)(colon - xboxPath);
        if (driveLen > 0 && driveLen < 32) {
            char drive[32];
            memcpy(drive, xboxPath, driveLen);
            drive[driveLen] = '\0';

            // Build translated path: xboxfs/DRIVE/rest...
            const char* rest = colon + 2; // skip ":\" or ":/"
            snprintf(s_buf, sizeof(s_buf), "%s/%s/%s", XBOXFS_BASE, drive, rest);

            // Convert backslashes to forward slashes
            for (char* p = s_buf; *p; p++) {
                if (*p == '\\') *p = '/';
            }

            // Case-insensitive resolve (Xbox FATX doesn't care about case)
            XboxFS_ResolveCaseInsensitive(s_buf, sizeof(s_buf));

            // Virtual game icon redirect: if the translated file doesn't exist
            // and it looks like a game icon path, check the virtual games DB
            {
                struct stat _vst;
                if (stat(s_buf, &_vst) != 0) {
                    // Check if this is an icon.jpg request for a virtual game
                    const char* iconTail = strstr(s_buf, "/icon.jpg");
                    if (!iconTail) iconTail = strstr(s_buf, "/icon.png");
                    if (iconTail) {
                        // Extract folder name: xboxfs/{drive}/{cat}/{name}/icon.jpg
                        char tmpBuf[512];
                        strncpy(tmpBuf, s_buf, sizeof(tmpBuf) - 1);
                        tmpBuf[iconTail - s_buf] = 0; // cut at /icon.jpg
                        extern int VGames_MatchFolder(const char*);
                        int vgIdx = VGames_MatchFolder(tmpBuf);
                        if (vgIdx >= 0) {
                            extern const char* VGames_GetIconPath(int);
                            const char* realIcon = VGames_GetIconPath(vgIdx);
                            if (realIcon) {
                                strncpy(s_buf, realIcon, sizeof(s_buf) - 1);
                                s_buf[sizeof(s_buf) - 1] = 0;
                            }
                        }
                    }
                }
            }
            return s_buf;
        }
    }

    // No drive letter found - return path as-is (with backslash conversion)
    strncpy(s_buf, xboxPath, sizeof(s_buf) - 1);
    s_buf[sizeof(s_buf) - 1] = '\0';
    for (char* p = s_buf; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    return s_buf;
}

// ============================================================================
// Shared helpers used by both Windows and POSIX paths
// ============================================================================

// Fills `buf` with a minimal fake XBE for a virtual game. Returns bytes
// written, or 0 if `buf` is too small. Layout: 0x178-byte header followed
// by a 0x1EC-byte certificate carrying the title name (UTF-16) and title
// ID. The dashboard's title enumeration only reads these two structures.
inline size_t XboxFS_FillFakeXBEBytes(uint8_t* buf, size_t bufSize,
                                       const char* titleName,
                                       unsigned long titleID) {
    const size_t need = 0x178 + 0x1EC;
    if (bufSize < need) return 0;
    memset(buf, 0, need);

    // XBE magic
    buf[0] = 'X'; buf[1] = 'B'; buf[2] = 'E'; buf[3] = 'H';

    uint32_t base = 0x00010000;
    memcpy(buf + 0x104, &base, 4);

    uint32_t hdrSize = (uint32_t)need;
    memcpy(buf + 0x108, &hdrSize, 4);

    uint32_t certAddr = base + 0x178;
    memcpy(buf + 0x118, &certAddr, 4);

    uint8_t* cert = buf + 0x178;
    uint32_t certSize = 0x1EC;
    memcpy(cert + 0x00, &certSize, 4);
    memcpy(cert + 0x08, &titleID, 4);

    uint16_t* titleNameU = (uint16_t*)(cert + 0x0C);
    for (int i = 0; i < 40 && titleName[i]; i++)
        titleNameU[i] = (uint16_t)(unsigned char)titleName[i];

    return need;
}

// Windows-style wildcard match. '*' = any sequence, '?' = single char.
// Case-insensitive. Used to filter synthesized virtual-game names against
// the FindFirstFile pattern; real entries are matched by Win32 / readdir.
// directly. Recursion depth is bounded by the number of '*' tokens in the
// pattern, which is tiny in practice.
inline bool XboxFS_WildcardMatch(const char* pattern, const char* name) {
    while (*pattern && *name) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return true;
            while (*name) {
                if (XboxFS_WildcardMatch(pattern, name)) return true;
                name++;
            }
            return false;
        }
        char p = (char)tolower((unsigned char)*pattern);
        char n = (char)tolower((unsigned char)*name);
        if (*pattern != '?' && p != n) return false;
        pattern++;
        name++;
    }
    while (*pattern == '*') pattern++;
    return *pattern == 0 && *name == 0;
}

#ifdef _WIN32
// On Windows, translate Xbox paths then call real Win32 CreateFileA
// First, save a reference to the real function name
#undef CreateFile
#undef CreateFileA
// Declare the real Win32 function (already declared by windows.h, but undef removed the macro)
extern "C" __declspec(dllimport) HANDLE __stdcall CreateFileA(
    LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

inline void XboxFS_ToBackslash(char* buf, size_t bufSize, const char* src) {
    strncpy(buf, src, bufSize - 1);
    buf[bufSize - 1] = '\0';
    for (char* p = buf; *p; p++) { if (*p == '/') *p = '\\'; }
}

// Forward decl. Definition lower down, alongside the Find* wrappers.
inline HANDLE XboxFS_CreateFakeXBE_Win32(const char* titleName, unsigned long titleID);

inline HANDLE XboxFS_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                                  void* sa, DWORD disp, DWORD flags, HANDLE templ) {
    const char* translated = XboxFS_TranslatePath(name);
    // Convert forward slashes back to backslashes for Win32 APIs
    char winPath[512];
    XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
    // Strip Xbox flags that cause issues on desktop Windows
    // FILE_FLAG_NO_BUFFERING requires sector-aligned reads; FILE_FLAG_OVERLAPPED needs async I/O setup
    flags &= ~(FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED);
    HANDLE h = ::CreateFileA(winPath, access, share, (LPSECURITY_ATTRIBUTES)sa, disp, flags, templ);
    if (h != INVALID_HANDLE_VALUE) return h;

    // Real open failed; check if this is the default.xbe inside a virtual
    // game folder (matching the behavior the POSIX CreateFileA gives the
    // dashboard's title enumeration).
    char folderPath[512];
    strncpy(folderPath, translated, sizeof(folderPath) - 1);
    folderPath[sizeof(folderPath) - 1] = '\0';
    // Both forward slashes (translated) and backslashes (Windows-native)
    // can show up here; normalize to forward for VGames_MatchFolder, which
    // expects xboxfs/<drive>/<cat>/<name> form.
    for (char* p = folderPath; *p; p++) { if (*p == '\\') *p = '/'; }
    char* lastSl = strrchr(folderPath, '/');
    if (lastSl) {
        const char* fileName = lastSl + 1;
        *lastSl = '\0';
        if (_stricmp(fileName, "default.xbe") == 0) {
            int vgIdx = VGames_MatchFolder(folderPath);
            if (vgIdx >= 0) {
                unsigned long tid = strtoul(g_vgames.games[vgIdx].titleID, NULL, 16);
                HANDLE fakeXbe = XboxFS_CreateFakeXBE_Win32(g_vgames.games[vgIdx].name, tid);
                if (fakeXbe != INVALID_HANDLE_VALUE) {
                    fprintf(stderr, "[xboxfs]   Virtual XBE for '%s' (TID=%08lX)\n",
                            g_vgames.games[vgIdx].name, tid);
                    return fakeXbe;
                }
            }
        }
    }
    return INVALID_HANDLE_VALUE;
}
#define CreateFileA XboxFS_CreateFileA
#define CreateFile CreateFileA

// Windows wrappers for FindFirstFile, FindNextFile, FindClose, GetFileAttributes, etc.
// These translate Xbox paths (E:\Games\*) to xboxfs paths before calling real Win32 APIs.
#undef FindFirstFile
#undef FindFirstFileA
#undef FindNextFile
#undef FindNextFileA
#undef FindClose
#undef GetFileAttributes
#undef GetFileAttributesA
#undef GetFileAttributesEx
#undef GetFileAttributesExA
#undef RemoveDirectory
#undef RemoveDirectoryA

extern "C" __declspec(dllimport) HANDLE __stdcall FindFirstFileA(
    LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
extern "C" __declspec(dllimport) BOOL __stdcall FindNextFileA(
    HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
extern "C" __declspec(dllimport) BOOL __stdcall FindClose(HANDLE hFindFile);
extern "C" __declspec(dllimport) DWORD __stdcall GetFileAttributesA(LPCSTR lpFileName);
extern "C" __declspec(dllimport) BOOL __stdcall GetFileAttributesExA(
    LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);
extern "C" __declspec(dllimport) BOOL __stdcall RemoveDirectoryA(LPCSTR lpPathName);
extern "C" __declspec(dllimport) DWORD __stdcall GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
extern "C" __declspec(dllimport) UINT  __stdcall GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName);
extern "C" __declspec(dllimport) BOOL  __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
extern "C" __declspec(dllimport) DWORD __stdcall SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);

// Windows fake XBE for virtual games
// On POSIX we hand back a tmpfile() FILE* cast to HANDLE; on Windows real
// HANDLEs and FILE*s aren't interchangeable, so create a temp file on disk
// flagged DELETE_ON_CLOSE, write the synthesized XBE bytes into it, rewind,
// and hand the OS HANDLE to the caller. The OS deletes the backing file
// when the caller closes the handle.
inline HANDLE XboxFS_CreateFakeXBE_Win32(const char* titleName, unsigned long titleID) {
    char tempDir[260];
    char tempFile[260];
    DWORD n = ::GetTempPathA(260, tempDir);
    if (n == 0 || n > 260) return INVALID_HANDLE_VALUE;
    if (::GetTempFileNameA(tempDir, "xbe", 0, tempFile) == 0)
        return INVALID_HANDLE_VALUE;
    HANDLE h = ::CreateFileA(tempFile,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    uint8_t buf[0x178 + 0x1EC];
    size_t bytes = XboxFS_FillFakeXBEBytes(buf, sizeof(buf), titleName, titleID);
    if (bytes == 0) { ::FindClose(h); return INVALID_HANDLE_VALUE; }
    DWORD written = 0;
    ::WriteFile(h, buf, (DWORD)bytes, &written, NULL);
    ::SetFilePointer(h, 0, NULL, 0 /* FILE_BEGIN */);
    return h;
}

// Windows wrapped Find handle
// Carries the real Win32 find handle plus the virtual-game cursor for the
// directory we're enumerating. XboxFS_FindFirstFileA always returns one of
// these; XboxFS_FindNextFileA / XboxFS_FindClose detect ours via the magic
// field and fall through to real Win32 if it doesn't match (defensive --
// in practice every entry point goes through our macro overrides).
#define XBOXFS_FFH_MAGIC 0x46464858  /* 'XHFF' */
struct XboxFSFindHandle {
    DWORD magic;
    HANDLE realHandle;     // Win32 find handle, or INVALID_HANDLE_VALUE if no real dir
    char dirPath[512];     // translated dir path with backslashes (for shadow checks)
    char pattern[256];     // basename pattern from the original call
    int  vgIndices[VGAMES_MAX];
    int  vgCount;
    int  vgPos;
};

// Pull a virtual-game folder list for a translated path that looks like
// "<...>\xboxfs\<DRIVE>\<CATEGORY>". Sets ffh->vgIndices/vgCount/0; leaves
// vgCount = 0 if the path doesn't match the game-category shape or no
// games.ini entries point at this drive+category.
inline void XboxFS_PopulateVirtualGames(XboxFSFindHandle* ffh, const char* dirPath) {
    ffh->vgCount = 0;
    ffh->vgPos = 0;
    // Find "xboxfs" segment, then the next two components are drive + category.
    const char* xfs = strstr(dirPath, "xboxfs");
    if (!xfs) return;
    const char* p = xfs + 6;
    if (*p != '\\' && *p != '/') return;
    p++;
    char drive[4] = {};
    int di = 0;
    while (*p && *p != '\\' && *p != '/' && di < 3) drive[di++] = *p++;
    if (*p != '\\' && *p != '/') return;
    p++;
    char cat[32] = {};
    int ci = 0;
    while (*p && *p != '\\' && *p != '/' && ci < 31) cat[ci++] = *p++;
    // Only inject for known game categories.
    static const char* gameCats[] = { "Games", "Applications", "Apps", "Homebrew", "Emulators", "Dashboards" };
    for (int gc = 0; gc < 6; gc++) {
        if (_stricmp(cat, gameCats[gc]) == 0) {
            ffh->vgCount = VGames_GetForDirectory(drive, cat, ffh->vgIndices, VGAMES_MAX);
            return;
        }
    }
}

// Returns the next virtual-game entry that (a) matches the pattern and
// (b) doesn't have a real folder of the same name in dirPath. Fills `fd`
// and returns TRUE on success; FALSE when virtual entries are exhausted.
inline BOOL XboxFS_NextVirtualEntry(XboxFSFindHandle* ffh, LPWIN32_FIND_DATAA fd) {
    while (ffh->vgPos < ffh->vgCount) {
        int idx = ffh->vgIndices[ffh->vgPos++];
        if (idx < 0 || idx >= VGAMES_MAX) continue;
        VirtualGame& vg = g_vgames.games[idx];
        if (!vg.valid) continue;
        if (!XboxFS_WildcardMatch(ffh->pattern, vg.name)) continue;
        // Skip if a real folder with this name already exists alongside.
        char shadow[768];
        snprintf(shadow, sizeof(shadow), "%s\\%s", ffh->dirPath, vg.name);
        DWORD attrs = ::GetFileAttributesA(shadow);
        if (attrs != INVALID_FILE_ATTRIBUTES) continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, vg.name, sizeof(fd->cFileName) - 1);
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return TRUE;
    }
    return FALSE;
}

inline HANDLE XboxFS_FindFirstFileA(LPCSTR path, LPWIN32_FIND_DATAA fd) {
    if (!path || !fd) return INVALID_HANDLE_VALUE;

    // Translate Xbox drive paths (E:\Games\* -> xboxfs\E\Games\*)
    char winPath[512];
    if (strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
    } else {
        strncpy(winPath, path, sizeof(winPath) - 1);
        winPath[sizeof(winPath) - 1] = '\0';
    }

    // Allocate the wrapper unconditionally. Even a pure pattern-not-found
    // case may have virtual matches to inject.
    XboxFSFindHandle* ffh = (XboxFSFindHandle*)calloc(1, sizeof(XboxFSFindHandle));
    if (!ffh) return INVALID_HANDLE_VALUE;
    ffh->magic = XBOXFS_FFH_MAGIC;
    ffh->realHandle = INVALID_HANDLE_VALUE;

    // Split dir + pattern at the last backslash (Win32 paths are backslashed).
    const char* lastSep = strrchr(winPath, '\\');
    if (!lastSep) lastSep = strrchr(winPath, '/');
    if (lastSep) {
        size_t dirLen = (size_t)(lastSep - winPath);
        if (dirLen >= sizeof(ffh->dirPath)) dirLen = sizeof(ffh->dirPath) - 1;
        memcpy(ffh->dirPath, winPath, dirLen);
        ffh->dirPath[dirLen] = '\0';
        strncpy(ffh->pattern, lastSep + 1, sizeof(ffh->pattern) - 1);
    } else {
        strcpy(ffh->dirPath, ".");
        strncpy(ffh->pattern, winPath, sizeof(ffh->pattern) - 1);
    }

    // Win32 / POSIX FindFirstFile both treat "*.*" as "match everything".
    // Our wildcard matcher is strict (literal '.' required), so collapse
    // the convention here; otherwise virtual game names without a dot
    // (most of them) get filtered out during injection.
    if (strcmp(ffh->pattern, "*.*") == 0)
        strcpy(ffh->pattern, "*");

    // Populate virtual game list for this directory before any filesystem call.
    XboxFS_PopulateVirtualGames(ffh, ffh->dirPath);

    // Now query the real filesystem.
    ffh->realHandle = ::FindFirstFileA(winPath, fd);
    if (ffh->realHandle != INVALID_HANDLE_VALUE)
        return (HANDLE)ffh;

    // Real find failed; try virtual entries directly.
    if (XboxFS_NextVirtualEntry(ffh, fd))
        return (HANDLE)ffh;

    free(ffh);
    return INVALID_HANDLE_VALUE;
}

inline BOOL XboxFS_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) {
    if (h == INVALID_HANDLE_VALUE || !fd) return FALSE;
    XboxFSFindHandle* ffh = (XboxFSFindHandle*)h;
    if (ffh->magic != XBOXFS_FFH_MAGIC) {
        // Defensive; something passed a raw Win32 handle in.
        return ::FindNextFileA(h, fd);
    }
    // Exhaust real entries first.
    if (ffh->realHandle != INVALID_HANDLE_VALUE) {
        if (::FindNextFileA(ffh->realHandle, fd))
            return TRUE;
        // Real entries done; close the real handle so subsequent calls
        // fall through to virtual entries directly.
        ::FindClose(ffh->realHandle);
        ffh->realHandle = INVALID_HANDLE_VALUE;
    }
    return XboxFS_NextVirtualEntry(ffh, fd);
}

inline BOOL XboxFS_FindClose(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    XboxFSFindHandle* ffh = (XboxFSFindHandle*)h;
    if (ffh->magic != XBOXFS_FFH_MAGIC) {
        return ::FindClose(h);
    }
    BOOL ok = TRUE;
    if (ffh->realHandle != INVALID_HANDLE_VALUE)
        ok = ::FindClose(ffh->realHandle);
    free(ffh);
    return ok;
}

inline DWORD XboxFS_GetFileAttributesA(LPCSTR path) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        char winPath[512];
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
        return ::GetFileAttributesA(winPath);
    }
    return ::GetFileAttributesA(path);
}

inline BOOL XboxFS_GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        char winPath[512];
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
        return ::GetFileAttributesExA(winPath, level, info);
    }
    return ::GetFileAttributesExA(path, level, info);
}

inline BOOL XboxFS_RemoveDirectoryA(LPCSTR path) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        char winPath[512];
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
        return ::RemoveDirectoryA(winPath);
    }
    return ::RemoveDirectoryA(path);
}

#define FindFirstFileA XboxFS_FindFirstFileA
#define FindFirstFile FindFirstFileA
#define FindNextFileA XboxFS_FindNextFileA
#define FindNextFile FindNextFileA
#define FindClose XboxFS_FindClose
#define GetFileAttributesA XboxFS_GetFileAttributesA
#define GetFileAttributes GetFileAttributesA
#define GetFileAttributesExA XboxFS_GetFileAttributesExA
#define GetFileAttributesEx GetFileAttributesExA
#define RemoveDirectoryA XboxFS_RemoveDirectoryA
#define RemoveDirectory RemoveDirectoryA
#else
// On non-Windows, use fopen-based implementation (HANDLE = FILE*)
#undef CreateFileA

// Create an in-memory FILE* containing a minimal fake XBE header for virtual games.
// Wraps the shared XboxFS_FillFakeXBEBytes helper in a tmpfile().
inline FILE* XboxFS_CreateFakeXBE(const char* titleName, unsigned long titleID) {
    FILE* f = tmpfile();
    if (!f) return NULL;
    uint8_t buf[0x178 + 0x1EC];
    size_t n = XboxFS_FillFakeXBEBytes(buf, sizeof(buf), titleName, titleID);
    if (n == 0) { fclose(f); return NULL; }
    fwrite(buf, 1, n, f);
    fseek(f, 0, SEEK_SET);
    return f;
}

// Try to load a file from mounted qcow2 FATX when xboxfs host file is missing.
// Parses Xbox path like "E:\UDATA\4d530004\TitleImage.xbx", walks the FATX
// directory tree, reads file data, writes to tmpfile() and returns it.
inline FILE* XboxFS_TryFATXFallback(const char* xboxPath) {
    // Only handle drive-letter paths
    if (!xboxPath || strlen(xboxPath) < 3 || xboxPath[1] != ':') return nullptr;

    // We need xbox_hdd.h types - forward declare what we need
    extern bool s_xboxHDDTried;
    class XboxHDD;
    class FATXReader;
    struct FATXDirEntry;

    // Access the global HDD instance from desktop_nodes.cpp
    // This is a bit hacky but avoids circular includes
    FILE* XboxFS_FATXReadFile(const char* xboxPath);
    return XboxFS_FATXReadFile(xboxPath);
}

inline HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share,
                          void* sa, DWORD disp, DWORD flags, HANDLE templ) {
    // xboxfs host filesystem is the primary source for UIX Desktop data
    const char* translated = XboxFS_TranslatePath(name);
    const char* mode = (access & 0x40000000) ? "wb" : "rb"; // GENERIC_WRITE : GENERIC_READ
    FILE* f = fopen(translated, mode);
    if (!f) {
        // Check if this is a virtual game file (e.g. default.xbe inside a virtual folder)
        char folderPath[512];
        strncpy(folderPath, translated, sizeof(folderPath) - 1);
        folderPath[sizeof(folderPath) - 1] = 0;
        char* lastSl = strrchr(folderPath, '/');
        if (lastSl) {
            const char* fileName = lastSl + 1;
            *lastSl = 0;
            extern int VGames_MatchFolder(const char*);
            int vgIdx = VGames_MatchFolder(folderPath);
            if (vgIdx >= 0) {
                if (strcasecmp(fileName, "default.xbe") == 0) {
                    extern VirtualGameDB g_vgames;
                    unsigned long tid = strtoul(g_vgames.games[vgIdx].titleID, NULL, 16);
                    FILE* fakeXbe = XboxFS_CreateFakeXBE(g_vgames.games[vgIdx].name, tid);
                    if (fakeXbe) {
                        fprintf(stderr, "[xboxfs]   Virtual XBE for '%s' (TID=%08lX)\n",
                                g_vgames.games[vgIdx].name, tid);
                        return (HANDLE)fakeXbe;
                    }
                }
            }
        }
        // Fallback: try qcow2 FATX but ONLY for save data paths (UDATA/TDATA)
        // Don't interfere with xboxfs/VGames for game icons, configs, etc.
        extern char g_qcowPath[512];
        if (g_qcowPath[0] && name && (strstr(name, "UDATA") || strstr(name, "TDATA"))) {
            f = XboxFS_TryFATXFallback(name);
            if (f) fprintf(stderr, "[xboxfs]   Loaded from qcow2 FATX\n");
        }
        // silently fail - not all lookups are expected to succeed
    }
    return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}

// Map CreateFile to CreateFileA (ANSI mode, matching TCHAR=char)
#undef CreateFile
#define CreateFile CreateFileA
#endif

// Override fopen/_tfopen calls that use Xbox paths
// settingsfile.cpp, runner.cpp, image.cpp all use fopen with raw Xbox paths like "C:\UIX Configs\cache.ini"
// Without this, macOS creates files with literal backslashes in their names
inline FILE* XboxFS_fopen(const char* path, const char* mode) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        // Ensure parent directories exist for write modes
        if (mode[0] == 'w' || mode[0] == 'a') {
            char dirBuf[512];
            strncpy(dirBuf, translated, sizeof(dirBuf) - 1);
            dirBuf[sizeof(dirBuf) - 1] = '\0';
            char* lastSlash = strrchr(dirBuf, '/');
            if (lastSlash) {
                *lastSlash = '\0';
                // Recursively create parent dirs
                for (char* p = dirBuf; *p; p++) {
                    if (*p == '/') {
                        *p = '\0';
                        #ifdef _WIN32
                        _mkdir(dirBuf);
#else
                        mkdir(dirBuf, 0755);
#endif
                        *p = '/';
                    }
                }
                #ifdef _WIN32
                        _mkdir(dirBuf);
#else
                        mkdir(dirBuf, 0755);
#endif
            }
        }
        return fopen(translated, mode);
    }
    return fopen(path, mode);
}
#define fopen XboxFS_fopen
#define _tfopen XboxFS_fopen
