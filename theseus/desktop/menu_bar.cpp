// menu_bar.cpp: desktop top-level ImGui menu bar + Settings/About/Shortcuts
// windows. The Development menu only renders in Development Mode.

#include "std.h"
#include "dashapp.h"
#include "panel_shared.h"
#include "title_maker.h"
#include "hdd_browser.h"
#include "audio_sdl.h"
#include "skin_editor.h"
#include "imgui.h"
#include "plex_client.h"
#include "jellyfin_client.h"
#include "media_player.h"
#include "milkdrop_window.h"

extern bool  g_bWireframe;
extern float g_masterVolume;

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

extern bool s_scanPanelVisible;   // defined later in this file

extern "C" void MediaDB_ScanAndCache();
extern "C" void MediaDB_RefreshMovies();
extern "C" void MediaDB_RefreshShows();
extern "C" int  MediaDB_GetMovieCount();
extern "C" int  MediaDB_GetShowCount();
extern "C" int  MediaDB_IsScanning();
extern "C" int  MediaDB_GetScanProgress();
extern "C" int  MediaDB_GetScanTotal();
extern "C" const char* MediaDB_GetScanPhase();

// Library roots (defined in sdl_main.cpp).
extern char g_musicRoot[512];
extern char g_moviesRoot[512];
extern char g_tvRoot[512];
extern char g_tmdbKey[128];

// DashMusic_* declared in audio_sdl.h (already included above).

// ============================================================================
// Internal State
// ============================================================================

bool g_settingsOpen = false;
static bool s_aboutOpen = false;
static bool s_shortcutsOpen = false;
bool g_projectMConfigOpen = false;

// Old "Open Media..." file-browser removed. Playback now flows through
// the Media Library (CMediaCollection.PlayMovie/PlayEpisode -> MediaUI
// fullscreen). The legacy CDVDPlayer XAP scene was painful to wire up
// and is no longer the desktop's playback path.

void ToggleSettingsWindow() { g_settingsOpen = !g_settingsOpen; }

// ============================================================================
// CRT Presets
// ============================================================================

struct CRTPreset {
    const char* name;
    float scanlines, curvature, phosphor, vignette;
    float bloom, flicker, colorBleed, brightness;
};

static const CRTPreset s_crtPresets[] = {
    { "Subtle",  0.2f, 0.1f, 0.1f, 0.1f, 0.10f, 0.05f, 0.5f, 1.00f },
    { "Classic", 0.5f, 0.4f, 0.3f, 0.3f, 0.15f, 0.20f, 1.0f, 1.05f },
    { "Heavy",   0.8f, 1.0f, 0.6f, 0.8f, 0.30f, 0.40f, 2.0f, 1.10f },
    { "Off",     0.0f, 0.0f, 0.0f, 0.0f, 0.00f, 0.00f, 0.0f, 1.00f },
};

static void ApplyCRTPreset(const CRTPreset& p) {
    g_crt.scanlineIntensity = p.scanlines;
    g_crt.curvature = p.curvature;
    g_crt.phosphorMask = p.phosphor;
    g_crt.vignette = p.vignette;
    g_crt.bloom = p.bloom;
    g_crt.flickerAmount = p.flicker;
    g_crt.colorBleed = p.colorBleed;
    g_crt.brightness = p.brightness;
}

// ============================================================================
// MSAA Options (shared between menu and settings)
// ============================================================================

static const char* s_msaaLabels[] = { "Off", "2x", "4x", "8x" };
static const int   s_msaaValues[] = { 0, 2, 4, 8 };
static const int   s_msaaCount = 4;

static const char* s_vsyncLabels[] = { "Adaptive", "On", "Off" };

static const char* s_resLabels[] = { "Native", "720p", "1080p", "1440p (2K)", "2160p (4K)" };
static const int   s_resValues[] = {  0,        720,    1080,    1440,         2160 };
static const int   s_resCount    = 5;
static int GetResIndex() {
    for (int i = 0; i < s_resCount; i++)
        if (s_resValues[i] == g_windowResolution) return i;
    return 0;
}

static const char* s_displayModeLabels[] = { "Windowed", "Borderless", "Fullscreen" };

static const char* s_fpsLabels[] = { "Unlimited", "30", "60", "90", "120", "144", "240" };
static const int   s_fpsValues[] = { 0,           30,   60,   90,   120,   144,   240 };
static const int   s_fpsCount    = 7;
static int GetFpsIndex() {
    for (int i = 0; i < s_fpsCount; i++)
        if (s_fpsValues[i] == g_fpsCap) return i;
    return 0;
}

