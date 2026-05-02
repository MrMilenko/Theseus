// platform_shim.h: desktop platform abstraction layer. Replaces Xbox
// XDK and D3D8 types with SDL + cross-platform equivalents (HANDLE,
// HRESULT, FILETIME, the WINAPI calling convention, kernel stubs).
// Pulled in early by std.h so the rest of the dashboard can keep
// using Win32 / Xbox idioms unmodified.

#pragma once

#define _WINDOWS 1       // Triggers #ifndef _WINDOWS guards on Xbox headers
#define UIX_DESKTOP 1
#define _THESEUS_STD_H 1 // Prevent toolbox xboxinternals.h kernel type redefs

#include <SDL.h>
#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cassert>
#include <ctime>
#include <cwchar>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define access _access
#ifndef F_OK
#define F_OK 0
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif
#endif
#include <cctype>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>
#include <wchar.h>
#include <ctype.h>
#endif

// -------------------------------------------------------
// Windows base types
// -------------------------------------------------------
#ifdef _WIN32
// On actual Windows, include <windows.h> for real Win32 API
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// D3D types not provided by windows.h
typedef DWORD     D3DCOLOR;
typedef float     D3DVALUE;
#else
// Non-Windows: define Windows types ourselves
typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef void*     LPSECURITY_ATTRIBUTES;
typedef int32_t   LONG;
typedef uint32_t  ULONG;
typedef int32_t   INT;
typedef uint32_t  UINT;
typedef int16_t   SHORT;
typedef uint16_t  USHORT;
typedef uint8_t   UCHAR;
typedef int       BOOL;
typedef float     FLOAT;
typedef void*     HANDLE;
typedef void*     LPVOID;
typedef const char* LPCSTR;
typedef const char* PCSTR;
typedef char*     LPSTR;
typedef const wchar_t* LPCWSTR;
typedef wchar_t*  LPWSTR;
typedef long      HRESULT;
typedef DWORD     D3DCOLOR;
typedef float     D3DVALUE;
typedef DWORD*    LPDWORD;
typedef char      CHAR;
typedef void*     PVOID;
typedef intptr_t  WPARAM;
typedef intptr_t  LPARAM;
typedef intptr_t  LRESULT;

// Window handle stubs (not used in SDL build but needed for _WINDOWS code paths)
typedef void*     HWND;
typedef void*     HINSTANCE;
typedef wchar_t   WCHAR;
#define VOID void
#define ANYSIZE_ARRAY 1

// FILETIME stub
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
#endif // _WIN32

#ifndef _WIN32
// Non-Windows stubs for Windows API types and constants
#define CONST const
#define MB_OK 0
#define CALLBACK
#define FAR
typedef size_t SIZE_T;
typedef DWORD ACCESS_MASK;
typedef ULONG ULONG_PTR;

// Critical section (stub; single-threaded on desktop for now)
typedef struct _RTL_CRITICAL_SECTION {
    int dummy;
} CRITICAL_SECTION, RTL_CRITICAL_SECTION;
inline void InitializeCriticalSection(CRITICAL_SECTION*) {}
inline void EnterCriticalSection(CRITICAL_SECTION*) {}
inline void LeaveCriticalSection(CRITICAL_SECTION*) {}
inline void DeleteCriticalSection(CRITICAL_SECTION*) {}

// lstr* string functions
#define lstrlen strlen
#define lstrcpy strcpy
#define lstrcat strcat
#define lstrcpyn strncpy

// SYSTEMTIME stub
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay;
    WORD wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

// TIME_ZONE_INFORMATION
typedef struct _TIME_ZONE_INFORMATION {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION;
#define TIME_ZONE_ID_UNKNOWN  0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2
inline DWORD GetTimeZoneInformation(TIME_ZONE_INFORMATION* tz) {
    memset(tz, 0, sizeof(*tz));
    return TIME_ZONE_ID_UNKNOWN;
}

// MEMORYSTATUS - reports real host memory (was fake 64MB Xbox values)
typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS;
inline void GlobalMemoryStatus(MEMORYSTATUS* p) {
    memset(p, 0, sizeof(*p));
    p->dwLength = sizeof(*p);
#ifdef __APPLE__
    // sysctl for total physical memory
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) == 0)
        p->dwTotalPhys = (SIZE_T)memsize;
    // vm_statistics for free pages
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics_data_t vmstat;
    if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vmstat, &count) == KERN_SUCCESS)
        p->dwAvailPhys = (SIZE_T)vmstat.free_count * vm_page_size;
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        p->dwTotalPhys = (SIZE_T)si.totalram * si.mem_unit;
        p->dwAvailPhys = (SIZE_T)si.freeram * si.mem_unit;
    }
#else
    p->dwTotalPhys = 4ULL * 1024 * 1024 * 1024; // fallback 4GB
    p->dwAvailPhys = 2ULL * 1024 * 1024 * 1024;
#endif
}

// RECT stub
typedef struct tagRECT { LONG left, top, right, bottom; } RECT;

// WIN32_FIND_DATA stub
typedef struct _WIN32_FIND_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    char cFileName[260];
    char cAlternateFileName[14];
} WIN32_FIND_DATA, *LPWIN32_FIND_DATA;

// WIN32_FILE_ATTRIBUTE_DATA stub
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

