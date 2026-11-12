// desktop_settings.cpp: the desktop.ini table. Every setting the desktop
// keeps is one row here; Load/SaveDesktopSettings and the settings screens
// all read it.

#include "std.h"
#include "dashapp.h"
#include "app_paths.h"
#include "panel_shared.h"
#include "settings_schema.h"
#include "audio_sdl.h"
#include "media_player.h"

extern char s_xemuPath[512];
extern char g_qcowPath[512];
extern char s_steamPath[512];
extern char s_retroarchPath[512];
extern char s_esdePath[512];
extern bool g_showRetroArchTab, g_showSteamTab, g_showEsdeTab;
extern bool g_retroarchFullscreen;
extern int  g_audioBufferFrames;
extern int  g_startupMode;
extern bool g_bUseOnScreenKeyboard, g_bShowBootAnimation;
extern bool g_menuBarAutoHide, g_padModeEnabled;
extern bool g_welcomeDone;
extern bool g_bEmulateXboxRes;
extern float g_masterVolume;
extern bool g_useMilkdropViz, g_showAlbumCover;
extern char g_musicRoot[512], g_moviesRoot[512], g_tvRoot[512];
extern char g_tmdbKey[128], g_romsDir[512];
extern char g_plexToken[256], g_plexClientId[64];
extern char g_xcloudRefreshToken[2048];
extern char g_jellyfinUrl[512], g_jellyfinToken[256], g_jellyfinUserId[64];
extern char g_jellyfinClientId[64], g_jellyfinUserName[128];

// The dashboard writes these itself through Q:\System\config.ini, which is
// this same file. The skin editor sets the skin too.
static char s_use720p[8]        = "Yes";
static char s_useProgressive[8] = "Yes";
char g_currentSkin[64]          = "Stock";

// What desktop.ini says. g_startupMode is this run's mode, which
// --dashboard / --development can override without it being saved.
static int s_startupModeSaved = 0;
static void ApplyStartupMode() { g_startupMode = s_startupModeSaved; }

static void ApplyMsaa()       { g_msaaChangeRequested = true; }
static void ApplyVsync()      { g_vsyncChangeRequested = true; }
static void ApplyDisplay()    { g_displayChangeRequested = true; }
static void ApplyProjection() { TheseusSetProjectionDirty(); }
static void ApplyVolume()
{
    DashAudio_SetMasterVolume(g_masterVolume);
    MediaPlayer_SetMasterVolume(g_masterVolume);
}

static const int   kStartupVals[] = { 0, 1, 2 };
static const char* kStartupLbl[]  = { "Ask on launch", "Dashboard", "Development" };
static const char* kStartupTok[]  = { "", "dashboard", "development" };

static const int   kMsaaVals[] = { 0, 2, 4, 8 };
static const char* kMsaaLbl[]  = { "Off", "2x", "4x", "8x" };

// bgfx has no adaptive vsync; 1 is an old "on" and still loads.
static const int   kVsyncVals[] = { 0, 2 };
static const char* kVsyncLbl[]  = { "On", "Off" };

static const int   kFpsVals[] = { 0, 30, 60, 90, 120, 144, 240 };
static const char* kFpsLbl[]  = { "Unlimited", "30", "60", "90", "120", "144", "240" };

static const int   kRendVals[] = { 0, 1, 2, 3, 4, 5 };
static const char* kRendLbl[]  = { "Auto", "Direct3D 11", "Vulkan", "OpenGL", "Metal", "OpenGL ES" };
static const char* kRendTok[]  = { "auto", "d3d11", "vulkan", "opengl", "metal", "opengles|gles" };

static const int   kResVals[] = { 0, 720, 1080, 1440, 2160 };
static const char* kResLbl[]  = { "Native", "720p", "1080p", "1440p (2K)", "2160p (4K)" };

// The guide's palette (ui_overlay_style.cpp's themes, in their order).
int g_guideTheme = 0;
static const int   kThemeVals[] = { 0, 1, 2 };
static const char* kThemeLbl[]  = { "Dashboard Green", "Blue", "Red" };
static const char* kThemeTok[]  = { "green", "blue", "red" };

static const int   kModeVals[] = { 0, 1, 2 };
static const char* kModeLbl[]  = { "Windowed", "Borderless", "Fullscreen" };

#define ENUM(v, l, t) (int)(sizeof(v) / sizeof(v[0])), v, l, t
#define NOENUM 0, NULL, NULL, NULL

#define S_BOOL(key, sec, page, label, var, def, fl) \
    { key, sec, page, label, SK_BOOL, &var, 0, def, fl, NOENUM, 0, 0, 0, NULL, NULL }
#define S_PATH(key, sec, page, label, var, fl) \
    { key, sec, page, label, SK_PATH, var, (int)sizeof(var), "", fl, NOENUM, 0, 0, 0, NULL, NULL }
