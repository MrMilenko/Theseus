// cdaudio.cpp: Audio CD detection, TOC reading, and playback.
//
// TOC detection: platform ioctls, no extra deps.
//   Linux  : <linux/cdrom.h>  CDROMREADTOCHDR / CDROMREADTOCENTRY
//   Windows: <ntddcdrm.h>     DeviceIoControl  IOCTL_CDROM_READ_TOC
//
// Playback backends (first match wins):
//   UIX_CDAUDIO           : libmpv audio-only handle
//   Linux or Windows      : CDROMREADAUDIO / IOCTL_CDROM_RAW_READ + SDL audio
//   Anything else         : stubs (detection still works)
//
// The SDL ring-buffer backend is shared between Linux and Windows;
// only the reader-thread ioctl differs.

#include "cdaudio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <climits>
#define MAX_CDTRACKS 99

// ============================================================================
// Platform: disc detection + TOC + track LBAs
// ============================================================================

#if defined(__linux__)

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>

static const char* s_devCandidates[] = {
    "/dev/cdrom", "/dev/sr0", "/dev/sr1", "/dev/dvd", nullptr
};
static int s_trackStartLBA[MAX_CDTRACKS] = {};
static int s_leadOutLBA = 0;

static int OpenDevice()
{
    for (const char** d = s_devCandidates; *d; ++d) {
        int fd = open(*d, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) return fd;
    }
    return -1;
}

static CdDiscType PlatformReadTOC(int* outCount, int* outDurations, int maxTracks)
{
    if (outCount) *outCount = 0;
    int fd = OpenDevice();
    if (fd < 0) return CD_NONE;

    if (ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT) != CDS_DISC_OK) { close(fd); return CD_NONE; }

    struct cdrom_tochdr hdr;
    if (ioctl(fd, CDROMREADTOCHDR, &hdr) < 0) { close(fd); return CD_NONE; }

    int first = hdr.cdth_trk0, last = hdr.cdth_trk1;
    if (outCount) *outCount = last - first + 1;

    bool allAudio = true;
    int  prevSec  = 0;
    bool gotPrev  = false;

    for (int t = first; t <= last + 1; ++t) {
        struct cdrom_tocentry e;
        memset(&e, 0, sizeof(e));
        e.cdte_track  = (t <= last) ? t : CDROM_LEADOUT;
        e.cdte_format = CDROM_MSF;
        if (ioctl(fd, CDROMREADTOCENTRY, &e) < 0) break;

        int sec = e.cdte_addr.msf.minute * 60 + e.cdte_addr.msf.second;
        int lba = ((int)e.cdte_addr.msf.minute * 60 + e.cdte_addr.msf.second) * 75
                  + e.cdte_addr.msf.frame - 150;
        if (lba < 0) lba = 0;

        if (gotPrev && outDurations) {
            int idx = (t - 1) - first;
            if (idx >= 0 && idx < maxTracks)
                outDurations[idx] = (sec > prevSec) ? (sec - prevSec) : 0;
        }
        if (t <= last) {
            if (e.cdte_ctrl & CDROM_DATA_TRACK) allAudio = false;
            s_trackStartLBA[t - first] = lba;
        } else {
            s_leadOutLBA = lba;
        }
        prevSec = sec;
        gotPrev = true;
    }
    close(fd);
    return allAudio ? CD_AUDIO : CD_DATA;
}

static void PlatformPopulatePlaybackInfo() {}  // already done in PlatformReadTOC

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>

// RAW_READ_INFO / IOCTL_CDROM_RAW_READ: defined locally so we don't depend
// on mingw-w64 version shipping them.
struct CdRawReadInfo {
    LARGE_INTEGER DiskOffset;   // LBA * 2048
    ULONG         SectorCount;
    ULONG         TrackMode;    // 2 = CDDA
};
#ifndef IOCTL_CDROM_RAW_READ
// CTL_CODE(0x2, 0xF, METHOD_OUT_DIRECT=2, FILE_READ_ACCESS=1)
#define IOCTL_CDROM_RAW_READ 0x0002403E
#endif

static char s_cdDriveLetter         = 0;
static int  s_trackStartLBA[MAX_CDTRACKS] = {};
static int  s_leadOutLBA            = 0;

static HANDLE OpenCdromDevice()
{
    char path[] = "\\\\.\\D:";
    for (char c = 'D'; c <= 'H'; ++c) {
        char root[4] = { c, ':', '\\', 0 };
        if (GetDriveTypeA(root) != DRIVE_CDROM) continue;
        path[4] = c;
        HANDLE h = CreateFileA(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            s_cdDriveLetter = c;
            return h;
        }
    }
    return INVALID_HANDLE_VALUE;
}

