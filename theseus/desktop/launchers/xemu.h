// xemu.h: xemu launcher module. ISO entries are stored as `xemu ...` when
// no xemu path is set; this finds xemu wherever it's installed at launch.

#pragma once

#include <stddef.h>

// Flatpak xemu (app.xemu.xemu), system or user install.
bool Xemu_FlatpakInstalled();

// Where xemu is installed: PATH, the usual install folders, RetroBat, scoop,
// Flatpak, snap and AppImages. On macOS it's the .app. False when it's nowhere.
bool Xemu_Find(char* out, size_t outSize);

// The games.ini Launch line for an Xbox ISO. False when it won't fit.
bool Xemu_IsoLaunch(const char* iso, char* out, size_t outSize);
