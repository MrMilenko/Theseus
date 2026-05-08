// audio_sdl.cpp: desktop audio backend on top of SDL_mixer. Counter-
// part to xbox/dsound_manager.cpp; loads / plays the dashboard's
// MP3, WAV, and XAP-script-driven audio cues. Intentionally does NOT
// include std.h, to avoid #define new conflicts with STL.

#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#endif

#include "audio_sdl.h"

// ---------------------------------------------------------------------------
// Xbox IMA ADPCM decoder (format tag 0x0069)
// Converts Xbox ADPCM WAV files to PCM16 Mix_Chunks at load time
// ---------------------------------------------------------------------------
#define XBOX_ADPCM_TAG 0x0069

// IMA step size table (standard)
static const int s_imaStepTable[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
    73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
    408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,
    1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
    7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
    22385,24623,27086,29794,32767
};

// IMA index adjustment table
static const int s_imaIndexTable[16] = {
    -1,-1,-1,-1, 2,4,6,8,
    -1,-1,-1,-1, 2,4,6,8
};

static inline int ClampIndex(int idx) {
    if (idx < 0) return 0;
    if (idx > 88) return 88;
    return idx;
}

static inline int16_t ClampSample(int s) {
    if (s < -32768) return -32768;
    if (s > 32767) return 32767;
    return (int16_t)s;
}

// Decode a single IMA ADPCM nibble
static inline int16_t DecodeNibble(int nibble, int* predictor, int* stepIndex)
{
    int step = s_imaStepTable[*stepIndex];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;

    *predictor = ClampSample(*predictor + diff);
    *stepIndex = ClampIndex(*stepIndex + s_imaIndexTable[nibble]);
    return (int16_t)*predictor;
}

