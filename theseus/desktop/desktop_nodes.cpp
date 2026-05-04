// desktop_nodes.cpp: node class registrations and stubs for the
// Xbox-only XAP nodes that don't have a desktop implementation yet
// (memory monitor, disc drive, copy game, etc.). Audio nodes have
// real SDL_mixer-backed implementations; the rest are stubs that
// keep XAP scripts resolving.

#include "std.h"
#ifdef _WIN32
#include <io.h>
#ifndef F_OK
#define F_OK 0
#endif
// Windows wingdi.h defines GetFreeSpace as a macro; conflicts with our method
#undef GetFreeSpace
#endif
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "audio_sdl.h"
#include "cdaudio.h"

// Helper: resolve XAP audio URL to local filesystem path
// XAP urls look like "Audio/MainAudio/A Button Select.wav" (relative to Q:\)
static const char* ResolveAudioURL(const TCHAR* url)
{
	if (!url || !*url) return NULL;

	static char s_path[512];

	// Check if it already has a drive letter (Q:\...)
	if (url[0] && url[1] == ':') {
		// Use XboxFS translation
		const char* translated = XboxFS_TranslatePath(url);
		return translated;
	}

	// Relative path; prepend xboxfs/Q/
	snprintf(s_path, sizeof(s_path), "xboxfs/Q/%s", url);
	// Convert backslashes
	for (char* p = s_path; *p; p++)
		if (*p == '\\') *p = '/';
	return s_path;
}

// ============================================================================
// CAudioClip - real SDL_mixer implementation
// ============================================================================
class CAudioClip : public CTimeDepNode
{
public:
	CAudioClip() : m_volume(1.0f), m_pan(0.0f), m_frequency(1.0f), m_fade(0.0f),
	               m_removeVoice(false), m_sendProgress(false), m_pause_on_moving(false), m_progress(0.0f),
	               m_url(NULL), m_transportMode(0),
	               m_soundHandle(-1), m_channel(-1), m_isStreaming(false), m_loadedUrl(NULL) {}
	~CAudioClip() {
		Unload();
	}

	void Unload() {
		if (m_loadedUrl && strncmp(m_loadedUrl, "cd:", 3) == 0)
			CdAudio_Stop();
		if (m_channel >= 0) { DashAudio_StopChannel(m_channel); m_channel = -1; }
		if (m_soundHandle >= 0) { DashAudio_FreeSound(m_soundHandle); m_soundHandle = -1; }
		if (m_isStreaming) { DashAudio_FreeMusic(); m_isStreaming = false; }
		free(m_loadedUrl);
		m_loadedUrl = NULL;
	}

	static bool IsCdUrl(const char* url) { return url && strncmp(url, "cd:", 3) == 0; }

	DECLARE_NODE(CAudioClip, CTimeDepNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	// XAP-visible properties
	float m_volume, m_pan, m_frequency, m_fade, m_progress;
	TCHAR* m_url;
	int m_transportMode; // 0=stopped, 1=playing, 2=paused

	bool m_removeVoice, m_sendProgress, m_pause_on_moving;

	// Internal state
	int   m_soundHandle;  // DashAudio sound handle (-1 if not loaded or streaming)
	int   m_channel;      // SDL_mixer channel (-1 if not playing)
	bool  m_isStreaming;   // true if using Mix_Music (long tracks), false for Mix_Chunk (SFX)
	char* m_loadedUrl;    // tracks what URL is currently loaded (detects changes)

	bool UrlChanged() {
		if (!m_url && !m_loadedUrl) return false;
		if (!m_url || !m_loadedUrl) return true;
		return strcmp(m_url, m_loadedUrl) != 0;
	}

	void LoadIfNeeded()
	{
		if (!m_url || !*m_url) return;

		// If URL changed, unload previous audio
		if (UrlChanged()) {
			Unload();
		}

		// Already loaded
		if (m_soundHandle >= 0 || m_isStreaming) return;

		// Handle "st:SONGID" URLs; soundtrack songs from the music collection
		if (strncmp(m_url, "st:", 3) == 0) {
			int songID = atoi(m_url + 3);
			const char* songPath = DashMusic_GetSongPathByID(songID);
			if (songPath) {
				if (DashAudio_LoadMusic(songPath) == 0)
					m_isStreaming = true;
			}
			m_loadedUrl = strdup(m_url);
			return;
		}

		// Handle "cd:N" URLs — CDDA track; CdAudio_Play handles the actual load
		if (strncmp(m_url, "cd:", 3) == 0) {
			m_loadedUrl = strdup(m_url);
			return;
		}

		const char* path = ResolveAudioURL(m_url);
		if (!path) return;

		// Use streaming for music files (long tracks)
		if (strstr(path, "/Music/") != NULL || strstr(path, ".mp3") != NULL) {
			if (DashAudio_LoadMusic(path) == 0)
				m_isStreaming = true;
		} else {
			// Short sound effect
			m_soundHandle = DashAudio_LoadSound(path);
		}
		m_loadedUrl = strdup(m_url);
	}

	void Play()
	{
		// If paused, resume from current position rather than restart.
		if (m_transportMode == 2) {
			if (IsCdUrl(m_loadedUrl)) {
				CdAudio_Resume();
				DashAudio_MuteAll();
			} else if (m_isStreaming) {
				DashAudio_ResumeMusic();
			} else if (m_channel >= 0) {
				DashAudio_ResumeChannel(m_channel);
			}
			m_transportMode = 1;
			CallFunction(this, _T("onPlay"));
			CallFunction(this, _T("OnTransportModeChanged"));
			return;
		}

		LoadIfNeeded();

		if (IsCdUrl(m_loadedUrl)) {
			int track = atoi(m_loadedUrl + 3);
			if (track < 1) track = 1;
			if (CdAudio_Play(track)) {
				DashAudio_MuteAll();
				m_transportMode = 1;
				m_isActive = true;
				CallFunction(this, _T("onPlay"));
				CallFunction(this, _T("OnTransportModeChanged"));
			}
			return;
		}

		if (m_isStreaming) {
			int loops = m_loop ? -1 : 0;
			int fadeMs = (m_fade > 0.0f) ? (int)(m_fade * 1000.0f) : 0;
			DashAudio_SetMusicVolume(m_volume);
			DashAudio_PlayMusic(loops, fadeMs);
			m_transportMode = 1;
			m_isActive = true;
			m_progress = 0.0f;
			CallFunction(this, _T("onPlay"));
			CallFunction(this, _T("OnTransportModeChanged"));
		} else if (m_soundHandle >= 0) {
			int loops = m_loop ? -1 : 0;
			int fadeMs = (m_fade > 0.0f) ? (int)(m_fade * 1000.0f) : 0;
			m_channel = DashAudio_PlaySound(m_soundHandle, loops, fadeMs);
			if (m_channel >= 0) {
				DashAudio_SetChannelVolume(m_channel, m_volume);
				DashAudio_SetChannelPan(m_channel, m_pan);
				m_transportMode = 1;
				m_isActive = true;
				CallFunction(this, _T("onPlay"));
				CallFunction(this, _T("OnTransportModeChanged"));
			}
		}
	}

	void Stop()
	{
		if (IsCdUrl(m_loadedUrl)) {
			CdAudio_Stop();
			DashAudio_UnmuteAll();
			m_transportMode = 0;
			m_isActive = false;
			m_progress = 0.0f;
			CallFunction(this, _T("onStop"));
			CallFunction(this, _T("OnTransportModeChanged"));
			return;
		}

		if (m_isStreaming) {
			int fadeMs = (m_fade > 0.0f) ? (int)(m_fade * 1000.0f) : 0;
			DashAudio_StopMusic(fadeMs);
		} else if (m_channel >= 0) {
			if (m_fade > 0.0f)
				DashAudio_FadeOutChannel(m_channel, (int)(m_fade * 1000.0f));
			else
				DashAudio_StopChannel(m_channel);
		}
		m_channel = -1;
		m_transportMode = 0;
		m_isActive = false;
		m_progress = 0.0f;
		CallFunction(this, _T("onStop"));
		CallFunction(this, _T("OnTransportModeChanged"));
	}

	void Pause()
	{
		if (m_transportMode == 2) {
			// Toggle: resume if already paused
			if (IsCdUrl(m_loadedUrl)) {
				CdAudio_Resume();
				DashAudio_MuteAll();
			} else if (m_isStreaming) {
				DashAudio_ResumeMusic();
			} else if (m_channel >= 0) {
				DashAudio_ResumeChannel(m_channel);
			}
			m_transportMode = 1;
			CallFunction(this, _T("onPlay"));
			CallFunction(this, _T("OnTransportModeChanged"));
			return;
		}

		if (m_transportMode != 1) return;

		if (IsCdUrl(m_loadedUrl)) {
			CdAudio_Pause();
			DashAudio_UnmuteAll();
			m_transportMode = 2;
			CallFunction(this, _T("onPause"));
			CallFunction(this, _T("OnTransportModeChanged"));
			return;
		}

		if (m_isStreaming)
			DashAudio_PauseMusic();
		else if (m_channel >= 0)
			DashAudio_PauseChannel(m_channel);
		m_transportMode = 2;
		CallFunction(this, _T("onPause"));
		CallFunction(this, _T("OnTransportModeChanged"));
	}

	void PlayOrPause()
	{
		if (IsCdUrl(m_loadedUrl)) {
			if (m_transportMode == 1) {
				Pause();
			} else if (m_transportMode == 2) {
				CdAudio_Resume();
				m_transportMode = 1;
				CallFunction(this, _T("onPlay"));
				CallFunction(this, _T("OnTransportModeChanged"));
			} else {
				Play();
			}
			return;
		}

		if (m_transportMode == 1)
			Pause();
		else if (m_transportMode == 2) {
			if (m_isStreaming)
				DashAudio_ResumeMusic();
			else if (m_channel >= 0)
				DashAudio_ResumeChannel(m_channel);
			m_transportMode = 1;
			CallFunction(this, _T("onPlay"));
			CallFunction(this, _T("OnTransportModeChanged"));
		} else {
			Play();
		}
	}

	int getMinutes()
	{
		double pos = IsCdUrl(m_loadedUrl) ? CdAudio_GetPosition()
		                                  : (m_isStreaming ? DashAudio_GetMusicPosition() : 0.0);
		return (int)(pos / 60.0);
	}

	int getSeconds()
	{
		double pos = IsCdUrl(m_loadedUrl) ? CdAudio_GetPosition()
		                                  : (m_isStreaming ? DashAudio_GetMusicPosition() : 0.0);
		return ((int)pos) % 60;
	}

	void Advance(float nSeconds)
	{
		CTimeDepNode::Advance(nSeconds);

		if (m_transportMode == 1 && IsCdUrl(m_loadedUrl)) {
			CdAudio_Update();

			if (m_sendProgress) {
				int track = atoi(m_loadedUrl + 3);
				int dur = CdAudio_GetTrackDurationSeconds(track);
				double pos = CdAudio_GetPosition();
				m_progress = (dur > 0) ? (float)(pos / dur) : 0.0f;
				CallFunction(this, _T("OnProgressChanged"));
			}

			if (!CdAudio_IsPlaying() && !CdAudio_IsPaused()) {
				DashAudio_UnmuteAll();
				m_transportMode = 0;
				m_isActive = false;
				m_progress = 1.0f;
				CallFunction(this, _T("OnEndOfAudio"));
				CallFunction(this, _T("OnTransportModeChanged"));
			}
			return;
		}

		if (m_transportMode == 1) {
			if (m_isStreaming) {
				DashAudio_SetMusicVolume(m_volume);

				// Update progress; always fire OnProgressChanged so script can poll time
				if (m_sendProgress) {
					double dur = DashAudio_GetMusicDuration();
					double pos = DashAudio_GetMusicPosition();
					float newProgress = (dur > 0.0) ? (float)(pos / dur) : 0.0f;
					m_progress = newProgress;
					// Always fire so the script can call getMinutes()/getSeconds()
					CallFunction(this, _T("OnProgressChanged"));
				}

				// Check if music finished
				if (!DashAudio_IsMusicPlaying()) {
					m_transportMode = 0;
					m_isActive = false;
					m_progress = 1.0f;
					CallFunction(this, _T("OnEndOfAudio"));
					CallFunction(this, _T("OnTransportModeChanged"));
				}
			} else if (m_channel >= 0) {
				DashAudio_SetChannelVolume(m_channel, m_volume);
				DashAudio_SetChannelPan(m_channel, m_pan);

				// Check if channel finished
				if (DashAudio_DidChannelFinish(m_channel)) {
					m_channel = -1;
					m_transportMode = 0;
					m_isActive = false;
					m_progress = 1.0f;
					CallFunction(this, _T("OnEndOfAudio"));
					CallFunction(this, _T("OnTransportModeChanged"));
				}
			}
		}
	}
};

IMPLEMENT_NODE("AudioClip", CAudioClip, CTimeDepNode)

START_NODE_PROPS(CAudioClip, CTimeDepNode)
	NODE_PROP(pt_number, CAudioClip, volume)
	NODE_PROP(pt_number, CAudioClip, pan)
	NODE_PROP(pt_number, CAudioClip, frequency)
	NODE_PROP(pt_number, CAudioClip, fade)
	NODE_PROP(pt_string, CAudioClip, url)
	NODE_PROP(pt_integer, CAudioClip, transportMode)
	NODE_PROP(pt_boolean, CAudioClip, removeVoice)
	NODE_PROP(pt_boolean, CAudioClip, sendProgress)
	NODE_PROP(pt_boolean, CAudioClip, pause_on_moving)
	NODE_PROP(pt_number, CAudioClip, progress)
	NODE_PROP(pt_boolean, CAudioClip, isActive)
	NODE_PROP(pt_boolean, CAudioClip, loop)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CAudioClip
START_NODE_FUN(CAudioClip, CTimeDepNode)
	NODE_FUN_VV(Play)
	NODE_FUN_VV(Stop)
	NODE_FUN_VV(Pause)
	NODE_FUN_IV(getMinutes)
	NODE_FUN_IV(getSeconds)
	NODE_FUN_VV(PlayOrPause)
END_NODE_FUN()

// ============================================================================
// CPeriodicAudioGroup - from Audio.cpp
// ============================================================================
class CPeriodicAudioGroup : public CGroup
{
public:
	CPeriodicAudioGroup() : m_isActive(false), m_period(10.0f), m_periodNoise(0.0f) {}

	DECLARE_NODE(CPeriodicAudioGroup, CGroup)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
	bool m_isActive;
	float m_period, m_periodNoise;
};

IMPLEMENT_NODE("PeriodicAudioGroup", CPeriodicAudioGroup, CGroup)

START_NODE_PROPS(CPeriodicAudioGroup, CGroup)
	NODE_PROP(pt_boolean, CPeriodicAudioGroup, isActive)
	NODE_PROP(pt_number, CPeriodicAudioGroup, period)
	NODE_PROP(pt_number, CPeriodicAudioGroup, periodNoise)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CPeriodicAudioGroup
START_NODE_FUN(CPeriodicAudioGroup, CGroup)
END_NODE_FUN()

// ============================================================================
// CAudioVisualizer - full port from AudioVisualizer.cpp
// Uses DashAudio_GetPCMSamples() ring buffer instead of Xbox GetSampleBuffer()
// ============================================================================
#include "tmap_system.h"
#include "fft.h"

#define TRANSPORT_PLAY 1

class CAudioVisualizer : public CNode
{
	DECLARE_NODE(CAudioVisualizer, CNode)
public:
	CAudioVisualizer();
	~CAudioVisualizer();

	TCHAR* m_type;      // "line", "spinner"/"spin", "circle", "analyzer"/"a"
	TCHAR* m_channel;   // "left"/"l", "center"/"c", "right"/"r"
	CNode* m_source;
	float m_scale;
	float m_offset;

protected:
	short* GetMonoPCM();
	void UpdateSpectrum();
	void RenderDynamicTexture(CSurfx* pSurfx);
	void Advance(float nSeconds);
	void CalcSpectrum(short* pcm, short* fft);
	void RenderEffect1(CSurfx* pSurfx);
	void RenderEffect2(CSurfx* pSurfx);

