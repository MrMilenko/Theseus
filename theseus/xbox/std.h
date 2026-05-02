// std.h: universal prelude header. Every .cpp file includes this
// first; sets up the type system, CRT headers, D3D types, and
// debug infrastructure for whichever platform we're building on.
//
//   _XBOX            : XDK kernel + XTL + D3D8 (original Xbox hardware)
//   !_XBOX + _WIN32  : Win32 API + SDL + OpenGL (Windows desktop)
//   !_XBOX + !_WIN32 : POSIX shims + SDL + OpenGL (macOS / Linux)

// Check Configuration Options
#include "stdcfg.h"

// Flag so toolbox/xboxinternals.h knows kernel types are already available
#define _THESEUS_STD_H

#ifndef _LIGHTS
#define _LIGHTS
#endif

// =========================================================================
// Platform-specific type system and API headers
// =========================================================================

#ifdef _XBOX
// --- Xbox: XDK kernel + XTL ---

#include <new.h>

#if defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <malloc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include <ntos.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <ntdddisk.h>
#include <ntddcdrm.h>
#include <ntddscsi.h>
#include <ntddcdvd.h>
#include "smcdef.h"
#include <scsi.h>
#include <init.h>
#include <xtl.h>
#include <xgraphics.h>
#include <xboxp.h>
#include <xboxverp.h>
#include "xapip.h"
#include <av.h>
#include <dm.h>
#include <smcdef.h>
#ifdef __cplusplus
}
#endif

#ifndef D3DLOCK_DISCARD
#define D3DLOCK_DISCARD 0
#endif

#include <tchar.h>
typedef TCHAR* PTCHAR;

typedef char FSCHAR;
#define _FS(s) s

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include <crtdbg.h>

#if !defined(_NOD3D)
#define D3D_OVERLOADS
#include <d3d8.h>
#include <d3dx8.h>
#include <d3d8perf.h>
#include "vertex8.h"
#endif

#else // !_XBOX
// --- Desktop: SDL platform shim + D3D8-to-OpenGL ---

#include "platform_shim.h"
#include "d3d8_sdl.h"

#endif // _XBOX

// =========================================================================
// Shared macros and utilities (all platforms)
// =========================================================================

#define BREAKONFAIL(a,b)  {if(FAILED(a)) {DbgPrint(b);break;} }

#ifdef __cplusplus

#define EXTERN_C extern "C"

// Xbox debug CRT overrides operator new for leak tracking.
// Desktop doesn't use this -- it has its own tooling.
#if defined(_XBOX) && defined(_DEBUG)
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#define RELEASENULL(object) { if ((object) != NULL) { (object)->Release(); (object) = NULL; } }

#else // !__cplusplus

#define EXTERN_C extern
#ifndef _XBOX
// C mode on desktop: bool/true/false come from <stdbool.h> via sdl_platform.h
#else
#define bool BOOL
#define true TRUE
#define false FALSE
#endif
#define inline _inline

#endif // __cplusplus

#ifndef countof
#define countof(n) (sizeof (n) / sizeof (n[0]))
#endif

#ifndef CopyChars
#define CopyChars(dest, src, count) CopyMemory(dest, src, (count) * sizeof (TCHAR))
#endif

// Node property offset -- uses 'this' so it only works inside member functions.
// Both platforms need this for the XAP property system.
// Conflicts with the standard offsetof(type, member) from <cstddef>.
#undef offsetof
#define offsetof(member) (((int)(intptr_t)&member) - (int)(intptr_t)this)

// Safe pointer-to-int cast. Xbox is 32-bit so (int)ptr works. On 64-bit
// desktop we need to go through intptr_t to avoid the clang error.
#ifdef _XBOX
#define PTR2INT(p) ((int)(p))
#else
#define PTR2INT(p) ((int)(intptr_t)(p))
#endif

#ifdef _UNICODE
extern void Unicode(TCHAR* wsz, const char* sz, int nMaxChars);
extern void Ansi(char* sz, const TCHAR* wsz, int nMaxChars);
#endif

// =========================================================================
// Debug infrastructure
// =========================================================================

#undef ASSERT
#undef VERIFY
#undef ASSERTHR
#undef VERIFYHR
#undef TRACE
#undef ALERT

#if defined(_XBOX) && defined(_DEBUG)
// Xbox debug: kernel assert + CRT debug break
#define ASSERT(f)       if (!(f)) RtlAssert((PVOID)#f, (PVOID)__FILE__, __LINE__, NULL)
#define VERIFY(f)       ASSERT(f)
#define ASSERTHR(f)     do { HRESULT hrverify = (f); if (FAILED(hrverify) && AssertFailed(_T(__FILE__), __LINE__, hrverify)) _CrtDbgBreak(); } while (0)
#define VERIFYHR(f)     ASSERTHR(f)
#define TRACE           Trace
#define ALERT           Alert

EXTERN_C bool AssertFailed(const TCHAR* szFile, int nLine, HRESULT hr);
EXTERN_C void Trace(const TCHAR* szMsg, ...);

#else
// Release (Xbox) and all desktop builds: no-op asserts, silent trace
#define ASSERT(f)       ((void)0)
#define VERIFY(f)       ((void)(f))
#define ASSERTHR(f)     ((void)0)
#define VERIFYHR(f)     ((void)(f))
#define TRACE           1 ? (void)0 : Trace
#define ALERT           1 ? (void)0 : Alert

inline void Trace(const TCHAR* szMsg, ...) { }

#endif

EXTERN_C void Alert(const TCHAR* szMsg, ...);

// =========================================================================
// Xbox-specific extras
// =========================================================================

#ifdef _XBOX
#include "xprofp.h"

#ifdef _PROFILE
#define START_PROFILE() XProfpControl(XPROF_START, 0)
#define END_PROFILE()   XProfpControl(XPROF_STOP, 0)
#else
#define START_PROFILE()
#define END_PROFILE()
#endif

typedef PVOID HDEVNOTIFY;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;
typedef RGBQUAD FAR* LPRGBQUAD;

#define MAX_COMPUTERNAME_LENGTH 15

#else // !_XBOX

#define START_PROFILE()
#define END_PROFILE()

// Desktop: DbgPrint goes to stderr
#ifndef DbgPrint
#define DbgPrint(...) fprintf(stderr, __VA_ARGS__)
#endif

#endif // _XBOX