// Try to load an Xbox ADPCM WAV, returns NULL if not Xbox ADPCM format
static Mix_Chunk* LoadXboxADPCM(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    // Read RIFF header
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr+8, "WAVE", 4) != 0) {
        fclose(f);
        return NULL;
    }

    // Parse chunks looking for 'fmt ' and 'data'
    uint16_t fmtTag = 0, channels = 0, bitsPerSample = 0, samplesPerBlock = 0;
    uint32_t sampleRate = 0, blockAlign = 0;
    uint8_t* adpcmData = NULL;
    uint32_t dataSize = 0;

    while (!feof(f)) {
        uint8_t chunkHdr[8];
        if (fread(chunkHdr, 1, 8, f) != 8) break;

        uint32_t chunkID = *(uint32_t*)chunkHdr;
        uint32_t chunkSize = chunkHdr[4] | (chunkHdr[5]<<8) | (chunkHdr[6]<<16) | (chunkHdr[7]<<24);

        if (chunkID == 0x20746D66) { // 'fmt '
            uint8_t fmt[64];
            uint32_t toRead = chunkSize < 64 ? chunkSize : 64;
            if (fread(fmt, 1, toRead, f) != toRead) break;
            if (chunkSize > toRead) fseek(f, chunkSize - toRead, SEEK_CUR);

            fmtTag       = fmt[0] | (fmt[1]<<8);
            channels     = fmt[2] | (fmt[3]<<8);
            sampleRate   = fmt[4] | (fmt[5]<<8) | (fmt[6]<<16) | (fmt[7]<<24);
            blockAlign   = fmt[12] | (fmt[13]<<8);
            bitsPerSample= fmt[14] | (fmt[15]<<8);

            // Extra data: samples per block
            if (toRead >= 20)
                samplesPerBlock = fmt[18] | (fmt[19]<<8);
        }
        else if (chunkID == 0x61746164) { // 'data'
            adpcmData = (uint8_t*)malloc(chunkSize);
            if (!adpcmData) { fclose(f); return NULL; }
            dataSize = (uint32_t)fread(adpcmData, 1, chunkSize, f);
        }
        else {
            fseek(f, chunkSize, SEEK_CUR);
        }
    }
    fclose(f);

    // Only handle Xbox ADPCM
    if (fmtTag != XBOX_ADPCM_TAG || !adpcmData || dataSize == 0 || channels == 0 || blockAlign == 0) {
        free(adpcmData);
        return NULL;
    }

    if (samplesPerBlock == 0)
        samplesPerBlock = (blockAlign - 4 * channels) * 2 / channels + 1;

    // Calculate output size
    uint32_t numBlocks = dataSize / blockAlign;
    uint32_t totalSamples = numBlocks * samplesPerBlock;
    // Allocate interleaved PCM: totalSamples frames * channels * 2 bytes
    uint32_t pcmSize = totalSamples * channels * sizeof(int16_t);

    int16_t* pcmBuf = (int16_t*)malloc(pcmSize);
    if (!pcmBuf) { free(adpcmData); return NULL; }
    memset(pcmBuf, 0, pcmSize);

    int16_t* out = pcmBuf;
    int16_t* outEnd = pcmBuf + totalSamples * channels;

    for (uint32_t b = 0; b < numBlocks; b++) {
        uint8_t* block = adpcmData + b * blockAlign;

        // Per-channel state from block preamble (4 bytes each)
        int predictor[8] = {};
        int stepIndex[8] = {};
        for (int ch = 0; ch < channels; ch++) {
            uint8_t* preamble = block + ch * 4;
            predictor[ch] = (int16_t)(preamble[0] | (preamble[1] << 8));
            stepIndex[ch] = ClampIndex(preamble[2]);
        }

        // Write initial predictor samples (1 frame)
        for (int ch = 0; ch < channels && out < outEnd; ch++)
            *out++ = (int16_t)predictor[ch];

        // Remaining samples to decode for this block
        int samplesRemaining = samplesPerBlock - 1; // predictor already written

        // Xbox layout: alternating groups of 4 bytes per channel (8 nibbles = 8 samples)
        uint8_t* nibbleData = block + 4 * channels;
        int bytesPerChannelGroup = 4;
        int groupSize = bytesPerChannelGroup * channels;
        int nibbleBytes = blockAlign - 4 * channels;
        int numGroups = nibbleBytes / groupSize;

        for (int g = 0; g < numGroups && samplesRemaining > 0; g++) {
            int16_t decoded[8][8]; // [channel][sample]
            for (int ch = 0; ch < channels; ch++) {
                uint8_t* src = nibbleData + g * groupSize + ch * bytesPerChannelGroup;
                for (int i = 0; i < 4; i++) {
                    uint8_t byte = src[i];
                    decoded[ch][i*2]   = DecodeNibble(byte & 0x0F, &predictor[ch], &stepIndex[ch]);
                    decoded[ch][i*2+1] = DecodeNibble((byte >> 4) & 0x0F, &predictor[ch], &stepIndex[ch]);
                }
            }
            // Interleave channels, respecting bounds
            int toWrite = (samplesRemaining < 8) ? samplesRemaining : 8;
            for (int s = 0; s < toWrite; s++) {
                for (int ch = 0; ch < channels && out < outEnd; ch++)
                    *out++ = decoded[ch][s];
            }
            samplesRemaining -= toWrite;
        }
    }
    free(adpcmData);

    // Build a Mix_Chunk with the PCM data
    // We need to convert to the audio device format
    SDL_AudioCVT cvt;
    int built = SDL_BuildAudioCVT(&cvt,
        AUDIO_S16LSB, channels, sampleRate,  // source format
        AUDIO_S16LSB, channels, 44100);       // target (our mixer format)

    uint8_t* finalBuf;
    uint32_t finalLen;

    if (built > 0) {
        // Conversion needed
        cvt.len = pcmSize;
        cvt.buf = (uint8_t*)malloc(pcmSize * cvt.len_mult);
        if (!cvt.buf) { free(pcmBuf); return NULL; }
        memcpy(cvt.buf, pcmBuf, pcmSize);
        free(pcmBuf);
        SDL_ConvertAudio(&cvt);
        finalBuf = cvt.buf;
        finalLen = cvt.len_cvt;
    } else {
        // No conversion needed (or error; use raw)
        finalBuf = (uint8_t*)pcmBuf;
        finalLen = pcmSize;
    }

    Mix_Chunk* chunk = (Mix_Chunk*)malloc(sizeof(Mix_Chunk));
    if (!chunk) { free(finalBuf); return NULL; }
    chunk->allocated = 1;
    chunk->abuf = finalBuf;
    chunk->alen = finalLen;
    chunk->volume = MIX_MAX_VOLUME;

    return chunk;
}