	short m_pcmLeft[256];
	short m_pcmRight[256];
	short m_fftLeft[128];
	short m_fftRight[128];
	bool m_bFFTValid;
	short m_pcmMono[256];
	bool m_bMonoValid;

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("AudioVisualizer", CAudioVisualizer, CNode)

START_NODE_PROPS(CAudioVisualizer, CNode)
	NODE_PROP(pt_number, CAudioVisualizer, scale)
	NODE_PROP(pt_number, CAudioVisualizer, offset)
	NODE_PROP(pt_string, CAudioVisualizer, type)
	NODE_PROP(pt_string, CAudioVisualizer, channel)
	NODE_PROP(pt_node, CAudioVisualizer, source)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CAudioVisualizer
START_NODE_FUN(CAudioVisualizer, CNode)
END_NODE_FUN()

CAudioVisualizer::CAudioVisualizer() :
	m_type(NULL),
	m_source(NULL),
	m_scale(1.0f),
	m_offset(0.0f),
	m_channel(NULL),
	m_bFFTValid(false),
	m_bMonoValid(false)
{
	memset(m_pcmLeft, 0, sizeof(m_pcmLeft));
	memset(m_pcmRight, 0, sizeof(m_pcmRight));
	memset(m_fftLeft, 0, sizeof(m_fftLeft));
	memset(m_fftRight, 0, sizeof(m_fftRight));
	memset(m_pcmMono, 0, sizeof(m_pcmMono));
}

CAudioVisualizer::~CAudioVisualizer()
{
	delete [] m_type;
	delete [] m_channel;

	if (m_source != NULL)
		m_source->Release();
}

void CAudioVisualizer::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_source != NULL)
		m_source->Advance(nSeconds);
}

short* CAudioVisualizer::GetMonoPCM()
{
	if (!m_bMonoValid)
	{
		for (int i = 0; i < 256; i += 1)
			m_pcmMono[i] = (short)(((int)m_pcmLeft[i] + (int)m_pcmRight[i]) / 2);
		m_bMonoValid = true;
	}
	return m_pcmMono;
}

void CAudioVisualizer::CalcSpectrum(short* pcm, short* fft)
{
	static fft_state *state = NULL;
	float buf[FFT_BUFFER_SIZE / 2 + 1];

	if (!state)
		state = fft_init();

	fft_perform(pcm, buf, state);

	for (int i = 0; i < FFT_BUFFER_SIZE / 2 + 1; i += 1)
	{
		buf[i] = sqrtf(buf[i]) / FFT_BUFFER_SIZE;
		fft[i] = (short)buf[i];
	}
}

void CAudioVisualizer::UpdateSpectrum()
{
	if (m_bFFTValid)
		return;

	CalcSpectrum(m_pcmLeft, m_fftLeft);
	CalcSpectrum(m_pcmRight, m_fftRight);

	m_bFFTValid = true;
}

// Helper pixel-set with bounds checking
static void VizSetPixel(CSurfx* pSurfx, int x, int y, BYTE color)
{
	if (x < 0 || y < 0 || x >= pSurfx->m_nWidth || y >= pSurfx->m_nHeight)
		return;
	*pSurfx->Pixel(x, y) = color;
}

static void VizSetPixel2(CSurfx* pSurfx, int x, int y, BYTE color)
{
	VizSetPixel(pSurfx, x, y, color);
	VizSetPixel(pSurfx, x + 1, y, color);
	VizSetPixel(pSurfx, x + 1, y + 1, color);
	VizSetPixel(pSurfx, x, y + 1, color);
}

#define viz_mag(s) ((float)samples[(int)(s * nSamples) * 2 + lrc] / 32767.0f)

void CAudioVisualizer::RenderDynamicTexture(CSurfx* pSurfx)
{
	// On desktop, check if source audio clip is playing
	CAudioClip* pAudioClip = (CAudioClip*)m_source;
	if (pAudioClip == NULL || pAudioClip->GetNodeClass() != NODE_CLASS(CAudioClip) || pAudioClip->m_transportMode != TRANSPORT_PLAY)
		return;

	// Desktop: pull PCM from the Mix_SetPostMix ring buffer
	DashAudio_GetPCMSamples(m_pcmLeft, m_pcmRight, 256);

	int nSamples = 256;
	m_bMonoValid = false;
	m_bFFTValid = false;

	// Build interleaved buffer for the mag() macro
	short samples[512];
	for (int i = 0; i < nSamples; i++) {
		samples[i * 2]     = m_pcmLeft[i];
		samples[i * 2 + 1] = m_pcmRight[i];
	}

	if (nSamples > pSurfx->m_nHeight)
		nSamples = pSurfx->m_nHeight;

	int lrc = 0;
	if (m_channel != NULL)
	{
		if (m_channel[0] == 'r' || m_channel[0] == 'R')
			lrc = 1;
	}

	int nType = 0;
	if (m_type != NULL)
	{
		switch (m_type[0])
		{
		case 's': case 'S': nType = 1; break; // spinner
		case 'c': case 'C': nType = 2; break; // circle
		case 'a': case 'A': nType = 3; break; // analyzer
		}
	}

	float t = (float)TheseusGetNow();

	switch (nType)
	{
	case 0: // Line Scope
		{
			int xCenter = pSurfx->m_nWidth / 2;
			int xp = xCenter;
			int yp = 0;

			for (int y = 0; y < nSamples; y += 1)
			{
				long s = (((long)samples[y * 2 + lrc]) * pSurfx->m_nWidth) >> 16;
				int x = xCenter + (int)s;
				pSurfx->Line(xp, yp, x, y, 255);
				xp = x;
				yp = y;
			}
		}
		break;

	case 1: // Spinner Scope
		{
			int xCenter = pSurfx->m_nWidth / 2;
			int yCenter = pSurfx->m_nHeight / 2;
			float step = 1.0f / (float)pSurfx->m_nWidth;
			float firstX = 0, firstY = 0;
			float prevX = 0, prevY = 0;

			float B0 = cosf(t * 0.2f);
			float B1 = sinf(t * 0.2f);

			for (float s = 0.0f; s <= 1.0f; s += step)
			{
				float C0 = viz_mag(s) * m_scale + m_offset;
				float C1 = 2.1f * (s - 0.5f);

				float X0 = B0 * C1 + B1 * C0;
				float Y0 = -B0 * C0 + B1 * C1;

				if (s == 0.0f)
				{
					firstX = X0;
					firstY = Y0;
				}
				else
				{
					pSurfx->Line(xCenter + (int)(prevX * xCenter), yCenter + (int)(prevY * yCenter),
						xCenter + (int)(X0 * xCenter), yCenter + (int)(Y0 * yCenter), 255);
				}

				prevX = X0;
				prevY = Y0;
			}
		}
		break;

	case 2: // Circle Scope (re-enabled from Xbox #if 0)
		{
			int xCenter = pSurfx->m_nWidth / 2;
			int yCenter = pSurfx->m_nHeight / 2;
			float step = 1.0f / (float)pSurfx->m_nWidth;
			float prevX2 = 0, prevY2 = 0;
			bool first = true;

			int i2 = 0;
			for (float s = 0.0f; s <= 1.0f; s += step, i2 += 1)
			{
				float sample = (float)samples[i2 * 2 + lrc] / 32767.0f;
				float C0 = sample * m_scale + m_offset;
				float C1 = s * 3.14159265f; // PI

				float X0 = C0 * sinf(C1);
				float Y0 = C0 * cosf(C1);

				if (!first)
				{
					pSurfx->Line(xCenter + (int)(prevX2 * xCenter), yCenter + (int)(prevY2 * yCenter),
						xCenter + (int)(X0 * xCenter), yCenter + (int)(Y0 * yCenter), 255);
					pSurfx->Line(xCenter - (int)(prevX2 * xCenter), yCenter + (int)(prevY2 * yCenter),
						xCenter - (int)(X0 * xCenter), yCenter + (int)(Y0 * yCenter), 255);
				}
				first = false;
				prevX2 = X0;
				prevY2 = Y0;
			}
		}
		break;

	case 3: // Spectrum Analyzer
		{
			UpdateSpectrum();

			static int peak_buf[256];
			BYTE spectrum[256];

			int i;
			for (i = 0; i < 128; i += 1)
			{
				float n = logf((float)(m_fftLeft[127 - i] + 1)) * 8.0f;
				if (n <= 0.0f)
					spectrum[i] = 0;
				else if (n >= 255.0f)
					spectrum[i] = 255;
				else
					spectrum[i] = (BYTE)n;
			}

			for (; i < 256; i += 1)
			{
				float n = logf((float)(m_fftRight[i - 128] + 1)) * 8.0f;
				if (n <= 0.0f)
					spectrum[i] = 0;
				else if (n >= 255.0f)
					spectrum[i] = 255;
				else
					spectrum[i] = (BYTE)n;
			}

			for (i = 0; i < 256; i += 1)
			{
				if (peak_buf[i] > 2)
					peak_buf[i] -= 2;
				else
					peak_buf[i] = 0;

				int y = spectrum[i] * 2;
				if (peak_buf[i] < y)
					peak_buf[i] = y;
			}

			int nWidth = pSurfx->m_nWidth;
			for (int x = 0; x < nWidth; x += 1)
			{
				float nHeight = ((float)spectrum[(x * 256) / nWidth] * (float)pSurfx->m_nHeight) / 256.0f;
				if ((int)nHeight > pSurfx->m_nHeight) nHeight = (float)pSurfx->m_nHeight;

				BYTE bColor = 255;
				for (int y = pSurfx->m_nHeight - (int)nHeight; y < pSurfx->m_nHeight; y += 1, bColor -= 1)
					*pSurfx->Pixel(x, y) = bColor;

				nHeight = (float)peak_buf[(x * 256) / nWidth];
				nHeight /= 2.0f;
				nHeight = (nHeight * (float)pSurfx->m_nHeight) / 256.0f;
				int peakY = pSurfx->m_nHeight - (int)nHeight;
				if (peakY >= 0 && peakY < pSurfx->m_nHeight)
					*pSurfx->Pixel(x, peakY) = 255;
			}

			RenderEffect1(pSurfx);
			RenderEffect2(pSurfx);
		}
		break;
	}
}

// Beat detection overlay
#define BASS_EXT_MEMORY 10

static struct {
	int max_recent;
	int max_old;
	int time_last_max;
	int min_recent;
	int min_old;
	int time_last_min;
	int activated;
} s_bass_info = {};

void CAudioVisualizer::RenderEffect1(CSurfx* pSurfx)
{
	static int t = 0;

	int bass = 0;
	const int step = 5;
	for (int i = 0; i < step; i += 1)
		bass += (m_fftLeft[i] >> 4) + (m_fftRight[i] >> 4);
	bass /= (step * 2);

	if (bass > s_bass_info.max_recent)
		s_bass_info.max_recent = bass;

	if (bass < s_bass_info.min_recent)
		s_bass_info.min_recent = bass;

	if (t - s_bass_info.time_last_max > BASS_EXT_MEMORY)
	{
		s_bass_info.max_old = s_bass_info.max_recent;
		s_bass_info.max_recent = 0;
		s_bass_info.time_last_max = t;
	}

	if (t - s_bass_info.time_last_min > BASS_EXT_MEMORY)
	{
		s_bass_info.min_old = s_bass_info.min_recent;
		s_bass_info.min_recent = 0;
		s_bass_info.time_last_min = t;
	}

	if (bass > (s_bass_info.max_old * 6 + s_bass_info.min_old * 4) / 10 && s_bass_info.activated == 0)
	{
		FillMemory(pSurfx->m_pels, pSurfx->m_nWidth * pSurfx->m_nHeight, 255);
		s_bass_info.activated = 1;
	}

	if (bass < (s_bass_info.max_old * 4 + s_bass_info.min_old * 6) / 10 && s_bass_info.activated == 1)
		s_bass_info.activated = 0;

	t += 1;
}

// Waveform curve overlay effects
static struct { int i; float *f; } s_cosw = { 0, NULL };
static struct { int i; float *f; } s_sinw = { 0, NULL };

static int s_spectral_amplitude = 50;
static int s_spectral_shift = 30;
static int s_mode_spectre = -1;
static BYTE s_spectral_color = 128;

void CAudioVisualizer::RenderEffect2(CSurfx* pSurfx)
{
	int halfheight, halfwidth;
	float old_y1, old_y2;
	float y1 = (float)((((m_pcmLeft[0] + m_pcmRight[0]) >> 9) * s_spectral_amplitude * pSurfx->m_nHeight) >> 12);
	float y2 = (float)((((m_pcmLeft[0] + m_pcmRight[0]) >> 9) * s_spectral_amplitude * pSurfx->m_nHeight) >> 12);
	const int density_lines = 5;
	const int step = 4;
	const int shift = (s_spectral_shift * pSurfx->m_nHeight) >> 8;

	static XTIME timeToChange = 0.0f;
	if (s_mode_spectre < 0 || TheseusGetNow() >= timeToChange)
	{
		s_mode_spectre += 1;
		timeToChange = TheseusGetNow() + 3.0f + rnd(5.0f);
	}

	if ((UINT)s_mode_spectre > 4)
		s_mode_spectre = 0;

	if (s_cosw.i != pSurfx->m_nWidth || s_sinw.i != pSurfx->m_nWidth)
	{
		delete [] s_cosw.f;
		delete [] s_sinw.f;
		s_sinw.f = s_cosw.f = NULL;
		s_sinw.i = s_cosw.i = 0;
	}

	const float halfPI = 3.14159265f / 2.0f;
	const float fullPI = 3.14159265f;

	if (s_cosw.i == 0 || s_cosw.f == NULL)
	{
		s_cosw.i = pSurfx->m_nWidth;
		s_cosw.f = new float[pSurfx->m_nWidth];
		for (int i = 0; i < pSurfx->m_nWidth; i += step)
			s_cosw.f[i] = cosf((float)i / pSurfx->m_nWidth * fullPI + halfPI);
	}

	if (s_sinw.i == 0 || s_sinw.f == NULL)
	{
		s_sinw.i = pSurfx->m_nWidth;
		s_sinw.f = new float[pSurfx->m_nWidth];
		for (int i = 0; i < pSurfx->m_nWidth; i += step)
			s_sinw.f[i] = sinf((float)i / pSurfx->m_nWidth * fullPI + halfPI);
	}

	if (s_mode_spectre == 3)
	{
		if (y1 < 0) y1 = 0;
		if (y2 < 0) y2 = 0;
	}

	halfheight = pSurfx->m_nHeight >> 1;
	halfwidth  = pSurfx->m_nWidth >> 1;

	for (int i = step; i < pSurfx->m_nWidth; i += step)
	{
		old_y1 = y1;
		old_y2 = y2;

		y1 = (float)(((m_pcmRight[(i << 8) / pSurfx->m_nWidth / density_lines] >> 8) * s_spectral_amplitude * pSurfx->m_nHeight) >> 12);
		y2 = (float)(((m_pcmLeft[(i << 8) / pSurfx->m_nWidth / density_lines] >> 8) * s_spectral_amplitude * pSurfx->m_nHeight) >> 12);

		switch (s_mode_spectre)
		{
		case 0:
			pSurfx->Line(i-step,(int)(halfheight+shift+old_y2),
			     i,(int)(halfheight+shift+y2), s_spectral_color);
			break;

		case 1:
			pSurfx->Line(i-step,(int)(halfheight+shift+old_y1),
			     i,(int)(halfheight+shift+y1), s_spectral_color);
			pSurfx->Line(i-step,(int)(halfheight-shift+old_y2),
			     i,(int)(halfheight-shift+y2), s_spectral_color);
			break;

		case 2:
			pSurfx->Line(i-step,(int)(halfheight+shift+old_y1),
			     i,(int)(halfheight+shift+y1), s_spectral_color);
			pSurfx->Line(i-step,(int)(halfheight-shift+old_y1),
			     i,(int)(halfheight-shift+y1), s_spectral_color);
			pSurfx->Line((int)(halfwidth+shift+old_y2),i-step,
			     (int)(halfwidth+shift+y2),i, s_spectral_color);
			pSurfx->Line((int)(halfwidth-shift+old_y2),i-step,
			     (int)(halfwidth-shift+y2),i, s_spectral_color);
			break;

		case 3:
			if (y1 < 0) y1 = 0;
			if (y2 < 0) y2 = 0;
			// FALL THROUGH

		case 4:
			pSurfx->Line(
				(int)(halfwidth  + s_cosw.f[i - step] * (shift + old_y1)),
				(int)(halfheight + s_sinw.f[i - step] * (shift + old_y1)),
				(int)(halfwidth  + s_cosw.f[i]        * (shift + y1)),
				(int)(halfheight + s_sinw.f[i]        * (shift + y1)),
				s_spectral_color);
			pSurfx->Line(
				(int)(halfwidth  - s_cosw.f[i - step] * (shift + old_y2)),
				(int)(halfheight + s_sinw.f[i - step] * (shift + old_y2)),
				(int)(halfwidth  - s_cosw.f[i]        * (shift + y2)),
				(int)(halfheight + s_sinw.f[i]        * (shift + y2)),
				s_spectral_color);
			break;
		}
	}

	if (s_mode_spectre == 3 || s_mode_spectre == 4)
	{
		pSurfx->Line(
			(int)(halfwidth  + s_cosw.f[pSurfx->m_nWidth - step] * (shift+y1)),
			(int)(halfheight + s_sinw.f[pSurfx->m_nWidth - step] * (shift+y1)),
			(int)(halfwidth  - s_cosw.f[pSurfx->m_nWidth - step] * (shift+y2)),
			(int)(halfheight + s_sinw.f[pSurfx->m_nWidth - step] * (shift+y2)),
			s_spectral_color);
	}

#define VIZ_CURVE_COLOR 255
#define VIZ_CURVE_AMPLITUDE 50
	static int x_curve = 0;

	// Curve overlay
	{
		int i, j, k;
		float v, vr;
		float x, y;
		float amplitude = (float)VIZ_CURVE_AMPLITUDE / 256;

		for (j = 0; j < 2; j += 1)
		{
			v = 80;
			vr = 0.001f;
			k = x_curve;
			for (i = 0; i < 64; i += 1)
			{
				x = cosf((float)(k) / (v + v * j * 1.34f)) * pSurfx->m_nHeight * amplitude;
				y = sinf((float)(k) / (1.756f * (v + v * j * 0.93f))) * pSurfx->m_nHeight * amplitude;
				VizSetPixel2(pSurfx,
					(int)(x * cosf((float)k * vr) + y * sinf((float)k * vr) + pSurfx->m_nWidth / 2),
					(int)(x * sinf((float)k * vr) - y * cosf((float)k * vr) + pSurfx->m_nHeight / 2),
					VIZ_CURVE_COLOR);
				k++;
			}
		}

		x_curve = k;
	}
}

