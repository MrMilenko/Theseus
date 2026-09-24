// frame_profiler.cpp: see frame_profiler.h. Theseus-original.

#include "std.h"
#include "frame_profiler.h"

#include <string.h>
#ifdef THESEUS_DESKTOP
#include <SDL.h>
#endif

int g_frameCount[FC_COUNTER_COUNT];

static long long s_freq;
static long long s_phaseStart[FP_PHASE_COUNT];
static long long s_phaseSum[FP_PHASE_COUNT];
static long long s_countSum[FC_COUNTER_COUNT];
static int       s_frames;
static long long s_windowStart;
static float     s_smoothedFps = 60.0f;
static bool      s_gpuRequested;
static bool      s_gpuTiming;
static float     s_lastLoadMs;
static float     s_maxLoadMs;
static FrameSnapshot s_snap;

#ifdef _XBOX
DWORD theseus_xbox_tss_shadow[8 * 32];
bool theseus_xbox_tss_seen[8 * 32];
LPDIRECT3DTEXTURE8 theseus_xbox_tex_shadow[8];
bool theseus_xbox_tex_seen[8];

static DWORD s_fence;
static bool  s_haveFence;
#else
static double s_hostGpuMs, s_hostWaitMs, s_hostDraws, s_hostVb, s_hostIb;
static int    s_hostFrames;
#endif

long long Frame_Now()
{
#ifdef _XBOX
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
#else
	return (long long)SDL_GetPerformanceCounter();
#endif
}

static long long Freq()
{
	if (s_freq == 0)
	{
#ifdef _XBOX
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		s_freq = li.QuadPart;
#else
		s_freq = (long long)SDL_GetPerformanceFrequency();
#endif
		if (s_freq == 0) s_freq = 1;
	}
	return s_freq;
}

float Frame_Ms(long long ticks)
{
	return (float)((double)ticks * 1000.0 / (double)Freq());
}

void Frame_RequestGpuTiming()
{
	s_gpuRequested = true;
}

void Frame_NoteLoad(float ms)
{
	s_lastLoadMs = ms;
	if (ms > s_maxLoadMs) s_maxLoadMs = ms;
}

#ifdef THESEUS_DESKTOP
void Frame_AddHostStats(float gpuMs, float gpuWaitMs, int draws, int transientVbBytes, int transientIbBytes)
{
	s_hostGpuMs  += gpuMs;
	s_hostWaitMs += gpuWaitMs;
	s_hostDraws  += draws;
	s_hostVb     += transientVbBytes;
	s_hostIb     += transientIbBytes;
	s_hostFrames++;
}
#endif

void Frame_Begin()
{
	if (s_windowStart == 0) s_windowStart = Frame_Now();
	memset(g_frameCount, 0, sizeof(g_frameCount));

#ifdef _XBOX
	// Waiting on last frame's fence is time the GPU was still behind. The
	// CPU would have paid it in Present anyway, so this only moves it.
	if (s_haveFence)
	{
		long long t0 = Frame_Now();
		D3DDevice::BlockOnFence(s_fence);
		s_phaseSum[FP_GPU_WAIT] += Frame_Now() - t0;
		s_haveFence = false;
	}
#endif
}

void Frame_PhaseBegin(FramePhase p)
{
	s_phaseStart[p] = Frame_Now();
}

void Frame_PhaseEnd(FramePhase p)
{
	s_phaseSum[p] += Frame_Now() - s_phaseStart[p];
}

void Frame_End()
{
	s_gpuTiming = s_gpuRequested;
	s_gpuRequested = false;

#ifdef _XBOX
	if (s_gpuTiming)
	{
		s_fence = D3DDevice::InsertFence();
		s_haveFence = true;
	}
#endif

	for (int i = 0; i < FC_COUNTER_COUNT; i++)
		s_countSum[i] += g_frameCount[i];
	s_frames++;

	long long now = Frame_Now();
	float elapsed = Frame_Ms(now - s_windowStart);
	if (elapsed < 250.0f)
		return;

	// Light EMA on fps so the readout is steady at 60 but still shows a drop.
	float instFps = (float)s_frames * 1000.0f / elapsed;
	s_smoothedFps = s_smoothedFps * 0.7f + instFps * 0.3f;

	FrameSnapshot snap;
	memset(&snap, 0, sizeof(snap));
	snap.fps = s_smoothedFps;
	snap.frameMs = s_smoothedFps > 0.01f ? 1000.0f / s_smoothedFps : 0.0f;

	float inv = 1.0f / (float)s_frames;
	for (int p = 0; p < FP_PHASE_COUNT; p++)
		snap.phaseMs[p] = Frame_Ms(s_phaseSum[p]) * inv;
	for (int c = 0; c < FC_COUNTER_COUNT; c++)
		snap.count[c] = (float)s_countSum[c] * inv;

#ifdef THESEUS_DESKTOP
	if (s_hostFrames > 0)
	{
		float hinv = 1.0f / (float)s_hostFrames;
		snap.gpuMs         = (float)(s_hostGpuMs * hinv);
		snap.phaseMs[FP_GPU_WAIT] = (float)(s_hostWaitMs * hinv);
		snap.hostDraws     = (float)(s_hostDraws * hinv);
		snap.transientVbKB = (float)(s_hostVb * hinv / 1024.0);
		snap.transientIbKB = (float)(s_hostIb * hinv / 1024.0);
	}
	s_hostGpuMs = s_hostWaitMs = s_hostDraws = s_hostVb = s_hostIb = 0;
	s_hostFrames = 0;
#endif

	snap.lastLoadMs = s_lastLoadMs;
	snap.maxLoadMs  = s_maxLoadMs;
	s_snap = snap;

	memset(s_phaseSum, 0, sizeof(s_phaseSum));
	memset(s_countSum, 0, sizeof(s_countSum));
	s_frames = 0;
	s_windowStart = now;
}

void Frame_Read(FrameSnapshot* out)
{
	if (out) *out = s_snap;
}
