// platform_shim.h: the Xbox-shaped world the dashboard code lives in on
// desktop. Win32 and XDK names (HANDLE, ReadFile, GetTickCount, FILETIME)
// backed by SDL, the C runtime and host.h.
//
// This file is the same on every host and never includes an OS header.
// Real Win32 lives in host_win.cpp, so dashboard code can't reach kernel32
// by accident.

#pragma once

#include "platform.h"
#include "host.h"
#include "xboxfs_drive.h" // XboxFS_DriveToPrefix shared helper, used by stubs below
#include "xbox_path.h"    // the one Xbox path translator, used by stubs below

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
#include <cctype>
#include <cerrno>
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
#include <errno.h>
#endif
// All three toolchains ship these. Not <dirent.h>: on mingw that's bx's
// compat header, which pulls in <windows.h>. Folder listing is host.h's.
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef F_OK
#define F_OK 0
#endif

// -------------------------------------------------------
// Windows base types
// -------------------------------------------------------
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

// Window handle stubs (not used in SDL build but needed by Win32-style code paths)
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
// Win32 semantics: copies at most n-1 chars and always terminates.
inline char* lstrcpyn(char* dst, const char* src, int n) {
    if (n <= 0) return dst;
    size_t i = 0;
    for (; i + 1 < (size_t)n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}

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

// MEMORYSTATUS: real host memory, not the Xbox's 64MB.
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
    uint64_t total = 0, avail = 0;
    Host_MemoryStatus(&total, &avail);
    p->dwTotalPhys = (SIZE_T)total;
    p->dwAvailPhys = (SIZE_T)avail;
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
#define FILE_ATTRIBUTE_READONLY     0x01
#define FILE_ATTRIBUTE_DIRECTORY    0x10
#define FILE_ATTRIBUTE_HIDDEN       0x02
#define FILE_ATTRIBUTE_SYSTEM       0x04
#define FILE_ATTRIBUTE_TEMPORARY    0x100
#define FILE_ATTRIBUTE_NORMAL       0x80
#define FILE_FLAG_BACKUP_SEMANTICS  0x02000000
#define FILE_FLAG_SEQUENTIAL_SCAN   0x08000000
#define FILE_SHARE_DELETE           4
#define FILE_SHARE_WRITE            2
typedef int GET_FILEEX_INFO_LEVELS;
#define GetFileExInfoStandard       0
#define INVALID_FILE_ATTRIBUTES     ((DWORD)-1)

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// --- Inline function stubs (must come after TRUE/FALSE/INVALID_HANDLE_VALUE) ---

// Virtual games database (needed by all platforms)
#include "virtual_games.h"

// Seconds since 1970 to FILETIME's 100ns ticks since 1601, and back.
inline FILETIME Shim_UnixToFileTime(int64_t t) {
    unsigned long long ull = (unsigned long long)(t + 11644473600LL) * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ull & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(ull >> 32);
    return ft;
}
inline int64_t Shim_FileTimeToUnix(const FILETIME* ft) {
    unsigned long long ull = ((unsigned long long)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    return (int64_t)(ull / 10000000ULL) - 11644473600LL;
}
// Days since 1970 for a proleptic Gregorian date, no time zone involved.
inline int64_t Shim_DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
// Local time minus UTC, in seconds, at the given moment.
inline int64_t Shim_UtcOffset(time_t t) {
    struct tm lt = *localtime(&t);
    struct tm gt = *gmtime(&t);
    int64_t l = Shim_DaysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
                lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    int64_t g = Shim_DaysFromCivil(gt.tm_year + 1900, gt.tm_mon + 1, gt.tm_mday) * 86400 +
                gt.tm_hour * 3600 + gt.tm_min * 60 + gt.tm_sec;
    return l - g;
}

// File HANDLEs are FILE*s on every host, opened by TheseusCreateFile.
inline BOOL GetFileTime(HANDLE h, FILETIME* c, FILETIME* a, FILETIME* w) {
    FILETIME ft = {0, 0};
    HostFileInfo info;
    if (h && h != INVALID_HANDLE_VALUE && Host_StatOpenFile((FILE*)h, &info))
        ft = Shim_UnixToFileTime(info.mtime);
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
inline DWORD GetLastError() { return 0; }
inline void Shim_TmToSystemTime(const struct tm* t, int ms, SYSTEMTIME* st) {
    st->wYear = (WORD)(t->tm_year + 1900);
    st->wMonth = (WORD)(t->tm_mon + 1);
    st->wDayOfWeek = (WORD)t->tm_wday;
    st->wDay = (WORD)t->tm_mday;
    st->wHour = (WORD)t->tm_hour;
    st->wMinute = (WORD)t->tm_min;
    st->wSecond = (WORD)t->tm_sec;
    st->wMilliseconds = (WORD)ms;
}
inline void GetLocalTime(SYSTEMTIME* st) {
    if (!st) return;
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    if (lt) Shim_TmToSystemTime(lt, 0, st);
    else memset(st, 0, sizeof(*st));
}
inline void GetSystemTime(SYSTEMTIME* st) {
    if (!st) return;
    time_t now = time(NULL);
    struct tm* gm = gmtime(&now);
    if (gm) Shim_TmToSystemTime(gm, 0, st);
    else memset(st, 0, sizeof(*st));
}
// SYSTEMTIME is UTC here, same as Win32.
inline BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft) {
    int64_t t = Shim_DaysFromCivil(st->wYear, st->wMonth, st->wDay) * 86400 +
                st->wHour * 3600 + st->wMinute * 60 + st->wSecond;
    unsigned long long ull = (unsigned long long)(t + 11644473600LL) * 10000000ULL +
                             (unsigned long long)st->wMilliseconds * 10000ULL;
    ft->dwLowDateTime = (DWORD)(ull & 0xFFFFFFFF);
    ft->dwHighDateTime = (DWORD)(ull >> 32);
    return TRUE;
}
inline BOOL FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st) {
    unsigned long long ull = ((unsigned long long)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    time_t tt = (time_t)Shim_FileTimeToUnix(ft);
    struct tm* gm = gmtime(&tt);
    if (gm && st) Shim_TmToSystemTime(gm, (int)((ull / 10000ULL) % 1000), st);
    else if (st) memset(st, 0, sizeof(*st));
    return gm != NULL;
}
inline BOOL FileTimeToLocalFileTime(const FILETIME* ftUtc, FILETIME* ftLocal) {
    unsigned long long ull = ((unsigned long long)ftUtc->dwHighDateTime << 32) | ftUtc->dwLowDateTime;
    int64_t off = Shim_UtcOffset((time_t)Shim_FileTimeToUnix(ftUtc));
    ull += (unsigned long long)(off * 10000000LL);
    ftLocal->dwLowDateTime = (DWORD)(ull & 0xFFFFFFFF);
    ftLocal->dwHighDateTime = (DWORD)(ull >> 32);
    return TRUE;
}

#define MAX_PATH 260

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
// mingw already has these as macros for its own headers.
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef WINAPI
#define WINAPI
#endif

class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID riid, void** ppv) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    virtual ~IUnknown() {}
};

