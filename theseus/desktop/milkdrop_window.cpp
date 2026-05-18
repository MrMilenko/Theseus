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

#else // THESEUS_HAS_MILKDROP

#include <SDL.h>
#include <libprojectM/projectM.hpp>
#include "audio_sdl.h"

#include <cstdio>
#include <cstring>

namespace {

SDL_Window*   s_win  = nullptr;
SDL_GLContext s_glc  = nullptr;
projectM*     s_pM   = nullptr;
bool          s_open = false;

bool OpenWindow()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    const int kW = 1280, kH = 720;
    s_win = SDL_CreateWindow(
        "UIX Desktop / MilkDrop",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kW, kH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    // projectM Settings. Preset path comes from Homebrew on macOS; we fall
    // back to a couple of likely Linux locations too. If none of them work,
    // projectM still inits but cycles through nothing.
    projectM::Settings cfg;
    cfg.windowWidth  = kW;
    cfg.windowHeight = kH;
    cfg.meshX        = 32;
    cfg.meshY        = 24;
    cfg.fps          = 60;
    cfg.textureSize  = 1024;
    cfg.smoothPresetDuration = 5;
    cfg.presetDuration       = 22;
    cfg.beatSensitivity      = 1.0f;
    cfg.aspectCorrection     = true;
    cfg.shuffleEnabled       = true;
    cfg.softCutRatingsEnabled = false;

    // Pick a preset directory that actually exists. fopen-the-directory-itself
    // works on macOS but not on Linux, so probe a known .milk inside each.
    const char* presetDirs[] = {
        "/opt/homebrew/share/projectM/presets/presets_milkdrop_200",
        "/opt/homebrew/share/projectM/presets/presets_milkdrop_104",
        "/usr/local/share/projectM/presets/presets_milkdrop_200",
        "/usr/share/projectM/presets/presets_milkdrop_200",
        nullptr,
    };
    cfg.presetURL = "";
    for (int i = 0; presetDirs[i]; ++i) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s/.", presetDirs[i]);
        FILE* fp = fopen(probe, "r");
        if (fp) { fclose(fp); cfg.presetURL = presetDirs[i]; break; }
    }
    if (cfg.presetURL.empty()) {
        fprintf(stderr, "[Milkdrop] no preset directory found; aborting open\n");
        SDL_GL_DeleteContext(s_glc); s_glc = nullptr;
        SDL_DestroyWindow(s_win);    s_win = nullptr;
        return false;
    }
    cfg.titleFontURL = "/opt/homebrew/share/projectM/fonts/Vera.ttf";
    cfg.menuFontURL  = "/opt/homebrew/share/projectM/fonts/VeraMono.ttf";
    cfg.datadir      = "/opt/homebrew/share/projectM";

    try {
        // No FLAG_DISABLE_PLAYLIST_LOAD - projectM needs to actually load
        // presets from cfg.presetURL or its first renderFrame walks
        // uninitialized warp-mesh vertex buffers and segfaults inside
        // glDrawArrays. The few hundred ms of preset scanning is worth it.
        s_pM = new projectM(cfg);
    } catch (...) {
        fprintf(stderr, "[Milkdrop] projectM ctor threw\n");
        SDL_GL_DeleteContext(s_glc); s_glc = nullptr;
        SDL_DestroyWindow(s_win);    s_win = nullptr;
        return false;
    }
    s_pM->projectM_resetGL(kW, kH);
    fprintf(stdout, "[Milkdrop] presets=%d at %s\n",
        (int)s_pM->getPlaylistSize(), cfg.presetURL.c_str());

    // Hand the GL context back to nobody so bgfx's main thread doesnt
    // accidentally start binding to it.
    SDL_GL_MakeCurrent(s_win, nullptr);
    return true;
}

void CloseWindow()
{
    if (s_pM) { delete s_pM; s_pM = nullptr; }
    if (s_glc) { SDL_GL_DeleteContext(s_glc); s_glc = nullptr; }
    if (s_win) { SDL_DestroyWindow(s_win); s_win = nullptr; }
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

    // Pull latest 512 samples from the post-mix ring. PCM16Data wants
    // interleaved stereo shorts, length in samples (frames, not bytes).
    int16_t left[512], right[512];
    DashAudio_GetPCMSamples(left, right, 512);
    short interleaved[1024];
    for (int i = 0; i < 512; ++i) {
        interleaved[i * 2 + 0] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }
    s_pM->pcm()->addPCM16Data(interleaved, 512);

    // Render. Make our context current, draw, swap, release context.
    SDL_GL_MakeCurrent(s_win, s_glc);

    int w = 0, h = 0;
    SDL_GetWindowSize(s_win, &w, &h);
    if (w > 0 && h > 0) {
        // projectM_resetGL when the window resizes (cheap when size unchanged).
        static int lastW = 0, lastH = 0;
        if (w != lastW || h != lastH) {
            s_pM->projectM_resetGL(w, h);
            lastW = w; lastH = h;
        }
    }

    s_pM->renderFrame();
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

#endif // THESEUS_HAS_MILKDROP
