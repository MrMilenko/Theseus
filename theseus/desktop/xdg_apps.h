// xdg_apps.h: Linux .desktop entries as launcher games. Reads the same
// folders an app menu does (yours, the system's, Flatpak and snap exports)
// plus the Desktop, and finds each one's icon in the icon themes.

#pragma once

#include <string>
#include <vector>

struct XdgApp {
	std::string name, exec, icon, path;   // path is the .desktop file
	std::string workdir;                  // its Path, where it wants to start
	bool game;                            // Categories has Game
};

// The folders to read, yours first, so your copy of an entry wins.
void XdgApps_Dirs(std::vector<std::string>& out);

// One .desktop file. False when it isn't something you can launch: not an
// Application, hidden, terminal only, or no Exec.
bool XdgApps_Parse(const char* file, XdgApp* out);

// Every launchable entry under the folders, games first, then A to Z.
void XdgApps_Scan(const std::vector<std::string>& dirs, std::vector<XdgApp>& out);

// Exec as a shell command: field codes filled in or dropped, every
// argument quoted the way sh wants it.
std::string XdgApps_Command(const XdgApp& app);

// A picture file for the entry's Icon, from the hicolor theme or pixmaps.
// Empty when there's none we can draw (SVG only, say).
std::string XdgApps_IconFile(const std::string& icon);

// Into the library under category, with its icon. False when a game of that
// name or command is already there. The caller saves.
bool XdgApps_Add(const XdgApp& app, const char* category);