inline long InterlockedIncrement(long* p) { return __sync_add_and_fetch(p, 1); }
inline long InterlockedDecrement(long* p) { return __sync_sub_and_fetch(p, 1); }

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
#define _tfopen fopen
#define _ftprintf fprintf

// _tcslwr / _tcsupr - in-place lowercase/uppercase
inline char* _tcslwr(char* s) { for (char* p = s; *p; p++) *p = tolower((unsigned char)*p); return s; }
inline char* _tcsupr(char* s) { for (char* p = s; *p; p++) *p = toupper((unsigned char)*p); return s; }

// Unicode/Ansi conversion stubs (we use char everywhere, so these are memcpy)
inline void Unicode(char* wsz, const char* sz, int nMaxChars) { strncpy(wsz, sz, nMaxChars); }
inline void Ansi(char* sz, const char* wsz, int nMaxChars) { strncpy(sz, wsz, nMaxChars); }

// Microsoft C runtime names. mingw links that runtime, so it has them.
#ifndef THESEUS_CRT_MS
#define _itoa(val, buf, radix) snprintf(buf, 32, "%d", val)
#define _strdup strdup
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _fileno fileno
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif

// Memory macros
#define CopyMemory(dst, src, len) memcpy(dst, src, len)
#define ZeroMemory(dst, len) memset(dst, 0, len)
#define FillMemory(dst, len, val) memset(dst, val, len)
#define MoveMemory(dst, src, len) memmove(dst, src, len)
#define CopyChars(dest, src, count) memcpy(dest, src, (count) * sizeof(TCHAR))

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








