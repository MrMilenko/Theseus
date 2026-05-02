// menu_bar.cpp: desktop top-level ImGui menu bar. Drives the
// Settings, About, and Shortcuts windows. Features (Title Maker,
// HDD Browser, Media) are always accessible; dev tools (Inspector,
// XAP Editor) only appear in Development Mode. Desktop-only.

#include "std.h"
#include "dashapp.h"
#include "panel_shared.h"
#include "title_maker.h"
#include "hdd_browser.h"
#include "media_player.h"
#include "imgui.h"
#include "imfilebrowser.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// ============================================================================
// Internal State
// ============================================================================

static bool s_settingsOpen = false;
static bool s_aboutOpen = false;
static bool s_shortcutsOpen = false;

static ImGui::FileBrowser s_mediaBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
static bool s_mediaBrowserInit = false;

void ToggleSettingsWindow() { s_settingsOpen = !s_settingsOpen; }

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

    // Init media browser on first use
    if (!s_mediaBrowserInit) {
        s_mediaBrowser.SetTitle("Open Media File");
        s_mediaBrowser.SetTypeFilters({
            ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
            ".mp3", ".flac", ".wav", ".ogg", ".wma", ".aac", ".m4a",
            ".MP4", ".MKV", ".AVI", ".MOV", ".MP3", ".FLAC", ".WAV"
        });
        s_mediaBrowserInit = true;
    }

    // ---- File ----
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Media...", "Ctrl+O")) {
            s_mediaBrowser.Open();
        }
        if (MediaPlayer_GetState() != MP_IDLE) {
            if (ImGui::MenuItem("Eject / Stop Media")) {
                MediaPlayer_Stop();
                DashAudio_UnmuteAll();
                extern void DiscDrive_SetDiscType(const char*);
                DiscDrive_SetDiscType("none");
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Restart Dashboard", "Ctrl+R")) {
            g_desktopRestartRequested = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...", "F4")) {
            s_settingsOpen = true;
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

    // ---- Tools (features always visible, dev tools gated) ----
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Title Maker", "F3", g_titleMakerOpen)) {
            g_titleMakerOpen = !g_titleMakerOpen;
        }
        if (ImGui::MenuItem("HDD Browser", "F5", g_hddBrowserOpen)) {
            g_hddBrowserOpen = !g_hddBrowserOpen;
        }

        // Dev tools only in Development Mode
        if (g_startupMode == 2 || g_extractedMode) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "Development");
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
                if (!g_debugMode && g_wireframe) {
                    g_wireframe = false;
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                }
            }
            if (ImGui::MenuItem("XAP Editor", "F2", g_xapEditorOpen)) {
                g_xapEditorOpen = !g_xapEditorOpen;
                if (g_xapEditorOpen)
                    CreateXapEditorWindow();
                else
                    DestroyXapEditorWindow();
            }
        }
        ImGui::EndMenu();
    }

    // ---- View ----
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("CRT Effect", NULL, g_crt.enabled)) {
            g_crt.enabled = !g_crt.enabled;
            SaveDesktopSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Mute Audio", "Ctrl+M", g_audioMuted)) {
            g_audioMuted = !g_audioMuted;
            if (g_audioMuted) DashAudio_MuteAll();
            else DashAudio_UnmuteAll();
            g_muteOverlayTimer = 2.0f;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Wireframe", NULL, g_wireframe)) {
            g_wireframe = !g_wireframe;
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

    // Media file browser (must render every frame)
    s_mediaBrowser.Display();
    if (s_mediaBrowser.HasSelected()) {
        std::string path = s_mediaBrowser.GetSelected().string();
        s_mediaBrowser.ClearSelected();

        static bool s_mpvInited = false;
        if (!s_mpvInited)
            s_mpvInited = MediaPlayer_Init();

        if (s_mpvInited && MediaPlayer_Open(path.c_str())) {
            DashAudio_MuteAll();
            extern void DiscDrive_SetDiscType(const char*);
            DiscDrive_SetDiscType("Video");
        }
    }

    MediaPlayer_Update();
}

// ============================================================================
// Settings Window
// ============================================================================

void RenderSettingsWindow() {
    if (!s_settingsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &s_settingsOpen)) {
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

            ImGui::Spacing();
            if (ImGui::Button("Save All Settings"))
                SaveDesktopSettings();

            ImGui::EndTabItem();
        }

        // ---- Display ----
        if (ImGui::BeginTabItem("Display")) {
            ImGui::Spacing();

            ImGui::Checkbox("CRT Effect", &g_crt.enabled);
            if (g_crt.enabled) {
                ImGui::Spacing();
                ImGui::SliderFloat("Scanlines", &g_crt.scanlineIntensity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Curvature", &g_crt.curvature, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Phosphor Mask", &g_crt.phosphorMask, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Vignette", &g_crt.vignette, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Bloom", &g_crt.bloom, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Flicker", &g_crt.flickerAmount, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Color Bleed", &g_crt.colorBleed, 0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("Brightness", &g_crt.brightness, 0.8f, 1.3f, "%.2f");

                ImGui::Spacing();
                ImGui::Text("Presets:");
                ImGui::SameLine();
                for (int i = 0; i < (int)(sizeof(s_crtPresets) / sizeof(s_crtPresets[0])); i++) {
                    if (i > 0) ImGui::SameLine();
                    if (ImGui::SmallButton(s_crtPresets[i].name))
                        ApplyCRTPreset(s_crtPresets[i]);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Wireframe", &g_wireframe);

            ImGui::Text("MSAA:");
            ImGui::SameLine();
            int msaaIdx = GetMSAAIndex();
            if (ImGui::Combo("##msaa", &msaaIdx, s_msaaLabels, s_msaaCount)) {
                g_msaaSamples = s_msaaValues[msaaIdx];
                g_msaaChangeRequested = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Save Display Settings"))
                SaveDesktopSettings();

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
        FILE* vf = fopen("xboxfs/C/version", "r");
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
