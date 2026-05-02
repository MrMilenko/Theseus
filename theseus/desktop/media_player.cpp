// media_player.cpp: libmpv wrapper for media playback. Uses mpv's
// OpenGL render API to render video frames to an FBO, then exposes
// it as a GL texture for the dashboard's DVD player scene to
// display. Desktop-only.

#ifdef UIX_MEDIA_PLAYER  // only compile if libmpv is available

#include "media_player.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <SDL.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

// ============================================================================
// State
// ============================================================================

static mpv_handle*         s_mpv = nullptr;
static mpv_render_context* s_mpvGL = nullptr;
static MediaPlayerState    s_state = MP_IDLE;

// Video FBO
static GLuint s_fbo = 0;
static GLuint s_videoTex = 0;
static int    s_videoWidth = 0;
static int    s_videoHeight = 0;
static bool   s_hasVideo = false;
static bool   s_needsRender = false;

// Cached properties
static double s_position = 0.0;
static double s_duration = 0.0;
static double s_speed = 1.0;
static int    s_chapter = 0;
static int    s_chapterCount = 0;

// A-B loop state
static int    s_abRepeatState = 0;  // 0=off, 1=A set, 2=A-B active
static double s_abLoopA = -1.0;
static double s_abLoopB = -1.0;

// Track info (queried on demand)
static int    s_audioChannels = 0;
static char   s_audioCodec[64] = "";
static char   s_audioLang[64] = "";
static char   s_subLang[64] = "";
static int    s_subTrack = 0;

// ============================================================================
// OpenGL helpers
// ============================================================================

