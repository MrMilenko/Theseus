// audio.h: CAudioClip XAP node, the script-facing audio player.
// Wraps CAudioBuf (DirectSound voice) plus the MP3 / WAV file
// loaders. Companion to shared/audio_system.cpp.

#pragma once

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

	bool m_capturedLoop;   // local copy of CTimeDepNode::m_loop captured at Play() time
	bool m_isActive;

    bool m_sendProgress;
    bool m_pause_on_moving;
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
	bool m_dirty;
    bool m_unpauseNeeded;
	float m_lastVolume;
	float m_lastPan;
	float m_lastFrequency;
	float m_fade;
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
