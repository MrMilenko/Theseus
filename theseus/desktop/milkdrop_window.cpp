// milkdrop_window.cpp
//
// libprojectM in a second SDL window. Toggled with X+Y; press it again to
// dismiss. Keeps its own GL context so it never interferes with the main
// dashboard window (which is bgfx-backed on the desktop targets).

#include "milkdrop_window.h"

#ifndef THESEUS_HAS_MILKDROP

bool MilkdropWindow_IsOpen() { return false; }
void MilkdropWindow_Toggle()   {}
void MilkdropWindow_Tick()     {}
void MilkdropWindow_Shutdown() {}
unsigned int MilkdropWindow_GetWindowID()    { return 0; }
void         MilkdropWindow_ToggleFullscreen() {}
unsigned short MilkdropWindow_GetBgfxTexId() { return 0xFFFF; }
int            MilkdropWindow_GetTexW()      { return 0; }
int            MilkdropWindow_GetTexH()      { return 0; }
const unsigned char* MilkdropWindow_GetReadbackRGBA(int*, int*) { return nullptr; }
void   MilkdropWindow_SetPreviewVisible(bool) {}
void   MilkdropWindow_NextPreset() {}
void   MilkdropWindow_PreviousPreset() {}
float  MilkdropWindow_GetBeatSensitivity()  { return 1.0f; }
void   MilkdropWindow_SetBeatSensitivity(float) {}
double MilkdropWindow_GetPresetDuration()   { return 22.0; }
void   MilkdropWindow_SetPresetDuration(double) {}
bool   MilkdropWindow_GetPresetLocked()     { return false; }
void   MilkdropWindow_SetPresetLocked(bool) {}
int         MilkdropWindow_GetPresetCount()      { return 0; }
const char* MilkdropWindow_GetPresetName(int)    { return ""; }
int         MilkdropWindow_GetCurrentPresetIndex() { return 0; }
void        MilkdropWindow_SetPresetIndex(int)   {}

#else // THESEUS_HAS_MILKDROP

#include <SDL.h>
#include <SDL_opengl.h>
#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include <projectM-4/render_opengl.h>
#include <bgfx/bgfx.h>
#include "audio_sdl.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>

// GL 3.0+ entry points used here. macOS ships them via <OpenGL/gl3.h>, but
// SDL_opengl.h already pulls those in on platforms with a system GL.
// We resolve them at runtime via SDL_GL_GetProcAddress to avoid linking gotchas.
typedef void (APIENTRY *PFNGENFBO)(GLsizei, GLuint*);
typedef void (APIENTRY *PFNBINDFBO)(GLenum, GLuint);
typedef void (APIENTRY *PFNFBOTEX2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFNFBOSTATUS)(GLenum);
typedef void (APIENTRY *PFNDELFBO)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFNBLITFB)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);

namespace {

SDL_Window*               s_win      = nullptr;
SDL_GLContext             s_glc      = nullptr;
projectm_handle           s_pM       = nullptr;
projectm_playlist_handle  s_playlist = nullptr;
bool                      s_open     = false;

// Offscreen target projectM renders into. Sized to a power of two so bgfx
// can sample it cleanly later (Phase B). The visible window blits from here
// during Phase A; Phase B switches to glReadPixels + bgfx upload.
GLuint                    s_fbo      = 0;
GLuint                    s_fboTex   = 0;
const int                 kFboW      = 1024;
const int                 kFboH      = 1024;

// Phase B: readback + bgfx texture exposed to the scene.
bgfx::TextureHandle       s_bgfxTex  = BGFX_INVALID_HANDLE;
uint8_t*                  s_readback = nullptr;

// GL function pointers resolved at OpenWindow time.
PFNGENFBO    s_glGenFramebuffers         = nullptr;
PFNBINDFBO   s_glBindFramebuffer         = nullptr;
PFNFBOTEX2D  s_glFramebufferTexture2D    = nullptr;
PFNFBOSTATUS s_glCheckFramebufferStatus  = nullptr;
PFNDELFBO    s_glDeleteFramebuffers      = nullptr;
PFNBLITFB    s_glBlitFramebuffer         = nullptr;

bool LoadGLEntries() {
    s_glGenFramebuffers        = (PFNGENFBO)   SDL_GL_GetProcAddress("glGenFramebuffers");
    s_glBindFramebuffer        = (PFNBINDFBO)  SDL_GL_GetProcAddress("glBindFramebuffer");
    s_glFramebufferTexture2D   = (PFNFBOTEX2D) SDL_GL_GetProcAddress("glFramebufferTexture2D");
    s_glCheckFramebufferStatus = (PFNFBOSTATUS)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    s_glDeleteFramebuffers     = (PFNDELFBO)   SDL_GL_GetProcAddress("glDeleteFramebuffers");
    s_glBlitFramebuffer        = (PFNBLITFB)   SDL_GL_GetProcAddress("glBlitFramebuffer");
    return s_glGenFramebuffers && s_glBindFramebuffer && s_glFramebufferTexture2D &&
           s_glCheckFramebufferStatus && s_glDeleteFramebuffers && s_glBlitFramebuffer;
}

bool CreateOffscreenTarget() {
    s_glGenFramebuffers(1, &s_fbo);
    glGenTextures(1, &s_fboTex);

    glBindTexture(GL_TEXTURE_2D, s_fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFboW, kFboH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    s_glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    s_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_fboTex, 0);
    GLenum st = s_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    s_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[Milkdrop] FBO incomplete: 0x%X\n", (unsigned)st);
        return false;
    }
    return true;
}

