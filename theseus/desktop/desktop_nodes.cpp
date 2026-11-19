// desktop_nodes.cpp: node class registrations and stubs for the
// Xbox-only XAP nodes that don't have a desktop implementation yet
// (memory monitor, disc drive, copy game, etc.). Audio nodes have
// real SDL_mixer-backed implementations; the rest are stubs that
// keep XAP scripts resolving.

#include "app_paths.h"
#include "std.h"
#include "host.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "audio_sdl.h"
#include "cdaudio.h"
#include "media_player.h"
#include "tmdb.h"
#include "playlist.h"
#include "xcloud_client.h"
extern void DiscDrive_SetDiscType(const char* type);

// Helper: resolve XAP audio URL to local filesystem path
// XAP urls look like "Audio/MainAudio/A Button Select.wav" (relative to Q:\)
static const char* ResolveAudioURL(const TCHAR* url)
{
	if (!url || !*url) return NULL;

	static char s_path[512];

	// Check if it already has a drive letter (Q:\...)
	if (url[0] && url[1] == ':') {
		// Use XboxFS translation
		XboxPath_Translate(url, s_path, sizeof(s_path));
		return s_path;
	}

	// Relative path; prepend Data/
	snprintf(s_path, sizeof(s_path), "%s", AppPathf("Data/%s", url));
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

		// Handle "cd:N" URLs, CDDA track; CdAudio_Play handles the actual load
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
			// Hard halt always. Mix_FadeOutChannel was observed leaving
			// channels audible past their fade window on at least macOS,
			// which made ambient sounds (fade=2 in default.xap) keep
			// playing under music. Match the dashboard's expectation that
			// isActive=false means "shut up now".
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

	// Match Xbox CAudioClip: writing isActive triggers Play/Stop. The dashboard
	// uses this to start/stop ambient + periodic audio (DisableAmbientAudio
	// sets every ambient.isActive = false). Without this hook, those writes
	// stored the bool but didn't stop playback, so ambient leaked under music.
	bool OnSetProperty(const PRD* pprd, const void* pvValue)
	{
		if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_isActive))
		{
			bool wantActive = *(bool*)pvValue;
			if (wantActive == m_isActive) return true;
			m_isActive = wantActive;
			if (m_isActive) Play();
			else            Stop();
		}
		return true;
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
// CAudioVisualizer
// 5960 code at 0x63760..0x64630, notes in docs/decomp/AudioVisualizer.md.
// The desktop reads the mixer's last 256 frames (DashAudio_GetPCMSamples)
// where 5960 reads its AudioClip source's sample buffer.
// ============================================================================
#include "tmap_system.h"
#include "fft.h"