static void RestoreDisplayDefaults() {
    g_crt.enabled = false;
    ApplyCRTPreset(s_crtPresets[1]); // "Classic"
    g_bWireframe = false;
    if (g_msaaSamples != 4) { g_msaaSamples = 4; g_msaaChangeRequested = true; }
    if (g_vsyncMode  != 0) { g_vsyncMode  = 0; g_vsyncChangeRequested = true; }
    g_fpsCap = 0;
    g_hwdec  = false;
}

static int GetMSAAIndex() {
    for (int i = 0; i < s_msaaCount; i++)
        if (s_msaaValues[i] == g_msaaSamples) return i;
    return 0;
}

// ============================================================================
// Menu Bar
// ============================================================================

void RenderMainMenuBar() {
    if (!ImGui::BeginMainMenuBar())
        return;

    // ---- File ----
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Restart Dashboard", "Ctrl+R")) {
            g_desktopRestartRequested = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...", "F4")) {
            g_settingsOpen = true;
        }
        extern bool g_showMenuBar;
        if (ImGui::MenuItem("Hide Menu Bar", "F10")) {
            g_showMenuBar = false;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            SDL_Event quit;
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
        }
        ImGui::EndMenu();
    }

    // ---- Tools (shipping tools only; dev tools live under Development) ----
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Title Maker", "F3", g_titleMakerOpen)) {
            g_titleMakerOpen = !g_titleMakerOpen;
        }
        if (ImGui::MenuItem("HDD Browser", "F5", g_hddBrowserOpen)) {
            g_hddBrowserOpen = !g_hddBrowserOpen;
        }
        extern bool g_playlistMakerOpen;
        if (ImGui::MenuItem("Playlist Maker", "F6", g_playlistMakerOpen)) {
            g_playlistMakerOpen = !g_playlistMakerOpen;
        }
        if (ImGui::MenuItem("Skin Editor", NULL, g_skinEditorOpen)) {
            g_skinEditorOpen = !g_skinEditorOpen;
        }
        ImGui::EndMenu();
    }

    // ---- View (display only) ----
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("CRT Effect", NULL, g_crt.enabled)) {
            g_crt.enabled = !g_crt.enabled;
            SaveDesktopSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Wireframe", NULL, g_bWireframe)) {
            g_bWireframe = !g_bWireframe;
        }
        if (ImGui::BeginMenu("MSAA")) {
            for (int i = 0; i < s_msaaCount; i++) {
                if (ImGui::MenuItem(s_msaaLabels[i], NULL, g_msaaSamples == s_msaaValues[i])) {
                    g_msaaSamples = s_msaaValues[i];
                    g_msaaChangeRequested = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // ---- Audio ----
    if (ImGui::BeginMenu("Audio")) {
        if (ImGui::MenuItem("Mute", "Ctrl+M", g_audioMuted)) {
            g_audioMuted = !g_audioMuted;
            if (g_audioMuted) DashAudio_MuteAll();
            else DashAudio_UnmuteAll();
            g_muteOverlayTimer = 2.0f;
        }
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Volume");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        float volPct = g_masterVolume * 100.0f;
        if (ImGui::SliderFloat("##mastervol", &volPct, 0.0f, 100.0f, "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp)) {
            float v = volPct / 100.0f;
            DashAudio_SetMasterVolume(v);
            MediaPlayer_SetMasterVolume(v);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            SaveDesktopSettings();
        }
        ImGui::EndMenu();
    }

    // ---- Development (dev-mode only) ----
    if (g_startupMode == 2 || g_extractedMode) {
        if (ImGui::BeginMenu("Development")) {
            if (ImGui::MenuItem("Inspector", "F1", g_debugMode)) {
                g_debugMode = !g_debugMode;
                g_inspectorOpen = g_debugMode;
                if (g_pD3DDev) {
                    g_pD3DDev->m_inspectorEnabled = g_debugMode;
                    if (!g_debugMode) {
                        g_pD3DDev->m_inspectorSelectedNode = NULL;
                        g_pD3DDev->m_inspectorHitID = -1;
                    }
                }
                if (!g_debugMode && g_bWireframe) {
                    g_bWireframe = false;
#ifndef THESEUS_USE_BGFX
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
                }
            }
            if (ImGui::MenuItem("XAP Editor", "F2", g_xapEditorOpen)) {
                g_xapEditorOpen = !g_xapEditorOpen;
                if (g_xapEditorOpen)
                    CreateXapEditorWindow();
                else
                    DestroyXapEditorWindow();
            }
            ImGui::EndMenu();
        }
    }

    // ---- Help ----
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts")) {
            s_shortcutsOpen = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("About UIX Desktop")) {
            s_aboutOpen = true;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// ============================================================================
// Settings Window
// ============================================================================

void RenderSettingsWindow() {
    if (!g_settingsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(540, 0), ImVec2(540, 800));
    ImGuiWindowFlags settingsFlags = ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Settings", &g_settingsOpen, settingsFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("SettingsTabs")) {

        // ---- General ----
        if (ImGui::BeginTabItem("General")) {
            ImGui::Spacing();

            // Startup mode
            ImGui::Text("Startup Mode:");
            ImGui::SameLine();
            const char* modeLabels[] = { "Ask on launch", "Dashboard", "Development" };
            if (ImGui::Combo("##startupmode", &g_startupMode, modeLabels, 3)) {
                SaveDesktopSettings();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Paths
            ImGui::Text("xemu Path:");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
            ImGui::InputText("##xemupath", s_xemuPath, sizeof(s_xemuPath));
            ImGui::SameLine();
            if (ImGui::Button("Save##xemu"))
                SaveDesktopSettings();

            ImGui::Spacing();
            ImGui::Text("qcow2 HDD Image:");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##qcowpath", g_qcowPath, sizeof(g_qcowPath));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            extern bool g_bUseOnScreenKeyboard;
            if (ImGui::Checkbox("Use On-Screen Keyboard Only", &g_bUseOnScreenKeyboard))
                SaveDesktopSettings();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("When checked, dashboard text fields ignore your physical\nkeyboard and require gamepad navigation of the on-screen keyboard.");

            extern bool g_bShowBootAnimation;
            if (ImGui::Checkbox("Show Boot Animation", &g_bShowBootAnimation))
                SaveDesktopSettings();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Plays the original Xbox boot animation once on app startup\nbefore the dashboard initializes. Press Esc/Enter/Space to skip.");

            ImGui::Spacing();
            if (ImGui::Button("Save All Settings"))
                SaveDesktopSettings();

            ImGui::EndTabItem();
        }

        // ---- Display ----
        if (ImGui::BeginTabItem("Display")) {
            ImGui::Spacing();

            // Slider drags fire IsItemDeactivatedAfterEdit on release; saves
            // once at end of drag, not every frame.
            if (ImGui::Checkbox("CRT Effect", &g_crt.enabled)) SaveDesktopSettings();
            if (g_crt.enabled) {
                ImGui::Spacing();
                ImGui::SliderFloat("Scanlines",     &g_crt.scanlineIntensity, 0.0f, 1.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Curvature",     &g_crt.curvature,         0.0f, 2.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Phosphor Mask", &g_crt.phosphorMask,      0.0f, 1.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Vignette",      &g_crt.vignette,          0.0f, 2.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Bloom",         &g_crt.bloom,             0.0f, 1.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Flicker",       &g_crt.flickerAmount,     0.0f, 1.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Color Bleed",   &g_crt.colorBleed,        0.0f, 4.0f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();
                ImGui::SliderFloat("Brightness",    &g_crt.brightness,        0.8f, 1.3f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();

                ImGui::Spacing();
                ImGui::Text("Presets:");
                ImGui::SameLine();
                for (int i = 0; i < (int)(sizeof(s_crtPresets) / sizeof(s_crtPresets[0])); i++) {
                    if (i > 0) ImGui::SameLine();
                    if (ImGui::SmallButton(s_crtPresets[i].name)) {
                        ApplyCRTPreset(s_crtPresets[i]);
                        SaveDesktopSettings();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Toggles grouped together
            if (ImGui::Checkbox("Wireframe", &g_bWireframe)) SaveDesktopSettings();
            if (ImGui::Checkbox("Hardware video decode (mpv)", &g_hwdec)) SaveDesktopSettings();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off = software decode, safest. On = faster on AMD/NVIDIA.\nApplies to next media open.");

            ImGui::Spacing();

            // Dropdowns: fixed label column + widget column so they line up.
            const float kLabelX  = 90.0f;
            const float kWidgetW = 160.0f;

            ImGui::AlignTextToFramePadding(); ImGui::Text("MSAA:");
            ImGui::SameLine(kLabelX); ImGui::SetNextItemWidth(kWidgetW);
            int msaaIdx = GetMSAAIndex();
            if (ImGui::Combo("##msaa", &msaaIdx, s_msaaLabels, s_msaaCount)) {
                g_msaaSamples = s_msaaValues[msaaIdx];
                g_msaaChangeRequested = true;
                SaveDesktopSettings();
            }

            ImGui::AlignTextToFramePadding(); ImGui::Text("VSync:");
            ImGui::SameLine(kLabelX); ImGui::SetNextItemWidth(kWidgetW);
            if (ImGui::Combo("##vsync", &g_vsyncMode, s_vsyncLabels, 3)) {
                g_vsyncChangeRequested = true;
                SaveDesktopSettings();
            }

            ImGui::AlignTextToFramePadding(); ImGui::Text("FPS Cap:");
            ImGui::SameLine(kLabelX); ImGui::SetNextItemWidth(kWidgetW);
            int fpsIdx = GetFpsIndex();
            if (ImGui::Combo("##fpscap", &fpsIdx, s_fpsLabels, s_fpsCount)) {
                g_fpsCap = s_fpsValues[fpsIdx];
                SaveDesktopSettings();
            }

            ImGui::AlignTextToFramePadding(); ImGui::Text("Resolution:");
            ImGui::SameLine(kLabelX); ImGui::SetNextItemWidth(kWidgetW);
            int resIdx = GetResIndex();
            if (ImGui::Combo("##res", &resIdx, s_resLabels, s_resCount)) {
                g_windowResolution = s_resValues[resIdx];
                g_displayChangeRequested = true;
                SaveDesktopSettings();
            }

            ImGui::AlignTextToFramePadding(); ImGui::Text("Renderer:");
            ImGui::SameLine(kLabelX); ImGui::SetNextItemWidth(kWidgetW);
            static const char* s_rendererLabels[] = {
                "Auto",
                "Direct3D 11",
                "Vulkan",
                "OpenGL",
                "Metal",
                "OpenGL ES",
            };
            if (ImGui::Combo("##renderer", &g_rendererPref, s_rendererLabels, 6)) {
                SaveDesktopSettings();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Takes effect on next launch.\n"
                    "Auto picks the best backend for your OS\n"
                    "(Metal on macOS, Direct3D 11 on Windows,\n"
                    "Vulkan on Linux). Pick a specific backend\n"
                    "if Auto doesn't work for you.");

            ImGui::AlignTextToFramePadding(); ImGui::Text("Display Mode:");
            ImGui::SameLine(kLabelX); ImGui::SetNextItemWidth(kWidgetW);
            if (ImGui::Combo("##dispmode", &g_windowMode, s_displayModeLabels, 3)) {
                g_displayChangeRequested = true;
                SaveDesktopSettings();
            }

            ImGui::Spacing();
            if (ImGui::Button("Restore Defaults")) {
                RestoreDisplayDefaults();
                SaveDesktopSettings();
            }

            ImGui::EndTabItem();
        }

        // ---- Audio ----
        if (ImGui::BeginTabItem("Audio")) {
            ImGui::Spacing();

            if (ImGui::Checkbox("Mute", &g_audioMuted)) {
                if (g_audioMuted) DashAudio_MuteAll();
                else DashAudio_UnmuteAll();
                g_muteOverlayTimer = 2.0f;
                SaveDesktopSettings();
            }

            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Master Volume");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            float volPct = g_masterVolume * 100.0f;
            if (ImGui::SliderFloat("##settingsmastervol", &volPct,
                                   0.0f, 100.0f, "%.0f%%",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                float v = volPct / 100.0f;
                DashAudio_SetMasterVolume(v);
                MediaPlayer_SetMasterVolume(v);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveDesktopSettings();

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scales dashboard sounds, music, and media\n"
                                  "playback. Mute overrides this until cleared.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            extern bool g_useMilkdropViz;
            if (ImGui::Checkbox("Use projectM visualizer (experimental)",
                                &g_useMilkdropViz)) {
                SaveDesktopSettings();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Replace the legacy music-scene visualizer with the\n"
                    "projectM (MilkDrop) renderer. X+Y on the music scene\n"
                    "fullscreens it once enabled.");
            }

            extern bool g_showAlbumCover;
            if (ImGui::Checkbox("Show album cover in orb",
                                &g_showAlbumCover)) {
                SaveDesktopSettings();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "When a soundtrack folder contains album.png, album.jpg,\n"
                    "or similar, show it in the orb instead of the visualizer\n"
                    "during playback.");
            }

            extern bool g_projectMConfigOpen;
            if (ImGui::Button("Configure projectM...")) {
                g_projectMConfigOpen = true;
            }

            ImGui::EndTabItem();
        }

        // ---- Media Library ----
        if (ImGui::BeginTabItem("Media")) {
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Local Library", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Configure where the dashboard scans for media. Empty values fall back to the bundled defaults shown in placeholder text.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Music
            ImGui::Text("Music Root");
            ImGui::SameLine();
            ImGui::TextDisabled("(%d soundtrack%s loaded)",
                DashMusic_GetSoundtrackCount(),
                DashMusic_GetSoundtrackCount() == 1 ? "" : "s");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 130);
            ImGui::InputTextWithHint("##musicroot", "Data/Music",
                g_musicRoot, sizeof(g_musicRoot));
            ImGui::SameLine();
            if (ImGui::Button("Refresh##music")) {
                DashMusic_Scan(DashMusic_GetConfiguredRoot());
                SaveDesktopSettings();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool scanning = MediaDB_IsScanning() != 0;

            // Movies
            ImGui::Text("Movies Root");
            ImGui::SameLine();
            ImGui::TextDisabled("(%d movies)", MediaDB_GetMovieCount());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##moviesroot", "e.g. /Users/you/Movies",
                g_moviesRoot, sizeof(g_moviesRoot));

            ImGui::Spacing();

            // TV
            ImGui::Text("TV Shows Root");
            ImGui::SameLine();
            ImGui::TextDisabled("(%d shows)", MediaDB_GetShowCount());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##tvroot", "e.g. /Users/you/TV",
                g_tvRoot, sizeof(g_tvRoot));

            ImGui::Spacing();

            if (scanning) {
                // Inline progress instead of a floating panel, user is
                // already on this tab, show the scan status here.
                const char* phase = MediaDB_GetScanPhase();
                int prog  = MediaDB_GetScanProgress();
                int total = MediaDB_GetScanTotal();
                ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
                    "%s", (phase && *phase) ? phase : "Working...");
                if (total > 0) {
                    char overlay[64];
                    snprintf(overlay, sizeof(overlay), "%d / %d", prog, total);
                    ImGui::ProgressBar((float)prog / (float)total, ImVec2(-FLT_MIN, 0.0f), overlay);
                } else if (prog > 0) {
                    char overlay[64];
                    snprintf(overlay, sizeof(overlay), "%d found", prog);
                    float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 2.0f);
                    ImGui::ProgressBar(pulse, ImVec2(-FLT_MIN, 0.0f), overlay);
                } else {
                    float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 2.0f);
                    ImGui::ProgressBar(pulse, ImVec2(-FLT_MIN, 0.0f), "");
                }
                ImGui::TextDisabled("Scan continues if you close this window.");
            } else {
                if (ImGui::Button("Refresh Media Library")) {
                    SaveDesktopSettings();
                    MediaDB_ScanAndCache();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // TMDB API key (optional; enables plot/poster lookups).
            ImGui::Text("TMDB API Key");
            ImGui::SameLine();
            if (g_tmdbKey[0])
                ImGui::TextDisabled("(configured)");
            else
                ImGui::TextDisabled("(empty - plots/posters disabled)");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##tmdbkey", "Get a free v3 key at themoviedb.org",
                g_tmdbKey, sizeof(g_tmdbKey), ImGuiInputTextFlags_Password);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Save Library Paths"))
                SaveDesktopSettings();
            } // Local Library header

            if (ImGui::CollapsingHeader("Plex")) {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Sign in to your Plex account to browse your libraries from the "
                "Media -> Plex scene.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (Plex_HasToken()) {
                Plex_StartSync();

                ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
                    "Signed in.");
                ImGui::Spacing();

                if (!Plex_SyncReady()) {
                    ImGui::Text("%s", Plex_SyncPhase().c_str());
                    float frac = Plex_SyncProgress() / 1000.0f;
                    ImGui::ProgressBar(frac, ImVec2(-1, 6.0f), "");
                    ImGui::Spacing();
                } else {
                    ImGui::TextDisabled("Library ready (%d libraries).",
                        (int)Plex_Cache_GetLibraries().size());
                    ImGui::Spacing();
                }

                if (ImGui::Button("Sign out")) {
                    Plex_SignOut();
                    SaveDesktopSettings();
                }
            } else if (Plex_PinAuthInFlight()) {
                std::string code = Plex_GetPinCode();
                if (code.empty()) {
                    ImGui::Text("Requesting code...");
                } else {
                    ImGui::Text("1. Open");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                        "plex.tv/link");
                    ImGui::SameLine();
                    ImGui::Text("on any device.");
                    ImGui::Text("2. Enter this code:");
                    ImGui::Spacing();

                    // Big, centered code display.
                    ImFont* font = ImGui::GetFont();
                    float old = font->Scale;
                    font->Scale = 3.0f;
                    ImGui::PushFont(font);
                    ImVec2 sz = ImGui::CalcTextSize(code.c_str());
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                        (avail - sz.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.85f, 1.0f, 0.85f, 1.0f),
                        "%s", code.c_str());
                    ImGui::PopFont();
                    font->Scale = old;
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Cancel")) Plex_CancelPinAuth();
            } else {
                ImGui::TextDisabled("Not signed in.");
                ImGui::Spacing();
                if (ImGui::Button("Sign in to Plex")) Plex_StartPinAuth();
            }
            } // Plex header

            if (ImGui::CollapsingHeader("Jellyfin")) {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Sign in to your Jellyfin server. Enter the server URL, then "
                "use Quick Connect to approve the 6-letter code on the server's "
                "web UI.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            static char s_urlBuf[512];
            static bool s_urlInit = false;
            if (!s_urlInit) {
                std::string cur = Jellyfin_GetServerUrl();
                strncpy(s_urlBuf, cur.c_str(), sizeof(s_urlBuf) - 1);
                s_urlBuf[sizeof(s_urlBuf) - 1] = 0;
                s_urlInit = true;
            }
            ImGui::Text("Server URL");
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##jellyurl", s_urlBuf, sizeof(s_urlBuf))) {
                Jellyfin_SetServerUrl(s_urlBuf);
            }
            ImGui::PopItemWidth();
            ImGui::Spacing();

            if (Jellyfin_HasToken()) {
                Jellyfin_StartSync();
                ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
                    "Signed in as %s.", Jellyfin_GetUserName().c_str());
                ImGui::Spacing();

                if (!Jellyfin_SyncReady()) {
                    ImGui::Text("%s", Jellyfin_SyncPhase().c_str());
                    float frac = Jellyfin_SyncProgress() / 1000.0f;
                    ImGui::ProgressBar(frac, ImVec2(-1, 6.0f), "");
                    ImGui::Spacing();
                } else {
                    ImGui::TextDisabled("Library ready (%d libraries).",
                        (int)Jellyfin_Cache_GetLibraries().size());
                    ImGui::Spacing();
                }

                if (ImGui::Button("Sign out")) {
                    Jellyfin_SignOut();
                    SaveDesktopSettings();
                }
            } else if (Jellyfin_QuickConnectInFlight()) {
                std::string code = Jellyfin_GetQuickConnectCode();
                if (code.empty()) {
                    ImGui::Text("Requesting code...");
                } else {
                    ImGui::Text("1. Open the Jellyfin web UI on any device.");
                    ImGui::Text("2. Go to your account -> Quick Connect.");
                    ImGui::Text("3. Enter this code:");
                    ImGui::Spacing();

                    ImFont* font = ImGui::GetFont();
                    float old = font->Scale;
                    font->Scale = 3.0f;
                    ImGui::PushFont(font);
                    ImVec2 sz = ImGui::CalcTextSize(code.c_str());
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                        (avail - sz.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.85f, 1.0f, 0.85f, 1.0f),
                        "%s", code.c_str());
                    ImGui::PopFont();
                    font->Scale = old;
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Cancel")) Jellyfin_CancelQuickConnect();
            } else {
                ImGui::TextDisabled("Not signed in.");
                ImGui::Spacing();
                if (s_urlBuf[0] == 0) {
                    ImGui::TextDisabled("Set a server URL first.");
                } else if (ImGui::Button("Sign in with Quick Connect")) {
                    Jellyfin_StartQuickConnect();
                }
            }
            } // Jellyfin header

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ============================================================================
// About Window
// ============================================================================

void RenderAboutWindow() {
    if (!s_aboutOpen) return;

    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("About UIX Desktop", &s_aboutOpen)) {
        ImGui::End();
        return;
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "UIX Desktop");
    ImGui::Text("Xbox Dashboard Preview Tool");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    static char s_version[64] = "";
    if (!s_version[0]) {
        FILE* vf = fopen("Configs/version", "r");
        if (vf) {
            char line[128];
            while (fgets(line, sizeof(line), vf)) {
                if (strncmp(line, "version=", 8) == 0) {
                    char* nl = strchr(line + 8, '\n'); if (nl) *nl = 0;
                    char* cr = strchr(line + 8, '\r'); if (cr) *cr = 0;
                    strncpy(s_version, line + 8, sizeof(s_version) - 1);
                    break;
                }
            }
            fclose(vf);
        }
        if (!s_version[0]) strncpy(s_version, "unknown", sizeof(s_version));
    }

    ImGui::Text("Version: %s", s_version);
    ImGui::Text("Platform: %s",
#ifdef __APPLE__
        "macOS"
#elif defined(__linux__)
        "Linux"
#elif defined(_WIN32)
        "Windows"
#else
        "Unknown"
#endif
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Credits:");
    ImGui::BulletText("Milenko");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "UIX Lite References:");
    ImGui::BulletText("BigJx, ImOkRuOk, Odb718, Rocky5, TeamUIX");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Special Thanks:");
    ImGui::BulletText("Team-Resurgent (PrometheOS, Toolbox)");
    ImGui::BulletText("Microsoft (Original Xbox Dashboard)");
    ImGui::BulletText("Xbox-Scene community");
    ImGui::BulletText("Those who weren't named, but contributed");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Libraries:");
    ImGui::BulletText("SDL2 + SDL2_mixer (windowing, audio)");
    ImGui::BulletText("OpenGL 3.2+ (rendering)");
    ImGui::BulletText("Dear ImGui (tool UI)");
    ImGui::BulletText("stb_image / stb_image_write (textures)");
    ImGui::BulletText("libmpv / mpv (media playback)");
    ImGui::BulletText("qcow2 / FATX (Xbox HDD support)");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
        "Based on a reverse-engineered reconstruction of the");
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
        "original Xbox Dashboard, ported to SDL2 and OpenGL.");

    ImGui::End();
}