// ---------------------------------------------------------------------------
// Sound effect cache
// ---------------------------------------------------------------------------
#define MAX_CHANNELS 32
#define MAX_SOUNDS   128

static Mix_Chunk* s_sounds[MAX_SOUNDS] = {};
static int        s_soundCount = 0;
static bool       s_initialized = false;

// Per-channel "finished" flags, set by callback, polled by Advance
static int s_channelFinished[MAX_CHANNELS] = {};

static void ChannelFinishedCallback(int channel)
{
    if (channel >= 0 && channel < MAX_CHANNELS)
        s_channelFinished[channel] = 1;
}

// ---------------------------------------------------------------------------
// PCM ring buffer for audio visualizer
// Mix_SetPostMix callback captures the final mixed stereo int16 stream
// ---------------------------------------------------------------------------
#define PCM_RING_SAMPLES 512   // stereo samples in ring buffer
static int16_t s_pcmRing[PCM_RING_SAMPLES * 2] = {};  // interleaved L,R
static int     s_pcmWritePos = 0;

static void PostMixCallback(void* /*udata*/, Uint8* stream, int len)
{
    // stream is signed 16-bit stereo (4 bytes per sample frame)
    int16_t* samples = (int16_t*)stream;
    int frames = len / 4;  // 2 channels * 2 bytes
    for (int i = 0; i < frames; i++) {
        int pos = (s_pcmWritePos + i) % PCM_RING_SAMPLES;
        s_pcmRing[pos * 2]     = samples[i * 2];
        s_pcmRing[pos * 2 + 1] = samples[i * 2 + 1];
    }
    s_pcmWritePos = (s_pcmWritePos + frames) % PCM_RING_SAMPLES;
}

void DashAudio_GetPCMSamples(int16_t* outLeft, int16_t* outRight, int count)
{
    if (count > PCM_RING_SAMPLES) count = PCM_RING_SAMPLES;
    // Read the most recent 'count' samples from ring buffer
    int start = (s_pcmWritePos - count + PCM_RING_SAMPLES) % PCM_RING_SAMPLES;
    for (int i = 0; i < count; i++) {
        int pos = (start + i) % PCM_RING_SAMPLES;
        outLeft[i]  = s_pcmRing[pos * 2];
        outRight[i] = s_pcmRing[pos * 2 + 1];
    }
}

int DashAudio_GetPCMSampleCount(void)
{
    return PCM_RING_SAMPLES;
}

// ---------------------------------------------------------------------------
// Streaming music
// ---------------------------------------------------------------------------
static Mix_Music* s_music = NULL;

// SDL_GetTicks-based playback time tracking (fallback for Mix_GetMusicPosition)
static uint32_t s_musicStartTicks = 0;  // SDL_GetTicks when playback started
static double s_musicPausePos   = 0.0; // accumulated position when paused
static bool   s_musicPaused     = false;

// ---------------------------------------------------------------------------
// Music collection database
// ---------------------------------------------------------------------------
struct Song {
    std::string name;     // display name (filename without extension)
    std::string path;     // full filesystem path
    int         duration; // seconds, 0 if unknown
};

struct Soundtrack {
    int                 id;
    std::string         name;
    std::vector<Song>   songs;
};

static std::vector<Soundtrack> s_soundtracks;
static char s_fmtBuf[32]; // for FormatTime

// Helper: check if a filename has an audio extension
static bool IsAudioFile(const char* name)
{
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    dot++;

    // Case-insensitive compare
    char ext[8] = {};
    for (int i = 0; i < 7 && dot[i]; i++)
        ext[i] = (char)tolower((unsigned char)dot[i]);

    return strcmp(ext, "wav") == 0 ||
           strcmp(ext, "mp3") == 0 ||
           strcmp(ext, "ogg") == 0 ||
           strcmp(ext, "flac") == 0 ||
           strcmp(ext, "wma") == 0 ||
           strcmp(ext, "m4a") == 0 ||
           strcmp(ext, "aac") == 0;
}