// ============================================================================
// CMusicCollection - real SDL_mixer implementation
// Delegates to DashMusic_* functions in audio_sdl.cpp
// ============================================================================
class CMusicCollection : public CNode
{
public:
	CMusicCollection() : m_copyProgress(0.0f), m_error(0) {}

	DECLARE_NODE(CMusicCollection, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
	float m_copyProgress;
	int m_error;

	int GetSoundtrackCount() { return DashMusic_GetSoundtrackCount(); }
	int GetSoundtrackID(int index) { return DashMusic_GetSoundtrackID(index); }
	int GetSoundtrackIndexFromID(int id) { return DashMusic_GetSoundtrackIndexFromID(id); }

	CStrObject* GetSoundtrackName(int index) {
		return new CStrObject(_T(DashMusic_GetSoundtrackName(index)));
	}

	CStrObject* FormatSoundtrackTime(int stIndex) {
		// Sum all song durations in this soundtrack
		int total = 0;
		int count = DashMusic_GetSongCount(stIndex);
		for (int i = 0; i < count; i++)
			total += DashMusic_GetSongDuration(stIndex, i);
		return new CStrObject(_T(DashMusic_FormatTime(total)));
	}

	int GetSoundtrackSongCount(int stIndex) { return DashMusic_GetSongCount(stIndex); }
	int GetSoundtrackSongID(int stIndex, int songIndex) { return DashMusic_GetSongID(stIndex, songIndex); }

	CStrObject* GetSoundtrackSongName(int stIndex, int songIndex) {
		return new CStrObject(_T(DashMusic_GetSongName(stIndex, songIndex)));
	}

	CStrObject* FormatSoundtrackSongTime(int stIndex, int songIndex) {
		int dur = DashMusic_GetSongDuration(stIndex, songIndex);
		return new CStrObject(_T(DashMusic_FormatTime(dur)));
	}

	// Editing operations; no-ops for now (read-only collection)
	void AddSoundtrack(int) {}
	void DeleteSoundtrack(int) {}
	void ClearCopyList() {}
	void AddSongToCopyList(int, int) {}
	void StartCopy() {}
	void SetSongName(int, int, CStrObject*) {}
	void SetSoundtrackName(int, CStrObject*) {}
	void MoveSongUp(int, int) {}
	void MoveSongDown(int, int) {}
	void DeleteSong(int, int) {}
	CStrObject* CreateSoundtrackName() { return new CStrObject(_T("New Soundtrack")); }
	CStrObject* GetUpdateString() { return new CStrObject; }
};

IMPLEMENT_NODE("MusicCollection", CMusicCollection, CNode)

START_NODE_PROPS(CMusicCollection, CNode)
	NODE_PROP(pt_number, CMusicCollection, copyProgress)
	NODE_PROP(pt_integer, CMusicCollection, error)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CMusicCollection
START_NODE_FUN(CMusicCollection, CNode)
	NODE_FUN_IV(GetSoundtrackCount)
	NODE_FUN_II(GetSoundtrackID)
	NODE_FUN_II(GetSoundtrackIndexFromID)
	NODE_FUN_SI(GetSoundtrackName)
	NODE_FUN_SI(FormatSoundtrackTime)
	NODE_FUN_II(GetSoundtrackSongCount)
	NODE_FUN_III(GetSoundtrackSongID)
	NODE_FUN_SII(GetSoundtrackSongName)
	NODE_FUN_SII(FormatSoundtrackSongTime)
	NODE_FUN_VI(AddSoundtrack)
	NODE_FUN_VI(DeleteSoundtrack)
	NODE_FUN_VV(ClearCopyList)
	NODE_FUN_VII(AddSongToCopyList)
	NODE_FUN_VV(StartCopy)
	NODE_FUN_VIIS(SetSongName)
	NODE_FUN_VIS(SetSoundtrackName)
	NODE_FUN_VII(MoveSongUp)
	NODE_FUN_VII(MoveSongDown)
	NODE_FUN_VII(DeleteSong)
	NODE_FUN_SV(CreateSoundtrackName)
	NODE_FUN_SV(GetUpdateString)
END_NODE_FUN()

// ============================================================================
// CMemoryMonitor - from Memory.cpp
// ============================================================================
class CMemoryMonitor : public CNode
{
public:
	CMemoryMonitor() : m_curDevUnit(8), m_invalidDevUnit(0), m_blockInsertion(false), m_enumerationOn(false) {}

	DECLARE_NODE(CMemoryMonitor, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
	int m_curDevUnit, m_invalidDevUnit;
	bool m_blockInsertion, m_enumerationOn;

	// Navigation: on desktop, always stay on HDD (unit 8)
	void selectUp() { m_curDevUnit = 8; }
	void selectDown() { m_curDevUnit = 8; }
	void selectLeft() { m_curDevUnit = 8; }
	void selectRight() { m_curDevUnit = 8; }
	int selectDevUnit(int unit) { m_curDevUnit = 8; return 8; }
	CStrObject* FormatDeviceName(int) { return new CStrObject(_T("Xbox Hard Disk")); }
	CStrObject* FormatTotalBlocks() { return new CStrObject(_T("50000+")); }
	CStrObject* FormatFreeBlocks(int) { return new CStrObject(_T("50000+")); }
	CStrObject* FormatFreeSlots(int) { return new CStrObject(_T("50000+")); }
	int GetTotalFreeBlocks(int) { return 50000; }
	int GetTotalFreeSlots(int) { return 50000; }
	int HaveDevice(int n) { return (n == 8) ? 1 : 0; }  // Hard drive always present, no MUs
	int HaveDeviceTop(int) { return 0; }    // No MUs on desktop
	int HaveDeviceBottom(int) { return 0; }  // No MUs on desktop
	float GetFreeTotalRatio(int) { return 1.0f; }
	void SetMUName(int, CStrObject*) {}
	void FormatMemoryUnit(int) {}
};

IMPLEMENT_NODE("MemoryMonitor", CMemoryMonitor, CNode)

START_NODE_PROPS(CMemoryMonitor, CNode)
	NODE_PROP(pt_integer, CMemoryMonitor, curDevUnit)
	NODE_PROP(pt_integer, CMemoryMonitor, invalidDevUnit)
	NODE_PROP(pt_boolean, CMemoryMonitor, blockInsertion)
	NODE_PROP(pt_boolean, CMemoryMonitor, enumerationOn)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CMemoryMonitor
START_NODE_FUN(CMemoryMonitor, CNode)
	NODE_FUN_VV(selectUp)
	NODE_FUN_VV(selectDown)
	NODE_FUN_VV(selectLeft)
	NODE_FUN_VV(selectRight)
	NODE_FUN_II(selectDevUnit)
	NODE_FUN_SI(FormatDeviceName)
	NODE_FUN_SV(FormatTotalBlocks)
	NODE_FUN_SI(FormatFreeBlocks)
	NODE_FUN_SI(FormatFreeSlots)
	NODE_FUN_II(GetTotalFreeBlocks)
	NODE_FUN_II(GetTotalFreeSlots)
	NODE_FUN_II(HaveDevice)
	NODE_FUN_II(HaveDeviceTop)
	NODE_FUN_II(HaveDeviceBottom)
	NODE_FUN_NI(GetFreeTotalRatio)
	NODE_FUN_VIS(SetMUName)
	NODE_FUN_VI(FormatMemoryUnit)
END_NODE_FUN()

// ============================================================================
// CTheseusLauncher - from TheseusLauncher.cpp
// ============================================================================
class CTheseusLauncher : public CNode
{
public:
	CTheseusLauncher() {}

