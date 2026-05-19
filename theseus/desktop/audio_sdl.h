// audio_sdl.h: desktop audio backend public API. Replaces Xbox
// DirectSound with cross-platform SDL2_mixer. Companion to
// desktop/audio_sdl.cpp.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize/shutdown SDL_mixer audio subsystem
int  DashAudio_Init(void);
void DashAudio_Shutdown(void);

// Sound effects (WAV/MP3 short clips; can play multiple simultaneously)
// Returns a handle for the loaded sound, or -1 on failure
int  DashAudio_LoadSound(const char* path);
void DashAudio_FreeSound(int handle);

// Play a loaded sound on the next free channel. Returns channel number or -1.
int  DashAudio_PlaySound(int handle, int loops, int fadeInMs);
void DashAudio_StopChannel(int channel);
void DashAudio_PauseChannel(int channel);
void DashAudio_ResumeChannel(int channel);
int  DashAudio_IsChannelPlaying(int channel);

// Channel properties
void DashAudio_SetChannelVolume(int channel, float vol);   // 0.0 - 1.0
void DashAudio_SetChannelPan(int channel, float pan);      // -1.0 (left) to 1.0 (right)
void DashAudio_FadeOutChannel(int channel, int fadeMs);

// Streaming music (one track at a time; MP3, WAV, OGG, etc.)
int  DashAudio_LoadMusic(const char* path);
void DashAudio_FreeMusic(void);
void DashAudio_PlayMusic(int loops, int fadeInMs);
void DashAudio_StopMusic(int fadeOutMs);
void DashAudio_PauseMusic(void);
void DashAudio_ResumeMusic(void);
int  DashAudio_IsMusicPlaying(void);
void DashAudio_SetMusicVolume(float vol);  // 0.0 - 1.0
double DashAudio_GetMusicPosition(void);   // seconds
double DashAudio_GetMusicDuration(void);   // seconds (if supported)

// Channel-finished detection (polled from Advance)
// Returns 1 if channel finished since last call, and resets the flag
int  DashAudio_DidChannelFinish(int channel);

// Music collection; scans a directory for soundtracks
// A "soundtrack" is a subdirectory of the music root. Songs are audio files within.
int  DashMusic_Scan(const char* musicRoot);   // e.g. "Data/Music"
const char* DashMusic_GetConfiguredRoot(void); // desktop.ini g_musicRoot or fallback
int  DashMusic_GetSoundtrackCount(void);
int  DashMusic_GetSoundtrackID(int index);
int  DashMusic_GetSoundtrackIndexFromID(int id);
const char* DashMusic_GetSoundtrackName(int stIndex);
int  DashMusic_GetSongCount(int stIndex);
int  DashMusic_GetSongID(int stIndex, int songIndex);
const char* DashMusic_GetSongName(int stIndex, int songIndex);
const char* DashMusic_GetSongPath(int stIndex, int songIndex);
int  DashMusic_GetSongDuration(int stIndex, int songIndex);  // seconds, 0 if unknown

// Resolve a song ID (from GetSoundtrackSongID) back to its filesystem path
// Song ID format: soundtrack_id * 1000 + song_index
const char* DashMusic_GetSongPathByID(int songID);

// Format time as "M:SS"
const char* DashMusic_FormatTime(int totalSeconds);

// Mute/unmute all audio; used for game launch and Ctrl+M toggle
void DashAudio_MuteAll(void);
void DashAudio_UnmuteAll(void);

// Master volume (0.0 - 1.0). Composes on top of per-channel and music volumes.
void DashAudio_SetMasterVolume(float vol);

// PCM capture for audio visualizer
// Fills outLeft/outRight with the most recent 'count' stereo samples (int16)
void DashAudio_GetPCMSamples(int16_t* outLeft, int16_t* outRight, int count);
int  DashAudio_GetPCMSampleCount(void);  // max samples available (512)

#ifdef __cplusplus
}
#endif
