// audio_system.cpp: sound playback for the Xbox build. CAudioBuf holds a
// short sound whole, CAudioPump streams a long one through a ring of
// segments, CFilePump reads wave data for it and CMP3Pump decodes
// soundtrack MP3s (Theseus's, 5960 streams WMA). On top sit the AudioClip
// and PeriodicAudioGroup nodes. Rebuilt from the 5960 retail XBE
// (0x61890..0x63320); see docs/decomp/AudioSystem.md. Theseus creates
// buffers through DSoundManager instead of 5960's AppShell_CreateSoundBuffer.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "lerper.h"
#include "file_util.h"
#include "audio.h"
#include "audio_pump.h"
#include "dsound_manager.h"
#include "runner.h"

// minimp3 types for CMP3Pump (implementation in minimp3_decode.cpp)
#define MINIMP3_NO_STDINT
#include "toolbox/xboxinternals.h"
#include "minimp3.h"

// removeVoice of the clip that advanced last (5960 keeps it at 0x17e768).
bool g_removeVoice = false;
extern bool g_bLevelSwitching;

static DWORD LoopFlag(bool loop)
{
	return loop ? DSBPLAY_LOOPING : 0;
}

// =========================================================================
// CAudioBuf
// =========================================================================

// 5960 builds these inline where a short wave file is loaded (0x61d20):
// no buffer yet, not looping, not paused.
CAudioBuf::CAudioBuf()
{
	m_loop = false;
	m_paused = false;
	m_bufferBytes = 0;
	m_dsBuffer = NULL;
}

// CAudioBuf::~CAudioBuf
// Reconstructed from 5960 retail XBE @ 0x62960: stop and release the buffer.
CAudioBuf::~CAudioBuf()
{
	if (m_dsBuffer == NULL)
		return;
	m_dsBuffer->Stop();
	m_dsBuffer->Release();
}

// CAudioBuf::Initialize(format, bytes, samples)
// 5960 inlines this into the wave loader: remember the size and byte rate,
// make a buffer with volume and frequency control. Theseus can also hand
// the samples over up front.
HRESULT CAudioBuf::Initialize(WAVEFORMATEX* pWaveFormat, int nBufferBytes, const void* pvSamples)
{
	m_bufferBytes = nBufferBytes;
	m_bytesPerSecond = pWaveFormat->nAvgBytesPerSec;

	DSoundManager* ds = DSoundManager::Instance();
	HRESULT hr = ds->DSoundCreateSoundBuffer(pWaveFormat, nBufferBytes, DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY, &m_dsBuffer);
	if (FAILED(hr))
		return hr;
	if (m_dsBuffer == NULL)
		return E_INVALIDARG;

	if (pvSamples != NULL)
	{
		hr = ds->DSoundSetSoundBufferData(m_dsBuffer, 0, nBufferBytes, pvSamples);
		if (FAILED(hr))
			return hr;
	}
	return S_OK;
}

// CAudioBuf::Lock / Unlock
// Reconstructed from 5960 retail XBE @ 0x629a0 / 0x629d0: the whole buffer
// from offset 0.
void* CAudioBuf::Lock()
{
	void* data;
	DWORD bytes;
	m_dsBuffer->Lock(0, m_bufferBytes, &data, &bytes, NULL, NULL, 0);
	return data;
}

void CAudioBuf::Unlock(void* pvBuffer)
{
	m_dsBuffer->Unlock(pvBuffer, m_bufferBytes, NULL, 0);
}

// Rewind and start, looping if asked. False with no buffer.
bool CAudioBuf::Play(bool bLoop)
{
	m_paused = false;
	m_loop = bLoop;
	if (m_dsBuffer == NULL)
		return false;

	m_dsBuffer->SetCurrentPosition(0);
	m_dsBuffer->Play(0, 0, LoopFlag(m_loop));
	return true;
}

// CAudioBuf::Stop
// 5960 @ 0x62a30 stops the buffer (and then waits for it to report
// stopped; Theseus doesn't wait).
void CAudioBuf::Stop()
{
	if (m_dsBuffer != NULL)
		m_dsBuffer->Stop();
}

// Theseus pauses by stopping in place and resumes with Play.
void CAudioBuf::Pause(bool bPause)
{
	if (m_dsBuffer == NULL)
		return;

	m_paused = bPause;
	if (bPause)
		m_dsBuffer->Stop();
	else
		m_dsBuffer->Play(0, 0, LoopFlag(m_loop));
}

bool CAudioBuf::IsPlaying()
{
	if (m_dsBuffer == NULL)
		return false;

	DWORD status;
	m_dsBuffer->GetStatus(&status);
	return (status & DSBSTATUS_PLAYING) != 0;
}

// Attenuation in dB to DirectSound's hundredths of a dB, negative.
void CAudioBuf::SetAttenuation(float nAttenuation)
{
	m_dsBuffer->SetVolume(-(int)(nAttenuation * 100.0f));
}

// 5960's clip remembers pan but never applies it.
void CAudioBuf::SetPan(float nPan)
{
}

// CAudioBuf::SetFrequency(hz)
// Reconstructed from 5960 retail XBE @ 0x62ad0:
//     SetFrequency(buffer, hz == 0 ? 0 : (int)hz);
// 0 is DSBFREQUENCY_ORIGINAL.
void CAudioBuf::SetFrequency(float nFrequency)
{
	m_dsBuffer->SetFrequency(nFrequency == 0.0f ? DSBFREQUENCY_ORIGINAL : (DWORD)nFrequency);
}