	DECLARE_NODE(CTheseusLauncher, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	void Launch(const TCHAR* path) { }
};

IMPLEMENT_NODE("TheseusLauncher", CTheseusLauncher, CNode)

START_NODE_PROPS(CTheseusLauncher, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CTheseusLauncher
START_NODE_FUN(CTheseusLauncher, CNode)
	NODE_FUN_VS(Launch)
END_NODE_FUN()

// ============================================================================
// CDiscDrive - from disc.cpp
// ============================================================================
class CDiscDrive : public CNode
{
public:
	static CDiscDrive* s_instance;

	CDiscDrive() : m_discType(NULL), m_locked(false), m_pollTimer(2.0f) {
		m_discType = strdup("none");
		s_instance = this;
	}

	DECLARE_NODE(CDiscDrive, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
	TCHAR* m_discType;
	bool   m_locked;
	float  m_pollTimer;

	void Advance(float nSeconds) {
		CNode::Advance(nSeconds);
		m_pollTimer += nSeconds;
		if (m_pollTimer < 2.0f) return;
		m_pollTimer = 0.0f;

		CdAudio_Poll();

		const char* newType;
		switch (CdAudio_GetDiscType()) {
			case CD_AUDIO: newType = "Audio"; break;
			default:       newType = "none";  break;
		}

		if (m_discType && strcmp(m_discType, newType) == 0) return;

		bool wasNone = !m_discType || strcmp(m_discType, "none") == 0;
		bool isNone  = strcmp(newType, "none") == 0;

		free(m_discType);
		m_discType = strdup(newType);

		if (!m_locked) {
			if (!isNone && wasNone)
				CallFunction(this, "OnDiscInserted");
			else if (isNone && !wasNone)
				CallFunction(this, "OnDiscRemoved");
		}
	}

	int getTrackCount() { return CdAudio_GetTrackCount(); }

	CStrObject* FormatTotalTime() {
		int total = CdAudio_GetTotalDurationSeconds();
		char buf[16];
		snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
		return new CStrObject(buf);
	}

	CStrObject* FormatTrackTime(int track) {
		int dur = CdAudio_GetTrackDurationSeconds(track + 1);  // script passes 0-based index
		char buf[16];
		snprintf(buf, sizeof(buf), "%d:%02d", dur / 60, dur % 60);
		return new CStrObject(buf);
	}

	void LaunchDisc() {
		// On real Xbox this reboots into the DVD player app.
		// On desktop, we intercept and just start the DVD inline player.
		fprintf(stderr, "[DiscDrive] LaunchDisc intercepted\n");
		CObject* pRoot = (CObject*)g_pObject;
		if (pRoot) {
			CallFunction(pRoot, "StartDVDPlayer");
		}
	}
	void OpenTray() {}
	void CloseTray() {}
	CStrObject* getArtist() { return new CStrObject; }
	CStrObject* getTitle()  { return new CStrObject; }
	CStrObject* getTrackName(int) { return new CStrObject; }
};

CDiscDrive* CDiscDrive::s_instance = nullptr;
IMPLEMENT_NODE("DiscDrive", CDiscDrive, CNode)

// Set the disc type from outside the XAP (used by menu_bar for simulation).
// Video: directly invokes StartDVDPlayer (bypasses XAP, matches prior desktop behaviour).
// Audio: fires the normal OnDiscInserted callback so the XAP music screen handles it.
// none : fires OnDiscRemoved (or navigates away from the DVD player if it was active).
void DiscDrive_SetDiscType(const char* type) {
	CDiscDrive* dd = CDiscDrive::s_instance;
	if (!dd) return;

	bool wasNone = !dd->m_discType || strcmp(dd->m_discType, "none") == 0;
	bool isNone  = strcmp(type, "none") == 0;

	if (dd->m_discType) free(dd->m_discType);
	dd->m_discType = strdup(type);

	if (isNone) {
		if (!wasNone) {
			CObject* pRoot = (CObject*)g_pObject;
			if (pRoot) CallFunction(pRoot, "GoToLauncher");
			fprintf(stderr, "[DiscDrive] discType='none', navigated to launcher\n");
		}
	} else if (strcmp(type, "Video") == 0) {
		CObject* pRoot = (CObject*)g_pObject;
		if (pRoot) {
			CallFunction(pRoot, "StartDVDPlayer");
			fprintf(stderr, "[DiscDrive] discType='Video', called StartDVDPlayer\n");
		}
	} else {
		// Audio CD (or anything else): let the XAP script decide via OnDiscInserted
		if (!dd->m_locked)
			CallFunction(dd, "OnDiscInserted");
		fprintf(stderr, "[DiscDrive] discType='%s', fired OnDiscInserted\n", type);
	}
}

START_NODE_PROPS(CDiscDrive, CNode)
	NODE_PROP(pt_string, CDiscDrive, discType)
	NODE_PROP(pt_boolean, CDiscDrive, locked)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CDiscDrive
START_NODE_FUN(CDiscDrive, CNode)
	NODE_FUN_IV(getTrackCount)
	NODE_FUN_SV(FormatTotalTime)
	NODE_FUN_SI(FormatTrackTime)
	NODE_FUN_VV(LaunchDisc)
	NODE_FUN_VV(OpenTray)
	NODE_FUN_VV(CloseTray)
	NODE_FUN_SV(getArtist)
	NODE_FUN_SV(getTitle)
	NODE_FUN_SI(getTrackName)
END_NODE_FUN()

// ============================================================================
// CSavedGameGrid - Reads from qcow2/FATX if available, falls back to xboxfs/
// ============================================================================

#include "xbox_hdd.h"
#include "media_player.h"
#include "panel_shared.h"  // for DashAudio_MuteAll/UnmuteAll
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/statvfs.h>
#endif

#define XBOX_BLOCK_SIZE 16384
#define SAVED_GAME_MAX_TITLES 256
#define SAVED_GAME_MAX_SAVES 64

struct DesktopSavedGame {
	char saveName[128];
	char folderName[64];
	uint64_t sizeBytes;
	time_t modTime;
};

struct DesktopTitle {
	char titleID[16];
	char titleName[128];
	char imagePath[512];
	bool hasImage;
	DesktopSavedGame saves[SAVED_GAME_MAX_SAVES];
	int saveCount;
	uint64_t totalSizeBytes;
};

static DesktopTitle s_titles[SAVED_GAME_MAX_TITLES];
static int s_titleCount = 0;
bool s_titlesEnumerated = false;  // non-static: reset by HDD browser when qcow path changes
static int s_totalBlocks = 0;
static int s_freeBlocks = 0;

// Global qcow2 HDD for reading saves directly from xemu images
static XboxHDD s_xboxHDD;
bool s_xboxHDDTried = false;  // non-static: reset by HDD browser when qcow path changes
static bool TryOpenXboxHDD();  // forward declaration

// Defined in sdl_main.cpp, loaded from desktop.ini
extern char g_qcowPath[512];

// Called from xboxfs.h CreateFileA fallback to read files from FATX
FILE* XboxFS_FATXReadFile(const char* xboxPath) {
	if (!s_xboxHDD.IsOpen()) {
		if (!s_xboxHDDTried) TryOpenXboxHDD();  // forward decl below
		if (!s_xboxHDD.IsOpen()) return nullptr;
	}

	// Parse Xbox path: "E:\UDATA\4d530004\TitleImage.xbx"
	if (!xboxPath || strlen(xboxPath) < 3 || xboxPath[1] != ':') return nullptr;

	char driveLetter = toupper(xboxPath[0]);

	// Find which partition this drive letter maps to
	int partIdx = -1;
	for (int i = 0; i < s_xboxHDD.GetPartitionCount(); i++) {
		for (int j = 0; j < XBOX_PARTITION_COUNT; j++) {
			if (XBOX_PARTITION_TABLE[j].letter == driveLetter) {
				// Find the partition with this offset
				FATXReader* p = s_xboxHDD.GetPartition(i);
				if (p && p->GetPartition().offset == XBOX_PARTITION_TABLE[j].offset) {
					partIdx = i;
					break;
				}
			}
		}
		if (partIdx >= 0) break;
	}
	if (partIdx < 0) return nullptr;

	FATXReader* part = s_xboxHDD.GetPartition(partIdx);
	if (!part) return nullptr;

	// Walk the path components: skip "E:\" then split on backslash
	const char* p = xboxPath + 3;  // skip "E:\"
	uint32_t currentCluster = 1;   // root directory

	// Split path and walk directories
	char pathCopy[512];
	strncpy(pathCopy, p, sizeof(pathCopy) - 1);
	pathCopy[sizeof(pathCopy) - 1] = 0;

	// Convert backslashes to forward slashes
	for (char* c = pathCopy; *c; c++) if (*c == '\\') *c = '/';

	char* component = pathCopy;
	char* nextSlash;

	while (component && *component) {
		nextSlash = strchr(component, '/');
		if (nextSlash) *nextSlash = 0;

		auto entries = part->ReadDirectory(currentCluster);
		bool found = false;
		for (const auto& e : entries) {
			if (strcasecmp(e.name, component) == 0) {
				if (!nextSlash) {
					// This is the target file
					if (e.IsFile() && e.fileSize > 0) {
						auto data = part->ReadFile(e.firstCluster, e.fileSize);
						if (!data.empty()) {
							FILE* tmp = tmpfile();
							if (tmp) {
								fwrite(data.data(), 1, data.size(), tmp);
								fseek(tmp, 0, SEEK_SET);
								return tmp;
							}
						}
					}
					return nullptr;
				}
				// Directory, keep walking
				if (e.IsDirectory()) {
					currentCluster = e.firstCluster;
					found = true;
				}
				break;
			}
		}
		if (!found && nextSlash) return nullptr;  // directory not found
		component = nextSlash ? nextSlash + 1 : nullptr;
	}
	return nullptr;
}

static bool TryOpenXboxHDD() {
	if (s_xboxHDDTried) return s_xboxHDD.IsOpen();
	s_xboxHDDTried = true;

	if (!g_qcowPath[0]) return false;

	if (s_xboxHDD.Open(g_qcowPath)) {
		fprintf(stderr, "[SavedGameGrid] Opened qcow2 HDD: %s\n", g_qcowPath);
		return true;
	}
	fprintf(stderr, "[SavedGameGrid] Failed to open qcow2: %s\n", g_qcowPath);
	return false;
}

// Enumerate titles from a mounted qcow2 FATX image
// Read a UTF-16LE .xbx meta file from FATX and extract a key's value
// Supports language sections: [en], [default], etc. (matches ParseXboxMetaValue behavior)
extern const char* GetLanguageCode(char* sz);

static bool ReadFATXMetaValue(FATXReader* part, uint32_t fileCluster, uint32_t fileSize,
                               const char* key, char* outBuf, int outBufSize) {
	outBuf[0] = 0;
	if (!part || fileCluster == 0 || fileSize == 0 || fileSize > 8192) return false;

	auto data = part->ReadFile(fileCluster, fileSize);
	if (data.empty()) return false;

	// Convert UTF-16LE to UTF-8 (skip BOM if present)
	std::string text;
	size_t start = 0;
	if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE)
		start = 2;  // skip UTF-16LE BOM
	for (size_t i = start; i + 1 < data.size(); i += 2) {
		uint16_t ch = data[i] | (data[i + 1] << 8);
		if (ch == 0 || ch == 0xFFFF) break;
		if (ch < 0x80) text += (char)ch;
		else if (ch < 0x800) {
			text += (char)(0xC0 | (ch >> 6));
			text += (char)(0x80 | (ch & 0x3F));
		} else {
			text += (char)(0xE0 | (ch >> 12));
			text += (char)(0x80 | ((ch >> 6) & 0x3F));
			text += (char)(0x80 | (ch & 0x3F));
		}
	}

	// Parse with language section support
	char szLang[8] = {};
	GetLanguageCode(szLang);
	size_t keyLen = strlen(key);
	std::string foundDefault;
	bool inTargetSection = false;
	bool inDefaultSection = true;

	size_t pos = 0;
	while (pos < text.size()) {
		size_t eol = text.find_first_of("\r\n", pos);
		if (eol == std::string::npos) eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;
		if (pos < text.size() && (text[pos] == '\r' || text[pos] == '\n')) pos++;

		if (line.empty()) continue;
		if (line[0] == '[') {
			size_t end = line.find(']');
			if (end != std::string::npos) {
				std::string section = line.substr(1, end - 1);
				inTargetSection = (strcasecmp(section.c_str(), szLang) == 0);
				inDefaultSection = (strcasecmp(section.c_str(), "default") == 0);
			}
			continue;
		}
		if (line.size() > keyLen && strncasecmp(line.c_str(), key, keyLen) == 0 && line[keyLen] == '=') {
			std::string val = line.substr(keyLen + 1);
			if (inTargetSection) {
				strncpy(outBuf, val.c_str(), outBufSize - 1);
				outBuf[outBufSize - 1] = 0;
				return true;
			}
			if (inDefaultSection && foundDefault.empty())
				foundDefault = val;
		}
	}

	if (!foundDefault.empty()) {
		strncpy(outBuf, foundDefault.c_str(), outBufSize - 1);
		outBuf[outBufSize - 1] = 0;
		return true;
	}
	return false;
}

// Calculate total size of all files in a FATX directory (non-recursive)
static uint64_t GetFATXDirectorySize(FATXReader* part, uint32_t dirCluster) {
	uint64_t total = 0;
	auto entries = part->ReadDirectory(dirCluster);
	for (const auto& e : entries) {
		if (e.IsFile()) total += e.fileSize;
		// Could recurse into subdirs but save folders are usually flat
	}
	return total;
}

static bool EnumerateTitlesFromFATX() {
	if (!TryOpenXboxHDD()) return false;

	int dataPart = s_xboxHDD.FindDataPartition();
	if (dataPart < 0) return false;

	FATXReader* part = s_xboxHDD.GetPartition(dataPart);
	if (!part) return false;

	// Find UDATA directory in root
	auto rootEntries = part->ReadRootDirectory();
	uint32_t udataCluster = 0;
	for (const auto& e : rootEntries) {
		if (e.IsDirectory() && strcasecmp(e.name, "UDATA") == 0) {
			udataCluster = e.firstCluster;
			break;
		}
	}
	if (udataCluster == 0) return false;

	// Enumerate title folders in UDATA/
	auto titleEntries = part->ReadDirectory(udataCluster);
	s_titleCount = 0;

	for (const auto& te : titleEntries) {
		if (!te.IsDirectory()) continue;
		if (s_titleCount >= SAVED_GAME_MAX_TITLES) break;

		DesktopTitle& dt = s_titles[s_titleCount];
		memset(&dt, 0, sizeof(dt));
		strncpy(dt.titleID, te.name, sizeof(dt.titleID) - 1);

		// Read title's contents: TitleMeta.xbx, TitleImage.xbx, save subdirs
		auto titleContents = part->ReadDirectory(te.firstCluster);

		for (const auto& fe : titleContents) {
			if (fe.IsFile() && strcasecmp(fe.name, "TitleMeta.xbx") == 0) {
				if (!ReadFATXMetaValue(part, fe.firstCluster, fe.fileSize,
				                        "TitleName", dt.titleName, sizeof(dt.titleName)))
					strncpy(dt.titleName, te.name, sizeof(dt.titleName) - 1);
			}
			if (fe.IsFile() && strcasecmp(fe.name, "TitleImage.xbx") == 0)
				dt.hasImage = true;
		}

		// If no title name was found, use the folder name
		if (!dt.titleName[0])
			strncpy(dt.titleName, te.name, sizeof(dt.titleName) - 1);

		// Enumerate save subdirectories
		dt.saveCount = 0;
		dt.totalSizeBytes = 0;
		for (const auto& fe : titleContents) {
			if (!fe.IsDirectory()) continue;
			if (dt.saveCount >= SAVED_GAME_MAX_SAVES) break;

			DesktopSavedGame& save = dt.saves[dt.saveCount];
			memset(&save, 0, sizeof(save));
			strncpy(save.folderName, fe.name, sizeof(save.folderName) - 1);

			// Read SaveMeta.xbx for the save name
			auto saveContents = part->ReadDirectory(fe.firstCluster);
			for (const auto& sf : saveContents) {
				if (sf.IsFile() && strcasecmp(sf.name, "SaveMeta.xbx") == 0) {
					ReadFATXMetaValue(part, sf.firstCluster, sf.fileSize,
					                   "Name", save.saveName, sizeof(save.saveName));
					break;
				}
			}
			if (!save.saveName[0])
				strncpy(save.saveName, fe.name, sizeof(save.saveName) - 1);

			save.sizeBytes = GetFATXDirectorySize(part, fe.firstCluster);
			dt.totalSizeBytes += save.sizeBytes;
			dt.saveCount++;
		}

		// Add the title directory's own files (TitleMeta.xbx, TitleImage.xbx, etc.)
		dt.totalSizeBytes += GetFATXDirectorySize(part, te.firstCluster);

		s_titleCount++;
	}

	// Sort alphabetically
	for (int i = 0; i < s_titleCount - 1; i++)
		for (int j = i + 1; j < s_titleCount; j++)
			if (strcasecmp(s_titles[i].titleName, s_titles[j].titleName) > 0)
				{ DesktopTitle tmp = s_titles[i]; s_titles[i] = s_titles[j]; s_titles[j] = tmp; }

	s_totalBlocks = 50000;
	s_freeBlocks = 50000;
	fprintf(stderr, "[SavedGameGrid] Enumerated %d titles from qcow2 FATX\n", s_titleCount);
	return true;
}

// Parse Xbox .xbx meta files (UTF-16LE INI format)
// Matches CSettingsFile::Open + GetValue behavior from Xbox source
// Desktop can't use CSettingsFile directly because _UNICODE isn't defined,
// so MakeUnicode() is compiled out and the raw UTF-16LE bytes aren't converted.
#include "settingsfile.h"

extern const char* GetLanguageCode(char* sz);

static bool ParseXboxMetaValue(const char* xboxPath, const char* key, char* outBuf, int outBufSize)
{
	outBuf[0] = 0;

	// Read file through xboxfs layer
	HANDLE hFile = CreateFile(xboxPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;
	unsigned char data[8192];
	DWORD bytesRead = 0;
	ReadFile(hFile, data, sizeof(data), &bytesRead, NULL);
	CloseHandle(hFile);
	if (bytesRead < 4) return false;

	// Convert UTF-16LE to UTF-8 (skip BOM)
	size_t pos = 0;
	if (data[0] == 0xFF && data[1] == 0xFE) pos = 2;
	char utf8[4096];
	int utf8Len = 0;
	for (size_t i = pos; i + 1 < bytesRead && utf8Len < (int)sizeof(utf8) - 4; i += 2) {
		uint16_t ch = data[i] | (data[i + 1] << 8);
		if (ch == 0) break;
		if (ch == 0x0D) continue;  // Skip CR
		if (ch < 0x80) utf8[utf8Len++] = (char)ch;
		else if (ch < 0x800) { utf8[utf8Len++] = (char)(0xC0|(ch>>6)); utf8[utf8Len++] = (char)(0x80|(ch&0x3F)); }
		else { utf8[utf8Len++] = (char)(0xE0|(ch>>12)); utf8[utf8Len++] = (char)(0x80|((ch>>6)&0x3F)); utf8[utf8Len++] = (char)(0x80|(ch&0x3F)); }
	}
	utf8[utf8Len] = 0;

	// Get language code for section lookup (matches Xbox: GetLanguageCode → section name)
	char szLangCode[8];
	GetLanguageCode(szLangCode);

	// Parse INI: search for key in language section, fall back to [default]
	// Matches Xbox CSettingsFile: values before any [section] go into "default"
	size_t keyLen = strlen(key);
	char* foundInDefault = NULL;
	bool inTargetSection = false;
	bool inDefaultSection = true;  // No section header = implicit [default]
	char* line = utf8;
	while (line && *line) {
		char* nextLine = strchr(line, '\n');
		if (nextLine) *nextLine = 0;

		if (line[0] == '[') {
			char* end = strchr(line, ']');
			if (end) {
				*end = 0;
				inTargetSection = (strcasecmp(line + 1, szLangCode) == 0);
				inDefaultSection = (strcasecmp(line + 1, "default") == 0);
			}
		}
		else if (strncmp(line, key, keyLen) == 0 && line[keyLen] == '=') {
			if (inTargetSection) {
				strncpy(outBuf, line + keyLen + 1, outBufSize - 1);
				outBuf[outBufSize - 1] = 0;
				return true;
			}
			if (inDefaultSection && !foundInDefault) {
				foundInDefault = line + keyLen + 1;
			}
		}

		line = nextLine ? nextLine + 1 : NULL;
	}

	// Fall back to [default] section
	if (foundInDefault) {
		strncpy(outBuf, foundInDefault, outBufSize - 1);
		outBuf[outBufSize - 1] = 0;
		return true;
	}
	return false;
}

static uint64_t GetDirectorySize(const char* dirPath)
{
	uint64_t total = 0;
#ifdef _WIN32
	char searchBuf[1024];
	snprintf(searchBuf, sizeof(searchBuf), "%s\\*", dirPath);
	struct _finddata_t fd;
	intptr_t hFind = _findfirst(searchBuf, &fd);
	if (hFind == -1) return 0;
	do {
		if (fd.name[0] == '.') continue;
		if (!(fd.attrib & _A_SUBDIR))
			total += fd.size;
	} while (_findnext(hFind, &fd) == 0);
	_findclose(hFind);
#else
	DIR* dir = opendir(dirPath);
	if (!dir) return 0;
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		char fullPath[1024];
		snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, ent->d_name);
		struct stat st;
		if (stat(fullPath, &st) == 0 && S_ISREG(st.st_mode))
			total += st.st_size;
	}
	closedir(dir);
#endif
	return total;
}

static void EnumerateTitles()
{
	if (s_titlesEnumerated) return;
	s_titlesEnumerated = true;
	s_titleCount = 0;

	// Try qcow2/FATX first
	if (EnumerateTitlesFromFATX()) return;

	// Fall back to xboxfs host directory
	const char* udataPath = "xboxfs/E/UDATA";

	// Helper lambda: process a single title directory entry
	auto ProcessTitleEntry = [&](const char* entName) {
		if (s_titleCount >= SAVED_GAME_MAX_TITLES) return;
		if (entName[0] == '.') return;
		char titlePath[1024];
		snprintf(titlePath, sizeof(titlePath), "%s/%s", udataPath, entName);
		struct stat st;
		if (stat(titlePath, &st) != 0 || !S_ISDIR(st.st_mode)) return;
		char metaPath[1024];
		snprintf(metaPath, sizeof(metaPath), "%s/TitleMeta.xbx", titlePath);
		if (access(metaPath, F_OK) != 0) return;
		DesktopTitle& title = s_titles[s_titleCount];
		memset(&title, 0, sizeof(title));
		strncpy(title.titleID, entName, sizeof(title.titleID) - 1);
		char xboxMetaPath[256];
		snprintf(xboxMetaPath, sizeof(xboxMetaPath), "E:\\UDATA\\%s\\TitleMeta.xbx", entName);
		if (!ParseXboxMetaValue(xboxMetaPath, "TitleName", title.titleName, sizeof(title.titleName)))
			strncpy(title.titleName, entName, sizeof(title.titleName) - 1);
		snprintf(title.imagePath, sizeof(title.imagePath), "%s/TitleImage.xbx", titlePath);
		title.hasImage = (access(title.imagePath, F_OK) == 0);
		title.saveCount = 0;
		title.totalSizeBytes = 0;
		// Enumerate saves within this title
#ifdef _WIN32
		char savSearchBuf[1024];
		snprintf(savSearchBuf, sizeof(savSearchBuf), "%s\\*", titlePath);
		struct _finddata_t savFd;
		intptr_t hSavFind = _findfirst(savSearchBuf, &savFd);
		if (hSavFind != -1) {
			do {
				if (savFd.name[0] == '.') continue;
				if (!(savFd.attrib & _A_SUBDIR)) continue;
				if (title.saveCount >= SAVED_GAME_MAX_SAVES) break;
				char savePath[1024];
				snprintf(savePath, sizeof(savePath), "%s/%s", titlePath, savFd.name);
				struct stat saveSt;
				if (stat(savePath, &saveSt) != 0) continue;
				DesktopSavedGame& save = title.saves[title.saveCount];
				memset(&save, 0, sizeof(save));
				strncpy(save.folderName, savFd.name, sizeof(save.folderName) - 1);
				char xboxSaveMetaPath[256];
				snprintf(xboxSaveMetaPath, sizeof(xboxSaveMetaPath), "E:\\UDATA\\%s\\%s\\SaveMeta.xbx", entName, savFd.name);
				if (!ParseXboxMetaValue(xboxSaveMetaPath, "Name", save.saveName, sizeof(save.saveName)))
					strncpy(save.saveName, savFd.name, sizeof(save.saveName) - 1);
				save.sizeBytes = GetDirectorySize(savePath);
				save.modTime = saveSt.st_mtime;
				title.totalSizeBytes += save.sizeBytes;
				title.saveCount++;
			} while (_findnext(hSavFind, &savFd) == 0);
			_findclose(hSavFind);
		}
#else
		DIR* titleDir = opendir(titlePath);
		if (titleDir) {
			struct dirent* saveEnt;
			while ((saveEnt = readdir(titleDir)) != NULL && title.saveCount < SAVED_GAME_MAX_SAVES) {
				if (saveEnt->d_name[0] == '.') continue;
				char savePath[1024];
				snprintf(savePath, sizeof(savePath), "%s/%s", titlePath, saveEnt->d_name);
				struct stat saveSt;
				if (stat(savePath, &saveSt) != 0 || !S_ISDIR(saveSt.st_mode)) continue;
				DesktopSavedGame& save = title.saves[title.saveCount];
				memset(&save, 0, sizeof(save));
				strncpy(save.folderName, saveEnt->d_name, sizeof(save.folderName) - 1);
				char xboxSaveMetaPath[256];
				snprintf(xboxSaveMetaPath, sizeof(xboxSaveMetaPath), "E:\\UDATA\\%s\\%s\\SaveMeta.xbx", entName, saveEnt->d_name);
				if (!ParseXboxMetaValue(xboxSaveMetaPath, "Name", save.saveName, sizeof(save.saveName)))
					strncpy(save.saveName, saveEnt->d_name, sizeof(save.saveName) - 1);
				save.sizeBytes = GetDirectorySize(savePath);
				save.modTime = saveSt.st_mtime;
				title.totalSizeBytes += save.sizeBytes;
				title.saveCount++;
			}
			closedir(titleDir);
		}
#endif
		title.totalSizeBytes += GetDirectorySize(titlePath);
		s_titleCount++;
	};

#ifdef _WIN32
	char searchBuf[1024];
	snprintf(searchBuf, sizeof(searchBuf), "%s\\*", udataPath);
	struct _finddata_t fd;
	intptr_t hFind = _findfirst(searchBuf, &fd);
	if (hFind == -1) { fprintf(stderr, "[SavedGameGrid] Cannot open %s\n", udataPath); return; }
	do { ProcessTitleEntry(fd.name); } while (_findnext(hFind, &fd) == 0);
	_findclose(hFind);
#else
	DIR* dir = opendir(udataPath);
	if (!dir) { fprintf(stderr, "[SavedGameGrid] Cannot open %s\n", udataPath); return; }
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) ProcessTitleEntry(ent->d_name);
	closedir(dir);
#endif
	// Sort alphabetically
	for (int i = 0; i < s_titleCount - 1; i++)
		for (int j = i + 1; j < s_titleCount; j++)
			if (strcasecmp(s_titles[i].titleName, s_titles[j].titleName) > 0)
				{ DesktopTitle tmp = s_titles[i]; s_titles[i] = s_titles[j]; s_titles[j] = tmp; }
#ifdef _WIN32
	{
		ULARGE_INTEGER freeBytesAvail, totalBytes;
		if (GetDiskFreeSpaceExA(udataPath, &freeBytesAvail, &totalBytes, NULL)) {
			s_totalBlocks = (int)(totalBytes.QuadPart / XBOX_BLOCK_SIZE);
			s_freeBlocks = (int)(freeBytesAvail.QuadPart / XBOX_BLOCK_SIZE);
		} else
			{ s_totalBlocks = 50000; s_freeBlocks = 50000; }
	}
#else
	struct statvfs vfs;
	if (statvfs(udataPath, &vfs) == 0) {
		s_totalBlocks = (int)((uint64_t)vfs.f_blocks * vfs.f_frsize / XBOX_BLOCK_SIZE);
		s_freeBlocks = (int)((uint64_t)vfs.f_bavail * vfs.f_frsize / XBOX_BLOCK_SIZE);
	} else
	{ s_totalBlocks = 50000; s_freeBlocks = 50000; }
#endif
	fprintf(stderr, "[SavedGameGrid] Enumerated %d titles from %s\n", s_titleCount, udataPath);
}

static void FormatBlockCount(char* buf, int bufSize, int blocks)
{
	if (blocks < 1000) snprintf(buf, bufSize, "%d", blocks);
	else if (blocks < 1000000) snprintf(buf, bufSize, "%d,%03d", blocks/1000, blocks%1000);
	else snprintf(buf, bufSize, "%d,%03d,%03d", blocks/1000000, (blocks/1000)%1000, blocks%1000);
}

static void FormatSaveTime(char* buf, int bufSize, time_t t)
{
	if (t == 0) { buf[0] = 0; return; }
	struct tm* tm = localtime(&t);
	if (tm) {
		const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
		snprintf(buf, bufSize, "%s %02d, %04d %02d:%02d", months[tm->tm_mon], tm->tm_mday, 1900+tm->tm_year, tm->tm_hour, tm->tm_min);
	} else buf[0] = 0;
}

extern const TCHAR *g_szSelTitleImage;
extern const char *g_szCurTitleImage;
extern float g_nEffectAlpha;

// ============================================================================
// Desktop icon database for harddrive.xap icon selector
// Reads Icons.ini and maps game folder names → icon.jpg paths
// This is separate from the UDATA enumeration used by the memory files grid
// ============================================================================
struct DesktopIconEntry {
	char id[128];          // lowercased ID from icons.ini value
	char iconPath[MAX_PATH]; // Xbox path: "E:\Games\FolderName\icon.jpg"
};
static DesktopIconEntry s_iconEntries[512];
static int s_iconCount = 0;
bool s_iconsLoaded = false;  // non-static: reset by Title Maker when games change

static void LoadDesktopIcons()
{
	if (s_iconsLoaded) return;
	s_iconsLoaded = true;
	s_iconCount = 0;

	// Build icon entries directly from VGames (games.ini) - no Icons.ini needed
	extern struct VirtualGameDB g_vgames;
	extern void VGames_Load();
	VGames_Load();

	for (int i = 0; i < g_vgames.count && s_iconCount < 512; i++) {
		if (!g_vgames.games[i].valid) continue;
		if (!g_vgames.games[i].titleID[0]) continue;

		// Check if icon file exists
		char iconPath[512];
		snprintf(iconPath, sizeof(iconPath), "xboxfs/C/UIX Configs/icons/%s.jpg", g_vgames.games[i].titleID);
		struct stat _ist;
		if (stat(iconPath, &_ist) != 0) {
			// Try .png
			snprintf(iconPath, sizeof(iconPath), "xboxfs/C/UIX Configs/icons/%s.png", g_vgames.games[i].titleID);
			if (stat(iconPath, &_ist) != 0) continue;
		}

		// Store Xbox-style path for VGames icon redirect in xboxfs.h
		snprintf(s_iconEntries[s_iconCount].iconPath, MAX_PATH,
			"%s:\\%s\\%s\\icon.jpg",
			g_vgames.games[i].drive,
			g_vgames.games[i].category[0] ? g_vgames.games[i].category : "Games",
			g_vgames.games[i].name);
		strncpy(s_iconEntries[s_iconCount].id, g_vgames.games[i].titleID, 127);
		s_iconEntries[s_iconCount].id[127] = 0;
		s_iconCount++;
	}
}

class CSavedGameGrid : public CNode
{
public:
	CSavedGameGrid() : m_pod(NULL), m_podRing(NULL), m_podXboxLiveAccountsPanel(NULL),
	                   m_XboxLiveAccountsIconPanel(NULL), m_iconBumpRing(NULL), m_iconArrow(NULL),
	                   m_iconCheck(NULL), m_curGridItem(-1), m_podSavePanel(NULL), m_podSoundtrackPanel(NULL),
	                   m_podHilite(NULL), m_MUheader(NULL), m_MUhiliteHeader(NULL), m_firstMURow(NULL),
	                   m_header(NULL), m_hiliteHeader(NULL), m_firstRow(NULL), m_secondRow(NULL),
	                   m_otherRow(NULL), m_renderIcons(false), m_iconsPerRow(4),
	                   m_scroll(0.0f), m_curTitle(-1), m_curSavedGame(-1), m_smallIcon(NULL), m_SavedIconPanel(NULL),
	                   m_SoundtrackIconPanel(NULL), m_iconRing(NULL), m_smallIconHilite(NULL),
	                   m_smallIconSpacing(0.0f), m_iconRowScroll(0), m_moreUp(NULL), m_moreDown(NULL),
	                   m_detachIcon(false), m_isActive(false), m_curDevUnit(-1),
	                   m_copyProgress(0.0f), m_freeBlocks(0), m_gameBlocks(0), m_nPrefColumn(0),
	                   m_deleteUserContent(false), m_enumDone(false),
	                   m_nScrollTo(0.0f), m_timeScroll(0.0f) {}

