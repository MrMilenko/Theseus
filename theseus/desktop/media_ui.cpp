// media_ui.cpp: fullscreen video render + Xbox-themed OSD.
//
// Two paint paths: CRT FBO blit when the post-process is on, ImGui::Image
// when it's off (direct FBO0 blits don't display on Apple Silicon GL).
// s_videoBlittedToFBO is the handshake.

#include "std.h"
#include "dashapp.h"
#include "media_player.h"
#include "audio_sdl.h"
#include "imgui.h"
#include "playlist.h"
#include "d3d8_sdl.h"  // for g_bgfxProgBlit / g_bgfxSamplerBlit on bgfx path

#include <string>
#include <vector>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifdef THESEUS_USE_BGFX
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/glew.h>
#endif
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
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

void MediaUI_ToggleTrackMenu()
{
    s_trackMenuOpen = !s_trackMenuOpen;
    s_lastActivity = NowSeconds(); // keep OSD up while menu is around
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
#ifndef THESEUS_USE_BGFX
    GLint boundFBO = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &boundFBO);
    if (boundFBO != 0) {
        GLint texName = 0;
        glGetFramebufferAttachmentParameteriv(
            GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texName);
        if (texName) {
            GLint prevTex = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
            glBindTexture(GL_TEXTURE_2D, (GLuint)texName);
            GLint w = 0, h = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
            if (w > 0 && h > 0) { ww = w; wh = h; }
        }
    } else if (g_pSDLWindow) {
        SDL_GetWindowSize(g_pSDLWindow, &ww, &wh);
    }

    glViewport(0, 0, ww, wh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#else
    int boundFBO = 0; // unused under BGFX
    if (g_pSDLWindow) SDL_GetWindowSize(g_pSDLWindow, &ww, &wh);
    // bgfx view 0 owns presentation; MediaPlayer_RenderToScreen
    // submits the fullscreen quad into view 0 directly. The clear is
    // configured on view 0 by the dashboard's Clear() shim.
#endif

    // Blit the video frame into whatever framebuffer is currently bound
    // (the CRT capture FBO when CRT is on, or the backbuffer otherwise).
    // Drawing inside the CRT capture means the CRT post-process applies to
    // the video, not just to the dashboard. Letterbox-fit to viewport size.
    s_videoBlittedToFBO = false;

#ifndef THESEUS_USE_BGFX
    // CRT path only. With no FBO bound, RenderOSD does the picture as
    // an ImGui::Image instead (FBO0 blits aren't reliable cross-platform).
    if (boundFBO == 0) return;

    int vw = 0, vh = 0;
    unsigned int tex = MediaPlayer_GetVideoTexture(&vw, &vh);
    if (tex && vw > 0 && vh > 0) {
        float vAspect = (float)vw / (float)vh;
        float wAspect = (float)ww / (float)wh;
        float dstW, dstH;
        if (vAspect > wAspect) { dstW = (float)ww; dstH = (float)ww / vAspect; }
        else                   { dstH = (float)wh; dstW = (float)wh * vAspect; }
        int dstX = (int)(((float)ww - dstW) * 0.5f);
        int dstY = (int)(((float)wh - dstH) * 0.5f);

        unsigned int srcFBO = MediaPlayer_GetFBO();
        if (srcFBO) {
            GLint prevDrawFBO = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
            GLint prevReadFBO = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
            glBlitFramebuffer(
                0, 0, vw, vh,
                dstX, dstY, dstX + (int)dstW, dstY + (int)dstH,
                GL_COLOR_BUFFER_BIT, GL_LINEAR);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevReadFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFBO);
            s_videoBlittedToFBO = true;
        }
    }
