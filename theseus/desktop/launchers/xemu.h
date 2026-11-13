// xemu.h: xemu launcher module. ISO entries are stored as `xemu ...` when
// no xemu path is set; this finds a Flatpak or snap xemu at launch.

#pragma once

#include <stddef.h>

// Flatpak xemu (app.xemu.xemu), system or user install.
bool Xemu_FlatpakInstalled();
