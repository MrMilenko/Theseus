// host_posix.cpp: host.h for macOS and Linux.

#include "host.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif

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

FILE* Host_TempFile() { return tmpfile(); }

int64_t Host_Seek(FILE* f, int64_t offset, int whence) {
	if (!f || fseeko(f, (off_t)offset, whence) != 0) return -1;
	return (int64_t)ftello(f);
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

bool Host_MemoryStatus(uint64_t* totalBytes, uint64_t* availBytes) {
	uint64_t total = 0, avail = 0;
#ifdef __APPLE__
	int mib[2] = { CTL_HW, HW_MEMSIZE };
	size_t len = sizeof(total);
	sysctl(mib, 2, &total, &len, NULL, 0);
	mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
	vm_statistics_data_t vmstat;
	if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vmstat, &count) == KERN_SUCCESS)
		avail = (uint64_t)vmstat.free_count * vm_page_size;
#else
	struct sysinfo si;
	if (sysinfo(&si) == 0) {
		total = (uint64_t)si.totalram * si.mem_unit;
		avail = (uint64_t)si.freeram * si.mem_unit;
	}
#endif
	if (totalBytes) *totalBytes = total;
	if (availBytes) *availBytes = avail;
	return total != 0;
}

// First non-loopback IPv4 interface.
uint32_t Host_LocalIPv4(uint32_t* netmask) {
	uint32_t addr = 0, mask = 0;
	struct ifaddrs* ifa = NULL;
	if (getifaddrs(&ifa) == 0) {
		for (struct ifaddrs* cur = ifa; cur; cur = cur->ifa_next) {
			if (!cur->ifa_addr || cur->ifa_addr->sa_family != AF_INET) continue;
			uint32_t a = ((struct sockaddr_in*)cur->ifa_addr)->sin_addr.s_addr;
			if ((a & 0xFF) == 127) continue;
			addr = a;
			mask = cur->ifa_netmask ? ((struct sockaddr_in*)cur->ifa_netmask)->sin_addr.s_addr : 0;
			break;
		}
		freeifaddrs(ifa);
	}
	if (netmask) *netmask = mask;
	return addr;
}
