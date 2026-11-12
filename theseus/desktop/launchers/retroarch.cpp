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
#include "esde.h"   // g_retroarchFullscreen

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "platform.h"
#include "host.h"

extern char s_retroarchPath[512];

namespace {

// Case-insensitive "name ends with ext", like a Win32 *.ext glob.
bool HasExt(const char* name, const char* ext) {
	size_t n = strlen(name), e = strlen(ext);
	return n >= e && strcasecmp(name + n - e, ext) == 0;
}

#ifdef THESEUS_HOST_WIN
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
#ifdef THESEUS_HOST_WIN
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

	// -f forces fullscreen for this run without touching retroarch.cfg, and
	// their video_windowed_fullscreen setting still picks borderless.
	const char* fs = g_retroarchFullscreen ? " -f" : "";
	const char* ovr = RetroArch_OverrideConfig();
	char extra[900];
	snprintf(extra, sizeof(extra), "%s%s%s%s", fs,
	         ovr[0] ? " --appendconfig \"" : "", ovr[0] ? ovr : "", ovr[0] ? "\"" : "");

#ifdef THESEUS_HOST_MAC
	// macOS: the cores dir is in ~/Library/Application Support/RetroArch,
	// but the binary is inside /Applications/RetroArch.app. Launch via
	// `open -na RetroArch --args ...` so we don't hardcode the bundle path.
	int n = snprintf(outCmd, outSize,
	                 "open -na RetroArch --args%s -L \"%s\" \"%s\"",
	                 extra, corePath, content);
#else
	char exePath[1024];
	if (install[0])
		snprintf(exePath, sizeof(exePath), "%s%c%s", install, kPathSep, kRetroArchExe);
	else
		snprintf(exePath, sizeof(exePath), "%s", kRetroArchExe);

