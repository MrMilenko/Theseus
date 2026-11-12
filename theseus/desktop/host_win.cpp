// host_win.cpp: host.h for Windows. The one place the desktop build talks
// to kernel32 for files. Never include std.h or platform_shim.h here.

#include "host.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Win32 takes '/' almost everywhere, but not everywhere. Hand it '\'.
static void ToWin(char* dst, size_t size, const char* src) {
	snprintf(dst, size, "%s", src);
	for (char* p = dst; *p; p++) if (*p == '/') *p = '\\';
}

// 100ns ticks since 1601 to seconds since 1970.
static int64_t ToUnix(const FILETIME& ft) {
	uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return (int64_t)(t / 10000000ULL) - 11644473600LL;
}

static uint32_t ToHostAttrs(DWORD a) {
	uint32_t r = 0;
	if (a & FILE_ATTRIBUTE_DIRECTORY) r |= HOST_ATTR_DIR;
	if (a & FILE_ATTRIBUTE_HIDDEN)    r |= HOST_ATTR_HIDDEN;
	if (a & FILE_ATTRIBUTE_SYSTEM)    r |= HOST_ATTR_SYSTEM;
	if (a & FILE_ATTRIBUTE_READONLY)  r |= HOST_ATTR_READONLY;
	return r;
}

bool Host_MakeDir(const char* path) {
	char p[MAX_PATH];
	ToWin(p, sizeof(p), path);
	return CreateDirectoryA(p, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool Host_RemoveDir(const char* path) {
	char p[MAX_PATH];
	ToWin(p, sizeof(p), path);
	return RemoveDirectoryA(p) != FALSE;
}

bool Host_RemoveFile(const char* path) {
	char p[MAX_PATH];
	ToWin(p, sizeof(p), path);
	return DeleteFileA(p) != FALSE;
}

bool Host_Stat(const char* path, HostFileInfo* out) {
	char p[MAX_PATH];
	ToWin(p, sizeof(p), path);
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesExA(p, GetFileExInfoStandard, &fad)) return false;
	if (out) {
		out->attrs = ToHostAttrs(fad.dwFileAttributes);
		out->size  = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
		out->mtime = ToUnix(fad.ftLastWriteTime);
	}
	return true;
}

bool Host_StatOpenFile(FILE* f, HostFileInfo* out) {
	struct _stat64 st;
	if (!f || _fstat64(_fileno(f), &st) != 0) return false;
	if (out) {
		out->attrs = (st.st_mode & _S_IFDIR) ? HOST_ATTR_DIR : 0;
		out->size  = (uint64_t)st.st_size;
		out->mtime = (int64_t)st.st_mtime;
	}
	return true;
}

bool Host_SetAttrs(const char* path, uint32_t attrs) {
	char p[MAX_PATH];
	ToWin(p, sizeof(p), path);
	DWORD a = 0;
	if (attrs & HOST_ATTR_HIDDEN)   a |= FILE_ATTRIBUTE_HIDDEN;
	if (attrs & HOST_ATTR_SYSTEM)   a |= FILE_ATTRIBUTE_SYSTEM;
	if (attrs & HOST_ATTR_READONLY) a |= FILE_ATTRIBUTE_READONLY;
	return SetFileAttributesA(p, a ? a : FILE_ATTRIBUTE_NORMAL) != FALSE;
}

// The CRT's rename() won't replace an existing file.
bool Host_RenameReplace(const char* from, const char* to) {
	char f[MAX_PATH], t[MAX_PATH];
	ToWin(f, sizeof(f), from);
	ToWin(t, sizeof(t), to);
	return MoveFileExA(f, t, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE;
}

bool Host_CopyFile(const char* from, const char* to, bool failIfExists) {
	char f[MAX_PATH], t[MAX_PATH];
	ToWin(f, sizeof(f), from);
	ToWin(t, sizeof(t), to);
	return CopyFileA(f, t, failIfExists) != FALSE;
}

bool Host_DiskSpace(const char* path, uint64_t* freeBytes, uint64_t* totalBytes) {
	char p[MAX_PATH];
	ToWin(p, sizeof(p), path);
	ULARGE_INTEGER avail, total;
	if (!GetDiskFreeSpaceExA(p, &avail, &total, NULL)) return false;
	if (freeBytes)  *freeBytes  = avail.QuadPart;
	if (totalBytes) *totalBytes = total.QuadPart;
	return true;
}

// The CRT's tmpfile() wants the drive root, which a normal user can't write.
// "D" is the CRT's delete-on-close flag.
FILE* Host_TempFile() {
	char dir[MAX_PATH], path[MAX_PATH];
	DWORD len = GetTempPathA(MAX_PATH, dir);
	if (len == 0 || len > MAX_PATH) return NULL;
	if (GetTempFileNameA(dir, "ths", 0, path) == 0) return NULL;
	return fopen(path, "w+bD");
}

int64_t Host_Seek(FILE* f, int64_t offset, int whence) {
	if (!f || _fseeki64(f, offset, whence) != 0) return -1;
	return _ftelli64(f);
}

struct HostDir {
	HANDLE find;
	WIN32_FIND_DATAA first;
	bool haveFirst;
};

HostDir* Host_OpenDir(const char* path) {
	char p[MAX_PATH];
	char pattern[MAX_PATH + 4];
	ToWin(p, sizeof(p), path);
	snprintf(pattern, sizeof(pattern), "%s\\*", p);
	HostDir* dir = (HostDir*)calloc(1, sizeof(HostDir));
	if (!dir) return NULL;
	dir->find = FindFirstFileA(pattern, &dir->first);
	if (dir->find == INVALID_HANDLE_VALUE) { free(dir); return NULL; }
	dir->haveFirst = true;
	return dir;
}

bool Host_ReadDir(HostDir* dir, char* name, size_t nameSize, HostFileInfo* info) {
	if (!dir) return false;
	WIN32_FIND_DATAA fd;
	if (dir->haveFirst) {
		fd = dir->first;
		dir->haveFirst = false;
	} else if (!FindNextFileA(dir->find, &fd)) {
		return false;
	}
	snprintf(name, nameSize, "%s", fd.cFileName);
	if (info) {
		info->attrs = ToHostAttrs(fd.dwFileAttributes);
		info->size  = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
		info->mtime = ToUnix(fd.ftLastWriteTime);
	}
	return true;
}

void Host_CloseDir(HostDir* dir) {
	if (!dir) return;
	FindClose(dir->find);
	free(dir);
}

bool Host_MemoryStatus(uint64_t* totalBytes, uint64_t* availBytes) {
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);
	if (!GlobalMemoryStatusEx(&ms)) return false;
	if (totalBytes) *totalBytes = ms.ullTotalPhys;
	if (availBytes) *availBytes = ms.ullAvailPhys;
	return true;
}

// Whatever the host name resolves to first. No netmask from this route.
uint32_t Host_LocalIPv4(uint32_t* netmask) {
	uint32_t addr = 0;
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) == 0) {
		struct hostent* he = gethostbyname(hostname);
		if (he && he->h_addr_list[0]) memcpy(&addr, he->h_addr_list[0], 4);
	}
	if (netmask) *netmask = 0;
	return addr;
}
