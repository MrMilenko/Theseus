// emu_find.h: looking for an emulator someone installed. The launcher
// modules hold the per-OS lists of where to look; this walks them.

#pragma once

#include <stddef.h>

// The first place on the list that exists. Each entry is a path, with these
// filled in: a leading ~ is the home folder, $NAME up to the next slash is
// that environment variable, and a leading ?: tries every drive C: to Z:.
bool Emu_FirstExisting(const char* const* places, char* out, size_t outSize);

// A program by name on PATH.
bool Emu_OnPath(const char* exe, char* out, size_t outSize);

// An AppImage whose name starts with prefix (any case) in one of the folders.
// Folders use the same fill-ins as Emu_FirstExisting.
bool Emu_FindAppImage(const char* const* dirs, const char* prefix, char* out, size_t outSize);

// The program inside a macOS app bundle: /Applications/X.app gives
// /Applications/X.app/Contents/MacOS/bin. Anything else comes back as is.
void Emu_AppBinary(const char* path, const char* bin, char* out, size_t outSize);
