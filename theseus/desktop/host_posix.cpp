// host_posix.cpp: host.h for macOS and Linux.

#include "host.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static void FillInfo(const struct stat& st, HostFileInfo* out) {
	out->attrs = S_ISDIR(st.st_mode) ? HOST_ATTR_DIR : 0;
	out->size  = (uint64_t)st.st_size;
	out->mtime = (int64_t)st.st_mtime;
}

bool Host_MakeDir(const char* path) {
	return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool Host_RemoveDir(const char* path)  { return rmdir(path) == 0; }
bool Host_RemoveFile(const char* path) { return remove(path) == 0; }

bool Host_Stat(const char* path, HostFileInfo* out) {
	struct stat st;
	if (stat(path, &st) != 0) return false;
	if (out) FillInfo(st, out);
	return true;
}

bool Host_StatOpenFile(FILE* f, HostFileInfo* out) {
	struct stat st;
	if (!f || fstat(fileno(f), &st) != 0) return false;
	if (out) FillInfo(st, out);
	return true;
}

// FATX attribute bits have no POSIX meaning worth keeping.
bool Host_SetAttrs(const char*, uint32_t) { return true; }

bool Host_RenameReplace(const char* from, const char* to) {
	return rename(from, to) == 0;
}

bool Host_CopyFile(const char* from, const char* to, bool failIfExists) {
	struct stat st;
	if (failIfExists && stat(to, &st) == 0) return false;
	FILE* in = fopen(from, "rb");
	if (!in) return false;
	FILE* out = fopen(to, "wb");
	if (!out) { fclose(in); return false; }
	char buf[64 * 1024];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	fclose(in);
	if (fclose(out) != 0) ok = false;
	return ok;
}

bool Host_DiskSpace(const char* path, uint64_t* freeBytes, uint64_t* totalBytes) {
	struct statvfs vfs;
	if (statvfs(path, &vfs) != 0) return false;
	if (freeBytes)  *freeBytes  = (uint64_t)vfs.f_bavail * vfs.f_frsize;
	if (totalBytes) *totalBytes = (uint64_t)vfs.f_blocks * vfs.f_frsize;
	return true;
}

struct HostDir {
	DIR* d;
	char path[512];
};

HostDir* Host_OpenDir(const char* path) {
	DIR* d = opendir(path);
	if (!d) return NULL;
	HostDir* dir = (HostDir*)calloc(1, sizeof(HostDir));
	if (!dir) { closedir(d); return NULL; }
	dir->d = d;
	snprintf(dir->path, sizeof(dir->path), "%s", path);
	return dir;
}

bool Host_ReadDir(HostDir* dir, char* name, size_t nameSize, HostFileInfo* info) {
	if (!dir) return false;
	struct dirent* e = readdir(dir->d);
	if (!e) return false;
	snprintf(name, nameSize, "%s", e->d_name);
	if (info) {
		memset(info, 0, sizeof(*info));
		char full[768];
		snprintf(full, sizeof(full), "%s/%s", dir->path, e->d_name);
		Host_Stat(full, info);
	}
	return true;
}

void Host_CloseDir(HostDir* dir) {
	if (!dir) return;
	closedir(dir->d);
	free(dir);
}
