// media_player.cpp: libmpv wrapper. mpv software-renders each frame into a
// CPU buffer that we upload to a bgfx texture; media_ui.cpp composites it.
//
// Each Play is a fresh mpv instance, each Stop is a full teardown. Partial-
// stop strategies leave the render pipeline wedged on Apple Silicon.
// s_fboWidth/Height tracks the render-target size, distinct from
// s_videoWidth/Height (mpv's reported native dims), so a video-params event
// can't lie about the current buffer size.

#include "media_player.h"
#include "log_redact.h"

#include <mpv/client.h>
#include <mpv/render.h>

#include <SDL.h>

#include <bgfx/bgfx.h>
#include "d3d8_sdl.h"  // for g_bgfxProgBlit / g_bgfxSamplerBlit

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
static bgfx::TextureHandle s_bgfxTex = BGFX_INVALID_HANDLE;
static uint8_t*            s_swBuf   = nullptr;  // libmpv SW render target
static size_t              s_swBufSize = 0;
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
// Render-target helpers
// ============================================================================

static void EnsureRenderTarget(int w, int h) {
    if (s_fboWidth == w && s_fboHeight == h) {
        if (bgfx::isValid(s_bgfxTex) && s_swBuf) return;
    }

    if (bgfx::isValid(s_bgfxTex)) { bgfx::destroy(s_bgfxTex); s_bgfxTex = BGFX_INVALID_HANDLE; }
    if (s_swBuf) { std::free(s_swBuf); s_swBuf = nullptr; s_swBufSize = 0; }

    s_fboWidth  = w;
    s_fboHeight = h;

    s_swBufSize = (size_t)w * (size_t)h * 4;
    s_swBuf = (uint8_t*)std::calloc(1, s_swBufSize);
    s_bgfxTex = bgfx::createTexture2D(
        (uint16_t)w, (uint16_t)h, false, 1,
        bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    fprintf(stderr, "[MediaPlayer] Created bgfx video tex: %dx%d\n", w, h);
}

// mpv render callback: called when a new frame is available
static void OnMpvRenderUpdate(void* ctx) {
    s_needsRender = true;
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
    mpv_set_option_string(s_mpv, "vo", "libmpv");       // render into our buffer, not a window
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

    // Software render: mpv decodes into a CPU buffer we supply per-frame,
    // we upload to a bgfx texture. No GL context needed.
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW },
        { MPV_RENDER_PARAM_INVALID,  nullptr }
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
    mpv_observe_property(s_mpv, 0, "video-params/w", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "video-params/h", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "audio-params/channel-count", MPV_FORMAT_INT64);
    mpv_observe_property(s_mpv, 0, "audio-codec-name", MPV_FORMAT_STRING);
    mpv_observe_property(s_mpv, 0, "sid", MPV_FORMAT_INT64);

    s_state = MP_IDLE;
    fprintf(stderr, "[MediaPlayer] Initialized (libmpv software render + bgfx upload)\n");
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
    if (bgfx::isValid(s_bgfxTex)) { bgfx::destroy(s_bgfxTex); s_bgfxTex = BGFX_INVALID_HANDLE; }
    if (s_swBuf) { std::free(s_swBuf); s_swBuf = nullptr; s_swBufSize = 0; }
    s_state = MP_IDLE;
    fprintf(stderr, "[MediaPlayer] Shutdown\n");
}

// ============================================================================
// Transport
// ============================================================================

bool MediaPlayer_Open(const char* path) {
    return MediaPlayer_OpenAt(path, 0.0);
}

bool MediaPlayer_OpenAt(const char* path, double startSeconds) {
    // Re-init if a previous Stop tore mpv down. This is the normal path
    // for any playback after the first one in the session.
    if (!s_mpv) {
        if (!MediaPlayer_Init()) return false;
    }

    // start is a global option, so it's put back once the file loads.
    if (startSeconds > 0.0) {
        char start[32];
        snprintf(start, sizeof(start), "%.0f", startSeconds);
        mpv_set_property_string(s_mpv, "start", start);
    }
    const char* cmd[] = { "loadfile", path, NULL };
    mpv_command(s_mpv, cmd);

    // Apply master volume to the new playback session. mpv's volume property
    // takes 0-100, our slider is 0.0-1.0.
    extern float g_masterVolume;
    double mpvVol = (double)(g_masterVolume * 100.0f);
    mpv_set_property(s_mpv, "volume", MPV_FORMAT_DOUBLE, &mpvVol);

    s_state = MP_PLAYING;
    s_position = 0.0;
    s_duration = 0.0;
    s_hasVideo = false;
    s_videoWidth  = 0;
    s_videoHeight = 0;
    fprintf(stderr, "[MediaPlayer] Opening: %s\n", Log_RedactSecrets(path).c_str());
    return true;
}

void MediaPlayer_SetMasterVolume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    if (!s_mpv) return;
    double mpvVol = (double)(vol * 100.0f);
    mpv_set_property(s_mpv, "volume", MPV_FORMAT_DOUBLE, &mpvVol);
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
    if (bgfx::isValid(s_bgfxTex)) { bgfx::destroy(s_bgfxTex); s_bgfxTex = BGFX_INVALID_HANDLE; }
    if (s_swBuf) { std::free(s_swBuf); s_swBuf = nullptr; s_swBufSize = 0; }
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

