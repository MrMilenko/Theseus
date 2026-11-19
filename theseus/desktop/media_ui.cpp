// media_ui.cpp: fullscreen video render + Xbox-themed OSD.
//
// Two paint paths: blit into the CRT pass when the post-process is on,
// ImGui::Image when it's off. s_videoBlittedToFBO is the handshake.

#include "display.h"
#include "std.h"
#include "dashapp.h"
#include "media_player.h"
#include "audio_sdl.h"
#include "imgui.h"
#include "playlist.h"
#include "guide.h"
#include "pads.h"
#include "ui_overlay.h"
#include "d3d8_sdl.h"  // for g_bgfxProgBlit / g_bgfxSamplerBlit on bgfx path

#include <string>
#include <vector>
#include <SDL.h>

#include <cstdio>
#include <cmath>

extern bool g_mediaFullscreen;
extern char g_mediaFullscreenTitle[256];
extern char g_mediaFullscreenSubtitle[256];
extern SDL_Window* g_pSDLWindow;

// CRT path blits into the bound FBO and sets this; non-CRT path leaves
// it false and RenderOSD draws the video as an ImGui::Image instead.
static bool s_videoBlittedToFBO = false;


// ============================================================================
// Auto-hide OSD chrome after no-input idle.
// MediaUI_NoteActivity() bumped from sdl_main.cpp's mouse / key events.
// ============================================================================

static const double OSD_VISIBLE_SECONDS = 2.5;   // fully shown for this long
static const double OSD_FADE_SECONDS    = 0.5;   // fade-out over this much

static double s_lastActivity = -1000.0; // far in past, hidden by default once entered
static bool   s_cursorVisible = true;
static bool   s_trackMenuOpen = false;

static double NowSeconds() { return (double)SDL_GetTicks() / 1000.0; }

void MediaUI_NoteActivity()
{
    s_lastActivity = NowSeconds();
}

// The hints name pad buttons while the pad is what's being used.
static bool s_hintPad = false;

void MediaUI_NoteKey()
{
    s_hintPad = false;
    MediaUI_NoteActivity();
}

void MediaUI_ToggleTrackMenu()
{
    s_trackMenuOpen = !s_trackMenuOpen;
    s_lastActivity = NowSeconds(); // keep OSD up while menu is around
}

// ============================================================================
// Server playback reports. Position is kept every frame because it's gone
// by the time the player has stopped.
// ============================================================================

static MediaReportFn s_report = NULL;
static bool   s_reportStarted = false;
static bool   s_reportPaused = false;
static double s_reportLast = 0.0;
static double s_reportPos = 0.0, s_reportDur = 0.0;

static void EndReport()
{
    if (s_report && s_reportStarted)
        s_report(MEDIA_REPORT_STOP, s_reportPos, s_reportDur, false);
    s_report = NULL;
    s_reportStarted = false;
}

void MediaUI_SetReporter(MediaReportFn fn)
{
    EndReport();
    s_report = fn;
    s_reportPos = s_reportDur = 0.0;
}

static void ReportFrame()
{
    if (!s_report) return;
    MediaPlayerState st = MediaPlayer_GetState();
    if (st != MP_PLAYING && st != MP_PAUSED) return;
    double dur = MediaPlayer_GetDuration();
    if (dur <= 0.0) return;
    s_reportPos = MediaPlayer_GetPosition();
    s_reportDur = dur;
    bool paused = st == MP_PAUSED;
    double now = NowSeconds();
    if (!s_reportStarted) {
        s_report(MEDIA_REPORT_START, s_reportPos, dur, paused);
        s_reportStarted = true;
    } else if (paused != s_reportPaused || now - s_reportLast >= 10.0) {
        s_report(MEDIA_REPORT_PROGRESS, s_reportPos, dur, paused);
    } else {
        return;
    }
    s_reportPaused = paused;
    s_reportLast = now;
}

// ============================================================================
// Playback queue. Snapshot of a playlist captured at PlayPlaylist time so
// edits to the source playlist while playing don't change what's queued.
// ============================================================================

struct QueueItem { std::string path, title; };
static std::vector<QueueItem> s_queue;
static std::string            s_queuePlaylistName;
static int                    s_queueIdx = -1;