#define S_STR(key, sec, page, label, var, def, fl) \
    { key, sec, page, label, SK_STRING, var, (int)sizeof(var), def, fl, NOENUM, 0, 0, 0, NULL, NULL }
#define S_CRT(key, label, field, def, lo, hi) \
    { key, "CRT", "Display", label, SK_FLOAT, &g_crt.field, 0, def, 0, NOENUM, lo, hi, 0.05f, NULL, NULL }

static const SettingDef kDefs[] = {
    S_PATH("XemuPath",      "Desktop", "General", "xemu Path",        s_xemuPath, 0),
    S_PATH("QcowPath",      "Desktop", "General", "qcow2 HDD Image",  g_qcowPath, 0),
    S_PATH("SteamPath",     "Desktop", "Games",   "Steam Folder",     s_steamPath, 0),
    S_PATH("RetroArchPath", "Desktop", "Games",   "RetroArch Folder", s_retroarchPath, 0),
    S_PATH("EsdePath",      "Desktop", "Games",   "ES-DE Folder",     s_esdePath, 0),
    S_BOOL("ShowEsdeTab",   "Desktop", "Games",   "ES-DE Import Tab", g_showEsdeTab, "1", 0),
    S_BOOL("RetroArchFullscreen", "Desktop", "Games", "RetroArch Fullscreen", g_retroarchFullscreen, "1", 0),
    { "AudioBufferFrames", "Desktop", "Audio", "Audio Buffer", SK_INT, &g_audioBufferFrames, 0, "1024",
      SF_RESTART | SF_HIDDEN, NOENUM, 0, 0, 256, NULL, NULL },
    S_BOOL("ShowRetroArchTab", "Desktop", "Games", "RetroArch Tab", g_showRetroArchTab, "1", 0),
    S_BOOL("ShowSteamTab",     "Desktop", "Games", "Steam Tab",     g_showSteamTab,     "1", 0),
    { "StartupMode", "Desktop", "General", "When UIX Desktop Starts", SK_ENUM, &s_startupModeSaved, 0, "", 0,
      ENUM(kStartupVals, kStartupLbl, kStartupTok), 0, 0, 0, NULL, ApplyStartupMode },
    S_BOOL("UseOnScreenKeyboard", "Desktop", "General", "On-Screen Keyboard Only", g_bUseOnScreenKeyboard, "0", 0),
    S_BOOL("ShowBootAnimation",   "Desktop", "General", "Boot Animation",          g_bShowBootAnimation,   "1", 0),
    { "MSAA", "Desktop", "Display", "MSAA", SK_ENUM, &g_msaaSamples, 0, "4", SF_STRICT,
      ENUM(kMsaaVals, kMsaaLbl, NULL), 0, 0, 0, NULL, ApplyMsaa },
    { "Vsync", "Desktop", "Display", "VSync", SK_ENUM, &g_vsyncMode, 0, "0", 0,
      ENUM(kVsyncVals, kVsyncLbl, NULL), 0, 2, 0, NULL, ApplyVsync },
    S_BOOL("MenuBarAutoHide", "Desktop", "General", "Auto-hide Menu Bar", g_menuBarAutoHide, "1", 0),
    S_BOOL("PadMode",         "Desktop", "General", "Controller Tools",   g_padModeEnabled,  "1", SF_HIDDEN),
    S_BOOL("WelcomeDone",     "Desktop", "General", "Welcome Shown",      g_welcomeDone,     "0", SF_HIDDEN),
    { "EmulateXboxRes", "Desktop", "Display", "Emulate Xbox Resolution", SK_BOOL, &g_bEmulateXboxRes, 0, "1", 0,
      NOENUM, 0, 0, 0, NULL, ApplyProjection },
    { "FpsCap", "Desktop", "Display", "FPS Cap", SK_ENUM, &g_fpsCap, 0, "0", 0,
      ENUM(kFpsVals, kFpsLbl, NULL), 0, 100000, 0, NULL, NULL },
    S_BOOL("Hwdec", "Desktop", "Display", "Hardware Video Decode", g_hwdec, "0", 0),
    { "MasterVolume", "Desktop", "Audio", "Master Volume", SK_FLOAT, &g_masterVolume, 0, "1.000",
      SF_PERCENT | SF_CLAMP, NOENUM, 0.0f, 1.0f, 0.05f, NULL, ApplyVolume },
    S_BOOL("UseMilkdropViz", "Desktop", "Audio", "projectM Visualizer", g_useMilkdropViz, "0", 0),
    S_BOOL("ShowAlbumCover", "Desktop", "Audio", "Album Cover in Orb",  g_showAlbumCover, "0", 0),
    { "Renderer", "Desktop", "Display", "Renderer", SK_ENUM, &g_rendererPref, 0, "auto", SF_RESTART,
      ENUM(kRendVals, kRendLbl, kRendTok), 0, 0, 0, NULL, NULL },
    { "Resolution", "Desktop", "Display", "Resolution", SK_ENUM, &g_windowResolution, 0, "720", SF_STRICT,
      ENUM(kResVals, kResLbl, NULL), 0, 0, 0, NULL, ApplyDisplay },
    { "DisplayMode", "Desktop", "Display", "Display Mode", SK_ENUM, &g_windowMode, 0, "0", SF_STRICT,
      ENUM(kModeVals, kModeLbl, NULL), 0, 0, 0, NULL, ApplyDisplay },

    { "GuideTheme", "Desktop", "Display", "Guide Color", SK_ENUM, &g_guideTheme, 0, "green", SF_STRICT,
      ENUM(kThemeVals, kThemeLbl, kThemeTok), 0, 0, 0, NULL, NULL },

    S_BOOL("CRT_Enabled", "CRT", "Display", "CRT Effect", g_crt.enabled, "0", 0),
    S_CRT("CRT_Scanlines",  "Scanlines",     scanlineIntensity, "0.500", 0.0f, 1.0f),
    S_CRT("CRT_Curvature",  "Curvature",     curvature,         "0.400", 0.0f, 2.0f),
    S_CRT("CRT_Phosphor",   "Phosphor Mask", phosphorMask,      "0.300", 0.0f, 1.0f),
    S_CRT("CRT_Vignette",   "Vignette",      vignette,          "0.300", 0.0f, 2.0f),
    S_CRT("CRT_Bloom",      "Bloom",         bloom,             "0.150", 0.0f, 1.0f),
    S_CRT("CRT_Flicker",    "Flicker",       flickerAmount,     "0.200", 0.0f, 1.0f),
    S_CRT("CRT_ColorBleed", "Color Bleed",   colorBleed,        "1.000", 0.0f, 4.0f),
    S_CRT("CRT_Brightness", "Brightness",    brightness,        "1.050", 0.8f, 1.3f),

    S_PATH("MusicRoot",  "Library", "Media", "Music Folder",    g_musicRoot, 0),
    S_PATH("MoviesRoot", "Library", "Media", "Movies Folder",   g_moviesRoot, 0),
    S_PATH("TvRoot",     "Library", "Media", "TV Shows Folder", g_tvRoot, 0),
    S_STR("TMDBKey",     "Library", "Media", "TMDB API Key",    g_tmdbKey, "", SF_SECRET),
    S_PATH("RomsDir",    "Library", "Games", "ROMs Folder",     g_romsDir, 0),
    S_STR("PlexToken",          "Library", "Media", "Plex Token",     g_plexToken,          "", SF_SECRET | SF_HIDDEN),
    S_STR("PlexClientId",       "Library", "Media", "Plex Client",    g_plexClientId,       "", SF_SECRET | SF_HIDDEN),
    S_STR("XcloudRefreshToken", "Library", "Media", "Xbox Sign-in",   g_xcloudRefreshToken, "", SF_SECRET | SF_HIDDEN),
    S_STR("JellyfinUrl",        "Library", "Media", "Jellyfin Server", g_jellyfinUrl,       "", SF_HIDDEN),
    S_STR("JellyfinToken",      "Library", "Media", "Jellyfin Token",  g_jellyfinToken,     "", SF_SECRET | SF_HIDDEN),
    S_STR("JellyfinUserId",     "Library", "Media", "Jellyfin User",   g_jellyfinUserId,    "", SF_SECRET | SF_HIDDEN),
    S_STR("JellyfinClientId",   "Library", "Media", "Jellyfin Client", g_jellyfinClientId,  "", SF_SECRET | SF_HIDDEN),
    S_STR("JellyfinUserName",   "Library", "Media", "Jellyfin Name",   g_jellyfinUserName,  "", SF_HIDDEN),

    S_STR("Use 720p",        "Progressive",        "Display", "720p",        s_use720p,        "Yes", SF_SHARED | SF_HIDDEN),
    S_STR("Use Progressive", "Progressive",        "Display", "Progressive", s_useProgressive, "Yes", SF_SHARED | SF_HIDDEN),
    S_STR("Current Skin",    "Dashboard Settings", "General", "Skin",        g_currentSkin,    "Stock", SF_SHARED | SF_HIDDEN),
};

const SettingsTable& DesktopSettings_Table()
{
    static const SettingsTable t = { kDefs, (int)(sizeof(kDefs) / sizeof(kDefs[0])) };
    return t;
}

const SettingDef* DesktopSettings_Find(const char* key)
{
    const SettingsTable& t = DesktopSettings_Table();
    for (int i = 0; i < t.count; i++)
        if (strcmp(t.defs[i].key, key) == 0) return &t.defs[i];
    return NULL;
}

void LoadDesktopSettings()
{
    Settings_Load(DesktopSettings_Table(), AppPath("Configs/desktop.ini"));
    ApplyStartupMode();
    TheseusSetProjectionDirty();
}

void SaveDesktopSettings()
{
    Plat_MkdirP(AppPath_Tree("Configs"));
    Settings_Save(DesktopSettings_Table(), AppPath("Configs/desktop.ini"));
}