static CdDiscType PlatformReadTOC(int* outCount, int* outDurations, int maxTracks)
{
    if (outCount) *outCount = 0;
    HANDLE h = OpenCdromDevice();
    if (h == INVALID_HANDLE_VALUE) return CD_NONE;

    CDROM_TOC toc;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_CDROM_READ_TOC,
                               nullptr, 0, &toc, sizeof(toc), &br, nullptr);
    CloseHandle(h);
    if (!ok) return CD_NONE;

    int count = toc.LastTrack - toc.FirstTrack + 1;
    if (outCount) *outCount = count;

    bool allAudio = true;
    for (int i = 0; i <= count; ++i) {  // +1 to include lead-out
        int ti = toc.FirstTrack - 1 + i;  // index into TrackData
        UCHAR* a = toc.TrackData[ti].Address;
        int lba = ((int)a[1] * 60 + a[2]) * 75 + a[3] - 150;
        if (lba < 0) lba = 0;

        if (i < count) {
            if (toc.TrackData[ti].Control & 4) allAudio = false;
            s_trackStartLBA[i] = lba;
            if (outDurations && i + 1 <= count) {
                UCHAR* b = toc.TrackData[ti + 1].Address;
                int t0 = (int)a[1] * 60 + a[2];
                int t1 = (int)b[1] * 60 + b[2];
                outDurations[i] = (t1 > t0) ? (t1 - t0) : 0;
            }
        } else {
            s_leadOutLBA = lba;
        }
    }
    return allAudio ? CD_AUDIO : CD_DATA;
}

static void PlatformPopulatePlaybackInfo() {}

#else

static CdDiscType PlatformReadTOC(int* outCount, int* outDurations, int maxTracks)
{
    if (outCount) *outCount = 0;
    (void)outDurations; (void)maxTracks;
    return CD_NONE;
}
static void PlatformPopulatePlaybackInfo() {}

#endif  // platform

// ============================================================================
// Shared state — populated by CdAudio_Poll()
// ============================================================================

static CdDiscType s_discType   = CD_NONE;
static int        s_trackCount = 0;
static int        s_trackDurations[MAX_CDTRACKS] = {};
static int        s_totalSeconds = 0;

// ============================================================================
// Playback backends
// ============================================================================

// ---- A: libmpv (cross-platform audio-only) ---------------------------------
#ifdef UIX_CDAUDIO

#include <mpv/client.h>

static mpv_handle* s_mpv            = nullptr;
static bool        s_playing        = false;
static bool        s_paused         = false;
static int         s_pendingChapter = -1;
static double      s_timePos        = 0.0;
static double      s_chapterStart   = 0.0;

