// steam.h: Steam install discovery API. Title Maker's "Import Steam
// Library" button walks this to find every steamapps/ directory the
// user has on disk, then scans appmanifest_*.acf inside each to
// enumerate installed games.

#pragma once

#include <stddef.h>

// Path discovery returns count of resolved directories. Buffers are
// fixed-size to keep the API allocator-free; the cap is set by the
// module to a value generous enough for any sane Steam setup
// (multiple library drives, custom installs, Flatpak coexisting
// with native).

// Common Steam install locations across platforms. Includes the
// user-configured override from desktop.ini's [Desktop] SteamPath
// (passed in as `userOverride`, may be empty), plus Windows
// registry-detected install path on Win, Flatpak and Debian
// variants on Linux, etc. Each entry is the Steam *root* (the
// directory containing steamapps/), not steamapps/ itself.
//
// Returns the count written into `outRoots` (each a NUL-terminated
// path), capped at `maxRoots`.
int Steam_DiscoverInstallRoots(const char* userOverride,
                                char outRoots[][512], int maxRoots);

// Walk every Steam root from Steam_DiscoverInstallRoots and parse
// each root's steamapps/libraryfolders.vdf to enumerate every
// library folder Steam knows about (covers users with games
// installed on extra drives, e.g. D:\MikesSteamLibrary). Returns
// each library's *steamapps* dir, deduplicated. Append the root's
// own steamapps even if libraryfolders.vdf is missing/old, so a
// freshly-installed Steam with one library still works.
//
// Returns the count written into `outLibraries`, capped at maxLibs.
int Steam_DiscoverLibraries(const char* userOverride,
                             char outLibraries[][512], int maxLibs);

// The app Steam says is running: 0 for none, -1 when Steam's state can't be
// read (not installed, or not started yet this session).
int Steam_RunningAppId();