class CAudioVisualizer : public CNode
{
	DECLARE_NODE(CAudioVisualizer, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CAudioVisualizer();
	~CAudioVisualizer();

	// 5960 layout, 0x834 bytes: m_type 0x1c, m_channel 0x20, m_source 0x24,
	// m_scale 0x28, m_offset 0x2c, then the sample and spectrum buffers.
	TCHAR* m_type;		// line (default), spinner, circle, analyzer: first letter counts
	TCHAR* m_channel;	// r or R picks the right channel, anything else the left
	CNode* m_source;
	float m_scale;
	float m_offset;

protected:
	void Advance(float dt);
	void RenderDynamicTexture(CSurfx* surface);

	short* GetMonoPCM();
	void CalcSpectrum(short* pcm, short* bins);
	void UpdateSpectrum();
	void RenderEffect1(CSurfx* surface);
	void RenderEffect2(CSurfx* surface);

	short m_waveL[256];		// 0x030
	short m_waveR[256];		// 0x230
	short m_specL[128];		// 0x430
	short m_specR[128];		// 0x530
	bool m_bHaveSpectrum;			// 0x630
	short m_pcmMono[256];		// 0x632
	bool m_bHaveMono;			// 0x832
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

// CAudioVisualizer::CAudioVisualizer()
// Reconstructed from 5960 retail XBE @ 0x00063770 (inlined in CreateNode):
//     m_type = m_channel = m_source = 0; m_scale = 1.0; m_offset = 0;
// 5960 leaves the buffers alone; Theseus zeroes them so the first frame is flat.
CAudioVisualizer::CAudioVisualizer()
{
	m_type = NULL;
	m_channel = NULL;
	m_source = NULL;
	m_scale = 1.0f;
	m_offset = 0.0f;
	m_bHaveSpectrum = false;
	m_bHaveMono = false;

	memset(m_waveL, 0, sizeof (m_waveL));
	memset(m_waveR, 0, sizeof (m_waveR));
	memset(m_specL, 0, sizeof (m_specL));
	memset(m_specR, 0, sizeof (m_specR));
	memset(m_pcmMono, 0, sizeof (m_pcmMono));
}

// CAudioVisualizer::~CAudioVisualizer()
// Reconstructed from 5960 retail XBE @ 0x00063800:
//     delete[] m_type; delete[] m_channel; if (m_source) m_source->Release();
CAudioVisualizer::~CAudioVisualizer()
{
	delete [] m_type;
	delete [] m_channel;
	if (m_source)
		m_source->Release();
}

// CAudioVisualizer::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x00063870:
//     CNode::Advance(dt); if (m_source) m_source->Advance(dt);
// The source clip is ticked from here because it isn't in the scene itself.
void CAudioVisualizer::Advance(float dt)
{
	CNode::Advance(dt);
	if (m_source)
		m_source->Advance(dt);
}

// Theseus: 5960 has no mono mix. Average of the two channels, cached per frame.
short* CAudioVisualizer::GetMonoPCM()
{
	if (m_bHaveMono)
		return m_pcmMono;

	for (int i = 0; i < 256; i++)
		m_pcmMono[i] = (short)((m_waveL[i] + m_waveR[i]) / 2);
	m_bHaveMono = true;
	return m_pcmMono;
}

// CAudioVisualizer::CalcSpectrum(pcm, bins)
// Reconstructed from 5960 retail XBE @ 0x000638a0:
//     if (!state) state = fft_init(); fft_perform(pcm, power, state);
//     for (i = 0; i < 129; i++) bins[i] = sqrt(power[i]) * (1/256);
// 129 bins land in a 128 short array, same as 5960: the last one spills into
// whatever follows (the right channel's first bin, or the valid flag).
void CAudioVisualizer::CalcSpectrum(short* pcm, short* bins)
{
	static fft_state* s_fft = NULL;
	const int nBins = FFT_BUFFER_SIZE / 2 + 1;
	float power[nBins];

	if (s_fft == NULL)
		s_fft = fft_init();
	fft_perform(pcm, power, s_fft);

	for (int i = 0; i < nBins; i++)
	{
		power[i] = sqrtf(power[i]) / FFT_BUFFER_SIZE;
		bins[i] = (short)power[i];
	}
}

// CAudioVisualizer::UpdateSpectrum()
// Reconstructed from 5960 retail XBE @ 0x00063900:
//     if (!m_bHaveSpectrum) { CalcSpectrum(left); CalcSpectrum(right); m_bHaveSpectrum = 1; }
void CAudioVisualizer::UpdateSpectrum()
{
	if (!m_bHaveSpectrum)
	{
		CalcSpectrum(m_waveL, m_specL);
		CalcSpectrum(m_waveR, m_specR);
		m_bHaveSpectrum = true;
	}
}

// Clipped single pixel. 5960 writes the analyzer's pixels unclipped; the
// curve overlay's 2x2 dot (0x640a0) clips each of its four the same way.
static void VizPlot(CSurfx* surface, int x, int y, BYTE color)
{
	if (x >= 0 && y >= 0 && x < surface->m_nWidth && y < surface->m_nHeight)
		*surface->Pixel(x, y) = color;
}

static void VizPlot2x2(CSurfx* surface, int x, int y, BYTE color)
{
	VizPlot(surface, x, y, color);
	VizPlot(surface, x + 1, y, color);
	VizPlot(surface, x + 1, y + 1, color);
	VizPlot(surface, x, y + 1, color);
}

// Clamped byte from a log level, as the analyzer bars use it.
static BYTE VizLevel(float n)
{
	if (n <= 0.0f)
		return 0;
	if (n >= 255.0f)
		return 255;
	return (BYTE)n;
}

// CAudioVisualizer::RenderDynamicTexture(surface)
// Reconstructed from 5960 retail XBE @ 0x00063940 (1363 bytes). 5960 bails
// unless the source is a playing AudioClip with samples, copies up to 256
// stereo frames in (zero filling the rest), clears both cache flags, clamps
// the count to the surface height, picks the channel from m_channel and the
// style from m_type's first letter, then draws:
//   line: one Line per row, x = w/2 + (sample * w >> 16).
//   spinner: a stretched waveform rotated by g_now * 0.2, w steps over [0, 1].
//   analyzer: log levels of both spectra (left reversed, then right), peaks
//     that fall 2 a frame, bars fading from 255 down, then RenderEffect1 and 2.
// Full walk through in docs/decomp/AudioVisualizer.md. Theseus differences:
// samples come from the desktop mixer every frame, the circle style (absent
// from 5960) is drawn, the analyzer takes log(level + 1) and clips the peak
// dot, and the spinner skips 5960's unused rand() call.
void CAudioVisualizer::RenderDynamicTexture(CSurfx* surface)
{
	DashAudio_GetPCMSamples(m_waveL, m_waveR, 256);
	m_bHaveMono = false;
	m_bHaveSpectrum = false;

	const int w = surface->m_nWidth;
	const int h = surface->m_nHeight;
	int nFrames = 256;
	if (nFrames > h)
		nFrames = h;

	// One channel, picked by m_channel.
	const short* pcm = m_waveL;
	if (m_channel != NULL && (m_channel[0] == 'r' || m_channel[0] == 'R'))
		pcm = m_waveR;

	TCHAR style = (m_type != NULL) ? m_type[0] : 0;
	if (style >= 'A' && style <= 'Z')
		style += 'a' - 'A';

	const float now = (float)TheseusGetNow();

	if (style == 's')
	{
		const int xMid = w / 2;
		const int yMid = h / 2;
		const float dx = 1.0f / (float)w;
		const float c = cosf(now * 0.2f);
		const float s = sinf(now * 0.2f);
		float xPrev = 0.0f;
		float yPrev = 0.0f;

		for (float t = 0.0f; t <= 1.0f; t += dx)
		{
			int iFrame = (int)(t * nFrames);
			if (iFrame >= nFrames)
				iFrame = nFrames - 1;	// t can land on exactly 1.0
			float amp = (float)pcm[iFrame] / 32767.0f * m_scale + m_offset;
			float along = 2.1f * (t - 0.5f);
			float x = c * along + s * amp;
			float y = -c * amp + s * along;

			if (t != 0.0f)
			{
				surface->Line(xMid + (int)(xPrev * xMid), yMid + (int)(yPrev * yMid),
					xMid + (int)(x * xMid), yMid + (int)(y * yMid), 255);
			}
			xPrev = x;
			yPrev = y;
		}
	}
	else if (style == 'c')
	{
		// Theseus: a half circle of the waveform, mirrored left and right.
		const int xMid = w / 2;
		const int yMid = h / 2;
		const float dx = 1.0f / (float)w;
		float xPrev = 0.0f;
		float yPrev = 0.0f;
		int iFrame = 0;

		for (float t = 0.0f; t <= 1.0f; t += dx, iFrame++)
		{
			float r = (float)pcm[iFrame < 256 ? iFrame : 255] / 32767.0f * m_scale + m_offset;	// past 256 columns, hold the last frame
			float angle = t * 3.14159265f;
			float x = r * sinf(angle);
			float y = r * cosf(angle);

			if (iFrame > 0)
			{
				surface->Line(xMid + (int)(xPrev * xMid), yMid + (int)(yPrev * yMid),
					xMid + (int)(x * xMid), yMid + (int)(y * yMid), 255);
				surface->Line(xMid - (int)(xPrev * xMid), yMid + (int)(yPrev * yMid),
					xMid - (int)(x * xMid), yMid + (int)(y * yMid), 255);
			}
			xPrev = x;
			yPrev = y;
		}
	}
	else if (style == 'a')
	{
		static int s_peak[256];
		BYTE level[256];

		UpdateSpectrum();

		// Left spectrum high to low across the first half, right low to high after.
		for (int i = 0; i < 128; i++)
			level[i] = VizLevel(logf((float)(m_specL[127 - i] + 1)) * 8.0f);
		for (int i = 128; i < 256; i++)
			level[i] = VizLevel(logf((float)(m_specR[i - 128] + 1)) * 8.0f);

		for (int i = 0; i < 256; i++)
		{
			s_peak[i] = (s_peak[i] > 2) ? s_peak[i] - 2 : 0;
			if (s_peak[i] < level[i] * 2)
				s_peak[i] = level[i] * 2;
		}

		for (int x = 0; x < w; x++)
		{
			int iBand = (x * 256) / w;

			float bar = ((float)level[iBand] * (float)h) / 256.0f;
			if ((int)bar > h)
				bar = (float)h;
			BYTE shade = 255;
			for (int y = h - (int)bar; y < h; y++)
				*surface->Pixel(x, y) = shade--;

			float peak = (float)s_peak[iBand] / 2.0f;
			peak = (peak * (float)h) / 256.0f;
			VizPlot(surface, x, h - (int)peak, 255);
		}

		RenderEffect1(surface);
		RenderEffect2(surface);
	}
	else
	{
		const int xMid = w / 2;
		int xPrev = xMid;
		int yPrev = 0;

		for (int y = 0; y < nFrames; y++)
		{
			int x = xMid + (int)(((long)pcm[y] * w) >> 16);
			surface->Line(xPrev, yPrev, x, y, 255);
			xPrev = x;
			yPrev = y;
		}
	}
}

// Bass tracker for the flash. Field order follows the 5960 globals at
// 0x17e6f4..0x17e70c; the frame counter is 0x188f50.
static struct
{
	int hiNow, hiPrev, hiFrame;
	int loNow, loPrev, loFrame;
	bool lit;
} s_bass;

// CAudioVisualizer::RenderEffect1(surface)
// Reconstructed from 5960 retail XBE @ 0x00063ef0. Decompile:
//     bass = sum(fftRight[0..4] >> 4) + sum(fftLeft[0..4] >> 4)) / 10;
//     track the max and min, rolling each over every 10 frames;
//     if (bass > (maxOld*6 + minOld*4)/10 && !lit) { fill surface 0xff; lit = 1; }
//     frame++; if (bass < (maxOld*4 + minOld*6)/10 && lit) lit = 0;
// A beat flashes the whole surface white once, then waits for the bass to drop.
void CAudioVisualizer::RenderEffect1(CSurfx* surface)
{
	static int s_frame = 0;

	int bass = 0;
	for (int i = 0; i < 5; i++)
		bass += (m_specL[i] >> 4) + (m_specR[i] >> 4);
	bass /= 10;

	if (bass > s_bass.hiNow)
		s_bass.hiNow = bass;
	if (bass < s_bass.loNow)
		s_bass.loNow = bass;

	if (s_frame - s_bass.hiFrame > 10)
	{
		s_bass.hiPrev = s_bass.hiNow;
		s_bass.hiNow = 0;
		s_bass.hiFrame = s_frame;
	}
	if (s_frame - s_bass.loFrame > 10)
	{
		s_bass.loPrev = s_bass.loNow;
		s_bass.loNow = 0;
		s_bass.loFrame = s_frame;
	}

	int on = (s_bass.hiPrev * 6 + s_bass.loPrev * 4) / 10;
	int off = (s_bass.hiPrev * 4 + s_bass.loPrev * 6) / 10;

	if (!s_bass.lit && bass > on)
	{
		memset(surface->m_pels, 0xff, surface->m_nWidth * surface->m_nHeight);
		s_bass.lit = true;
	}
	s_frame++;
	if (s_bass.lit && bass < off)
		s_bass.lit = false;
}

// CAudioVisualizer::RenderEffect2(surface)
// Reconstructed from 5960 retail XBE @ 0x00064130 (1761 bytes). Every 3 to
// 8 seconds the style steps through five (0..4, starting from -1). Each
// style draws the two channels' waveforms (high byte of every fifth frame,
// scaled by 50 * h >> 12) in color 128 every 4 pixels: 0 one line, 1 two
// lines apart, 2 four (two across, two down), 3 and 4 bent round a half
// circle from cos/sin tables of w entries (3 keeps them outside). 3 and 4
// close the circle with one more line. After that two 64 dot curves (2x2,
// color 255) trace a slowly turning figure eight from a running counter.
// Constants from 0x2c124..0x2c160 and 0x176034..0x176040. Theseus picks the
// next change with rnd() and draws the curves at amplitude 50 / 256.
static int s_waveStyle = -1;
static int s_waveTableW = 0;
static float* s_waveCos = NULL;
static float* s_waveSin = NULL;

void CAudioVisualizer::RenderEffect2(CSurfx* surface)
{
	const int w = surface->m_nWidth;
	const int h = surface->m_nHeight;
	const int amp = 50;
	const int gap = (30 * h) >> 8;
	const BYTE color = 128;
	const int dx = 4;

	static XTIME s_nextStyle = 0.0f;
	if (s_waveStyle < 0 || TheseusGetNow() >= s_nextStyle)
	{
		s_waveStyle++;
		s_nextStyle = TheseusGetNow() + 3.0f + rnd(5.0f);
	}
	if ((UINT)s_waveStyle > 4)
		s_waveStyle = 0;

	// Tables are rebuilt when the surface width changes; only every 4th entry is used.
	if (s_waveTableW != w || s_waveCos == NULL || s_waveSin == NULL)
	{
		delete [] s_waveCos;
		delete [] s_waveSin;
		s_waveTableW = w;
		s_waveCos = new float[w];
		s_waveSin = new float[w];
		for (int i = 0; i < w; i += dx)
			s_waveCos[i] = cosf((float)i / w * 3.14159265f + 3.14159265f / 2.0f);
		for (int i = 0; i < w; i += dx)
			s_waveSin[i] = sinf((float)i / w * 3.14159265f + 3.14159265f / 2.0f);
	}

	float yA = (float)((((m_waveL[0] + m_waveR[0]) >> 9) * amp * h) >> 12);
	float yB = yA;
	if (s_waveStyle == 3 && yA < 0)
	{
		yA = 0;
		yB = 0;
	}

	const int hh = h >> 1;
	const int hw = w >> 1;

	for (int i = dx; i < w; i += dx)
	{
		float yAPrev = yA;
		float yBPrev = yB;
		int iFrame = (i << 8) / w / 5;
		yA = (float)(((m_waveR[iFrame] >> 8) * amp * h) >> 12);
		yB = (float)(((m_waveL[iFrame] >> 8) * amp * h) >> 12);

		switch (s_waveStyle)
		{
		case 0:
			surface->Line(i - dx, (int)(hh + gap + yBPrev), i, (int)(hh + gap + yB), color);
			break;

		case 1:
			surface->Line(i - dx, (int)(hh + gap + yAPrev), i, (int)(hh + gap + yA), color);
			surface->Line(i - dx, (int)(hh - gap + yBPrev), i, (int)(hh - gap + yB), color);
			break;

		case 2:
			surface->Line(i - dx, (int)(hh + gap + yAPrev), i, (int)(hh + gap + yA), color);
			surface->Line(i - dx, (int)(hh - gap + yAPrev), i, (int)(hh - gap + yA), color);
			surface->Line((int)(hw + gap + yBPrev), i - dx, (int)(hw + gap + yB), i, color);
			surface->Line((int)(hw - gap + yBPrev), i - dx, (int)(hw - gap + yB), i, color);
			break;

		case 3:
			if (yA < 0)
				yA = 0;
			if (yB < 0)
				yB = 0;
			// fall through
		case 4:
			surface->Line((int)(hw + s_waveCos[i - dx] * (gap + yAPrev)), (int)(hh + s_waveSin[i - dx] * (gap + yAPrev)),
				(int)(hw + s_waveCos[i] * (gap + yA)), (int)(hh + s_waveSin[i] * (gap + yA)), color);
			surface->Line((int)(hw - s_waveCos[i - dx] * (gap + yBPrev)), (int)(hh + s_waveSin[i - dx] * (gap + yBPrev)),
				(int)(hw - s_waveCos[i] * (gap + yB)), (int)(hh + s_waveSin[i] * (gap + yB)), color);
			break;
		}
	}

	if (s_waveStyle == 3 || s_waveStyle == 4)
	{
		int iLast = w - dx;
		surface->Line((int)(hw + s_waveCos[iLast] * (gap + yA)), (int)(hh + s_waveSin[iLast] * (gap + yA)),
			(int)(hw - s_waveCos[iLast] * (gap + yB)), (int)(hh + s_waveSin[iLast] * (gap + yB)), color);
	}

	static int s_curveTick = 0;
	const float size = 50.0f / 256;
	int tick = s_curveTick;

	for (int j = 0; j < 2; j++)
	{
		const float v = 80.0f;
		tick = s_curveTick;
		for (int n = 0; n < 64; n++, tick++)
		{
			float cx = cosf((float)tick / (v + v * j * 1.34f)) * h * size;
			float cy = sinf((float)tick / (1.756f * (v + v * j * 0.93f))) * h * size;
			float spin = (float)tick * 0.001f;
			VizPlot2x2(surface,
				(int)(cx * cosf(spin) + cy * sinf(spin) + w / 2),
				(int)(cx * sinf(spin) - cy * cosf(spin) + h / 2),
				255);
		}
	}
	s_curveTick = tick;
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

	CDiscDrive() : m_discType(NULL), m_locked(false), m_bDeferNotification(false), m_pollTimer(2.0f) {
		m_discType = strdup("none");
		s_instance = this;
	}

	DECLARE_NODE(CDiscDrive, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
	TCHAR* m_discType;
	bool   m_locked;
	bool   m_bDeferNotification;
	float  m_pollTimer;

	// CDiscDrive::Advance, 5960 @ 0x00039f20: a change seen while locked sets
	// bDeferNotification; once unlocked the next frame clears it and fires
	// OnDiscInserted, or OnDiscRemoved when the drive is empty.
	void FireDeferred() {
		if (!m_bDeferNotification || m_locked) return;
		m_bDeferNotification = false;
		bool isNone = !m_discType || strcmp(m_discType, "none") == 0;
		CallFunction(this, isNone ? "OnDiscRemoved" : "OnDiscInserted");
	}

	void Advance(float nSeconds) {
		CNode::Advance(nSeconds);
		FireDeferred();
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
		} else if (isNone != wasNone) {
			m_bDeferNotification = true;
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
		else
			dd->m_bDeferNotification = true;
		fprintf(stderr, "[DiscDrive] discType='%s', fired OnDiscInserted\n", type);
	}
}

// 5960 PRD @ 0x00174550: discType, locked, bDeferNotification.
START_NODE_PROPS(CDiscDrive, CNode)
	NODE_PROP(pt_string, CDiscDrive, discType)
	NODE_PROP(pt_boolean, CDiscDrive, locked)
	NODE_PROP(pt_boolean, CDiscDrive, bDeferNotification)
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
#include <deque>

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

// Called from TheseusCreateFile (xbox_file.cpp) to read save data off FATX.
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
	HANDLE hFile = TheseusCreateFile(xboxPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
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
			char* end = strrchr(line, ']');
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
	HostDir* dir = Host_OpenDir(dirPath);
	if (!dir) return 0;
	char name[260];
	HostFileInfo info;
	while (Host_ReadDir(dir, name, sizeof(name), &info)) {
		if (name[0] == '.') continue;
		if (!(info.attrs & HOST_ATTR_DIR))
			total += info.size;
	}
	Host_CloseDir(dir);
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
	const char* udataPath = AppPath_Tree("Library/UDATA");

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
		// imagePath has to be an Xbox-style path so MakeAbsoluteURL leaves it
		// alone. Probe with the host-style form, commit Xbox-style on success.
		title.hasImage = false;
		char hostProbe[1024];
		snprintf(hostProbe, sizeof(hostProbe), "%s/TitleImage.xbx", titlePath);
		if (access(hostProbe, F_OK) == 0) {
			snprintf(title.imagePath, sizeof(title.imagePath),
			         "E:\\UDATA\\%s\\TitleImage.xbx", entName);
			title.hasImage = true;
		} else {
			// Synthetic UDATA entries fall back to Configs/icons/<TitleID>.
			// xbox_path.h maps "C:\icons\..." to "Configs/icons/...".
			snprintf(hostProbe, sizeof(hostProbe), "%s", AppPathf("Configs/icons/%s.jpg", entName));
			if (access(hostProbe, F_OK) == 0) {
				snprintf(title.imagePath, sizeof(title.imagePath),
				         "C:\\icons\\%s.jpg", entName);
				title.hasImage = true;
			} else {
				snprintf(hostProbe, sizeof(hostProbe), "%s", AppPathf("Configs/icons/%s.png", entName));
				if (access(hostProbe, F_OK) == 0) {
					snprintf(title.imagePath, sizeof(title.imagePath),
					         "C:\\icons\\%s.png", entName);
					title.hasImage = true;
				}
			}
		}
		title.saveCount = 0;
		title.totalSizeBytes = 0;
		// Enumerate saves within this title
		HostDir* titleDir = Host_OpenDir(titlePath);
		if (titleDir) {
			char saveName[260];
			HostFileInfo saveInfo;
			while (title.saveCount < SAVED_GAME_MAX_SAVES &&
			       Host_ReadDir(titleDir, saveName, sizeof(saveName), &saveInfo)) {
				if (saveName[0] == '.') continue;
				if (!(saveInfo.attrs & HOST_ATTR_DIR)) continue;
				char savePath[1024];
				snprintf(savePath, sizeof(savePath), "%s/%s", titlePath, saveName);
				DesktopSavedGame& save = title.saves[title.saveCount];
				memset(&save, 0, sizeof(save));
				strncpy(save.folderName, saveName, sizeof(save.folderName) - 1);
				char xboxSaveMetaPath[256];
				snprintf(xboxSaveMetaPath, sizeof(xboxSaveMetaPath), "E:\\UDATA\\%s\\%s\\SaveMeta.xbx", entName, saveName);
				if (!ParseXboxMetaValue(xboxSaveMetaPath, "Name", save.saveName, sizeof(save.saveName)))
					strncpy(save.saveName, saveName, sizeof(save.saveName) - 1);
				save.sizeBytes = GetDirectorySize(savePath);
				save.modTime = (time_t)saveInfo.mtime;
				title.totalSizeBytes += save.sizeBytes;
				title.saveCount++;
			}
			Host_CloseDir(titleDir);
		}
		title.totalSizeBytes += GetDirectorySize(titlePath);
		s_titleCount++;
	};

	HostDir* dir = Host_OpenDir(udataPath);
	if (!dir) { fprintf(stderr, "[SavedGameGrid] Cannot open %s\n", udataPath); return; }
	char entry[260];
	while (Host_ReadDir(dir, entry, sizeof(entry), NULL)) ProcessTitleEntry(entry);
	Host_CloseDir(dir);
	// Sort alphabetically
	for (int i = 0; i < s_titleCount - 1; i++)
		for (int j = i + 1; j < s_titleCount; j++)
			if (strcasecmp(s_titles[i].titleName, s_titles[j].titleName) > 0)
				{ DesktopTitle tmp = s_titles[i]; s_titles[i] = s_titles[j]; s_titles[j] = tmp; }
	uint64_t freeBytes = 0, totalBytes = 0;
	if (Host_DiskSpace(udataPath, &freeBytes, &totalBytes)) {
		s_totalBlocks = (int)(totalBytes / XBOX_BLOCK_SIZE);
		s_freeBlocks = (int)(freeBytes / XBOX_BLOCK_SIZE);
	} else
	{ s_totalBlocks = 50000; s_freeBlocks = 50000; }
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

extern const TCHAR *g_szPickedTitleArt;
extern const char *g_szTitleArt;
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
// Grows and never shrinks: the selected title's image path points in here,
// and a deque keeps that pointer good while it grows.
static std::deque<DesktopIconEntry> s_iconEntries;
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

	for (int i = 0; i < g_vgames.count; i++) {
		if (!g_vgames.games[i].valid) continue;
		if (!g_vgames.games[i].titleID[0]) continue;

		// Check if icon file exists
		char iconPath[512];
		snprintf(iconPath, sizeof(iconPath), "%s", AppPathf("Configs/icons/%s.jpg", g_vgames.games[i].titleID));
		struct stat _ist;
		if (stat(iconPath, &_ist) != 0) {
			// Try .png
			snprintf(iconPath, sizeof(iconPath), "%s", AppPathf("Configs/icons/%s.png", g_vgames.games[i].titleID));
			if (stat(iconPath, &_ist) != 0) continue;
		}

		// Store the Xbox path so xbox_path.h can redirect it to the cached icon.
		if ((int)s_iconEntries.size() <= s_iconCount) s_iconEntries.emplace_back();
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

// The Memory blade's list on the desktop. Titles and saves come from the
// desktop's UDATA scan (or an xemu image) instead of the console's device
// enumeration; there's one device, no copies and no deletes. The layout,
// scrolling and selection are rebuilt from the 5960 grid (vtable 0x000267b0,
// 0x47e50..0x49840), see docs/decomp/SavedGameGrid.md.
class CSavedGameGrid : public CNode
{
public:
	CSavedGameGrid()
	{
		m_pod = m_podRing = m_podXboxLiveAccountsPanel = m_XboxLiveAccountsIconPanel = NULL;
		m_iconBumpRing = m_iconArrow = m_iconCheck = NULL;
		m_podSavePanel = m_podSoundtrackPanel = m_podHilite = NULL;
		m_MUheader = m_MUhiliteHeader = m_firstMURow = NULL;
		m_header = m_hiliteHeader = m_firstRow = m_secondRow = m_otherRow = NULL;
		m_smallIcon = m_SavedIconPanel = m_SoundtrackIconPanel = NULL;
		m_iconRing = m_smallIconHilite = m_moreUp = m_moreDown = NULL;

		m_curTitle = -1;
		m_curGridItem = -1;
		m_curSavedGame = -1;
		m_curDevUnit = -1;
		m_iconsPerRow = 4;
		m_renderIcons = false;
		m_scroll = 0.0f;
		m_smallIconSpacing = 0.0f;
		m_iconRowScroll = 0;
		m_detachIcon = false;
		m_isActive = false;
		m_copyProgress = 0.0f;
		m_freeBlocks = 0;
		m_gameBlocks = 0;
		m_nPrefColumn = 0;
		m_deleteUserContent = false;
		m_enumDone = false;
		m_nScrollTo = 0.0f;
		m_slideBegan = 0.0f;
	}

	DECLARE_NODE(CSavedGameGrid, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	// Scene pieces the XAP hands over.
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
	int m_curSavedGame;		// Theseus: alpha dash scripts read this
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
	float m_nScrollTo;		// 5960 +0xd4
	float m_slideBegan;		// 5960 keeps a double at +0xd8; this one is a float

	// CSavedGameGrid::Advance(dt)
	// From 5960 @ 0x00047e50: nothing without a device or while hidden; the
	// title list is refreshed (OnUpdatingTitlesBegin / End around it); and a
	// running scroll eases toward its target over a quarter second,
	// scroll = (1 - t) * scroll + t * target. The desktop lists once, on the
	// calling thread, and sums the game blocks itself; 5960's copy, delete,
	// device and free space upkeep has no desktop counterpart.
	void Advance(float dt)
	{
		CNode::Advance(dt);
		if (m_curDevUnit == -1 || !m_isActive)
			return;

		if (!m_enumDone)
		{
			m_enumDone = true;
			CallFunction(this, _T("OnUpdatingTitlesBegin"));
			EnumerateTitles();
			m_freeBlocks = s_freeBlocks;
			m_gameBlocks = 0;
			for (int i = 0; i < s_titleCount; i++)
				m_gameBlocks += (int)((s_titles[i].totalSizeBytes + XBOX_BLOCK_SIZE - 1) / XBOX_BLOCK_SIZE);
			CallFunction(this, _T("OnUpdatingTitlesEnd"));
		}

		if (m_slideBegan == 0.0f)
			return;
		float t = (float)(TheseusGetNow() - m_slideBegan) / 0.25f;
		if (t >= 1.0f)
		{
			t = 1.0f;
			m_slideBegan = 0.0f;
		}
		m_scroll = (1.0f - t) * m_scroll + t * m_nScrollTo;
	}

	// Draws pNode under the current world transform times pMatrix.
	static void RenderNodeAt(CNode* pNode, D3DXMATRIX* pMatrix)
	{
		if (pNode == NULL)
			return;
		TheseusPushWorld();
		TheseusMultWorld(pMatrix);
		TheseusUpdateWorld();
		pNode->Render();
		TheseusPopWorld();
	}

	// CSavedGameGrid::RenderIconRow(frame, y, title, first, count)
	// From 5960 @ 0x00048690: up to iconsPerRow icons starting at item first,
	// smallIconSpacing apart, each through SavedIconPanel with its image set
	// for the texture lookup, and the ring on the selected one unless its
	// icon is lifted out. The desktop takes a save's own SaveImage.xbx when
	// the folder has one, else the title's, and rings with iconRing present
	// (5960 checks smallIconHilite).
	void RenderIconRow(D3DXMATRIX* pFrame, float y, int nTitle, int nFirst, int nCount)
	{
		if (m_smallIcon == NULL || nTitle < 0 || nTitle >= s_titleCount)
			return;

		int nEnd = nFirst + m_iconsPerRow;
		if (nEnd > nCount)
			nEnd = nCount;

		DesktopTitle& title = s_titles[nTitle];
		float x = 0.0f;
		for (int item = nFirst; item < nEnd; item++, x += m_smallIconSpacing)
		{
			D3DXMATRIX at;
			D3DXMatrixTranslation(&at, x, y, 0.0f);
			D3DXMatrixMultiply(&at, pFrame, &at);

			static char s_szImage[512];
			if (item >= title.saveCount)
			{
				strcpy(s_szImage, "xboxlogo64.xbx");
			}
			else
			{
				char szHost[512];
				snprintf(szHost, sizeof (szHost), "%s/UDATA/%s/%s/SaveImage.xbx", AppPath_Tree("Library"),
					title.titleID, title.saves[item].folderName);
				struct stat st;
				if (stat(szHost, &st) == 0)
					snprintf(s_szImage, sizeof (s_szImage), "E:\\UDATA\\%s\\%s\\SaveImage.xbx", title.titleID, title.saves[item].folderName);
				else
					snprintf(s_szImage, sizeof (s_szImage), "E:\\UDATA\\%s\\SaveImage.xbx", title.titleID);
			}

			g_szTitleArt = s_szImage;
			RenderNodeAt(m_SavedIconPanel, &at);
			g_szTitleArt = NULL;

			if (!m_detachIcon && m_iconRing != NULL && nTitle == m_curTitle && item == m_curGridItem)
				RenderNodeAt(m_iconRing, &at);
		}
	}

	// Places pNode at (x, y, z) in the grid's frame.
	static void RenderInFrame(CNode* pNode, D3DXMATRIX* pFrame, float x, float y, float z)
	{
		D3DXMATRIX at;
		D3DXMatrixTranslation(&at, x, y, z);
		D3DXMatrixMultiply(&at, pFrame, &at);
		RenderNodeAt(pNode, &at);
	}

	// CSavedGameGrid::RenderLoop(render)
	// From 5960 @ 0x00048910. The frame is a -1.571 turn about -x times a
	// 0.05942 scale. From y = scroll each title draws its pod (with the save
	// panel, and podRing when it's selected with no item), its header (the
	// highlighted one when selected) and name, then y -= 0.0092, the first
	// icon row at x -0.3292 (y -= 0.2455), the second at -0.3301 and one more
	// at -0.3412 (y -= 0.24499 each), and y -= 0.09 before the next title.
	// Rows only draw while y - 0.25 < 0.5, and the loop stops below -1.25.
	// Unselected titles draw at half strength. With render off it returns
	// the selected title's offset from the top. The desktop has no device
	// row, arrows or soundtrack row, swaps the selection ease for a straight
	// half, draws the name with the stock text node and takes the pod image
	// from its own title scan.
	float RenderLoop(bool bRender)
	{
		const float xPod = -0.3292f, zPod = -0.0271f, zRow = 0.01144f;
		const float scale = 0.05942f;

		D3DXMATRIX frame, scaleMat;
		D3DXVECTOR3 axis(-1.0f, 0.0f, 0.0f);
		D3DXMatrixRotationAxis(&frame, &axis, -1.571f);
		D3DXMatrixScaling(&scaleMat, scale, scale, scale);
		D3DXMatrixMultiply(&frame, &frame, &scaleMat);

		const float yLimit = -1.25f;
		float y = m_scroll;

		for (int nTitle = 0; nTitle < s_titleCount && (!bRender || y > yLimit); nTitle++)
		{
			const bool bSelected = (nTitle == m_curTitle);
			const float alphaSave = g_nEffectAlpha;

			const int nSaves = s_titles[nTitle].saveCount;
			const int nRows = (nSaves + m_iconsPerRow - 1) / m_iconsPerRow;
			int nRowScroll = 0;
			if (bSelected)
				nRowScroll = m_iconRowScroll;
			else if (nTitle < m_curTitle)
				nRowScroll = (nRows - 3 > 0) ? nRows - 3 : 0;	// titles above show their last rows

			if (!bRender)
			{
				if (bSelected)
					return y - m_scroll;
			}
			else
			{
				g_nEffectAlpha *= bSelected ? 1.0f : 0.5f;
			}

			const bool bVisible = bRender && y - 0.25f < 0.5f;

			if (bVisible && m_pod != NULL)
			{
				static char s_szPodImage[512];
				if (s_titles[nTitle].hasImage)
				{
					// Already host relative: the title's TitleImage.xbx or a Configs/icons picture.
					strncpy(s_szPodImage, s_titles[nTitle].imagePath, sizeof (s_szPodImage) - 1);
					s_szPodImage[sizeof (s_szPodImage) - 1] = 0;
				}
				else
				{
					strcpy(s_szPodImage, "xboxlogo128.xbx");
				}

				g_szTitleArt = s_szPodImage;
				D3DXMATRIX at;
				D3DXMatrixTranslation(&at, xPod, y, zPod);
				D3DXMatrixMultiply(&at, &frame, &at);
				RenderNodeAt(m_pod, &at);
				if (m_podSavePanel != NULL)
					RenderNodeAt(m_podSavePanel, &at);
				if (bSelected && m_curGridItem == -1 && m_podHilite != NULL)
					RenderNodeAt(m_podRing, &at);
				g_szTitleArt = NULL;
			}

			if (bVisible)
			{
				RenderInFrame(bSelected ? m_hiliteHeader : m_header, &frame, xPod, y, zPod);

				if (m_renderIcons)
				{
					extern CNode* GetTextNode(const char* szText, float nWidth);
					D3DXMATRIX nameScale, at;
					D3DXMatrixScaling(&nameScale, 0.06442f, 0.06442f, 0.06442f);
					D3DXMatrixTranslation(&at, xPod - scale, y + scale, zPod + (0.6f * scale));
					D3DXMatrixMultiply(&at, &nameScale, &at);
					RenderNodeAt(GetTextNode(s_titles[nTitle].titleName, -11.2f), &at);
				}
			}
			y -= 0.0092f;

			if (bRender && y - 0.25f < 0.5f)
			{
				RenderInFrame(m_firstRow, &frame, xPod, y, zRow);
				RenderIconRow(&frame, y, nTitle, nRowScroll * m_iconsPerRow, nSaves);
			}
			y -= 0.2455f;

			if (nSaves > m_iconsPerRow)
			{
				if (bRender && y - 0.25f < 0.5f)
				{
					RenderInFrame(m_secondRow, &frame, -0.3301f, y, zRow);
					RenderIconRow(&frame, y, nTitle, (nRowScroll + 1) * m_iconsPerRow, nSaves);
				}
				y -= 0.24499f;

				// At most three rows show; the third is the only "other" row.
				if (nSaves > m_iconsPerRow * 2 && nRows - nRowScroll > 2 && (!bRender || y > yLimit))
				{
					if (bRender && y - 0.25f < 0.5f)
					{
						RenderInFrame(m_otherRow, &frame, -0.3412f, y, zRow);
						RenderIconRow(&frame, y, nTitle, (nRowScroll + 2) * m_iconsPerRow, nSaves);
					}
					y -= 0.24499f;
				}
			}

			y -= 0.09f;
			g_nEffectAlpha = alphaSave;
		}
		return 0.0f;
	}

	// CSavedGameGrid::Render()
	// From 5960 @ 0x000488a0: needs the header and the three row pieces, a
	// device and the blade showing, then runs the loop.
	void Render()
	{
		if (m_header == NULL || m_firstRow == NULL || m_secondRow == NULL || m_otherRow == NULL)
			return;
		if (m_curDevUnit == -1 || !m_isActive)
			return;
		EnumerateTitles();
		if (s_titleCount > 0)
			RenderLoop(true);
	}

	// CSavedGameGrid::OnSetProperty(prop, value)
	// From 5960 @ 0x00048530: a new curDevUnit lists that device and selects
	// its first row (none for the memory units), a new curTitle selects it;
	// both store the value themselves. Everything else is stored as usual.
	bool OnSetProperty(const PRD* prop, const void* value)
	{
		intptr_t offset = (intptr_t)prop->pbOffset;
		if (offset == MEMBER_OFFSET(m_curDevUnit))
		{
			m_curDevUnit = *(const int*)value;
			EnumerateTitles();
			SelectTitle(m_curDevUnit == 8 ? 0 : -1);
			return false;
		}
		if (offset == MEMBER_OFFSET(m_curTitle))
		{
			SelectTitle(*(const int*)value);
			return false;
		}
		return true;
	}

	// CSavedGameGrid::SelectTitle(n)
	// From 5960 @ 0x00049410: clamp to the rows, aim the scroll at the row's
	// offset (RenderLoop with render off) and start the ease. The desktop
	// rows run past the UDATA titles into its game icon entries, which only
	// scroll when they're real titles; it always lands on the title itself
	// (no item, first icon row) instead of 5960's remembered column, and
	// tells the script with OnSelChange.
	void SelectTitle(int n)
	{
		LoadDesktopIcons();
		const int nRowsTotal = s_titleCount + s_iconCount;
		if (n < -1)
			n = -1;
		if (n >= nRowsTotal)
			n = nRowsTotal - 1;

		m_curTitle = n;
		m_curGridItem = -1;
		m_iconRowScroll = 0;

		EnumerateTitles();
		if (n >= 0 && n < s_titleCount)
		{
			m_nScrollTo = -RenderLoop(false);
			m_slideBegan = TheseusGetNow();
		}
		CallFunction(this, "OnSelChange");
	}

	// selectUp / selectDown (5960 @ 0x00049590, 0x000495f0) move one title;
	// selectLeft / selectRight (0x00049680, 0x000497b0) one item along the
	// row. The desktop stops at the ends and only walks saves, and a sideways
	// move reports OnSelChange itself.
	void selectUp()
	{
		EnumerateTitles();
		if (s_titleCount > 0 && m_curTitle > 0)
			SelectTitle(m_curTitle - 1);
	}

	void selectDown()
	{
		EnumerateTitles();
		if (s_titleCount > 0 && m_curTitle < s_titleCount - 1)
			SelectTitle(m_curTitle + 1);
	}

	void selectLeft()
	{
		EnumerateTitles();
		if (m_curTitle < 0 || m_curTitle >= s_titleCount)
			return;
		if (s_titles[m_curTitle].saveCount > 0 && m_curGridItem > 0)
		{
			m_curGridItem--;
			CallFunction(this, "OnSelChange");
		}
	}

	void selectRight()
	{
		EnumerateTitles();
		if (m_curTitle < 0 || m_curTitle >= s_titleCount)
			return;
		if (s_titles[m_curTitle].saveCount > 0 && m_curGridItem < s_titles[m_curTitle].saveCount - 1)
		{
			m_curGridItem++;
			CallFunction(this, "OnSelChange");
		}
	}

	// CSavedGameGrid::setSelImage()
	// From 5960 @ 0x00049840: points the selected-title image at the current
	// title's picture. The desktop also covers its game icon entries.
	void setSelImage()
	{
		EnumerateTitles();
		LoadDesktopIcons();
		static char s_szSelImage[512];
		if (m_curTitle >= 0 && m_curTitle < s_titleCount)
		{
			if (!s_titles[m_curTitle].hasImage)
				return;
			strncpy(s_szSelImage, s_titles[m_curTitle].imagePath, sizeof (s_szSelImage) - 1);
			s_szSelImage[sizeof (s_szSelImage) - 1] = 0;
			g_szPickedTitleArt = s_szSelImage;
		}
		else if (m_curTitle >= s_titleCount && m_curTitle < s_titleCount + s_iconCount)
		{
			g_szPickedTitleArt = s_iconEntries[m_curTitle - s_titleCount].iconPath;
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
	CLiveAccounts() : m_nCurrentIndex(0), m_bLogon(false), m_fLogOnSuccess(false), m_fLogOnInProgress(false),
	                  m_currentAccount(-1), m_newAccountFromMU(-1) {}

	DECLARE_NODE(CLiveAccounts, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	int m_nCurrentIndex;
	bool m_bLogon, m_fLogOnSuccess, m_fLogOnInProgress;

	// 5960 PRD @ 0x00176234: currentAccount and newAccountFromMU, both -1
	// until a logon or an MU account shows up. Desktop has neither.
	int m_currentAccount;
	int m_newAccountFromMU;

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
	// 5960 @ 0x000698b0 / 0x000698e0: an index past the account list reads
	// as no passcode and a passcode that matches. Desktop has no accounts.
	int IsPasswordEnabled(int) { return 0; }
	int VerifyPassword(int, const TCHAR*) { return 1; }
};

IMPLEMENT_NODE("LiveAccounts", CLiveAccounts, CNode)

START_NODE_PROPS(CLiveAccounts, CNode)
	NODE_PROP(pt_boolean, CLiveAccounts, bLogon)
	NODE_PROP(pt_boolean, CLiveAccounts, fLogOnSuccess)
	NODE_PROP(pt_boolean, CLiveAccounts, fLogOnInProgress)
	NODE_PROP(pt_integer, CLiveAccounts, nCurrentIndex)
	NODE_PROP(pt_integer, CLiveAccounts, currentAccount)
	NODE_PROP(pt_integer, CLiveAccounts, newAccountFromMU)
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
	NODE_FUN_II(IsPasswordEnabled)
	NODE_FUN_IIS(VerifyPassword)
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
	    m_osdTimer(0.0f), m_lastOsdSeconds(-1),
	    m_navDomain(0), m_navTitle(0), m_navChapter(0), m_navAngle(0), m_navAngleCount(0),
	    m_navVideoWidth(0), m_navVideoHeight(0) {}

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

	// What the script was last told, so a change fires once.
	int m_navDomain, m_navTitle, m_navChapter, m_navAngle, m_navAngleCount;
	int m_navVideoWidth, m_navVideoHeight;

	// CDVDPlayer::Advance, 5960 @ 0x00065f10 (the part past the navigator
	// calls, 0x00066029..0x0006639b), polls the navigator every frame and
	// fires an event for each value that moved, in this order:
	//     domain -> OnDomainChange, title -> OnTitleChange,
	//     chapter -> OnChapterChange, h:m:s -> OnTimeChange,
	//     angle or angle count -> OnAngleChange, audio -> OnAudioChange,
	//     subtitle -> OnSubTitleChange, source video size -> OnVideoSizeChange
	// Desktop already fires the time, audio and subtitle ones; these cover
	// the rest from what mpv reports.
	void ReportNavigationChanges() {
		if (m_domain != m_navDomain) {
			m_navDomain = m_domain;
			CallFunction(this, "OnDomainChange");
		}
		if (m_title != m_navTitle) {
			m_navTitle = m_title;
			CallFunction(this, "OnTitleChange");
		}
		if (m_chapter != m_navChapter) {
			m_navChapter = m_chapter;
			CallFunction(this, "OnChapterChange");
		}
	}

	void ReportAngleAndVideoSize() {
		if (m_angle != m_navAngle || m_angleCount != m_navAngleCount) {
			m_navAngle = m_angle;
			m_navAngleCount = m_angleCount;
			CallFunction(this, "OnAngleChange");
		}
		int w = 0, h = 0;
		MediaPlayer_GetVideoTexture(&w, &h);
		if (w != m_navVideoWidth || h != m_navVideoHeight) {
			m_navVideoWidth = w;
			m_navVideoHeight = h;
			CallFunction(this, "OnVideoSizeChange");
		}
	}

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

			ReportNavigationChanges();

			// Fire time change callback only when the displayed second changes
			int curSec = m_hours * 3600 + m_minutes * 60 + m_seconds;
			if (curSec != m_lastOsdSeconds) {
				m_lastOsdSeconds = curSec;
				CallFunction(this, "OnTimeChange");
			}

			ReportAngleAndVideoSize();
		} else if (mpState == MP_STOPPED && m_playbackMode != 0) {
			m_playbackMode = 0;
			m_scanSpeed = 1;
			m_scanSlow = false;
			m_bScanBackward = false;
			m_title = 0;
			m_domain = 0;
			CallFunction(this, "OnPlaybackModeChange");
			ReportNavigationChanges();
		}

		// Sync A-B repeat state
		m_abRepeatState = MediaPlayer_GetABRepeatState();
	}

	void Render() {
		if (!MediaPlayer_HasVideo()) return;
		extern void MediaPlayer_RenderToScreen(int w, int h);
		// w/h are ignored by the bgfx RenderToScreen path. view 0's
		// viewport rect (kept current by SDL_WINDOWEVENT_SIZE_CHANGED)
		// decides where the fullscreen quad lands.
		MediaPlayer_RenderToScreen(0, 0);
	}

	// 5960 ends each user op on the navigator's answer (helper @ 0x000669f0
	// and its inlined copies through 0x00067f00):
	//     hr == 0x87840001 (UOP prohibited) or 0x80034013 -> OnProhibited
	//     anything else                                   -> OnUserOpAllowed
	// mpv takes every transport op; the disc menu, select and angle ops have
	// nothing to act on in a file, so they report prohibited.
	void ReportUserOp(bool bAllowed) {
		CallFunction(this, bAllowed ? "OnUserOpAllowed" : "OnProhibited");
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
		ReportUserOp(true);
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

	void pause() { MediaPlayer_Pause(); ReportUserOp(true); }
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
			ReportUserOp(true);
			return;
		}
		MediaPlayer_TogglePause();
		ReportUserOp(true);
	}
	void resume() { MediaPlayer_Play(); ReportUserOp(true); }

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
		ReportUserOp(true);
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
		ReportUserOp(true);
	}

	void stopScan() {
		m_scanSpeed = 1;
		m_scanSlow = false;
		m_bScanBackward = false;
		m_backScanAccum = 0.0f;
		MediaPlayer_SetSpeed(1.0);
		MediaPlayer_Play();
		m_playbackMode = 3;  // DPM_PLAYING
		ReportUserOp(true);
	}

	// === Frame stepping ===
	void frameAdvance() {
		MediaPlayer_FrameStep();
		m_playbackMode = 8;  // DPM_TRICKPLAY
		ReportUserOp(true);
	}

	void frameReverse() {
		MediaPlayer_FrameBackStep();
		m_playbackMode = 8;  // DPM_TRICKPLAY
		ReportUserOp(true);
	}

	// === Chapter navigation ===
	void startChapter() { MediaPlayer_Seek(0); ReportUserOp(true); }
	void nextChapter() { MediaPlayer_NextChapter(); ReportUserOp(true); }
	void prevChapter() { MediaPlayer_PrevChapter(); ReportUserOp(true); }

	// === Track selection ===
	void nextAudioStream() {
		MediaPlayer_NextAudioTrack();
		UpdateTrackLanguages();  // refresh language strings after track change
		CallFunction(this, "OnAudioChange");
		ReportUserOp(true);
	}

	void nextSubtitle() {
		MediaPlayer_NextSubtitleTrack();
		UpdateTrackLanguages();  // refresh language strings after track change
		CallFunction(this, "OnSubTitleChange");
		ReportUserOp(true);
	}

	void nextAngle() { ReportUserOp(false); } // N/A for file playback

	// === DVD menu navigation (nothing to navigate in a file) ===
	void selectUp() { ReportUserOp(false); }
	void selectDown() { ReportUserOp(false); }
	void selectRight() { ReportUserOp(false); }
	void selectLeft() { ReportUserOp(false); }
	void activate() { ReportUserOp(false); }
	void goUp() { ReportUserOp(false); }
	void menu() { ReportUserOp(false); }
	void titleMenu() { ReportUserOp(false); }

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
		ReportUserOp(true);
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




// ============================================================================
// CPlexLibrary, XAP-callable view over the user's Plex server. Reads from
// the pre-stage cache in plex_client.cpp; multi-server selection is a
// future feature, the first reachable server wins.
// ============================================================================

#include "plex_client.h"
#include "guide.h"
#include "pads.h"

// "S2E05 · Title" for an episode row. No number, just the title.
static std::string EpisodeCode(const std::string& season, const std::string& episode)
{
	if (episode.empty()) return "";
	char buf[32];
	snprintf(buf, sizeof(buf), "S%sE%02d", season.empty() ? "?" : season.c_str(), atoi(episode.c_str()));
	return buf;
}

// "1h 13m", "47m".
static std::string RunLength(long long seconds)
{
	if (seconds <= 0) return "";
	char buf[32];
	long long m = (seconds + 30) / 60;
	if (m >= 60) snprintf(buf, sizeof(buf), "%lldh %02lldm", m / 60, m % 60);
	else         snprintf(buf, sizeof(buf), "%lldm", m < 1 ? 1 : m);
	return buf;
}

// "47:41", "1:02:03".
static std::string ClockTime(long long seconds)
{
	char buf[32];
	if (seconds >= 3600) snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
	else                 snprintf(buf, sizeof(buf), "%lld:%02lld", seconds / 60, seconds % 60);
	return buf;
}

static void JoinDot(std::string& out, const std::string& part)
{
	if (part.empty()) return;
	if (!out.empty()) out += "  \xC2\xB7  ";
	out += part;
}

// "1931 · 1h 13m · NR · 7.2/10" for the info card.
static std::string FactsLine(const std::string& year, long long runSeconds,
                             const std::string& rating, float stars)
{
	std::string out;
	JoinDot(out, year);
	JoinDot(out, RunLength(runSeconds));
	JoinDot(out, rating);
	if (stars > 0.0f) {
		char buf[16]; snprintf(buf, sizeof(buf), "%.1f/10", stars);
		JoinDot(out, buf);
	}
	return out;
}

// "Watched", "Resume from 47:41 of 1:14:26", or nothing.
static std::string WatchLine(bool played, long long posSeconds, long long runSeconds)
{
	if (posSeconds > 0) {
		std::string s = "Resume from " + ClockTime(posSeconds);
		if (runSeconds > 0) s += " of " + ClockTime(runSeconds);
		return s;
	}
	return played ? "Watched" : "";
}

// Row prefix: a check once watched, a chevron while part watched.
static std::string WatchMark(bool played, long long posSeconds, long long runSeconds)
{
	(void)runSeconds;
	if (posSeconds > 0) return "\xC2\xBB ";
	return played ? "\xE2\x88\x9A " : "";
}

static void AppendLine(std::string& body, const std::string& line)
{
	if (line.empty()) return;
	if (!body.empty()) body += "\n";
	body += line;
}

static std::string ClipInfo(std::string body)
{
	const size_t kMax = 1200;
	if (body.size() > kMax) {
		size_t n = kMax - 1;
		while (n > 0 && ((unsigned char)body[n] & 0xC0) == 0x80) n--;
		body.resize(n);
		body += "\xE2\x80\xA6";
	}
	return body;
}

// The info card's last line: which button flips the watched mark, named for
// the pad or the keyboard used last.
static std::string MarkHint(bool played)
{
	const char* key = "`";
	if (Joy_LastDevice() == 1) {
		switch (Pads_Style()) {
		case PAD_STYLE_PLAYSTATION: key = "R1"; break;
		case PAD_STYLE_NINTENDO:    key = "R"; break;
		default:                    key = "RB"; break;
		}
	}
	return std::string("Press ") + key + (played ? " to mark unwatched" : " to mark watched");
}

class CPlexLibrary : public CNode
{
public:
	CPlexLibrary()
		: m_currentShow(-1), m_currentSeason(-1) {}

	DECLARE_NODE(CPlexLibrary, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	// The library open, by key: the shelves above the libraries come and go.
	std::string m_libKey, m_libName;
	int  m_currentShow;
	int  m_currentSeason;
	std::string m_serverUri;
	// Artists, albums and collections opened inside the library, innermost
	// last, with the row to put the cursor back on when B leaves each one.
	struct Folder { std::string key, name; int cursor; };
	std::vector<Folder> m_folders;

	static bool IsFolderType(const std::string& t)
	{
		return t == "artist" || t == "album" || t == "collection" || t == "playlist";
	}

	void EnsureLibrariesPending()
	{
		if (!Plex_HasToken()) return;
		Plex_StartSync();
		// Playback goes to the server the sync settled on, which isn't always the first listed.
		if (Plex_SyncReady()) m_serverUri = Plex_GetActiveServerUri();
	}

	int GetCount()
	{
		EnsureLibrariesPending();
		return (int)Plex_Cache_GetLibraries().size();
	}

	CStrObject* GetName(int i)
	{
		std::vector<PlexLibrary> libs = Plex_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return new CStrObject;
		return new CStrObject(_T(libs[i].title.c_str()));
	}

	// Library row: "Movies  \xC2\xB7  12".
	CStrObject* GetLibraryRow(int i)
	{
		std::vector<PlexLibrary> libs = Plex_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return new CStrObject;
		char buf[32];
		snprintf(buf, sizeof(buf), "  \xC2\xB7  %d", (int)Plex_Cache_GetItems(libs[i].sectionKey).size());
		return new CStrObject(_T((libs[i].title + buf).c_str()));
	}

	CStrObject* GetMeta(int i)
	{
		std::vector<PlexLibrary> libs = Plex_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return new CStrObject;
		const char* t = libs[i].type.c_str();
		return new CStrObject(_T(t));
	}

	void SetCurrent(int i)
	{
		m_folders.clear();
		std::vector<PlexLibrary> libs = Plex_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return;
		m_libName = libs[i].title;
		if (libs[i].sectionKey == m_libKey) return;
		m_libKey        = libs[i].sectionKey;
		m_currentShow   = -1;
		m_currentSeason = -1;
	}

	CStrObject* GetServerName()
	{
		EnsureLibrariesPending();
		return new CStrObject(_T(Plex_GetActiveServerName().c_str()));
	}

	int         IsSyncReady()    { EnsureLibrariesPending(); return Plex_SyncReady() ? 1 : 0; }
	CStrObject* GetSyncPhase()   { return new CStrObject(_T(Plex_SyncPhase().c_str())); }
	int         GetSyncProgress(){ return Plex_SyncProgress(); }

	std::vector<PlexItem> CurrentItems()
	{
		if (m_libKey.empty()) return {};
		if (!m_folders.empty()) {
			std::vector<PlexItem> kids;
			Plex_Cache_GetChildren(m_folders.back().key, kids);
			return kids;
		}
		return Plex_Cache_GetItems(m_libKey);
	}

	void DrillIntoFolder(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size() || !IsFolderType(items[i].type)) return;
		m_folders.push_back({ items[i].ratingKey, items[i].title, i });
		Plex_RetryFetch(items[i].ratingKey);
		m_currentShow   = -1;
		m_currentSeason = -1;
		std::vector<PlexItem> dummy;
		(void)Plex_Cache_GetChildren(items[i].ratingKey, dummy);
	}

	int GetFolderDepth() { return (int)m_folders.size(); }

	// Steps out of the innermost folder; the row it was opened from.
	int LeaveFolder()
	{
		if (m_folders.empty()) return 0;
		int cursor = m_folders.back().cursor;
		m_folders.pop_back();
		m_currentShow   = -1;
		m_currentSeason = -1;
		return cursor;
	}

	// The library's name, or the folder's once one is open.
	CStrObject* GetListTitle()
	{
		if (!m_folders.empty()) return new CStrObject(_T(m_folders.back().name.c_str()));
		return new CStrObject(_T(m_libName.c_str()));
	}

	CStrObject* GetItemType(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return new CStrObject;
		if (IsFolderType(items[i].type)) return new CStrObject(_T("folder"));
		return new CStrObject(_T(items[i].type.c_str()));
	}

	// Subtitle per view: 1="MOVIE / SHOW (year)", 2="SEASON N", 3="SxEy".
	CStrObject* GetSubtitle(int view, int i)
	{
		if (i < 0) return new CStrObject;
		std::string s;

		if (view == 1) {
			std::vector<PlexItem> items = CurrentItems();
			if (i >= (int)items.size()) return new CStrObject;
			const PlexItem& it = items[i];
			s = it.type;
			for (auto& c : s) c = (char)toupper((unsigned char)c);
			if (!it.year.empty()) { s += "  \xC2\xB7  "; s += it.year; }
			return new CStrObject(_T(s.c_str()));
		}
		if (view == 2) {
			std::vector<PlexSeason> seasons;
			Plex_Cache_GetSeasons(CurrentShowKey(), seasons);
			if (i >= (int)seasons.size()) return new CStrObject;
			s = "SEASON ";
			s += seasons[i].index;
			return new CStrObject(_T(s.c_str()));
		}
		if (view == 3) {
			std::vector<PlexEpisode> eps;
			Plex_Cache_GetEpisodes(CurrentSeasonKey(), eps);
			if (i >= (int)eps.size()) return new CStrObject;
			s = "S";
			std::vector<PlexSeason> seasons;
			Plex_Cache_GetSeasons(CurrentShowKey(), seasons);
			if (m_currentSeason >= 0 && m_currentSeason < (int)seasons.size())
				s += seasons[m_currentSeason].index;
			if (!eps[i].index.empty()) { s += "E"; s += eps[i].index; }
			return new CStrObject(_T(s.c_str()));
		}
		return new CStrObject;
	}

	// Y: the info card. Title, the facts line, genres, where you left off,
	// then the plot. Seasons show the show's plot.
	CStrObject* GetInfoText(int view, int i)
	{
		if (i < 0) return new CStrObject;
		std::string body;

		if (view == 1) {
			std::vector<PlexItem> items = CurrentItems();
			if (i >= (int)items.size()) return new CStrObject;
			const PlexItem& it = items[i];
			body = it.title;
			AppendLine(body, FactsLine(it.year, it.durationMs / 1000, it.rating, it.stars));
			AppendLine(body, it.genres);
			if (it.leafCount > 0) AppendLine(body, LeftLine(it));
			else AppendLine(body, WatchLine(it.Played(), it.offsetMs / 1000, it.durationMs / 1000));
			if (!IsFolderType(it.type)) AppendLine(body, MarkHint(it.Played()));
			if (!it.summary.empty()) { body += "\n\n"; body += it.summary; }
		}
		else if (view == 2) {
			std::vector<PlexItem> items = CurrentItems();
			if (m_currentShow < 0 || m_currentShow >= (int)items.size()) return new CStrObject;
			const PlexItem& sh = items[m_currentShow];
			std::vector<PlexSeason> seasons;
			Plex_Cache_GetSeasons(CurrentShowKey(), seasons);
			body = sh.title;
			if (i < (int)seasons.size()) { body += "  \xC2\xB7  "; body += seasons[i].title; }
			AppendLine(body, FactsLine(sh.year, 0, sh.rating, sh.stars));
			AppendLine(body, sh.genres);
			if (i < (int)seasons.size()) AppendLine(body, LeftLine(seasons[i]));
			if (i < (int)seasons.size()) AppendLine(body, MarkHint(seasons[i].Played()));
			if (!sh.summary.empty()) { body += "\n\n"; body += sh.summary; }
		}
		else if (view == 3) {
			std::vector<PlexEpisode> eps;
			Plex_Cache_GetEpisodes(CurrentSeasonKey(), eps);
			if (i >= (int)eps.size()) return new CStrObject;
			const PlexEpisode& e = eps[i];
			body = EpisodeLabel(e.index, e.title);
			std::vector<PlexItem> items = CurrentItems();
			if (m_currentShow >= 0 && m_currentShow < (int)items.size())
				AppendLine(body, items[m_currentShow].title);
			AppendLine(body, FactsLine("", e.durationMs / 1000, e.rating, e.stars));
			AppendLine(body, WatchLine(e.Played(), e.offsetMs / 1000, e.durationMs / 1000));
			AppendLine(body, MarkHint(e.Played()));
			if (!e.summary.empty()) { body += "\n\n"; body += e.summary; }
		}
		return new CStrObject(_T(ClipInfo(body).c_str()));
	}

	// Truncated plot blurb for inline display. Items + episodes only.
	CStrObject* GetSummary(int view, int i)
	{
		if (i < 0) return new CStrObject;
		std::string s;
		if (view == 1) {
			std::vector<PlexItem> items = CurrentItems();
			if (i >= (int)items.size()) return new CStrObject;
			s = items[i].summary;
		} else if (view == 3) {
			std::vector<PlexEpisode> eps;
			Plex_Cache_GetEpisodes(CurrentSeasonKey(), eps);
			if (i >= (int)eps.size()) return new CStrObject;
			s = eps[i].summary;
		} else {
			return new CStrObject;
		}
		const size_t kMax = 140;
		if (s.size() > kMax) {
			s.resize(kMax - 1);
			s += "\xE2\x80\xA6";   // ellipsis
		}
		return new CStrObject(_T(s.c_str()));
	}

	int GetItemCount() { return (int)CurrentItems().size(); }

	// "Dracula (1931)", or "Show · S1E02 · Title" for an episode out on the
	// On Deck shelf.
	static std::string ItemLabel(const PlexItem& it)
	{
		if (it.type == "episode" && !it.showTitle.empty()) {
			std::string s = it.showTitle;
			std::string code = EpisodeCode(it.seasonIndex, it.episodeIndex);
			if (!code.empty()) { s += " \xC2\xB7 "; s += code; }
			return s + " \xC2\xB7 " + it.title;
		}
		std::string s = it.title;
		if (!it.year.empty()) { s += " ("; s += it.year; s += ")"; }
		return s;
	}

	// Title and year without the watch mark, for the header.
	CStrObject* GetItemName(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return new CStrObject;
		return new CStrObject(_T(ItemLabel(items[i]).c_str()));
	}

	// X plays from the top instead of the resume point.
	bool m_startOver = false;
	void SetStartOver(int on) { m_startOver = on != 0; }

	CStrObject* GetItemTitle(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return new CStrObject;
		const PlexItem& it = items[i];
		std::string s = ItemLabel(it);
		if (!IsFolderType(it.type)) s = Mark(it) + s;
		return new CStrObject(_T(s.c_str()));
	}

	void QueueItemArt(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return;
		const PlexItem& it = items[i];
		if (!it.thumbUrl.empty()) Plex_QueueArtDownload(it.ratingKey, it.thumbUrl);
	}

	// First poster in the list that's on disk, else the logo. A missing file
	// would stick as a miss in the texture cache, so it's never asked for.
	static CStrObject* ArtOrLogo(const std::string& a, const std::string& b = "",
	                             const std::string& c = "")
	{
		const std::string* keys[3] = { &a, &b, &c };
		for (const std::string* k : keys)
			if (!k->empty() && Plex_ArtReady(*k))
				return new CStrObject(_T(Plex_ArtCachePath(*k).c_str()));
		return new CStrObject(_T("E:\\Plex\\plex_logo.png"));
	}

	void QueueShowArt()
	{
		std::vector<PlexItem> items = CurrentItems();
		if (m_currentShow < 0 || m_currentShow >= (int)items.size()) return;
		const PlexItem& sh = items[m_currentShow];
		if (!sh.thumbUrl.empty()) Plex_QueueArtDownload(sh.ratingKey, sh.thumbUrl);
	}

	CStrObject* GetItemArtPath(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return ArtOrLogo("");
		return ArtOrLogo(items[i].ratingKey);
	}

	void QueueSeasonArt(int i)
	{
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (showKey.empty()) return;
		Plex_Cache_GetSeasons(showKey, seasons);
		if (i < 0 || i >= (int)seasons.size()) return;
		const PlexSeason& s = seasons[i];
		QueueShowArt();
		if (!s.thumbUrl.empty()) Plex_QueueArtDownload(s.ratingKey, s.thumbUrl);
	}

	CStrObject* GetSeasonArtPath(int i)
	{
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (showKey.empty()) return ArtOrLogo("");
		Plex_Cache_GetSeasons(showKey, seasons);
		if (i < 0 || i >= (int)seasons.size()) return ArtOrLogo("");
		return ArtOrLogo(seasons[i].ratingKey, showKey);
	}

	void QueueEpisodeArt(int i)
	{
		std::vector<PlexEpisode> eps;
		std::string seasonKey = CurrentSeasonKey();
		if (seasonKey.empty()) return;
		Plex_Cache_GetEpisodes(seasonKey, eps);
		if (i < 0 || i >= (int)eps.size()) return;
		const PlexEpisode& e = eps[i];
		QueueShowArt();
		if (!e.thumbUrl.empty()) Plex_QueueArtDownload(e.ratingKey, e.thumbUrl);
	}

	CStrObject* GetEpisodeArtPath(int i)
	{
		std::vector<PlexEpisode> eps;
		std::string seasonKey = CurrentSeasonKey();
		if (seasonKey.empty()) return ArtOrLogo("");
		Plex_Cache_GetEpisodes(seasonKey, eps);
		if (i < 0 || i >= (int)eps.size()) return ArtOrLogo("");
		return ArtOrLogo(eps[i].ratingKey, seasonKey, CurrentShowKey());
	}

	// What's playing, for the server's playback reports.
	static std::string s_playing;
	static void Report(int event, double pos, double dur, bool paused)
	{
		Plex_ReportPlayback(s_playing, event, (int)(pos * 1000.0), (int)(dur * 1000.0), paused);
	}

	// Bumps when a playback report or a mark changes watch state.
	int GetWatchStamp() { return (int)Plex_WatchStamp(); }

	// RB: watched becomes unwatched and the other way round. The new state,
	// or -1 when the row can't be marked.
	int ToggleWatched(int view, int i)
	{
		if (i < 0) return -1;
		std::string key;
		bool played = false;
		if (view == 1) {
			std::vector<PlexItem> items = CurrentItems();
			if (i >= (int)items.size() || IsFolderType(items[i].type)) return -1;
			key = items[i].ratingKey; played = items[i].Played();
		} else if (view == 2) {
			std::vector<PlexSeason> seasons;
			Plex_Cache_GetSeasons(CurrentShowKey(), seasons);
			if (i >= (int)seasons.size()) return -1;
			key = seasons[i].ratingKey; played = seasons[i].Played();
		} else if (view == 3) {
			std::vector<PlexEpisode> eps;
			std::string seasonKey = CurrentSeasonKey();
			if (seasonKey.empty()) return -1;
			Plex_Cache_GetEpisodes(seasonKey, eps);
			if (i >= (int)eps.size()) return -1;
			key = eps[i].ratingKey; played = eps[i].Played();
		}
		if (key.empty()) return -1;
		Plex_SetPlayed(key, !played);
		return played ? 0 : 1;
	}

	// The row has a resume point, so X has a start to go back to.
	int CanStartOver(int view, int i)
	{
		if (i < 0) return 0;
		if (view == 1) {
			std::vector<PlexItem> items = CurrentItems();
			if (i >= (int)items.size() || IsFolderType(items[i].type) || items[i].type == "show") return 0;
			return items[i].offsetMs > 0 ? 1 : 0;
		}
		if (view == 3) {
			std::vector<PlexEpisode> eps;
			std::string seasonKey = CurrentSeasonKey();
			if (seasonKey.empty()) return 0;
			Plex_Cache_GetEpisodes(seasonKey, eps);
			return i < (int)eps.size() && eps[i].offsetMs > 0 ? 1 : 0;
		}
		return 0;
	}

	void PlayRatingKey(const std::string& ratingKey,
	                   const std::string& title,
	                   const std::string& subtitle,
	                   int offsetMs = 0)
	{
		if (m_serverUri.empty()) m_serverUri = Plex_GetActiveServerUri();
		if (m_serverUri.empty() || ratingKey.empty()) return;
		std::string url = Plex_ResolveStreamUrl(m_serverUri, ratingKey);
		if (url.empty()) {
			fprintf(stderr, "[Plex] stream URL resolve failed for %s\n",
				ratingKey.c_str());
			return;
		}
		double start = m_startOver ? 0.0 : offsetMs / 1000.0;
		MediaUI_SetReporter(NULL);
		if (!MediaPlayer_OpenAt(url.c_str(), start)) {
			fprintf(stderr, "[Plex] MediaPlayer_Open failed for %s\n",
				ratingKey.c_str());
			return;
		}
		s_playing = ratingKey;
		MediaUI_SetReporter(Report);
		extern bool g_mediaFullscreen;
		extern char g_mediaFullscreenTitle[256];
		extern char g_mediaFullscreenSubtitle[256];
		extern void ApplyEffectiveMute_Public();
		g_mediaFullscreen = true;
		ApplyEffectiveMute_Public();
		strncpy(g_mediaFullscreenTitle, title.c_str(),
			sizeof(g_mediaFullscreenTitle) - 1);
		g_mediaFullscreenTitle[sizeof(g_mediaFullscreenTitle) - 1] = 0;
		strncpy(g_mediaFullscreenSubtitle, subtitle.c_str(),
			sizeof(g_mediaFullscreenSubtitle) - 1);
		g_mediaFullscreenSubtitle[sizeof(g_mediaFullscreenSubtitle) - 1] = 0;
	}

	void PlayItem(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return;
		const PlexItem& it = items[i];
		if (IsFolderType(it.type)) return;
		std::string sub = "Plex";
		if (it.type == "episode" && !it.showTitle.empty()) {
			sub = it.showTitle;
			std::string code = EpisodeCode(it.seasonIndex, it.episodeIndex);
			if (!code.empty()) { sub += "  \xC2\xB7  "; sub += code; }
		} else if (!it.year.empty()) { sub += "  \xC2\xB7  "; sub += it.year; }
		PlayRatingKey(it.ratingKey, it.title, sub, it.offsetMs);
	}

	// ----- TV drill: show -> seasons -> episodes -----

	std::string CurrentShowKey()
	{
		std::vector<PlexItem> items = CurrentItems();
		if (m_currentShow < 0 || m_currentShow >= (int)items.size()) return "";
		return items[m_currentShow].ratingKey;
	}

	std::string CurrentSeasonKey()
	{
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (showKey.empty()) return "";
		Plex_Cache_GetSeasons(showKey, seasons);
		if (m_currentSeason < 0 || m_currentSeason >= (int)seasons.size()) return "";
		return seasons[m_currentSeason].ratingKey;
	}

	void DrillIntoShow(int i)
	{
		std::vector<PlexItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return;
		const PlexItem& it = items[i];
		if (it.type != "show") return;
		Plex_RetryFetch(it.ratingKey);
		m_currentShow   = i;
		m_currentSeason = -1;
		std::vector<PlexSeason> dummy;
		(void)Plex_Cache_GetSeasons(it.ratingKey, dummy);
	}

	int GetSeasonCount()
	{
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (showKey.empty()) return 0;
		Plex_Cache_GetSeasons(showKey, seasons);
		return (int)seasons.size();
	}

	CStrObject* GetSeasonTitle(int i)
	{
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (showKey.empty()) return new CStrObject;
		Plex_Cache_GetSeasons(showKey, seasons);
		if (i < 0 || i >= (int)seasons.size()) return new CStrObject;
		return new CStrObject(_T((Mark(seasons[i]) + seasons[i].title).c_str()));
	}

	void DrillIntoSeason(int i)
	{
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (showKey.empty()) return;
		Plex_Cache_GetSeasons(showKey, seasons);
		if (i < 0 || i >= (int)seasons.size()) return;
		m_currentSeason = i;
		Plex_RetryFetch(seasons[i].ratingKey);
		std::vector<PlexEpisode> dummy;
		(void)Plex_Cache_GetEpisodes(seasons[i].ratingKey, dummy);
	}

	static std::string Mark(const PlexDetails& d) { return WatchMark(d.Played(), d.offsetMs / 1000, d.durationMs / 1000); }

	static std::string LeftLine(const PlexDetails& d)
	{
		if (d.Played() || d.Unplayed() <= 0) return d.Played() ? "Watched" : "";
		char buf[48]; snprintf(buf, sizeof(buf), "%d episode%s left", d.Unplayed(), d.Unplayed() == 1 ? "" : "s");
		return buf;
	}

	std::string EpisodeLabel(const std::string& index, const std::string& title)
	{
		std::vector<PlexSeason> seasons;
		Plex_Cache_GetSeasons(CurrentShowKey(), seasons);
		std::string season;
		if (m_currentSeason >= 0 && m_currentSeason < (int)seasons.size())
			season = seasons[m_currentSeason].index;
		std::string code = EpisodeCode(season, index);
		return code.empty() ? title : code + " \xC2\xB7 " + title;
	}

	int GetEpisodeCount()
	{
		std::vector<PlexEpisode> eps;
		std::string seasonKey = CurrentSeasonKey();
		if (seasonKey.empty()) return 0;
		Plex_Cache_GetEpisodes(seasonKey, eps);
		return (int)eps.size();
	}

	CStrObject* GetEpisodeTitle(int i)
	{
		std::vector<PlexEpisode> eps;
		std::string seasonKey = CurrentSeasonKey();
		if (seasonKey.empty()) return new CStrObject;
		Plex_Cache_GetEpisodes(seasonKey, eps);
		if (i < 0 || i >= (int)eps.size()) return new CStrObject;
		return new CStrObject(_T((Mark(eps[i]) + EpisodeLabel(eps[i].index, eps[i].title)).c_str()));
	}

	void PlayEpisode(int i)
	{
		std::vector<PlexEpisode> eps;
		std::string seasonKey = CurrentSeasonKey();
		if (seasonKey.empty()) return;
		Plex_Cache_GetEpisodes(seasonKey, eps);
		if (i < 0 || i >= (int)eps.size()) return;
		const PlexEpisode& e = eps[i];
		std::string sub;
		std::vector<PlexItem>   items = CurrentItems();
		std::vector<PlexSeason> seasons;
		std::string showKey = CurrentShowKey();
		if (!showKey.empty()) Plex_Cache_GetSeasons(showKey, seasons);
		if (m_currentShow >= 0 && m_currentShow < (int)items.size())
			sub = items[m_currentShow].title;
		if (m_currentSeason >= 0 && m_currentSeason < (int)seasons.size()) {
			std::string code = EpisodeCode(seasons[m_currentSeason].index, e.index);
			if (!code.empty()) { sub += "  \xC2\xB7  "; sub += code; }
		}
		PlayRatingKey(e.ratingKey, e.title, sub, e.offsetMs);
	}

	int IsSignedIn() { return Plex_HasToken() ? 1 : 0; }

	// What an empty list says instead: still loading, the server didn't
	// answer, or there's nothing there. Empty while there are rows.
	CStrObject* GetStatus(int view)
	{
		return new CStrObject(_T(StatusFor(view).c_str()));
	}

	static std::string Unreachable()
	{
		std::string name = Plex_GetActiveServerName();
		return "Can't reach " + (name.empty() ? std::string("your Plex server") : name);
	}

	template <typename T>
	static std::string Pending(bool cached, const std::vector<T>& rows, const std::string& key, const char* none)
	{
		if (cached) return rows.empty() ? none : "";
		return Plex_FetchFailed(key) ? Unreachable() : "Loading...";
	}

	std::string StatusFor(int view)
	{
		if (!Plex_HasToken()) return "Sign in to Plex in Settings";
		if (!Plex_SyncReady()) return "Loading...";
		if (view == 0) {
			if (Plex_GetActiveServerUri().empty()) return Unreachable();
			return Plex_Cache_GetLibraries().empty() ? "No libraries" : "";
		}
		if (view == 1) {
			if (m_folders.empty()) return CurrentItems().empty() ? "Nothing here" : "";
			std::vector<PlexItem> kids;
			bool cached = Plex_Cache_GetChildren(m_folders.back().key, kids);
			return Pending(cached, kids, m_folders.back().key, "Nothing here");
		}
		if (view == 2) {
			std::string showKey = CurrentShowKey();
			if (showKey.empty()) return "";
			std::vector<PlexSeason> seasons;
			bool cached = Plex_Cache_GetSeasons(showKey, seasons);
			return Pending(cached, seasons, showKey, "No seasons");
		}
		std::string seasonKey = CurrentSeasonKey();
		if (seasonKey.empty()) return "Loading...";
		std::vector<PlexEpisode> eps;
		bool cached = Plex_Cache_GetEpisodes(seasonKey, eps);
		return Pending(cached, eps, seasonKey, "No episodes");
	}
};

std::string CPlexLibrary::s_playing;

IMPLEMENT_NODE("PlexLibrary", CPlexLibrary, CNode)

START_NODE_PROPS(CPlexLibrary, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CPlexLibrary
START_NODE_FUN(CPlexLibrary, CNode)
	NODE_FUN_IV(GetCount)
	NODE_FUN_SI(GetName)
	NODE_FUN_SI(GetMeta)
	NODE_FUN_VI(SetCurrent)
	NODE_FUN_SV(GetServerName)
	NODE_FUN_IV(IsSyncReady)
	NODE_FUN_SV(GetSyncPhase)
	NODE_FUN_IV(GetSyncProgress)
	NODE_FUN_IV(GetItemCount)
	NODE_FUN_SI(GetItemTitle)
	NODE_FUN_SI(GetItemType)
	NODE_FUN_VI(QueueItemArt)
	NODE_FUN_SI(GetItemArtPath)
	NODE_FUN_VI(PlayItem)
	NODE_FUN_SII(GetSubtitle)
	NODE_FUN_SII(GetSummary)
	NODE_FUN_SII(GetInfoText)
	NODE_FUN_VI(DrillIntoShow)
	NODE_FUN_IV(GetSeasonCount)
	NODE_FUN_SI(GetSeasonTitle)
	NODE_FUN_VI(DrillIntoSeason)
	NODE_FUN_VI(QueueSeasonArt)
	NODE_FUN_SI(GetSeasonArtPath)
	NODE_FUN_IV(GetEpisodeCount)
	NODE_FUN_SI(GetEpisodeTitle)
	NODE_FUN_VI(PlayEpisode)
	NODE_FUN_VI(QueueEpisodeArt)
	NODE_FUN_SI(GetEpisodeArtPath)
	NODE_FUN_IV(IsSignedIn)
	NODE_FUN_VI(DrillIntoFolder)
	NODE_FUN_IV(GetFolderDepth)
	NODE_FUN_IV(LeaveFolder)
	NODE_FUN_SV(GetListTitle)
	NODE_FUN_SI(GetLibraryRow)
	NODE_FUN_SI(GetStatus)
	NODE_FUN_SI(GetItemName)
	NODE_FUN_VI(SetStartOver)
	NODE_FUN_IV(GetWatchStamp)
	NODE_FUN_III(CanStartOver)
	NODE_FUN_III(ToggleWatched)
END_NODE_FUN()


// ============================================================================
// CJellyfinLibrary: mirror of CPlexLibrary over Jellyfin's API surface.
// ============================================================================

#include "jellyfin_client.h"

class CJellyfinLibrary : public CNode
{
public:
	CJellyfinLibrary()
		: m_currentShow(-1), m_currentSeason(-1) {}

	DECLARE_NODE(CJellyfinLibrary, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	// The library open, by key: the shelves above the libraries come and go.
	std::string m_libKey, m_libName;
	int  m_currentShow;
	int  m_currentSeason;
	// Folders opened inside the library, innermost last, with the row to
	// put the cursor back on when B leaves each one.
	struct Folder { std::string id, name; int cursor; };
	std::vector<Folder> m_folders;

	void EnsureLibrariesPending()
	{
		if (!Jellyfin_HasToken()) return;
		Jellyfin_StartSync();
	}

	int GetCount()
	{
		EnsureLibrariesPending();
		return (int)Jellyfin_Cache_GetLibraries().size();
	}

	CStrObject* GetName(int i)
	{
		std::vector<JellyfinLibrary> libs = Jellyfin_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return new CStrObject;
		return new CStrObject(_T(libs[i].name.c_str()));
	}

	// Library row: "Movies  \xC2\xB7  12".
	CStrObject* GetLibraryRow(int i)
	{
		std::vector<JellyfinLibrary> libs = Jellyfin_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return new CStrObject;
		char buf[32];
		snprintf(buf, sizeof(buf), "  \xC2\xB7  %d", (int)Jellyfin_Cache_GetItems(libs[i].id).size());
		return new CStrObject(_T((libs[i].name + buf).c_str()));
	}

	CStrObject* GetMeta(int i)
	{
		std::vector<JellyfinLibrary> libs = Jellyfin_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return new CStrObject;
		return new CStrObject(_T(libs[i].type.c_str()));
	}

	void SetCurrent(int i)
	{
		m_folders.clear();
		std::vector<JellyfinLibrary> libs = Jellyfin_Cache_GetLibraries();
		if (i < 0 || i >= (int)libs.size()) return;
		m_libName = libs[i].name;
		if (libs[i].id == m_libKey) return;
		m_libKey        = libs[i].id;
		m_currentShow   = -1;
		m_currentSeason = -1;
	}

	CStrObject* GetServerName()
	{
		EnsureLibrariesPending();
		// Jellyfin doesn't surface a friendly name in /Views; use the
		// signed-in user's name as the breadcrumb anchor.
		return new CStrObject(_T(Jellyfin_GetUserName().c_str()));
	}

	int         IsSyncReady()    { EnsureLibrariesPending(); return Jellyfin_SyncReady() ? 1 : 0; }
	CStrObject* GetSyncPhase()   { return new CStrObject(_T(Jellyfin_SyncPhase().c_str())); }
	int         GetSyncProgress(){ return Jellyfin_SyncProgress(); }

	std::vector<JellyfinItem> CurrentItems()
	{
		if (m_libKey.empty()) return {};
		if (!m_folders.empty()) {
			std::vector<JellyfinItem> kids;
			Jellyfin_Cache_GetChildren(m_folders.back().id, kids);
			return kids;
		}
		return Jellyfin_Cache_GetItems(m_libKey);
	}

	void DrillIntoFolder(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size() || !items[i].isFolder) return;
		m_folders.push_back({ items[i].id, items[i].name, i });
		Jellyfin_RetryFetch(items[i].id);
		m_currentShow   = -1;
		m_currentSeason = -1;
		std::vector<JellyfinItem> dummy;
		(void)Jellyfin_Cache_GetChildren(items[i].id, dummy);
	}

	int GetFolderDepth() { return (int)m_folders.size(); }

	// Steps out of the innermost folder; the row it was opened from.
	int LeaveFolder()
	{
		if (m_folders.empty()) return 0;
		int cursor = m_folders.back().cursor;
		m_folders.pop_back();
		m_currentShow   = -1;
		m_currentSeason = -1;
		return cursor;
	}

	// The library's name, or the folder's once one is open.
	CStrObject* GetListTitle()
	{
		if (!m_folders.empty()) return new CStrObject(_T(m_folders.back().name.c_str()));
		return new CStrObject(_T(m_libName.c_str()));
	}

	int GetItemCount() { return (int)CurrentItems().size(); }

	// "Dracula (1931)", or "Pioneer One · S1E02 · The Man From Mars" for an
	// episode out on the Continue Watching or Next Up shelf.
	static std::string ItemLabel(const JellyfinItem& it)
	{
		if (it.type == "Episode" && !it.seriesName.empty()) {
			std::string s = it.seriesName;
			std::string code = EpisodeCode(it.seasonIndex, it.episodeIndex);
			if (!code.empty()) { s += " \xC2\xB7 "; s += code; }
			return s + " \xC2\xB7 " + it.name;
		}
		std::string s = it.name;
		if (!it.year.empty()) { s += " ("; s += it.year; s += ")"; }
		return s;
	}

	// Title and year without the watch mark, for the header.
	CStrObject* GetItemName(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return new CStrObject;
		return new CStrObject(_T(ItemLabel(items[i]).c_str()));
	}

	// X plays from the top instead of the resume point.
	bool m_startOver = false;
	void SetStartOver(int on) { m_startOver = on != 0; }

	CStrObject* GetItemTitle(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return new CStrObject;
		const JellyfinItem& it = items[i];
		std::string s = ItemLabel(it);
		if (!it.isFolder || it.type == "Series") s = Mark(it) + s;
		return new CStrObject(_T(s.c_str()));
	}

	CStrObject* GetItemType(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return new CStrObject;
		// Map Jellyfin type to the lowercase "show" the wrapper checks for.
		std::string t = items[i].type;
		if (t == "Series") return new CStrObject(_T("show"));
		if (t == "Movie")  return new CStrObject(_T("movie"));
		if (items[i].isFolder) return new CStrObject(_T("folder"));
		std::string lc = t;
		for (auto& c : lc) c = (char)tolower((unsigned char)c);
		return new CStrObject(_T(lc.c_str()));
	}

	void QueueItemArt(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return;
		Jellyfin_QueueArtDownload(items[i].id, items[i].primaryTag);
	}

	// First poster in the list that's on disk, else the logo. A missing file
	// would stick as a miss in the texture cache, so it's never asked for.
	static CStrObject* ArtOrLogo(const std::string& a, const std::string& b = "",
	                             const std::string& c = "")
	{
		const std::string* ids[3] = { &a, &b, &c };
		for (const std::string* id : ids)
			if (!id->empty() && Jellyfin_ArtReady(*id))
				return new CStrObject(_T(Jellyfin_ArtCachePath(*id).c_str()));
		return new CStrObject(_T("E:\\Jellyfin\\jellyfin_logo.png"));
	}

	void QueueShowArt()
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (m_currentShow < 0 || m_currentShow >= (int)items.size()) return;
		Jellyfin_QueueArtDownload(items[m_currentShow].id, items[m_currentShow].primaryTag);
	}

	CStrObject* GetItemArtPath(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return ArtOrLogo("");
		return ArtOrLogo(items[i].id);
	}

	std::string CurrentShowId()
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (m_currentShow < 0 || m_currentShow >= (int)items.size()) return "";
		return items[m_currentShow].id;
	}

	std::string CurrentSeasonId()
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (showId.empty()) return "";
		Jellyfin_Cache_GetSeasons(showId, seasons);
		if (m_currentSeason < 0 || m_currentSeason >= (int)seasons.size()) return "";
		return seasons[m_currentSeason].id;
	}

	// What's playing, for the server's playback reports.
	static std::string s_playing;
	static void Report(int event, double pos, double dur, bool paused)
	{
		(void)dur;
		Jellyfin_ReportPlayback(s_playing, event, (long long)(pos * 10000000.0), paused);
	}

	// Bumps when a playback report or a mark changes watch state.
	int GetWatchStamp() { return (int)Jellyfin_WatchStamp(); }

	// Movies, episodes, tracks and whole series take a watched mark; other
	// folders don't.
	static bool Markable(const JellyfinItem& it) { return !it.isFolder || it.type == "Series"; }

	// RB: watched becomes unwatched and the other way round. The new state,
	// or -1 when the row can't be marked.
	int ToggleWatched(int view, int i)
	{
		if (i < 0) return -1;
		std::string id;
		bool played = false;
		if (view == 1) {
			std::vector<JellyfinItem> items = CurrentItems();
			if (i >= (int)items.size() || !Markable(items[i])) return -1;
			id = items[i].id; played = items[i].played;
		} else if (view == 2) {
			std::vector<JellyfinSeason> seasons;
			std::string showId = CurrentShowId();
			if (showId.empty()) return -1;
			Jellyfin_Cache_GetSeasons(showId, seasons);
			if (i >= (int)seasons.size()) return -1;
			id = seasons[i].id; played = seasons[i].played;
		} else if (view == 3) {
			std::vector<JellyfinEpisode> eps;
			std::string seasonId = CurrentSeasonId();
			if (seasonId.empty()) return -1;
			Jellyfin_Cache_GetEpisodes(seasonId, eps);
			if (i >= (int)eps.size()) return -1;
			id = eps[i].id; played = eps[i].played;
		}
		if (id.empty()) return -1;
		Jellyfin_SetPlayed(id, !played);
		return played ? 0 : 1;
	}

	// The row has a resume point, so X has a start to go back to.
	int CanStartOver(int view, int i)
	{
		if (i < 0) return 0;
		if (view == 1) {
			std::vector<JellyfinItem> items = CurrentItems();
			if (i >= (int)items.size() || items[i].isFolder) return 0;
			return items[i].posTicks > 0 ? 1 : 0;
		}
		if (view == 3) {
			std::vector<JellyfinEpisode> eps;
			std::string seasonId = CurrentSeasonId();
			if (seasonId.empty()) return 0;
			Jellyfin_Cache_GetEpisodes(seasonId, eps);
			return i < (int)eps.size() && eps[i].posTicks > 0 ? 1 : 0;
		}
		return 0;
	}

	void PlayRatingKey(const std::string& itemId,
	                   const std::string& title,
	                   const std::string& subtitle,
	                   long long posTicks = 0)
	{
		std::string url = Jellyfin_StreamUrl(itemId, m_playAudio);
		if (url.empty()) {
			fprintf(stderr, "[Jellyfin] stream URL build failed for %s\n", itemId.c_str());
			return;
		}
		double start = m_startOver ? 0.0 : (double)Secs(posTicks);
		MediaUI_SetReporter(NULL);
		if (!MediaPlayer_OpenAt(url.c_str(), start)) {
			fprintf(stderr, "[Jellyfin] MediaPlayer_Open failed\n");
			return;
		}
		s_playing = itemId;
		MediaUI_SetReporter(Report);
		extern bool g_mediaFullscreen;
		extern char g_mediaFullscreenTitle[256];
		extern char g_mediaFullscreenSubtitle[256];
		extern void ApplyEffectiveMute_Public();
		g_mediaFullscreen = true;
		ApplyEffectiveMute_Public();
		strncpy(g_mediaFullscreenTitle, title.c_str(),
			sizeof(g_mediaFullscreenTitle) - 1);
		g_mediaFullscreenTitle[sizeof(g_mediaFullscreenTitle) - 1] = 0;
		strncpy(g_mediaFullscreenSubtitle, subtitle.c_str(),
			sizeof(g_mediaFullscreenSubtitle) - 1);
		g_mediaFullscreenSubtitle[sizeof(g_mediaFullscreenSubtitle) - 1] = 0;
	}

	bool m_playAudio = false;

	void PlayItem(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return;
		const JellyfinItem& it = items[i];
		if (it.isFolder) return;
		m_playAudio = (it.type == "Audio");
		std::string sub = "Jellyfin";
		if (it.type == "Episode" && !it.seriesName.empty()) {
			sub = it.seriesName;
			std::string code = EpisodeCode(it.seasonIndex, it.episodeIndex);
			if (!code.empty()) { sub += "  \xC2\xB7  "; sub += code; }
		} else if (!it.year.empty()) { sub += "  \xC2\xB7  "; sub += it.year; }
		PlayRatingKey(it.id, it.name, sub, it.posTicks);
	}

	void DrillIntoShow(int i)
	{
		std::vector<JellyfinItem> items = CurrentItems();
		if (i < 0 || i >= (int)items.size()) return;
		const JellyfinItem& it = items[i];
		if (it.type != "Series") return;
		Jellyfin_RetryFetch(it.id);
		m_currentShow   = i;
		m_currentSeason = -1;
		std::vector<JellyfinSeason> dummy;
		(void)Jellyfin_Cache_GetSeasons(it.id, dummy);
	}

	int GetSeasonCount()
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (showId.empty()) return 0;
		Jellyfin_Cache_GetSeasons(showId, seasons);
		return (int)seasons.size();
	}

	CStrObject* GetSeasonTitle(int i)
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (showId.empty()) return new CStrObject;
		Jellyfin_Cache_GetSeasons(showId, seasons);
		if (i < 0 || i >= (int)seasons.size()) return new CStrObject;
		return new CStrObject(_T((Mark(seasons[i]) + seasons[i].name).c_str()));
	}