	int n = snprintf(outCmd, outSize,
	                 "\"%s\"%s -L \"%s\" \"%s\"",
	                 exePath, extra, corePath, content);
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

#ifdef THESEUS_HOST_WIN
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
#elif defined(THESEUS_HOST_MAC)
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

static bool DirExists(const char* path) {
	if (!path || !*path) return false;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool FileExists(const char* path) {
	if (!path || !*path) return false;
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool RetroArch_DiscoverPlaylistsDir(const char* installRoot,
                                     char* outDir, size_t outSize) {
	if (outSize) outDir[0] = '\0';
	char buf[600];

	if (installRoot && *installRoot) {
		snprintf(buf, sizeof(buf), "%s%cplaylists", installRoot, kPathSep);
		if (DirExists(buf)) { strncpy(outDir, buf, outSize - 1); outDir[outSize - 1] = 0; return true; }
	}

#ifdef THESEUS_HOST_MAC
	const char* home = getenv("HOME");
	if (home) {
		snprintf(buf, sizeof(buf), "%s/Documents/RetroArch/playlists", home);
		if (DirExists(buf)) { strncpy(outDir, buf, outSize - 1); outDir[outSize - 1] = 0; return true; }
	}
#elif !defined(THESEUS_HOST_WIN)
	const char* home = getenv("HOME");
	if (home) {
		snprintf(buf, sizeof(buf), "%s/.config/retroarch/playlists", home);
		if (DirExists(buf)) { strncpy(outDir, buf, outSize - 1); outDir[outSize - 1] = 0; return true; }
		snprintf(buf, sizeof(buf),
		         "%s/.var/app/org.libretro.RetroArch/config/retroarch/playlists", home);
		if (DirExists(buf)) { strncpy(outDir, buf, outSize - 1); outDir[outSize - 1] = 0; return true; }
	}
#endif
	return false;
}

bool RetroArch_FindCoreForSystem(const char* installRoot, const char* systemName,
                                  char* outCoreFile, size_t outSize) {
	if (outSize) outCoreFile[0] = '\0';
	if (!installRoot || !*installRoot || !systemName || !*systemName) return false;

#ifdef THESEUS_HOST_WIN
	const char* coreExt = ".dll";
#elif defined(THESEUS_HOST_MAC)
	const char* coreExt = ".dylib";
#else
	const char* coreExt = ".so";
#endif

	char infoDir[600], coresDir[600];
	snprintf(infoDir,  sizeof(infoDir),  "%s%cinfo",  installRoot, kPathSep);
	snprintf(coresDir, sizeof(coresDir), "%s%ccores", installRoot, kPathSep);

	bool found = false;
	auto tryFile = [&](const char* infoName) {
		char infoPath[800];
		snprintf(infoPath, sizeof(infoPath), "%s%c%s", infoDir, kPathSep, infoName);
		FILE* fp = fopen(infoPath, "r");
		if (!fp) return;
		char line[512];
		while (fgets(line, sizeof(line), fp) && !found) {
			const char* eq = strchr(line, '=');
			if (!eq) continue;
			const char* keyEnd = eq;
			while (keyEnd > line && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) keyEnd--;
			const char* keyStart = line;
			while (keyStart < keyEnd && (*keyStart == ' ' || *keyStart == '\t')) keyStart++;
			size_t klen = (size_t)(keyEnd - keyStart);
			if (klen != strlen("systemname") ||
			    strncmp(keyStart, "systemname", klen) != 0) continue;
			const char* q = strchr(eq, '"');
			if (!q) continue;
			q++;
			const char* qEnd = strchr(q, '"');
			if (!qEnd) continue;
			size_t vlen = (size_t)(qEnd - q);
			if (vlen != strlen(systemName) || strncmp(q, systemName, vlen) != 0) continue;

			char coreFile[256];
			strncpy(coreFile, infoName, sizeof(coreFile) - 1);
			coreFile[sizeof(coreFile) - 1] = 0;
			char* dot = strrchr(coreFile, '.');
			if (dot) snprintf(dot, sizeof(coreFile) - (dot - coreFile), "%s", coreExt);
			char corePath[900];
			snprintf(corePath, sizeof(corePath), "%s%c%s", coresDir, kPathSep, coreFile);
			if (FileExists(corePath)) {
				strncpy(outCoreFile, coreFile, outSize - 1);
				outCoreFile[outSize - 1] = 0;
				found = true;
			}
		}
		fclose(fp);
	};

	HostDir* d = Host_OpenDir(infoDir);
	if (d) {
		char name[260];
		HostFileInfo info;
		while (!found && Host_ReadDir(d, name, sizeof(name), &info)) {
			if (info.attrs & HOST_ATTR_DIR) continue;
			if (!HasExt(name, ".info")) continue;
			tryFile(name);
		}
		Host_CloseDir(d);
	}
	return found;
}

bool RetroArch_GetSystemForCore(const char* installRoot, const char* coreFilename,
                                 char* outSystem, size_t outSize) {
	if (outSize) outSystem[0] = '\0';
	if (!installRoot || !*installRoot || !coreFilename || !*coreFilename) return false;

	char infoName[256];
	strncpy(infoName, coreFilename, sizeof(infoName) - 1);
	infoName[sizeof(infoName) - 1] = 0;
	char* dot = strrchr(infoName, '.');
	if (dot) snprintf(dot, sizeof(infoName) - (dot - infoName), ".info");
	else {
		size_t l = strlen(infoName);
		if (l + 5 < sizeof(infoName)) strcpy(infoName + l, ".info");
	}

	char infoPath[800];
	snprintf(infoPath, sizeof(infoPath), "%s%cinfo%c%s",
	         installRoot, kPathSep, kPathSep, infoName);

	FILE* fp = fopen(infoPath, "r");
	if (!fp) return false;
	bool found = false;
	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		const char* eq = strchr(line, '=');
		if (!eq) continue;
		const char* keyEnd = eq;
		while (keyEnd > line && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) keyEnd--;
		const char* keyStart = line;
		while (keyStart < keyEnd && (*keyStart == ' ' || *keyStart == '\t')) keyStart++;
		size_t klen = (size_t)(keyEnd - keyStart);
		if (klen != strlen("systemname") ||
		    strncmp(keyStart, "systemname", klen) != 0) continue;
		const char* q = strchr(eq, '"');
		if (!q) continue;
		q++;
		const char* qEnd = strchr(q, '"');
		if (!qEnd) continue;
		size_t vlen = (size_t)(qEnd - q);
		if (vlen >= outSize) vlen = outSize - 1;
		memcpy(outSystem, q, vlen);
		outSystem[vlen] = '\0';
		found = true;
		break;
	}
	fclose(fp);
	return found;
}

static void SanitizeThumbnailKey(const char* in, char* out, size_t outSize) {
	size_t op = 0;
	for (const char* p = in; *p && op + 1 < outSize; p++) {
		char c = *p;
		bool replace = (c == '&' || c == '*' || c == '/' || c == ':' ||
		                c == '`' || c == '<' || c == '>' || c == '?' ||
		                c == '\\' || c == '|');
		out[op++] = replace ? '_' : c;
	}
	out[op] = '\0';
}

bool RetroArch_FindBoxart(const char* installRoot,
                          const char* dbName, const char* label,
                          const char* contentPath,
                          char* outPath, size_t outSize) {
	if (outSize) outPath[0] = '\0';
	if (!installRoot || !*installRoot || !dbName || !*dbName) return false;

	char system[256];
	strncpy(system, dbName, sizeof(system) - 1);
	system[sizeof(system) - 1] = 0;
	size_t sl = strlen(system);
	if (sl > 4 && strcmp(system + sl - 4, ".lpl") == 0) system[sl - 4] = '\0';

	char keys[2][512];
	int keyCount = 0;

	if (label && *label) {
		SanitizeThumbnailKey(label, keys[keyCount], sizeof(keys[keyCount]));
		keyCount++;
	}

	if (contentPath && *contentPath) {
		const char* fwd = strrchr(contentPath, '/');
		const char* back = strrchr(contentPath, '\\');
		const char* fname = (fwd > back) ? fwd : back;
		fname = fname ? fname + 1 : contentPath;
		const char* hash = strchr(fname, '#');
		const char* baseSrc = hash ? hash + 1 : fname;
		char base[512];
		strncpy(base, baseSrc, sizeof(base) - 1);
		base[sizeof(base) - 1] = 0;
		char* dot = strrchr(base, '.');
		if (dot) *dot = '\0';
		SanitizeThumbnailKey(base, keys[keyCount], sizeof(keys[keyCount]));
		keyCount++;
	}

	for (int i = 0; i < keyCount; i++) {
		char path[1024];
		snprintf(path, sizeof(path), "%s%cthumbnails%c%s%cNamed_Boxarts%c%s.png",
		         installRoot, kPathSep, kPathSep, system, kPathSep, kPathSep, keys[i]);
		if (FileExists(path)) {
			strncpy(outPath, path, outSize - 1);
			outPath[outSize - 1] = 0;
			return true;
		}
	}
	return false;
}

namespace {

bool ExtractStringField(const char* start, const char* end,
                        const char* key, char* out, size_t outSize) {
	if (outSize) out[0] = '\0';
	char pattern[64];
	int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	if (pn <= 0) return false;
	for (const char* p = start; p + pn < end; p++) {
		if (strncmp(p, pattern, pn) != 0) continue;
		p += pn;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p >= end || *p != ':') continue;
		p++;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p >= end || *p != '"') continue;
		p++;
		size_t op = 0;
		while (p < end && *p != '"' && op + 1 < outSize) {
			if (*p == '\\' && p + 1 < end) {
				p++;
				char e = *p;
				if      (e == 'n')  out[op++] = '\n';
				else if (e == 't')  out[op++] = '\t';
				else if (e == '"')  out[op++] = '"';
				else if (e == '\\') out[op++] = '\\';
				else                out[op++] = e;
			} else {
				out[op++] = *p;
			}
			p++;
		}
		out[op] = '\0';
		return true;
	}
	return false;
}

int WalkOnePlaylist(const char* filePath, RetroArchItemCB cb, void* userdata) {
	FILE* fp = fopen(filePath, "rb");
	if (!fp) return 0;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(fp); return 0; }
	fseek(fp, 0, SEEK_SET);
	char* buf = (char*)malloc((size_t)sz + 1);
	if (!buf) { fclose(fp); return 0; }
	size_t r = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[r] = '\0';
	const char* end = buf + r;

