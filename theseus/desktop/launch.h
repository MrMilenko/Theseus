// launch.h: desktop launch dispatcher public API. Cross-platform
// launch dispatcher for arbitrary user-supplied launch specs (URLs,
// file paths, executable paths, full command lines). Companion to
// desktop/launch.cpp.

#pragma once
// Two entry points:
//
//   DesktopLaunch(spec)
//     Fire-and-forget. Used by Title Maker's "Test Launch" button and
//     anywhere else we want to spawn something without tearing down the
//     dashboard.
//
//   DesktopLaunchGame(spec)
//     Full game-launch pipeline: hide the SDL window, fork and wait for
//     the child to exit, then exec the dashboard back so we come up
//     clean. Used by .uixshortcut and virtual-game launches from the
//     scene graph.
//
// Both share the same dispatch rules:
//
//   - "steam://..."   on Linux: invoke the `steam` binary directly so we
//                     bypass KIO. KDE+Plasma's URL chain often has no
//                     registered handler for the steam scheme, which
//                     causes "Unknown protocol 'steam'" errors.
//                     macOS / Windows: fall through to the system URL
//                     opener -- it works there.
//   - "<scheme>://..." system URL opener (xdg-open / open / ShellExecute)
//   - anything else   pass to the shell as a command line, so the user
//                     can paste a raw executable path, a path with args,
//                     a bash script, or a multi-token command.

void DesktopLaunch(const char* spec);
void DesktopLaunchGame(const char* spec);

// Bridge from the XAP `launch()` built-in to DesktopLaunchGame on desktop.
// Resolves an Xbox-style path (drive-letter or \Device\Harddisk0\Partition*)
// to a real launch command via games.ini (primary) or default.uixshortcut
// (fallback), then dispatches to DesktopLaunchGame.
void DesktopLaunchTitle(const char* devicePath);
