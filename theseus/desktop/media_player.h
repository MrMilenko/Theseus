// media_player.h: libmpv wrapper public API. Renders video frames
// to an OpenGL texture for display in the DVD player XAP scene.
// Companion to desktop/media_player.cpp.

#pragma once

#include <cstdint>

// Playback state
enum MediaPlayerState {
    MP_IDLE = 0,
    MP_PLAYING,
    MP_PAUSED,
    MP_STOPPED
};

// Initialize the media player subsystem (call once at startup)
bool MediaPlayer_Init();

// Shutdown and free all resources
void MediaPlayer_Shutdown();

// Open a media file for playback
bool MediaPlayer_Open(const char* path);

// Transport controls
void MediaPlayer_Play();
void MediaPlayer_Pause();
void MediaPlayer_TogglePause();
void MediaPlayer_Stop();

// Seeking
void MediaPlayer_Seek(double seconds);       // absolute seek
void MediaPlayer_SeekRelative(double delta);  // relative seek (+/- seconds)

// Chapter navigation (if available)
void MediaPlayer_NextChapter();
void MediaPlayer_PrevChapter();
int  MediaPlayer_GetChapter();
int  MediaPlayer_GetChapterCount();

// Audio/subtitle track cycling
void MediaPlayer_NextAudioTrack();
void MediaPlayer_NextSubtitleTrack();

// Playback speed
void MediaPlayer_SetSpeed(double speed);
double MediaPlayer_GetSpeed();

// State queries
MediaPlayerState MediaPlayer_GetState();
double MediaPlayer_GetPosition();   // current position in seconds
double MediaPlayer_GetDuration();   // total duration in seconds
bool   MediaPlayer_HasVideo();      // true if current file has a video stream

// Video frame rendering
// Call each frame when media is playing. Returns the GL texture ID of the
// current video frame, or 0 if no frame is available. Width/height are output.
unsigned int MediaPlayer_GetVideoTexture(int* outWidth, int* outHeight);

// Update (call each frame to process mpv events)
void MediaPlayer_Update();

// Get the raw FBO ID (for glBlitFramebuffer)
unsigned int MediaPlayer_GetFBO();

// Render current frame directly to framebuffer 0 (the screen)
void MediaPlayer_RenderToScreen(int screenW, int screenH);

// Frame stepping
void MediaPlayer_FrameStep();       // advance one frame (pauses if playing)
void MediaPlayer_FrameBackStep();   // go back one frame (pauses if playing)

// Zoom
void MediaPlayer_SetZoom(double scale);            // 1.0 = normal, 2.0 = 2x, etc.
void MediaPlayer_SetZoomPos(double x, double y);   // pan -1..1

// A-B loop
void MediaPlayer_SetABLoopA();     // set loop point A to current position
void MediaPlayer_SetABLoopB();     // set loop point B to current position
void MediaPlayer_ClearABLoop();    // clear both loop points
int  MediaPlayer_GetABRepeatState(); // 0=off, 1=A set, 2=A-B active

// Track info
int         MediaPlayer_GetAudioChannels();
int         MediaPlayer_GetAudioFormat();    // 0=AC3, 1=MPEG1, 2=MPEG2, 3=PCM/Stereo, 4=DTS, 5=SDDS
const char* MediaPlayer_GetAudioLanguage();  // current audio track language
const char* MediaPlayer_GetSubtitleLanguage(); // current subtitle track language
int         MediaPlayer_GetSubtitleTrack();  // 0 = off, >0 = track index