static void OpenCurrentQueueItem() {
    if (s_queueIdx < 0 || s_queueIdx >= (int)s_queue.size()) return;
    const QueueItem& it = s_queue[s_queueIdx];
    MediaUI_SetReporter(NULL);
    if (!MediaPlayer_Open(it.path.c_str())) return;

    g_mediaFullscreen = true;
    extern void ApplyEffectiveMute_Public();
    ApplyEffectiveMute_Public();
    strncpy(g_mediaFullscreenTitle, it.title.c_str(), sizeof(g_mediaFullscreenTitle) - 1);
    g_mediaFullscreenTitle[sizeof(g_mediaFullscreenTitle) - 1] = 0;
    char sub[64];
    snprintf(sub, sizeof(sub), "%s  (%d / %d)",
        s_queuePlaylistName.c_str(), s_queueIdx + 1, (int)s_queue.size());
    strncpy(g_mediaFullscreenSubtitle, sub, sizeof(g_mediaFullscreenSubtitle) - 1);
    g_mediaFullscreenSubtitle[sizeof(g_mediaFullscreenSubtitle) - 1] = 0;
}

void MediaUI_PlayPlaylist(const char* playlistName, int startIdx) {
    const Playlist* p = Playlist_Find(playlistName);
    if (!p || p->items.empty()) return;
    s_queue.clear();
    for (size_t i = 0; i < p->items.size(); i++)
        s_queue.push_back({p->items[i].path, p->items[i].title});
    s_queuePlaylistName = playlistName;
    s_queueIdx = (startIdx < 0 || startIdx >= (int)s_queue.size()) ? 0 : startIdx;
    OpenCurrentQueueItem();
}

void MediaUI_PlaylistNext() {
    if (s_queueIdx + 1 >= (int)s_queue.size()) return;
    s_queueIdx++;
    OpenCurrentQueueItem();
}

void MediaUI_PlaylistPrev() {
    if (s_queueIdx <= 0) return;
    s_queueIdx--;
    OpenCurrentQueueItem();
}

bool MediaUI_PlaylistActive() { return s_queueIdx >= 0 && !s_queue.empty(); }

// The scene is frozen while video is up, so the pad is read here: B or Back
// stops, A or Start pauses, left and right seek, LB and RB step a playlist.
void MediaUI_PadFrame()
{
    static unsigned s_prev = ~0u;
    if (!g_mediaFullscreen) { s_prev = ~0u; return; }
    // Show the title for a moment as playback starts.
    if (s_prev == ~0u) {
        MediaUI_NoteActivity();
        s_hintPad = Joy_LastDevice() == 1;
    }
    unsigned pad = Joy_PadButtons();
    // The A that started playback is usually still held on the first frame.
    unsigned pressed = (s_prev == ~0u) ? 0 : (pad & ~s_prev);
    s_prev = pad;
    if (!pressed) return;
    MediaUI_NoteActivity();
    s_hintPad = true;
    extern void MediaUI_StopFullscreen();
    if (pressed & (GUIDE_B | GUIDE_BACK)) { MediaUI_StopFullscreen(); return; }
    if (pressed & (GUIDE_A | GUIDE_START)) MediaPlayer_TogglePause();
    if (pressed & GUIDE_LEFT)  MediaPlayer_SeekRelative(-10.0);
    if (pressed & GUIDE_RIGHT) MediaPlayer_SeekRelative(10.0);
    if (pressed & GUIDE_LB) MediaUI_PlaylistPrev();
    if (pressed & GUIDE_RB) MediaUI_PlaylistNext();
}

// 0.0 = hidden, 1.0 = fully visible. Smoothly fades.
static float OsdAlpha()
{
    if (!g_mediaFullscreen) return 1.0f;
    double dt = NowSeconds() - s_lastActivity;
    if (dt < OSD_VISIBLE_SECONDS) return 1.0f;
    if (dt > OSD_VISIBLE_SECONDS + OSD_FADE_SECONDS) return 0.0f;
    float t = (float)((dt - OSD_VISIBLE_SECONDS) / OSD_FADE_SECONDS);
    // smoothstep
    t = t * t * (3.0f - 2.0f * t);
    return 1.0f - t;
}

bool MediaUI_OsdVisible()
{
    return OsdAlpha() > 0.0f;
}


