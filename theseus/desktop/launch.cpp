// launch.cpp: desktop title-launch bridge. Implements
// DesktopLaunchTitle, the desktop-side equivalent of XLaunchNewImage:
// looks the title up in games.ini, mutes audio, minimizes the
// window, and execs the host platform's launcher.

#include "app_paths.h"
#include "std.h"
#include "launch.h"
#include "panel_shared.h"
#include "xcloud_client.h"

#include <SDL.h>

#include "host.h"

#include "virtual_games.h"
#include "stb_image.h"
#include "path_template.h"
#include "launchers/launcher.h"

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
	HostFileInfo info;
	return Host_Stat(path, &info) && !(info.attrs & HOST_ATTR_DIR);
}

// Quotes an unquoted program path that has spaces in it, like anything under
// "Program Files (x86)". Takes the longest leading run that names a real file.
static void QuoteLeadingPath(char* s, size_t size)
{
	if (!s || !s[0] || s[0] == '"') return;
	size_t len = strlen(s);
	size_t best = 0;
	char probe[2048];
	for (size_t i = 1; i <= len && i < sizeof(probe); i++) {
		if (s[i] != '\0' && !isspace((unsigned char)s[i])) continue;
		memcpy(probe, s, i);
		probe[i] = '\0';
		if (IsExistingFile(probe)) best = i;
	}
	if (best == 0 || len + 3 > size) return;
	bool hasSpace = false;
	for (size_t i = 0; i < best; i++) if (isspace((unsigned char)s[i])) hasSpace = true;
	if (!hasSpace) return;
	memmove(s + best + 2, s + best, len - best + 1);
	memmove(s + 1, s, best);
	s[0] = '"';
	s[best + 1] = '"';
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
	if (!IsUrl(spec)) QuoteLeadingPath(spec, sizeof(spec));

	char workdir[1024];
	bool haveWorkdir = DeriveWorkDir(spec, workdir, sizeof(workdir));

	if (!Host_Spawn(spec, IsUrl(spec), IsSteamUrl(spec), haveWorkdir ? workdir : NULL,
	                g_launchLastResult, sizeof(g_launchLastResult))) {
		fprintf(stderr, "[launch] %s\n", g_launchLastResult);
		return false;
	}

	snprintf(g_launchLastResult, sizeof(g_launchLastResult),
	         "Launched: %s", spec);
	return true;
}

// Expand $VARs, route the spec through the matching launcher module
// (Build()), log the trace, then spawn. Both public dispatchers funnel
// through this so the trace, error reporting, and command-form rules
// live in one place.
static bool Launch_ExpandAndSpawn(const char* spec,
                                  char* finalOut, size_t finalSize)
{
	if (!spec || !spec[0]) return false;
	char expanded[2048];
	PathTemplate_Expand(spec, expanded, sizeof(expanded));
	Launcher_Build(expanded, finalOut, finalSize);
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
	Launch_ExpandAndSpawn(spec, finalCmd, sizeof(finalCmd));
}

// Spawn called by the launch overlay tick once the fade-in completes.
// Identical to DesktopLaunch except we minimize the dashboard window
// after a successful spawn so the game gets focus.
static void SpawnLaunchSpec(const char* spec)
{
	char finalCmd[2048];
	if (!Launch_ExpandAndSpawn(spec, finalCmd, sizeof(finalCmd))) return;

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
		const char* path = AppPath("Configs/xboxlogo.png");
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

	// xCloud / remote play take over the window in-process through the same
	// helper the Settings panel uses. They must NOT go through the launch
	// overlay + window-minimize + spawn path a real on-disk title uses, so
	// intercept here before any of that.
	extern void Xcloud_LaunchStream(const char* type, const char* id);
	if (strncmp(spec, "xcloud://", 9) == 0) {
		fprintf(stderr, "[launch] xCloud stream: %s\n", spec + 9);
		Xcloud_LaunchStream("cloud", spec + 9);
		return;
	}
	if (strncmp(spec, "xhome://", 8) == 0) {
		fprintf(stderr, "[launch] remote play: %s\n", spec + 8);
		Xcloud_LaunchStream("home", spec + 8);
		return;
	}

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

	// Translate via xbox_path.h. Handles both
	// drive-letter ("E:\...") and device-path ("\Device\Harddisk0\...")
	// forms and routes to Library/Configs/Data as appropriate.
	char localPath[512];
	if (!XboxPath_Translate(devicePath, localPath, sizeof(localPath)) || !*localPath) {
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