// File attribute constants
#define FILE_ATTRIBUTE_DIRECTORY    0x10
#define FILE_ATTRIBUTE_HIDDEN       0x02
#define FILE_ATTRIBUTE_SYSTEM       0x04
#define FILE_ATTRIBUTE_TEMPORARY    0x100
#define FILE_ATTRIBUTE_NORMAL       0x80
#define FILE_FLAG_BACKUP_SEMANTICS  0x02000000
#define FILE_FLAG_SEQUENTIAL_SCAN   0x08000000
#define FILE_SHARE_DELETE           4
#define FILE_SHARE_WRITE            2
#define GetFileExInfoStandard       0
#define INVALID_FILE_ATTRIBUTES     ((DWORD)-1)

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif // !_WIN32

// --- Inline function stubs (must come after TRUE/FALSE/INVALID_HANDLE_VALUE) ---

// Virtual games database (needed by all platforms)
#include "virtual_games.h"

// FindFirstFile/FindNextFile/FindClose; POSIX implementation for non-Windows
#ifndef _WIN32

struct FindFileHandle {
    DIR* dir;
    char dirPath[512];
    char pattern[256]; // filename glob pattern (e.g. "*.xtf")
    // Virtual game injection
    int  vgIndices[VGAMES_MAX]; // indices into g_vgames
    int  vgCount;               // number of virtual entries for this directory
    int  vgPos;                 // current position in virtual list
    char vgDrive[4];            // drive letter for this search (if applicable)
    char vgCategory[32];        // category for this search (if applicable)
};
inline HANDLE FindFirstFile(const char* path, WIN32_FIND_DATA* fd) {
    if (!path || !fd) return INVALID_HANDLE_VALUE;

    // Translate Xbox-style paths (e.g. "Q:\Fonts\*.xtf" -> "xboxfs/Q/Fonts/*.xtf")
    char localPath[512];
    const char* colon = strchr(path, ':');
    if (colon && (colon[1] == '\\' || colon[1] == '/')) {
        int driveLen = (int)(colon - path);
        if (driveLen > 0 && driveLen < 32) {
            char drive[32];
            memcpy(drive, path, driveLen);
            drive[driveLen] = '\0';
            snprintf(localPath, sizeof(localPath), "xboxfs/%s/%s", drive, colon + 2);
        } else {
            strncpy(localPath, path, sizeof(localPath) - 1);
            localPath[sizeof(localPath) - 1] = '\0';
        }
    } else {
        strncpy(localPath, path, sizeof(localPath) - 1);
        localPath[sizeof(localPath) - 1] = '\0';
    }
    // Convert backslashes
    for (char* p = localPath; *p; p++) { if (*p == '\\') *p = '/'; }

    // Find last / to split dir from pattern
    char* lastSlash = strrchr(localPath, '/');
    char dirBuf[512], patBuf[256];
    if (lastSlash) {
        size_t dirLen = lastSlash - localPath;
        memcpy(dirBuf, localPath, dirLen);
        dirBuf[dirLen] = '\0';
        strncpy(patBuf, lastSlash + 1, sizeof(patBuf) - 1);
        patBuf[sizeof(patBuf) - 1] = '\0';
    } else {
        strcpy(dirBuf, ".");
        strncpy(patBuf, localPath, sizeof(patBuf) - 1);
        patBuf[sizeof(patBuf) - 1] = '\0';
    }

    DIR* dir = opendir(dirBuf);
    if (!dir) {
        // Directory doesn't exist; but it might be a virtual game folder
        // Check if dirBuf matches a virtual game: xboxfs/{drive}/{cat}/{name}
        int vgIdx = VGames_MatchFolder(dirBuf);
        if (vgIdx >= 0) {
            // Virtual folder; check what file pattern is being requested
            if (fnmatch("*.xbe", patBuf, FNM_CASEFOLD) == 0 ||
                fnmatch("default.xbe", patBuf, FNM_CASEFOLD) == 0) {
                // Return a fake default.xbe
                memset(fd, 0, sizeof(*fd));
                strcpy(fd->cFileName, "default.xbe");
                fd->nFileSizeLow = 4096; // fake size
                FindFileHandle* ffh = new FindFileHandle;
                ffh->dir = NULL; // no real dir
                strncpy(ffh->dirPath, dirBuf, sizeof(ffh->dirPath) - 1);
                strncpy(ffh->pattern, patBuf, sizeof(ffh->pattern) - 1);
                ffh->vgCount = 0; ffh->vgPos = 0;
                ffh->vgDrive[0] = 0; ffh->vgCategory[0] = 0;
                return (HANDLE)ffh;
            }
            if (fnmatch("default.uixshortcut", patBuf, FNM_CASEFOLD) == 0 ||
                fnmatch("*.*", patBuf, FNM_CASEFOLD) == 0 ||
                strcmp(patBuf, "*") == 0) {
                memset(fd, 0, sizeof(*fd));
                strcpy(fd->cFileName, "default.uixshortcut");
                fd->nFileSizeLow = 256;
                FindFileHandle* ffh = new FindFileHandle;
                ffh->dir = NULL;
                strncpy(ffh->dirPath, dirBuf, sizeof(ffh->dirPath) - 1);
                strncpy(ffh->pattern, patBuf, sizeof(ffh->pattern) - 1);
                ffh->vgCount = 0; ffh->vgPos = 0;
                ffh->vgDrive[0] = 0; ffh->vgCategory[0] = 0;
                return (HANDLE)ffh;
            }
        }
        return INVALID_HANDLE_VALUE;
    }

    FindFileHandle* ffh = new FindFileHandle;
    ffh->dir = dir;
    strncpy(ffh->dirPath, dirBuf, sizeof(ffh->dirPath) - 1);
    ffh->dirPath[sizeof(ffh->dirPath) - 1] = '\0';
    // Windows compat: "*.*" matches all files including those without dots
    if (strcmp(patBuf, "*.*") == 0)
        strcpy(patBuf, "*");
    strncpy(ffh->pattern, patBuf, sizeof(ffh->pattern) - 1);
    ffh->pattern[sizeof(ffh->pattern) - 1] = '\0';

    // Check if this is a game directory scan; inject virtual entries
    // Detect pattern: xboxfs/{drive}/{category}  (e.g. "xboxfs/E/Games")
    ffh->vgCount = 0;
    ffh->vgPos = 0;
    ffh->vgDrive[0] = 0;
    ffh->vgCategory[0] = 0;
    {
        char driveBuf[4] = {}, catBuf[32] = {};
        if (sscanf(dirBuf, "xboxfs/%3[^/]/%31[^/]", driveBuf, catBuf) == 2) {
            // Check if this is a game-related category
            const char* gameCats[] = { "Games", "Applications", "Apps", "Homebrew", "Emulators", "Dashboards" };
            for (int gc = 0; gc < 6; gc++) {
                if (strcasecmp(catBuf, gameCats[gc]) == 0) {
                    strncpy(ffh->vgDrive, driveBuf, sizeof(ffh->vgDrive) - 1);
                    strncpy(ffh->vgCategory, catBuf, sizeof(ffh->vgCategory) - 1);
                    ffh->vgCount = VGames_GetForDirectory(driveBuf, catBuf, ffh->vgIndices, VGAMES_MAX);
                    break;
                }
            }
        }
    }

    // Find first matching entry (real filesystem)
    struct dirent* entry;
    while ((entry = readdir(ffh->dir)) != NULL) {
        if (fnmatch(ffh->pattern, entry->d_name, FNM_CASEFOLD) == 0) {
            memset(fd, 0, sizeof(*fd));
            strncpy(fd->cFileName, entry->d_name, sizeof(fd->cFileName) - 1);
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", ffh->dirPath, entry->d_name);
            struct stat st;
            if (stat(fullPath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
                fd->nFileSizeLow = (DWORD)st.st_size;
            }
            return (HANDLE)ffh;
        }
    }

    // No real entries; try virtual game entries
    while (ffh->vgPos < ffh->vgCount) {
        int idx = ffh->vgIndices[ffh->vgPos++];
        VirtualGame& vg = g_vgames.games[idx];
        if (!vg.valid) continue;
        if (fnmatch(ffh->pattern, vg.name, FNM_CASEFOLD) != 0) continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, vg.name, sizeof(fd->cFileName) - 1);
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return (HANDLE)ffh;
    }

    // No match found at all
    closedir(ffh->dir);
    delete ffh;
    return INVALID_HANDLE_VALUE;
}
inline BOOL FindNextFile(HANDLE h, WIN32_FIND_DATA* fd) {
    if (!h || h == INVALID_HANDLE_VALUE || !fd) return FALSE;
    FindFileHandle* ffh = (FindFileHandle*)h;

    // First exhaust real directory entries (if we have a real dir)
    if (!ffh->dir) goto virtual_entries;
    struct dirent* entry;
    while ((entry = readdir(ffh->dir)) != NULL) {
        if (fnmatch(ffh->pattern, entry->d_name, FNM_CASEFOLD) == 0) {
            memset(fd, 0, sizeof(*fd));
            strncpy(fd->cFileName, entry->d_name, sizeof(fd->cFileName) - 1);
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", ffh->dirPath, entry->d_name);
            struct stat st;
            if (stat(fullPath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
                fd->nFileSizeLow = (DWORD)st.st_size;
            }
            return TRUE;
        }
    }

    // Then serve virtual game entries
    virtual_entries:
    while (ffh->vgPos < ffh->vgCount) {
        int idx = ffh->vgIndices[ffh->vgPos++];
        VirtualGame& vg = g_vgames.games[idx];
        if (!vg.valid) continue;
        // Skip if a real folder with this name already exists
        char checkPath[512];
        snprintf(checkPath, sizeof(checkPath), "%s/%s", ffh->dirPath, vg.name);
        struct stat cst;
        if (stat(checkPath, &cst) == 0) continue; // real folder takes priority
        // Check against glob pattern
        if (fnmatch(ffh->pattern, vg.name, FNM_CASEFOLD) != 0) continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, vg.name, sizeof(fd->cFileName) - 1);
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return TRUE;
    }

    return FALSE;
}
inline BOOL FindClose(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE) {
        FindFileHandle* ffh = (FindFileHandle*)h;
        if (ffh->dir) closedir(ffh->dir);
        delete ffh;
    }
    return TRUE;
}
#endif // !_WIN32