static void EnsureFBO(int w, int h) {
    if (s_fbo && s_videoWidth == w && s_videoHeight == h)
        return;

    // Delete old
    if (s_fbo) glDeleteFramebuffers(1, &s_fbo);
    if (s_videoTex) glDeleteTextures(1, &s_videoTex);

    s_videoWidth = w;
    s_videoHeight = h;

    // Create texture - use rgba16f to match mpv's preferred format
    glGenTextures(1, &s_videoTex);
    glBindTexture(GL_TEXTURE_2D, s_videoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create FBO
    glGenFramebuffers(1, &s_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_videoTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    fprintf(stderr, "[MediaPlayer] Created video FBO: %dx%d\n", w, h);
}

// mpv render callback: called when a new frame is available
static void OnMpvRenderUpdate(void* ctx) {
    s_needsRender = true;
}

// mpv GL get_proc_address callback
static void* GetProcAddress(void* ctx, const char* name) {
    return (void*)SDL_GL_GetProcAddress(name);
}

// ============================================================================
// Init / Shutdown
// ============================================================================

bool MediaPlayer_Init() {
    s_mpv = mpv_create();
    if (!s_mpv) {
        fprintf(stderr, "[MediaPlayer] mpv_create() failed\n");
        return false;
    }

    // Set options before init
    mpv_set_option_string(s_mpv, "vo", "libmpv");       // render to our FBO, not a window
    mpv_set_option_string(s_mpv, "hwdec", "no");           // software decode - avoids GL texture errors on macOS core profile
    mpv_set_option_string(s_mpv, "keep-open", "yes");
    mpv_set_option_string(s_mpv, "video", "yes");
    mpv_set_option_string(s_mpv, "terminal", "yes");     // enable mpv log output
    mpv_set_option_string(s_mpv, "msg-level", "all=v");  // verbose logging
    mpv_set_option_string(s_mpv, "osc", "no");

    if (mpv_initialize(s_mpv) < 0) {
        fprintf(stderr, "[MediaPlayer] mpv_initialize() failed\n");
        mpv_destroy(s_mpv);
        s_mpv = nullptr;
        return false;
    }

    // Create OpenGL render context
    mpv_opengl_init_params gl_init = { GetProcAddress, nullptr };
    int advanced = 1;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };

    if (mpv_render_context_create(&s_mpvGL, s_mpv, params) < 0) {
        fprintf(stderr, "[MediaPlayer] mpv_render_context_create() failed\n");
        mpv_destroy(s_mpv);
        s_mpv = nullptr;
        return false;
    }

    mpv_render_context_set_update_callback(s_mpvGL, OnMpvRenderUpdate, nullptr);

    // Observe properties
    mpv_observe_property(s_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(s_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(s_mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(s_mpv, 0, "chapter", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "chapter-list/count", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "video-params/w", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "video-params/h", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "audio-params/channel-count", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "audio-codec-name", MPV_FORMAT_STRING);
    mpv_observe_property(s_mpv, 0, "sid", MPV_FORMAT_INT64);

    s_state = MP_IDLE;
    fprintf(stderr, "[MediaPlayer] Initialized (libmpv + OpenGL render)\n");
    return true;
}

void MediaPlayer_Shutdown() {
    if (s_mpvGL) {
        mpv_render_context_free(s_mpvGL);
        s_mpvGL = nullptr;
    }
    if (s_mpv) {
        mpv_destroy(s_mpv);
        s_mpv = nullptr;
    }
    if (s_fbo) { glDeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
    if (s_videoTex) { glDeleteTextures(1, &s_videoTex); s_videoTex = 0; }
    s_state = MP_IDLE;
    fprintf(stderr, "[MediaPlayer] Shutdown\n");
}

// ============================================================================
// Transport
// ============================================================================

bool MediaPlayer_Open(const char* path) {
    if (!s_mpv) return false;

    const char* cmd[] = { "loadfile", path, NULL };
    mpv_command(s_mpv, cmd);

    s_state = MP_PLAYING;
    s_position = 0.0;
    s_duration = 0.0;
    s_hasVideo = false;
    fprintf(stderr, "[MediaPlayer] Opening: %s\n", path);
    return true;
}

void MediaPlayer_Play() {
    if (!s_mpv) return;
    mpv_set_property_string(s_mpv, "pause", "no");
    s_state = MP_PLAYING;
}

void MediaPlayer_Pause() {
    if (!s_mpv) return;
    mpv_set_property_string(s_mpv, "pause", "yes");
    s_state = MP_PAUSED;
}

void MediaPlayer_TogglePause() {
    if (s_state == MP_PLAYING) MediaPlayer_Pause();
    else if (s_state == MP_PAUSED) MediaPlayer_Play();
}

void MediaPlayer_Stop() {
    if (!s_mpv) return;
    const char* cmd[] = { "stop", NULL };
    mpv_command(s_mpv, cmd);
    s_state = MP_STOPPED;
    s_position = 0.0;
    s_hasVideo = false;
}

void MediaPlayer_Seek(double seconds) {
    if (!s_mpv) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", seconds);
    const char* cmd[] = { "seek", buf, "absolute", NULL };
    mpv_command(s_mpv, cmd);
}

void MediaPlayer_SeekRelative(double delta) {
    if (!s_mpv) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", delta);
    const char* cmd[] = { "seek", buf, "relative", NULL };
    mpv_command(s_mpv, cmd);
}

void MediaPlayer_NextChapter() {
    if (!s_mpv) return;
    const char* cmd[] = { "add", "chapter", "1", NULL };
    mpv_command(s_mpv, cmd);
}

void MediaPlayer_PrevChapter() {
    if (!s_mpv) return;
    const char* cmd[] = { "add", "chapter", "-1", NULL };
    mpv_command(s_mpv, cmd);
}

int MediaPlayer_GetChapter() { return s_chapter; }
int MediaPlayer_GetChapterCount() { return s_chapterCount; }

void MediaPlayer_NextAudioTrack() {
    if (!s_mpv) return;
    const char* cmd[] = { "cycle", "audio", NULL };
    mpv_command(s_mpv, cmd);
}

void MediaPlayer_NextSubtitleTrack() {
    if (!s_mpv) return;
    const char* cmd[] = { "cycle", "sub", NULL };
    mpv_command(s_mpv, cmd);
}

void MediaPlayer_SetSpeed(double speed) {
    if (!s_mpv) return;
    mpv_set_property(s_mpv, "speed", MPV_FORMAT_DOUBLE, &speed);
}

double MediaPlayer_GetSpeed() { return s_speed; }

// ============================================================================
// State queries
// ============================================================================

MediaPlayerState MediaPlayer_GetState() { return s_state; }
double MediaPlayer_GetPosition() { return s_position; }
double MediaPlayer_GetDuration() { return s_duration; }
bool   MediaPlayer_HasVideo() { return s_hasVideo; }

// ============================================================================
// Update + render
// ============================================================================

void MediaPlayer_Update() {
    if (!s_mpv) return;

    // Process mpv events
    while (1) {
        mpv_event* event = mpv_wait_event(s_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                mpv_event_property* prop = (mpv_event_property*)event->data;
                if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE)
                    s_position = *(double*)prop->data;
                else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE)
                    s_duration = *(double*)prop->data;
                else if (strcmp(prop->name, "speed") == 0 && prop->format == MPV_FORMAT_DOUBLE)
                    s_speed = *(double*)prop->data;
                else if (strcmp(prop->name, "chapter") == 0 && prop->format == MPV_FORMAT_INT64)
                    s_chapter = (int)*(int64_t*)prop->data;
                else if (strcmp(prop->name, "chapter-list/count") == 0 && prop->format == MPV_FORMAT_INT64)
                    s_chapterCount = (int)*(int64_t*)prop->data;
                else if (strcmp(prop->name, "video-params/w") == 0 && prop->format == MPV_FORMAT_INT64) {
                    int w = (int)*(int64_t*)prop->data;
                    if (w > 0) { s_hasVideo = true; s_videoWidth = w; }
                }
                else if (strcmp(prop->name, "video-params/h") == 0 && prop->format == MPV_FORMAT_INT64) {
                    int h = (int)*(int64_t*)prop->data;
                    if (h > 0) s_videoHeight = h;
                }
                else if (strcmp(prop->name, "audio-params/channel-count") == 0 && prop->format == MPV_FORMAT_INT64)
                    s_audioChannels = (int)*(int64_t*)prop->data;
                else if (strcmp(prop->name, "audio-codec-name") == 0 && prop->format == MPV_FORMAT_STRING) {
                    const char* codec = *(const char**)prop->data;
                    if (codec) snprintf(s_audioCodec, sizeof(s_audioCodec), "%s", codec);
                }
                else if (strcmp(prop->name, "sid") == 0 && prop->format == MPV_FORMAT_INT64)
                    s_subTrack = (int)*(int64_t*)prop->data;
                break;
            }
            case MPV_EVENT_END_FILE:
                s_state = MP_STOPPED;
                break;
            case MPV_EVENT_FILE_LOADED:
                s_state = MP_PLAYING;
                break;
            default:
                break;
        }
    }

    // Render video frame whenever mpv has an update
    if (s_mpvGL && (mpv_render_context_update(s_mpvGL) & MPV_RENDER_UPDATE_FRAME)) {
        // Use video dimensions if known, otherwise default to 640x480
        int w = s_videoWidth > 0 ? s_videoWidth : 640;
        int h = s_videoHeight > 0 ? s_videoHeight : 480;

        EnsureFBO(w, h);
        s_hasVideo = true;

        // Save current GL state and reset to defaults for mpv
        GLint prevFBO, prevViewport[4], prevProg, prevVAO, prevTex;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

        // Reset GL state to defaults (mpv expects clean state)
        glUseProgram(0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        glActiveTexture(GL_TEXTURE0);

        mpv_opengl_fbo fbo_params = {
            (int)s_fbo,
            w, h,
            0  // internal format
        };

        int flip = 1;
        mpv_render_param render_params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &fbo_params },
            { MPV_RENDER_PARAM_FLIP_Y, &flip },
            { MPV_RENDER_PARAM_INVALID, nullptr }
        };

        mpv_render_context_render(s_mpvGL, render_params);
        // Don't call report_swap - it causes mpv to clear the FBO

        // Restore GL state
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glUseProgram(prevProg);
        glBindVertexArray(prevVAO);
        glBindTexture(GL_TEXTURE_2D, prevTex);
    }
}

unsigned int MediaPlayer_GetVideoTexture(int* outWidth, int* outHeight) {
    if (!s_hasVideo || !s_videoTex) return 0;
    if (outWidth) *outWidth = s_videoWidth;
    if (outHeight) *outHeight = s_videoHeight;
    return s_videoTex;
}

unsigned int MediaPlayer_GetFBO() {
    return s_fbo;
}

void MediaPlayer_RenderToScreen(int screenW, int screenH) {
    if (!s_mpvGL) return;

    // Render to whatever FBO is currently bound (CRT capture FBO or screen)
    GLint currentFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

    mpv_opengl_fbo fbo_params = {
        (int)currentFBO,
        screenW, screenH,
        0
    };

    int flip = 1;
    mpv_render_param render_params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo_params },
        { MPV_RENDER_PARAM_FLIP_Y, &flip },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };

    // Save and restore GL state
    GLint prevFBO; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);

    mpv_render_context_render(s_mpvGL, render_params);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