// ============================================================================
// Fullscreen video framebuffer clear (called in place of Draw())
// ============================================================================

void MediaUI_DrawFullscreenVideo()
{
    // Pick destination size based on the actually-bound framebuffer.
    int ww = 1280, wh = 720;
    int boundFBO = 0; // unused under BGFX
    if (g_pSDLWindow) SDL_GetWindowSize(g_pSDLWindow, &ww, &wh);
    // bgfx view 0 owns presentation; MediaPlayer_RenderToScreen
    // submits the fullscreen quad into view 0 directly. The clear is
    // configured on view 0 by the dashboard's Clear() shim.

    // Blit the video frame into whatever framebuffer is currently bound
    // (the CRT capture FBO when CRT is on, or the backbuffer otherwise).
    // Drawing inside the CRT capture means the CRT post-process applies to
    // the video, not just to the dashboard. Letterbox-fit to viewport size.
    s_videoBlittedToFBO = false;

    // Submit the mpv frame as an aspect-fit quad on view 0. View 0 is
    // pointed at the CRT capture FBO (when CRT is on) or the backbuffer
    // (when off) by sdl_main.cpp's caller, so this lands exactly where
    // the CRT pass on view 1 will pick it up. View 0's clear paints the
    // letterbox region; without it the previous frame's pixels show
    // through outside the aspect-fit rect.
    (void)boundFBO;

    int dw = (ww > 0) ? ww : 1;
    int dh = (wh > 0) ? wh : 1;

    // Viewport in pixels. ww/wh above are points, right for the ImGui
    // chrome but a quarter of the surface on a HiDPI display.
    int pw = 0, ph = 0;
    Plat_GetDrawableSize(&pw, &ph);
    if (pw <= 0 || ph <= 0) { pw = dw; ph = dh; }
    bgfx::setViewRect(0, 0, 0, (uint16_t)pw, (uint16_t)ph);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    // Black until the first frame decodes, not the menu frozen underneath.
    bgfx::touch(0);

    int vw = 0, vh = 0;
    unsigned int texIdx = MediaPlayer_GetVideoTexture(&vw, &vh);
    if (!bgfx::isValid(g_bgfxProgBlit) || texIdx == 0 || vw <= 0 || vh <= 0) return;
    bgfx::TextureHandle vidTex; vidTex.idx = (uint16_t)texIdx;
    if (!bgfx::isValid(vidTex)) return;

    float qw, qh;
    Bgfx_AspectFitNDC(vw, vh, dw, dh, qw, qh);
    Bgfx_BlitTexturedQuad(vidTex, qw, qh);
    s_videoBlittedToFBO = true;
}



// ============================================================================
// OSD: video frame + chrome (auto-hide, fade)
// ============================================================================

static void FormatTime(double seconds, char* buf, int sz)
{
    if (seconds < 0.0 || seconds != seconds) seconds = 0.0;
    int total = (int)seconds;
    int h = total / 3600;
    int m = (total / 60) % 60;
    int s = total % 60;
    if (h > 0) snprintf(buf, sz, "%d:%02d:%02d", h, m, s);
    else       snprintf(buf, sz, "%d:%02d", m, s);
}

// The transport hints, in the guide's button style: pad buttons or key caps,
// whichever was used last. Drawn on the guide's overlay, which works in a
// 640x480 layout scaled to the window's height.
struct OsdHint { int button[2]; const char* key[2]; const char* word; };

static float HintGlyph(const OsdHint& h, int i, float size, float x, float y, unsigned int color, bool measure)
{
    if (!s_hintPad)
        return measure ? ui_overlay_key_width(h.key[i], size) : ui_overlay_key(h.key[i], size, x, y, color);
    return measure ? ui_overlay_button_width(h.button[i], size) : ui_overlay_button(h.button[i], size, x, y, color);
}

static float DrawHint(const OsdHint& h, float size, float x, float y, unsigned int glyph, unsigned int text, bool measure)
{
    float w = 0.0f;
    for (int i = 0; i < 2; i++) {
        if (i == 1 && (s_hintPad ? h.button[1] < 0 : !h.key[1])) break;
        if (i == 1) w += 2.0f;
        w += HintGlyph(h, i, size, x + w, y, glyph, measure);
    }
    w += 4.0f;
    float ts = size * 0.72f;
    w += measure ? ui_overlay_text_width(UI_FONT_BOLD, ts, h.word)
                 : ui_overlay_text(UI_FONT_BOLD, ts, x + w, y + size * 0.14f, UI_ALIGN_LEFT, text, h.word);
    return w;
}

