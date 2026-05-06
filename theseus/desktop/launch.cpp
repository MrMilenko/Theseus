// launch.cpp: desktop title-launch bridge. Implements
// DesktopLaunchTitle, the desktop-side equivalent of XLaunchNewImage:
// looks the title up in games.ini, mutes audio, minimizes the
// window, and execs the host platform's launcher.

#include "std.h"
#include "launch.h"

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#endif

#include "virtual_games.h"

extern "C" void DashAudio_MuteAll(void);
extern SDL_Window* g_pSDLWindow;
extern bool  g_audioMuted;
extern float g_muteOverlayTimer;

// "scheme://..."; letters/digits/+/-/. then "://"
static bool IsUrl(const char* s)
{
	const char* p = s;
	while (*p && (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.'))
		p++;
	return p > s && p[0] == ':' && p[1] == '/' && p[2] == '/';
}

static bool IsSteamUrl(const char* s)
{
	return strncmp(s, "steam://", 8) == 0;
}

#ifndef _WIN32
// Build the argv for execlp/execl based on dispatch rules. Caller forks
// first; this runs in the child and never returns on success.
static void ExecLaunch(const char* spec)
{
	if (IsSteamUrl(spec))
	{
#ifdef __APPLE__
		// macOS: system handler works fine for steam://
		execlp("open", "open", spec, (char*)NULL);
#else
		// Linux: bypass xdg-open/KIO; steam binary takes the URL directly.
		execlp("steam", "steam", spec, (char*)NULL);
#endif
	}
	else if (IsUrl(spec))
	{
#ifdef __APPLE__
		execlp("open", "open", spec, (char*)NULL);
#else
		execlp("xdg-open", "xdg-open", spec, (char*)NULL);
#endif
	}
	else
	{
		// Raw command line: shell parses paths, args, scripts, etc.
		execl("/bin/sh", "sh", "-c", spec, (char*)NULL);
	}
}
#endif

void DesktopLaunch(const char* spec)
{
	if (!spec || !spec[0]) return;

	fprintf(stderr, "[launch] %s\n", spec);

#ifdef _WIN32
	// URLs go through ShellExecute (handles steam:// via the registered
	// handler, http(s) via default browser, etc.). Raw commands run
	// through cmd /C in detached mode.
	if (IsUrl(spec))
	{
		ShellExecuteA(NULL, "open", spec, NULL, NULL, SW_SHOWNORMAL);
	}
	else
	{
		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "cmd /C %s", spec);
		if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
		                   DETACHED_PROCESS, NULL, NULL, &si, &pi))
		{
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
	}
#else
	pid_t pid = fork();
	if (pid == 0)
	{
		ExecLaunch(spec);
		_exit(127);
	}
	// Parent: don't wait. Fire-and-forget.
#endif
}