// Simple Xbox path translation for stubs below (full version in xboxfs.h)
// Converts "X:\path\to\file" -> "xboxfs/X/path/to/file"
inline const char* _StubTranslatePath(const char* xboxPath) {
    static char s_stubBuf[512];
    if (!xboxPath) return xboxPath;
    const char* colon = strchr(xboxPath, ':');
    if (colon && (colon[1] == '\\' || colon[1] == '/')) {
        int driveLen = (int)(colon - xboxPath);
        if (driveLen > 0 && driveLen < 32) {
            char drive[32];
            memcpy(drive, xboxPath, driveLen);
            drive[driveLen] = '\0';
            const char* rest = colon + 2;
            snprintf(s_stubBuf, sizeof(s_stubBuf), "xboxfs/%s/%s", drive, rest);
            for (char* p = s_stubBuf; *p; p++) { if (*p == '\\') *p = '/'; }
            return s_stubBuf;
        }
    }
    return xboxPath;
}

// Win32 API function stubs (only needed on non-Windows platforms)
#ifndef _WIN32
inline BOOL GetFileAttributesEx(const char* path, int level, void* info) {
    (void)level; (void)info;
    const char* p = path;
    if (path && strchr(path, ':')) p = _StubTranslatePath(path);
    struct stat st;
    if (stat(p, &st) == 0) {
        // Fill in basic WIN32_FILE_ATTRIBUTE_DATA if info is provided
        if (info) {
            WIN32_FILE_ATTRIBUTE_DATA* fad = (WIN32_FILE_ATTRIBUTE_DATA*)info;
            memset(fad, 0, sizeof(*fad));
            fad->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            fad->nFileSizeLow = (DWORD)(st.st_size & 0xFFFFFFFF);
            fad->nFileSizeHigh = (DWORD)(st.st_size >> 32);
        }
        return TRUE;
    }
    return FALSE;
}
inline DWORD GetFileAttributes(const char* path) {
    const char* p = path;
    if (path && strchr(path, ':')) p = _StubTranslatePath(path);
    struct stat st;
    if (stat(p, &st) == 0)
        return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    return INVALID_FILE_ATTRIBUTES;
}
inline BOOL GetFileTime(HANDLE h, FILETIME* c, FILETIME* a, FILETIME* w) {
    (void)h;
    FILETIME ft = {0, 0};
    if (c) *c = ft;
    if (a) *a = ft;
    if (w) *w = ft;
    return TRUE;
}
inline BOOL CompareFileTime(const FILETIME* a, const FILETIME* b) {
    if (a->dwHighDateTime != b->dwHighDateTime) return a->dwHighDateTime > b->dwHighDateTime ? 1 : -1;
    if (a->dwLowDateTime != b->dwLowDateTime) return a->dwLowDateTime > b->dwLowDateTime ? 1 : -1;
    return 0;
}
inline BOOL RemoveDirectory(const char* path) {
    const char* p = path;
    if (path && strchr(path, ':')) p = _StubTranslatePath(path);
    return rmdir(p) == 0;
}
inline DWORD GetLastError() { return 0; }
inline void GetLocalTime(SYSTEMTIME* st) {
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    if (lt && st) {
        st->wYear = (WORD)(lt->tm_year + 1900);
        st->wMonth = (WORD)(lt->tm_mon + 1);
        st->wDayOfWeek = (WORD)lt->tm_wday;
        st->wDay = (WORD)lt->tm_mday;
        st->wHour = (WORD)lt->tm_hour;
        st->wMinute = (WORD)lt->tm_min;
        st->wSecond = (WORD)lt->tm_sec;
        st->wMilliseconds = 0;
    } else if (st) { memset(st, 0, sizeof(*st)); }
}
inline void GetSystemTime(SYSTEMTIME* st) {
    time_t now = time(NULL);
    struct tm* gm = gmtime(&now);
    if (gm && st) {
        st->wYear = (WORD)(gm->tm_year + 1900);
        st->wMonth = (WORD)(gm->tm_mon + 1);
        st->wDayOfWeek = (WORD)gm->tm_wday;
        st->wDay = (WORD)gm->tm_mday;
        st->wHour = (WORD)gm->tm_hour;
        st->wMinute = (WORD)gm->tm_min;
        st->wSecond = (WORD)gm->tm_sec;
        st->wMilliseconds = 0;
    } else if (st) { memset(st, 0, sizeof(*st)); }
}
inline BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft) {
    // Approximate: store as Unix timestamp * 10,000,000 + Windows epoch offset
    struct tm t = {};
    t.tm_year = st->wYear - 1900; t.tm_mon = st->wMonth - 1; t.tm_mday = st->wDay;
    t.tm_hour = st->wHour; t.tm_min = st->wMinute; t.tm_sec = st->wSecond;
    time_t tt = mktime(&t);
    unsigned long long ull = (unsigned long long)tt * 10000000ULL + 116444736000000000ULL;
    ft->dwLowDateTime = (DWORD)(ull & 0xFFFFFFFF);
    ft->dwHighDateTime = (DWORD)(ull >> 32);
    return TRUE;
}
inline BOOL FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st) {
    unsigned long long ull = ((unsigned long long)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    time_t tt = (time_t)((ull - 116444736000000000ULL) / 10000000ULL);
    struct tm* gm = gmtime(&tt);
    if (gm && st) {
        st->wYear = (WORD)(gm->tm_year + 1900); st->wMonth = (WORD)(gm->tm_mon + 1);
        st->wDayOfWeek = (WORD)gm->tm_wday; st->wDay = (WORD)gm->tm_mday;
        st->wHour = (WORD)gm->tm_hour; st->wMinute = (WORD)gm->tm_min;
        st->wSecond = (WORD)gm->tm_sec; st->wMilliseconds = 0;
    } else if (st) { memset(st, 0, sizeof(*st)); }
    return TRUE;
}
inline BOOL FileTimeToLocalFileTime(const FILETIME* ftUtc, FILETIME* ftLocal) {
    // On desktop, just copy (timezone offset not critical for preview)
    *ftLocal = *ftUtc;
    return TRUE;
}

#define MAX_PATH 260
#endif // !_WIN32

#ifndef _WIN32
// HRESULT values
#define S_OK          ((HRESULT)0)
#define S_FALSE       ((HRESULT)1)
#define E_FAIL        ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_NOTIMPL     ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr)    ((HRESULT)(hr) < 0)
#define HRESULT_CODE(hr)     ((hr) & 0xFFFF)
#define HRESULT_FACILITY(hr) (((hr) >> 16) & 0x1FFF)

typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

// GUID / IUnknown stubs
typedef struct _GUID { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } GUID, IID, CLSID;
typedef const GUID& REFIID;
typedef const GUID& REFCLSID;
#define STDMETHODCALLTYPE
#define __stdcall
#define __cdecl
#define WINAPI

class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID riid, void** ppv) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    virtual ~IUnknown() {}
};

