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
