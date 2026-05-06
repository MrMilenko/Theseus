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
#include "stb_image.h"
#include "path_template.h"
#include <SDL_opengl.h>

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

// Last-launch diagnostic captured by SpawnLaunchSpec / DesktopLaunch so
// Title Maker's "Test Launch" button can surface it as a status toast
// instead of users having to dig in stderr.
char g_launchLastResult[256] = "";

void DesktopLaunch(const char* spec)
{
	if (!spec || !spec[0]) return;

	// Expand $XEMU_PATH / $STEAM_PATH / etc. before logging or spawning so
	// the trace shows what we actually executed.
	char expanded[2048];
	PathTemplate_Expand(spec, expanded, sizeof(expanded));

	fprintf(stderr, "[launch] in:  %s\n", spec);
	if (strcmp(spec, expanded) != 0)
		fprintf(stderr, "[launch] cmd: %s\n", expanded);

#ifdef _WIN32
	// URLs go through ShellExecute (handles steam:// via the registered
	// handler, http(s) via default browser, etc.). Raw commands run
	// through cmd /S /C in detached mode -- /S tells cmd to treat the
	// outermost quotes as literal command boundaries instead of trying
	// to be clever, which is what breaks UNC paths and quoted args.
	if (IsUrl(expanded))
	{
		HINSTANCE rc = ShellExecuteA(NULL, "open", expanded, NULL, NULL, SW_SHOWNORMAL);
		if ((INT_PTR)rc <= 32) {
			snprintf(g_launchLastResult, sizeof(g_launchLastResult),
			         "ShellExecute failed (code %ld): %s", (long)(INT_PTR)rc, expanded);
			fprintf(stderr, "[launch] %s\n", g_launchLastResult);
			return;
		}
	}
	else
	{
		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "cmd /S /C \"%s\"", expanded);
		if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
		                    DETACHED_PROCESS, NULL, NULL, &si, &pi))
		{
			DWORD err = GetLastError();
			snprintf(g_launchLastResult, sizeof(g_launchLastResult),
			         "CreateProcess failed (error %lu): %s", err, expanded);
			fprintf(stderr, "[launch] %s\n", g_launchLastResult);
			return;
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
#else
	pid_t pid = fork();
	if (pid == 0)
	{
		ExecLaunch(expanded);
		_exit(127);
	}
	else if (pid < 0)
	{
		snprintf(g_launchLastResult, sizeof(g_launchLastResult),
		         "fork() failed: %s", strerror(errno));
		fprintf(stderr, "[launch] %s\n", g_launchLastResult);
		return;
	}
	// Parent: don't wait. Fire-and-forget.
#endif

	snprintf(g_launchLastResult, sizeof(g_launchLastResult),
	         "Launched: %s", expanded);
}

// Actually fork/spawn the launch spec. Called by the overlay tick once the
// fade-in has completed. Same mechanics as before, just split out so the
// overlay can drive timing.
static void SpawnLaunchSpec(const char* spec)
{
	if (!spec || !spec[0]) return;

	// Expand template variables before spawning. Logged separately from
	// the input so users can spot bad $VAR substitutions in the trace.
	char expanded[2048];
	PathTemplate_Expand(spec, expanded, sizeof(expanded));

	fprintf(stderr, "[launch] in:  %s\n", spec);
	if (strcmp(spec, expanded) != 0)
		fprintf(stderr, "[launch] cmd: %s\n", expanded);

#ifndef _WIN32
	pid_t pid = fork();
	if (pid == 0)
	{
		ExecLaunch(expanded);
		_exit(127);
	}
	else if (pid < 0)
	{
		snprintf(g_launchLastResult, sizeof(g_launchLastResult),
		         "fork() failed: %s", strerror(errno));
		fprintf(stderr, "[launch] %s\n", g_launchLastResult);
		return;
	}
#else
	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	BOOL ok = FALSE;

	if (IsUrl(expanded))
	{
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "cmd /S /C start \"\" \"%s\"", expanded);
		ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	}
	else
	{
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "cmd /S /C \"%s\"", expanded);
		ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	}

	if (!ok)
	{
		DWORD err = GetLastError();
		snprintf(g_launchLastResult, sizeof(g_launchLastResult),
		         "CreateProcess failed (error %lu): %s", err, expanded);
		fprintf(stderr, "[launch] %s\n", g_launchLastResult);
		return;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
#endif

	snprintf(g_launchLastResult, sizeof(g_launchLastResult),
	         "Launched: %s", expanded);

	extern SDL_Window* g_pSDLWindow;
	if (g_pSDLWindow) SDL_MinimizeWindow(g_pSDLWindow);
}

// ============================================================================
// Launch overlay state machine
//
// Mirrors theLaunchGameLevel's timing: a brief fade-in to black so the user
// sees a clear "we're launching" beat before the dashboard window minimizes
// and the game's own splash takes over. Lives outside the scene graph so we
// don't trip the CLevel::Advance crash that fires when navigating back out
// of theLaunchGameLevel after launch() returns on desktop.
// ============================================================================

static bool   s_overlayActive  = false;
static double s_overlayStart   = 0.0;
static bool   s_overlaySpawned = false;
static char   s_pendingSpec[2048];