inline long InterlockedIncrement(long* p) { return __sync_add_and_fetch(p, 1); }
inline long InterlockedDecrement(long* p) { return __sync_sub_and_fetch(p, 1); }
#else
// Windows: NTSTATUS not always in windows.h
typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#endif // !_WIN32

#ifndef _WIN32
// -------------------------------------------------------
// TCHAR - use char (ANSI), same as Xbox
// -------------------------------------------------------
typedef char TCHAR;
typedef char* PTCHAR;
typedef TCHAR FSCHAR;
typedef const TCHAR* PCTSTR;
typedef const TCHAR* LPCTSTR;
typedef TCHAR* PTSTR;
typedef TCHAR* LPTSTR;

#define _T(x) x
#define _FS(s) s
#define _tcslen strlen
#define _tcscmp strcmp
#define _tcsncmp strncmp
#define _tcsicmp strcasecmp
#define _tcsnicmp strncasecmp
#define _tcschr strchr
#define _tcsrchr strrchr
#define _tcsstr strstr
#define _tcscpy strcpy
#define _tcsncpy strncpy
#define _tcscat strcat
#define _tcsncat strncat
#define _stprintf sprintf
#define _sntprintf snprintf
#define _vstprintf vsprintf
#define _vsntprintf vsnprintf
#define _ttoi atoi
#define _ttof atof
#define _istdigit isdigit
#define _istspace isspace
#define _istupper isupper
#define _istlower islower
#define _totupper toupper
#define _totlower tolower
#define _tcstod strtod
#define _itoa(val, buf, radix) snprintf(buf, 32, "%d", val)
#define lstrcpyn(dst, src, n) strncpy(dst, src, n)
#define _tfopen fopen
#define _ftprintf fprintf