// Helper: strip file extension from name
static std::string StripExtension(const char* name)
{
    std::string s(name);
    size_t dot = s.rfind('.');
    if (dot != std::string::npos)
        s.erase(dot);
    return s;
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------
int DashAudio_Init(void)
{
    if (s_initialized) return 0;

    // 4096-sample buffer (~93ms at 44.1kHz). Higher than ideal for game
    // audio latency, but the dashboard isn't latency-sensitive, and the
    // bigger buffer keeps audio stable when the scan thread / libmpv decode
    // / TMDB curl bursts hog CPU. Smaller buffer = popping under load.
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        fprintf(stderr, "[Audio] Mix_OpenAudio failed: %s\n", Mix_GetError());
        return -1;
    }

    // Enable MP3, OGG, FLAC decoding
    int flags = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC;
    int initted = Mix_Init(flags);
    if ((initted & flags) != flags) {
        fprintf(stderr, "[Audio] Mix_Init partial: requested 0x%x, got 0x%x (%s)\n",
                flags, initted, Mix_GetError());
        // Continue anyway; WAV always works
    }

    Mix_AllocateChannels(MAX_CHANNELS);
    Mix_ChannelFinished(ChannelFinishedCallback);
    Mix_SetPostMix(PostMixCallback, NULL);

    memset(s_channelFinished, 0, sizeof(s_channelFinished));
    memset(s_sounds, 0, sizeof(s_sounds));
    s_soundCount = 0;
    s_initialized = true;

    // Honor --muted before XAP initialize() fires Audio nodes during InitApp.
    extern bool g_audioMuted;
    if (g_audioMuted) DashAudio_MuteAll();


    // Auto-scan music collection. Root is configurable via desktop.ini
    // [Library] MusicRoot=...; falls back to Data/Music for legacy
    // installs. Declared in audio_sdl.h.
    DashMusic_Scan(DashMusic_GetConfiguredRoot());

    return 0;
}

// Read the configured music root from desktop.ini's g_musicRoot global
// (loaded by sdl_main.cpp::LoadDesktopSettings). Falls back to the
// legacy Data/Music path if the user hasn't configured one.
extern "C" char g_musicRoot[512];

const char* DashMusic_GetConfiguredRoot(void)
{
    if (g_musicRoot[0]) return g_musicRoot;
    return "Data/Music";
}

static bool s_muted = false;

void DashAudio_MuteAll(void)
{
    if (!s_initialized) return;
    s_muted = true;
    Mix_Volume(-1, 0);    // Set all channels to volume 0
    Mix_VolumeMusic(0);   // Set music to volume 0
}

void DashAudio_UnmuteAll(void)
{
    if (!s_initialized) return;
    s_muted = false;
    Mix_Volume(-1, MIX_MAX_VOLUME);   // Restore all channels
    Mix_VolumeMusic(MIX_MAX_VOLUME);  // Restore music
}

void DashAudio_Shutdown(void)
{
    if (!s_initialized) return;

    // Free all loaded sounds
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (s_sounds[i]) {
            Mix_FreeChunk(s_sounds[i]);
            s_sounds[i] = NULL;
        }
    }
    s_soundCount = 0;

    // Free music
    DashAudio_FreeMusic();

    s_soundtracks.clear();

    Mix_CloseAudio();
    Mix_Quit();

    s_initialized = false;
    fprintf(stderr, "[Audio] SDL_mixer shutdown\n");
}

// ---------------------------------------------------------------------------
// Sound effects
// ---------------------------------------------------------------------------
int DashAudio_LoadSound(const char* path)
{
    if (!s_initialized || !path) return -1;

    // Find a free slot
    int slot = -1;
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (!s_sounds[i]) { slot = i; break; }
    }
    if (slot < 0) {
        fprintf(stderr, "[Audio] Sound cache full (%d)\n", MAX_SOUNDS);
        return -1;
    }

    // Try Xbox ADPCM decoder first, then fall back to SDL_mixer
    Mix_Chunk* chunk = LoadXboxADPCM(path);
    if (!chunk) {
        chunk = Mix_LoadWAV(path);
        if (!chunk) {
            fprintf(stderr, "[Audio] Failed to load '%s': %s\n", path, Mix_GetError());
            return -1;
        }
    }

    s_sounds[slot] = chunk;
    s_soundCount++;
    return slot;
}