// ============================================================================
// Frame stepping
// ============================================================================

void MediaPlayer_FrameStep() {
    if (!s_mpv) return;
    const char* cmd[] = { "frame-step", NULL };
    mpv_command(s_mpv, cmd);
    s_state = MP_PAUSED;
}

void MediaPlayer_FrameBackStep() {
    if (!s_mpv) return;
    const char* cmd[] = { "frame-back-step", NULL };
    mpv_command(s_mpv, cmd);
    s_state = MP_PAUSED;
}

// ============================================================================
// Zoom
// ============================================================================

void MediaPlayer_SetZoom(double scale) {
    if (!s_mpv) return;
    if (scale < 1.0) scale = 1.0;
    // mpv video-zoom is log2 scale: 0 = 1x, 1 = 2x, 2 = 4x
    double logZoom = (scale <= 1.0) ? 0.0 : log2(scale);
    mpv_set_property(s_mpv, "video-zoom", MPV_FORMAT_DOUBLE, &logZoom);
}

void MediaPlayer_SetZoomPos(double x, double y) {
    if (!s_mpv) return;
    mpv_set_property(s_mpv, "video-pan-x", MPV_FORMAT_DOUBLE, &x);
    mpv_set_property(s_mpv, "video-pan-y", MPV_FORMAT_DOUBLE, &y);
}

