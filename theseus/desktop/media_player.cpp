// media_player.cpp: libmpv wrapper. mpv renders video to a private GL
// FBO; we expose its color texture to media_ui.cpp for compositing.
//
// Each Play is a fresh mpv instance, each Stop is a full teardown.
// Partial-stop strategies leave the render pipeline wedged on Apple
// Silicon. s_fboWidth/Height tracks the actual FBO size, distinct from
// s_videoWidth/Height (mpv's reported video native dims) so the
// video-params event can't lie to EnsureFBO about FBO state.

#include "media_player.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <SDL.h>

// OpenGL 3.2 Core Profile -- match d3d8_sdl.h's per-platform pattern.
// Linux gl.h ships only the 1.x ABI; we need GL_GLEXT_PROTOTYPES + glext.h
// to pick up framebuffer / shader / VAO entry points. Windows mingw goes
// through GLEW because there's no equivalent prototype-on-demand mechanism.
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
static int    s_videoWidth = 0;    // mpv-reported video native width
static int    s_videoHeight = 0;   // mpv-reported video native height
static int    s_fboWidth = 0;      // size we created the render FBO at
static int    s_fboHeight = 0;
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
    if (s_fbo && s_fboWidth == w && s_fboHeight == h)
        return;

    // Delete old
    if (s_fbo) glDeleteFramebuffers(1, &s_fbo);
    if (s_videoTex) glDeleteTextures(1, &s_videoTex);

    s_fboWidth  = w;
    s_fboHeight = h;

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
    if (s_mpv) return true;  // already initialised
    s_mpv = mpv_create();
    if (!s_mpv) {
        fprintf(stderr, "[MediaPlayer] mpv_create() failed\n");
        return false;
    }

    // Set options before init
    extern bool g_hwdec;
    mpv_set_option_string(s_mpv, "vo", "libmpv");       // render to our FBO, not a window
    mpv_set_option_string(s_mpv, "hwdec", g_hwdec ? "auto" : "no");
    mpv_set_option_string(s_mpv, "keep-open", "yes");
    mpv_set_option_string(s_mpv, "video", "yes");
    mpv_set_option_string(s_mpv, "terminal", "no");
    mpv_set_option_string(s_mpv, "msg-level", "all=error");
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
    // Re-init if a previous Stop tore mpv down. This is the normal path
    // for any playback after the first one in the session.
    if (!s_mpv) {
        if (!MediaPlayer_Init()) return false;
    }

    const char* cmd[] = { "loadfile", path, NULL };
    mpv_command(s_mpv, cmd);

    s_state = MP_PLAYING;
    s_position = 0.0;
    s_duration = 0.0;
    s_hasVideo = false;
    s_videoWidth  = 0;
    s_videoHeight = 0;
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
    // Full teardown. Partial-stop strategies (mpv "stop", pause+mute) leave
    // the render context in a wedged state on some GL stacks. Open
    // re-inits lazily, so next playback gets a fresh pipeline.
    if (s_mpvGL) {
        mpv_render_context_free(s_mpvGL);
        s_mpvGL = nullptr;
    }
    if (s_mpv) {
        mpv_destroy(s_mpv);
        s_mpv = nullptr;
    }
    if (s_fbo)      { glDeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
    if (s_videoTex) { glDeleteTextures(1, &s_videoTex); s_videoTex = 0; }
    s_state       = MP_IDLE;
    s_position    = 0.0;
    s_duration    = 0.0;
    s_hasVideo    = false;
    s_videoWidth  = 0;
    s_videoHeight = 0;
    s_fboWidth    = 0;
    s_fboHeight   = 0;
    fprintf(stderr, "[MediaPlayer] Torn down\n");
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

    // Natural EOF on a prior tick; tear down so we stop pumping.
    if (s_state == MP_STOPPED) {
        MediaPlayer_Stop();
        return;
    }

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
            case MPV_EVENT_END_FILE: {
                // Only treat natural EOF as a true stop. STOP/QUIT/REDIRECT
                // get fired during user-driven transitions (loadfile to a
                // new file, etc.) and shouldn't trigger MediaUI's auto-exit.
                mpv_event_end_file* ef = (mpv_event_end_file*)event->data;
                if (ef && ef->reason == MPV_END_FILE_REASON_EOF) {
                    s_state = MP_STOPPED;
                }
                break;
            }
            case MPV_EVENT_FILE_LOADED:
                s_state = MP_PLAYING;
                break;
            default:
                break;
        }
    }

    // Render video frame whenever mpv has an update
    if (s_mpvGL && (mpv_render_context_update(s_mpvGL) & MPV_RENDER_UPDATE_FRAME)) {
        // Use the latest mpv-reported native dims; fall back to the previous
        // FBO's size if no video-params have arrived yet (which keeps mpv's
        // render pump fed and avoids "render not called or stuck" warnings).
        // EnsureFBO recreates the FBO if dims actually change, and
        // GetVideoTexture reports FBO dims so the blit src rect always
        // matches what's in the texture.
        int w = s_videoWidth  > 0 ? s_videoWidth  : (s_fboWidth  > 0 ? s_fboWidth  : 640);
        int h = s_videoHeight > 0 ? s_videoHeight : (s_fboHeight > 0 ? s_fboHeight : 480);

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
    // Return FBO dims (what the texture *actually contains*), not the
    // mpv-reported video native dims. If those drift apart (e.g. we
    // created the FBO before video-params arrived), reading past FBO
    // extent gives garbage in the blit.
    if (outWidth)  *outWidth  = s_fboWidth;
    if (outHeight) *outHeight = s_fboHeight;
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