// Total fade duration (seconds). The first kFadeIn portion is the alpha ramp,
// after which we hold full black for the remaining time and fire the spawn at
// kSpawnAt. theLaunchGameLevel uses sleep 1.1; we match that order of magnitude.
static const float kFadeIn  = 0.45f;
static const float kSpawnAt = 0.60f;
static const float kHoldEnd = 1.10f;

static double LaunchOverlay_Now()
{
	return (double)SDL_GetTicks() / 1000.0;
}

bool LaunchOverlay_IsActive()
{
	return s_overlayActive;
}

float LaunchOverlay_Alpha()
{
	if (!s_overlayActive) return 0.0f;
	double t = LaunchOverlay_Now() - s_overlayStart;
	if (t <= 0.0) return 0.0f;
	if (t >= (double)kFadeIn) return 1.0f;
	return (float)(t / (double)kFadeIn);
}

void LaunchOverlay_Reset()
{
	s_overlayActive = false;
	s_overlaySpawned = false;
	s_overlayStart = 0.0;
	s_pendingSpec[0] = 0;
}

// Lazy-load the Xbox logo PNG (Configs/xboxlogo.png) into a
// GL texture and cache the handle for the rest of the process. The PNG is
// pre-decoded from Stock's xboxlogo.xbx via OXDK xbx-convert and lives in
// the desktop-only UIX Configs directory; loading raw PNG with stb_image
// avoids dragging the engine's asset loader (and its TCHAR / Xbox typedef
// chain) into launch.cpp.
unsigned int LaunchOverlay_LogoGLTex(int* outW, int* outH)
{
	static GLuint s_logoTex = 0;
	static int    s_logoW = 0;
	static int    s_logoH = 0;
	static bool   s_loadAttempted = false;

	if (!s_loadAttempted) {
		s_loadAttempted = true;
		const char* path = "Configs/xboxlogo.png";
		int w = 0, h = 0, ch = 0;
		unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
		if (pixels) {
			glGenTextures(1, &s_logoTex);
			glBindTexture(GL_TEXTURE_2D, s_logoTex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
			             GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			stbi_image_free(pixels);
			s_logoW = w;
			s_logoH = h;
			fprintf(stderr, "[launch] Loaded Xbox logo: %dx%d (tex %u)\n",
			        w, h, (unsigned)s_logoTex);
		} else {
			fprintf(stderr, "[launch] Failed to load %s: %s\n",
			        path, stbi_failure_reason());
		}
	}

	if (outW) *outW = s_logoW;
	if (outH) *outH = s_logoH;
	return (unsigned int)s_logoTex;
}

void LaunchOverlay_Tick()
{
	if (!s_overlayActive) return;

	double t = LaunchOverlay_Now() - s_overlayStart;

	if (!s_overlaySpawned && t >= (double)kSpawnAt)
	{
		s_overlaySpawned = true;
		SpawnLaunchSpec(s_pendingSpec);
	}

	if (t >= (double)kHoldEnd)
	{
		// Fade window has elapsed. The window is minimized at this point
		// (SpawnLaunchSpec calls SDL_MinimizeWindow), so the user won't see
		// the overlay disappear; just clear state for next time.
		LaunchOverlay_Reset();
	}
}

void DesktopLaunchGame(const char* spec)
{
	if (!spec || !spec[0]) return;

	fprintf(stderr, "[launch] Queued: %s\n", spec);

	// If an overlay is already running, fire its pending spawn immediately
	// so we don't double-stack overlays. Practically this only happens if a
	// XAP triggers two launches in the same frame.
	if (s_overlayActive && !s_overlaySpawned && s_pendingSpec[0])
	{
		s_overlaySpawned = true;
		SpawnLaunchSpec(s_pendingSpec);
	}

	strncpy(s_pendingSpec, spec, sizeof(s_pendingSpec) - 1);
	s_pendingSpec[sizeof(s_pendingSpec) - 1] = 0;
	s_overlayStart   = LaunchOverlay_Now();
	s_overlaySpawned = false;
	s_overlayActive  = true;
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

// Partition -> drive letter map (Xbox-side; partitions 6/7/8/9 are
// the optional F/G/R/S drives that don't exist on desktop and resolve
// to NULL via XboxFS_DriveToPrefix).
static const struct { int partition; char drive; } s_partitionDrives[] = {
	{ 1, 'E' }, { 2, 'C' }, { 6, 'F' }, { 7, 'G' }, { 8, 'R' }, { 9, 'S' }
};

static bool ConvertXboxPathToLocal(const char* xboxPath, char* outBuf, size_t outSize)
{
	if (!xboxPath || !*xboxPath || outSize == 0) return false;

	// Drive-letter form: "X:\..." or "X:/..."
	const char* colon = strchr(xboxPath, ':');
	if (colon && (colon[1] == '\\' || colon[1] == '/')) {
		int driveLen = (int)(colon - xboxPath);
		if (driveLen == 1) {
			const char* prefix = XboxFS_DriveToPrefix(xboxPath[0]);
			if (!prefix) return false;
			snprintf(outBuf, outSize, "%s/%s", prefix, colon + 2);
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
		char drive = 0;
		for (size_t i = 0; i < sizeof(s_partitionDrives) / sizeof(s_partitionDrives[0]); i++) {
			if (s_partitionDrives[i].partition == partNum) { drive = s_partitionDrives[i].drive; break; }
		}
		const char* prefix = XboxFS_DriveToPrefix(drive);
		if (!prefix) return false;
		snprintf(outBuf, outSize, "%s/%s", prefix, p);
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
