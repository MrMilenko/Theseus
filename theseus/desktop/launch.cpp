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
#include "launchers/launcher.h"
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

// Last-launch diagnostic captured by Launch_DoSpawn so Title Maker's
// "Test Launch" button can surface it as a status toast instead of users
// having to dig in stderr.
char g_launchLastResult[256] = "";

// Single shared spawn implementation. Both DesktopLaunch (fire-and-forget,
// e.g. Title Maker Test Launch) and SpawnLaunchSpec (overlay-driven game
// launch) call this. Returns true if the OS handed us back a process /
// shell-exec handle; the parent doesn't wait on the child either way.
static bool Launch_DoSpawn(const char* expanded)
{
	if (!expanded || !*expanded) return false;

#ifdef _WIN32
	// URLs go through ShellExecute (handles steam:// via the registered
	// handler, http(s) via default browser, etc.). Raw commands run
	// through cmd /S /C -- /S tells cmd to treat the outermost quotes
	// as literal command boundaries instead of trying to be clever,
	// which is what breaks UNC paths and quoted args.
	if (IsUrl(expanded))
	{
		HINSTANCE rc = ShellExecuteA(NULL, "open", expanded, NULL, NULL, SW_SHOWNORMAL);
		if ((INT_PTR)rc <= 32) {
			snprintf(g_launchLastResult, sizeof(g_launchLastResult),
			         "ShellExecute failed (code %ld): %s", (long)(INT_PTR)rc, expanded);
			fprintf(stderr, "[launch] %s\n", g_launchLastResult);
			return false;
		}
	}
	else
	{
		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "cmd /S /C \"\"%s\"\"", expanded);
		if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
		                    DETACHED_PROCESS, NULL, NULL, &si, &pi))
		{
			DWORD err = GetLastError();
			snprintf(g_launchLastResult, sizeof(g_launchLastResult),
			         "CreateProcess failed (error %lu): %s", err, expanded);
			fprintf(stderr, "[launch] %s\n", g_launchLastResult);
			return false;
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
		return false;
	}
	// Parent: don't wait. Fire-and-forget.
#endif

	snprintf(g_launchLastResult, sizeof(g_launchLastResult),
	         "Launched: %s", expanded);
	return true;
}

// Expand $VARs, route the spec through the matching launcher module
// (Build()), log the trace, then spawn. Both public dispatchers funnel
// through this so the trace, error reporting, and command-form rules
// live in one place. typeHint is the games.ini `type=` value when the
// caller has it; pass NULL to fall back to Claims-based detection.
static bool Launch_ExpandAndSpawn(const char* spec, const char* typeHint,
                                  char* finalOut, size_t finalSize)
{
	if (!spec || !spec[0]) return false;
	char expanded[2048];
	PathTemplate_Expand(spec, expanded, sizeof(expanded));
	Launcher_Build(expanded, typeHint, finalOut, finalSize);
	fprintf(stderr, "[launch] in:  %s\n", spec);
	if (strcmp(spec, expanded) != 0)
		fprintf(stderr, "[launch] var: %s\n", expanded);
	if (strcmp(expanded, finalOut) != 0)
		fprintf(stderr, "[launch] cmd: %s\n", finalOut);
	return Launch_DoSpawn(finalOut);
}

void DesktopLaunch(const char* spec)
{
	char finalCmd[2048];
	Launch_ExpandAndSpawn(spec, NULL, finalCmd, sizeof(finalCmd));
}

// Spawn called by the launch overlay tick once the fade-in completes.
// Identical to DesktopLaunch except we minimize the dashboard window
// after a successful spawn so the game gets focus.
static void SpawnLaunchSpec(const char* spec)
{
	char finalCmd[2048];
	if (!Launch_ExpandAndSpawn(spec, NULL, finalCmd, sizeof(finalCmd))) return;

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

	// Translate via the canonical xboxfs.h resolver -- handles both
	// drive-letter ("E:\...") and device-path ("\Device\Harddisk0\...")
	// forms and routes to Library/Configs/Data as appropriate.
	const char* translated = XboxFS_TranslatePath(devicePath);
	if (!translated || !*translated) {
		fprintf(stderr, "[launch] DesktopLaunchTitle: unrecognized path format: %s\n", devicePath);
		return;
	}
	char localPath[512];
	strncpy(localPath, translated, sizeof(localPath) - 1);
	localPath[sizeof(localPath) - 1] = '\0';

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
