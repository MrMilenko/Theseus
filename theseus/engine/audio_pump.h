// audio_pump.h: the sound buffers behind an AudioClip. CAudioBuf holds a
// whole short sound in one DirectSound buffer; CAudioPump streams through
// a ring of segments refilled by a worker thread, and CFilePump (wave
// files) and CMP3Pump (soundtracks) feed it. Built by
// shared/audio_system.cpp. See docs/decomp/AudioSystem.md.

#pragma once

#ifdef _XBOX
#define _D3DTYPES_H_
#include <dsound.h>
#endif

// A whole sound in one buffer.
class CAudioBuf
{
public:
	CAudioBuf();
	virtual ~CAudioBuf();

	HRESULT Initialize(WAVEFORMATEX* pWaveFormat, int nBufferBytes, const void* pvSamples = NULL);
	void* Lock();
	void Unlock(void* pvBuffer);

	virtual bool Play(bool bLoop = false);
	virtual void Stop();
	virtual void Pause(bool bPause);
	virtual bool IsPlaying();
	virtual float GetPlaybackTime();
	virtual float GetPlaybackLength();   // 0 when the length isn't known
	virtual void* GetSampleBuffer();
	virtual DWORD GetSampleBufferSize();

	bool IsPaused() const { return m_paused; }

	void SetAttenuation(float nAttenuation);   // dB down, 0..100
	void SetPan(float nPan);
	void SetFrequency(float nFrequency);       // 0 plays at the recorded rate

protected:
	bool m_loop;
	bool m_paused;
	int m_bufferBytes;        // one segment for a pump
	int m_bytesPerSecond;
#ifdef _XBOX
	LPDIRECTSOUNDBUFFER m_dsBuffer;
#else
	void* m_dsBuffer;
#endif
};

// A ring of segments a worker thread keeps filled from GetData.
class CAudioPump : public CAudioBuf
{
public:
	CAudioPump();
	virtual ~CAudioPump();

	HRESULT Initialize(DWORD dwStackSize, WAVEFORMATEX* pWaveFormat, int nBufferBytes, int nSegmentsPerBuffer = 4, int nPrebufferSegments = 1);

	virtual bool Play(bool bLoop = false);
	virtual void Stop();
	virtual void Pause(bool bPause);
	virtual bool IsPlaying();
	virtual float GetPlaybackTime();
	virtual float GetPlaybackLength();

protected:
	enum { PUMP_STOPPED, PUMP_BUFFERING, PUMP_RUNNING, PUMP_DRAINING };

	static DWORD CALLBACK ThreadEntry(LPVOID pvContext);
	DWORD ThreadProc();
	bool FillBuffer(int nSegment);

	// Fills up to cbBuffer bytes; fewer means the sound has ended.
	virtual int GetData(BYTE* pbBuffer, int cbBuffer) = 0;
	virtual void OnAudioEnd() {}

	int m_segments;
	int m_prebuffer;
	int m_state;
	int m_filled;             // segments filled since the last stop
	int m_wraps;              // times the play cursor went round
	DWORD m_lastCursor;
	bool* m_segmentFilled;
	HANDLE* m_segmentEvents;  // set as playback crosses each segment end
	HANDLE m_quit;
	HANDLE m_lock;
	HANDLE m_worker;
};

// Streams the data chunk of an open wave file.
class CFilePump : public CAudioPump
{
public:
	CFilePump();
	virtual ~CFilePump();

	HRESULT Initialize(HANDLE hFile, int nFileBytes, WAVEFORMATEX* pFormat);

	virtual void Stop();
	virtual float GetPlaybackLength();
	virtual void* GetSampleBuffer();
	virtual DWORD GetSampleBufferSize();

protected:
	virtual int GetData(BYTE* pbBuffer, int cbBuffer);

	HANDLE m_file;
	DWORD m_dataStart;        // file offset of the first sample
	float m_seconds;
	void* m_lastBlock;
	DWORD m_blockBytes;
};

// Decodes an MP3 soundtrack with minimp3 (see minimp3_decode.cpp).
class CMP3Pump : public CAudioPump
{
public:
	CMP3Pump();
	virtual ~CMP3Pump();

	HRESULT Initialize(const char* szFilePath);

	virtual void Stop();
	virtual float GetPlaybackLength();
	virtual void* GetSampleBuffer();
	virtual DWORD GetSampleBufferSize();

protected:
	virtual int GetData(BYTE* pbBuffer, int cbBuffer);

	BYTE* m_fileData;         // the whole file
	int   m_fileSize;
	int   m_fileOffset;       // next byte to decode
	void* m_decoder;          // mp3dec_t, kept opaque so callers needn't include minimp3.h
	void* m_buffer;
	DWORD m_bufferSize;
	float m_playbackLength;
	int   m_channels;
	int   m_sampleRate;
};