float CAudioBuf::GetPlaybackTime()
{
	if (m_dsBuffer == NULL)
		return 0.0f;

	DWORD cursor;
	m_dsBuffer->GetCurrentPosition(&cursor, NULL);
	return (float)cursor / m_bytesPerSecond;
}

float CAudioBuf::GetPlaybackLength()
{
	return (float)m_bufferBytes / m_bytesPerSecond;
}

void* CAudioBuf::GetSampleBuffer()
{
	return NULL;
}

DWORD CAudioBuf::GetSampleBufferSize()
{
	return 0;
}

// =========================================================================
// CAudioPump
// =========================================================================

// 5960's pump constructor is folded into its subclasses (0x63220,
// 0x63430): everything zero and stopped.
CAudioPump::CAudioPump()
{
	m_bufferBytes = 0;
	m_segments = 0;
	m_prebuffer = 0;
	m_state = PUMP_STOPPED;
	m_filled = 0;
	m_wraps = 0;
	m_lastCursor = 0;
	m_segmentFilled = NULL;
	m_segmentEvents = NULL;
	m_quit = NULL;
	m_lock = NULL;
	m_worker = NULL;
}

// CAudioPump::~CAudioPump
// Reconstructed from 5960 retail XBE @ 0x62b90: tell the worker to quit
// and wait for it, close the quit event and the lock, stop and release
// the buffer, close the segment events, free the arrays.
CAudioPump::~CAudioPump()
{
	if (m_quit != NULL)
		SetEvent(m_quit);
	if (m_worker != NULL)
	{
		WaitForSingleObject(m_worker, INFINITE);
		CloseHandle(m_worker);
	}
	if (m_quit != NULL)
		CloseHandle(m_quit);
	if (m_lock != NULL)
		CloseHandle(m_lock);

	if (m_dsBuffer != NULL)
	{
		m_dsBuffer->Stop();
		m_dsBuffer->Release();
		m_dsBuffer = NULL;
	}

	if (m_segmentEvents != NULL)
	{
		for (int i = 0; i < m_segments; i++)
		{
			if (m_segmentEvents[i] != NULL)
				CloseHandle(m_segmentEvents[i]);
		}
		delete [] m_segmentEvents;
	}
	delete [] m_segmentFilled;
}

DWORD CALLBACK CAudioPump::ThreadEntry(LPVOID pvContext)
{
	return ((CAudioPump*)pvContext)->ThreadProc();
}

// CAudioPump::ThreadProc
// Reconstructed from 5960 retail XBE @ 0x62c90. Waits on the quit event
// plus every segment event, then on quit or the lock. Quit stops playback
// and ends the thread. Holding the lock: nothing to do when stopped; while
// buffering the next segment in order is filled; once draining with no
// filled segments left it stops and runs OnAudioEnd; otherwise the first
// signaled segment is refilled. The lock is let go before waiting again.
// 5960 also clears a segment's filled mark and drops the count when it
// comes round; Theseus doesn't (see the doc).
DWORD CAudioPump::ThreadProc()
{
	HANDLE* waitList = new HANDLE [m_segments + 1];
	if (waitList == NULL)
		return (DWORD)-1;

	waitList[0] = m_quit;
	for (int i = 0; i < m_segments; i++)
		waitList[i + 1] = m_segmentEvents[i];

	const HANDLE quitOrLock[] = { m_quit, m_lock };
	bool locked = false;
	for (;;)
	{
		if (locked)
		{
			ReleaseMutex(m_lock);
			locked = false;
		}

		if (WaitForMultipleObjects(m_segments + 1, waitList, FALSE, INFINITE) == WAIT_OBJECT_0)
			break;
		if (WaitForMultipleObjects(2, quitOrLock, FALSE, INFINITE) == WAIT_OBJECT_0)
			break;
		locked = true;

		if (m_state == PUMP_STOPPED)
			continue;

		if (m_state == PUMP_BUFFERING)
		{
			FillBuffer(m_filled);
			continue;
		}

		if (m_state == PUMP_DRAINING && m_filled <= 0)
		{
			Stop();
			OnAudioEnd();
			continue;
		}

		int seg = 0;
		while (seg < m_segments && WaitForSingleObject(m_segmentEvents[seg], 0) != WAIT_OBJECT_0)
			seg++;
		if (seg < m_segments)
			FillBuffer(seg);
	}

	Stop();
	if (locked)
		ReleaseMutex(m_lock);
	delete [] waitList;
	return 0;
}

