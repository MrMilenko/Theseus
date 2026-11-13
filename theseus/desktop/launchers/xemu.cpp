// xemu.cpp: xemu launcher module. Title Maker stores an ISO as a bare
// `xemu -machine ... -dvd_path "..."` when no xemu path is set. Flatpak
// xemu never puts xemu on PATH and snap's /snap/bin isn't always on it, so
// a bare xemu that PATH can't find is pointed at whichever is installed.
// Flatpak xemu can read the whole disk, so the ISO path works. Neither
// exists off Linux, so there the spec runs as is.

#include "launcher.h"
#include "xemu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace {

bool Exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// The text after a leading bare "xemu", or NULL when the spec isn't one.
const char* AfterBareXemu(const char* spec) {
	if (!spec) return NULL;
	while (*spec == ' ' || *spec == '\t') spec++;
	if (strncmp(spec, "xemu", 4) != 0) return NULL;
	if (spec[4] != ' ' && spec[4] != '\t' && spec[4] != 0) return NULL;
	return spec + 4;
}

bool OnPath(const char* exe) {
	const char* path = getenv("PATH");
	if (!path) return false;
	char dir[1024];
	while (*path) {
		const char* end = strchr(path, ':');
		size_t n = end ? (size_t)(end - path) : strlen(path);
		if (n > 0 && n < sizeof(dir) - 32) {
			memcpy(dir, path, n);
			snprintf(dir + n, sizeof(dir) - n, "/%s", exe);
			if (Exists(dir)) return true;
		}
		if (!end) break;
		path = end + 1;
	}
	return false;
}

bool Claims(const char* spec) {
	return AfterBareXemu(spec) != NULL;
}

bool Build(const char* spec, char* outCmd, size_t outSize) {
	const char* rest = AfterBareXemu(spec);
	if (!rest || OnPath("xemu")) return false;
	const char* runner = NULL;
	if (Exists("/snap/bin/xemu"))         runner = "/snap/bin/xemu";
	else if (Xemu_FlatpakInstalled())     runner = "flatpak run app.xemu.xemu";
	if (!runner) return false;
	int n = snprintf(outCmd, outSize, "%s%s", runner, rest);
	return n > 0 && (size_t)n < outSize;
}

const Launcher kXemuLauncher = {
	"xemu",
	"xemu",
	Claims,
	300,
	Build,
};

} // namespace

bool Xemu_FlatpakInstalled() {
	if (Exists("/var/lib/flatpak/app/app.xemu.xemu")) return true;
	const char* home = getenv("HOME");
	if (!home || !*home) return false;
	char userApp[1024];
	snprintf(userApp, sizeof(userApp), "%s/.local/share/flatpak/app/app.xemu.xemu", home);
	return Exists(userApp);
}

void Launcher_RegisterXemu() {
	Launcher_Register(&kXemuLauncher);
}
