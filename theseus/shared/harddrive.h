// harddrive.h: drive / filesystem inspection helpers used by the
// memory pane. Free-space queries in bytes and Xbox blocks. Inherited
// from the public XDK Installer sample.

#pragma once

bool FolderExists(const char* path);
bool FileExists(const char* path);
bool GetFreeSpaceInBytes(const char* drive, ULONGLONG& totalBytes, ULONGLONG& freeBytes);
bool GetFreeSpaceInBlocks(const char* drive, DWORD& totalBlocks, DWORD& freeBlocks);