	DECLARE_NODE(CSavedGameGrid, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	// Properties
	CNode* m_pod;
	CNode* m_podRing;
	CNode* m_podXboxLiveAccountsPanel;
	CNode* m_XboxLiveAccountsIconPanel;
	CNode* m_iconBumpRing;
	CNode* m_iconArrow;
	CNode* m_iconCheck;
	int m_curGridItem;
	CNode* m_podSavePanel;
	CNode* m_podSoundtrackPanel;
	CNode* m_podHilite;
	CNode* m_MUheader;
	CNode* m_MUhiliteHeader;
	CNode* m_firstMURow;
	CNode* m_header;
	CNode* m_hiliteHeader;
	CNode* m_firstRow;
	CNode* m_secondRow;
	CNode* m_otherRow;
	int m_renderIcons;
	int m_iconsPerRow;
	float m_scroll;
	int m_curTitle;
	int m_curSavedGame;  // Alpha compat: save index within a title
	CNode* m_smallIcon;
	CNode* m_SavedIconPanel;
	CNode* m_SoundtrackIconPanel;
	CNode* m_iconRing;
	CNode* m_smallIconHilite;
	float m_smallIconSpacing;
	int m_iconRowScroll;
	CNode* m_moreUp;
	CNode* m_moreDown;
	bool m_detachIcon;
	bool m_isActive;
	int m_curDevUnit;
	float m_copyProgress;
	int m_freeBlocks;
	int m_gameBlocks;
	int m_nPrefColumn;
	bool m_deleteUserContent;
	bool m_enumDone;
	float m_nScrollTo;
	float m_timeScroll;

	void Advance(float dt)
	{
		CNode::Advance(dt);
		if (m_curDevUnit == -1 || !m_isActive)
			return;
		if (!m_enumDone) {
			m_enumDone = true;
			CallFunction(this, _T("OnUpdatingTitlesBegin"));
			EnumerateTitles();
			m_freeBlocks = s_freeBlocks;
			m_gameBlocks = 0;
			for (int i = 0; i < s_titleCount; i++)
				m_gameBlocks += (int)((s_titles[i].totalSizeBytes + XBOX_BLOCK_SIZE - 1) / XBOX_BLOCK_SIZE);
			CallFunction(this, _T("OnUpdatingTitlesEnd"));
		}
		// Smooth scroll animation (from Xbox Advance)
		if (m_timeScroll != 0.0f) {
			float t = (float)(TheseusGetNow() - m_timeScroll) / 0.25f;
			if (t >= 1.0f) { m_timeScroll = 0.0f; t = 1.0f; }
			m_scroll = (1.0f - t) * m_scroll + t * m_nScrollTo;
		}
	}

	// --- Rendering (adapted from Xbox CSavedGameGrid::Render/RenderLoop/RenderIconRow) ---

	static void RenderNodeAt(CNode* pNode, D3DXMATRIX* pMatrix)
	{
		if (!pNode) return;
		TheseusPushWorld();
		TheseusMultWorld(pMatrix);
		TheseusUpdateWorld();
		pNode->Render();
		TheseusPopWorld();
	}

	void RenderIconRow(D3DXMATRIX* pBaseMat, float y, int nTitle, int nFirstSave, int nTotalSaves)
	{
		if (!m_smallIcon || nTitle < 0 || nTitle >= s_titleCount) return;
		int nLim = nFirstSave + m_iconsPerRow;
		if (nLim > nTotalSaves) nLim = nTotalSaves;
		float x = 0.0f;
		for (int i = nFirstSave; i < nLim; i++) {
			D3DXMATRIX mat2;
			D3DXMatrixTranslation(&mat2, x, y, 0.0f);
			D3DXMatrixMultiply(&mat2, pBaseMat, &mat2);
			// Set title image for icon texture lookup
			// Try save-specific image first, fall back to title-level SaveImage.xbx
			static char szImgBuf[512];
			DesktopTitle& t = s_titles[nTitle];
			if (i < t.saveCount) {
				snprintf(szImgBuf, sizeof(szImgBuf), "E:\\UDATA\\%s\\%s\\SaveImage.xbx",
				         t.titleID, t.saves[i].folderName);
				// Check if save-specific image exists, fall back to title root
				char hostPath[512];
				snprintf(hostPath, sizeof(hostPath), "xboxfs/E/UDATA/%s/%s/SaveImage.xbx",
				         t.titleID, t.saves[i].folderName);
				struct stat _st;
				if (stat(hostPath, &_st) != 0) {
					snprintf(szImgBuf, sizeof(szImgBuf), "E:\\UDATA\\%s\\SaveImage.xbx", t.titleID);
				}
			} else {
				strcpy(szImgBuf, "xboxlogo64.xbx");
			}
			g_szCurTitleImage = szImgBuf;
			RenderNodeAt(m_SavedIconPanel, &mat2);
			g_szCurTitleImage = NULL;
			// Highlight ring on selected icon
			if (!m_detachIcon && m_iconRing && nTitle == m_curTitle && i == m_curGridItem)
				RenderNodeAt(m_iconRing, &mat2);
			x += m_smallIconSpacing;
		}
	}

	// RenderLoop: bRender=true draws, bRender=false returns Y offset of curTitle
	float RenderLoop(bool bRender)
	{
		D3DXMATRIX mat, mat2;
		D3DXVECTOR3 v(-1.0f, 0.0f, 0.0f);
		D3DXMatrixRotationAxis(&mat, &v, -1.571f);
		D3DXMatrixScaling(&mat2, 0.05942f, 0.05942f, 0.05942f);
		D3DXMatrixMultiply(&mat, &mat, &mat2);

		float y = m_scroll;
		float yLimit = -1.25f;
		int nTitleCount = s_titleCount;

		for (int nTitle = 0; nTitle < nTitleCount && (!bRender || y > yLimit); nTitle++) {
			float nEffectAlphaSave = g_nEffectAlpha;
			int nSaves = (nTitle >= 0 && nTitle < s_titleCount) ? s_titles[nTitle].saveCount : 0;
			int nRowCount = (nSaves + m_iconsPerRow - 1) / m_iconsPerRow;
			int nIconRowScroll = (nTitle == m_curTitle) ? m_iconRowScroll : 0;
			if (nTitle < m_curTitle) {
				nIconRowScroll = nRowCount - 3;
				if (nIconRowScroll < 0) nIconRowScroll = 0;
			}

			if (bRender) {
				// Dim non-selected titles
				float sel = (nTitle == m_curTitle) ? 1.0f : 0.0f;
				g_nEffectAlpha *= 0.5f + 0.5f * sel;
			} else if (nTitle == m_curTitle) {
				return y - m_scroll;
			}

			// Render title pod
			if (bRender && y - 0.25f < 0.5f && m_pod) {
				static char szPodImg[512];
				if (nTitle >= 0 && nTitle < s_titleCount && s_titles[nTitle].hasImage)
					snprintf(szPodImg, sizeof(szPodImg), "E:\\UDATA\\%s\\TitleImage.xbx", s_titles[nTitle].titleID);
				else
					strcpy(szPodImg, "xboxlogo128.xbx");
				g_szCurTitleImage = szPodImg;
				D3DXMatrixTranslation(&mat2, -0.3292f, y, -0.0271f);
				D3DXMatrixMultiply(&mat2, &mat, &mat2);
				RenderNodeAt(m_pod, &mat2);
				if (m_podSavePanel) RenderNodeAt(m_podSavePanel, &mat2);
				if (nTitle == m_curTitle && m_curGridItem == -1 && m_podHilite)
					RenderNodeAt(m_podRing, &mat2);
				g_szCurTitleImage = NULL;
			}

			// Render header
			if (bRender && y - 0.25f < 0.5f) {
				D3DXMatrixTranslation(&mat2, -0.3292f, y, -0.0271f);
				D3DXMatrixMultiply(&mat2, &mat, &mat2);
				if (nTitle == m_curTitle)
					RenderNodeAt(m_hiliteHeader, &mat2);
				else
					RenderNodeAt(m_header, &mat2);
				// Draw title name text
				if (m_renderIcons && nTitle >= 0 && nTitle < s_titleCount) {
					D3DXMATRIX mat3;
					D3DXMatrixScaling(&mat3, 0.06442f, 0.06442f, 0.06442f);
					D3DXMatrixTranslation(&mat2, -0.3292f - 0.05942f, y + 0.05942f, -0.0271f + (0.6f * 0.05942f));
					D3DXMatrixMultiply(&mat2, &mat3, &mat2);
					extern CNode* GetTextNode(const char* szText, float nWidth);
					RenderNodeAt(GetTextNode(s_titles[nTitle].titleName, -11.2f), &mat2);
				}
			}
			y -= 0.0092f;

			// Render first icon row
			if (bRender && y - 0.25f < 0.5f) {
				D3DXMatrixTranslation(&mat2, -0.3292f, y, 0.01144f);
				D3DXMatrixMultiply(&mat2, &mat, &mat2);
				RenderNodeAt(m_firstRow, &mat2);
				RenderIconRow(&mat, y, nTitle, nIconRowScroll * m_iconsPerRow, nSaves);
			}
			y -= 0.2455f;

			// Second row
			if (nSaves > m_iconsPerRow) {
				if (bRender && y - 0.25f < 0.5f) {
					D3DXMatrixTranslation(&mat2, -0.3301f, y, 0.01144f);
					D3DXMatrixMultiply(&mat2, &mat, &mat2);
					RenderNodeAt(m_secondRow, &mat2);
					RenderIconRow(&mat, y, nTitle, (nIconRowScroll + 1) * m_iconsPerRow, nSaves);
				}
				y -= 0.24499f;
				// Third+ rows
				if (nSaves > m_iconsPerRow * 2) {
					for (int nRow = 2; nRow < (nRowCount - nIconRowScroll) && nRow < 3 && (!bRender || y > yLimit); nRow++) {
						if (bRender && y - 0.25f < 0.5f) {
							D3DXMatrixTranslation(&mat2, -0.3412f, y, 0.01144f);
							D3DXMatrixMultiply(&mat2, &mat, &mat2);
							RenderNodeAt(m_otherRow, &mat2);
							RenderIconRow(&mat, y, nTitle, (nIconRowScroll + nRow) * m_iconsPerRow, nSaves);
						}
						y -= 0.24499f;
					}
				}
			}
			y -= 0.09f;
			g_nEffectAlpha = nEffectAlphaSave;
		}
		return 0.0f;
	}

	void Render()
	{
		if (!m_header || !m_firstRow || !m_secondRow || !m_otherRow) return;
		if (m_curDevUnit == -1 || !m_isActive) return;
		EnumerateTitles();
		if (s_titleCount == 0) return;
		RenderLoop(true);
	}

	bool OnSetProperty(const PRD* pprd, const void* pvValue)
	{
		// Match Xbox behavior: only react to specific property changes
		if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_curDevUnit))
		{
			m_curDevUnit = *(int*)pvValue;
			EnumerateTitles();
			if (m_curDevUnit == 8)
				SelectTitle(0);
			else
				SelectTitle(-1);
			return false;
		}
		else if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_curTitle))
		{
			SelectTitle(*(int*)pvValue);
			return false;
		}
		// All other properties: let base class handle
		return true;
	}

	void SelectTitle(int idx)
	{
		LoadDesktopIcons();
		int totalCount = s_titleCount + s_iconCount;
		if (idx < -1) idx = -1;
		if (idx >= totalCount) idx = totalCount - 1;
		m_curTitle = idx;
		m_curGridItem = -1;
		m_iconRowScroll = 0;
		// Compute scroll target for UDATA titles (icon entries don't render in grid)
		EnumerateTitles();
		if (idx >= 0 && idx < s_titleCount) {
			float y = -RenderLoop(false);
			m_nScrollTo = y;
			m_timeScroll = TheseusGetNow();
		}
		CallFunction(this, "OnSelChange");
	}

	// Navigation
	void selectUp()
	{
		EnumerateTitles();
		if (s_titleCount == 0) return;
		if (m_curTitle > 0) {
			SelectTitle(m_curTitle - 1);
		}
	}

	void selectDown()
	{
		EnumerateTitles();
		if (s_titleCount == 0) return;
		if (m_curTitle < s_titleCount - 1) {
			SelectTitle(m_curTitle + 1);
		}
	}

	void selectLeft()
	{
		EnumerateTitles();
		if (m_curTitle < 0 || m_curTitle >= s_titleCount) return;
		DesktopTitle& t = s_titles[m_curTitle];
		if (t.saveCount > 0 && m_curGridItem > 0) {
			m_curGridItem--;
			CallFunction(this, "OnSelChange");
		}
	}

	void selectRight()
	{
		EnumerateTitles();
		if (m_curTitle < 0 || m_curTitle >= s_titleCount) return;
		DesktopTitle& t = s_titles[m_curTitle];
		if (t.saveCount > 0 && m_curGridItem < t.saveCount - 1) {
			m_curGridItem++;
			CallFunction(this, "OnSelChange");
		}
	}

	void setSelImage()
	{
		EnumerateTitles();
		LoadDesktopIcons();
		static char imgPath[512];
		if (m_curTitle >= 0 && m_curTitle < s_titleCount && s_titles[m_curTitle].hasImage) {
			// UDATA title image
			snprintf(imgPath, sizeof(imgPath), "E:\\UDATA\\%s\\TitleImage.xbx", s_titles[m_curTitle].titleID);
			g_szSelTitleImage = imgPath;
		} else if (m_curTitle >= s_titleCount && m_curTitle < s_titleCount + s_iconCount) {
			// Desktop icon entry (from Icons.ini via VGames)
			g_szSelTitleImage = s_iconEntries[m_curTitle - s_titleCount].iconPath;
		}
	}

	// Format functions - return data about currently selected item
	CStrObject* FormatGridItemName()
	{
		EnumerateTitles();
		if (m_curTitle >= 0 && m_curTitle < s_titleCount) {
			DesktopTitle& t = s_titles[m_curTitle];
			if (m_curGridItem >= 0 && m_curGridItem < t.saveCount)
				return new CStrObject(t.saves[m_curGridItem].saveName);
			return new CStrObject(t.titleName);
		}
		return new CStrObject;
	}

	CStrObject* FormatGridItemTime()
	{
		EnumerateTitles();
		if (m_curTitle >= 0 && m_curTitle < s_titleCount) {
			DesktopTitle& t = s_titles[m_curTitle];
			if (m_curGridItem >= 0 && m_curGridItem < t.saveCount) {
				char buf[64];
				FormatSaveTime(buf, sizeof(buf), t.saves[m_curGridItem].modTime);
				return new CStrObject(buf);
			}
		}
		return new CStrObject;
	}

	CStrObject* FormatGridItemSize()
	{
		EnumerateTitles();
		if (m_curTitle >= 0 && m_curTitle < s_titleCount) {
			DesktopTitle& t = s_titles[m_curTitle];
			if (m_curGridItem >= 0 && m_curGridItem < t.saveCount) {
				int blocks = (int)((t.saves[m_curGridItem].sizeBytes + XBOX_BLOCK_SIZE - 1) / XBOX_BLOCK_SIZE);
				char buf[32];
				FormatBlockCount(buf, sizeof(buf), blocks);
				return new CStrObject(buf);
			}
		}
		return new CStrObject(_T("0"));
	}

	CStrObject* FormatTitleSize()
	{
		EnumerateTitles();
		if (m_curTitle >= 0 && m_curTitle < s_titleCount) {
			int blocks = (int)((s_titles[m_curTitle].totalSizeBytes + XBOX_BLOCK_SIZE - 1) / XBOX_BLOCK_SIZE);
			char buf[32];
			FormatBlockCount(buf, sizeof(buf), blocks);
			return new CStrObject(buf);
		}
		return new CStrObject(_T("0"));
	}

	CStrObject* FormatTotalBlocks()
	{
		EnumerateTitles();
		char buf[32];
		FormatBlockCount(buf, sizeof(buf), s_totalBlocks);
		return new CStrObject(buf);
	}

	int GetTotalBlocks()
	{
		EnumerateTitles();
		return s_totalBlocks;
	}

	CStrObject* FormatFreeBlocks()
	{
		EnumerateTitles();
		char buf[32];
		FormatBlockCount(buf, sizeof(buf), s_freeBlocks);
		return new CStrObject(buf);
	}

	int CanDetachIcon() { return 0; }

	int GetSavedGameCount(int titleIdx)
	{
		EnumerateTitles();
		if (titleIdx >= 0 && titleIdx < s_titleCount)
			return s_titles[titleIdx].saveCount;
		return 0;
	}

	CStrObject* GetTitleName(int titleIdx)
	{
		EnumerateTitles();
		if (titleIdx >= 0 && titleIdx < s_titleCount)
			return new CStrObject(s_titles[titleIdx].titleName);
		return new CStrObject;
	}

	CStrObject* GetTitleID(int titleIdx)
	{
		EnumerateTitles();
		LoadDesktopIcons();
		if (titleIdx >= 0 && titleIdx < s_titleCount)
			return new CStrObject(s_titles[titleIdx].titleID);
		// Desktop icon entries follow UDATA titles
		int iconIdx = titleIdx - s_titleCount;
		if (iconIdx >= 0 && iconIdx < s_iconCount)
			return new CStrObject(s_iconEntries[iconIdx].id);
		return new CStrObject;
	}

	void StartCopy(int) {}
	void StartSavedGameCopy(int) {}
	void StartXboxLiveAccountCopy(int) {}
	void StartDelete() {}

	int DoesSavedGameExists(int titleIdx)
	{
		EnumerateTitles();
		if (titleIdx >= 0 && titleIdx < s_titleCount)
			return s_titles[titleIdx].saveCount > 0 ? 1 : 0;
		return 0;
	}

	int DoesXboxLiveAccountExists(int) { return 0; }

	CStrObject* GetSavedGamePath(int titleIdx, int saveIdx)
	{
		EnumerateTitles();
		if (titleIdx >= 0 && titleIdx < s_titleCount) {
			DesktopTitle& t = s_titles[titleIdx];
			if (saveIdx >= 0 && saveIdx < t.saveCount) {
				char buf[256];
				snprintf(buf, sizeof(buf), "E:\\UDATA\\%s\\%s", t.titleID, t.saves[saveIdx].folderName);
				return new CStrObject(buf);
			}
		}
		return new CStrObject;
	}

	int CanCopy() { return 0; }
	int IsSoundtrackSelected() { return 0; }
	int IsDevUnitReady(int nUnit) { return (nUnit == 8) ? 1 : 0; }
	int IsXboxLiveAccountSelected() { return 0; }

	int IsSavedGameSelected()
	{
		EnumerateTitles();
		if (m_curTitle >= 0 && m_curTitle < s_titleCount) {
			DesktopTitle& t = s_titles[m_curTitle];
			if (m_curGridItem >= 0 && m_curGridItem < t.saveCount)
				return 1;
		}
		return 0;
	}

	int IsDLContentSelected() { return 0; }

	int IsGameTitleSelected()
	{
		return (m_curTitle >= 0 && m_curTitle < s_titleCount) ? 1 : 0;
	}

	int DoesUserContentExist()
	{
		EnumerateTitles();
		return s_titleCount > 0 ? 1 : 0;
	}

	int GetDLContentCount(int) { return 0; }
	int GetXboxLiveAccountsCount(int) { return 0; }

	int GetGridItemCount(int titleIdx)
	{
		EnumerateTitles();
		if (titleIdx >= 0 && titleIdx < s_titleCount)
			return s_titles[titleIdx].saveCount;
		return 0;
	}

	int GetTitleCount()
	{
		EnumerateTitles();
		LoadDesktopIcons();
		return s_titleCount + s_iconCount;
	}

	CStrObject* GetUpdateString() { return new CStrObject; }
};

