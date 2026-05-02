// std.h: desktop precompiled header. Replaces Xbox XDK / Windows
// includes with the SDL2 + OpenGL platform layer. Counterpart to
// xbox/std.h.

#include "stdcfg.h"

// Flag so toolbox/xboxinternals.h knows kernel types are already available
#define _THESEUS_STD_H

// SDL platform abstraction (HANDLE, HRESULT, D3D compat types, Xbox kernel stubs)
#include "platform_shim.h"

// D3D8 type stubs (no-op interfaces, math types, enums)
#include "d3d8_sdl.h"

// Filesystem path translation (Q:\ -> ./xboxfs/Q/)
#include "xboxfs.h"

// Standard C/C++ headers (already included by platform_shim.h, but
// some source files expect these to come from std.h)
#include <new>
#include <cerrno>

// MSVC: DWORD is 'unsigned long', uint32_t is 'unsigned int'. Both are 32-bit
// but MSVC treats them as incompatible pointer types. Win32 APIs (ReadFile,
// WriteFile, CreateThread, etc.) expect DWORD*. This cast macro bridges the gap
// at API boundaries so src/ files can use uint32_t consistently.
// Both MSVC and mingw GCC treat DWORD/uint32_t as distinct types when
// they target Windows headers, so the cast is needed for any _WIN32 build.
#ifdef _WIN32
#define LPDW(p) reinterpret_cast<DWORD*>(p)
#else
#define LPDW(p) (p)
#endif



#define BREAKONFAIL(a,b)  {if(FAILED(a)) {fprintf(stderr, "%s\n", b); break;} }



#ifdef __cplusplus

#define EXTERN_C extern "C"

#define RELEASENULL(object) { if ((object) != NULL) { (object)->Release(); (object) = NULL; } }

#else // !__cplusplus

#define EXTERN_C extern
#define bool BOOL
#define true TRUE
#define false FALSE

#endif

#ifndef countof
#define countof(n) (sizeof(n) / sizeof(n[0]))
#endif

// Computes the byte offset of a member within the enclosing class.
// Uses the same null-pointer trick as NODE_PROP so the result matches
// pprd->pbOffset. Subtracting (uint8_t*)0 keeps the arithmetic valid
// without depending on the runtime value of 'this'.
#define MEMBER_OFFSET(member) \
	((intptr_t)((uint8_t*)&(((decltype(this))0)->member) - (uint8_t*)0))

// Single-arg member offset, matching xbox/std.h. Conflicts with the standard
// offsetof(type, member) from <cstddef>, so undef it first.
#undef offsetof
#define offsetof(member) MEMBER_OFFSET(member)

// Safe pointer-to-int cast for 64-bit desktop hosts.
#define PTR2INT(p) ((int)(intptr_t)(p))

// ============================================================================
// Debug Infrastructure
// ============================================================================
// TRACE/ASSERT are always active on desktop. Debug tools (F1 inspector,
// XAP editor) are runtime-toggled, not compile-time gated.

#undef ASSERT
#undef VERIFY
#undef ASSERTHR
#undef VERIFYHR
#undef TRACE
#undef ALERT

EXTERN_C void Trace(const char* szMsg, ...);
EXTERN_C void Alert(const char* szMsg, ...);

#define ASSERT(f)       do { if (!(f)) { fprintf(stderr, "[ASSERT] %s:%d: %s\n", __FILE__, __LINE__, #f); } } while(0)
#define VERIFY(f)       ASSERT(f)
#define ASSERTHR(f)     do { HRESULT _hr = (f); if (FAILED(_hr)) { fprintf(stderr, "[ASSERT] %s:%d: HRESULT 0x%08x\n", __FILE__, __LINE__, (unsigned)_hr); } } while(0)
#define VERIFYHR(f)     ASSERTHR(f)
#define TRACE           Trace
#define ALERT           Alert

// Profiling stubs (Xbox profiling removed)
#define START_PROFILE()
#define END_PROFILE()



// Types not provided by platform_shim.h: GDI/RGB color helpers and
// the device-notify stub used by Win32 device hot-plug callbacks.
#ifndef _WIN32
typedef void* HDEVNOTIFY;

typedef struct tagRGBQUAD {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
} RGBQUAD;
typedef RGBQUAD* LPRGBQUAD;

#define MAX_COMPUTERNAME_LENGTH 15

// MoveFile stub (translates Xbox paths). DeleteFile / CreateFile / WriteFile /
// CreateDirectory and the thread / critical section / event stubs all live
// in platform_shim.h.
inline BOOL MoveFile(const char* src, const char* dst) {
    const char* s = src;
    const char* d = dst;
    if (src && strchr(src, ':')) s = _StubTranslatePath(src);
    char srcBuf[512];
    strncpy(srcBuf, s, sizeof(srcBuf) - 1); srcBuf[sizeof(srcBuf) - 1] = '\0';
    if (dst && strchr(dst, ':')) d = _StubTranslatePath(dst);
    return rename(srcBuf, d) == 0;
}

// GetCurrentThreadId (platform_shim.h doesn't provide this one)
inline DWORD GetCurrentThreadId() { return (DWORD)SDL_ThreadID(); }
#else
// Windows: RGBQUAD comes from windows.h, but LPRGBQUAD isn't always defined
#ifndef LPRGBQUAD
typedef RGBQUAD* LPRGBQUAD;
#endif
#endif // !_WIN32

// D3D vertex types
#include "vertex8.h"