void DesktopLaunchGame(const char* spec)
{
	if (!spec || !spec[0]) return;

	fprintf(stderr, "[launch] Starting: %s\n", spec);

	// Fire-and-forget the game launcher (no waitpid -- steam/xemu fork-and-
	// exit and would falsely trigger a "game ended" pop-back). The XAP-side
	// theLaunchGameLevel navigates back to main menu after a short delay,
	// and minimizing the window cuts dashboard audio via the focus-loss
	// path automatically.
#ifndef _WIN32
	pid_t pid = fork();
	if (pid == 0)
	{
		ExecLaunch(spec);
		_exit(127);
	}
	else if (pid < 0)
	{
		fprintf(stderr, "[launch] fork() failed: %s\n", strerror(errno));
		return;
	}
#else
	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	BOOL ok = FALSE;

	if (IsUrl(spec))
	{
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "cmd /C start \"\" \"%s\"", spec);
		ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	}
	else
	{
		char* cmdCopy = _strdup(spec);
		ok = CreateProcessA(NULL, cmdCopy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
		free(cmdCopy);
	}

	if (!ok)
	{
		fprintf(stderr, "[launch] CreateProcess failed: %lu\n", GetLastError());
		return;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
#endif

	extern SDL_Window* g_pSDLWindow;
	if (g_pSDLWindow) SDL_MinimizeWindow(g_pSDLWindow);
}

// ============================================================================
// Title launch bridge (XAP launch() built-in -> DesktopLaunchGame)
//
// The dashboard's harddrive.xap (and TitleMenu) call the VM `launch()`
// built-in with an Xbox-style path like
// "\Device\Harddisk0\Partition1\Games\Prison Architect". On Xbox that maps
// to XWriteTitleInfoAndReboot. On desktop the path needs to be resolved to
// a real launch command: virtualized entries in games.ini are the primary
// source, default.uixshortcut files are the fallback.
// ============================================================================

static const struct { int partition; const char* drive; } s_partitionDrives[] = {
	{ 1, "E" }, { 2, "C" }, { 6, "F" }, { 7, "G" }, { 8, "R" }, { 9, "S" }
};

static bool ConvertXboxPathToLocal(const char* xboxPath, char* outBuf, size_t outSize)
{
	if (!xboxPath || !*xboxPath || outSize == 0) return false;

	// Drive-letter form: "X:\..." or "X:/..."
	const char* colon = strchr(xboxPath, ':');
	if (colon && (colon[1] == '\\' || colon[1] == '/')) {
		int driveLen = (int)(colon - xboxPath);
		if (driveLen > 0 && driveLen < 32) {
			char drive[32];
			memcpy(drive, xboxPath, driveLen);
			drive[driveLen] = '\0';
			snprintf(outBuf, outSize, "xboxfs/%s/%s", drive, colon + 2);
			for (char* p = outBuf; *p; p++) if (*p == '\\') *p = '/';
			return true;
		}
	}

	// Device-path form: "\Device\Harddisk0\PartitionN\..."
	static const char kPrefix[] = "\\Device\\Harddisk0\\Partition";
	const size_t kPrefixLen = sizeof(kPrefix) - 1;
	if (strncmp(xboxPath, kPrefix, kPrefixLen) == 0) {
		const char* p = xboxPath + kPrefixLen;
		int partNum = 0;
		while (*p >= '0' && *p <= '9') { partNum = partNum * 10 + (*p - '0'); p++; }
		if (*p == '\\' || *p == '/') p++;
		const char* drive = NULL;
		for (size_t i = 0; i < sizeof(s_partitionDrives) / sizeof(s_partitionDrives[0]); i++) {
			if (s_partitionDrives[i].partition == partNum) { drive = s_partitionDrives[i].drive; break; }
		}
		if (!drive) return false;
		snprintf(outBuf, outSize, "xboxfs/%s/%s", drive, p);
		for (char* q = outBuf; *q; q++) if (*q == '\\') *q = '/';
		return true;
	}

	return false;
}

static bool ReadShortcutLaunch(const char* localFolder, char* outCmd, size_t outSize)
{
	char shortcutPath[512];
	snprintf(shortcutPath, sizeof(shortcutPath), "%s/default.uixshortcut", localFolder);

	FILE* fp = fopen(shortcutPath, "r");
	if (!fp) return false;

	char line[1024];
	bool found = false;
	while (fgets(line, sizeof(line), fp)) {
		char* nl = strchr(line, '\n'); if (nl) *nl = 0;
		char* cr = strchr(line, '\r'); if (cr) *cr = 0;
		if (strncmp(line, "Launch=", 7) == 0) {
			strncpy(outCmd, line + 7, outSize - 1);
			outCmd[outSize - 1] = 0;
			found = true;
			break;
		}
	}
	fclose(fp);
	return found;
}

void DesktopLaunchTitle(const char* devicePath)
{
	if (!devicePath || !*devicePath) {
		fprintf(stderr, "[launch] DesktopLaunchTitle: empty path\n");
		return;
	}

	char localPath[512];
	if (!ConvertXboxPathToLocal(devicePath, localPath, sizeof(localPath))) {
		fprintf(stderr, "[launch] DesktopLaunchTitle: unrecognized path format: %s\n", devicePath);
		return;
	}

	// Strip trailing slash for VGames matching.
	size_t pathLen = strlen(localPath);
	while (pathLen > 0 && (localPath[pathLen - 1] == '/' || localPath[pathLen - 1] == '\\'))
		localPath[--pathLen] = 0;

	// Primary: virtualized games.ini lookup.
	int vgIdx = VGames_MatchFolder(localPath);
	if (vgIdx >= 0 && g_vgames.games[vgIdx].launch[0]) {
		fprintf(stderr, "[launch] VGames match: %s -> %s\n", devicePath, g_vgames.games[vgIdx].launch);
		DesktopLaunchGame(g_vgames.games[vgIdx].launch);
		return;
	}

	// Fallback: read Launch= from default.uixshortcut.
	char launchCmd[1024];
	if (ReadShortcutLaunch(localPath, launchCmd, sizeof(launchCmd))) {
		fprintf(stderr, "[launch] uixshortcut launch: %s -> %s\n", devicePath, launchCmd);
		DesktopLaunchGame(launchCmd);
		return;
	}

	fprintf(stderr, "[launch] No launch command found for %s (resolved local: %s)\n",
		devicePath, localPath);
}