static void DrawHints(float alpha, float right, float y)
{
    static const OsdHint kPad[] = {
        { { UI_BUTTON_B, -1 }, { "", NULL }, "Stop" },
        { { UI_BUTTON_A, -1 }, { "", NULL }, "Pause" },
        { { UI_BUTTON_DPAD_LEFT, UI_BUTTON_DPAD_RIGHT }, { "", NULL }, "Seek" },
        { { UI_BUTTON_LEFT_SHOULDER, UI_BUTTON_RIGHT_SHOULDER }, { "", NULL }, "Prev/Next" },
    };
    static const OsdHint kKeys[] = {
        { { -1, -1 }, { "ESC", NULL }, "Stop" },
        { { -1, -1 }, { "SPACE", NULL }, "Pause" },
        { { -1, -1 }, { "\xE2\x80\xB9", "\xE2\x80\xBA" }, "Seek" },
        { { -1, -1 }, { "T", NULL }, "Tracks" },
        { { -1, -1 }, { "[", "]" }, "Prev/Next" },
    };
    const OsdHint* hints = s_hintPad ? kPad : kKeys;
    int n = s_hintPad ? 3 : 4;
    if (MediaUI_PlaylistActive()) n++;

    ui_overlay_set_device(s_hintPad ? 1 : 0);
    ui_overlay_set_pad_style(Pads_Style());
    ui_overlay_set_pad_swap(g_padSwapConfirm);
    ui_overlay_layer(0);
    unsigned int a = (unsigned int)(alpha * 255.0f) & 0xFF;
    unsigned int glyph = 0xFFFFFF00 | a;
    unsigned int text = 0xD9FFD900 | (unsigned int)(alpha * 230.0f);
    const float size = 15.0f, gap = 14.0f;
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += DrawHint(hints[i], size, 0, 0, glyph, text, true) + (i ? gap : 0.0f);
    float x = right - total;
    for (int i = 0; i < n; i++) x += DrawHint(hints[i], size, x, y, glyph, text, false) + gap;
}

// Pack RGBA float -> ImU32 with alpha multiplier.
static ImU32 RGBA(float r, float g, float b, float a)
{
    if (a < 0) a = 0; if (a > 1) a = 1;
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}


