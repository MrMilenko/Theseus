// xcloud_audio.cpp: xCloud Opus audio to SDL. Each audio RTP frame carries one
// Opus packet (48 kHz stereo, matches the stereo=1 fmtp we offer). We decode to
// S16 and queue to our own SDL audio device, dropping frames if the queue backs
// up so the stream stays close to real time.
//
// Gated on WebRTC: no libdatachannel means no xCloud, so the opus dependency
// (and this whole file) drops out. Matches xcloud_client.cpp.

#if defined(THESEUS_HAVE_WEBRTC)

#include "xcloud_audio.h"

#include <SDL.h>
#include <opus.h>

#include <mutex>
#include <cstdio>

static std::mutex          s_mtx;
static OpusDecoder*        s_dec = nullptr;
static SDL_AudioDeviceID   s_dev = 0;

static const int kRate     = 48000;
static const int kChannels = 2;
static const int kMaxFrame = 5760;   // 120 ms at 48 kHz

void XcloudAudio_Start()
{
    std::lock_guard<std::mutex> l(s_mtx);
    if (s_dec) return;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) SDL_InitSubSystem(SDL_INIT_AUDIO);

    int err = 0;
    s_dec = opus_decoder_create(kRate, kChannels, &err);
    if (err != OPUS_OK || !s_dec) { s_dec = nullptr; fprintf(stderr, "[xCloudAudio] opus init failed\n"); return; }

    SDL_AudioSpec want{}, have{};
    want.freq     = kRate;
    want.format   = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples  = 960;             // 20 ms
    s_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!s_dev) { fprintf(stderr, "[xCloudAudio] SDL_OpenAudioDevice failed: %s\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(s_dev, 0);
    fprintf(stderr, "[xCloudAudio] audio device ready\n");
}

void XcloudAudio_Stop()
{
    std::lock_guard<std::mutex> l(s_mtx);
    if (s_dev) { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
    if (s_dec) { opus_decoder_destroy(s_dec); s_dec = nullptr; }
}

void XcloudAudio_PushFrame(const uint8_t* opus, size_t len)
{
    std::lock_guard<std::mutex> l(s_mtx);
    if (!s_dec || !s_dev || !opus || !len) return;

    static int16_t pcm[kMaxFrame * kChannels];
    int samples = opus_decode(s_dec, opus, (int)len, pcm, kMaxFrame, 0);
    if (samples <= 0) return;

    // Keep at most ~200 ms queued; if we are behind, drop this frame so audio
    // tracks the live stream instead of drifting further and further back.
    const Uint32 maxQueued = (Uint32)(kRate * kChannels * sizeof(int16_t)) / 5;
    if (SDL_GetQueuedAudioSize(s_dev) > maxQueued) return;

    SDL_QueueAudio(s_dev, pcm, (Uint32)(samples * kChannels * sizeof(int16_t)));
}

#endif // THESEUS_HAVE_WEBRTC