void DashAudio_FreeSound(int handle)
{
    if (handle >= 0 && handle < MAX_SOUNDS && s_sounds[handle]) {
        Mix_FreeChunk(s_sounds[handle]);
        s_sounds[handle] = NULL;
        s_soundCount--;
    }
}

int DashAudio_PlaySound(int handle, int loops, int fadeInMs)
{
    if (handle < 0 || handle >= MAX_SOUNDS || !s_sounds[handle]) return -1;

    int ch;
    if (fadeInMs > 0)
        ch = Mix_FadeInChannel(-1, s_sounds[handle], loops, fadeInMs);
    else
        ch = Mix_PlayChannel(-1, s_sounds[handle], loops);

    if (ch < 0) {
        fprintf(stderr, "[Audio] PlaySound failed: %s\n", Mix_GetError());
    }
    // SDL_mixer's ChannelFinishedCallback latches s_channelFinished[ch] = 1 on
    // any halt or natural end. When the channel gets reused, that stale flag
    // lies to DashAudio_DidChannelFinish, which causes Advance() to immediately
    // mark the new playback finished. Clear it on every fresh play.
    if (ch >= 0) {
        s_channelFinished[ch] = 0;
        if (s_muted) Mix_Volume(ch, 0);
    }
    return ch;
}

void DashAudio_StopChannel(int channel)
{
    if (channel >= 0) Mix_HaltChannel(channel);
}

void DashAudio_PauseChannel(int channel)
{
    if (channel >= 0) Mix_Pause(channel);
}

void DashAudio_ResumeChannel(int channel)
{
    if (channel >= 0) Mix_Resume(channel);
}

int DashAudio_IsChannelPlaying(int channel)
{
    if (channel < 0) return 0;
    return Mix_Playing(channel) && !Mix_Paused(channel);
}

void DashAudio_SetChannelVolume(int channel, float vol)
{
    if (channel < 0) return;
    if (s_muted) return; // Block volume changes while muted
    int v = (int)(vol * MIX_MAX_VOLUME);
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    Mix_Volume(channel, v);
}

void DashAudio_SetChannelPan(int channel, float pan)
{
    if (channel < 0) return;
    // SDL_mixer panning: 0-255 for left, 0-255 for right
    // pan=-1 → full left, pan=0 → center, pan=1 → full right
    int left  = (int)(255.0f * (1.0f - pan) / 2.0f);
    int right = (int)(255.0f * (1.0f + pan) / 2.0f);
    if (left < 0) left = 0; if (left > 255) left = 255;
    if (right < 0) right = 0; if (right > 255) right = 255;
    Mix_SetPanning(channel, (Uint8)left, (Uint8)right);
}

void DashAudio_FadeOutChannel(int channel, int fadeMs)
{
    if (channel >= 0) Mix_FadeOutChannel(channel, fadeMs);
}

int DashAudio_DidChannelFinish(int channel)
{
    if (channel < 0 || channel >= MAX_CHANNELS) return 0;
    int val = s_channelFinished[channel];
    s_channelFinished[channel] = 0;
    return val;
}

// ---------------------------------------------------------------------------
// Streaming music
// ---------------------------------------------------------------------------
int DashAudio_LoadMusic(const char* path)
{
    if (!s_initialized || !path) return -1;

    DashAudio_FreeMusic();
    s_music = Mix_LoadMUS(path);
    if (!s_music) {
        fprintf(stderr, "[Audio] Failed to load music '%s': %s\n", path, Mix_GetError());
        return -1;
    }
    return 0;
}

void DashAudio_FreeMusic(void)
{
    if (s_music) {
        Mix_HaltMusic();
        Mix_FreeMusic(s_music);
        s_music = NULL;
    }
    s_musicStartTicks = 0;
    s_musicPausePos = 0.0;
    s_musicPaused = false;
}

void DashAudio_PlayMusic(int loops, int fadeInMs)
{
    if (!s_music) return;
    if (fadeInMs > 0)
        Mix_FadeInMusic(s_music, loops, fadeInMs);
    else
        Mix_PlayMusic(s_music, loops);
    s_musicStartTicks = SDL_GetTicks();
    s_musicPausePos = 0.0;
    s_musicPaused = false;
}

