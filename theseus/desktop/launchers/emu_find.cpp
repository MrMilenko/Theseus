// emu_find.cpp: walks the launcher modules' lists of install folders.

#include "emu_find.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>

#include "host.h"
#include "platform_env.h"

namespace {

bool IsFile(const char* path) {
	HostFileInfo info;
	return Host_Stat(path, &info);
}

// One list entry with its fill-ins done. False when a variable it needs isn't set.
bool Expand(const char* place, char drive, char* out, size_t outSize) {
	if (outSize) out[0] = '\0';
	std::string s;
	const char* p = place;
	if (p[0] == '~' && (p[1] == '/' || p[1] == '\\' || !p[1])) {
		const char* home = PlatformHomeDir();
		if (!*home) return false;
		s = home;
		p++;
	} else if (p[0] == '$') {
		const char* end = p + 1;
		while (*end && *end != '/' && *end != '\\') end++;
		std::string name(p + 1, end - p - 1);
		const char* v = getenv(name.c_str());
		if (!v || !*v) return false;
		s = v;
		p = end;
	} else if (p[0] == '?' && p[1] == ':') {
		s = drive;
		p++;
	}
	s += p;
	if (s.size() >= outSize) return false;
	memcpy(out, s.c_str(), s.size() + 1);
	return true;
}

} // namespace

bool Emu_FirstExisting(const char* const* places, char* out, size_t outSize) {
	char buf[1024];
	for (int i = 0; places && places[i]; i++) {
		bool anyDrive = places[i][0] == '?';
		for (char d = 'C'; d <= (anyDrive ? 'Z' : 'C'); d++) {
			if (!Expand(places[i], d, buf, sizeof(buf)) || !IsFile(buf)) continue;
			snprintf(out, outSize, "%s", buf);
			return true;
		}
	}
	return false;
}

bool Emu_OnPath(const char* exe, char* out, size_t outSize) {
	const char* path = getenv("PATH");
	if (!path || !exe || !*exe) return false;
	// Windows lists PATH with ';' because its paths carry a ':'.
	char listSep = Host_PathSep == '\\' ? ';' : ':';
	char buf[1024];
	while (*path) {
		const char* end = strchr(path, listSep);
		size_t n = end ? (size_t)(end - path) : strlen(path);
		if (n > 0 && n + strlen(exe) + 2 < sizeof(buf)) {
			memcpy(buf, path, n);
			snprintf(buf + n, sizeof(buf) - n, "%c%s", Host_PathSep, exe);
			HostFileInfo info;
			if (Host_Stat(buf, &info) && !(info.attrs & HOST_ATTR_DIR)) {
				snprintf(out, outSize, "%s", buf);
				return true;
			}
		}
		if (!end) break;
		path = end + 1;
	}
	return false;
}

bool Emu_FindAppImage(const char* const* dirs, const char* prefix, char* out, size_t outSize) {
	size_t plen = strlen(prefix);
	char dir[1024], name[512];
	for (int i = 0; dirs && dirs[i]; i++) {
		if (!Expand(dirs[i], 'C', dir, sizeof(dir))) continue;
		HostDir* d = Host_OpenDir(dir);
		if (!d) continue;
		bool isDir = false, found = false;
		while (!found && Host_ReadDirType(d, name, sizeof(name), &isDir)) {
			size_t n = strlen(name);
			if (isDir || n < plen + 9) continue;
			if (strncasecmp(name, prefix, plen) != 0) continue;
			if (strcasecmp(name + n - 9, ".AppImage") != 0) continue;
			snprintf(out, outSize, "%s/%s", dir, name);
			found = true;
		}
		Host_CloseDir(d);
		if (found) return true;
	}
	return false;
}

void Emu_AppBinary(const char* path, const char* bin, char* out, size_t outSize) {
	size_t n = strlen(path);
	while (n > 1 && path[n - 1] == '/') n--;
	if (n > 4 && strncasecmp(path + n - 4, ".app", 4) == 0)
		snprintf(out, outSize, "%.*s/Contents/MacOS/%s", (int)n, path, bin);
	else
		snprintf(out, outSize, "%s", path);
}
