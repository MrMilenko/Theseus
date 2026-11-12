// std.h: desktop precompiled header. Replaces Xbox XDK / Windows
// includes with the SDL2 + OpenGL platform layer. Counterpart to
// xbox/std.h.

#include "platform.h"
#include "stdcfg.h"

// Flag so toolbox/xboxinternals.h knows kernel types are already available
#define _THESEUS_STD_H

// SDL platform abstraction (HANDLE, HRESULT, D3D compat types, Xbox kernel stubs)
#include "platform_shim.h"

// D3D8 type stubs (no-op interfaces, math types, enums)
#include "d3d8_sdl.h"


// Standard C/C++ headers (already included by platform_shim.h, but
// some source files expect these to come from std.h)
#include <new>
#include <cerrno>

// DWORD is uint32_t on every desktop host, so uint32_t* passes straight
// through to ReadFile and friends.
#define LPDW(p) (p)



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
typedef void* HDEVNOTIFY;

typedef struct tagRGBQUAD {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
} RGBQUAD;
typedef RGBQUAD* LPRGBQUAD;

#define MAX_COMPUTERNAME_LENGTH 15

// GetCurrentThreadId (platform_shim.h doesn't provide this one)
inline DWORD GetCurrentThreadId() { return (DWORD)SDL_ThreadID(); }

// D3D vertex types
#include "vertex8.h"
