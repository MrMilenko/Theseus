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

// Files and folders. Host_MakeDir is true when the folder exists afterwards.
bool Host_MakeDir(const char* path);
bool Host_RemoveDir(const char* path);
bool Host_RemoveFile(const char* path);
bool Host_Stat(const char* path, HostFileInfo* out);
bool Host_StatOpenFile(FILE* f, HostFileInfo* out);
bool Host_SetAttrs(const char* path, uint32_t attrs);
bool Host_RenameReplace(const char* from, const char* to);
bool Host_CopyFile(const char* from, const char* to, bool failIfExists);
bool Host_DiskSpace(const char* path, uint64_t* freeBytes, uint64_t* totalBytes);

// Folder listing. Hands back every entry, "." and ".." included.
struct HostDir;
HostDir* Host_OpenDir(const char* path);
bool Host_ReadDir(HostDir* dir, char* name, size_t nameSize, HostFileInfo* info);
void Host_CloseDir(HostDir* dir);