// CAudioPump::Initialize(stack, format, segmentBytes, segments, prebuffer)
// Reconstructed from 5960 retail XBE @ 0x62de0. Keeps the segment size and
// byte rate, makes one manual reset event per segment, an auto reset quit
// event, the lock and the worker thread, then a buffer of segments *
// segmentBytes with position notify, volume and frequency control. Each
// segment's event fires a block before its end. Theseus reports which step
// failed instead of a bool.
HRESULT CAudioPump::Initialize(DWORD dwStackSize, WAVEFORMATEX* pWaveFormat, int nBufferBytes, int nSegmentsPerBuffer, int nPrebufferSegments)
{
	m_bytesPerSecond = pWaveFormat->nAvgBytesPerSec;
	m_bufferBytes = nBufferBytes;
	m_segments = nSegmentsPerBuffer;
	m_prebuffer = nPrebufferSegments;

	m_segmentEvents = new HANDLE [m_segments];
	if (m_segmentEvents == NULL)
		return E_OUTOFMEMORY;
	for (int i = 0; i < m_segments; i++)
		m_segmentEvents[i] = NULL;

	m_quit = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (m_quit == INVALID_HANDLE_VALUE)
		return ERROR_INVALID_HANDLE;
	m_lock = CreateMutex(NULL, FALSE, NULL);
	if (m_lock == INVALID_HANDLE_VALUE)
		return ERROR_INVALID_HANDLE;
	for (int i = 0; i < m_segments; i++)
	{
		m_segmentEvents[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (m_segmentEvents[i] == INVALID_HANDLE_VALUE)
			return E_OUTOFMEMORY;
	}

	m_segmentFilled = new bool [m_segments];
	if (m_segmentFilled == NULL)
		return E_OUTOFMEMORY;

	DWORD id;
	m_worker = CreateThread(NULL, dwStackSize, ThreadEntry, this, 0, &id);
	if (m_worker == INVALID_HANDLE_VALUE)
		return ERROR_INVALID_HANDLE;

	DSoundManager* ds = DSoundManager::Instance();
	HRESULT hr = ds->DSoundCreateSoundBuffer(pWaveFormat, m_segments * m_bufferBytes,
		DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY, &m_dsBuffer);
	if (m_dsBuffer == NULL)
		return hr;

	DSBPOSITIONNOTIFY* marks = new DSBPOSITIONNOTIFY [m_segments];
	if (marks == NULL)
		return E_OUTOFMEMORY;
	for (int i = 0; i < m_segments; i++)
	{
		marks[i].dwOffset = (i + 1) * m_bufferBytes - pWaveFormat->nBlockAlign;
		marks[i].hEventNotify = m_segmentEvents[i];
	}
	hr = ds->DSoundSetSoundBufferNotify(m_dsBuffer, m_segments, marks);
	delete [] marks;
	return hr;
}

// Theseus's Play: under the lock, stop, then start buffering with every
// segment event set so the worker fills them all, fill the prebuffer
// segments here, and start the ring looping.
bool CAudioPump::Play(bool bLoop)
{
	m_loop = bLoop;
	if (m_dsBuffer == NULL)
		return false;

	WaitForSingleObject(m_lock, INFINITE);
	Stop();
	m_state = PUMP_BUFFERING;
	for (int i = 0; i < m_segments; i++)
	{
		SetEvent(m_segmentEvents[i]);
		m_segmentFilled[i] = false;
	}
	for (int i = 0; i < m_prebuffer; i++)
		FillBuffer(i);
	m_dsBuffer->Play(0, 0, DSBPLAY_LOOPING);
	ReleaseMutex(m_lock);
	return true;
}

// CAudioPump::Stop
// Reconstructed from 5960 retail XBE @ 0x62fe0. Under the lock: stop,
// rewind, let DirectSound catch up, clear every segment event, and reset
// the counters to stopped.
void CAudioPump::Stop()
{
	if (m_dsBuffer == NULL)
		return;

	WaitForSingleObject(m_lock, INFINITE);
	m_dsBuffer->Stop();
	m_dsBuffer->SetCurrentPosition(0);
	DirectSoundDoWork();
	for (int i = 0; i < m_segments; i++)
		ResetEvent(m_segmentEvents[i]);
	m_wraps = 0;
	m_filled = 0;
	m_state = PUMP_STOPPED;
	ReleaseMutex(m_lock);
}

// CAudioPump::Pause(pause)
// Reconstructed from 5960 retail XBE @ 0x63060: flag and buffer under the
// lock. 5960 calls IDirectSoundBuffer_Pause; Theseus stops and replays.
void CAudioPump::Pause(bool bPause)
{
	if (m_dsBuffer == NULL)
		return;

	WaitForSingleObject(m_lock, INFINITE);
	m_paused = bPause;
	if (bPause)
		m_dsBuffer->Stop();
	else
		m_dsBuffer->Play(0, 0, DSBPLAY_LOOPING);
	ReleaseMutex(m_lock);
}

bool CAudioPump::IsPlaying()
{
	return m_state != PUMP_STOPPED;
}

// CAudioPump::FillBuffer(segment)
// Reconstructed from 5960 retail XBE @ 0x630d0. Under the lock: clear the
// segment's event, lock its bytes and ask GetData for them (nothing once
// draining). A short read means the sound is ending: drain, and zero the
// rest. A segment that got data is marked filled and counted; when
// buffering has filled the whole ring the pump is running. True when
// there was data.
bool CAudioPump::FillBuffer(int nSegment)
{
	WaitForSingleObject(m_lock, INFINITE);
	ResetEvent(m_segmentEvents[nSegment]);

	void* data;
	DWORD bytes;
	m_dsBuffer->Lock(nSegment * m_bufferBytes, m_bufferBytes, &data, &bytes, NULL, NULL, 0);

	int got = (m_state == PUMP_DRAINING) ? 0 : GetData((BYTE*)data, m_bufferBytes);
	if (got < m_bufferBytes)
	{
		m_state = PUMP_DRAINING;
		int keep = got > 0 ? got : 0;
		ZeroMemory((BYTE*)data + keep, m_bufferBytes - keep);
	}

	m_dsBuffer->Unlock(data, bytes, NULL, 0);

	if (got > 0)
	{
		m_segmentFilled[nSegment] = true;
		m_filled++;
		if (m_state == PUMP_BUFFERING && m_filled >= m_segments)
			m_state = PUMP_RUNNING;
	}

	ReleaseMutex(m_lock);
	return got > 0;
}

// CAudioPump::GetPlaybackTime
// Reconstructed from 5960 retail XBE @ 0x631c0. The cursor going backwards
// means it went round the ring once more:
//     if (cursor < last) wraps++; last = cursor;
//     return (wraps * segments * segmentBytes + cursor) / bytesPerSecond;
float CAudioPump::GetPlaybackTime()
{
	if (m_dsBuffer == NULL)
		return 0.0f;

	DWORD cursor;
	m_dsBuffer->GetCurrentPosition(&cursor, NULL);
	if (cursor < m_lastCursor)
		m_wraps++;
	m_lastCursor = cursor;

	return (float)(m_wraps * (m_bufferBytes * m_segments) + cursor) / m_bytesPerSecond;
}

// A stream doesn't know its length; subclasses that do override this.
float CAudioPump::GetPlaybackLength()
{
	return 0.0f;
}

// =========================================================================
// CFilePump
// =========================================================================

// Reconstructed from 5960 retail XBE @ 0x63220: no file yet.
CFilePump::CFilePump()
{
	m_file = INVALID_HANDLE_VALUE;
	m_dataStart = 0;
	m_lastBlock = NULL;
	m_blockBytes = 0;
}

// Reconstructed from 5960 retail XBE @ 0x63290: close the file.
CFilePump::~CFilePump()
{
	if (m_file != INVALID_HANDLE_VALUE)
		CloseHandle(m_file);
}

// CFilePump::Initialize(file, dataBytes, format)
// Reconstructed from 5960 retail XBE @ 0x632b0. Segments are the most
// whole blocks that fit in 8 KB; the pump gets an 8 KB stack. The pump
// takes the open file, works out the length in seconds and remembers where
// the samples start.
HRESULT CFilePump::Initialize(HANDLE hFile, int nFileBytes, WAVEFORMATEX* pFormat)
{
	m_blockBytes = (0x2000 / pFormat->nBlockAlign) * pFormat->nBlockAlign;

	HRESULT hr = CAudioPump::Initialize(0x2000, pFormat, m_blockBytes);
	if (FAILED(hr))
		return hr;

	m_file = hFile;
	m_seconds = (float)nFileBytes / m_bytesPerSecond;
	m_dataStart = SetFilePointer(m_file, 0, NULL, FILE_CURRENT);
	return hr;
}

// Stop and wind the file back to the first sample.
void CFilePump::Stop()
{
	if (m_file == INVALID_HANDLE_VALUE)
		return;
	CAudioPump::Stop();
	SetFilePointer(m_file, m_dataStart, NULL, FILE_BEGIN);
}

float CFilePump::GetPlaybackLength()
{
	return m_seconds;
}

void* CFilePump::GetSampleBuffer()
{
	return m_lastBlock;
}

DWORD CFilePump::GetSampleBufferSize()
{
	return m_blockBytes;
}

// Reads the next block. When looping, a short read wraps to the first
// sample and tops the block up. -1 on a read error.
int CFilePump::GetData(BYTE* pbBuffer, int cbBuffer)
{
	DWORD got;
	if (!ReadFile(m_file, pbBuffer, cbBuffer, &got, NULL))
		return -1;

	if (m_loop && got != (DWORD)cbBuffer)
	{
		SetFilePointer(m_file, m_dataStart, NULL, FILE_BEGIN);
		DWORD more;
		if (!ReadFile(m_file, pbBuffer + got, cbBuffer - got, &more, NULL))
			return -1;
		got += more;
	}

	m_lastBlock = pbBuffer;
	return (int)got;
}

// =========================================================================
// CMP3Pump: Theseus's soundtrack decoder, not in 5960
// =========================================================================

extern void mp3dec_init_s(mp3dec_t* dec);
extern int mp3dec_decode_frame_s(mp3dec_t* dec, const BYTE* mp3,
    int mp3_bytes, short* pcm, mp3dec_frame_info_t* info);

CMP3Pump::CMP3Pump()
{
	m_fileData = NULL;
	m_fileSize = 0;
	m_fileOffset = 0;
	m_decoder = NULL;
	m_buffer = NULL;
	m_bufferSize = 0;
	m_playbackLength = 0.0f;
	m_channels = 0;
	m_sampleRate = 0;
}

CMP3Pump::~CMP3Pump()
{
	if (m_fileData) free(m_fileData);
	if (m_decoder) free((mp3dec_t*)m_decoder);
}

HRESULT CMP3Pump::Initialize(const char* szFilePath)
{
	// Load entire MP3 into memory (file I/O from a thread is simpler this way)
	HANDLE hFile = TheseusCreateFile(szFilePath, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return E_FAIL;

	m_fileSize = (int)GetFileSize(hFile, NULL);
	if (m_fileSize <= 0 || m_fileSize > 32 * 1024 * 1024)
	{
		CloseHandle(hFile);
		return E_FAIL;
	}

	m_fileData = (BYTE*)malloc(m_fileSize);
	if (!m_fileData) { CloseHandle(hFile); return E_OUTOFMEMORY; }

	DWORD bytesRead = 0;
	ReadFile(hFile, m_fileData, m_fileSize, &bytesRead, NULL);
	CloseHandle(hFile);

	if ((int)bytesRead != m_fileSize)
	{
		free(m_fileData); m_fileData = NULL;
		return E_FAIL;
	}

	// Allocate decoder state
	m_decoder = malloc(sizeof(mp3dec_t));
	if (!m_decoder) return E_OUTOFMEMORY;
	mp3dec_init_s((mp3dec_t*)m_decoder);

	// Probe first frame for format info
	mp3dec_frame_info_t info;
	short pcm[1152 * 2];
	int samples = mp3dec_decode_frame_s((mp3dec_t*)m_decoder, m_fileData, m_fileSize, pcm, &info);
	if (info.hz == 0 || info.channels == 0) return E_FAIL;

	m_sampleRate = info.hz;
	m_channels = info.channels;
	m_fileOffset = 0;

	// Estimate duration from bitrate
	if (info.bitrate_kbps > 0)
		m_playbackLength = (float)m_fileSize / (info.bitrate_kbps * 125);

	// Reset decoder for actual playback
	mp3dec_init_s((mp3dec_t*)m_decoder);

	// Set up PCM format for DirectSound
	WAVEFORMATEX wfx;
	ZeroMemory(&wfx, sizeof(wfx));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = (WORD)m_channels;
	wfx.nSamplesPerSec = m_sampleRate;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (WORD)(m_channels * 2);
	wfx.nAvgBytesPerSec = m_sampleRate * m_channels * 2;

	// Buffer size: ~0.5 second of PCM
	m_bufferSize = (wfx.nAvgBytesPerSec / 2);
	m_bufferSize = (m_bufferSize / wfx.nBlockAlign) * wfx.nBlockAlign;

	// 64KB stack: mp3dec_decode_frame allocates ~16KB of scratch on the
	// worker thread stack (mp3dec_scratch_t). 8KB overflows on first decode.
	HRESULT hr = CAudioPump::Initialize(65536, &wfx, m_bufferSize);
	if (FAILED(hr))
	{
		DbgPrint("[CMP3Pump] CAudioPump::Initialize failed\n");
		return hr;
	}

	DbgPrint("[CMP3Pump] Ready: %dHz %dch, %.1fs\n", m_sampleRate, m_channels, m_playbackLength);
	return S_OK;
}

void CMP3Pump::Stop()
{
	CAudioPump::Stop();
	m_fileOffset = 0;
	if (m_decoder) mp3dec_init_s((mp3dec_t*)m_decoder);
}

float CMP3Pump::GetPlaybackLength() { return m_playbackLength; }
void* CMP3Pump::GetSampleBuffer() { return m_buffer; }
DWORD CMP3Pump::GetSampleBufferSize() { return m_bufferSize; }

int CMP3Pump::GetData(BYTE* pbBuffer, int cbBuffer)
{
	int bytesWritten = 0;

	while (bytesWritten < cbBuffer && m_fileOffset < m_fileSize)
	{
		mp3dec_frame_info_t info;
		short pcm[1152 * 2];
		int samples = mp3dec_decode_frame_s((mp3dec_t*)m_decoder,
			m_fileData + m_fileOffset,
			m_fileSize - m_fileOffset,
			pcm, &info);

		if (info.frame_bytes == 0) break;
		m_fileOffset += info.frame_bytes;

		if (samples > 0)
		{
			int pcmBytes = samples * m_channels * 2;
			int space = cbBuffer - bytesWritten;
			int toCopy = (pcmBytes < space) ? pcmBytes : space;
			memcpy(pbBuffer + bytesWritten, pcm, toCopy);
			bytesWritten += toCopy;
		}
	}

	// Handle looping
	if (m_loop && bytesWritten < cbBuffer && m_fileOffset >= m_fileSize)
	{
		m_fileOffset = 0;
		if (m_decoder) mp3dec_init_s((mp3dec_t*)m_decoder);
	}

	m_buffer = pbBuffer;
	return bytesWritten;
}


// =========================================================================
// CAudioClip
// =========================================================================

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
END_NODE_PROPS()

#define _FND_CLASS CAudioClip
START_NODE_FUN(CAudioClip, CTimeDepNode)
	NODE_FUN_VV(Play)
	NODE_FUN_VV(Stop)
	NODE_FUN_VV(Pause)
	NODE_FUN_IV(getMinutes)
	NODE_FUN_IV(getSeconds)
	NODE_FUN_VV(PlayOrPause)
END_NODE_FUN()
#undef _FND_CLASS

// CAudioClip::CAudioClip
// Reconstructed from 5960 retail XBE @ 0x61890: invisible, full volume,
// centered, recorded rate, no fade, no url, stopped, and dirty so the
// first use loads the sound. The last applied volume starts at 1.
CAudioClip::CAudioClip()
{
	m_visible = false;
	m_url = NULL;
	m_capturedLoop = false;
	m_sendProgress = false;
	m_pause_on_moving = false;
	m_progress = 0.0f;
	m_removeVoice = false;
	m_transportMode = TRANSPORT_STOP;
	m_lastTransportMode = -1;
	m_dirty = true;
	m_unpauseNeeded = false;
	m_lastVolume = 1.0f;
	m_lastPan = 0.0f;
	m_lastFrequency = 0.0f;
	m_fade = 0.0f;
	m_volume = 1.0f;
	m_pan = 0.0f;
	m_frequency = 0.0f;
	m_sound = NULL;
}

// Reconstructed from 5960 retail XBE @ 0x61940: stop and drop the sound,
// free the url.
CAudioClip::~CAudioClip()
{
	Cleanup();
	delete [] m_url;
}

// CAudioClip::OnSetProperty
// Reconstructed from 5960 retail XBE @ 0x619f0. With a fade set, a new
// volume or pan isn't stored; a lerper glides the field there over fade
// seconds instead (replacing any running one on this clip). A new url
// marks the clip dirty, and isActive starts or stops it. Theseus also
// takes sendProgress here.
bool CAudioClip::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	int field = PTR2INT(pprd->pbOffset);

	if (field == offsetof(m_sendProgress))
		m_sendProgress = *(const bool*)pvValue;

	if ((field == offsetof(m_volume) || field == offsetof(m_pan)) && m_fade != 0.0f)
	{
		float* target = (field == offsetof(m_volume)) ? &m_volume : &m_pan;
		float value = *(const float*)pvValue;
		CLerper::RemoveObject(this);
		new CLerper(this, target, value, m_fade);
		return false;
	}

	if (field == offsetof(m_url))
	{
		m_dirty = true;
	}
	else if (field == offsetof(m_isActive))
	{
		m_isActive = *(const bool*)pvValue;
		OnIsActiveChanged();
	}
	return true;
}

// CAudioClip::OnIsActiveChanged
// Reconstructed from 5960 retail XBE @ 0x61ae0: load if dirty, then Play
// when active, Stop when not.
void CAudioClip::OnIsActiveChanged()
{
	if (m_dirty)
		Initialize();

	if (m_isActive)
		Play();
	else
		Stop();
}

// CAudioClip::Cleanup
// Reconstructed from 5960 retail XBE @ 0x61b40: Stop, delete the sound,
// and forget the volume, pan and rate last applied.
HRESULT CAudioClip::Cleanup()
{
	Stop();
	delete m_sound;
	m_sound = NULL;
	m_lastVolume = 1.0f;
	m_lastPan = 0.0f;
	m_lastFrequency = 0.0f;
	return S_OK;
}

// CAudioClip::Initialize
// Reconstructed from 5960 retail XBE @ 0x61ba0. Drops the old sound and
// clears dirty. An empty url plays nothing. Then by url: "cd:" is an audio
// CD track, "st:" a soundtrack song, and otherwise the extension picks the
// loader. 5960 plays CDs and WMA soundtracks and files; Theseus has no CD
// playback, streams soundtrack MP3s and loads only .wav files here.
HRESULT CAudioClip::Initialize()
{
	Cleanup();
	m_dirty = false;

	if (m_url == NULL || m_url[0] == 0)
		return S_FALSE;

	if (_tcsnicmp(m_url, _T("cd:"), 3) == 0)
	{
		DbgPrint("CD playback not supported\n");
		return S_FALSE;
	}

	if (_tcsnicmp(m_url, _T("st:"), 3) == 0)
	{
		OpenMP3Soundtrack();
		return S_OK;
	}

	char narrowUrl[256];
	WideCharToMultiByte(CP_ACP, 0, m_url, -1, narrowUrl, sizeof(narrowUrl), NULL, NULL);
	DbgPrint("[AudioClip] url = \"%s\"\n", narrowUrl);

	const TCHAR* ext = _tcsrchr(m_url, '.');
	if (ext != NULL && _tcsicmp(ext + 1, _T("wav")) == 0)
		OpenWaveFile();
	else
		DbgPrint("Invalid AudioClip url: %s\n", narrowUrl);

	return S_OK;
}

// The RIFF header up to the format chunk's size, and the head of every
// chunk after it.
struct RiffWaveHead
{
	char riff[4];
	DWORD fileBytes;
	char wave[4];
	char fmt[4];
	DWORD formatBytes;
};

struct RiffChunkHead
{
	DWORD id;
	DWORD bytes;
};

static bool TagIs(const char* tag, const char* want)
{
	return tag[0] == want[0] && tag[1] == want[1] && tag[2] == want[2] && tag[3] == want[3];
}

// CAudioClip::OpenWaveFile
// Reconstructed from 5960 retail XBE @ 0x61d20. Finds the file the url
// names and checks for "RIFF....WAVEfmt ". The format chunk is read into
// m_format (as much as fits; the rest is skipped), then chunks are skipped
// until "data". Up to 64 KB of samples load into a CAudioBuf; anything
// longer streams from the file through a CFilePump, which then owns it.
bool CAudioClip::OpenWaveFile()
{
	TCHAR path[MAX_PATH];
	FindFilePath(path, m_url);
	HANDLE file = TheseusCreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	DWORD got;
	RiffWaveHead head;
	if (!ReadFile(file, &head, sizeof (head), &got, NULL) || got != sizeof (head) ||
		!TagIs(head.riff, "RIFF") || !TagIs(head.wave, "WAVE") || !TagIs(head.fmt, "fmt "))
	{
		CloseHandle(file);
		return false;
	}

	ZeroMemory(&m_format, sizeof (m_format));
	DWORD formatBytes = head.formatBytes < sizeof (m_format) ? head.formatBytes : sizeof (m_format);
	if (!ReadFile(file, &m_format, formatBytes, &got, NULL) || got != formatBytes)
	{
		CloseHandle(file);
		return false;
	}
	if (head.formatBytes != formatBytes)
		SetFilePointer(file, head.formatBytes - formatBytes, 0, FILE_CURRENT);

	DWORD dataBytes = 0;
	RiffChunkHead chunk;
	while (ReadFile(file, &chunk, sizeof (chunk), &got, NULL))
	{
		if (chunk.id == 'atad')
		{
			dataBytes = chunk.bytes;
			break;
		}
		if (!SetFilePointer(file, chunk.bytes, 0, FILE_CURRENT))
			break;
	}
	if (dataBytes == 0)
	{
		CloseHandle(file);
		return false;
	}

	if (dataBytes > 65536)
	{
		CFilePump* pump = new CFilePump;
		m_sound = pump;
		if (FAILED(pump->Initialize(file, dataBytes, &m_format.wfx)))
		{
			CloseHandle(file);
			delete pump;
			m_sound = NULL;
			return false;
		}
		return true;
	}

	m_sound = new CAudioBuf;
	if (FAILED(m_sound->Initialize(&m_format.wfx, dataBytes)))
	{
		CloseHandle(file);
		delete m_sound;
		m_sound = NULL;
		return false;
	}

	void* samples = m_sound->Lock();
	bool bad = !ReadFile(file, samples, dataBytes, &got, NULL) || got != dataBytes;
	m_sound->Unlock(samples);
	CloseHandle(file);

	if (bad)
	{
		delete m_sound;
		m_sound = NULL;
		return false;
	}
	return true;
}

// Theseus: st:<id> is a song from music_collection.cpp's MP3 scan,
// decoded on the pump's thread so the song never sits in RAM as PCM.
extern const char* MusicCollection_FindSongByID(int songID);

bool CAudioClip::OpenMP3Soundtrack()
{
	int songID = _ttoi(m_url + 3);
	const char* songPath = MusicCollection_FindSongByID(songID);
	if (songPath == NULL)
	{
		DbgPrint("[AudioClip] st: song ID %d not found\n", songID);
		return false;
	}

	DbgPrint("[AudioClip] Loading MP3: %s\n", songPath);

	CMP3Pump* pump = new CMP3Pump;
	m_sound = pump;
	if (FAILED(pump->Initialize(songPath)))
	{
		DbgPrint("[AudioClip] CMP3Pump init failed for %s\n", songPath);
		delete pump;
		m_sound = NULL;
		return false;
	}
	return true;
}

// Fraction of the sound played, 0 when the length isn't known.
static float PlayedFraction(CAudioBuf* sound)
{
	float length = sound->GetPlaybackLength();
	return length != 0.0f ? sound->GetPlaybackTime() / length : 0.0f;
}

// CAudioClip::Advance(seconds)
// Reconstructed from 5960 retail XBE @ 0x62050. Loads if dirty. With
// pause_on_moving and a sound that isn't muted, a level change pauses it
// (and skips the rest of the frame) and the end of one resumes it. After
// the time-dependent base advance it publishes removeVoice. With no sound
// the clip is inactive and stopped. While active it applies volume
// changes, pausing at zero volume and resuming when it comes back, plus
// pan and rate; sends OnProgressChanged when sendProgress is on and the
// fraction moved; and tracks pause and play state, firing OnEndOfAudio
// when a playing sound stops. Any transport change fires
// OnTransportModeChanged. 5960 only ends a looping clip when it's also
// inactive; Theseus ends any that stops, and with sendProgress also one
// that has played past its length.
void CAudioClip::Advance(float nSeconds)
{
	if (m_dirty)
		Initialize();

	if (m_pause_on_moving && m_sound != NULL && m_volume != 0.0f)
	{
		if (g_bLevelSwitching)
		{
			if (!m_sound->IsPaused())
			{
				m_sound->Pause(true);
				m_unpauseNeeded = true;
			}
			return;
		}
		if (m_unpauseNeeded)
		{
			if (m_sound->IsPaused())
				m_sound->Pause(false);
			m_unpauseNeeded = false;
		}
	}

	CTimeDepNode::Advance(nSeconds);
	g_removeVoice = m_removeVoice;

	if (m_sound == NULL)
	{
		m_isActive = false;
		m_transportMode = TRANSPORT_STOP;
	}
	else if (m_isActive)
	{
		if (m_volume != m_lastVolume)
		{
			if (m_lastVolume == 0.0f && m_transportMode == TRANSPORT_PAUSE)
				Pause();
			m_lastVolume = m_volume;
			m_sound->SetAttenuation((1.0f - m_volume) * 100.0f);
			if (m_volume == 0.0f && m_transportMode == TRANSPORT_PLAY)
				Pause();
		}

		if (m_pan != m_lastPan)
		{
			m_lastPan = m_pan;
			m_sound->SetPan(m_pan);
		}

		if (m_frequency != m_lastFrequency)
		{
			m_lastFrequency = m_frequency;
			m_sound->SetFrequency(m_frequency);
		}

		if (m_sendProgress)
		{
			float fraction = PlayedFraction(m_sound);
			if (fraction != m_progress)
			{
				m_progress = fraction;
				CallFunction(this, _T("OnProgressChanged"));
			}
		}

		bool ended = false;
		if (m_sound->IsPaused())
		{
			m_transportMode = TRANSPORT_PAUSE;
		}
		else if (m_sound->IsPlaying())
		{
			m_transportMode = TRANSPORT_PLAY;
			if (m_sendProgress)
			{
				float length = m_sound->GetPlaybackLength();
				float played = length != 0.0f ? m_sound->GetPlaybackTime() : 0.0f;
				ended = played > length;
			}
		}
		else
		{
			ended = m_transportMode == TRANSPORT_PLAY;
		}

		if (ended)
		{
			m_transportMode = TRANSPORT_STOP;
			m_isActive = false;
			CallFunction(this, _T("OnEndOfAudio"));
		}
	}

	if (m_transportMode != m_lastTransportMode)
	{
		m_lastTransportMode = m_transportMode;
		CallFunction(this, _T("OnTransportModeChanged"));
	}
}

// CAudioClip::PlayOrPause   [script fn 'PlayOrPause', FND 0x00188720, sig 1]
// Reconstructed from 5960 retail XBE @ 0x62290:
//     if (transportMode is PLAY or PAUSE) Pause(); else Play();
void CAudioClip::PlayOrPause()
{
	if (m_transportMode == TRANSPORT_PLAY || m_transportMode == TRANSPORT_PAUSE)
		Pause();
	else
		Play();
}

// CAudioClip::Play   [script fn 'Play', FND 0x001886a8, sig 1]
// Reconstructed from 5960 retail XBE @ 0x622b0. Loads if dirty, applies
// volume, pan and rate, and starts the sound looping when the clip loops;
// if it started the clip is playing and active.
void CAudioClip::Play()
{
	if (m_dirty)
		Initialize();

	m_capturedLoop = m_loop;
	if (m_sound == NULL)
		return;

	m_sound->SetAttenuation((1.0f - m_volume) * 100.0f);
	m_sound->SetPan(m_pan);
	m_sound->SetFrequency(m_frequency);
	if (m_sound->Play(m_capturedLoop))
	{
		m_transportMode = TRANSPORT_PLAY;
		m_isActive = true;
	}
}

// CAudioClip::Stop   [script fn 'Stop', FND 0x001886c0, sig 1]
// Reconstructed from 5960 retail XBE @ 0x62350. Unless already stopped:
// stop and delete the sound, go inactive and mark dirty so the next play
// loads it fresh from the start.
void CAudioClip::Stop()
{
	if (m_transportMode == TRANSPORT_STOP)
		return;

	m_transportMode = TRANSPORT_STOP;
	if (m_sound != NULL)
		m_sound->Stop();
	delete m_sound;
	m_sound = NULL;
	m_isActive = false;
	m_dirty = true;
}

// CAudioClip::Pause   [script fn 'Pause', FND 0x001886d8, sig 1]
// Reconstructed from 5960 retail XBE @ 0x62390: loads if dirty, then
// toggles between paused and playing.
void CAudioClip::Pause()
{
	if (m_dirty)
		Initialize();

	bool pausing = m_transportMode != TRANSPORT_PAUSE;
	m_transportMode = pausing ? TRANSPORT_PAUSE : TRANSPORT_PLAY;
	if (m_sound != NULL)
		m_sound->Pause(pausing);
}

// CAudioClip::getMinutes / getSeconds   [script fns, FND 0x001886f0 / 0x00188708, sig 2]
// Reconstructed from 5960 retail XBE @ 0x623e0 / 0x62420: whole seconds
// played, / 60 or % 60; 0 without a sound.
int CAudioClip::getMinutes()
{
	if (m_dirty)
		Initialize();
	if (m_sound == NULL)
		return 0;
	return (int)m_sound->GetPlaybackTime() / 60;
}

int CAudioClip::getSeconds()
{
	if (m_dirty)
		Initialize();
	if (m_sound == NULL)
		return 0;
	return (int)m_sound->GetPlaybackTime() % 60;
}

// Theseus: the visualizer reads the block the sound played last.
void* CAudioClip::GetSampleBuffer()
{
	return m_sound != NULL ? m_sound->GetSampleBuffer() : NULL;
}

int CAudioClip::GetSampleBufferSize()
{
	return m_sound != NULL ? m_sound->GetSampleBufferSize() : 0;
}

// Theseus: lets native code point a clip at a file.
void CAudioClip::SetUrl(const TCHAR* pszAudioFile)
{
	delete [] m_url;
	m_dirty = true;
	if (pszAudioFile != NULL)
	{
		m_url = new TCHAR [_tcslen(pszAudioFile) + 1];
		_tcscpy(m_url, pszAudioFile);
	}
}

// =========================================================================
// CPeriodicAudioGroup
// =========================================================================

// Plays its AudioClip children one after another, every period seconds
// plus up to periodNoise more.
class CPeriodicAudioGroup : public CGroup
{
	DECLARE_NODE(CPeriodicAudioGroup, CGroup)
public:
	CPeriodicAudioGroup();

	void Advance(float nSeconds);

	bool m_isActive;
	bool m_pause_on_moving;
	float m_period;
	float m_periodNoise;

	XTIME m_nextPlay;         // 0 until the next one is scheduled
	int m_nextChild;          // -1 until a random first pick
	CAudioClip* m_playing;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("PeriodicAudioGroup", CPeriodicAudioGroup, CGroup)

START_NODE_PROPS(CPeriodicAudioGroup, CGroup)
	NODE_PROP(pt_boolean, CPeriodicAudioGroup, isActive)
	NODE_PROP(pt_number, CPeriodicAudioGroup, period)
	NODE_PROP(pt_number, CPeriodicAudioGroup, periodNoise)
END_NODE_PROPS()

// CPeriodicAudioGroup::CPeriodicAudioGroup
// Reconstructed from 5960 retail XBE @ 0x624a0: invisible, inactive, holds
// still through level changes, a 10 second period with no noise, nothing
// scheduled or picked.
CPeriodicAudioGroup::CPeriodicAudioGroup()
{
	m_visible = false;
	m_isActive = false;
	m_pause_on_moving = true;
	m_period = 10.0f;
	m_periodNoise = 0.0f;
	m_nextPlay = 0.0f;
	m_nextChild = -1;
	m_playing = NULL;
}

// CPeriodicAudioGroup::Advance(seconds)
// Reconstructed from 5960 retail XBE @ 0x62540. During a level change the
// clip it started is stopped. Inactive, the schedule is cleared. Otherwise,
// with children: the first time it picks a random one to start at; with
// nothing scheduled it schedules now + period + noise; once that time
// passes it plays the next child (wrapping to the first), if that child is
// an AudioClip.
void CPeriodicAudioGroup::Advance(float nSeconds)
{
	CGroup::Advance(nSeconds);

	if (m_pause_on_moving && g_bLevelSwitching)
	{
		if (m_playing != NULL && m_playing->IsKindOf(NODE_CLASS(CAudioClip)))
			m_playing->Stop();
		return;
	}

	if (!m_isActive)
	{
		m_nextPlay = 0.0f;
		return;
	}

	int count = m_children.GetLength();
	if (count == 0)
		return;

	if (m_nextChild < 0)
		m_nextChild = rand() % count;

	if (m_nextPlay == 0.0f)
	{
		m_nextPlay = TheseusGetNow() + m_period + rnd(m_periodNoise);
		return;
	}

	if (TheseusGetNow() <= m_nextPlay)
		return;

	m_nextPlay = 0.0f;
	if (m_nextChild >= count)
		m_nextChild = 0;
	m_playing = (CAudioClip*)m_children.GetNode(m_nextChild++);
	if (m_playing->IsKindOf(NODE_CLASS(CAudioClip)))
		m_playing->Play();
}
