// steam.cpp: Steam install discovery. Steam_DiscoverInstallRoots and
// Steam_DiscoverLibraries enumerate every Steam install + library
// directory on disk so Title Maker's Import Steam button can populate
// without users hunting for paths.
//
// Path coverage is intentionally over-thorough. We've heard "Steam
// isn't being detected" from Flatpak Steam, Debian's repackaged steam-
// installer, custom drive letters on Windows, and Steam Deck's
// `~/.local/share/Steam`. Each variant gets its own candidate. Users
// with truly weird setups can set [Desktop] SteamPath= in desktop.ini
// to point at the install root (or steamapps/) directly; that path is
// preferred over auto-detected ones.
//
// steam:// specs launch through the generic URL path in launch.cpp
// (Launch_DoSpawn detects the scheme); there's no launcher module here.

#include "steam.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "platform.h"
#include "host.h"

namespace {

// ----- Discovery helpers ------------------------------------------------

// Append a path to outBuf if not already present and not empty,
// returning the new count. Caller checks against maxOut.
int AppendUnique(char outBuf[][512], int count, int maxOut, const char* path) {
	if (!path || !*path || count >= maxOut) return count;
	for (int i = 0; i < count; i++) {
		if (strcasecmp(outBuf[i], path) == 0) return count;
	}
	strncpy(outBuf[count], path, 511);
	outBuf[count][511] = '\0';
	return count + 1;
}

bool DirExists(const char* path) {
	if (!path || !*path) return false;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Build candidate Steam install roots for the host platform.
// Returns count appended; doesn't validate paths exist (that's
// the caller's filter).
int CollectCandidateRoots(char outRoots[][512], int maxRoots) {
	int n = 0;

#ifdef THESEUS_HOST_WIN
	// Windows registry: HKCU\Software\Valve\Steam\SteamPath is
	// what the Steam installer writes. Most reliable for non-
	// default installs (e.g. user picked D:\Steam).
	{
		char buf[512];
		if (Host_RegistryString("Software\\Valve\\Steam", "SteamPath", buf, sizeof(buf)))
			n = AppendUnique(outRoots, n, maxRoots, buf);
	}
	// Default Windows install paths.
	{
		const char* envs[] = {
			"ProgramFiles(x86)", "ProgramFiles", NULL
		};
		for (int i = 0; envs[i]; i++) {
			const char* env = getenv(envs[i]);
			if (!env) continue;
			char path[512];
			snprintf(path, sizeof(path), "%s\\Steam", env);
			n = AppendUnique(outRoots, n, maxRoots, path);
		}
	}
#elif defined(THESEUS_HOST_MAC)
	// macOS: official installer puts it under ~/Library/Application Support.
	const char* home = getenv("HOME");
	if (home && *home) {
		char path[512];
		snprintf(path, sizeof(path), "%s/Library/Application Support/Steam", home);
		n = AppendUnique(outRoots, n, maxRoots, path);
	}
#else
	// Linux / Steam Deck. Cover the common variants:
	//   - ~/.steam/steam              -> classic install
	//   - ~/.local/share/Steam        -> Steam Deck + newer distros
	//   - ~/.var/app/com.valvesoftware.Steam/data/Steam -> Flatpak
	//   - ~/.steam/debian-installation -> Debian's steam-installer
	const char* home = getenv("HOME");
	if (home && *home) {
		const char* tails[] = {
			"/.steam/steam",
			"/.local/share/Steam",
			"/.var/app/com.valvesoftware.Steam/data/Steam",
			"/.steam/debian-installation",
			NULL
		};
		for (int i = 0; tails[i]; i++) {
			char path[512];
			snprintf(path, sizeof(path), "%s%s", home, tails[i]);
			n = AppendUnique(outRoots, n, maxRoots, path);
		}
	}
#endif

	return n;
}

// Parse Steam's libraryfolders.vdf to enumerate library directories
// beyond the default install root. Format is a Valve KeyValues blob;
// we just grep for `"path"` lines, which is what every existing
// parser in the wild does.
void ParseLibraryFoldersVdf(const char* steamappsDir,
                            char outLibs[][512], int* count, int maxLibs) {
	char vdfPath[512];
	// Steam moved this file around historically. Try the modern
	// location (steamapps/libraryfolders.vdf) and the legacy one
	// (config/libraryfolders.vdf). Modern wins because newer Steam
	// keeps both in sync but only modern has libraries added after
	// the migration.
	const char* candidates[] = {
		"%s/libraryfolders.vdf",
		"%s/../config/libraryfolders.vdf",
		NULL
	};
	for (int c = 0; candidates[c]; c++) {
		snprintf(vdfPath, sizeof(vdfPath), candidates[c], steamappsDir);
		FILE* fp = fopen(vdfPath, "r");
		if (!fp) continue;

		char line[1024];
		while (fgets(line, sizeof(line), fp)) {
			char* pathKey = strstr(line, "\"path\"");
			if (!pathKey) continue;
			char* q1 = strchr(pathKey + 6, '"');
			if (!q1) continue;
			char* q2 = strchr(q1 + 1, '"');
			if (!q2) continue;
			*q2 = '\0';
			// Steam writes paths with escaped backslashes on Windows
			// ("D:\\\\Library\\\\steamapps"), collapse them to
			// single backslashes for our consumers.
			char clean[512];
			int ci = 0;
			for (const char* s = q1 + 1; *s && ci + 1 < (int)sizeof(clean); ) {
				if (s[0] == '\\' && s[1] == '\\') { clean[ci++] = '\\'; s += 2; }
				else { clean[ci++] = *s++; }
			}
			clean[ci] = '\0';

			char libSteamapps[512];
			snprintf(libSteamapps, sizeof(libSteamapps), "%s/steamapps", clean);
			if (DirExists(libSteamapps)) {
				if (*count < maxLibs)
					*count = AppendUnique(outLibs, *count, maxLibs, libSteamapps);
			}
		}
		fclose(fp);
		break; // first matching VDF wins
	}
}

} // namespace

int Steam_DiscoverInstallRoots(const char* userOverride,
                                char outRoots[][512], int maxRoots) {
	int n = 0;

	// User override wins. Accept either the install root or steamapps
	// itself (strip the trailing "steamapps" so callers always see
	// the root).
	if (userOverride && *userOverride) {
		char root[512];
		strncpy(root, userOverride, sizeof(root) - 1);
		root[sizeof(root) - 1] = '\0';
		size_t len = strlen(root);
		if (len > 9 && strcasecmp(root + len - 9, "steamapps") == 0) {
			root[len - 9] = '\0';
			// Strip trailing slash if present.
			len = strlen(root);
			if (len > 0 && (root[len - 1] == '/' || root[len - 1] == '\\'))
				root[len - 1] = '\0';
		}
		if (DirExists(root)) n = AppendUnique(outRoots, n, maxRoots, root);
	}

	char candidates[16][512];
	int candCount = CollectCandidateRoots(candidates, 16);
	for (int i = 0; i < candCount && n < maxRoots; i++) {
		if (DirExists(candidates[i]))
			n = AppendUnique(outRoots, n, maxRoots, candidates[i]);
	}

	return n;
}

int Steam_DiscoverLibraries(const char* userOverride,
                             char outLibraries[][512], int maxLibs) {
	int n = 0;

	char roots[16][512];
	int rootCount = Steam_DiscoverInstallRoots(userOverride, roots, 16);

	for (int i = 0; i < rootCount && n < maxLibs; i++) {
		// Each install root has its own steamapps; that's the
		// "default library" even if the user has others elsewhere.
		char steamapps[512];
		snprintf(steamapps, sizeof(steamapps), "%s/steamapps", roots[i]);
		if (DirExists(steamapps)) {
			n = AppendUnique(outLibraries, n, maxLibs, steamapps);
			ParseLibraryFoldersVdf(steamapps, outLibraries, &n, maxLibs);
		}
	}

	return n;
}