	const char* p = strstr(buf, "\"items\"");
	if (!p) { free(buf); return 0; }
	p = strchr(p, '[');
	if (!p) { free(buf); return 0; }
	p++;

	int count = 0;
	while (p < end) {
		while (p < end && *p != '{' && *p != ']') p++;
		if (p >= end || *p == ']') break;
		const char* objStart = p;
		int depth = 1;
		p++;
		while (p < end && depth > 0) {
			if (*p == '"') {
				p++;
				while (p < end && *p != '"') {
					if (*p == '\\' && p + 1 < end) p += 2;
					else p++;
				}
			}
			else if (*p == '{') depth++;
			else if (*p == '}') depth--;
			if (p < end) p++;
		}
		const char* objEnd = p;

		char label[512] = "", path2[1024] = "", dbName[256] = "",
		     coreName[256] = "", corePath[512] = "";
		ExtractStringField(objStart, objEnd, "label",     label,    sizeof(label));
		ExtractStringField(objStart, objEnd, "path",      path2,    sizeof(path2));
		ExtractStringField(objStart, objEnd, "db_name",   dbName,   sizeof(dbName));
		ExtractStringField(objStart, objEnd, "core_name", coreName, sizeof(coreName));
		ExtractStringField(objStart, objEnd, "core_path", corePath, sizeof(corePath));

		if (path2[0]) {
			cb(label, path2, dbName, coreName, corePath, userdata);
			count++;
		}
	}
	free(buf);
	return count;
}

} // namespace