// _tcslwr / _tcsupr - in-place lowercase/uppercase
inline char* _tcslwr(char* s) { for (char* p = s; *p; p++) *p = tolower((unsigned char)*p); return s; }
inline char* _tcsupr(char* s) { for (char* p = s; *p; p++) *p = toupper((unsigned char)*p); return s; }

// Unicode/Ansi conversion stubs (we use char everywhere, so these are memcpy)
inline void Unicode(char* wsz, const char* sz, int nMaxChars) { strncpy(wsz, sz, nMaxChars); }
inline void Ansi(char* sz, const char* wsz, int nMaxChars) { strncpy(sz, wsz, nMaxChars); }

// Windows string functions mapped to POSIX
#define _strdup strdup
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _fileno fileno
#define stricmp strcasecmp
#define strnicmp strncasecmp

// Memory macros
#define CopyMemory(dst, src, len) memcpy(dst, src, len)
#define ZeroMemory(dst, len) memset(dst, 0, len)
#define FillMemory(dst, len, val) memset(dst, val, len)
#define MoveMemory(dst, src, len) memmove(dst, src, len)
#define CopyChars(dest, src, count) memcpy(dest, src, (count) * sizeof(TCHAR))
#else
// Windows: TCHAR mappings come from <tchar.h>
#include <tchar.h>
typedef TCHAR FSCHAR;
#define _FS(s) _T(s)
// Unicode/Ansi conversion stubs (we use char everywhere)
inline void Unicode(char* wsz, const char* sz, int nMaxChars) { strncpy(wsz, sz, nMaxChars); }
inline void Ansi(char* sz, const char* wsz, int nMaxChars) { strncpy(sz, wsz, nMaxChars); }
#define CopyChars(dest, src, count) memcpy(dest, src, (count) * sizeof(TCHAR))
#endif // !_WIN32

// -------------------------------------------------------
// Xbox kernel string
// -------------------------------------------------------
typedef struct _STRING {
    USHORT Length;
    USHORT MaximumLength;
    char* Buffer;
} STRING, *PSTRING, ANSI_STRING, *PANSI_STRING, OBJECT_STRING, *POBJECT_STRING;

