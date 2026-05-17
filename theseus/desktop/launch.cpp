// launch.cpp: desktop title-launch bridge. Implements
// DesktopLaunchTitle, the desktop-side equivalent of XLaunchNewImage:
// looks the title up in games.ini, mutes audio, minimizes the
// window, and execs the host platform's launcher.

#include "std.h"
#include "launch.h"
#include "panel_shared.h"

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
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

// Collapse a single outer quote pair so our own quoting doesn't stack.
// Only when there are exactly 2 quotes (one outer pair, nothing else).
// Build()-emitted multi-arg commands like `"exe" -L "core" "content"`
// have more than 2 quotes and must stay intact, otherwise we strip
// the outer pair around the exe and break the spec.
static void TrimOuterQuotes(char* s)
{
	if (!s) return;
	size_t len = strlen(s);
	if (len < 2 || s[0] != '"' || s[len - 1] != '"') return;
	int quoteCount = 0;
	for (size_t i = 0; i < len; i++) if (s[i] == '"') quoteCount++;
	if (quoteCount != 2) return;
	memmove(s, s + 1, len - 2);
	s[len - 2] = '\0';
}

static bool IsExistingFile(const char* path)
{
#ifdef _WIN32
	DWORD attr = GetFileAttributesA(path);
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

// Parent dir of the program being spawned. Tries quoted path, then whole
// string as a file, then first whitespace-delimited token.
static bool DeriveWorkDir(const char* cmd, char* out, size_t outSize)
{
	if (outSize) out[0] = '\0';
	if (!cmd || !*cmd || outSize == 0) return false;

	char target[1024];
	target[0] = '\0';

	if (cmd[0] == '"') {
		const char* p = cmd + 1;
		size_t n = 0;
		while (*p && *p != '"' && n + 1 < sizeof(target)) target[n++] = *p++;
		target[n] = '\0';
	} else if (IsExistingFile(cmd)) {
		strncpy(target, cmd, sizeof(target) - 1);
		target[sizeof(target) - 1] = '\0';
	} else {
		const char* p = cmd;
		size_t n = 0;
		while (*p && !isspace((unsigned char)*p) && n + 1 < sizeof(target))
			target[n++] = *p++;
		target[n] = '\0';
	}

	if (!target[0]) return false;

	const char* lastFwd = strrchr(target, '/');
	const char* lastBack = strrchr(target, '\\');
	const char* sep = lastFwd > lastBack ? lastFwd : lastBack;
	if (!sep || sep == target) return false;

	size_t dirLen = (size_t)(sep - target);
	if (dirLen >= outSize) dirLen = outSize - 1;
	memcpy(out, target, dirLen);
	out[dirLen] = '\0';
	return dirLen > 0;
}

// Single shared spawn implementation. Both DesktopLaunch (fire-and-forget,
// e.g. Title Maker Test Launch) and SpawnLaunchSpec (overlay-driven game
// launch) call this. Returns true if the OS handed us back a process /
// shell-exec handle; the parent doesn't wait on the child either way.
static bool Launch_DoSpawn(const char* expanded)
{
	if (!expanded || !*expanded) return false;

	char spec[2048];
	strncpy(spec, expanded, sizeof(spec) - 1);
	spec[sizeof(spec) - 1] = '\0';
	TrimOuterQuotes(spec);

	char workdir[1024];
	bool haveWorkdir = DeriveWorkDir(spec, workdir, sizeof(workdir));

#ifdef _WIN32
	// URLs go through ShellExecute. Raw commands run through cmd /C; no
	// /S, so cmd preserves outer quotes for the 2-quote/exists case and
	// strips first/last otherwise.
	if (IsUrl(spec))
	{
		HINSTANCE rc = ShellExecuteA(NULL, "open", spec, NULL,
		                             haveWorkdir ? workdir : NULL, SW_SHOWNORMAL);
		if ((INT_PTR)rc <= 32) {
			snprintf(g_launchLastResult, sizeof(g_launchLastResult),
			         "ShellExecute failed (code %ld): %s", (long)(INT_PTR)rc, spec);
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
		// Quoted .exe spec skips cmd entirely. CreateProcessA with
		// lpApplicationName=NULL parses the first quoted token as the
		// exe with no shell quote-mangling. cmd's strip-first-and-last
		// rule eats our outer wrapper otherwise. .bat / .cmd / etc.
		// stay on the cmd path because they need a shell to interpret.
		bool directExe = false;
		if (spec[0] == '"') {
			const char* exeEnd = strchr(spec + 1, '"');
			if (exeEnd && exeEnd - spec >= 5 &&
			    strncasecmp(exeEnd - 4, ".exe", 4) == 0) {
				directExe = true;
			}
		}
		if (directExe) {
			strncpy(cmd, spec, sizeof(cmd) - 1);
			cmd[sizeof(cmd) - 1] = 0;
		} else {
			snprintf(cmd, sizeof(cmd), "cmd /C \"%s\"", spec);
		}
		if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
		                    DETACHED_PROCESS, NULL,
		                    haveWorkdir ? workdir : NULL,
		                    &si, &pi))
		{
			DWORD err = GetLastError();
			snprintf(g_launchLastResult, sizeof(g_launchLastResult),
			         "CreateProcess failed (error %lu): %s", err, spec);
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
		if (haveWorkdir) (void)chdir(workdir);
		ExecLaunch(spec);
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
	         "Launched: %s", spec);
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

// Lazy-load the Xbox logo PNG (Configs/xboxlogo.png) into an ImGui
// texture and cache the handle for the rest of the process. The PNG is
// pre-decoded from Stock's xboxlogo.xbx via OXDK xbx-convert and lives in
// the desktop-only UIX Configs directory; loading raw PNG with stb_image
// avoids dragging the engine's asset loader (and its TCHAR / Xbox typedef
// chain) into launch.cpp.
unsigned long long LaunchOverlay_LogoGLTex(int* outW, int* outH)
{
	static GuiTexture* s_logoTex = NULL;
	static int    s_logoW = 0;
	static int    s_logoH = 0;
	static bool   s_loadAttempted = false;

	if (!s_loadAttempted) {
		s_loadAttempted = true;
		const char* path = "Configs/xboxlogo.png";
		int w = 0, h = 0, ch = 0;
		unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
		if (pixels) {
			s_logoTex = GuiTextureCreate(w, h, pixels);
			stbi_image_free(pixels);
			s_logoW = w;
			s_logoH = h;
			fprintf(stderr, "[launch] Loaded Xbox logo: %dx%d\n", w, h);
		} else {
			fprintf(stderr, "[launch] Failed to load %s: %s\n",
			        path, stbi_failure_reason());
		}
	}

	if (outW) *outW = s_logoW;
	if (outH) *outH = s_logoH;
	return GuiTextureImId(s_logoTex);
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

	// Translate via the canonical xboxfs.h resolver. handles both
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