	void DrillIntoSeason(int i)
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (showId.empty()) return;
		Jellyfin_Cache_GetSeasons(showId, seasons);
		if (i < 0 || i >= (int)seasons.size()) return;
		m_currentSeason = i;
		Jellyfin_RetryFetch(seasons[i].id);
		std::vector<JellyfinEpisode> dummy;
		(void)Jellyfin_Cache_GetEpisodes(seasons[i].id, dummy);
	}

	void QueueSeasonArt(int i)
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (showId.empty()) return;
		Jellyfin_Cache_GetSeasons(showId, seasons);
		if (i < 0 || i >= (int)seasons.size()) return;
		QueueShowArt();
		if (!seasons[i].primaryTag.empty())
			Jellyfin_QueueArtDownload(seasons[i].id, seasons[i].primaryTag);
	}

	CStrObject* GetSeasonArtPath(int i)
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (showId.empty()) return ArtOrLogo("");
		Jellyfin_Cache_GetSeasons(showId, seasons);
		if (i < 0 || i >= (int)seasons.size()) return ArtOrLogo("");
		return ArtOrLogo(seasons[i].id, showId);
	}

	static long long Secs(long long ticks) { return ticks / 10000000; }
	static std::string Mark(const JellyfinDetails& d) { return WatchMark(d.played, Secs(d.posTicks), Secs(d.runTicks)); }

	std::string EpisodeLabel(const std::string& index, const std::string& title)
	{
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (!showId.empty()) Jellyfin_Cache_GetSeasons(showId, seasons);
		std::string season;
		if (m_currentSeason >= 0 && m_currentSeason < (int)seasons.size())
			season = seasons[m_currentSeason].index;
		std::string code = EpisodeCode(season, index);
		return code.empty() ? title : code + " \xC2\xB7 " + title;
	}

	int GetEpisodeCount()
	{
		std::vector<JellyfinEpisode> eps;
		std::string seasonId = CurrentSeasonId();
		if (seasonId.empty()) return 0;
		Jellyfin_Cache_GetEpisodes(seasonId, eps);
		return (int)eps.size();
	}

	CStrObject* GetEpisodeTitle(int i)
	{
		std::vector<JellyfinEpisode> eps;
		std::string seasonId = CurrentSeasonId();
		if (seasonId.empty()) return new CStrObject;
		Jellyfin_Cache_GetEpisodes(seasonId, eps);
		if (i < 0 || i >= (int)eps.size()) return new CStrObject;
		return new CStrObject(_T((Mark(eps[i]) + EpisodeLabel(eps[i].index, eps[i].name)).c_str()));
	}

	void PlayEpisode(int i)
	{
		std::vector<JellyfinEpisode> eps;
		std::string seasonId = CurrentSeasonId();
		if (seasonId.empty()) return;
		Jellyfin_Cache_GetEpisodes(seasonId, eps);
		if (i < 0 || i >= (int)eps.size()) return;
		const JellyfinEpisode& e = eps[i];
		m_playAudio = false;
		std::vector<JellyfinItem>   items = CurrentItems();
		std::vector<JellyfinSeason> seasons;
		std::string showId = CurrentShowId();
		if (!showId.empty()) Jellyfin_Cache_GetSeasons(showId, seasons);
		std::string sub;
		if (m_currentShow >= 0 && m_currentShow < (int)items.size())
			sub = items[m_currentShow].name;
		if (m_currentSeason >= 0 && m_currentSeason < (int)seasons.size()) {
			std::string code = EpisodeCode(seasons[m_currentSeason].index, e.index);
			if (!code.empty()) { sub += "  \xC2\xB7  "; sub += code; }
		}
		PlayRatingKey(e.id, e.name, sub, e.posTicks);
	}

	void QueueEpisodeArt(int i)
	{
		std::vector<JellyfinEpisode> eps;
		std::string seasonId = CurrentSeasonId();
		if (seasonId.empty()) return;
		Jellyfin_Cache_GetEpisodes(seasonId, eps);
		if (i < 0 || i >= (int)eps.size()) return;
		QueueShowArt();
		if (!eps[i].primaryTag.empty())
			Jellyfin_QueueArtDownload(eps[i].id, eps[i].primaryTag);
	}

	CStrObject* GetEpisodeArtPath(int i)
	{
		std::vector<JellyfinEpisode> eps;
		std::string seasonId = CurrentSeasonId();
		if (seasonId.empty()) return ArtOrLogo("");
		Jellyfin_Cache_GetEpisodes(seasonId, eps);
		if (i < 0 || i >= (int)eps.size()) return ArtOrLogo("");
		return ArtOrLogo(eps[i].id, seasonId, CurrentShowId());
	}

	CStrObject* GetSubtitle(int view, int i)
	{
		if (i < 0) return new CStrObject;
		std::string s;
		if (view == 1) {
			std::vector<JellyfinItem> items = CurrentItems();
			if (i >= (int)items.size()) return new CStrObject;
			const JellyfinItem& it = items[i];
			std::string t = it.type;
			for (auto& c : t) c = (char)toupper((unsigned char)c);
			s = t;
			if (!it.year.empty()) { s += "  \xC2\xB7  "; s += it.year; }
			return new CStrObject(_T(s.c_str()));
		}
		if (view == 2) {
			std::vector<JellyfinSeason> seasons;
			std::string showId = CurrentShowId();
			if (showId.empty()) return new CStrObject;
			Jellyfin_Cache_GetSeasons(showId, seasons);
			if (i >= (int)seasons.size()) return new CStrObject;
			s = "SEASON ";
			s += seasons[i].index;
			return new CStrObject(_T(s.c_str()));
		}
		if (view == 3) {
			std::vector<JellyfinEpisode> eps;
			std::string seasonId = CurrentSeasonId();
			if (seasonId.empty()) return new CStrObject;
			Jellyfin_Cache_GetEpisodes(seasonId, eps);
			if (i >= (int)eps.size()) return new CStrObject;
			s = "S";
			std::vector<JellyfinSeason> seasons;
			std::string showId = CurrentShowId();
			if (!showId.empty()) Jellyfin_Cache_GetSeasons(showId, seasons);
			if (m_currentSeason >= 0 && m_currentSeason < (int)seasons.size())
				s += seasons[m_currentSeason].index;
			if (!eps[i].index.empty()) { s += "E"; s += eps[i].index; }
			return new CStrObject(_T(s.c_str()));
		}
		return new CStrObject;
	}

	CStrObject* GetSummary(int view, int i)
	{
		if (i < 0) return new CStrObject;
		std::string s;
		if (view == 1) {
			std::vector<JellyfinItem> items = CurrentItems();
			if (i >= (int)items.size()) return new CStrObject;
			s = items[i].overview;
		} else if (view == 3) {
			std::vector<JellyfinEpisode> eps;
			std::string seasonId = CurrentSeasonId();
			if (seasonId.empty()) return new CStrObject;
			Jellyfin_Cache_GetEpisodes(seasonId, eps);
			if (i >= (int)eps.size()) return new CStrObject;
			s = eps[i].overview;
		} else {
			return new CStrObject;
		}
		const size_t kMax = 140;
		if (s.size() > kMax) {
			s.resize(kMax - 1);
			s += "\xE2\x80\xA6";
		}
		return new CStrObject(_T(s.c_str()));
	}

	// Y: the info card. Title, the facts line, genres, where you left off,
	// then the plot. Seasons show the show's plot.
	CStrObject* GetInfoText(int view, int i)
	{
		if (i < 0) return new CStrObject;
		std::string body;
		if (view == 1) {
			std::vector<JellyfinItem> items = CurrentItems();
			if (i >= (int)items.size()) return new CStrObject;
			const JellyfinItem& it = items[i];
			body = it.name;
			AppendLine(body, FactsLine(it.year, Secs(it.runTicks), it.rating, it.stars));
			AppendLine(body, it.genres);
			if (it.type == "Series" && it.unplayed > 0 && !it.played) {
				char buf[48]; snprintf(buf, sizeof(buf), "%d episode%s left", it.unplayed, it.unplayed == 1 ? "" : "s");
				AppendLine(body, buf);
			} else {
				AppendLine(body, WatchLine(it.played, Secs(it.posTicks), Secs(it.runTicks)));
			}
			if (Markable(it)) AppendLine(body, MarkHint(it.played));
			if (!it.overview.empty()) { body += "\n\n"; body += it.overview; }
		}
		else if (view == 2) {
			std::vector<JellyfinItem> items = CurrentItems();
			if (m_currentShow < 0 || m_currentShow >= (int)items.size()) return new CStrObject;
			const JellyfinItem& sh = items[m_currentShow];
			std::vector<JellyfinSeason> seasons;
			Jellyfin_Cache_GetSeasons(sh.id, seasons);
			body = sh.name;
			if (i < (int)seasons.size()) { body += "  \xC2\xB7  "; body += seasons[i].name; }
			AppendLine(body, FactsLine(sh.year, 0, sh.rating, sh.stars));
			AppendLine(body, sh.genres);
			if (i < (int)seasons.size() && seasons[i].unplayed > 0 && !seasons[i].played) {
				char buf[48]; snprintf(buf, sizeof(buf), "%d episode%s left", seasons[i].unplayed, seasons[i].unplayed == 1 ? "" : "s");
				AppendLine(body, buf);
			} else if (i < (int)seasons.size() && seasons[i].played) {
				AppendLine(body, "Watched");
			}
			if (i < (int)seasons.size()) AppendLine(body, MarkHint(seasons[i].played));
			if (!sh.overview.empty()) { body += "\n\n"; body += sh.overview; }
		}
		else if (view == 3) {
			std::vector<JellyfinEpisode> eps;
			std::string seasonId = CurrentSeasonId();
			if (seasonId.empty()) return new CStrObject;
			Jellyfin_Cache_GetEpisodes(seasonId, eps);
			if (i >= (int)eps.size()) return new CStrObject;
			const JellyfinEpisode& e = eps[i];
			body = EpisodeLabel(e.index, e.name);
			std::vector<JellyfinItem> items = CurrentItems();
			if (m_currentShow >= 0 && m_currentShow < (int)items.size())
				AppendLine(body, items[m_currentShow].name);
			AppendLine(body, FactsLine("", Secs(e.runTicks), e.rating, e.stars));
			AppendLine(body, WatchLine(e.played, Secs(e.posTicks), Secs(e.runTicks)));
			AppendLine(body, MarkHint(e.played));
			if (!e.overview.empty()) { body += "\n\n"; body += e.overview; }
		}
		return new CStrObject(_T(ClipInfo(body).c_str()));
	}

	int IsSignedIn() { return Jellyfin_HasToken() ? 1 : 0; }

	// What an empty list says instead: still loading, the server didn't
	// answer, or there's nothing there. Empty while there are rows.
	CStrObject* GetStatus(int view)
	{
		return new CStrObject(_T(StatusFor(view).c_str()));
	}

	static std::string Unreachable()
	{
		std::string host = Jellyfin_GetServerUrl();
		size_t scheme = host.find("://");
		if (scheme != std::string::npos) host = host.substr(scheme + 3);
		host = host.substr(0, host.find('/'));
		return "Can't reach " + (host.empty() ? std::string("the server") : host);
	}

	template <typename T>
	static std::string Pending(bool cached, const std::vector<T>& rows, const std::string& id, const char* none)
	{
		if (cached) return rows.empty() ? none : "";
		return Jellyfin_FetchFailed(id) ? Unreachable() : "Loading...";
	}

	std::string StatusFor(int view)
	{
		if (!Jellyfin_HasToken()) return "Sign in to Jellyfin in Settings";
		if (!Jellyfin_SyncReady()) return "Loading...";
		if (view == 0) {
			if (Jellyfin_SyncFailed()) return Unreachable();
			return Jellyfin_Cache_GetLibraries().empty() ? "No libraries" : "";
		}
		if (view == 1) {
			if (m_folders.empty()) return CurrentItems().empty() ? "Nothing here" : "";
			std::vector<JellyfinItem> kids;
			bool cached = Jellyfin_Cache_GetChildren(m_folders.back().id, kids);
			return Pending(cached, kids, m_folders.back().id, "Nothing here");
		}
		if (view == 2) {
			std::string showId = CurrentShowId();
			std::vector<JellyfinSeason> seasons;
			bool cached = Jellyfin_Cache_GetSeasons(showId, seasons);
			return Pending(cached, seasons, showId, "No seasons");
		}
		std::string seasonId = CurrentSeasonId();
		std::vector<JellyfinEpisode> eps;
		bool cached = Jellyfin_Cache_GetEpisodes(seasonId, eps);
		return Pending(cached, eps, seasonId, "No episodes");
	}
};