void MediaUI_RenderOSD()
{
    if (!g_mediaFullscreen) return;

    // Use ImGui's display size (logical units) so chrome positions match
    // the UI canvas on Retina/HiDPI, where the pixel size is 2x and would
    // push the overlay off-screen.
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    int ww = (int)disp.x;
    int wh = (int)disp.y;

    // Auto-end: when libmpv reports stopped/idle (track finished), advance the
    // playlist queue if active, otherwise bail back to dashboard.
    ReportFrame();
    MediaPlayerState st = MediaPlayer_GetState();
    if (st == MP_IDLE || st == MP_STOPPED) {
        if (MediaUI_PlaylistActive() && s_queueIdx + 1 < (int)s_queue.size()) {
            s_queueIdx++;
            OpenCurrentQueueItem();
            return;
        }
        extern void MediaUI_StopFullscreen();
        MediaUI_StopFullscreen();
        return;
    }

    // CRT path already painted the video; only the non-CRT path needs
    // to draw it here, layered below the OSD chrome.
    if (!s_videoBlittedToFBO) {
        int vw = 0, vh = 0;
        unsigned int tex = MediaPlayer_GetVideoTexture(&vw, &vh);
        if (tex && vw > 0 && vh > 0) {
            float vAspect = (float)vw / (float)vh;
            float wAspect = (float)ww / (float)wh;
            float dstW, dstH;
            if (vAspect > wAspect) { dstW = (float)ww; dstH = (float)ww / vAspect; }
            else                   { dstH = (float)wh; dstW = (float)wh * vAspect; }
            float dstX = ((float)ww - dstW) * 0.5f;
            float dstY = ((float)wh - dstH) * 0.5f;
            // Background draw list so the OSD chrome (foreground list) layers above.
            // UV orientation: GL renders the video into an FBO whose row 0 is
            // visual bottom (GL framebuffer convention), so we sample with V
            // flipped to put visual-top at the top of the drawn rect. bgfx's
            // SW path writes the texture top-down (row 0 = visual top), so
            // no flip needed.
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            bg->AddImage((ImTextureID)(intptr_t)tex,
                ImVec2(dstX, dstY), ImVec2(dstX + dstW, dstY + dstH),
                ImVec2(0, 0), ImVec2(1, 1));
        }
    }

    float alpha = OsdAlpha();

    // Sync OS cursor visibility with chrome alpha.
    bool wantCursor = (alpha > 0.0f);
    if (wantCursor != s_cursorVisible) {
        SDL_ShowCursor(wantCursor ? SDL_ENABLE : SDL_DISABLE);
        s_cursorVisible = wantCursor;
    }

    if (alpha <= 0.001f) return;

    // Top gradient, Xbox green tint, fades to transparent.
    {
        const float h = 96.0f;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImU32 top    = RGBA(0.02f, 0.10f, 0.04f, 0.85f * alpha);
        ImU32 bottom = RGBA(0.02f, 0.10f, 0.04f, 0.00f);
        dl->AddRectFilledMultiColor(
            ImVec2(0, 0), ImVec2((float)ww, h),
            top, top, bottom, bottom);

        // Title text
        ImU32 titleCol = RGBA(0.85f, 1.00f, 0.85f, alpha);
        ImU32 subCol   = RGBA(0.55f, 0.90f, 0.55f, alpha * 0.85f);

        ImFont* font = ImGui::GetFont();
        float titleSize = 26.0f;
        float subSize   = 14.0f;

        if (g_mediaFullscreenTitle[0]) {
            dl->AddText(font, titleSize, ImVec2(28, 18), titleCol, g_mediaFullscreenTitle);
        }
        if (g_mediaFullscreenSubtitle[0]) {
            dl->AddText(font, subSize, ImVec2(28, 52), subCol, g_mediaFullscreenSubtitle);
        }
    }

    // Bottom gradient, fades up from black; transport sits on it.
    {
        const float h = 110.0f;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImU32 top    = RGBA(0.02f, 0.10f, 0.04f, 0.00f);
        ImU32 bottom = RGBA(0.02f, 0.10f, 0.04f, 0.85f * alpha);
        float y0 = (float)wh - h;
        dl->AddRectFilledMultiColor(
            ImVec2(0, y0), ImVec2((float)ww, (float)wh),
            top, top, bottom, bottom);

        double pos  = MediaPlayer_GetPosition();
        double dur  = MediaPlayer_GetDuration();
        float frac  = (dur > 0.0) ? (float)(pos / dur) : 0.0f;
        char tPos[24], tDur[24];
        FormatTime(pos, tPos, sizeof(tPos));
        FormatTime(dur, tDur, sizeof(tDur));

        ImU32 textCol = RGBA(0.85f, 1.00f, 0.85f, alpha);

        ImFont* font = ImGui::GetFont();
        float timeSize = 16.0f;

        // Time on left
        char timeStr[64];
        snprintf(timeStr, sizeof(timeStr), "%s / %s", tPos, tDur);
        dl->AddText(font, timeSize, ImVec2(28, y0 + 28), textCol, timeStr);

        // Hints on the right, in layout units: the window is 480 tall there.
        float toLayout = 480.0f / (float)(wh > 0 ? wh : 1);
        DrawHints(alpha, ui_overlay_layout_right() - 28.0f * toLayout, (y0 + 24.0f) * toLayout);

        // Progress bar, Xbox green track on dim bg.
        float barX = 28.0f;
        float barW = (float)ww - 56.0f;
        float barY = y0 + 70.0f;
        float barH = 6.0f;

        ImU32 trackBg   = RGBA(1.00f, 1.00f, 1.00f, 0.10f * alpha);
        ImU32 trackFg   = RGBA(0.40f, 1.00f, 0.30f, 0.95f * alpha);
        ImU32 knobCol   = RGBA(0.70f, 1.00f, 0.55f, 1.00f * alpha);
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), trackBg, 3.0f);
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * frac, barY + barH), trackFg, 3.0f);
        // Glow knob at the playhead
        float kx = barX + barW * frac;
        float ky = barY + barH * 0.5f;
        dl->AddCircleFilled(ImVec2(kx, ky), 6.0f, knobCol, 16);
    }

    // Track picker (T key). Audio + subtitles, click to select.
    if (s_trackMenuOpen) {
        s_lastActivity = NowSeconds();
        MediaTrack tracks[MEDIA_TRACK_MAX];
        int n = MediaPlayer_GetTracks(tracks, MEDIA_TRACK_MAX);

        ImGui::SetNextWindowPos(ImVec2((float)ww - 380.0f, 110.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("Tracks", &s_trackMenuOpen, fl)) {
            ImGui::TextDisabled("Audio");
            ImGui::Separator();
            bool anyAudio = false;
            for (int i = 0; i < n; i++) {
                if (tracks[i].type != 0) continue;
                anyAudio = true;
                char label[256];
                snprintf(label, sizeof(label), "%s%-4s %s##a%d",
                    tracks[i].selected ? "* " : "  ",
                    tracks[i].lang[0] ? tracks[i].lang : "---",
                    tracks[i].title[0] ? tracks[i].title : "(untitled)",
                    tracks[i].id);
                if (ImGui::Selectable(label, tracks[i].selected))
                    MediaPlayer_SetAudioTrack(tracks[i].id);
            }
            if (!anyAudio) ImGui::TextDisabled("  (none)");

            ImGui::Spacing();
            ImGui::TextDisabled("Subtitles");
            ImGui::Separator();
            bool subOff = true;
            for (int i = 0; i < n; i++) if (tracks[i].type == 1 && tracks[i].selected) subOff = false;
            if (ImGui::Selectable(subOff ? "* Off" : "  Off"))
                MediaPlayer_SetSubtitleTrack(0);
            for (int i = 0; i < n; i++) {
                if (tracks[i].type != 1) continue;
                char label[256];
                snprintf(label, sizeof(label), "%s%-4s %s%s##s%d",
                    tracks[i].selected ? "* " : "  ",
                    tracks[i].lang[0] ? tracks[i].lang : "---",
                    tracks[i].title[0] ? tracks[i].title : "(untitled)",
                    tracks[i].external ? " (external)" : "",
                    tracks[i].id);
                if (ImGui::Selectable(label, tracks[i].selected))
                    MediaPlayer_SetSubtitleTrack(tracks[i].id);
            }
        }
        ImGui::End();
    }
}


