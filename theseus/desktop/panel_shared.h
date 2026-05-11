// panel_shared.h: shared state for desktop UI panels. Central
// declarations for globals shared between ImGui panels and the main
// loop; definitions live in the file that owns each global.
#pragma once

#include <SDL.h>

// Forward declarations
struct IDirect3DDevice8;
struct CRTState;

// ============================================================================
// SDL/GL Core (defined in sdl_main.cpp)
// ============================================================================

extern SDL_Window*   g_pSDLWindow;
extern SDL_GLContext  g_pGLContext;
extern CRTState      g_crt;

// ============================================================================
// Application Mode
// ============================================================================

extern int  g_startupMode;       // 0=ask, 1=dashboard, 2=development (sdl_main.cpp)
extern bool g_extractedMode;     // true = loaded from extracted XAPs (xap_editor.cpp)
extern bool g_debugMode;         // true = dev tools active (sdl_main.cpp)

// ============================================================================
// Panel Visibility (each defined in its own tool's .cpp)
// ============================================================================

extern bool g_inspectorOpen;     // inspector.cpp
extern bool g_xapEditorOpen;     // xap_editor.cpp
extern bool g_titleMakerOpen;    // title_maker.cpp
extern bool g_hddBrowserOpen;    // hdd_browser.cpp

// ============================================================================
// Rendering (defined in sdl_main.cpp unless noted)
// ============================================================================

extern bool g_wireframe;         // menu_bar.cpp
extern int  g_msaaSamples;       // sdl_main.cpp
extern bool g_msaaChangeRequested;
extern int  g_vsyncMode;         // 0=adaptive, 1=on, 2=off
extern bool g_vsyncChangeRequested;
extern int  g_fpsCap;            // 0=unlimited; manual frame pacing target
extern bool g_hwdec;             // mpv hwdec; takes effect on next media open
extern int  g_windowResolution;  // 720 / 1080 / 1440 / 2160; 0 = native
extern int  g_windowMode;        // 0=windowed, 1=borderless, 2=exclusive
extern bool g_displayChangeRequested;
extern bool g_scrollToSelected;

// ============================================================================
// Audio (defined in sdl_main.cpp)
// ============================================================================

extern bool  g_audioMuted;
extern float g_muteOverlayTimer;

// ============================================================================
// Paths (defined in sdl_main.cpp)
// ============================================================================

extern char s_xemuPath[512];
extern char g_qcowPath[512];
extern char s_steamPath[512];   // user's Steam install root (contains steamapps/)
extern char s_retroarchPath[512]; // user's RetroArch install root (contains retroarch + cores/)
extern bool g_showRetroArchTab;
extern bool g_showSteamTab;

// ============================================================================
// Restart Control (defined in sdl_main.cpp)
// ============================================================================

extern bool g_desktopRestartRequested;
extern bool g_desktopRestartMuted;

// ============================================================================
// Window Management (defined in sdl_main.cpp)
// ============================================================================

extern SDL_Window* g_pXapEditorWindow;
void CreateXapEditorWindow();
void DestroyXapEditorWindow();

// ============================================================================
// Settings I/O (defined in sdl_main.cpp)
// ============================================================================

void LoadDesktopSettings();
void SaveDesktopSettings();

// ============================================================================
// Audio Helpers (defined in audio_sdl.cpp, C linkage)
// ============================================================================

extern "C" void DashAudio_MuteAll();
extern "C" void DashAudio_UnmuteAll();

// ============================================================================
// Input
// ============================================================================

bool ImGui_WantsKeyboard();
