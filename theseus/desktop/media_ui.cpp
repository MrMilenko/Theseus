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

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#include <GL/glew.h>
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

static double NowSeconds() { return (double)SDL_GetTicks() / 1000.0; }

void MediaUI_NoteActivity()
{
    s_lastActivity = NowSeconds();
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

    // Blit the video frame into whatever framebuffer is currently bound
    // (the CRT capture FBO when CRT is on, or the backbuffer otherwise).
    // Drawing inside the CRT capture means the CRT post-process applies to
    // the video, not just to the dashboard. Letterbox-fit to viewport size.
    s_videoBlittedToFBO = false;

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

    // Auto-end: when libmpv reports stopped/idle (track finished), bail.
    MediaPlayerState st = MediaPlayer_GetState();
    if (st == MP_IDLE || st == MP_STOPPED) {
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
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            bg->AddImage((ImTextureID)(intptr_t)tex,
                ImVec2(dstX, dstY), ImVec2(dstX + dstW, dstY + dstH),
                ImVec2(0, 1), ImVec2(1, 0));
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

    // Top gradient — Xbox green tint, fades to transparent.
    {
        const float h = 96.0f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
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

    // Bottom gradient — fades up from black; transport sits on it.
    {
        const float h = 110.0f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
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
        const char* hint = "ESC  Stop     SPACE  Pause     <-/->  Seek 5s";
        ImVec2 hintSz = ImGui::CalcTextSize(hint);
        // CalcTextSize uses default font size; estimate hint width by length.
        float estHintW = (float)strlen(hint) * 6.5f;
        dl->AddText(font, hintSize, ImVec2((float)ww - estHintW - 28.0f, y0 + 30), hintCol, hint);

        // Progress bar — Xbox green track on dim bg.
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
}


// ============================================================================
// Stop fullscreen video, return to dashboard.
// ============================================================================

void MediaUI_StopFullscreen()
{
    if (!g_mediaFullscreen) return;
    MediaPlayer_Stop();
    DashAudio_UnmuteAll();
    g_mediaFullscreen = false;
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
