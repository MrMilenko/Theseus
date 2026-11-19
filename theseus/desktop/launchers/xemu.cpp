// xemu.cpp: xemu launcher module. Title Maker stores an ISO as a bare
// `xemu -machine ... -dvd_path "..."` when no xemu path is set. Flatpak
// xemu never puts xemu on PATH and Windows installs rarely do, so a bare
// xemu that PATH can't find is pointed at whichever install Xemu_Find
// turns up. Flatpak xemu can read the whole disk, so the ISO path works.

#include "launcher.h"
#include "xemu.h"
#include "emu_find.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform.h"
#include "host.h"

extern char s_xemuPath[512];

namespace {

#ifdef THESEUS_HOST_WIN
const char* kExe = "xemu.exe";
const char* const kPlaces[] = {
	"$ProgramFiles\\xemu\\xemu.exe",
	"$ProgramFiles(x86)\\xemu\\xemu.exe",
	"$LOCALAPPDATA\\Programs\\xemu\\xemu.exe",
	"$LOCALAPPDATA\\xemu\\xemu.exe",
	"~\\scoop\\apps\\xemu\\current\\xemu.exe",
	"$APPDATA\\EmuDeck\\Emulators\\xemu\\xemu.exe",
	"?:\\RetroBat\\emulators\\xemu\\xemu.exe",
	"?:\\LaunchBox\\Emulators\\xemu\\xemu.exe",
	"?:\\Emulators\\xemu\\xemu.exe",
	"?:\\xemu\\xemu.exe",
	NULL
};
const char* const kAppImageDirs[] = { NULL };
#elif defined(THESEUS_HOST_MAC)
const char* kExe = "xemu";
const char* const kPlaces[] = {
	"/Applications/xemu.app",
	"~/Applications/xemu.app",
	"/opt/homebrew/bin/xemu",
	"/usr/local/bin/xemu",
	NULL
};
const char* const kAppImageDirs[] = { NULL };
#else
const char* kExe = "xemu";
const char* const kPlaces[] = {
	"/usr/bin/xemu",
	"/usr/local/bin/xemu",
	"~/.local/bin/xemu",
	"/snap/bin/xemu",
	"/var/lib/flatpak/exports/bin/app.xemu.xemu",
	"~/.local/share/flatpak/exports/bin/app.xemu.xemu",
	NULL
};
const char* const kAppImageDirs[] = {
	"~/Applications", "~/AppImages", "~/.local/bin", "~/Desktop", "~/Downloads", "/opt", NULL
};
#endif

bool Exists(const char* path) {
	HostFileInfo info;
	return Host_Stat(path, &info);
}

// The text after a leading bare "xemu", or NULL when the spec isn't one.
const char* AfterBareXemu(const char* spec) {
	if (!spec) return NULL;
	while (*spec == ' ' || *spec == '\t') spec++;
	if (strncmp(spec, "xemu", 4) != 0) return NULL;
	if (spec[4] != ' ' && spec[4] != '\t' && spec[4] != 0) return NULL;
	return spec + 4;
}

bool Claims(const char* spec) {
	return AfterBareXemu(spec) != NULL;
}

bool Build(const char* spec, char* outCmd, size_t outSize) {
	const char* rest = AfterBareXemu(spec);
	char found[1024];
	if (!rest || Emu_OnPath(kExe, found, sizeof(found))) return false;
	int n;
	if (Xemu_Find(found, sizeof(found))) {
		char bin[1100];
		Emu_AppBinary(found, "xemu", bin, sizeof(bin));
		n = snprintf(outCmd, outSize, "\"%s\"%s", bin, rest);
	} else if (Xemu_FlatpakInstalled()) {
		n = snprintf(outCmd, outSize, "flatpak run app.xemu.xemu%s", rest);
	} else {
		return false;
	}
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

bool Xemu_Find(char* out, size_t outSize) {
	if (outSize) out[0] = '\0';
	return Emu_OnPath(kExe, out, outSize) ||
	       Emu_FirstExisting(kPlaces, out, outSize) ||
	       Emu_FindAppImage(kAppImageDirs, "xemu", out, outSize);
}

bool Xemu_IsoLaunch(const char* iso, char* out, size_t outSize) {
	int n;
	if (s_xemuPath[0]) {
		char bin[1024];
		Emu_AppBinary(s_xemuPath, "xemu", bin, sizeof(bin));
		n = snprintf(out, outSize, "\"%s\" -machine xbox,short-animation=on -dvd_path \"%s\"", bin, iso);
	} else {
		// Bare xemu is found at launch.
		n = snprintf(out, outSize, "xemu -machine xbox,short-animation=on -dvd_path \"%s\"", iso);
	}
	return n > 0 && (size_t)n < outSize;
}

void Launcher_RegisterXemu() {
	Launcher_Register(&kXemuLauncher);
}