// ============================================================================
// A-B loop
// ============================================================================

void MediaPlayer_SetABLoopA() {
    if (!s_mpv) return;
    s_abLoopA = s_position;
    mpv_set_property(s_mpv, "ab-loop-a", MPV_FORMAT_DOUBLE, &s_abLoopA);
    s_abRepeatState = 1;
}

void MediaPlayer_SetABLoopB() {
    if (!s_mpv) return;
    s_abLoopB = s_position;
    mpv_set_property(s_mpv, "ab-loop-b", MPV_FORMAT_DOUBLE, &s_abLoopB);
    s_abRepeatState = 2;
}

void MediaPlayer_ClearABLoop() {
    if (!s_mpv) return;
    mpv_set_property_string(s_mpv, "ab-loop-a", "no");
    mpv_set_property_string(s_mpv, "ab-loop-b", "no");
    s_abLoopA = -1.0;
    s_abLoopB = -1.0;
    s_abRepeatState = 0;
}

int MediaPlayer_GetABRepeatState() { return s_abRepeatState; }

// ============================================================================
// Track info
// ============================================================================

int MediaPlayer_GetAudioChannels() { return s_audioChannels; }

int MediaPlayer_GetAudioFormat() {
    // Map mpv codec name to Xbox DVD audio format enum
    if (s_audioCodec[0] == '\0') return 3;  // default to stereo/PCM
    if (strcmp(s_audioCodec, "ac3") == 0 || strcmp(s_audioCodec, "eac3") == 0) return 0;  // Dolby Digital
    if (strcmp(s_audioCodec, "mp1") == 0) return 1;   // MPEG1
    if (strcmp(s_audioCodec, "mp2") == 0) return 2;   // MPEG2
    if (strcmp(s_audioCodec, "dts") == 0 || strcmp(s_audioCodec, "dca") == 0) return 4;   // DTS
    return 3;  // PCM/Stereo (aac, flac, vorbis, opus, pcm, etc.)
}