// Desktop soft restart flag; set by launch() when a .uixshortcut is opened,
// checked by the main loop to reinitialize the dashboard
extern bool g_desktopRestartRequested;

// D3D present flags
#define D3DPRESENTFLAG_WIDESCREEN   0x00000010
#define D3DPRESENTFLAG_INTERLACED   0x00000020
#define D3DPRESENTFLAG_PROGRESSIVE  0x00000040








// Network - host IP query (real implementation, not a stub)
typedef struct { DWORD ina, inaMask, inaGateway, inaDnsPrimary, inaDnsSecondary; } XNetConfigStatus;
inline int XNetGetConfigStatus(XNetConfigStatus* p) {
    memset(p, 0, sizeof(*p));
#ifdef _WIN32
    // Use Winsock to get local IP
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent* he = gethostbyname(hostname);
        if (he && he->h_addr_list[0]) {
            memcpy(&p->ina, he->h_addr_list[0], 4);
        }
    }
#else
    // Get real local IP via getifaddrs (POSIX)
    struct ifaddrs* ifa = NULL;
    if (getifaddrs(&ifa) == 0) {
        for (struct ifaddrs* cur = ifa; cur; cur = cur->ifa_next) {
            if (!cur->ifa_addr || cur->ifa_addr->sa_family != AF_INET) continue;
            DWORD addr = ((struct sockaddr_in*)cur->ifa_addr)->sin_addr.s_addr;
            if ((addr & 0xFF) == 127) continue;
            p->ina = addr;
            p->inaMask = cur->ifa_netmask ? ((struct sockaddr_in*)cur->ifa_netmask)->sin_addr.s_addr : 0;
            break;
        }
        freeifaddrs(ifa);
    }
#endif
    if (p->ina == 0) p->ina = 0x0100007f; // fallback 127.0.0.1
    return 0;
}
inline void XNetInAddrToString(DWORD ina, char* pStr, int len) {
    snprintf(pStr, len, "%d.%d.%d.%d", ina & 0xFF, (ina >> 8) & 0xFF, (ina >> 16) & 0xFF, (ina >> 24) & 0xFF);
}

// Crypto stubs
typedef struct { BYTE Signature[20]; } XCALCSIG_SIGNATURE;
#define XCALCSIG_FLAG_SAVE_GAME 0
#define XCALCSIG_FLAG_NON_ROAMABLE 0



#ifndef _WIN32
// File API stubs
// NOTE: CreateFileA is defined in xboxfs.h with path translation
inline BOOL ReadFile(HANDLE h, void* buf, DWORD nBytes, DWORD* nRead, void* ovlp) {
    size_t r = fread(buf, 1, nBytes, (FILE*)h);
    if (nRead) *nRead = (DWORD)r;
    // Windows ReadFile returns TRUE even on short/zero reads (EOF); FALSE only on error
    return !ferror((FILE*)h);
}
inline BOOL CloseHandle(HANDLE h) { if (h && h != INVALID_HANDLE_VALUE) fclose((FILE*)h); return TRUE; }
inline DWORD SetFilePointer(HANDLE h, LONG dist, LONG* distHigh, DWORD method) {
    int origin = (method == 0) ? SEEK_SET : (method == 1) ? SEEK_CUR : SEEK_END;
    fseek((FILE*)h, dist, origin);
    return (DWORD)ftell((FILE*)h);
}
inline DWORD GetFileSize(HANDLE h, DWORD* high) {
    long pos = ftell((FILE*)h);
    fseek((FILE*)h, 0, SEEK_END);
    long size = ftell((FILE*)h);
    fseek((FILE*)h, pos, SEEK_SET);
    if (high) *high = 0;
    return (DWORD)size;
}
#define GENERIC_READ  0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define OPEN_ALWAYS   4
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

// WriteFile stub
inline BOOL WriteFile(HANDLE h, const void* buf, DWORD nBytes, DWORD* nWritten, void* ovlp) {
    (void)ovlp;
    size_t w = fwrite(buf, 1, nBytes, (FILE*)h);
    if (nWritten) *nWritten = (DWORD)w;
    return !ferror((FILE*)h);
}

// SetFileTime stub (no-op on desktop)
inline BOOL SetFileTime(HANDLE h, const FILETIME* c, const FILETIME* a, const FILETIME* w) {
    (void)h; (void)c; (void)a; (void)w;
    return TRUE;
}

// SetFileAttributes stub
inline BOOL SetFileAttributes(const char* path, DWORD attr) {
    (void)path; (void)attr;
    return TRUE;
}
#define SetFileAttributesA SetFileAttributes

// CharUpper/CharLower (Win32 string functions)
inline LPTSTR CharUpper(LPTSTR p) {
    if ((uintptr_t)p < 256) return (LPTSTR)(uintptr_t)toupper((int)(uintptr_t)p);
    for (char* c = (char*)p; *c; c++) *c = toupper((unsigned char)*c);
    return p;
}
inline LPTSTR CharLower(LPTSTR p) {
    if ((uintptr_t)p < 256) return (LPTSTR)(uintptr_t)tolower((int)(uintptr_t)p);
    for (char* c = (char*)p; *c; c++) *c = tolower((unsigned char)*c);
    return p;
}