// ============================================================================
// Keyboard Shortcuts Window
// ============================================================================

void RenderShortcutsWindow() {
    if (!s_shortcutsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(360, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Keyboard Shortcuts", &s_shortcutsOpen)) {
        ImGui::Text("F3       Title Maker");
        ImGui::Text("F4       Settings");
        ImGui::Text("F5       HDD Browser");
        ImGui::Text("F6       Playlist Maker");
        ImGui::Text("F10      Hide Menu Bar");
        ImGui::Text("F11      Toggle Fullscreen");
        ImGui::Text("F12      Toggle Borderless Window");
        if (g_startupMode == 2 || g_extractedMode) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "Development Mode:");
            ImGui::Text("F1       Inspector");
            ImGui::Text("F2       XAP Editor");
        }
        ImGui::Separator();
        ImGui::Text("Ctrl+O   Open Media...");
        ImGui::Text("Ctrl+M   Toggle Mute");
        ImGui::Text("Ctrl+R   Restart Dashboard");
        ImGui::Text("Ctrl+Q   Quit");
        ImGui::Text("Escape   Deselect (Inspector)");
        ImGui::Separator();
        ImGui::Text("Gamepad / Arrow Keys navigate the dashboard.");
    }
    ImGui::End();
}


// Scan progress is now rendered inline in the Settings -> Media Library tab.
// No floating panel. Keep this stub so sdl_main.cpp's PreSwap call is a no-op.
bool s_scanPanelVisible = false;
void RenderScanProgressModal() {}

