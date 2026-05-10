// retroarch.h: RetroArch launcher module. Discovery side; the
// launcher contract (Claims/Build) registers in retroarch.cpp.

#pragma once

#include <stddef.h>

// Find RetroArch install roots. userOverride from desktop.ini
// [Desktop] RetroArchPath= wins when set. Returns the count.
int RetroArch_DiscoverInstall(const char* userOverride,
                               char outRoots[][512], int maxRoots);

// List core filenames (e.g. "ppsspp_libretro.dll") found in
// <installRoot>/cores/. Returns the count.
int RetroArch_EnumerateCores(const char* installRoot,
                              char outCores[][256], int maxCores);
