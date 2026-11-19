// pcsx2.h: PCSX2 launcher module. PS2 discs are stored as a bare
// `pcsx2 ...` line and the PCSX2 install is looked up at launch, so moving
// or updating PCSX2 never breaks a game.

#pragma once

#include <stddef.h>

// desktop.ini [Desktop] Pcsx2Path. Empty means find it at launch.
extern char s_pcsx2Path[512];

// Where PCSX2 is installed: PATH, the usual install folders, RetroBat, scoop,
// Flatpak, Lutris and AppImages. On macOS it's the .app. False when it's nowhere.
bool Pcsx2_Find(char* out, size_t outSize);

// A disc image PCSX2 can boot, by extension. An .iso can be an Xbox disc
// too, so check for that first.
bool Pcsx2_IsDisc(const char* path);

// The games.ini Launch line for a PS2 disc. False when it won't fit.
bool Pcsx2_DiscLaunch(const char* disc, char* out, size_t outSize);