void DestroyOffscreenTarget() {
    if (s_fboTex) { glDeleteTextures(1, &s_fboTex); s_fboTex = 0; }
    if (s_fbo && s_glDeleteFramebuffers) { s_glDeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
}

// Preset-name cache for the dropdown selector. Populated lazily on
// first access; cleared in CloseWindow when the session tears down.
std::vector<std::string> s_presetNames;
int s_cachedSize = -1;

void EnsureBgfxTexture() {
    if (bgfx::isValid(s_bgfxTex)) return;
    s_bgfxTex = bgfx::createTexture2D(
        (uint16_t)kFboW, (uint16_t)kFboH, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
}

void DestroyBgfxTexture() {
    if (bgfx::isValid(s_bgfxTex)) {
        bgfx::destroy(s_bgfxTex);
        s_bgfxTex = BGFX_INVALID_HANDLE;
    }
    if (s_readback) { std::free(s_readback); s_readback = nullptr; }
}

bool OpenWindow()
{
    // projectM 4.x renderer requires GL 3.3 core profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    const int kW = 1280, kH = 720;
    // Create visible so Metal/SDL allocates the framebuffer, then hide
    // immediately. Show on demand via MilkdropWindow_SetPreviewVisible.
    s_win = SDL_CreateWindow(
        "UIX Desktop / projectM",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kW, kH,
        SDL_WINDOW_OPENGL);
    if (!s_win) {
        fprintf(stderr, "[Milkdrop] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    s_glc = SDL_GL_CreateContext(s_win);
    if (!s_glc) {
        fprintf(stderr, "[Milkdrop] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_win); s_win = nullptr;
        return false;
    }
    SDL_GL_MakeCurrent(s_win, s_glc);

    if (!LoadGLEntries()) {
        fprintf(stderr, "[Milkdrop] could not resolve GL FBO entry points\n");
        SDL_GL_DeleteContext(s_glc); s_glc = nullptr;
        SDL_DestroyWindow(s_win);    s_win = nullptr;
        return false;
    }
    if (!CreateOffscreenTarget()) {
        SDL_GL_DeleteContext(s_glc); s_glc = nullptr;
        SDL_DestroyWindow(s_win);    s_win = nullptr;
        return false;
    }
    s_readback = (uint8_t*)std::malloc((size_t)kFboW * kFboH * 4);

    s_pM = projectm_create();
    if (!s_pM) {
        fprintf(stderr, "[Milkdrop] projectm_create() failed\n");
        DestroyOffscreenTarget();
        SDL_GL_DeleteContext(s_glc); s_glc = nullptr;
        SDL_DestroyWindow(s_win);    s_win = nullptr;
        return false;
    }
    // projectM's "window size" is really its render target size; match the
    // FBO so the projection matrices it computes line up.
    projectm_set_window_size(s_pM, (size_t)kFboW, (size_t)kFboH);
    projectm_set_mesh_size(s_pM, 32, 24);
    projectm_set_fps(s_pM, 60);
    projectm_set_beat_sensitivity(s_pM, 1.0f);
    projectm_set_aspect_correction(s_pM, true);
    projectm_set_preset_duration(s_pM, 22.0);
    projectm_set_soft_cut_duration(s_pM, 5.0);

    // Pick a preset directory that actually exists. Probe a known .milk
    // inside each (fopen on the bare dir works on macOS but not on Linux).
    const char* presetDirs[] = {
        "/opt/homebrew/share/projectM/presets/presets_milkdrop_200",
        "/opt/homebrew/share/projectM/presets/presets_milkdrop_104",
        "/usr/local/share/projectM/presets/presets_milkdrop_200",
        "/usr/share/projectM/presets/presets_milkdrop_200",
        nullptr,
    };
    const char* presetPath = nullptr;
    for (int i = 0; presetDirs[i]; ++i) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s/.", presetDirs[i]);
        FILE* fp = fopen(probe, "r");
        if (fp) { fclose(fp); presetPath = presetDirs[i]; break; }
    }

    s_playlist = projectm_playlist_create(s_pM);
    if (s_playlist && presetPath) {
        uint32_t added = projectm_playlist_add_path(s_playlist, presetPath, true, false);
        fprintf(stdout, "[Milkdrop] %u presets loaded from %s\n", added, presetPath);
        if (added > 0) projectm_playlist_play_next(s_playlist, true);
    } else if (!presetPath) {
        fprintf(stderr, "[Milkdrop] no preset directory found; renderer will idle\n");
    }

    SDL_GL_MakeCurrent(s_win, nullptr);
    // Hide immediately; user shows it explicitly via the Configure panel.
    SDL_HideWindow(s_win);
    return true;
}

void CloseWindow()
{
    DestroyBgfxTexture();
    if (s_playlist) { projectm_playlist_destroy(s_playlist); s_playlist = nullptr; }
    if (s_pM)       { projectm_destroy(s_pM); s_pM = nullptr; }
    DestroyOffscreenTarget();
    if (s_glc) { SDL_GL_DeleteContext(s_glc); s_glc = nullptr; }
    if (s_win) { SDL_DestroyWindow(s_win); s_win = nullptr; }
    s_presetNames.clear();
    s_cachedSize = -1;
}

} // anon

bool MilkdropWindow_IsOpen() { return s_open; }

void MilkdropWindow_Toggle()
{
    if (s_open) {
        CloseWindow();
        s_open = false;
        return;
    }
    if (OpenWindow()) {
        s_open = true;
    }
}

void MilkdropWindow_Tick()
{
    if (!s_open || !s_pM || !s_win || !s_glc) return;

    // Pull latest 512 samples from the post-mix ring. projectm_pcm_add_int16
    // wants interleaved stereo shorts, count in frames, channel count = 2.
    int16_t left[512], right[512];
    DashAudio_GetPCMSamples(left, right, 512);


    int16_t interleaved[1024];
    for (int i = 0; i < 512; ++i) {
        interleaved[i * 2 + 0] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }
    projectm_pcm_add_int16(s_pM, interleaved, 512, PROJECTM_STEREO);

    SDL_GL_MakeCurrent(s_win, s_glc);

    // Render projectM into our offscreen FBO at its native size, then blit
    // to the visible window's framebuffer. Phase A keeps the window so we
    // can confirm visually; Phase B will read pixels back instead and the
    // window can be hidden.
    projectm_opengl_render_frame_fbo(s_pM, s_fbo);

    // Read pixels back to CPU, push into the bgfx texture for the scene.
    if (s_readback) {
        s_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, kFboW, kFboH, GL_RGBA, GL_UNSIGNED_BYTE, s_readback);
        s_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        EnsureBgfxTexture();
        if (bgfx::isValid(s_bgfxTex)) {
            const uint32_t bytes = (uint32_t)kFboW * kFboH * 4;
            bgfx::updateTexture2D(s_bgfxTex, 0, 0, 0, 0,
                (uint16_t)kFboW, (uint16_t)kFboH,
                bgfx::copy(s_readback, bytes));
        }
    }

    int winW = 0, winH = 0;
    SDL_GetWindowSize(s_win, &winW, &winH);
    if (winW > 0 && winH > 0) {
        s_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
        s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        s_glBlitFramebuffer(0, 0, kFboW, kFboH,
                            0, 0, winW, winH,
                            GL_COLOR_BUFFER_BIT, GL_LINEAR);
        s_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    SDL_GL_SwapWindow(s_win);
    SDL_GL_MakeCurrent(s_win, nullptr);

    // Handle close requests on the secondary window so the user can hit
    // the red x as well as toggling X+Y again.
    Uint32 wid = SDL_GetWindowID(s_win);
    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT) > 0) {
        if (ev.window.windowID == wid && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
            CloseWindow();
            s_open = false;
            return;
        }
    }
}

void MilkdropWindow_Shutdown()
{
    if (s_open) {
        CloseWindow();
        s_open = false;
    }
}

unsigned int MilkdropWindow_GetWindowID()
{
    return s_win ? (unsigned int)SDL_GetWindowID(s_win) : 0;
}

void MilkdropWindow_ToggleFullscreen()
{
    if (!s_win) return;
    Uint32 flags = SDL_GetWindowFlags(s_win);
    bool fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(s_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

unsigned short MilkdropWindow_GetBgfxTexId()
{
    return bgfx::isValid(s_bgfxTex) ? s_bgfxTex.idx : (unsigned short)0xFFFF;
}

int MilkdropWindow_GetTexW() { return bgfx::isValid(s_bgfxTex) ? kFboW : 0; }
int MilkdropWindow_GetTexH() { return bgfx::isValid(s_bgfxTex) ? kFboH : 0; }

const unsigned char* MilkdropWindow_GetReadbackRGBA(int* outW, int* outH)
{
    if (!s_open || !s_readback) return nullptr;
    if (outW) *outW = kFboW;
    if (outH) *outH = kFboH;
    return s_readback;
}

void MilkdropWindow_SetPreviewVisible(bool show)
{
    if (!s_win) return;
    if (show) SDL_ShowWindow(s_win);
    else      SDL_HideWindow(s_win);
}

// Debounce: ignore rapid repeats within this window so accidental
// double-clicks / sticky UI events don't skip presets in bursts.
static Uint32 s_lastPresetActionMs = 0;
static bool PresetActionDebounce() {
    Uint32 now = SDL_GetTicks();
    if (now - s_lastPresetActionMs < 250) return false;
    s_lastPresetActionMs = now;
    return true;
}

// Switching presets compiles shader programs, which requires projectM's GL
// context to be current. Outside of Tick, the dashboard's bgfx context owns
// GL, so any explicit nav call must rebind first or the new preset's GLSL
// compile silently fails and the preview goes black.
static void JumpToPreset(uint32_t idx) {
    if (!s_playlist || !s_win || !s_glc) return;
    SDL_GL_MakeCurrent(s_win, s_glc);
    bool wasLocked = s_pM && projectm_get_preset_locked(s_pM);
    if (wasLocked) projectm_set_preset_locked(s_pM, false);
    projectm_playlist_set_position(s_playlist, idx, false);
    if (wasLocked) projectm_set_preset_locked(s_pM, true);
    SDL_GL_MakeCurrent(s_win, nullptr);
}

void MilkdropWindow_NextPreset()
{
    if (!s_playlist || !PresetActionDebounce()) return;
    uint32_t sz = projectm_playlist_size(s_playlist);
    if (sz == 0) return;
    uint32_t cur = projectm_playlist_get_position(s_playlist);
    JumpToPreset((cur + 1) % sz);
}

void MilkdropWindow_PreviousPreset()
{
    if (!s_playlist || !PresetActionDebounce()) return;
    uint32_t sz = projectm_playlist_size(s_playlist);
    if (sz == 0) return;
    uint32_t cur = projectm_playlist_get_position(s_playlist);
    JumpToPreset((cur == 0) ? (sz - 1) : (cur - 1));
}

float MilkdropWindow_GetBeatSensitivity()
{
    return s_pM ? projectm_get_beat_sensitivity(s_pM) : 1.0f;
}

void MilkdropWindow_SetBeatSensitivity(float v)
{
    if (s_pM) projectm_set_beat_sensitivity(s_pM, v);
}

double MilkdropWindow_GetPresetDuration()
{
    return s_pM ? projectm_get_preset_duration(s_pM) : 22.0;
}

void MilkdropWindow_SetPresetDuration(double seconds)
{
    if (s_pM) projectm_set_preset_duration(s_pM, seconds);
}

bool MilkdropWindow_GetPresetLocked()
{
    return s_pM ? projectm_get_preset_locked(s_pM) : false;
}

void MilkdropWindow_SetPresetLocked(bool locked)
{
    if (s_pM) projectm_set_preset_locked(s_pM, locked);
}

static void EnsurePresetNamesCached()
{
    if (!s_playlist) { s_presetNames.clear(); s_cachedSize = -1; return; }
    int sz = (int)projectm_playlist_size(s_playlist);
    if (sz == s_cachedSize) return;
    s_presetNames.clear();
    s_presetNames.reserve(sz);
    for (int i = 0; i < sz; i++) {
        char* full = projectm_playlist_item(s_playlist, (uint32_t)i);
        if (!full) { s_presetNames.push_back(std::string("(?)")); continue; }
        const char* base = full;
        for (const char* p = full; *p; p++) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
        std::string name = base;
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) name.resize(dot);
        s_presetNames.push_back(std::move(name));
        projectm_playlist_free_string(full);
    }
    s_cachedSize = sz;
}

int MilkdropWindow_GetPresetCount()
{
    return s_playlist ? (int)projectm_playlist_size(s_playlist) : 0;
}

const char* MilkdropWindow_GetPresetName(int idx)
{
    EnsurePresetNamesCached();
    if (idx < 0 || idx >= (int)s_presetNames.size()) return "";
    return s_presetNames[idx].c_str();
}

int MilkdropWindow_GetCurrentPresetIndex()
{
    return s_playlist ? (int)projectm_playlist_get_position(s_playlist) : 0;
}

void MilkdropWindow_SetPresetIndex(int idx)
{
    if (!s_playlist) return;
    uint32_t sz = projectm_playlist_size(s_playlist);
    if (idx < 0 || (uint32_t)idx >= sz) return;
    JumpToPreset((uint32_t)idx);
}

#endif // THESEUS_HAS_MILKDROP
