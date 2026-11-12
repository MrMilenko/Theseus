// menu_bar.cpp: desktop top-level ImGui menu bar + Settings/About/Shortcuts
// windows. The Development menu only renders in Development Mode.

#include "std.h"
#include "dashapp.h"
#include "app_paths.h"

// The version stamp belongs to the build, not the user folder: Configs there is
// seeded once and never refreshed, so it would show the first version forever.
static const char* ShippedVersionPath() {
    static char path[1024];
    snprintf(path, sizeof(path), "%s/Configs/version", Plat_ShippedDir());
    return path;
}
#include "panel_shared.h"
#include "title_maker.h"
#include "hdd_browser.h"
#include "audio_sdl.h"
#include "skin_editor.h"
#include "imgui.h"
#include "plex_client.h"
#include "jellyfin_client.h"
#include "xcloud_client.h"
#include "media_player.h"
#include "milkdrop_window.h"
#include "xbox_buttons.h"
#include "stb_image.h"

extern bool  g_bWireframe;
extern float g_masterVolume;

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <cfloat>

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
// fullscreen).

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

// bgfx has no adaptive vsync, so the control is a plain On/Off. g_vsyncMode
// still stores 0/1 = on, 2 = off for config compatibility.
static const char* s_vsyncLabels[] = { "On", "Off" };

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