int RetroArch_WalkPlaylists(const char* playlistsDir,
                             RetroArchItemCB cb, void* userdata) {
	if (!playlistsDir || !*playlistsDir || !cb) return 0;
	int total = 0;

	HostDir* d = Host_OpenDir(playlistsDir);
	if (d) {
		char name[260];
		HostFileInfo info;
		while (Host_ReadDir(d, name, sizeof(name), &info)) {
			if (info.attrs & HOST_ATTR_DIR) continue;
			if (!HasExt(name, ".lpl")) continue;
			char filePath[900];
			snprintf(filePath, sizeof(filePath), "%s%c%s", playlistsDir, kPathSep, name);
			total += WalkOnePlaylist(filePath, cb, userdata);
		}
		Host_CloseDir(d);
	}

	// Also walk the recent-launches history under builtin/ which lives
	// outside the top-level .lpl glob.
	char history[800];
	snprintf(history, sizeof(history), "%s%cbuiltin%ccontent_history.lpl",
	         playlistsDir, kPathSep, kPathSep);
	if (FileExists(history)) total += WalkOnePlaylist(history, cb, userdata);

	return total;
}

int RetroArch_EnumerateCores(const char* installRoot,
                              char outCores[][256], int maxCores) {
	if (!installRoot || !*installRoot || !outCores || maxCores <= 0) return 0;

#ifdef THESEUS_HOST_WIN
	const char* ext = ".dll";
	char sep = '\\';
#elif defined(THESEUS_HOST_MAC)
	const char* ext = ".dylib";
	char sep = '/';
#else
	const char* ext = ".so";
	char sep = '/';
#endif

	char dir[600];
	snprintf(dir, sizeof(dir), "%s%ccores", installRoot, sep);

	int n = 0;
	size_t extLen = strlen(ext);

	HostDir* d = Host_OpenDir(dir);
	if (!d) return 0;
	char name[260];
	HostFileInfo info;
	while (n < maxCores && Host_ReadDir(d, name, sizeof(name), &info)) {
		if (info.attrs & HOST_ATTR_DIR) continue;
		if (!HasExt(name, ext)) continue;
		strncpy(outCores[n], name, 255);
		outCores[n][255] = '\0';
		n++;
	}
	Host_CloseDir(d);

	return n;
}
