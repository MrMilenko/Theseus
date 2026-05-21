// milkdrop_window.h
//
// Phase 1 of the milkdrop integration. Owns a second SDL window + its own
// GL context + a libprojectM instance. The PCM ring buffer that the
// dashboard already feeds (Mix_SetPostMix -> DashAudio_GetPCMSamples) is
// what we hand to projectM each frame. Audio output is unchanged - same
// SDL_mixer stream to the same speakers, we just visualize alongside.
//
// When libprojectM is not present at build time (no pkg-config hit),
// THESEUS_HAS_MILKDROP is undefined and these all compile to no-ops so
// the main loop can call them unconditionally.

#pragma once

bool MilkdropWindow_IsOpen();
void MilkdropWindow_Toggle();
void MilkdropWindow_Tick();
void MilkdropWindow_Shutdown();

// SDL window ID of the projectM window (0 if not open). Used by the main
// event loop to route F11 to ToggleFullscreen below when the projectM
// window owns the keystroke.
unsigned int MilkdropWindow_GetWindowID();
void         MilkdropWindow_ToggleFullscreen();

// bgfx texture handle ID for the in-scene projectM texture; 0xFFFF when
// invalid (window closed, bgfx not ready, or build has no projectM).
// The scene side casts back to bgfx::TextureHandle{idx}.
unsigned short MilkdropWindow_GetBgfxTexId();
int            MilkdropWindow_GetTexW();
int            MilkdropWindow_GetTexH();

// Raw RGBA readback for code paths that already have a CPU buffer to fill
// (CDynamicTexture's LockRect). Returns NULL when no live render is up.
// outW / outH receive the source dimensions; can be NULL.
const unsigned char* MilkdropWindow_GetReadbackRGBA(int* outW, int* outH);

// Preview window visibility (the secondary SDL window). Hidden by default
// because the fullscreen overlay does the user-facing rendering; show it
// when the configure UI is up so the user can see the preset they're
// adjusting.
void MilkdropWindow_SetPreviewVisible(bool show);

// projectM live controls. No-ops when the session isn't running.
void   MilkdropWindow_NextPreset();
void   MilkdropWindow_PreviousPreset();
float  MilkdropWindow_GetBeatSensitivity();
void   MilkdropWindow_SetBeatSensitivity(float v);
double MilkdropWindow_GetPresetDuration();
void   MilkdropWindow_SetPresetDuration(double seconds);
bool   MilkdropWindow_GetPresetLocked();
void   MilkdropWindow_SetPresetLocked(bool locked);

// Preset list access for a dropdown selector. Names returned are basenames
// with the .milk extension stripped. Cached lazily inside.
int         MilkdropWindow_GetPresetCount();
const char* MilkdropWindow_GetPresetName(int idx);
int         MilkdropWindow_GetCurrentPresetIndex();
void        MilkdropWindow_SetPresetIndex(int idx);