// DeleteFile is provided by xboxfs.h with path translation

// CreateDirectory
#include <errno.h>
inline BOOL CreateDirectory(const char* path, void* sa) {
    (void)sa;
    const char* p = path;
    if (path && strchr(path, ':')) p = _StubTranslatePath(path);
#ifdef _WIN32
    return CreateDirectoryA(p, NULL);
#else
    return mkdir(p, 0755) == 0 || errno == EEXIST;
#endif
}
#define CreateDirectoryA CreateDirectory

// CreateThread stub (single-threaded on POSIX for now)
typedef DWORD (*LPTHREAD_START_ROUTINE)(void*);
inline HANDLE CreateThread(void* sa, DWORD stackSize, LPTHREAD_START_ROUTINE startAddr, void* param, DWORD flags, DWORD* tid) {
    (void)sa; (void)stackSize; (void)startAddr; (void)param; (void)flags; (void)tid;
    return NULL;
}
#define INFINITE 0xFFFFFFFF
inline BOOL SetEvent(HANDLE h) { (void)h; return TRUE; }
inline BOOL ResetEvent(HANDLE h) { (void)h; return TRUE; }
inline HANDLE CreateEvent(void* sa, BOOL manual, BOOL initial, void* name) {
    (void)sa; (void)manual; (void)initial; (void)name;
    return (HANDLE)(intptr_t)1; // non-NULL dummy
}
#define WAIT_OBJECT_0 0
inline DWORD WaitForSingleObject(HANDLE h, DWORD ms) { (void)h; (void)ms; return WAIT_OBJECT_0; }
inline DWORD SignalObjectAndWait(HANDLE a, HANDLE b, DWORD ms, BOOL alert) { (void)a; (void)b; (void)ms; (void)alert; return WAIT_OBJECT_0; }

// Error constants
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS 183
#endif
#ifndef ERROR_DEVICE_NOT_CONNECTED
#define ERROR_DEVICE_NOT_CONNECTED 1167
#endif
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0
#endif

// GetTickCount
inline DWORD GetTickCount() { return (DWORD)SDL_GetTicks(); }

// Sleep
inline void Sleep(DWORD ms) { SDL_Delay(ms); }

// Debug output
inline void OutputDebugStringA(const char* s) { fprintf(stderr, "%s", s); }
#define OutputDebugString OutputDebugStringA
#endif // !_WIN32

// -------------------------------------------------------
// Macros
// -------------------------------------------------------
#ifndef countof
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef _WIN32
// SetRect stub (also in Image.h for non-Windows)
#ifndef SetRect
#define SetRect(pRect, nLeft, nTop, nRight, nBottom) \
    do { (pRect)->left = (nLeft); (pRect)->top = (nTop); (pRect)->right = (nRight); (pRect)->bottom = (nBottom); } while(0)
#endif
#endif // !_WIN32

// XGWriteSurfaceToFile stub (used by screen.cpp screenshot)
inline HRESULT XGWriteSurfaceToFile(void* pSurface, const char* path) { (void)pSurface; (void)path; return 0; }

// WAVEFORMATEX (from Windows mmsystem.h / dsound.h)
#ifndef _WAVEFORMATEX_
#define _WAVEFORMATEX_
typedef struct tWAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
#endif
#define WAVE_FORMAT_PCM 1

// DirectSound buffer stub (desktop uses SDL audio, not DSound)
#ifndef _XBOX
typedef void* LPDIRECTSOUNDBUFFER;
typedef void* LPDIRECTSOUND;
#endif

#ifndef _WIN32
// OVERLAPPED stub (async I/O - not used on desktop)
typedef struct _OVERLAPPED {
    DWORD Internal;
    DWORD InternalHigh;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
} OVERLAPPED;

#define FILE_FLAG_OVERLAPPED    0x40000000
#define FILE_FLAG_NO_BUFFERING  0x20000000
#define ERROR_IO_PENDING        997
#define HasOverlappedIoCompleted(lpOverlapped) (TRUE)

// HBITMAP / HDC stubs for Image.h (GDI types not available on macOS)
typedef void* HBITMAP;
typedef void* HDC;

// SIZE struct (Windows GDI)
#ifndef _SIZE_DEFINED
#define _SIZE_DEFINED
typedef struct tagSIZE { LONG cx, cy; } SIZE;
#endif

// COLORREF
typedef DWORD COLORREF;
#endif // !_WIN32

// D3DBACKBUFFER_TYPE_MONO
#define D3DBACKBUFFER_TYPE_MONO 0

// Xbox XPR (texture resource) structures
// Field order matches XDK XGraphics.h: Magic, TotalSize, HeaderSize
typedef struct _XPR_HEADER {
    DWORD dwMagic;
    DWORD dwTotalSize;
    DWORD dwHeaderSize;
} XPR_HEADER;
#define XPR_MAGIC_VALUE 0x30525058  // 'XPR0'

// Xbox D3D resource types (for XBX texture parsing)
#define D3DTEXTURE_ALIGNMENT     256
#define D3DCOMMON_D3DCREATED     0x00010000
#define D3DCOMMON_TYPE_MASK      0x00070000
#define D3DCOMMON_TYPE_TEXTURE   0x00040000
#define D3DCOMMON_TYPE_VERTEXBUFFER 0x00010000
#define D3DCOMMON_TYPE_INDEXBUFFER  0x00020000
typedef void* LPDIRECT3DRESOURCE8;