IMPLEMENT_NODE("SavedGameGrid", CSavedGameGrid, CNode)

START_NODE_PROPS(CSavedGameGrid, CNode)
	NODE_PROP(pt_node, CSavedGameGrid, pod)
	NODE_PROP(pt_node, CSavedGameGrid, podRing)
	NODE_PROP(pt_node, CSavedGameGrid, podXboxLiveAccountsPanel)
	NODE_PROP(pt_node, CSavedGameGrid, XboxLiveAccountsIconPanel)
	NODE_PROP(pt_node, CSavedGameGrid, iconBumpRing)
	NODE_PROP(pt_node, CSavedGameGrid, iconArrow)
	NODE_PROP(pt_node, CSavedGameGrid, iconCheck)
	NODE_PROP(pt_integer, CSavedGameGrid, curGridItem)
	NODE_PROP(pt_node, CSavedGameGrid, podSavePanel)
	NODE_PROP(pt_node, CSavedGameGrid, podSoundtrackPanel)
	NODE_PROP(pt_node, CSavedGameGrid, podHilite)
	NODE_PROP(pt_node, CSavedGameGrid, MUheader)
	NODE_PROP(pt_node, CSavedGameGrid, MUhiliteHeader)
	NODE_PROP(pt_node, CSavedGameGrid, firstMURow)
	NODE_PROP(pt_node, CSavedGameGrid, header)
	NODE_PROP(pt_node, CSavedGameGrid, hiliteHeader)
	NODE_PROP(pt_node, CSavedGameGrid, firstRow)
	NODE_PROP(pt_node, CSavedGameGrid, secondRow)
	NODE_PROP(pt_node, CSavedGameGrid, otherRow)
	NODE_PROP(pt_integer, CSavedGameGrid, renderIcons)
	NODE_PROP(pt_integer, CSavedGameGrid, iconsPerRow)
	NODE_PROP(pt_number, CSavedGameGrid, scroll)
	NODE_PROP(pt_integer, CSavedGameGrid, curTitle)
	NODE_PROP(pt_integer, CSavedGameGrid, curSavedGame)
	NODE_PROP(pt_node, CSavedGameGrid, smallIcon)
	NODE_PROP(pt_node, CSavedGameGrid, SavedIconPanel)
	NODE_PROP(pt_node, CSavedGameGrid, SoundtrackIconPanel)
	NODE_PROP(pt_node, CSavedGameGrid, iconRing)
	NODE_PROP(pt_node, CSavedGameGrid, smallIconHilite)
	NODE_PROP(pt_number, CSavedGameGrid, smallIconSpacing)
	NODE_PROP(pt_integer, CSavedGameGrid, iconRowScroll)
	NODE_PROP(pt_node, CSavedGameGrid, moreUp)
	NODE_PROP(pt_node, CSavedGameGrid, moreDown)
	NODE_PROP(pt_boolean, CSavedGameGrid, detachIcon)
	NODE_PROP(pt_boolean, CSavedGameGrid, isActive)
	NODE_PROP(pt_integer, CSavedGameGrid, curDevUnit)
	NODE_PROP(pt_boolean, CSavedGameGrid, deleteUserContent)
	NODE_PROP(pt_number, CSavedGameGrid, copyProgress)
	NODE_PROP(pt_integer, CSavedGameGrid, freeBlocks)
	NODE_PROP(pt_integer, CSavedGameGrid, gameBlocks)
	NODE_PROP(pt_integer, CSavedGameGrid, nPrefColumn)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CSavedGameGrid
