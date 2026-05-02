// stdcfg.h: desktop build configuration. Counterpart to
// engine/stdcfg.h on Xbox; platform detection uses __APPLE__,
// __linux__, _WIN32.

#define UIX_DESKTOP 1

// Desktop uses UTF-8 char strings (SDL2, POSIX, OpenGL are all char*-based).
// Xbox used UTF-16LE via _UNICODE/TCHAR. That path has been removed.

// Debug infrastructure; TRACE() and ASSERT() are always available.
// No _DEBUG gating; desktop debug tools are runtime-toggled (F1 inspector, etc.)
#define _LOG

// Match the Xbox build's lighting feature flag so shared/theseus.h's
// TheseusSetLight / TheseusLightEnable inlines are visible.
#ifndef _LIGHTS
#define _LIGHTS
#endif