void DashAudio_StopMusic(int fadeOutMs)
{
    if (fadeOutMs > 0)
        Mix_FadeOutMusic(fadeOutMs);
    else
        Mix_HaltMusic();
    s_musicStartTicks = 0;
    s_musicPausePos = 0.0;
    s_musicPaused = false;
}

void DashAudio_PauseMusic(void)
{
    Mix_PauseMusic();
    // Save accumulated position at pause time
    if (!s_musicPaused && s_musicStartTicks > 0) {
        s_musicPausePos += (SDL_GetTicks() - s_musicStartTicks) / 1000.0;
    }
    s_musicPaused = true;
}

void DashAudio_ResumeMusic(void)
{
    Mix_ResumeMusic();
    s_musicStartTicks = SDL_GetTicks();
    s_musicPaused = false;
}

int DashAudio_IsMusicPlaying(void)
{
    return Mix_PlayingMusic() && !Mix_PausedMusic();
}

void DashAudio_SetMusicVolume(float vol)
{
    if (s_muted) return; // Block volume changes while muted
    int v = (int)(vol * MIX_MAX_VOLUME);
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    Mix_VolumeMusic(v);
}

double DashAudio_GetMusicPosition(void)
{
    if (!s_music) return 0.0;

    // Try SDL_mixer 2.6+ API first
    double pos = Mix_GetMusicPosition(s_music);
    if (pos >= 0.0) return pos;

    // Fallback: tick-based tracking
    if (s_musicPaused)
        return s_musicPausePos;
    if (s_musicStartTicks > 0)
        return s_musicPausePos + (SDL_GetTicks() - s_musicStartTicks) / 1000.0;
    return 0.0;
}

double DashAudio_GetMusicDuration(void)
{
    if (!s_music) return 0.0;
    double d = Mix_MusicDuration(s_music);
    return (d < 0.0) ? 0.0 : d;
}