// Network: the host's own address.
typedef struct { DWORD ina, inaMask, inaGateway, inaDnsPrimary, inaDnsSecondary; } XNetConfigStatus;
inline int XNetGetConfigStatus(XNetConfigStatus* p) {
    memset(p, 0, sizeof(*p));
    uint32_t mask = 0;
    p->ina = Host_LocalIPv4(&mask);
    p->inaMask = mask;
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



// Handle-based file calls. File HANDLEs are FILE*s on every host, opened
// by TheseusCreateFile in xbox_file.cpp.
inline BOOL ReadFile(HANDLE h, void* buf, DWORD nBytes, DWORD* nRead, void* ovlp) {
    size_t r = fread(buf, 1, nBytes, (FILE*)h);
    if (nRead) *nRead = (DWORD)r;
    // Windows ReadFile returns TRUE even on short/zero reads (EOF); FALSE only on error
    return !ferror((FILE*)h);
}
inline BOOL CloseHandle(HANDLE h) { if (h && h != INVALID_HANDLE_VALUE) fclose((FILE*)h); return TRUE; }
inline DWORD SetFilePointer(HANDLE h, LONG dist, LONG* distHigh, DWORD method) {
    int origin = (method == 0) ? SEEK_SET : (method == 1) ? SEEK_CUR : SEEK_END;
    int64_t off = distHigh ? (int64_t)(((uint64_t)(uint32_t)*distHigh << 32) | (uint32_t)dist) : (int64_t)dist;
    int64_t pos = Host_Seek((FILE*)h, off, origin);
    if (pos < 0) return 0xFFFFFFFF;
    if (distHigh) *distHigh = (LONG)(pos >> 32);
    return (DWORD)(pos & 0xFFFFFFFF);
}
inline DWORD GetFileSize(HANDLE h, DWORD* high) {
    HostFileInfo info;
    fflush((FILE*)h);
    if (!Host_StatOpenFile((FILE*)h, &info)) info.size = 0;
    if (high) *high = (DWORD)(info.size >> 32);
    return (DWORD)(info.size & 0xFFFFFFFF);
}
#define GENERIC_READ  0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define OPEN_ALWAYS   4
#ifndef CREATE_NEW
#define CREATE_NEW    1
#endif
#ifndef TRUNCATE_EXISTING
#define TRUNCATE_EXISTING 5
#endif
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

// Desktop handles are FILE*s; this only empties the CRT buffer, reporting a failed write.
inline BOOL FlushFileBuffers(HANDLE h) {
    return h && h != INVALID_HANDLE_VALUE && fflush((FILE*)h) == 0;
}

// SetFileTime stub (no-op on desktop)
inline BOOL SetFileTime(HANDLE h, const FILETIME* c, const FILETIME* a, const FILETIME* w) {
    (void)h; (void)c; (void)a; (void)w;
    return TRUE;
}

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

// -------------------------------------------------------
// Macros
// -------------------------------------------------------
#ifndef countof
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef SetRect
#define SetRect(pRect, nLeft, nTop, nRight, nBottom) \
    do { (pRect)->left = (nLeft); (pRect)->top = (nTop); (pRect)->right = (nRight); (pRect)->bottom = (nBottom); } while(0)
#endif

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

// LARGE_INTEGER
typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    long long QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    unsigned long long QuadPart;
} ULARGE_INTEGER;

// LONGLONG
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

// ANSI build, so the A struct is the struct.
typedef WIN32_FIND_DATA WIN32_FIND_DATAA;

// __debugbreak is a compiler intrinsic under mingw.
#ifndef THESEUS_CRT_MS
#define __debugbreak() SDL_TriggerBreakpoint()
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0
#endif


// -------------------------------------------------------
// Xbox game region constants (used by Locale.h)
// -------------------------------------------------------
#define XC_GAME_REGION_NA           1
#define XC_GAME_REGION_JAPAN        2
#define XC_GAME_REGION_RESTOFWORLD  3


// -------------------------------------------------------
// Missing constants for Locale.cpp / Xbox.cpp
// -------------------------------------------------------
#define GENERIC_ALL 0x10000000

// -------------------------------------------------------
// Xbox.cpp stubs
// -------------------------------------------------------

// IN_ADDR
typedef struct in_addr_stub {
    DWORD s_addr;
    in_addr_stub() : s_addr(0) {}
    in_addr_stub(DWORD a) : s_addr(a) {}
} IN_ADDR;

// Overload for IN_ADDR parameter
inline void XNetInAddrToString(IN_ADDR addr, char* pStr, int len) {
    DWORD a = addr.s_addr;
    snprintf(pStr, len, "%d.%d.%d.%d", a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF);
}






#ifndef THESEUS_CRT_MS
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
#endif


