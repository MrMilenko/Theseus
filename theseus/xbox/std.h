// std.h: the Xbox build's prelude. Every Xbox .cpp includes this first;
// it sets up the XDK kernel + XTL, CRT headers, D3D8 types and debug
// infrastructure. The desktop has its own in desktop/std.h.

// Check Configuration Options
#include "platform.h"
#ifdef THESEUS_DESKTOP
#error "xbox/std.h is the Xbox prelude. Desktop code includes desktop/std.h."
#endif
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

#include <init.h>
#include <xtl.h>
#include <xgraphics.h>
#include "xbox_private_types.h"

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

#endif // _XBOX

// =========================================================================
// Shared macros and utilities (all platforms)
// =========================================================================

#define BREAKONFAIL(a,b)  {if(FAILED(a)) {DbgPrint(b);break;} }

#ifdef __cplusplus

#define EXTERN_C extern "C"

// Xbox debug CRT overrides operator new for leak tracking.
// Desktop doesn't use this, it has its own tooling.
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

// Node property offset: uses 'this' so it only works inside member functions.
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

// CHECK survives retail, unlike ASSERT. Use it where failing means bad memory.
#define CHECK(f)        TheseusCheck(!!(f), #f, __FILE__, __LINE__)
inline bool TheseusCheck(bool ok, const char* expr, const char* file, int line)
{
	if (!ok)
		Trace(_T("\001CHECK failed %hs:%d: %hs\n"), file, line, expr);
	return ok;
}

#else
// Release (Xbox) and all desktop builds: no-op asserts, silent trace
#define ASSERT(f)       ((void)0)
#define VERIFY(f)       ((void)(f))
#define ASSERTHR(f)     ((void)0)
#define VERIFYHR(f)     ((void)(f))
#define TRACE           1 ? (void)0 : Trace
#define ALERT           1 ? (void)0 : Alert

inline void Trace(const TCHAR* szMsg, ...) { }

#define CHECK(f)        (!!(f))

#endif

EXTERN_C void Alert(const TCHAR* szMsg, ...);

// =========================================================================
// Xbox-specific extras
// =========================================================================

#ifdef _XBOX

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
