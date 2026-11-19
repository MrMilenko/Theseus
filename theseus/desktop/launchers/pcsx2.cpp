// pcsx2.cpp: PCSX2 launcher module. Claims a bare `pcsx2 ...` line and puts
// the real program in front: the PCSX2 path setting when it's set,
// otherwise whatever Pcsx2_Find turns up. -batch makes PCSX2 quit when the
// game stops, so the dashboard comes back and the play time counts.

#include "launcher.h"
#include "pcsx2.h"
#include "emu_find.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

#include "platform.h"
#include "host.h"

char s_pcsx2Path[512] = "";

namespace {

#ifdef THESEUS_HOST_WIN
const char* const kExes[] = { "pcsx2-qt.exe", NULL };
const char* const kPlaces[] = {
	"$ProgramFiles\\PCSX2\\pcsx2-qt.exe",
	"$LOCALAPPDATA\\Programs\\PCSX2\\pcsx2-qt.exe",
	"$ProgramFiles(x86)\\PCSX2\\pcsx2-qt.exe",
	"~\\scoop\\apps\\pcsx2\\current\\pcsx2-qt.exe",
	"?:\\RetroBat\\emulators\\pcsx2\\pcsx2-qt.exe",
	"?:\\RetroBat\\emulators\\pcsx2\\pcsx2-qtx64-avx2.exe",
	"?:\\RetroBat\\emulators\\pcsx2\\pcsx2-qtx64.exe",
	"?:\\LaunchBox\\Emulators\\PCSX2\\pcsx2-qt.exe",
	"?:\\Emulators\\PCSX2\\pcsx2-qt.exe",
	"?:\\PCSX2\\pcsx2-qt.exe",
	NULL
};
const char* const kAppImageDirs[] = { NULL };
#elif defined(THESEUS_HOST_MAC)
const char* const kExes[] = { "pcsx2-qt", NULL };
const char* const kPlaces[] = {
	"/Applications/PCSX2.app",
	"~/Applications/PCSX2.app",
	NULL
};
const char* const kAppImageDirs[] = { NULL };
#else
const char* const kExes[] = { "pcsx2-qt", "pcsx2", NULL };
const char* const kPlaces[] = {
	"/var/lib/flatpak/exports/bin/net.pcsx2.PCSX2",
	"~/.local/share/flatpak/exports/bin/net.pcsx2.PCSX2",
	"/snap/bin/pcsx2",
	NULL
};
const char* const kAppImageDirs[] = {
	"~/Applications", "~/AppImages", "~/.local/bin", "~/.local/share/lutris/runners/pcsx2",
	"~/Desktop", "~/Downloads", "/opt", NULL
};
#endif

const char* const kDiscExts[] = { ".iso", ".chd", ".cso", ".zso", ".mdf", NULL };

// The text after a leading bare "pcsx2", or NULL when the spec isn't one.
const char* AfterBarePcsx2(const char* spec) {
	if (!spec) return NULL;
	while (*spec == ' ' || *spec == '\t') spec++;
	if (strncmp(spec, "pcsx2", 5) != 0) return NULL;
	if (spec[5] != ' ' && spec[5] != '\t' && spec[5] != 0) return NULL;
	return spec + 5;
}

bool Claims(const char* spec) {
	return AfterBarePcsx2(spec) != NULL;
}

bool Build(const char* spec, char* outCmd, size_t outSize) {
	const char* rest = AfterBarePcsx2(spec);
	if (!rest) return false;
	char found[512];
	const char* where = s_pcsx2Path;
	if (!where[0]) {
		if (!Pcsx2_Find(found, sizeof(found))) return false;
		where = found;
	}
	char bin[1024];
	Emu_AppBinary(where, "PCSX2", bin, sizeof(bin));
	int n = snprintf(outCmd, outSize, "\"%s\"%s", bin, rest);
	return n > 0 && (size_t)n < outSize;
}

const Launcher kPcsx2Launcher = {
	"pcsx2",
	"PCSX2",
	Claims,
	300,
	Build,
};

} // namespace

bool Pcsx2_Find(char* out, size_t outSize) {
	if (outSize) out[0] = '\0';
	for (int i = 0; kExes[i]; i++)
		if (Emu_OnPath(kExes[i], out, outSize)) return true;
	return Emu_FirstExisting(kPlaces, out, outSize) ||
	       Emu_FindAppImage(kAppImageDirs, "pcsx2", out, outSize);
}

bool Pcsx2_IsDisc(const char* path) {
	const char* dot = path ? strrchr(path, '.') : NULL;
	if (!dot) return false;
	for (int i = 0; kDiscExts[i]; i++)
		if (strcasecmp(dot, kDiscExts[i]) == 0) return true;
	return false;
}

bool Pcsx2_DiscLaunch(const char* disc, char* out, size_t outSize) {
	int n = snprintf(out, outSize, "pcsx2 -batch -fastboot -fullscreen -- \"%s\"", disc);
	return n > 0 && (size_t)n < outSize;
}

void Launcher_RegisterPcsx2() {
	Launcher_Register(&kPcsx2Launcher);
}