static void CdBack_Init()
{
    s_mpv = mpv_create();
    if (!s_mpv) { fprintf(stderr, "[CdAudio] mpv_create failed\n"); return; }
    mpv_set_option_string(s_mpv, "video",         "no");
    mpv_set_option_string(s_mpv, "audio-display", "no");
    mpv_set_option_string(s_mpv, "keep-open",     "yes");
    mpv_set_option_string(s_mpv, "terminal",      "no");
    if (mpv_initialize(s_mpv) < 0) {
        fprintf(stderr, "[CdAudio] mpv_initialize failed\n");
        mpv_destroy(s_mpv); s_mpv = nullptr; return;
    }
    mpv_observe_property(s_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
}

static void CdBack_Shutdown() { if (s_mpv) { mpv_destroy(s_mpv); s_mpv = nullptr; } }

bool CdAudio_Play(int track)
{
    if (!s_mpv || track < 1) return false;
    s_pendingChapter = track - 1;
    s_playing = true; s_paused = false; s_timePos = 0.0; s_chapterStart = 0.0;
    for (int i = 0; i < track - 1 && i < s_trackCount; ++i) s_chapterStart += s_trackDurations[i];
    const char* cmd[] = { "loadfile", "cdda://", "replace", nullptr };
    if (mpv_command(s_mpv, cmd) < 0) { s_playing = false; return false; }
    return true;
}
void CdAudio_Stop()   { if (s_mpv) { const char* c[] = {"stop",nullptr}; mpv_command(s_mpv,c); } s_playing=s_paused=false; }
void CdAudio_Pause()  { if (s_mpv&&s_playing){ mpv_set_property_string(s_mpv,"pause","yes"); s_paused=true; } }
void CdAudio_Resume() { if (s_mpv){ mpv_set_property_string(s_mpv,"pause","no"); s_paused=false; } }
bool   CdAudio_IsPlaying()   { return s_playing && !s_paused; }
bool   CdAudio_IsPaused()    { return s_playing && s_paused; }
double CdAudio_GetPosition() { double p=s_timePos-s_chapterStart; return p<0.0?0.0:p; }
void CdAudio_Update()
{
    if (!s_mpv) return;
    for (mpv_event* e; (e=mpv_wait_event(s_mpv,0))->event_id!=MPV_EVENT_NONE;) {
        if (e->event_id==MPV_EVENT_FILE_LOADED && s_pendingChapter>=0) {
            int64_t ch=s_pendingChapter; mpv_set_property(s_mpv,"chapter",MPV_FORMAT_INT64,&ch); s_pendingChapter=-1;
        } else if (e->event_id==MPV_EVENT_END_FILE) {
            s_playing=s_paused=false;
        } else if (e->event_id==MPV_EVENT_PROPERTY_CHANGE) {
            auto* p=(mpv_event_property*)e->data;
            if (!strcmp(p->name,"time-pos")&&p->format==MPV_FORMAT_DOUBLE) s_timePos=*(double*)p->data;
        }
    }
}

// ---- B: SDL ring-buffer + platform raw-sector read (Linux + Windows) -------
#elif defined(__linux__) || defined(_WIN32)

#include <SDL.h>

#define CDDA_FRAME_BYTES  2352
#define CDDA_RING_FRAMES  300
#define CDDA_RING_BYTES   (CDDA_RING_FRAMES * CDDA_FRAME_BYTES)
#define CDDA_READ_BATCH   25

static uint8_t s_ring[CDDA_RING_BYTES];
static int     s_ringRead = 0, s_ringWrite = 0, s_ringFill = 0;

static SDL_mutex*        s_mutex  = nullptr;
static SDL_cond*         s_cond   = nullptr;
static SDL_AudioDeviceID s_dev    = 0;
static SDL_Thread*       s_thread = nullptr;

static SDL_atomic_t s_atomPlaying   = {0};
static SDL_atomic_t s_atomPaused    = {0};
static SDL_atomic_t s_atomStop      = {0};
static SDL_atomic_t s_bytesConsumed = {0};

static void SDLCALL AudioCb(void*, uint8_t* stream, int len)
{
    SDL_LockMutex(s_mutex);
    int fill = len <= s_ringFill ? len : s_ringFill;
    for (int i = 0; i < fill; i++) {
        stream[i] = s_ring[s_ringRead];
        s_ringRead = (s_ringRead + 1) % CDDA_RING_BYTES;
    }
    if (fill < len) memset(stream + fill, 0, len - fill);
    s_ringFill -= fill;
    SDL_CondSignal(s_cond);
    SDL_UnlockMutex(s_mutex);
    SDL_AtomicAdd(&s_bytesConsumed, fill);
}

struct ReaderArgs { int startLBA, endLBA; };

static int SDLCALL ReaderFunc(void* arg)
{
    ReaderArgs ra = *(ReaderArgs*)arg;
    free(arg);

    uint8_t buf[CDDA_READ_BATCH * CDDA_FRAME_BYTES];
    int lba = ra.startLBA;
    bool readOk = true;

// ----- platform-specific device open + read -----
#if defined(__linux__)
    int fd = OpenDevice();
    if (fd < 0) {
        fprintf(stderr, "[CdAudio] cannot open device\n");
        SDL_AtomicSet(&s_atomPlaying, 0);
        return -1;
    }

    while (!SDL_AtomicGet(&s_atomStop) && lba < ra.endLBA) {
        if (SDL_AtomicGet(&s_atomPaused)) { SDL_Delay(20); continue; }
        int n = CDDA_READ_BATCH;
        if (lba + n > ra.endLBA) n = ra.endLBA - lba;

        struct cdrom_read_audio rda;
        memset(&rda, 0, sizeof(rda));
        rda.addr.lba = lba; rda.addr_format = CDROM_LBA;
        rda.nframes  = n;   rda.buf          = buf;

        if (ioctl(fd, CDROMREADAUDIO, &rda) < 0) {
            fprintf(stderr, "[CdAudio] CDROMREADAUDIO at LBA %d: %s\n", lba, strerror(errno));
            readOk = false; break;
        }

        int bytes = n * CDDA_FRAME_BYTES;
        SDL_LockMutex(s_mutex);
        while (!SDL_AtomicGet(&s_atomStop) && (CDDA_RING_BYTES - s_ringFill) < bytes)
            SDL_CondWait(s_cond, s_mutex);
        if (!SDL_AtomicGet(&s_atomStop)) {
            for (int i = 0; i < bytes; i++) { s_ring[s_ringWrite]=buf[i]; s_ringWrite=(s_ringWrite+1)%CDDA_RING_BYTES; }
            s_ringFill += bytes;
        }
        SDL_UnlockMutex(s_mutex);
        lba += n;
    }
    close(fd);

#elif defined(_WIN32)
    char devPath[16];
    snprintf(devPath, sizeof(devPath), "\\\\.\\%c:", s_cdDriveLetter ? s_cdDriveLetter : 'D');
    HANDLE hDev = CreateFileA(devPath, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDev == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[CdAudio] cannot open %s: error %lu\n", devPath, GetLastError());
        SDL_AtomicSet(&s_atomPlaying, 0);
        return -1;
    }

    while (!SDL_AtomicGet(&s_atomStop) && lba < ra.endLBA) {
        if (SDL_AtomicGet(&s_atomPaused)) { SDL_Delay(20); continue; }
        int n = CDDA_READ_BATCH;
        if (lba + n > ra.endLBA) n = ra.endLBA - lba;

        CdRawReadInfo rri;
        rri.DiskOffset.QuadPart = (LONGLONG)lba * 2048;
        rri.SectorCount = n;
        rri.TrackMode   = 2;  // CDDA

        DWORD br = 0;
        if (!DeviceIoControl(hDev, IOCTL_CDROM_RAW_READ,
                              &rri, sizeof(rri), buf, n * CDDA_FRAME_BYTES, &br, nullptr)) {
            fprintf(stderr, "[CdAudio] IOCTL_CDROM_RAW_READ at LBA %d: error %lu\n", lba, GetLastError());
            readOk = false; break;
        }

        int bytes = n * CDDA_FRAME_BYTES;
        SDL_LockMutex(s_mutex);
        while (!SDL_AtomicGet(&s_atomStop) && (CDDA_RING_BYTES - s_ringFill) < bytes)
            SDL_CondWait(s_cond, s_mutex);
        if (!SDL_AtomicGet(&s_atomStop)) {
            for (int i = 0; i < bytes; i++) { s_ring[s_ringWrite]=buf[i]; s_ringWrite=(s_ringWrite+1)%CDDA_RING_BYTES; }
            s_ringFill += bytes;
        }
        SDL_UnlockMutex(s_mutex);
        lba += n;
    }
    CloseHandle(hDev);
#endif  // platform read
// ------------------------------------------------

    if (readOk) {
        // Wait for ring to drain before marking end-of-track
        SDL_LockMutex(s_mutex);
        while (!SDL_AtomicGet(&s_atomStop) && s_ringFill > 0)
            SDL_CondWait(s_cond, s_mutex);
        SDL_UnlockMutex(s_mutex);
    }

    SDL_AtomicSet(&s_atomPlaying, 0);
    return 0;
}

static void StopAndJoin()
{
    SDL_AtomicSet(&s_atomStop, 1);
    if (s_dev) SDL_PauseAudioDevice(s_dev, 1);
    if (s_mutex) { SDL_LockMutex(s_mutex); SDL_CondBroadcast(s_cond); SDL_UnlockMutex(s_mutex); }
    if (s_thread) { SDL_WaitThread(s_thread, nullptr); s_thread = nullptr; }
    SDL_AtomicSet(&s_atomPlaying, 0);
    SDL_AtomicSet(&s_atomPaused,  0);
    SDL_AtomicSet(&s_atomStop,    0);
    if (s_mutex) { SDL_LockMutex(s_mutex); s_ringRead=s_ringWrite=s_ringFill=0; SDL_UnlockMutex(s_mutex); }
}

static void CdBack_Init()
{
    s_mutex = SDL_CreateMutex();
    s_cond  = SDL_CreateCond();
}

static void CdBack_Shutdown()
{
    StopAndJoin();
    if (s_dev)   { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
    if (s_cond)  { SDL_DestroyCond(s_cond);     s_cond = nullptr; }
    if (s_mutex) { SDL_DestroyMutex(s_mutex);   s_mutex = nullptr; }
}

bool CdAudio_Play(int track)
{
    if (track < 1 || track > s_trackCount) return false;

    StopAndJoin();

    int startLBA = s_trackStartLBA[track - 1];
    int endLBA   = (track < s_trackCount) ? s_trackStartLBA[track] : s_leadOutLBA;

    if (endLBA <= startLBA) {
        fprintf(stderr, "[CdAudio] bad LBA range track %d: %d..%d\n", track, startLBA, endLBA);
        return false;
    }

    SDL_AtomicSet(&s_bytesConsumed, 0);

    if (!s_dev) {
        SDL_AudioSpec want = {}, got = {};
        want.freq = 44100; want.format = AUDIO_S16LSB;
        want.channels = 2; want.samples = 2048; want.callback = AudioCb;
        s_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
        if (!s_dev) {
            fprintf(stderr, "[CdAudio] SDL_OpenAudioDevice: %s\n", SDL_GetError());
            return false;
        }
    }

    SDL_AtomicSet(&s_atomPlaying, 1);

    ReaderArgs* ra = (ReaderArgs*)malloc(sizeof(ReaderArgs));
    ra->startLBA = startLBA; ra->endLBA = endLBA;
    s_thread = SDL_CreateThread(ReaderFunc, "CdAudioReader", ra);
    if (!s_thread) {
        fprintf(stderr, "[CdAudio] SDL_CreateThread: %s\n", SDL_GetError());
        SDL_AtomicSet(&s_atomPlaying, 0); free(ra); return false;
    }

    SDL_PauseAudioDevice(s_dev, 0);
    fprintf(stderr, "[CdAudio] Playing track %d (LBA %d..%d)\n", track, startLBA, endLBA);
    return true;
}

void   CdAudio_Stop()    { StopAndJoin(); }
void   CdAudio_Pause()   { if (SDL_AtomicGet(&s_atomPlaying)) { SDL_AtomicSet(&s_atomPaused,1); if(s_dev) SDL_PauseAudioDevice(s_dev,1); } }
void   CdAudio_Resume()  { if (SDL_AtomicGet(&s_atomPlaying)) { SDL_AtomicSet(&s_atomPaused,0); if(s_dev) SDL_PauseAudioDevice(s_dev,0); } }
bool   CdAudio_IsPlaying()   { return SDL_AtomicGet(&s_atomPlaying) && !SDL_AtomicGet(&s_atomPaused); }
bool   CdAudio_IsPaused()    { return SDL_AtomicGet(&s_atomPlaying) &&  SDL_AtomicGet(&s_atomPaused); }
double CdAudio_GetPosition() { return SDL_AtomicGet(&s_atomPlaying) ? SDL_AtomicGet(&s_bytesConsumed)/(44100.0*4.0) : 0.0; }
void   CdAudio_Update()      {}

// ---- C: stubs --------------------------------------------------------------
#else

static void CdBack_Init()     {}
static void CdBack_Shutdown() {}
bool   CdAudio_Play(int)     { return false; }
void   CdAudio_Stop()        {}
void   CdAudio_Pause()       {}
void   CdAudio_Resume()      {}
bool   CdAudio_IsPlaying()   { return false; }
bool   CdAudio_IsPaused()    { return false; }
double CdAudio_GetPosition() { return 0.0; }
void   CdAudio_Update()      {}

#endif  // playback backend

// ============================================================================
// Public API
// ============================================================================

void CdAudio_Init()     { CdBack_Init(); }
void CdAudio_Shutdown() { CdBack_Shutdown(); }

void CdAudio_Poll()
{
    int durations[MAX_CDTRACKS] = {};
    int count = 0;
    CdDiscType type = PlatformReadTOC(&count, durations, MAX_CDTRACKS);

    s_discType = type;
    if (type == CD_AUDIO) {
        s_trackCount = count;
        int total = 0;
        for (int i = 0; i < count; ++i) { s_trackDurations[i] = durations[i]; total += durations[i]; }
        s_totalSeconds = total;
        PlatformPopulatePlaybackInfo();
    } else {
        s_trackCount = s_totalSeconds = 0;
        memset(s_trackDurations, 0, sizeof(s_trackDurations));
    }
}

CdDiscType CdAudio_GetDiscType()                  { return s_discType; }
int        CdAudio_GetTrackCount()                { return s_trackCount; }
int        CdAudio_GetTotalDurationSeconds()      { return s_totalSeconds; }
int        CdAudio_GetTrackDurationSeconds(int t) { return (t>=1&&t<=s_trackCount)?s_trackDurations[t-1]:0; }