int MediaPlayer_GetTracks(MediaTrack* out, int maxCount) {
    if (!s_mpv || !out || maxCount <= 0) return 0;
    mpv_node node;
    if (mpv_get_property(s_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0) return 0;
    int count = 0;
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        mpv_node_list* list = node.u.list;
        for (int i = 0; i < list->num && count < maxCount; i++) {
            mpv_node* item = &list->values[i];
            if (item->format != MPV_FORMAT_NODE_MAP) continue;
            mpv_node_list* m = item->u.list;
            MediaTrack t = {};
            t.type = -1;
            for (int j = 0; j < m->num; j++) {
                const char* k = m->keys[j];
                mpv_node* v = &m->values[j];
                if (!strcmp(k, "id") && v->format == MPV_FORMAT_INT64)
                    t.id = (int)v->u.int64;
                else if (!strcmp(k, "type") && v->format == MPV_FORMAT_STRING) {
                    if (!strcmp(v->u.string, "audio")) t.type = 0;
                    else if (!strcmp(v->u.string, "sub")) t.type = 1;
                }
                else if (!strcmp(k, "title") && v->format == MPV_FORMAT_STRING)
                    strncpy(t.title, v->u.string, sizeof(t.title) - 1);
                else if (!strcmp(k, "lang") && v->format == MPV_FORMAT_STRING)
                    strncpy(t.lang, v->u.string, sizeof(t.lang) - 1);
                else if (!strcmp(k, "selected") && v->format == MPV_FORMAT_FLAG)
                    t.selected = v->u.flag != 0;
                else if (!strcmp(k, "external") && v->format == MPV_FORMAT_FLAG)
                    t.external = v->u.flag != 0;
            }
            if (t.type >= 0) out[count++] = t;
        }
    }
    mpv_free_node_contents(&node);
    return count;
}

static void SetTrackProp(const char* prop, int id) {
    if (!s_mpv) return;
    char buf[16];
    if (id <= 0) snprintf(buf, sizeof(buf), "no");
    else         snprintf(buf, sizeof(buf), "%d", id);
    mpv_set_property_string(s_mpv, prop, buf);
}

void MediaPlayer_SetAudioTrack(int id)    { SetTrackProp("aid", id); }
void MediaPlayer_SetSubtitleTrack(int id) { SetTrackProp("sid", id); }

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
                // A stream that won't open would otherwise sit on a black
                // screen forever.
                if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                    fprintf(stderr, "[MediaPlayer] couldn't play it: %s\n", mpv_error_string(ef->error));
                    s_state = MP_STOPPED;
                }
                break;
            }
            case MPV_EVENT_FILE_LOADED:
                s_state = MP_PLAYING;
                mpv_set_property_string(s_mpv, "start", "none");
                break;
            default:
                break;
        }
    }

    // Render video frame whenever mpv has an update
    if (s_mpvGL && (mpv_render_context_update(s_mpvGL) & MPV_RENDER_UPDATE_FRAME)) {
        // Use the latest mpv-reported native dims; fall back to the previous
        // render-target's size if no video-params have arrived yet (which
        // keeps mpv's render pump fed and avoids "render not called or
        // stuck" warnings). EnsureRenderTarget recreates if dims change.
        int w = s_videoWidth  > 0 ? s_videoWidth  : (s_fboWidth  > 0 ? s_fboWidth  : 640);
        int h = s_videoHeight > 0 ? s_videoHeight : (s_fboHeight > 0 ? s_fboHeight : 480);

        EnsureRenderTarget(w, h);
        s_hasVideo = true;

        // Software render path: ask mpv to write directly into our CPU
        // buffer, then upload to the bgfx texture for sampling by
        // MediaPlayer_RenderToScreen / consumer fullscreen blits.
        if (s_swBuf && s_swBufSize >= (size_t)w * (size_t)h * 4) {
            int swSize[2] = { w, h };
            char swFmt[]  = "bgra";
            size_t swStride = (size_t)w * 4;
            mpv_render_param render_params[] = {
                { MPV_RENDER_PARAM_SW_SIZE,    swSize },
                { MPV_RENDER_PARAM_SW_FORMAT,  swFmt },
                { MPV_RENDER_PARAM_SW_STRIDE,  &swStride },
                { MPV_RENDER_PARAM_SW_POINTER, s_swBuf },
                { MPV_RENDER_PARAM_INVALID,    nullptr }
            };
            if (mpv_render_context_render(s_mpvGL, render_params) == 0 &&
                bgfx::isValid(s_bgfxTex)) {
                bgfx::updateTexture2D(s_bgfxTex, 0, 0, 0, 0,
                    (uint16_t)w, (uint16_t)h,
                    bgfx::copy(s_swBuf, (uint32_t)s_swBufSize));
            }
        }
    }
}

unsigned int MediaPlayer_GetVideoTexture(int* outWidth, int* outHeight) {
    // Returns the bgfx texture handle, not a GL handle. Fullscreen video
    // display goes through MediaPlayer_RenderToScreen.
    if (outWidth)  *outWidth  = s_fboWidth;
    if (outHeight) *outHeight = s_fboHeight;
    return s_hasVideo && bgfx::isValid(s_bgfxTex) ? (unsigned int)s_bgfxTex.idx : 0;
}

void MediaPlayer_RenderToScreen(int screenW, int screenH) {
    // Nothing to submit here: MediaPlayer_GetVideoTexture hands the bgfx
    // texture to media_ui.cpp, which draws it via ImGui::AddImage.
    (void)screenW; (void)screenH;
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