#else
    // Submit the mpv frame as an aspect-fit quad on view 0. View 0 is
    // pointed at the CRT capture FBO (when CRT is on) or the backbuffer
    // (when off) by sdl_main.cpp's caller, so this lands exactly where
    // the CRT pass on view 1 will pick it up. View 0's clear paints the
    // letterbox region; without it the previous frame's pixels show
    // through outside the aspect-fit rect.
    (void)boundFBO;

    int vw = 0, vh = 0;
    unsigned int texIdx = MediaPlayer_GetVideoTexture(&vw, &vh);
    if (!bgfx::isValid(g_bgfxProgBlit) || texIdx == 0 || vw <= 0 || vh <= 0) return;
    bgfx::TextureHandle vidTex; vidTex.idx = (uint16_t)texIdx;
    if (!bgfx::isValid(vidTex)) return;

    int dw = (ww > 0) ? ww : 1;
    int dh = (wh > 0) ? wh : 1;

    bgfx::setViewRect(0, 0, 0, (uint16_t)dw, (uint16_t)dh);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);

    float vAspect = (float)vw / (float)vh;
    float wAspect = (float)dw / (float)dh;
    float qw, qh; // half-extents in NDC
    if (vAspect > wAspect) { qw = 1.0f; qh = wAspect / vAspect; }
    else                   { qh = 1.0f; qw = vAspect / wAspect; }

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Color1,    4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    struct V { float px, py, pz, nx, ny, nz; uint32_t c0, c1; float u, v; };
    // Texture is written top-down (row 0 = visual top). NDC +1 Y = top
    // of screen, so the top edge of the quad samples V=0. Matches the
    // boot_anim blit's mapping.
    V verts[4] = {
        { -qw, -qh, 0.f, 0,0,0, 0,0, 0.f, 1.f },
        {  qw, -qh, 0.f, 0,0,0, 0,0, 1.f, 1.f },
        {  qw,  qh, 0.f, 0,0,0, 0,0, 1.f, 0.f },
        { -qw,  qh, 0.f, 0,0,0, 0,0, 0.f, 0.f },
    };
    const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer  tib;
    if (bgfx::getAvailTransientVertexBuffer(4, layout) < 4) return;
    if (bgfx::getAvailTransientIndexBuffer(6)            < 6) return;
    bgfx::allocTransientVertexBuffer(&tvb, 4, layout);
    memcpy(tvb.data, verts, sizeof(verts));
    bgfx::allocTransientIndexBuffer(&tib, 6);
    memcpy(tib.data, idx, sizeof(idx));

    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setTexture(0, g_bgfxSamplerBlit, vidTex);
    bgfx::setVertexBuffer(0, &tvb, 0, 4);
    bgfx::setIndexBuffer(&tib, 0, 6);
    bgfx::submit(0, g_bgfxProgBlit);
    s_videoBlittedToFBO = true;
#endif
}


// Aspect-fit (vw,vh) inside (ww,wh) -> outX/Y/W/H letterbox rect.
static void FitRect(int vw, int vh, int ww, int wh,
                    float& outX, float& outY, float& outW, float& outH)
{
    float wAspect = (float)ww / (float)wh;
    float vAspect = (float)vw / (float)vh;
    if (vAspect > wAspect) {
        outW = (float)ww;
        outH = (float)ww / vAspect;
    } else {
        outH = (float)wh;
        outW = (float)wh * vAspect;
    }
    outX = ((float)ww - outW) * 0.5f;
    outY = ((float)wh - outH) * 0.5f;
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
    // the actual UI canvas on Retina/HiDPI screens. Drawable pixels
    // (SDL_GL_GetDrawableSize) are 2x on Mac Retina and would push the
    // overlay off-screen.
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    int ww = (int)disp.x;
    int wh = (int)disp.y;

    // Auto-end: when libmpv reports stopped/idle (track finished), advance the
    // playlist queue if active, otherwise bail back to dashboard.
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
#ifndef THESEUS_USE_BGFX
            bg->AddImage((ImTextureID)(intptr_t)tex,
                ImVec2(dstX, dstY), ImVec2(dstX + dstW, dstY + dstH),
                ImVec2(0, 1), ImVec2(1, 0));
#else
            bg->AddImage((ImTextureID)(intptr_t)tex,
                ImVec2(dstX, dstY), ImVec2(dstX + dstW, dstY + dstH),
                ImVec2(0, 0), ImVec2(1, 1));
#endif
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
        ImU32 hintCol = RGBA(0.55f, 0.90f, 0.55f, alpha * 0.7f);

        ImFont* font = ImGui::GetFont();
        float timeSize = 16.0f;
        float hintSize = 12.0f;

        // Time on left
        char timeStr[64];
        snprintf(timeStr, sizeof(timeStr), "%s / %s", tPos, tDur);
        dl->AddText(font, timeSize, ImVec2(28, y0 + 28), textCol, timeStr);

        // Hint on right
        const char* hint = MediaUI_PlaylistActive()
            ? "ESC Stop  SPACE Pause  <-/-> Seek  T Tracks  [/] Prev/Next"
            : "ESC Stop  SPACE Pause  <-/-> Seek  T Tracks";
        ImVec2 hintSz = ImGui::CalcTextSize(hint);
        // CalcTextSize uses default font size; estimate hint width by length.
        float estHintW = (float)strlen(hint) * 6.5f;
        dl->AddText(font, hintSize, ImVec2((float)ww - estHintW - 28.0f, y0 + 30), hintCol, hint);

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
}