const char* MediaPlayer_GetAudioLanguage() {
    if (!s_mpv) return "";
    // Query current audio track language
    char* lang = nullptr;
    if (mpv_get_property(s_mpv, "current-tracks/audio/lang", MPV_FORMAT_STRING, &lang) >= 0 && lang) {
        snprintf(s_audioLang, sizeof(s_audioLang), "%s", lang);
        mpv_free(lang);
        return s_audioLang;
    }
    return "";
}

const char* MediaPlayer_GetSubtitleLanguage() {
    if (!s_mpv) return "";
    char* lang = nullptr;
    if (mpv_get_property(s_mpv, "current-tracks/sub/lang", MPV_FORMAT_STRING, &lang) >= 0 && lang) {
        snprintf(s_subLang, sizeof(s_subLang), "%s", lang);
        mpv_free(lang);
        return s_subLang;
    }
    return "";
}

int MediaPlayer_GetSubtitleTrack() { return s_subTrack; }

#else  // UIX_MEDIA_PLAYER not defined - stub implementations

#include "media_player.h"
#include <cstdio>

bool MediaPlayer_Init() {
    fprintf(stderr, "[MediaPlayer] Not available (libmpv not found at build time)\n");
    return false;
}
void MediaPlayer_Shutdown() {}
bool MediaPlayer_Open(const char*) { return false; }
void MediaPlayer_Play() {}
void MediaPlayer_Pause() {}
void MediaPlayer_TogglePause() {}
void MediaPlayer_Stop() {}
void MediaPlayer_Seek(double) {}
void MediaPlayer_SeekRelative(double) {}
void MediaPlayer_NextChapter() {}
void MediaPlayer_PrevChapter() {}
int  MediaPlayer_GetChapter() { return 0; }
int  MediaPlayer_GetChapterCount() { return 0; }
void MediaPlayer_NextAudioTrack() {}
void MediaPlayer_NextSubtitleTrack() {}
void MediaPlayer_SetSpeed(double) {}
double MediaPlayer_GetSpeed() { return 1.0; }
MediaPlayerState MediaPlayer_GetState() { return MP_IDLE; }
double MediaPlayer_GetPosition() { return 0.0; }
double MediaPlayer_GetDuration() { return 0.0; }
bool   MediaPlayer_HasVideo() { return false; }
unsigned int MediaPlayer_GetVideoTexture(int*, int*) { return 0; }
unsigned int MediaPlayer_GetFBO() { return 0; }
void MediaPlayer_RenderToScreen(int, int) {}
void MediaPlayer_Update() {}
void MediaPlayer_FrameStep() {}
void MediaPlayer_FrameBackStep() {}
void MediaPlayer_SetZoom(double) {}
void MediaPlayer_SetZoomPos(double, double) {}
void MediaPlayer_SetABLoopA() {}
void MediaPlayer_SetABLoopB() {}
void MediaPlayer_ClearABLoop() {}
int  MediaPlayer_GetABRepeatState() { return 0; }
int  MediaPlayer_GetAudioChannels() { return 0; }
int  MediaPlayer_GetAudioFormat() { return 3; }
const char* MediaPlayer_GetAudioLanguage() { return ""; }
const char* MediaPlayer_GetSubtitleLanguage() { return ""; }
int  MediaPlayer_GetSubtitleTrack() { return 0; }

#endif // UIX_MEDIA_PLAYER