// ============================================================================
// projectM Configuration Window
// ============================================================================

void RenderProjectMConfig() {
    extern bool g_useMilkdropViz;
    static bool s_previewOn = false;
    if (!g_projectMConfigOpen) {
        if (s_previewOn) {
            MilkdropWindow_SetPreviewVisible(false);
            s_previewOn = false;
        }
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);
    if (!ImGui::Begin("Configure projectM", &g_projectMConfigOpen,
                      ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    if (!g_useMilkdropViz) {
        ImGui::TextWrapped(
            "projectM is disabled. Enable it in the Audio tab first.");
        ImGui::End();
        return;
    }

    // Session controls: lets the user spin projectM up / down without
    // hunting for the X+Y combo.
    if (MilkdropWindow_IsOpen()) {
        if (ImGui::Button("Stop session", ImVec2(120, 0)))
            MilkdropWindow_Toggle();
    } else {
        if (ImGui::Button("Start session", ImVec2(120, 0)))
            MilkdropWindow_Toggle();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(or hit X+Y)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!MilkdropWindow_IsOpen()) ImGui::BeginDisabled();

    if (ImGui::Button("< Prev", ImVec2(80, 0)))
        MilkdropWindow_PreviousPreset();
    ImGui::SameLine();
    if (ImGui::Button("Next >", ImVec2(80, 0)))
        MilkdropWindow_NextPreset();
    ImGui::SameLine();
    bool locked = MilkdropWindow_GetPresetLocked();
    if (ImGui::Checkbox("Lock", &locked))
        MilkdropWindow_SetPresetLocked(locked);

    ImGui::Spacing();
    int presetCount = MilkdropWindow_GetPresetCount();
    int curPreset   = MilkdropWindow_GetCurrentPresetIndex();
    const char* curName = (curPreset >= 0 && curPreset < presetCount)
        ? MilkdropWindow_GetPresetName(curPreset) : "";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##presetpicker", curName)) {
        // Filter
        static char s_presetFilter[64] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##presetfilter", "Filter...",
                                 s_presetFilter, sizeof(s_presetFilter));

        ImGui::BeginChild("PresetList", ImVec2(0, 240), false);
        for (int i = 0; i < presetCount; i++) {
            const char* nm = MilkdropWindow_GetPresetName(i);
            if (!nm || !*nm) continue;
            if (s_presetFilter[0]) {
                bool match = false;
                for (const char* p = nm; *p; p++) {
                    const char* a = p; const char* b = s_presetFilter;
                    while (*a && *b && (tolower(*a) == tolower(*b))) { a++; b++; }
                    if (!*b) { match = true; break; }
                }
                if (!match) continue;
            }
            bool sel = (i == curPreset);
            char label[256];
            snprintf(label, sizeof(label), "%s##p%d", nm, i);
            if (ImGui::Selectable(label, sel))
                MilkdropWindow_SetPresetIndex(i);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float kLabelX = 130.0f;
    const float kSliderW = 200.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Beat sensitivity");
    ImGui::SameLine(kLabelX);
    ImGui::SetNextItemWidth(kSliderW);
    float sens = MilkdropWindow_GetBeatSensitivity();
    if (ImGui::SliderFloat("##sens", &sens, 0.0f, 2.0f, "%.2f"))
        MilkdropWindow_SetBeatSensitivity(sens);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Preset duration");
    ImGui::SameLine(kLabelX);
    ImGui::SetNextItemWidth(kSliderW);
    float durf = (float)MilkdropWindow_GetPresetDuration();
    if (ImGui::SliderFloat("##dur", &durf, 1.0f, 60.0f, "%.0fs"))
        MilkdropWindow_SetPresetDuration((double)durf);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Checkbox("Show preview window", &s_previewOn))
        MilkdropWindow_SetPreviewVisible(s_previewOn);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Separate window showing the raw projectM render.\n"
            "Independent from the fullscreen overlay in the dashboard.");
    }

    if (!MilkdropWindow_IsOpen()) ImGui::EndDisabled();

    // ---------- Track picker for previewing without the music scene ----------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Test audio");

    int stCount = DashMusic_GetSoundtrackCount();
    static int s_stIdx = 0;
    if (s_stIdx >= stCount) s_stIdx = 0;
    const char* stName = (stCount > 0) ? DashMusic_GetSoundtrackName(s_stIdx) : "(none)";

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##soundtrack", stName ? stName : "(none)")) {
        for (int i = 0; i < stCount; i++) {
            const char* nm = DashMusic_GetSoundtrackName(i);
            bool sel = (i == s_stIdx);
            if (ImGui::Selectable(nm ? nm : "(?)", sel)) s_stIdx = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    int songCount = (stCount > 0) ? DashMusic_GetSongCount(s_stIdx) : 0;
    static int s_songIdx = 0;
    if (s_songIdx >= songCount) s_songIdx = 0;

    ImGui::BeginChild("ProjectMSongs", ImVec2(0, 140), true);
    for (int i = 0; i < songCount; i++) {
        const char* nm = DashMusic_GetSongName(s_stIdx, i);
        bool sel = (i == s_songIdx);
        char label[128];
        snprintf(label, sizeof(label), "%s##s%d", nm ? nm : "(?)", i);
        if (ImGui::Selectable(label, sel))
            s_songIdx = i;
    }
    ImGui::EndChild();

    bool playing = DashAudio_IsMusicPlaying() != 0;
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(80, 0))) {
        if (playing) {
            DashAudio_StopMusic(0);
        } else if (songCount > 0) {
            const char* path = DashMusic_GetSongPath(s_stIdx, s_songIdx);
            if (path && *path) {
                if (DashAudio_LoadMusic(path) == 0)
                    DashAudio_PlayMusic(0, 0);
            }
        }
    }

    ImGui::End();
}
