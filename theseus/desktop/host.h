// host.h: the narrow door from the desktop build to the host OS. Plain
// types only, so it never drags an OS header into Xbox-shaped code.
// host_posix.cpp (mac, Linux) and host_win.cpp implement it.
//
// Every path here is a host path with '/' separators. Xbox paths go
// through xbox_path.h first.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
	HOST_ATTR_DIR      = 0x01,
	HOST_ATTR_HIDDEN   = 0x02,
	HOST_ATTR_SYSTEM   = 0x04,
	HOST_ATTR_READONLY = 0x08,
};

struct HostFileInfo {
	uint32_t attrs;    // HOST_ATTR_*
	uint64_t size;
	int64_t  mtime;    // seconds since 1970, UTC
};

// The separator native paths show the user.
extern const char Host_PathSep;

// Files and folders. Host_MakeDir is true when the folder exists afterwards.
bool Host_MakeDir(const char* path);
bool Host_RemoveDir(const char* path);
bool Host_RemoveFile(const char* path);
bool Host_Stat(const char* path, HostFileInfo* out);
bool Host_StatOpenFile(FILE* f, HostFileInfo* out);
bool Host_SetAttrs(const char* path, uint32_t attrs);
bool Host_RenameReplace(const char* from, const char* to);
// Pushes a closed file's data all the way to disk, so a rename after it can't land first.
bool Host_FlushToDisk(const char* path);
bool Host_CopyFile(const char* from, const char* to, bool failIfExists);
bool Host_DiskSpace(const char* path, uint64_t* freeBytes, uint64_t* totalBytes);

// An empty read/write file that's deleted when closed.
FILE* Host_TempFile();

// 64-bit seek on an open file. Returns the new position, or -1.
int64_t Host_Seek(FILE* f, int64_t offset, int whence);

// Folder listing. Hands back every entry, "." and ".." included.
struct HostDir;
HostDir* Host_OpenDir(const char* path);
bool Host_ReadDir(HostDir* dir, char* name, size_t nameSize, HostFileInfo* info);
// Name and folder-or-not only. Skips the per-entry stat when the OS already
// knows the type, which matters on a network share. Entries that can't be
// looked at are skipped.
bool Host_ReadDirType(HostDir* dir, char* name, size_t nameSize, bool* isDir);
void Host_CloseDir(HostDir* dir);

// The machine itself.
bool Host_MemoryStatus(uint64_t* totalBytes, uint64_t* availBytes);
uint32_t Host_LocalIPv4(uint32_t* netmask);   // network order, 0 when there's none

// Process plumbing for main() and the launcher. host_win_app.cpp and
// host_posix_app.cpp.
struct SDL_Window;

// Windows only: attach to the parent shell's console, or with none (an
// Explorer launch) send stdout and stderr to logPath. No-op elsewhere.
void Host_ConsoleOrLog(const char* logPath);
// Ask the OS for game-like scheduling. No-op where there's nothing to ask.
void Host_BoostProcess();
// Each crash writes crashes/<time>.txt under userDataDir. shippedDir holds
// Configs/version.
void Host_InstallCrashHandler(const char* userDataDir, const char* shippedDir);
// What bgfx needs to draw into the SDL window. ndt is the display
// connection (X11/Wayland), NULL elsewhere.
bool Host_NativeWindow(SDL_Window* window, void** nwh, void** ndt, bool* wayland);
// Fire and forget: a URL goes to the OS handler, anything else runs as a
// shell command from workdir (may be NULL). err gets a line on failure.
bool Host_Spawn(const char* spec, bool isUrl, bool isSteamUrl, const char* workdir,
                char* err, size_t errSize);
// Native "open file" dialog. False on cancel or when there isn't one.
bool Host_PickFile(const char* title, char* out, size_t outSize);
// Full path of the running executable.
bool Host_ExePath(char* out, size_t outSize);
// Show a folder in the OS file manager. Doesn't wait.
void Host_OpenFolder(const char* dir);
// A string value under HKEY_CURRENT_USER. Always false off Windows.
bool Host_RegistryString(const char* subKey, const char* value, char* out, size_t outSize);
// Audio CD in the first optical drive: track count and per-track lengths
// in seconds. -1 when there's no readable disc.
int Host_CdReadToc(int* durations, int maxTracks, bool* allAudio);

// "macOS", "Windows" or "Linux", for people and servers that ask.
const char* Host_Name();
// The bgfx shader folder to try when the renderer doesn't say.
const char* Host_DefaultShaderDir();
// Where the user's Configs and Library live, and where the shipped Data
// sits relative to the executable's folder.
void Host_UserDataDir(char* out, size_t outSize);
void Host_ShippedDir(const char* exeDir, char* out, size_t outSize);