START_NODE_FUN(CSavedGameGrid, CNode)
	NODE_FUN_VV(selectUp)
	NODE_FUN_VV(selectDown)
	NODE_FUN_VV(selectLeft)
	NODE_FUN_VV(selectRight)
	NODE_FUN_VV(setSelImage)
	NODE_FUN_SV(FormatGridItemName)
	NODE_FUN_SV(FormatGridItemTime)
	NODE_FUN_SV(FormatGridItemSize)
	NODE_FUN_SV(FormatTitleSize)
	NODE_FUN_SV(FormatTotalBlocks)
	NODE_FUN_IV(GetTotalBlocks)
	NODE_FUN_SV(FormatFreeBlocks)
	NODE_FUN_IV(CanDetachIcon)
	NODE_FUN_II(GetSavedGameCount)
	NODE_FUN_SI(GetTitleName)
	NODE_FUN_SI(GetTitleID)
	NODE_FUN_VI(StartCopy)
	NODE_FUN_VV(StartDelete)
	NODE_FUN_VI(StartSavedGameCopy)
	NODE_FUN_VI(StartXboxLiveAccountCopy)
	NODE_FUN_II(DoesSavedGameExists)
	NODE_FUN_II(DoesXboxLiveAccountExists)
	NODE_FUN_SII(GetSavedGamePath)
	NODE_FUN_IV(CanCopy)
	NODE_FUN_IV(IsSoundtrackSelected)
	NODE_FUN_II(IsDevUnitReady)
	NODE_FUN_IV(IsXboxLiveAccountSelected)
	NODE_FUN_IV(IsSavedGameSelected)
	NODE_FUN_IV(IsDLContentSelected)
	NODE_FUN_IV(IsGameTitleSelected)
	NODE_FUN_IV(DoesUserContentExist)
	NODE_FUN_II(GetDLContentCount)
	NODE_FUN_II(GetXboxLiveAccountsCount)
	NODE_FUN_II(GetGridItemCount)
	NODE_FUN_IV(GetTitleCount)
	NODE_FUN_SV(GetUpdateString)
END_NODE_FUN()

// ============================================================================
// CHardDrive - from Harddrive.cpp
// ============================================================================
class CHardDrive : public CNode
{
public:
	CHardDrive() : m_BytesToCopy(0), m_BytesCopied(0), m_FilesToCopy(0), m_FilesCopied(0) {}

	DECLARE_NODE(CHardDrive, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	int m_BytesToCopy, m_BytesCopied;
	int m_FilesToCopy, m_FilesCopied;

	int GetFreeSpace(const TCHAR*) { return 0; }
	int GetTotalSpace(const TCHAR*) { return 0; }
	int ConvertMBToGB(int) { return 0; }
	int FileExists(const TCHAR*) { return 0; }
	int GetThisFileSize(const TCHAR*) { return 0; }
	void MoveThisFile(const TCHAR*, const TCHAR*) {}
	void MoveThisDirectory(const TCHAR*, const TCHAR*) {}
	int DeleteThisFile(const TCHAR*) { return 0; }
	int DeleteThisDirectory(const TCHAR*) { return 0; }
	void CopyThisFile(const TCHAR*, const TCHAR*) {}
	void CopyThisDirectory(const TCHAR*, const TCHAR*) {}
	void RenameThisFile(const TCHAR*, const TCHAR*) {}
	void RenameThisDirectory(const TCHAR*, const TCHAR*) {}
	int CreateThisDirectory(const TCHAR*) { return 0; }
	int RemoveThisDirectory(const TCHAR*) { return 0; }
	int CopyGame(const TCHAR*) { return 0; }
	int GetBytesToCopy() { return 0; }
	int GetBytesCopied() { return 0; }
};

IMPLEMENT_NODE("HardDrive", CHardDrive, CNode)

START_NODE_PROPS(CHardDrive, CNode)
	NODE_PROP(pt_integer, CHardDrive, BytesToCopy)
	NODE_PROP(pt_integer, CHardDrive, BytesCopied)
	NODE_PROP(pt_integer, CHardDrive, FilesToCopy)
	NODE_PROP(pt_integer, CHardDrive, FilesCopied)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CHardDrive
START_NODE_FUN(CHardDrive, CNode)
	NODE_FUN_IS(GetFreeSpace)
	NODE_FUN_IS(GetTotalSpace)
	NODE_FUN_II(ConvertMBToGB)
	NODE_FUN_IS(FileExists)
	NODE_FUN_IS(GetThisFileSize)
	NODE_FUN_VSS(MoveThisFile)
	NODE_FUN_VSS(MoveThisDirectory)
	NODE_FUN_IS(DeleteThisFile)
	NODE_FUN_IS(DeleteThisDirectory)
	NODE_FUN_VSS(CopyThisFile)
	NODE_FUN_VSS(CopyThisDirectory)
	NODE_FUN_VSS(RenameThisFile)
	NODE_FUN_VSS(RenameThisDirectory)
	NODE_FUN_IS(CreateThisDirectory)
	NODE_FUN_IS(RemoveThisDirectory)
	NODE_FUN_IS(CopyGame)
	NODE_FUN_IV(GetBytesToCopy)
	NODE_FUN_IV(GetBytesCopied)
END_NODE_FUN()

// ============================================================================
// CCopyDestination - from CopyDest.cpp
// ============================================================================
class CCopyDestination : public CNode
{
public:
	CCopyDestination() : m_pod(NULL), m_panelMU(NULL), m_panelMUHilite(NULL),
	    m_panelText(NULL), m_panelTextHilite(NULL), m_console(NULL), m_memoryUnit(NULL),
	    m_curDevUnit(0), m_selDevUnit(0), m_sourceDevUnit(0), m_spacing(0.0f),
	    m_isActive(false), m_select(0) {}

	DECLARE_NODE(CCopyDestination, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	CNode* m_pod;
	CNode* m_panelMU;
	CNode* m_panelMUHilite;
	CNode* m_panelText;
	CNode* m_panelTextHilite;
	CNode* m_console;
	CNode* m_memoryUnit;
	int m_curDevUnit, m_selDevUnit, m_sourceDevUnit;
	float m_spacing;
	bool m_isActive;
	int m_select;

	void selectUp() {}
	void selectDown() {}
};

IMPLEMENT_NODE("CopyDestination", CCopyDestination, CNode)

START_NODE_PROPS(CCopyDestination, CNode)
	NODE_PROP(pt_node, CCopyDestination, pod)
	NODE_PROP(pt_node, CCopyDestination, panelMU)
	NODE_PROP(pt_node, CCopyDestination, panelMUHilite)
	NODE_PROP(pt_node, CCopyDestination, panelText)
	NODE_PROP(pt_node, CCopyDestination, panelTextHilite)
	NODE_PROP(pt_node, CCopyDestination, console)
	NODE_PROP(pt_node, CCopyDestination, memoryUnit)
	NODE_PROP(pt_integer, CCopyDestination, curDevUnit)
	NODE_PROP(pt_integer, CCopyDestination, selDevUnit)
	NODE_PROP(pt_integer, CCopyDestination, sourceDevUnit)
	NODE_PROP(pt_number, CCopyDestination, spacing)
	NODE_PROP(pt_boolean, CCopyDestination, isActive)
	NODE_PROP(pt_integer, CCopyDestination, select)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CCopyDestination
START_NODE_FUN(CCopyDestination, CNode)
	NODE_FUN_VV(selectUp)
	NODE_FUN_VV(selectDown)
END_NODE_FUN()

// ============================================================================
// CXboxLive - from XboxLive.cpp
// ============================================================================
class CXboxLive : public CNode
{
public:
	CXboxLive() {}