// ---------------------------------------------------------------------------
// Music collection scanner
// ---------------------------------------------------------------------------
int DashMusic_Scan(const char* musicRoot)
{
    s_soundtracks.clear();

    if (!musicRoot) return 0;

    // Process a single soundtrack directory
    int nextID = 1;
    auto ProcessSoundtrackDir = [&](const char* dirName) {
        if (dirName[0] == '.') return;
        std::string stPath = std::string(musicRoot) + "/" + dirName;
        struct stat st;
        if (stat(stPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return;

        Soundtrack soundtrack;
        soundtrack.id = nextID++;
        soundtrack.name = dirName;

        // Scan for songs within this soundtrack directory
        auto ProcessSongFile = [&](const char* songName) {
            if (songName[0] == '.') return;
            if (!IsAudioFile(songName)) return;
            Song song;
            song.name = StripExtension(songName);
            song.path = stPath + "/" + songName;
            song.duration = 0;
            Mix_Music* probe = Mix_LoadMUS(song.path.c_str());
            if (probe) {
                double d = Mix_MusicDuration(probe);
                if (d > 0.0) song.duration = (int)(d + 0.5);
                Mix_FreeMusic(probe);
            }
            soundtrack.songs.push_back(song);
        };

#ifdef _WIN32
        char searchBuf[512];
        snprintf(searchBuf, sizeof(searchBuf), "%s\\*", stPath.c_str());
        struct _finddata_t fd;
        intptr_t hFind = _findfirst(searchBuf, &fd);
        if (hFind != -1) {
            do { ProcessSongFile(fd.name); } while (_findnext(hFind, &fd) == 0);
            _findclose(hFind);
        }
#else
        DIR* stDir = opendir(stPath.c_str());
        if (stDir) {
            struct dirent* songEntry;
            while ((songEntry = readdir(stDir)) != NULL) ProcessSongFile(songEntry->d_name);
            closedir(stDir);
        }
#endif

        std::sort(soundtrack.songs.begin(), soundtrack.songs.end(),
            [](const Song& a, const Song& b) { return a.name < b.name; });
        if (!soundtrack.songs.empty()) {
            s_soundtracks.push_back(soundtrack);
        }
    };

#ifdef _WIN32
    char searchBuf[512];
    snprintf(searchBuf, sizeof(searchBuf), "%s\\*", musicRoot);
    struct _finddata_t fd;
    intptr_t hFind = _findfirst(searchBuf, &fd);
    if (hFind == -1) {
        fprintf(stderr, "[Audio] Music directory '%s' not found (this is OK if no music installed)\n", musicRoot);
        return 0;
    }
    do { ProcessSoundtrackDir(fd.name); } while (_findnext(hFind, &fd) == 0);
    _findclose(hFind);
#else
    DIR* rootDir = opendir(musicRoot);
    if (!rootDir) {
        fprintf(stderr, "[Audio] Music directory '%s' not found (this is OK if no music installed)\n", musicRoot);
        return 0;
    }
    struct dirent* entry;
    while ((entry = readdir(rootDir)) != NULL) ProcessSoundtrackDir(entry->d_name);
    closedir(rootDir);
#endif

    // Sort soundtracks alphabetically
    std::sort(s_soundtracks.begin(), s_soundtracks.end(),
        [](const Soundtrack& a, const Soundtrack& b) { return a.name < b.name; });

    if (s_soundtracks.size() > 0)
        fprintf(stderr, "[Audio] %zu soundtracks loaded\n", s_soundtracks.size());
    return (int)s_soundtracks.size();
}

int DashMusic_GetSoundtrackCount(void) { return (int)s_soundtracks.size(); }

int DashMusic_GetSoundtrackID(int index)
{
    if (index < 0 || index >= (int)s_soundtracks.size()) return -1;
    return s_soundtracks[index].id;
}

int DashMusic_GetSoundtrackIndexFromID(int id)
{
    for (int i = 0; i < (int)s_soundtracks.size(); i++)
        if (s_soundtracks[i].id == id) return i;
    return -1;
}

const char* DashMusic_GetSoundtrackName(int stIndex)
{
    if (stIndex < 0 || stIndex >= (int)s_soundtracks.size()) return "";
    return s_soundtracks[stIndex].name.c_str();
}

int DashMusic_GetSongCount(int stIndex)
{
    if (stIndex < 0 || stIndex >= (int)s_soundtracks.size()) return 0;
    return (int)s_soundtracks[stIndex].songs.size();
}

int DashMusic_GetSongID(int stIndex, int songIndex)
{
    if (stIndex < 0 || stIndex >= (int)s_soundtracks.size()) return -1;
    if (songIndex < 0 || songIndex >= (int)s_soundtracks[stIndex].songs.size()) return -1;
    // Simple ID scheme: soundtrack_id * 1000 + song_index
    return s_soundtracks[stIndex].id * 1000 + songIndex;
}

const char* DashMusic_GetSongName(int stIndex, int songIndex)
{
    if (stIndex < 0 || stIndex >= (int)s_soundtracks.size()) return "";
    auto& songs = s_soundtracks[stIndex].songs;
    if (songIndex < 0 || songIndex >= (int)songs.size()) return "";
    return songs[songIndex].name.c_str();
}

const char* DashMusic_GetSongPath(int stIndex, int songIndex)
{
    if (stIndex < 0 || stIndex >= (int)s_soundtracks.size()) return "";
    auto& songs = s_soundtracks[stIndex].songs;
    if (songIndex < 0 || songIndex >= (int)songs.size()) return "";
    return songs[songIndex].path.c_str();
}

int DashMusic_GetSongDuration(int stIndex, int songIndex)
{
    if (stIndex < 0 || stIndex >= (int)s_soundtracks.size()) return 0;
    auto& songs = s_soundtracks[stIndex].songs;
    if (songIndex < 0 || songIndex >= (int)songs.size()) return 0;
    return songs[songIndex].duration;
}

const char* DashMusic_GetSongPathByID(int songID)
{
    // Song ID format: soundtrack_id * 1000 + song_index
    int stID = songID / 1000;
    int songIdx = songID % 1000;
    int stIndex = DashMusic_GetSoundtrackIndexFromID(stID);
    if (stIndex < 0) return NULL;
    const char* p = DashMusic_GetSongPath(stIndex, songIdx);
    return (p && *p) ? p : NULL;
}

const char* DashMusic_FormatTime(int totalSeconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    int m = totalSeconds / 60;
    int s = totalSeconds % 60;
    snprintf(s_fmtBuf, sizeof(s_fmtBuf), "%d:%02d", m, s);
    return s_fmtBuf;
}