// D3D_OK
#define D3D_OK S_OK

// NOTE: D3DXCreateTexture and D3DXCreateTextureFromFileInMemoryEx
// are defined in d3d8_sdl.h where IDirect3DTexture8 is available.

// D3D format info masks (Xbox-specific, for XBX image validation)
#define D3DFORMAT_USIZE_MASK   0x00F00000
#define D3DFORMAT_USIZE_SHIFT  20
#define D3DFORMAT_VSIZE_MASK   0x0F000000
#define D3DFORMAT_VSIZE_SHIFT  24

// Xbox D3D memory stubs
typedef struct { DWORD Common; DWORD Data; void (*Register)(void*); } D3DResource, D3DBaseTexture;
inline void D3D_CopyContiguousMemoryToVideo(void* p) { (void)p; }
inline void* D3D_AllocContiguousMemory(DWORD size, DWORD align) { return malloc(size); }

#ifndef _WIN32
// LARGE_INTEGER
typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    long long QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    unsigned long long QuadPart;
} ULARGE_INTEGER;

// GetDiskFreeSpaceEx stub
inline BOOL GetDiskFreeSpaceEx(const char* path, ULARGE_INTEGER* lpFree,
                                ULARGE_INTEGER* lpTotal, ULARGE_INTEGER* lpTotalFree) {
    (void)path;
    if (lpFree) lpFree->QuadPart = 0;
    if (lpTotal) lpTotal->QuadPart = 0;
    if (lpTotalFree) lpTotalFree->QuadPart = 0;
    return TRUE;
}
#define GetDiskFreeSpaceExA GetDiskFreeSpaceEx
#endif // !_WIN32



#ifndef _WIN32
// LARGE_INTEGER / LONGLONG
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;

// MultiByteToWideChar / WideCharToMultiByte stubs (no-op in ANSI build)
#define CP_ACP 0
inline int MultiByteToWideChar(UINT codePage, DWORD flags, const char* src, int srcLen, wchar_t* dst, int dstLen) {
    (void)codePage; (void)flags;
    int len = (srcLen == -1) ? (int)strlen(src) + 1 : srcLen;
    if (dstLen == 0) return len;
    for (int i = 0; i < len && i < dstLen; i++) dst[i] = (wchar_t)src[i];
    return len;
}
inline int WideCharToMultiByte(UINT codePage, DWORD flags, const wchar_t* src, int srcLen, char* dst, int dstLen, const char* defChar, BOOL* usedDef) {
    (void)codePage; (void)flags; (void)defChar; (void)usedDef;
    int len = (srcLen == -1) ? (int)wcslen(src) + 1 : srcLen;
    if (dstLen == 0) return len;
    for (int i = 0; i < len && i < dstLen; i++) dst[i] = (char)src[i];
    return len;
}

// FindFirstFileA / FindNextFileA - same as FindFirstFile (ANSI build)
typedef WIN32_FIND_DATA WIN32_FIND_DATAA;
#define FindFirstFileA FindFirstFile
#define FindNextFileA FindNextFile

// __debugbreak
#define __debugbreak() SDL_TriggerBreakpoint()
#endif // !_WIN32

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0
#endif


// -------------------------------------------------------
// Xbox game region constants (used by Locale.h)
// -------------------------------------------------------
#define XC_GAME_REGION_NA           1
#define XC_GAME_REGION_JAPAN        2
#define XC_GAME_REGION_RESTOFWORLD  3

// (TIME_ZONE_INFORMATION moved earlier, before timezone stubs)

// -------------------------------------------------------
// Missing constants for Locale.cpp / Xbox.cpp
// -------------------------------------------------------
#ifndef _WIN32
#define GENERIC_ALL 0x10000000
#endif

// -------------------------------------------------------
// Xbox.cpp stubs
// -------------------------------------------------------

#ifndef _WIN32
// DeleteFileA stub
inline BOOL DeleteFileA(const char* path) {
    const char* p = path;
    if (path && strchr(path, ':')) p = _StubTranslatePath(path);
    return remove(p) == 0;
}
#endif // !_WIN32

// IN_ADDR struct (Windows already has this from winsock2.h)
#ifndef _WIN32
typedef struct in_addr_stub {
    DWORD s_addr;
    in_addr_stub() : s_addr(0) {}
    in_addr_stub(DWORD a) : s_addr(a) {}
} IN_ADDR;
#endif

// Overload for IN_ADDR parameter
inline void XNetInAddrToString(IN_ADDR addr, char* pStr, int len) {
    DWORD a = addr.s_addr;
    snprintf(pStr, len, "%d.%d.%d.%d", a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF);
}






#ifndef _WIN32
// swprintf - macOS/Linux swprintf requires a size param, but Xbox code doesn't pass one
// Use a wrapper function instead of a recursive macro
inline int _uix_swprintf(wchar_t* buf, const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vswprintf(buf, 4096, fmt, args);
    va_end(args);
    return r;
}
#define swprintf _uix_swprintf
#endif // !_WIN32

// Xbox filesystem path translation; provides CreateFile/fopen/etc.
// macro redirection so engine code that calls Win32 file APIs works
// transparently on all platforms via xboxfs path mapping.
#include "xboxfs.h"