	DECLARE_NODE(CXboxLive, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	CStrObject* GetGamertag() { return new CStrObject; }
	CStrObject* GetXUID() { return new CStrObject; }
	CStrObject* GetDomain() { return new CStrObject; }
	CStrObject* GetRealm() { return new CStrObject; }
	int IsVerified() { return 0; }
};

IMPLEMENT_NODE("XboxLive", CXboxLive, CNode)

START_NODE_PROPS(CXboxLive, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CXboxLive
START_NODE_FUN(CXboxLive, CNode)
	NODE_FUN_SV(GetGamertag)
	NODE_FUN_SV(GetXUID)
	NODE_FUN_SV(GetDomain)
	NODE_FUN_SV(GetRealm)
	NODE_FUN_IV(IsVerified)
END_NODE_FUN()

// ============================================================================
// CLiveAccounts - from LiveAccounts.cpp
// ============================================================================
class CLiveAccounts : public CNode
{
public:
	CLiveAccounts() : m_nCurrentIndex(0), m_bLogon(false), m_fLogOnSuccess(false), m_fLogOnInProgress(false) {}

	DECLARE_NODE(CLiveAccounts, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	int m_nCurrentIndex;
	bool m_bLogon, m_fLogOnSuccess, m_fLogOnInProgress;

	int GetNumberOfAccounts() { return 0; }
	int GetNumAccountsOnHD() { return 0; }
	CStrObject* GetAccountName(int) { return new CStrObject; }
	void Refresh() {}
	CStrObject* GetMessageOfTheDayText() { return new CStrObject; }
	void ShowIcon(int) {}
	void ClearMOTDCache() {}
	void ClearLastLogonUser() {}
	void Logon(int, const TCHAR*) {}
	void Logoff() {}
	int IsPasswordVerified() { return 0; }
	int IsBackFromEntryPoint() { return 0; }
	void PersistUser(int) {}
	int IsVoiceAllowed() { return 0; }
	CStrObject* GetGameInvites() { return new CStrObject; }
	CStrObject* GetFriendInvites() { return new CStrObject; }
	CStrObject* GetNumberOfFriendsOnline() { return new CStrObject(_T("0")); }
	void LaunchEntryPoint(int, int, const TCHAR*) {}
};

IMPLEMENT_NODE("LiveAccounts", CLiveAccounts, CNode)

START_NODE_PROPS(CLiveAccounts, CNode)
	NODE_PROP(pt_boolean, CLiveAccounts, bLogon)
	NODE_PROP(pt_boolean, CLiveAccounts, fLogOnSuccess)
	NODE_PROP(pt_boolean, CLiveAccounts, fLogOnInProgress)
	NODE_PROP(pt_integer, CLiveAccounts, nCurrentIndex)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CLiveAccounts
START_NODE_FUN(CLiveAccounts, CNode)
	NODE_FUN_IV(GetNumberOfAccounts)
	NODE_FUN_IV(GetNumAccountsOnHD)
	NODE_FUN_SI(GetAccountName)
	NODE_FUN_VV(Refresh)
	NODE_FUN_SV(GetMessageOfTheDayText)
	NODE_FUN_VI(ShowIcon)
	NODE_FUN_VV(ClearMOTDCache)
	NODE_FUN_VV(ClearLastLogonUser)
	NODE_FUN_VIS(Logon)
	NODE_FUN_VV(Logoff)
	NODE_FUN_IV(IsPasswordVerified)
	NODE_FUN_IV(IsBackFromEntryPoint)
	NODE_FUN_VI(PersistUser)
	NODE_FUN_IV(IsVoiceAllowed)
	NODE_FUN_SV(GetGameInvites)
	NODE_FUN_SV(GetFriendInvites)
	NODE_FUN_SV(GetNumberOfFriendsOnline)
	NODE_FUN_VIIS(LaunchEntryPoint)
END_NODE_FUN()

// ============================================================================
// CDVDPlayer - from dvd2.cpp
// ============================================================================
class CDVDPlayer : public CNode
{
public:
	CDVDPlayer() : m_speed(0.0f), m_title(0), m_chapter(0), m_hours(0), m_minutes(0),
	    m_seconds(0), m_frames(0), m_videoModePreferrence(0), m_parentalLevel(0),
	    m_audioStream(0), m_left(0), m_top(0), m_width(0), m_height(0),
	    m_closedCaption(false), m_subTitle(0), m_domain(0), m_angle(0), m_angleCount(0),
	    m_playbackMode(0), m_audioFormat(0), m_audioChannels(0), m_audioLanguage(NULL),
	    m_subTitleLanguage(NULL), m_abRepeatState(0), m_number(0), m_scanSpeed(1),
	    m_scanSlow(false), m_autoStop(false), m_bScanBackward(false),
	    m_backScanAccum(0.0f), m_digitAccum(0), m_digitTimer(0.0f),
	    m_zoomScale(1.0f), m_zoomX(0.0f), m_zoomY(0.0f),
	    m_osdTimer(0.0f), m_lastOsdSeconds(-1) {}

	DECLARE_NODE(CDVDPlayer, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	float m_speed;
	int m_title, m_chapter, m_hours, m_minutes, m_seconds, m_frames;
	int m_videoModePreferrence, m_parentalLevel, m_audioStream;
	int m_left, m_top, m_width, m_height;
	bool m_closedCaption;
	int m_subTitle, m_domain, m_angle, m_angleCount;
	int m_playbackMode, m_audioFormat, m_audioChannels;
	TCHAR* m_audioLanguage;
	TCHAR* m_subTitleLanguage;
	int m_abRepeatState, m_number, m_scanSpeed;
	bool m_scanSlow, m_autoStop, m_bScanBackward;

	// Internal state for scan/zoom/digit/OSD
	float m_backScanAccum;
	int m_digitAccum;
	float m_digitTimer;
	float m_zoomScale, m_zoomX, m_zoomY;
	float m_osdTimer;       // throttle OnTimeChange
	int m_lastOsdSeconds;   // detect second change

	// Update time + track properties from media player
	void UpdateFromMediaPlayer() {
		double pos = MediaPlayer_GetPosition();
		int totalSecs = (int)pos;
		m_hours = totalSecs / 3600;
		m_minutes = (totalSecs % 3600) / 60;
		m_seconds = totalSecs % 60;
		m_frames = (int)((pos - totalSecs) * 30.0);
		m_speed = (float)MediaPlayer_GetSpeed();
		m_chapter = MediaPlayer_GetChapter();
		m_title = 1;  // always "title 1" for file playback

		// Track info (cheap cached values, no mpv calls)
		m_audioFormat = MediaPlayer_GetAudioFormat();
		m_audioChannels = MediaPlayer_GetAudioChannels();
		m_subTitle = MediaPlayer_GetSubtitleTrack();
	}

	// Query language strings from mpv (expensive, call sparingly)
	void UpdateTrackLanguages() {
		const char* aLang = MediaPlayer_GetAudioLanguage();
		if (aLang && aLang[0]) {
			free(m_audioLanguage);
			m_audioLanguage = strdup(aLang);
		}
		const char* sLang = MediaPlayer_GetSubtitleLanguage();
		if (sLang && sLang[0]) {
			free(m_subTitleLanguage);
			m_subTitleLanguage = strdup(sLang);
		}
	}

	void Advance(float dt) {
		CNode::Advance(dt);
		MediaPlayerState mpState = MediaPlayer_GetState();

		if (mpState == MP_PLAYING || mpState == MP_PAUSED) {
			UpdateFromMediaPlayer();

			// Backward scan: periodically seek backward since mpv can't play in reverse
			if (m_playbackMode == 5 && m_bScanBackward) {
				m_backScanAccum += dt;
				if (m_backScanAccum >= 0.25f) {
					MediaPlayer_SeekRelative(-(double)m_scanSpeed * m_backScanAccum);
					m_backScanAccum = 0.0f;
				}
			}

			// Digit entry timeout: auto-execute after 2 seconds
			if (m_digitTimer > 0.0f) {
				m_digitTimer -= dt;
				if (m_digitTimer <= 0.0f && m_number > 0) {
					// Jump to chapter
					MediaPlayer_Seek(0);  // reset
					for (int i = 1; i < m_number; i++)
						MediaPlayer_NextChapter();
					m_number = 0;
					m_digitAccum = 0;
				}
			}

			// Determine playback mode for XAP
			// Don't override if we're in scanning (5) or trick play (8)
			if (m_playbackMode != 5 && m_playbackMode != 8) {
				int newMode = (mpState == MP_PLAYING) ? 3 : 1;
				if (m_playbackMode != newMode) {
					m_playbackMode = newMode;
					m_domain = 3;  // TT_DOM (title domain, enables OSD)
					m_visible = true;
					CallFunction(this, "OnPlaybackModeChange");
				}
			}

			// Ensure we stay visible while playing
			if (!m_visible) m_visible = true;

			// Fire time change callback only when the displayed second changes
			int curSec = m_hours * 3600 + m_minutes * 60 + m_seconds;
			if (curSec != m_lastOsdSeconds) {
				m_lastOsdSeconds = curSec;
				CallFunction(this, "OnTimeChange");
			}
		} else if (mpState == MP_STOPPED && m_playbackMode != 0) {
			m_playbackMode = 0;
			m_scanSpeed = 1;
			m_scanSlow = false;
			m_bScanBackward = false;
			m_title = 0;
			m_domain = 0;
			CallFunction(this, "OnPlaybackModeChange");
		}

		// Sync A-B repeat state
		m_abRepeatState = MediaPlayer_GetABRepeatState();
	}

	void Render() {
		if (!MediaPlayer_HasVideo()) return;
		extern void MediaPlayer_RenderToScreen(int w, int h);
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		MediaPlayer_RenderToScreen(viewport[2], viewport[3]);
	}

	// === Transport ===
	void init() {}

	void play() {
		// Reset scan state on explicit play
		if (m_scanSpeed > 1 || m_bScanBackward) {
			m_scanSpeed = 1;
			m_scanSlow = false;
			m_bScanBackward = false;
			MediaPlayer_SetSpeed(1.0);
		}
		MediaPlayer_Play();
		m_playbackMode = 3;  // DPM_PLAYING
	}

	void stop() {
		m_scanSpeed = 1;
		m_scanSlow = false;
		m_bScanBackward = false;
		MediaPlayer_SetSpeed(1.0);
		MediaPlayer_ClearABLoop();
		MediaPlayer_SetZoom(1.0);
		MediaPlayer_SetZoomPos(0, 0);
		m_zoomScale = 1.0f;
		m_zoomX = m_zoomY = 0.0f;
		MediaPlayer_Stop();
	}

	void pause() { MediaPlayer_Pause(); }
	void playOrPause() {
		// If scanning, stop scan and resume normal playback
		if (m_playbackMode == 5 || m_playbackMode == 8 || m_scanSpeed > 1 || m_bScanBackward) {
			m_scanSpeed = 1;
			m_scanSlow = false;
			m_bScanBackward = false;
			m_backScanAccum = 0.0f;
			MediaPlayer_SetSpeed(1.0);
			MediaPlayer_Play();
			m_playbackMode = 3;  // DPM_PLAYING
			return;
		}
		MediaPlayer_TogglePause();
	}
	void resume() { MediaPlayer_Play(); }

	// === Scanning (fast forward/rewind with speed ramp) ===
	void forwardScan() {
		bool wasPaused = (m_playbackMode == 1);  // DPM_PAUSED

		if (wasPaused) {
			// Slow motion: 1/2x, 1/4x, 1/8x, 1/16x
			m_scanSlow = true;
			m_bScanBackward = false;
			if (m_scanSpeed <= 1) m_scanSpeed = 2;
			else if (m_scanSpeed < 16) m_scanSpeed *= 2;
			else m_scanSpeed = 2;
			MediaPlayer_SetSpeed(1.0 / m_scanSpeed);
			MediaPlayer_Play();
		} else {
			// Fast forward: 2x, 4x, 8x, 16x, 32x
			m_scanSlow = false;
			m_bScanBackward = false;
			if (!m_bScanBackward && m_scanSpeed > 1 && m_scanSpeed < 32)
				m_scanSpeed *= 2;
			else
				m_scanSpeed = 2;
			MediaPlayer_SetSpeed((double)m_scanSpeed);
		}
		m_playbackMode = 5;  // DPM_SCANNING
	}

	void backwardScan() {
		bool wasPaused = (m_playbackMode == 1);

		if (wasPaused) {
			// Slow reverse: 1/2x, 1/4x, 1/8x, 1/16x backward
			m_scanSlow = true;
			m_bScanBackward = true;
			if (m_scanSpeed <= 1) m_scanSpeed = 2;
			else if (m_scanSpeed < 16) m_scanSpeed *= 2;
			else m_scanSpeed = 2;
			// mpv can't play in reverse, we simulate in Advance()
			MediaPlayer_Pause();
		} else {
			// Fast rewind: 2x, 4x, 8x, 16x, 32x backward
			m_scanSlow = false;
			m_bScanBackward = true;
			if (m_bScanBackward && m_scanSpeed > 1 && m_scanSpeed < 32)
				m_scanSpeed *= 2;
			else
				m_scanSpeed = 2;
			// Pause mpv, we'll seek backward in Advance()
			MediaPlayer_Pause();
		}
		m_backScanAccum = 0.0f;
		m_playbackMode = 5;  // DPM_SCANNING
	}

	void stopScan() {
		m_scanSpeed = 1;
		m_scanSlow = false;
		m_bScanBackward = false;
		m_backScanAccum = 0.0f;
		MediaPlayer_SetSpeed(1.0);
		MediaPlayer_Play();
		m_playbackMode = 3;  // DPM_PLAYING
	}

	// === Frame stepping ===
	void frameAdvance() {
		MediaPlayer_FrameStep();
		m_playbackMode = 8;  // DPM_TRICKPLAY
	}

	void frameReverse() {
		MediaPlayer_FrameBackStep();
		m_playbackMode = 8;  // DPM_TRICKPLAY
	}

	// === Chapter navigation ===
	void startChapter() { MediaPlayer_Seek(0); }
	void nextChapter() { MediaPlayer_NextChapter(); }
	void prevChapter() { MediaPlayer_PrevChapter(); }

	// === Track selection ===
	void nextAudioStream() {
		MediaPlayer_NextAudioTrack();
		UpdateTrackLanguages();  // refresh language strings after track change
		CallFunction(this, "OnAudioChange");
	}

	void nextSubtitle() {
		MediaPlayer_NextSubtitleTrack();
		UpdateTrackLanguages();  // refresh language strings after track change
		CallFunction(this, "OnSubTitleChange");
	}

	void nextAngle() {} // N/A for file playback

	// === DVD menu navigation (no-ops for file playback) ===
	void selectUp() {}
	void selectDown() {}
	void selectRight() {}
	void selectLeft() {}
	void activate() {}
	void goUp() {}
	void menu() {}
	void titleMenu() {}

	// === Zoom ===
	void setScale(float scale) {
		m_zoomScale = scale;
		MediaPlayer_SetZoom((double)scale);
	}

	void setZoomPos(float x, float y) {
		m_zoomX = x;
		m_zoomY = y;
		// Xbox range is -1..1, mpv video-pan range is -1..1 (scaled by zoom)
		MediaPlayer_SetZoomPos((double)x, (double)y);
	}

	// === A-B repeat ===
	void abRepeat() {
		if (m_abRepeatState == 0) {
			MediaPlayer_SetABLoopA();
			m_abRepeatState = 1;
		} else if (m_abRepeatState == 1) {
			MediaPlayer_SetABLoopB();
			m_abRepeatState = 2;
		} else {
			MediaPlayer_ClearABLoop();
			m_abRepeatState = 0;
		}
	}

	// === Digit entry (chapter jump) ===
	void digit(int d) {
		if (d == -1) {
			// Clear
			m_number = 0;
			m_digitAccum = 0;
			m_digitTimer = 0.0f;
			return;
		}
		m_digitAccum = m_digitAccum * 10 + d;
		m_number = m_digitAccum;
		m_digitTimer = 2.0f;  // auto-execute after 2 seconds
	}

	// === Eject (stop + unmute) ===
	void eject() {
		stop();
		DashAudio_UnmuteAll();
	}

	// === UOP (User Operation Permitted) ===
	int isUOPValid(int) { return 1; }  // all operations allowed for file playback

	int isPlaybackDomain() {
		MediaPlayerState st = MediaPlayer_GetState();
		return (st == MP_PLAYING || st == MP_PAUSED) ? 1 : 0;
	}

	// === Widescreen ===
	void enableWideScreen() {
		// Could set mpv video-aspect-override to 16:9, but mpv auto-detects
	}
	void disableWideScreen() {}

	// === Settings ===
	void refreshAudioSettings() {}
};

IMPLEMENT_NODE("DVDPlayer", CDVDPlayer, CNode)

START_NODE_PROPS(CDVDPlayer, CNode)
	NODE_PROP(pt_number, CDVDPlayer, speed)
	NODE_PROP(pt_integer, CDVDPlayer, title)
	NODE_PROP(pt_integer, CDVDPlayer, chapter)
	NODE_PROP(pt_integer, CDVDPlayer, hours)
	NODE_PROP(pt_integer, CDVDPlayer, minutes)
	NODE_PROP(pt_integer, CDVDPlayer, seconds)
	NODE_PROP(pt_integer, CDVDPlayer, frames)
	NODE_PROP(pt_integer, CDVDPlayer, videoModePreferrence)
	NODE_PROP(pt_integer, CDVDPlayer, parentalLevel)
	NODE_PROP(pt_integer, CDVDPlayer, audioStream)
	NODE_PROP(pt_integer, CDVDPlayer, left)
	NODE_PROP(pt_integer, CDVDPlayer, top)
	NODE_PROP(pt_integer, CDVDPlayer, width)
	NODE_PROP(pt_integer, CDVDPlayer, height)
	NODE_PROP(pt_boolean, CDVDPlayer, closedCaption)
	NODE_PROP(pt_integer, CDVDPlayer, subTitle)
	NODE_PROP(pt_integer, CDVDPlayer, domain)
	NODE_PROP(pt_integer, CDVDPlayer, angle)
	NODE_PROP(pt_integer, CDVDPlayer, angleCount)
	NODE_PROP(pt_integer, CDVDPlayer, playbackMode)
	NODE_PROP(pt_integer, CDVDPlayer, audioFormat)
	NODE_PROP(pt_integer, CDVDPlayer, audioChannels)
	NODE_PROP(pt_string, CDVDPlayer, audioLanguage)
	NODE_PROP(pt_string, CDVDPlayer, subTitleLanguage)
	NODE_PROP(pt_integer, CDVDPlayer, abRepeatState)
	NODE_PROP(pt_integer, CDVDPlayer, number)
	NODE_PROP(pt_integer, CDVDPlayer, scanSpeed)
	NODE_PROP(pt_boolean, CDVDPlayer, scanSlow)
	NODE_PROP(pt_boolean, CDVDPlayer, autoStop)
	NODE_PROP(pt_boolean, CDVDPlayer, bScanBackward)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CDVDPlayer
START_NODE_FUN(CDVDPlayer, CNode)
	NODE_FUN_VV(init)
	NODE_FUN_VV(play)
	NODE_FUN_VV(stop)
	NODE_FUN_VV(resume)
	NODE_FUN_VV(goUp)
	NODE_FUN_VV(pause)
	NODE_FUN_VV(playOrPause)
	NODE_FUN_VV(selectUp)
	NODE_FUN_VV(selectDown)
	NODE_FUN_VV(selectRight)
	NODE_FUN_VV(selectLeft)
	NODE_FUN_VV(activate)
	NODE_FUN_VV(nextAudioStream)
	NODE_FUN_VV(nextAngle)
	NODE_FUN_VV(startChapter)
	NODE_FUN_VV(nextChapter)
	NODE_FUN_VV(prevChapter)
	NODE_FUN_VV(menu)
	NODE_FUN_VV(titleMenu)
	NODE_FUN_VV(nextSubtitle)
	NODE_FUN_VV(forwardScan)
	NODE_FUN_VV(backwardScan)
	NODE_FUN_VV(stopScan)
	NODE_FUN_VV(eject)
	NODE_FUN_VN(setScale)
	NODE_FUN_VNN(setZoomPos)
	NODE_FUN_VV(frameAdvance)
	NODE_FUN_VV(frameReverse)
	NODE_FUN_II(isUOPValid)
	NODE_FUN_VV(abRepeat)
	NODE_FUN_VI(digit)
	NODE_FUN_VV(refreshAudioSettings)
	NODE_FUN_IV(isPlaybackDomain)
	NODE_FUN_VV(enableWideScreen)
	NODE_FUN_VV(disableWideScreen)
END_NODE_FUN()