std::string CJellyfinLibrary::s_playing;

IMPLEMENT_NODE("JellyfinLibrary", CJellyfinLibrary, CNode)

START_NODE_PROPS(CJellyfinLibrary, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CJellyfinLibrary
START_NODE_FUN(CJellyfinLibrary, CNode)
	NODE_FUN_IV(GetCount)
	NODE_FUN_SI(GetName)
	NODE_FUN_SI(GetMeta)
	NODE_FUN_VI(SetCurrent)
	NODE_FUN_SV(GetServerName)
	NODE_FUN_IV(IsSyncReady)
	NODE_FUN_SV(GetSyncPhase)
	NODE_FUN_IV(GetSyncProgress)
	NODE_FUN_IV(GetItemCount)
	NODE_FUN_SI(GetItemTitle)
	NODE_FUN_SI(GetItemType)
	NODE_FUN_VI(QueueItemArt)
	NODE_FUN_SI(GetItemArtPath)
	NODE_FUN_VI(PlayItem)
	NODE_FUN_SII(GetSubtitle)
	NODE_FUN_SII(GetSummary)
	NODE_FUN_SII(GetInfoText)
	NODE_FUN_VI(DrillIntoShow)
	NODE_FUN_IV(GetSeasonCount)
	NODE_FUN_SI(GetSeasonTitle)
	NODE_FUN_VI(DrillIntoSeason)
	NODE_FUN_VI(QueueSeasonArt)
	NODE_FUN_SI(GetSeasonArtPath)
	NODE_FUN_IV(GetEpisodeCount)
	NODE_FUN_SI(GetEpisodeTitle)
	NODE_FUN_VI(PlayEpisode)
	NODE_FUN_VI(QueueEpisodeArt)
	NODE_FUN_SI(GetEpisodeArtPath)
	NODE_FUN_IV(IsSignedIn)
	NODE_FUN_VI(DrillIntoFolder)
	NODE_FUN_IV(GetFolderDepth)
	NODE_FUN_IV(LeaveFolder)
	NODE_FUN_SV(GetListTitle)
	NODE_FUN_SI(GetLibraryRow)
	NODE_FUN_SI(GetStatus)
	NODE_FUN_SI(GetItemName)
	NODE_FUN_VI(SetStartOver)
	NODE_FUN_IV(GetWatchStamp)
	NODE_FUN_III(CanStartOver)
	NODE_FUN_III(ToggleWatched)
END_NODE_FUN()


// ============================================================================
// CGamesCollection - XAP-callable view over the VGames database (games.ini),
// filtered by category. Pairs with the desktop harddrive scene to replace
// the Xbox-era cache.ini batched paging (which capped at 25 per segment and
// truncated when buffers overflowed). Auto-refreshes by tracking VGames'
// generation counter, so adds/edits from Title Maker show up immediately
// without anyone calling Refresh().
// ============================================================================

#include <algorithm>
#include "virtual_games.h"

extern void DesktopLaunchTitle(const char* devicePath);

class CGamesCollection : public CNode
{
public:
	CGamesCollection() : m_gen(0), m_showHidden(false)
	{
		m_category[0] = 0;
		m_group[0] = 0;
	}

	DECLARE_NODE(CGamesCollection, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	// Current filter
	char m_category[32];
	// Folder we've drilled into, "/" separated, empty at the top of a category
	char m_group[128];
	// Folder rows, the distinct next path segment below m_group
	std::vector<std::string> m_folders;
	// Indices into g_vgames.games, sorted alphabetically by name
	std::vector<int> m_slice;
	// Last seen generation. Re-filter when it changes.
	unsigned int m_gen;
	// Hidden titles come back so a scene can offer to unhide them.
	bool m_showHidden;

	// Rows are folders first, then titles, so an index below the folder count
	// addresses m_folders and anything above it addresses m_slice.
	int TitleIndex(int row) const { return row - (int)m_folders.size(); }

	void EnsureFresh()
	{
		VGames_Load();
		if (m_gen == g_vgames.generation && !(m_slice.empty() && m_folders.empty())) return;
		Rebuild();
	}

	void Rebuild()
	{
		m_slice.clear();
		m_folders.clear();
		if (!m_category[0]) {
			m_gen = g_vgames.generation;
			return;
		}
		if (VGames_IsMarkList(m_category)) {
			VGames_MarkList(m_category, m_slice);
			m_gen = g_vgames.generation;
			return;
		}
		// Match what xboxfs / Title Maker do: drive defaults to "E" for the
		// harddrive view. Future-proofed for F/G if those ever come back.
		int tmp[VGAMES_MAX];
		int n = VGames_GetForDirectory("E", m_category, tmp, VGAMES_MAX);
		size_t glen = strlen(m_group);

		for (int i = 0; i < n; i++) {
			if (!m_showHidden && !VGames_IsShown(g_vgames.games[tmp[i]])) continue;
			const char* grp = g_vgames.games[tmp[i]].group;
			const char* rest;
			if (glen == 0) {
				rest = grp;
			} else {
				// Must sit under m_group exactly, not merely share a prefix,
				// so "Mario" can't swallow "Mario Series".
				if (strncasecmp(grp, m_group, glen) != 0) continue;
				if (grp[glen] == '\0')     rest = grp + glen;
				else if (grp[glen] == '/') rest = grp + glen + 1;
				else                       continue;
			}

			if (!*rest) { m_slice.push_back(tmp[i]); continue; }

			const char* slash = strchr(rest, '/');
			std::string seg = slash ? std::string(rest, slash - rest) : std::string(rest);
			bool have = false;
			for (size_t f = 0; f < m_folders.size() && !have; f++)
				have = (strcasecmp(m_folders[f].c_str(), seg.c_str()) == 0);
			if (!have) m_folders.push_back(seg);
		}

		// Alphabetical sort, case-insensitive, matches scene expectation.
		std::sort(m_folders.begin(), m_folders.end(), [](const std::string& a, const std::string& b) {
			return strcasecmp(a.c_str(), b.c_str()) < 0;
		});
		std::sort(m_slice.begin(), m_slice.end(), [](int a, int b) {
			return strcasecmp(g_vgames.games[a].name, g_vgames.games[b].name) < 0;
		});
		m_gen = g_vgames.generation;
	}

	// Reload from disk and re-filter. Hooked to a future hotkey for paranoid
	// users; the generation counter normally makes this redundant.
	void Refresh()
	{
		VGames_Reload();
		Rebuild();
	}

	// Point the node at a category. "Games" / "Emulators" / "Homebrew" /
	// "Applications" / "Dashboards". Empty string resets to no-list.
	void SetCategory(const TCHAR* cat)
	{
		if (cat) {
			strncpy(m_category, cat, sizeof(m_category) - 1);
			m_category[sizeof(m_category) - 1] = 0;
		} else {
			m_category[0] = 0;
		}
		// Entering a category always lands at its top.
		m_group[0] = 0;
		Rebuild();
	}

	int GetCount()
	{
		EnsureFresh();
		return (int)m_folders.size() + (int)m_slice.size();
	}

	// How many live titles sit in a category, without disturbing the current
	// filter. The launcher uses it to decide whether a bolted-on category
	// (ES-DE) is worth showing a row for at all.
	int CountInCategory(const TCHAR* cat)
	{
		VGames_Load();
		if (!cat || !*cat) return 0;
		if (VGames_IsMarkList(cat)) {
			std::vector<int> list;
			VGames_MarkList(cat, list);
			return (int)list.size();
		}
		int n = 0;
		for (int i = 0; i < g_vgames.count; i++) {
			const VirtualGame& g = g_vgames.games[i];
			if (VGames_IsShown(g) && strcasecmp(g.category, cat) == 0) n++;
		}
		return n;
	}

	// The DB row behind a list row, or -1 for folders and anything past the end.
	int Row(int i)
	{
		EnsureFresh();
		int t = TitleIndex(i);
		return (t >= 0 && t < (int)m_slice.size()) ? m_slice[t] : -1;
	}

	int IsFavorite(int i) { int r = Row(i); return (r >= 0 && g_vgames.games[r].favorite) ? 1 : 0; }
	int IsHidden(int i)   { int r = Row(i); return (r >= 0 && g_vgames.games[r].hidden) ? 1 : 0; }
	void SetFavorite(int i, int on) { VGames_SetFavorite(Row(i), on != 0); }
	void SetHidden(int i, int on)   { VGames_SetHidden(Row(i), on != 0); }

	// 1 lists hidden titles too, 0 goes back to normal.
	void ShowHidden(int on)
	{
		m_showHidden = on != 0;
		Rebuild();
	}

	// "2026-11-12 18:40", or empty for never.
	CStrObject* GetLastPlayed(int i)
	{
		int r = Row(i);
		if (r < 0 || !g_vgames.games[r].lastPlayed) return new CStrObject;
		time_t t = (time_t)g_vgames.games[r].lastPlayed;
		char buf[32];
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", localtime(&t));
		return new CStrObject(buf);
	}

	// 1 when the row drills down instead of launching.
	int IsFolder(int i)
	{
		EnsureFresh();
		return (i >= 0 && i < (int)m_folders.size()) ? 1 : 0;
	}

	// Drill into a folder row. Anything else is ignored.
	void EnterFolder(int i)
	{
		EnsureFresh();
		if (i < 0 || i >= (int)m_folders.size()) return;
		char next[sizeof(m_group)];
		if (m_group[0]) snprintf(next, sizeof(next), "%s/%s", m_group, m_folders[i].c_str());
		else            snprintf(next, sizeof(next), "%s", m_folders[i].c_str());
		strcpy(m_group, next);
		Rebuild();
	}

	// 1 if we climbed a level, 0 if we were already at the top of the
	// category. The scene uses that to decide whether B pops a folder or
	// leaves the title list.
	int GoUpFolder()
	{
		if (!m_group[0]) return 0;
		char* slash = strrchr(m_group, '/');
		if (slash) *slash = 0;
		else       m_group[0] = 0;
		Rebuild();
		return 1;
	}

	// Breadcrumb for the list header, empty at the top of a category.
	CStrObject* GetGroupPath()
	{
		return new CStrObject(m_group);
	}

	CStrObject* GetTitle(int i)
	{
		EnsureFresh();
		if (i >= 0 && i < (int)m_folders.size())
			return new CStrObject(m_folders[i].c_str());
		int t = TitleIndex(i);
		if (t < 0 || t >= (int)m_slice.size()) return new CStrObject;
		return new CStrObject(g_vgames.games[m_slice[t]].name);
	}

	CStrObject* GetTitleID(int i)
	{
		EnsureFresh();
		int t = TitleIndex(i);
		if (t < 0 || t >= (int)m_slice.size()) return new CStrObject;
		return new CStrObject(g_vgames.games[m_slice[t]].titleID);
	}

	CStrObject* GetLaunchURL(int i)
	{
		EnsureFresh();
		int t = TitleIndex(i);
		if (t < 0 || t >= (int)m_slice.size()) return new CStrObject;
		return new CStrObject(g_vgames.games[m_slice[t]].launch);
	}

	// Returns the Xbox-style icon path. xbox_path.h intercepts the load and
	// redirects to Configs/icons/<titleID>.{jpg,png} via VGames_GetIconPath.
	CStrObject* GetIcon(int i)
	{
		EnsureFresh();
		// Folder rows have no art; the scene draws its own folder glyph.
		int t = TitleIndex(i);
		if (t < 0 || t >= (int)m_slice.size()) return new CStrObject;
		const VirtualGame& g = g_vgames.games[m_slice[t]];
		// Streaming entries (xCloud) carry box art in the shared art cache,
		// keyed by titleId, not a per-folder icon.jpg.
		if (VGames_IsStreaming(g))
			return new CStrObject(_T(Xcloud_ArtCachePath(g.titleID).c_str()));
		// Nothing registered: hand back empty so the scene keeps the default
		// orb. Returning a path that isn't there makes the loader shout once
		// per row, which on a few thousand unscraped roms is all you see.
		if (!VGames_GetIconPath(m_slice[t]))
			return new CStrObject;
		char buf[MAX_PATH];
		snprintf(buf, sizeof(buf), "%s:\\%s\\%s\\icon.jpg",
		         g.drive[0] ? g.drive : "E",
		         g.category[0] ? g.category : "Games",
		         g.name);
		return new CStrObject(buf);
	}

	// Fire the launch. DesktopLaunchTitle takes the Xbox-style folder path
	// the dashboard uses everywhere else; the launch URL itself comes from
	// the VGames entry that path maps back to.
	void LaunchTitle(int i)
	{
		EnsureFresh();
		int t = TitleIndex(i);
		if (t < 0 || t >= (int)m_slice.size()) return;
		const VirtualGame& g = g_vgames.games[m_slice[t]];
		char devicePath[MAX_PATH];
		snprintf(devicePath, sizeof(devicePath),
		         "\\Device\\Harddisk0\\Partition1\\%s\\%s",
		         g.category[0] ? g.category : "Games",
		         g.name);
		DesktopLaunchTitle(devicePath);
	}
};

IMPLEMENT_NODE("GamesCollection", CGamesCollection, CNode)

START_NODE_PROPS(CGamesCollection, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CGamesCollection
START_NODE_FUN(CGamesCollection, CNode)
	NODE_FUN_VS(SetCategory)
	NODE_FUN_IV(GetCount)
	NODE_FUN_IS(CountInCategory)
	NODE_FUN_II(IsFolder)
	NODE_FUN_VI(EnterFolder)
	NODE_FUN_IV(GoUpFolder)
	NODE_FUN_SV(GetGroupPath)
	NODE_FUN_SI(GetTitle)
	NODE_FUN_SI(GetTitleID)
	NODE_FUN_SI(GetLaunchURL)
	NODE_FUN_SI(GetIcon)
	NODE_FUN_VI(LaunchTitle)
	NODE_FUN_VV(Refresh)
	NODE_FUN_II(IsFavorite)
	NODE_FUN_VII(SetFavorite)
	NODE_FUN_II(IsHidden)
	NODE_FUN_VII(SetHidden)
	NODE_FUN_VI(ShowHidden)
	NODE_FUN_SI(GetLastPlayed)
END_NODE_FUN()


// ============================================================================
// CDisplay. XAP-callable view over the desktop window's resolution + mode.
// Same g_windowResolution / g_windowMode the ImGui Settings -> Display tab
// drives, so changes from either surface stay in sync.
// ============================================================================

extern int  g_windowResolution;
extern int  g_windowMode;
extern bool g_displayChangeRequested;
extern void SaveDesktopSettings();

class CDisplay : public CNode
{
public:
	CDisplay() {}
	DECLARE_NODE(CDisplay, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	int GetResolution() { return g_windowResolution; }
	int GetMode()       { return g_windowMode; }

	void SetResolution(int n)
	{
		if (n != 0 && n != 720 && n != 1080 && n != 1440 && n != 2160) return;
		if (g_windowResolution == n) return;
		g_windowResolution = n;
		g_displayChangeRequested = true;
		SaveDesktopSettings();
	}

	void SetMode(int n)
	{
		if (n < 0 || n > 2) return;
		if (g_windowMode == n) return;
		g_windowMode = n;
		g_displayChangeRequested = true;
		SaveDesktopSettings();
	}

	// Convenience predicates the scene reads to render YES/NO labels.
	int IsResolution720()  { return g_windowResolution == 720; }
	int IsResolution1080() { return g_windowResolution == 1080; }
	int IsResolution1440() { return g_windowResolution == 1440; }
	int IsResolution2160() { return g_windowResolution == 2160; }
	int IsFullscreen()     { return g_windowMode != 0; }
};

IMPLEMENT_NODE("Display", CDisplay, CNode)

START_NODE_PROPS(CDisplay, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CDisplay
START_NODE_FUN(CDisplay, CNode)
	NODE_FUN_IV(GetResolution)
	NODE_FUN_IV(GetMode)
	NODE_FUN_VI(SetResolution)
	NODE_FUN_VI(SetMode)
	NODE_FUN_IV(IsResolution720)
	NODE_FUN_IV(IsResolution1080)
	NODE_FUN_IV(IsResolution1440)
	NODE_FUN_IV(IsResolution2160)
	NODE_FUN_IV(IsFullscreen)
END_NODE_FUN()
