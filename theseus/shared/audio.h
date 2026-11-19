// audio.h: the AudioClip node scripts use to play a sound: a wave file,
// or a soundtrack song as st:<id>. Built by shared/audio_system.cpp.
// render/tmap_system.cpp reads m_transportMode and the sample buffer for
// the visualizer, so the member order here is load bearing.

#pragma once

// transportMode values the scripts see.
#define TRANSPORT_STOP  0
#define TRANSPORT_PLAY  1
#define TRANSPORT_PAUSE 2

class CAudioBuf;

class CAudioClip : public CTimeDepNode
{
	DECLARE_NODE(CAudioClip, CTimeDepNode)
public:
	CAudioClip();
	~CAudioClip();

	TCHAR* m_url;
	void SetUrl(const TCHAR* AudioFile);

	bool m_capturedLoop;      // m_loop as it was when Play started
	bool m_isActive;
	bool m_sendProgress;      // keep m_progress updated and fire OnProgressChanged
	bool m_pause_on_moving;   // hold the sound while the menus change level
	float m_progress;
	bool m_removeVoice;
	int m_transportMode;
	int m_lastTransportMode;

	void Play();
	void Pause();
	void PlayOrPause();
	void Stop();
	int getMinutes();
	int getSeconds();

	void* GetSampleBuffer();
	int GetSampleBufferSize();

	HRESULT Initialize();
	HRESULT Cleanup();

protected:
	bool m_dirty;             // url changed; the sound gets loaded again
	bool m_unpauseNeeded;     // paused for a level change, resume after
	float m_lastVolume;
	float m_lastPan;
	float m_lastFrequency;
	float m_fade;             // seconds a volume or pan change glides over
	float m_volume;
	float m_pan;
	float m_frequency;

	CAudioBuf* m_sound;

	bool OpenWaveFile();
	bool OpenMP3Soundtrack();

#ifdef _XBOX
	XBOXADPCMWAVEFORMAT m_format;
#else
	struct { struct { WORD wFormatTag; WORD nChannels; DWORD nSamplesPerSec; DWORD nAvgBytesPerSec; WORD nBlockAlign; WORD wBitsPerSample; WORD cbSize; } wfx; WORD wSamplesPerBlock; } m_format;
#endif

	void Advance(float nSeconds);
	void OnIsActiveChanged();
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};