// ============================================================================
// Stop fullscreen video, return to dashboard.
// ============================================================================

void MediaUI_StopFullscreen()
{
    if (!g_mediaFullscreen) return;
    EndReport();
    MediaPlayer_Stop();
    g_mediaFullscreen = false;
    s_queue.clear();
    s_queuePlaylistName.clear();
    s_queueIdx = -1;
    // libmpv's render context just trashed our GL state without going
    // through the shim. Invalidate the cache so the dashboard's first frame
    // back actually re-applies blend/depth/cull instead of trusting stale
    // cache values (otherwise the cellwall renders solid green). Two
    // layers to invalidate: the shim's own m_* cache, AND the
    // theseus.h wrapper-level cache that short-circuits before the
    // shim. Missing either leaves the dashboard's next "set" silently
    // skipped.
    if (g_pD3DDev) g_pD3DDev->InvalidateStateCache();
    TheseusInvalidateWrapperCaches();
    extern void ApplyEffectiveMute_Public();
    ApplyEffectiveMute_Public();
    g_mediaFullscreenTitle[0] = 0;
    g_mediaFullscreenSubtitle[0] = 0;
    // Pulse the XAP wrapper so the action menu re-runs ShowMediaActionMenu().
    // Without this, scene-local state (action highlight, drilled-into seasons,
    // etc.) survives the playback round-trip and a second Play press fails.
    extern int g_mediaPlaybackExited;
    g_mediaPlaybackExited = 1;
    if (!s_cursorVisible) {
        SDL_ShowCursor(SDL_ENABLE);
        s_cursorVisible = true;
    }
    // Whatever closed the video (Esc, B) is still held as the menu wakes up.
    Joy_Rebaseline();
}
