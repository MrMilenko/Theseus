// stdcfg.h: build-time configuration switches and platform-define
// consistency checks. Pulled in early by std.h so the rest of the tree
// can rely on _UNICODE / _XBOX / DEBUG / etc. being normalized.

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef _LIGHTS
#define _LIGHTS
#endif

#if defined(DEBUG) && !defined(_DEBUG)
#define _DEBUG
#endif
#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG
#endif

#if defined(XBOX) && !defined(_XBOX)
#define _XBOX
#endif
#if defined(_XBOX) && !defined(XBOX)
#define XBOX
#endif

#if defined(UNICODE) && !defined(_UNICODE)
#define _UNICODE
#endif
#if defined(_UNICODE) && !defined(UNICODE)
#define UNICODE
#endif

#ifdef _XBOX
#ifndef _UNICODE
#error "Xbox build must use Unicode!"
#endif
#endif

#ifdef _WINDOWS
#define _LOG
#define _JPEG
//#define _LAN
#endif

#ifdef _PROFILE
#undef _DEBUG
#endif
