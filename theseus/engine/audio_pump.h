// audio_pump.h: DirectSound buffer class hierarchy used by the audio
// system. CAudioBuf is the static-buffer base used for short SFX;
// streaming subclasses (CAudioPump, CFilePump, etc.) layer worker-thread
// segment-fed playback on top. Companion to shared/audio_system.cpp.
// See docs/decomp/AudioSystem.md.

#pragma once

#ifdef _XBOX
#define _D3DTYPES_H_
#include <dsound.h>
#endif

// Static audio buffer for short SFX clips.
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
	virtual float GetPlaybackTime();
	virtual float GetPlaybackLength(); // NOTE: will return 0 if unknown!
	virtual bool IsPlaying();

	virtual void Pause(bool bPause);
	inline bool IsPaused() const { return m_paused; }
	
	virtual void* GetSampleBuffer();
	virtual DWORD GetSampleBufferSize();

	void SetAttenuation(float nAttenuation); // 0..100 dB
	void SetPan(float nPan); // -100..100 dB
	void SetFrequency(float nFrequency); // 0 (normal), or 100..100,000

protected:
	bool m_loop;
	bool m_paused;
	int m_bufferBytes;
	int m_bytesPerSecond;
#ifdef _XBOX
	LPDIRECTSOUNDBUFFER m_dsBuffer;
#else
	void* m_dsBuffer;
#endif
};


// Double-buffered Audio Buffer for streaming...
class CAudioPump : public CAudioBuf
{
public:
	CAudioPump();
	virtual ~CAudioPump();

	HRESULT Initialize(DWORD dwStackSize, WAVEFORMATEX* pWaveFormat, int nBufferBytes, int nSegmentsPerBuffer = 4, int nPrebufferSegments = 1);

	virtual bool Play(bool bLoop = false);
	virtual void Stop();
	virtual bool IsPlaying();
	virtual float GetPlaybackTime();
	virtual void Pause(bool bPause);
	virtual float GetPlaybackLength(); // NOTE: will return 0 if unknown!


    int m_segmentsPerBuffer;

protected:
	enum
    {
        PUMPSTATE_STOPPED,
        PUMPSTATE_BUFFERING,
        PUMPSTATE_RUNNING,
        PUMPSTATE_STOPPING,
    };

    static DWORD CALLBACK StartThread(LPVOID pvContext);
	DWORD ThreadProc();

	HANDLE m_playThread;
	HANDLE m_terminateEvent;
	HANDLE m_runEvent;
	HANDLE* m_notifyEvents;        // m_segmentsPerBuffer events
    HANDLE m_mutex;

    DWORD m_prevCursor;
	int m_completedBuffers;
    int m_filledBuffers;
    bool* m_bufferFilled;          // m_segmentsPerBuffer bools
    int m_pumpState;

    int m_prebufferSegments;

	bool FillBuffer(int nBuffer);
	virtual int GetData(BYTE* pbBuffer, int cbBuffer) = 0;
	virtual void OnAudioEnd() {} 
	
};


// Audio Buffer for streaming large files...
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
	void* m_buffer;
	float m_playbackLength;
	DWORD m_startPos;
    DWORD m_bufferSize;
};

// MP3 streaming decoder for soundtrack playback
// mp3dec_t is defined in minimp3.h; include it before audio_pump.h
// or use an opaque pointer (we use void* internally, cast in .cpp)

class CMP3Pump : public CAudioPump
{
public:
	CMP3Pump();
	virtual ~CMP3Pump();

	HRESULT Initialize(const char* szFilePath);

	virtual float GetPlaybackLength();
	virtual void* GetSampleBuffer();
	virtual DWORD GetSampleBufferSize();
	virtual void Stop();

protected:
	virtual int GetData(BYTE* pbBuffer, int cbBuffer);

	BYTE* m_fileData;       // entire MP3 file in memory
	int   m_fileSize;
	int   m_fileOffset;     // current read position
	void* m_decoder;        // mp3dec_t*, opaque here to avoid header dep
	void* m_buffer;
	DWORD m_bufferSize;
	float m_playbackLength;
	int   m_channels;
	int   m_sampleRate;
};