// Controller Menu. ImGui nav needs a focusable window to land on, and the
// menu bar isn't one: it leaves NavActive at 0 and the stick does nothing.
void RenderControllerMenu() {
    extern bool g_padModeActive;
    if (!g_padModeActive) return;

    extern bool g_playlistMakerOpen;
    struct Entry { const char* label; bool* flag; const char* desc; const char* win; };
    // Skin Editor stays out: colour pickers and direct manipulation are
    // pointer work. Mouse and keyboard only.
    static const Entry kEntries[] = {
        { "Settings",       &g_settingsOpen,      "Display, audio, media libraries and accounts",       "Settings" },
        { "Title Maker",    &g_titleMakerOpen,    "Add games and apps, import from Steam or RetroArch",  "UIX Title Maker" },
        { "HDD Browser",    &g_hddBrowserOpen,    "Open Xbox qcow2 and FATX drive images",               "Xbox HDD Browser" },
        { "Playlist Maker", &g_playlistMakerOpen, "Build and edit music playlists",                      "UIX Playlist Maker" },
    };
    static const int kCount = (int)(sizeof(kEntries) / sizeof(kEntries[0]));

    ImGuiIO& io = ImGui::GetIO();

    // Focus a freshly opened tool window a frame later, once it has actually
    // been submitted, then get out of its way.
    static const char* s_pendingFocus = NULL;
    if (s_pendingFocus) { ImGui::SetWindowFocus(s_pendingFocus); s_pendingFocus = NULL; }

    // B backs out, and has to run whether the menu is on screen or a tool
    // window has taken over, so it lives above the early out below.
    //
    // ImGui cancels popups during NewFrame, so by the time we look the popup is
    // already gone and we would close the window on the same press. Latch last
    // frame's state and give B one frame of grace. LT held means the LT+B
    // toggle is in play, and the keyboard uses B for Done, so skip both.
    {
        extern bool OSK_IsActive();
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                                      ImGuiPopupFlags_AnyPopupLevel);
        const bool itemActive = ImGui::IsAnyItemActive();
        static bool s_imguiHadB = false;

        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) &&
            !ImGui::IsKeyDown(ImGuiKey_GamepadL2) &&
            !popupOpen && !itemActive && !s_imguiHadB && !OSK_IsActive()) {
            bool closedOne = false;
            for (int i = 0; i < kCount; i++) {
                if (*kEntries[i].flag) { *kEntries[i].flag = false; closedOne = true; break; }
            }
            if (!closedOne) g_padModeActive = false;
        }
        s_imguiHadB = popupOpen || itemActive || OSK_IsActive();
    }

    for (int i = 0; i < kCount; i++) {
        if (*kEntries[i].flag) return;   // a tool is up; it owns the screen
    }

    const float scale = 1.6f;
    const float rowH  = 19.0f * scale;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const float menuW = 340.0f * scale;
    ImGui::SetNextWindowSizeConstraints(ImVec2(menuW, 0.0f), ImVec2(menuW, FLT_MAX));

    // Focus the frame it opens, otherwise NavActive stays 0 and the stick
    // does nothing, which is the bug this whole window exists to fix.
    static bool s_wasOpen = false;
    const bool justOpened = !s_wasOpen;
    if (justOpened) ImGui::SetNextWindowFocus();

    // Dashboard green, not ImGui grey.
    ImGui::PushStyleColor(ImGuiCol_WindowBg,       ImVec4(0.04f, 0.09f, 0.04f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.35f, 0.75f, 0.30f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Header,         ImVec4(0.20f, 0.55f, 0.20f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.28f, 0.70f, 0.26f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.34f, 0.85f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight,   ImVec4(0.55f, 1.00f, 0.50f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.88f, 1.00f, 0.86f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("Controller Menu", NULL, flags)) {
        ImGui::SetWindowFontScale(1.25f);

        ImGui::TextColored(ImVec4(0.55f, 1.00f, 0.50f, 1.00f), "UIX DESKTOP");
        ImGui::SameLine();
        {
            static char s_ver[64] = {0};
            static bool s_verRead = false;
            if (!s_verRead) {
                s_verRead = true;
                FILE* vf = fopen(ShippedVersionPath(), "r");
                if (vf) { if (fgets(s_ver, sizeof(s_ver), vf)) s_ver[strcspn(s_ver, "\r\n")] = 0; fclose(vf); }
            }
            const float tw = ImGui::CalcTextSize(s_ver[0] ? s_ver : "").x;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - tw - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextDisabled("%s", s_ver);
        }
        ImGui::Separator();
        ImGui::Spacing();

        int focused = -1;
        for (int i = 0; i < kCount; i++) {
            if (justOpened && i == 0) ImGui::SetKeyboardFocusHere();
            if (ImGui::Selectable(kEntries[i].label, false, 0, ImVec2(0.0f, rowH))) {
                *kEntries[i].flag = !*kEntries[i].flag;
                if (*kEntries[i].flag) s_pendingFocus = kEntries[i].win;
            }
            if (ImGui::IsItemFocused() || ImGui::IsItemHovered()) focused = i;
        }

        // Describe the highlight, so you know before you commit.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.82f, 0.60f, 1.00f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(focused >= 0 ? kEntries[focused].desc : " ");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Selectable("Resume Dashboard", false, 0, ImVec2(0.0f, rowH)))
            g_padModeActive = false;
        if (ImGui::Selectable("Exit to Desktop", false, 0, ImVec2(0.0f, rowH))) {
            SDL_Event quit;
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
        }

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(7);
    s_wasOpen = g_padModeActive;

}

// Controller hints along the bottom in pad mode. Glyphs are embedded, so the
// tool UI owes nothing to skin or data files.
void RenderControllerHints() {
    ImGuiIO& io = ImGui::GetIO();
    extern bool g_padModeActive;
    if (!g_padModeActive) return;
    if (!(io.BackendFlags & ImGuiBackendFlags_HasGamepad)) return;

    // Two glyphs per hint, so chords read as buttons rather than text.
    struct Hint {
        const unsigned char* a; unsigned int alen;
        const unsigned char* b; unsigned int blen;
        const char* label;
    };
    // Same buttons mean different things while typing, so say which.
    static const Hint kNav[] = {
        { xbtn_a_png,  xbtn_a_png_len,  NULL,       0,              "Select" },
        { xbtn_b_png,  xbtn_b_png_len,  NULL,       0,              "Back"   },
        { xbtn_y_png,  xbtn_y_png_len,  NULL,       0,              "Type"   },
        { xbtn_x_png,  xbtn_x_png_len,  NULL,       0,              "Hold: Move / Resize" },
        { xbtn_lt_png, xbtn_lt_png_len, xbtn_b_png, xbtn_b_png_len, "Exit Pad Mode" },
    };
    static const Hint kType[] = {
        { xbtn_a_png,  xbtn_a_png_len,  NULL, 0, "Type"      },
        { xbtn_x_png,  xbtn_x_png_len,  NULL, 0, "Backspace" },
        { xbtn_b_png,  xbtn_b_png_len,  NULL, 0, "Done"      },
    };
    extern bool OSK_IsActive();
    const bool typing = OSK_IsActive();
    const Hint* kHints = typing ? kType : kNav;
    const int kCount = typing ? (int)(sizeof(kType) / sizeof(kType[0]))
                              : (int)(sizeof(kNav)  / sizeof(kNav[0]));

    // Cache by source pointer: the hint set swaps, the glyphs don't.
    struct Tex { const unsigned char* key; GuiTexture* t; int w, h; };
    static Tex s_cache[12];
    static int  s_cacheN = 0;
    struct L {
        static Tex* Get(Tex* cache, int& n, const unsigned char* png, unsigned int len) {
            if (!png) return NULL;
            for (int i = 0; i < n; i++) if (cache[i].key == png) return &cache[i];
            if (n >= 12) return NULL;
            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(png, (int)len, &w, &h, &ch, 4);
            if (!px) return NULL;
            cache[n].key = png; cache[n].t = GuiTextureCreate(w, h, px);
            cache[n].w = w; cache[n].h = h;
            stbi_image_free(px);
            return &cache[n++];
        }
    };

    // One height for every glyph, scaled on aspect, or a tall trigger and a
    // round face button stagger against each other.
    const float fh      = ImGui::GetFontSize();
    const float glyphH  = fh * 1.25f;
    const float padX    = fh * 0.75f;
    const float gapIcon = fh * 0.30f;
    const float gapItem = fh * 1.25f;
    const float barH    = glyphH + fh * 0.85f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - barH));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, barH));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.08f, 0.03f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##padhints", NULL, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 org = ImGui::GetWindowPos();
        const float cy = org.y + barH * 0.5f;

        // Hairline on top so it reads as chrome, not a floating box.
        dl->AddLine(ImVec2(org.x, org.y), ImVec2(org.x + io.DisplaySize.x, org.y),
                    IM_COL32(90, 190, 85, 140), 1.0f);

        float x = org.x + padX;
        for (int i = 0; i < kCount; i++) {
            const unsigned char* srcs[2] = { kHints[i].a, kHints[i].b };
            const unsigned int   lens[2] = { kHints[i].alen, kHints[i].blen };
            for (int k = 0; k < 2; k++) {
                Tex* tp = L::Get(s_cache, s_cacheN, srcs[k], lens[k]);
                if (!tp || !tp->t) continue;
                const Tex& t = *tp;
                if (k == 1) {
                    const ImVec2 ps = ImGui::CalcTextSize("+");
                    dl->AddText(ImVec2(x, cy - ps.y * 0.5f), IM_COL32(190, 220, 190, 255), "+");
                    x += ps.x + gapIcon;
                }
                const float gw = glyphH * ((float)t.w / (float)t.h);
                dl->AddImage((ImTextureID)GuiTextureImId(t.t),
                             ImVec2(x, cy - glyphH * 0.5f),
                             ImVec2(x + gw, cy + glyphH * 0.5f));
                x += gw + gapIcon;
            }
            const ImVec2 ls = ImGui::CalcTextSize(kHints[i].label);
            dl->AddText(ImVec2(x, cy - ls.y * 0.5f), IM_COL32(225, 240, 222, 255),
                        kHints[i].label);
            x += ls.x + gapItem;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

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
    // View is for what you're looking at. Renderer settings that need a device
    // reset (MSAA, resolution) live in Settings > Display, so there's one place
    // to change them instead of two that disagree.
    if (ImGui::BeginMenu("View")) {
        extern int  g_windowMode;
        extern bool g_displayChangeRequested;
        extern bool g_menuBarAutoHide;
        extern bool g_showMenuBar;

        if (ImGui::MenuItem("Fullscreen", "F11", g_windowMode == 2)) {
            g_windowMode = (g_windowMode == 2) ? 0 : 2;
            g_displayChangeRequested = true;
            SaveDesktopSettings();
        }
        if (ImGui::MenuItem("Borderless Window", "F12")) {
            Uint32 wf = SDL_GetWindowFlags(g_pSDLWindow);
            SDL_SetWindowBordered(g_pSDLWindow,
                                  (wf & SDL_WINDOW_BORDERLESS) ? SDL_TRUE : SDL_FALSE);
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Menu Bar")) {
            if (ImGui::MenuItem("Auto-hide", NULL, g_menuBarAutoHide)) {
                g_menuBarAutoHide = !g_menuBarAutoHide;
                SaveDesktopSettings();
            }
            if (ImGui::MenuItem("Hide", "F10")) {
                g_showMenuBar = false;
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("CRT Effect", NULL, g_crt.enabled)) {
            g_crt.enabled = !g_crt.enabled;
            SaveDesktopSettings();
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
                }
            }
            if (ImGui::MenuItem("Wireframe", NULL, g_bWireframe)) {
                g_bWireframe = !g_bWireframe;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("XAP Editor", "F2", g_xapEditorOpen)) {
                g_xapEditorOpen = !g_xapEditorOpen;
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
            {
                extern bool g_menuBarAutoHide;
                if (ImGui::Checkbox("Auto-hide menu bar", &g_menuBarAutoHide))
                    SaveDesktopSettings();
                ImGui::SameLine();
                ImGui::TextDisabled("(reveals when the pointer reaches the top edge)");
            }

            ImGui::Spacing();
            ImGui::Text("User Directory:");
            ImGui::TextDisabled("%s", Plat_UserDataDir());
            if (ImGui::Button("Open User Directory"))
                Plat_OpenInFileManager(Plat_UserDataDir());
            ImGui::SameLine();
            ImGui::TextDisabled("(skins, orbs, music, saves and settings)");

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
            int vsyncIdx = (g_vsyncMode == 2) ? 1 : 0;
            if (ImGui::Combo("##vsync", &vsyncIdx, s_vsyncLabels, 2)) {
                g_vsyncMode = (vsyncIdx == 1) ? 2 : 0;
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

            if (ImGui::Checkbox("Emulate Xbox Resolution", &g_bEmulateXboxRes)) {
                TheseusSetProjectionDirty();
                SaveDesktopSettings();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(720x480 design space, keeps XIP scenes framed as authored)");
            ImGui::Spacing();

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

            if (ImGui::CollapsingHeader("Xbox / xCloud")) {
            ImGui::Spacing();

            if (Xcloud_HasToken()) {
                Xcloud_StartSession();

                if (!Xcloud_SessionReady()) {
                    ImGui::Text("%s", Xcloud_SessionPhase().c_str());
                } else {
                    std::string gt = Xcloud_GetGamertag();
                    ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
                        "Signed in%s%s.", gt.empty() ? "" : " as ", gt.c_str());
                    ImGui::Spacing();

                    Xcloud_FetchConsoles();
                    ImGui::TextDisabled("Your consoles");
                    ImGui::Separator();
                    if (!Xcloud_ConsolesReady()) {
                        ImGui::TextDisabled("Loading...");
                    } else {
                        std::vector<XcloudConsole> cs = Xcloud_GetConsoles();
                        if (cs.empty()) ImGui::TextDisabled("None found.");
                        for (size_t i = 0; i < cs.size(); i++) {
                            ImGui::PushID((int)i);
                            ImGui::Text("%s  (%s)", cs[i].name.c_str(), cs[i].powerState.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Stream")) {
                                extern void Xcloud_LaunchStream(const char* type, const char* id);
                                Xcloud_LaunchStream("home", cs[i].serverId.c_str());
                                g_settingsOpen = false;
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::Spacing();

                    Xcloud_FetchGames();
                    ImGui::TextDisabled("Your xCloud games");
                    ImGui::SameLine();
                    static bool s_showAllGames = false;
                    ImGui::Checkbox("Show all", &s_showAllGames);
                    ImGui::Separator();
                    if (!Xcloud_GamesReady()) {
                        ImGui::TextDisabled("Loading...");
                    } else {
                        std::vector<XcloudGame> gs = Xcloud_GetGames();
                        int shown = 0;
                        ImGui::BeginChild("xcgames", ImVec2(0, 180), true);
                        for (size_t i = 0; i < gs.size(); i++) {
                            if (!s_showAllGames && !gs[i].playable) continue;   // owned/GP/free only
                            shown++;
                            ImGui::PushID((int)(i + 10000));
                            if (ImGui::Selectable(gs[i].name.c_str())) {
                                extern void Xcloud_LaunchStream(const char* type, const char* id);
                                Xcloud_LaunchStream("cloud", gs[i].titleId.c_str());
                                g_settingsOpen = false;
                            }
                            ImGui::PopID();
                        }
                        if (shown == 0)
                            ImGui::TextDisabled(gs.empty() ? "None found."
                                : "Nothing playable. Tick \"Show all\" for the full catalog.");
                        ImGui::EndChild();
                    }

                    // Session progress. This is the signaling half; the
                    // WebRTC transport attaches once the session is up.
                    if (Xcloud_StreamRunning() || Xcloud_StreamReady()) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Stream: %s", Xcloud_StreamPhase().c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Stop##xcstream")) Xcloud_StopStreamSession();
                    }
                }
                ImGui::Spacing();
                if (ImGui::Button("Sign out##xc")) {
                    Xcloud_SignOut();
                    SaveDesktopSettings();
                }
            } else if (Xcloud_LoginInFlight()) {
                std::string code = Xcloud_GetUserCode();
                std::string uri  = Xcloud_GetVerificationUri();
                if (code.empty()) {
                    ImGui::Text("%s", Xcloud_GetLoginStatus().c_str());
                } else {
                    ImGui::Text("1. Open");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", uri.c_str());
                    ImGui::SameLine();
                    ImGui::Text("on any device.");
                    ImGui::Text("2. Enter this code:");
                    ImGui::Spacing();

                    ImFont* font = ImGui::GetFont();
                    float old = font->Scale;
                    font->Scale = 3.0f;
                    ImGui::PushFont(font);
                    ImVec2 sz = ImGui::CalcTextSize(code.c_str());
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - sz.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.85f, 1.0f, 0.85f, 1.0f), "%s", code.c_str());
                    ImGui::PopFont();
                    font->Scale = old;

                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", Xcloud_GetLoginStatus().c_str());
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Cancel##xc")) Xcloud_CancelLogin();
            } else {
                ImGui::TextDisabled("Not signed in.");
                ImGui::Spacing();
                if (ImGui::Button("Sign in to Xbox")) Xcloud_StartLogin();
            }
            } // Xbox / xCloud header

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
        FILE* vf = fopen(ShippedVersionPath(), "r");
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
    ImGui::Text("Platform: %s", Host_Name());

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
        ImGui::Text("LT+B     Overlay / Menu Bar (gamepad)"); ImGui::Text("Ctrl+M   Toggle Mute");
        ImGui::Text("Ctrl+R   Restart Dashboard");
        ImGui::Text("Ctrl+Q   Quit");
        ImGui::Text("Escape   Deselect (Inspector)");
        ImGui::Separator();
        ImGui::Text("Gamepad / Arrow Keys navigate the dashboard.");
    }
    ImGui::End();
}


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
