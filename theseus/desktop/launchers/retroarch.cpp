// retroarch.cpp: RetroArch launcher module. Claims retroarch:// URLs
// and builds a retroarch.exe command line from query-string fields.
//
// Spec form:
//   retroarch://run?core=<urlenc>&content=<urlenc>
//
// `core` is either a filename relative to <install>/cores/, or an
// absolute path. `content` is the ROM/ISO path. Install root comes
// from s_retroarchPath (desktop.ini [Desktop] RetroArchPath=).

#include "launcher.h"
#include "retroarch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

extern char s_retroarchPath[512];

namespace {

#ifdef _WIN32
const char  kPathSep     = '\\';
const char* kRetroArchExe = "retroarch.exe";
#else
const char  kPathSep     = '/';
const char* kRetroArchExe = "retroarch";
#endif

bool Claims(const char* spec) {
	if (!spec) return false;
	const char* p = spec;
	while (*p == ' ' || *p == '\t') p++;
	return strncmp(p, "retroarch://", 12) == 0;
}

int HexVal(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	return -1;
}

void UrlDecode(const char* in, size_t inLen, char* out, size_t outSize) {
	if (outSize == 0) return;
	size_t op = 0;
	for (size_t ip = 0; ip < inLen && op + 1 < outSize; ip++) {
		if (in[ip] == '%' && ip + 2 < inLen) {
			int hi = HexVal(in[ip + 1]);
			int lo = HexVal(in[ip + 2]);
			if (hi >= 0 && lo >= 0) {
				out[op++] = (char)((hi << 4) | lo);
				ip += 2;
				continue;
			}
		}
		out[op++] = (in[ip] == '+') ? ' ' : in[ip];
	}
	out[op] = '\0';
}

bool ParseField(const char* query, const char* key, char* out, size_t outSize) {
	if (outSize) out[0] = '\0';
	size_t keyLen = strlen(key);
	const char* p = query;
	while (*p) {
		const char* eq = strchr(p, '=');
		if (!eq) return false;
		const char* end = strchr(eq + 1, '&');
		if (!end) end = eq + 1 + strlen(eq + 1);
		if ((size_t)(eq - p) == keyLen && strncmp(p, key, keyLen) == 0) {
			UrlDecode(eq + 1, (size_t)(end - (eq + 1)), out, outSize);
			return true;
		}
		if (!*end) return false;
		p = end + 1;
	}
	return false;
}

bool LooksAbsolute(const char* p) {
	if (!p || !*p) return false;
#ifdef _WIN32
	if (p[0] && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) return true;
	if (p[0] == '\\' && p[1] == '\\') return true;
	return false;
#else
	return p[0] == '/';
#endif
}

bool Build(const char* spec, char* outCmd, size_t outSize) {
	if (!spec || !outCmd || outSize == 0) return false;

	const char* p = spec;
	while (*p == ' ' || *p == '\t') p++;
	if (strncmp(p, "retroarch://", 12) != 0) return false;
	p += 12;

	const char* q = strchr(p, '?');
	if (!q) return false;

	char core[512]    = "";
	char content[512] = "";
	if (!ParseField(q + 1, "core",    core,    sizeof(core)))    return false;
	if (!ParseField(q + 1, "content", content, sizeof(content))) return false;

	const char* install = s_retroarchPath[0] ? s_retroarchPath : "";

	char corePath[1024];
	if (LooksAbsolute(core))
		snprintf(corePath, sizeof(corePath), "%s", core);
	else if (install[0])
		snprintf(corePath, sizeof(corePath), "%s%ccores%c%s",
		         install, kPathSep, kPathSep, core);
	else
		snprintf(corePath, sizeof(corePath), "cores%c%s", kPathSep, core);

#ifdef __APPLE__
	// macOS: the cores dir is in ~/Library/Application Support/RetroArch,
	// but the binary is inside /Applications/RetroArch.app. Launch via
	// `open -na RetroArch --args ...` so we don't hardcode the bundle path.
	int n = snprintf(outCmd, outSize,
	                 "open -na RetroArch --args -L \"%s\" \"%s\"",
	                 corePath, content);
#else
	char exePath[1024];
	if (install[0])
		snprintf(exePath, sizeof(exePath), "%s%c%s", install, kPathSep, kRetroArchExe);
	else
		snprintf(exePath, sizeof(exePath), "%s", kRetroArchExe);

	int n = snprintf(outCmd, outSize,
	                 "\"%s\" -L \"%s\" \"%s\"",
	                 exePath, corePath, content);
#endif
	return n > 0 && (size_t)n < outSize;
}

// Priority < 500 so retroarch:// claims before the generic url
// handler picks it up.
const Launcher kRetroArchLauncher = {
	"retroarch",
	"RetroArch",
	Claims,
	200,
	Build,
};

} // namespace

void Launcher_RegisterRetroArch() {
	Launcher_Register(&kRetroArchLauncher);
}

int RetroArch_DiscoverInstall(const char* userOverride,
                               char outRoots[][512], int maxRoots) {
	int n = 0;
	auto append = [&](const char* path) {
		if (!path || !*path || n >= maxRoots) return;
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return;
		for (int i = 0; i < n; i++)
			if (strcmp(outRoots[i], path) == 0) return;
		strncpy(outRoots[n], path, 511);
		outRoots[n][511] = '\0';
		n++;
	};

	if (userOverride && *userOverride) append(userOverride);

#ifdef _WIN32
	const char* candidates[] = {
		"C:\\RetroArch",
		"C:\\RetroArch-Win64",
		"C:\\Program Files\\RetroArch",
		"C:\\Program Files\\RetroArch-Win64",
		"D:\\RetroArch",
		"D:\\RetroArch-Win64",
		"E:\\RetroArch",
		0
	};
	for (int i = 0; candidates[i]; i++) append(candidates[i]);
#elif defined(__APPLE__)
	const char* home = getenv("HOME");
	if (home) {
		char buf[512];
		snprintf(buf, sizeof(buf), "%s/Library/Application Support/RetroArch", home);
		append(buf);
	}
	append("/Applications/RetroArch.app");
	append("/opt/homebrew/share/libretro");
#else
	const char* home = getenv("HOME");
	if (home) {
		char buf[512];
		snprintf(buf, sizeof(buf), "%s/.config/retroarch", home);
		append(buf);
		snprintf(buf, sizeof(buf), "%s/.local/share/retroarch", home);
		append(buf);
		snprintf(buf, sizeof(buf), "%s/.var/app/org.libretro.RetroArch/config/retroarch", home);
		append(buf);
	}
	append("/usr/share/libretro");
	append("/usr/local/share/libretro");
#endif

	return n;
}

int RetroArch_EnumerateCores(const char* /*installRoot*/,
                              char /*outCores*/[][256], int /*maxCores*/) {
	return 0;
}
