// platform.h: which build this is. First thing both std.h files include.
//
// Every build is exactly one target: _XBOX (the XBE) or THESEUS_DESKTOP.
// A desktop build also gets exactly one host OS. Don't test _WIN32 to mean
// either one: it's true on the Xbox build too.

#pragma once

#if defined(_XBOX)
#  if defined(THESEUS_DESKTOP)
#    error "_XBOX and THESEUS_DESKTOP are both set. Pick one target."
#  endif
#  if defined(THESEUS_HOST_WIN) || defined(THESEUS_HOST_MAC) || defined(THESEUS_HOST_LINUX)
#    error "The Xbox build has no host OS."
#  endif
#else
#  ifndef THESEUS_DESKTOP
#    define THESEUS_DESKTOP 1
#  endif
#  if defined(_WIN32)
#    define THESEUS_HOST_WIN 1
     // mingw links the Microsoft C runtime: _stricmp and friends are real.
#    define THESEUS_CRT_MS 1
#  elif defined(__APPLE__)
#    define THESEUS_HOST_MAC 1
#  elif defined(__linux__)
#    define THESEUS_HOST_LINUX 1
#  else
#    error "Unknown desktop host OS. Theseus builds for Windows, macOS and Linux."
#  endif
#endif
