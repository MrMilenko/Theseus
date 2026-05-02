// audio_system.cpp: DirectSound-based audio pipeline. Three layers:
// CAudioBuf (static buffer), CAudioPump (streaming base with worker
// thread), and the script-callable CAudioClip / CMusicCollection /
// CFilePump nodes. Decompiled from the 5960 retail XBE; see
// docs/decomp/AudioSystem.md.

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

// Latches the most recently active CAudioClip's removeVoice setting so the
// audio mixer can decide whether to keep the voice channel allocated when
// the clip stops. Set every Advance pass; read by the dsound pump on the
// next buffer notify.
bool g_removeVoice = false;
extern bool g_bLevelTransition;

// =========================================================================
// CAudioBuf: single-shot sound buffer
// =========================================================================

CAudioBuf::CAudioBuf()
{
	m_dsBuffer = NULL;
	m_paused = false;
	m_loop = false;
	m_bufferBytes = 0;
}

CAudioBuf::~CAudioBuf()
{
	if (m_dsBuffer != NULL)
	{
		m_dsBuffer->Stop();
		m_dsBuffer->Release();
	}
}

HRESULT CAudioBuf::Initialize(WAVEFORMATEX* pWaveFormat, int nBufferBytes, const void* pvSamples/*=NULL*/)
{
	m_bufferBytes = nBufferBytes;
	m_bytesPerSecond = pWaveFormat->nAvgBytesPerSec;

	HRESULT hr = DSoundManager::Instance()->DSoundCreateSoundBuffer(
		pWaveFormat, nBufferBytes,
		DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY,
		&m_dsBuffer);
	if (FAILED(hr) || m_dsBuffer == NULL)
	{
		DbgPrint("CAudioBuf::Initialize - fail to create sound buffer");
		return FAILED(hr) ? hr : E_INVALIDARG;
	}

	if (pvSamples != NULL)
	{
		hr = DSoundManager::Instance()->DSoundSetSoundBufferData(
			m_dsBuffer, 0, nBufferBytes, pvSamples);
		if (FAILED(hr))
		{
			DbgPrint("CAudioBuf::Initialize - fail to set sound buffer");
			return hr;
		}
	}

	return S_OK;
}

void* CAudioBuf::Lock()
{
	LPVOID pvBuffer;
	DWORD dwBufferLength;

	ASSERT(m_dsBuffer != NULL);

	VERIFYHR(m_dsBuffer->Lock(0, m_bufferBytes, &pvBuffer, &dwBufferLength, NULL, NULL, 0L));
	ASSERT(dwBufferLength == (DWORD)m_bufferBytes);
	return pvBuffer;
}

void CAudioBuf::Unlock(void* pvBuffer)
{
	VERIFYHR(m_dsBuffer->Unlock(pvBuffer, m_bufferBytes, NULL, 0));
}

bool CAudioBuf::Play(bool bLoop/*=false*/)
{
	m_paused = false;
	m_loop = bLoop;

	if (m_dsBuffer == NULL)
		return false;

	VERIFYHR(m_dsBuffer->SetCurrentPosition(0));
	VERIFYHR(m_dsBuffer->Play(0, 0, m_loop ? DSBPLAY_LOOPING : 0));

	return true;
}

void CAudioBuf::Stop()
{
	if (m_dsBuffer != NULL)
    {
		VERIFYHR(m_dsBuffer->Stop());
    }
}

void CAudioBuf::Pause(bool bPause)
{
	if (m_dsBuffer == NULL)
		return;

	if (bPause)
	{
		m_paused = true;
		VERIFYHR(m_dsBuffer->Stop());
	}
	else
	{
		m_paused = false;
		VERIFYHR(m_dsBuffer->Play(0, 0, m_loop ? DSBPLAY_LOOPING : 0));
	}
}

bool CAudioBuf::IsPlaying()
{
	if (m_dsBuffer == NULL)
		return false;

	DWORD dwStatus;
	VERIFYHR(m_dsBuffer->GetStatus(&dwStatus));
	return (dwStatus & DSBSTATUS_PLAYING) != 0;
}

void CAudioBuf::SetAttenuation(float nAttenuation)
{
	ASSERT(m_dsBuffer != NULL);
	VERIFYHR(m_dsBuffer->SetVolume(-(int)(nAttenuation * 100.0f)));
}

void CAudioBuf::SetPan(float nPan)
{
	ASSERT(m_dsBuffer != NULL);
}

void CAudioBuf::SetFrequency(float nFrequency)
{
	ASSERT(m_dsBuffer != NULL);
	VERIFYHR(m_dsBuffer->SetFrequency(nFrequency == 0.0f ? DSBFREQUENCY_ORIGINAL : (DWORD)nFrequency));
}

float CAudioBuf::GetPlaybackTime()
{
	if (m_dsBuffer == NULL)
		return 0.0f;

	DWORD dwPlayCursor;
	VERIFYHR(m_dsBuffer->GetCurrentPosition(&dwPlayCursor, NULL));

	return (float)dwPlayCursor / m_bytesPerSecond;
}

float CAudioBuf::GetPlaybackLength()
{
	return (float)m_bufferBytes / m_bytesPerSecond;
}

void* CAudioBuf::GetSampleBuffer() { return NULL; }
DWORD CAudioBuf::GetSampleBufferSize() { return 0; }

// =========================================================================
// CAudioPump: streaming audio with threaded buffer fill
// =========================================================================

CAudioPump::CAudioPump()
{
	m_playThread = NULL;
	m_terminateEvent = NULL;
    m_mutex = NULL;
	m_bufferBytes = 0;
	m_completedBuffers = 0;
    m_prevCursor = 0;
    m_filledBuffers = 0;
    m_pumpState = PUMPSTATE_STOPPED;

    m_notifyEvents = NULL;
    m_segmentsPerBuffer = 0;
    m_bufferFilled = 0;
}

CAudioPump::~CAudioPump()
{
	if (m_terminateEvent != NULL)
		SetEvent(m_terminateEvent);

	if (m_playThread != NULL)
	{
		WaitForSingleObject(m_playThread, INFINITE);
		CloseHandle(m_playThread);
	}

	if (m_terminateEvent != NULL)
		CloseHandle(m_terminateEvent);

	if (m_mutex != NULL)
		CloseHandle(m_mutex);

	if (m_dsBuffer != NULL)
	{
		m_dsBuffer->Stop();
		m_dsBuffer->Release();
		m_dsBuffer = NULL;
	}

    if(m_notifyEvents){
	    int i;
	    for (i = 0; i < m_segmentsPerBuffer; i++)
        {
            if (m_notifyEvents[i])
                CloseHandle(m_notifyEvents[i]);
        }
        delete [] m_notifyEvents;
    }
    if(m_bufferFilled){
        delete [] m_bufferFilled;
    }
}

DWORD CALLBACK CAudioPump::StartThread(LPVOID pvContext)
{
	CAudioPump *pThis = (CAudioPump*)pvContext;
	return pThis->ThreadProc();
}

DWORD CAudioPump::ThreadProc()
{
    const HANDLE ahMutex[] = { m_terminateEvent, m_mutex };
    HANDLE* ahNotify = new HANDLE[1 + m_segmentsPerBuffer];
    if(!ahNotify){
        return -1;
    }
    int nBuffer;
    bool fMutex = false;
    DWORD dwWaitObj;

    ahNotify[0] = m_terminateEvent;

    for (int i = 0; i < m_segmentsPerBuffer; i++)
    {
        ahNotify[i + 1] = m_notifyEvents[i];
    }

	for (;;)
	{
		if (fMutex)
        {
            ReleaseMutex(m_mutex);
            fMutex = false;
        }

        dwWaitObj = WaitForMultipleObjects(1 + m_segmentsPerBuffer, ahNotify, FALSE, INFINITE);
		if (dwWaitObj == WAIT_OBJECT_0)
        {
            break;
        }

        dwWaitObj = WaitForMultipleObjects(2, ahMutex, FALSE, INFINITE);
		if (dwWaitObj == WAIT_OBJECT_0)
        {
			break;
        }

        fMutex = true;

        if (m_pumpState == PUMPSTATE_STOPPED)
        {
            continue;
        }

        if (m_pumpState == PUMPSTATE_BUFFERING)
        {
            FillBuffer(m_filledBuffers);
            continue;
        }

        if ((m_pumpState == PUMPSTATE_STOPPING) && (m_filledBuffers <= 0))
        {
            Stop();
			OnAudioEnd();  // check if smthng to be done at the end of the play,
						   // eg. change track to the next one for the CD player
            continue;
        }

        int i;
        for (i = 0; i < m_segmentsPerBuffer; i++)
        {
            if (WAIT_OBJECT_0 == WaitForSingleObject(m_notifyEvents[i], 0))
            {
                nBuffer = i;

                break;
            }
        }

        if (i >= m_segmentsPerBuffer)
        {
            continue;
        }

        // We start the ball rolling by signaled all events. This
        // wakes us enough times that we can fill up all our buffers.
        // But, a side effect is that we get a bunch of false
        // signals. When we are playing very short audio clips, there may
        // actually be more false signals than there is data to play.
        // To keep this case from confusing us, we need to keep track
        // of which buffers actually have data in them.
        //
        // The m_bufferFilled array keeps track of which buffers are
        // actually  filled. This allows us to distinguish the initial
        // false signals from the normal signals.

		FillBuffer(nBuffer);
	}

    Stop();

    if (fMutex)
    {
        ReleaseMutex(m_mutex);
    }

    if(ahNotify){
        delete [] ahNotify;
    }

	return 0;
}

HRESULT CAudioPump::Initialize(DWORD dwStackSize, WAVEFORMATEX* pWaveFormat, int nBufferBytes, int nSegmentsPerBuffer, int nPrebufferSegments)
{

	m_bytesPerSecond = pWaveFormat->nAvgBytesPerSec;
	m_bufferBytes = nBufferBytes;

    m_segmentsPerBuffer = nSegmentsPerBuffer;
    m_prebufferSegments = nPrebufferSegments;

    ASSERT((m_prebufferSegments <= m_segmentsPerBuffer));

    m_notifyEvents = new HANDLE[m_segmentsPerBuffer];
    if(m_notifyEvents == NULL)
	{
        return E_OUTOFMEMORY;
    }

    int i;
    for (i = 0; i < m_segmentsPerBuffer; i++)
    {
        m_notifyEvents[i] = NULL;
    }

	m_terminateEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	ASSERT(	m_terminateEvent != INVALID_HANDLE_VALUE);
	if(m_terminateEvent == INVALID_HANDLE_VALUE)
	{
		return ERROR_INVALID_HANDLE;
	}

    m_mutex = CreateMutex(NULL, FALSE, NULL);
	ASSERT(m_mutex != INVALID_HANDLE_VALUE );
	if(m_mutex == INVALID_HANDLE_VALUE)
	{
		return ERROR_INVALID_HANDLE;
	}

    for (i = 0; i < m_segmentsPerBuffer; i++)
    {
	    m_notifyEvents[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if(m_notifyEvents[i] == INVALID_HANDLE_VALUE)
		{
			return E_OUTOFMEMORY;
		}
    }

    m_bufferFilled = new bool[m_segmentsPerBuffer];
    if(!m_bufferFilled){
        return E_OUTOFMEMORY;
    }

	DWORD dwThreadId;
	m_playThread = CreateThread(NULL, dwStackSize, StartThread, this, 0, &dwThreadId);

	ASSERT(m_playThread!= INVALID_HANDLE_VALUE);
	if(m_playThread == INVALID_HANDLE_VALUE)
	{
		return ERROR_INVALID_HANDLE;
	}

	HRESULT hr = DSoundManager::Instance()->DSoundCreateSoundBuffer(pWaveFormat, m_segmentsPerBuffer * m_bufferBytes, DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY, &m_dsBuffer);

    if (m_dsBuffer == NULL)
	{
		DbgPrint("CAudioPump::Initialize - fail to create sound buffer");
		return hr;
	}

	// Wire each segment boundary to its corresponding notify event so the
	// pump thread wakes up exactly when a buffer slot is ready to be refilled.
	DSBPOSITIONNOTIFY* dsbpn = new DSBPOSITIONNOTIFY[m_segmentsPerBuffer];
	if (dsbpn == NULL)
		return E_OUTOFMEMORY;

	for (i = 0; i < m_segmentsPerBuffer; i++)
	{
		dsbpn[i].dwOffset      = m_bufferBytes * (i + 1) - pWaveFormat->nBlockAlign;
		dsbpn[i].hEventNotify  = m_notifyEvents[i];
	}

	hr = DSoundManager::Instance()->DSoundSetSoundBufferNotify(m_dsBuffer, m_segmentsPerBuffer, dsbpn);
	if (FAILED(hr))
		DbgPrint("CAudioPump::Initialize - fail to set sound buffer notify");

	delete [] dsbpn;
	return hr;
}

bool CAudioPump::Play(bool bLoop/*=false*/)
{
    m_loop = bLoop;

	if (m_dsBuffer == NULL)
		return false;

    WaitForSingleObject(m_mutex, INFINITE);

    Stop();

    m_pumpState = PUMPSTATE_BUFFERING;

    int i;
    for (i = 0; i < m_segmentsPerBuffer; i++)
    {
        SetEvent(m_notifyEvents[i]);
        m_bufferFilled[i] = false;
    }

	for (i = 0; i < m_prebufferSegments; i++)
    {
        FillBuffer(i);
    }

	VERIFYHR(m_dsBuffer->Play(0, 0, DSBPLAY_LOOPING));

    ReleaseMutex(m_mutex);

	return true;
}

void CAudioPump::Stop()
{
	if (m_dsBuffer == NULL)
		return;

    WaitForSingleObject(m_mutex, INFINITE);

	VERIFYHR(m_dsBuffer->Stop());
	VERIFYHR(m_dsBuffer->SetCurrentPosition(0));

    DirectSoundDoWork();

    int i;
    for (i = 0; i < m_segmentsPerBuffer; i++)
    {
        ResetEvent(m_notifyEvents[i]);
    }

	m_completedBuffers = 0;
    m_filledBuffers = 0;
    m_pumpState = PUMPSTATE_STOPPED;

    ReleaseMutex(m_mutex);
}

void CAudioPump::Pause(bool bPause)
{
	if (m_dsBuffer == NULL)
		return;

    WaitForSingleObject(m_mutex, INFINITE);

	if (bPause)
	{
		m_paused = true;
		VERIFYHR(m_dsBuffer->Stop());
	}
	else
	{
		m_paused = false;
		VERIFYHR(m_dsBuffer->Play(0, 0, DSBPLAY_LOOPING));
	}

    ReleaseMutex(m_mutex);
}

bool CAudioPump::IsPlaying()
{
	return (PUMPSTATE_STOPPED != m_pumpState);
}

bool CAudioPump::FillBuffer(int nBuffer)
{
	int nBytes;

    ASSERT(m_dsBuffer != NULL);

	LPVOID pvBuffer;
	DWORD dwBufferLength;

    WaitForSingleObject(m_mutex, INFINITE);

    ResetEvent(m_notifyEvents[nBuffer]);

	VERIFYHR(m_dsBuffer->Lock(nBuffer * m_bufferBytes, m_bufferBytes, &pvBuffer, &dwBufferLength, NULL, NULL, 0L));
	ASSERT(dwBufferLength == (DWORD)m_bufferBytes);

	if (PUMPSTATE_STOPPING == m_pumpState)
    {
        nBytes = 0;
    }
    else
    {
        nBytes = GetData((BYTE*)pvBuffer, m_bufferBytes);
    }

	if (nBytes < m_bufferBytes)
    {
        m_pumpState = PUMPSTATE_STOPPING;

        if (nBytes > 0)
        {
		    ZeroMemory(((BYTE*)pvBuffer) + nBytes, m_bufferBytes - nBytes);
        }
        else
        {
		    ZeroMemory(pvBuffer, m_bufferBytes);
        }
    }

	VERIFYHR(m_dsBuffer->Unlock(pvBuffer, dwBufferLength, NULL, 0));

    if (nBytes > 0)
    {
        m_bufferFilled[nBuffer] = true;
        m_filledBuffers++;

        if (PUMPSTATE_BUFFERING == m_pumpState)
        {
            if(m_filledBuffers >= m_segmentsPerBuffer)
            {
                m_pumpState = PUMPSTATE_RUNNING;
            }
        }
    }

    ReleaseMutex(m_mutex);

	return nBytes > 0;
}

float CAudioPump::GetPlaybackTime()
{
	if (m_dsBuffer == NULL)
		return 0.0f;

	DWORD dwPlayCursor;
	VERIFYHR(m_dsBuffer->GetCurrentPosition(&dwPlayCursor, NULL));

    if( dwPlayCursor < m_prevCursor )
    {
        m_completedBuffers++;
    }

    m_prevCursor = dwPlayCursor;

    return (float)(m_completedBuffers * ( m_bufferBytes * m_segmentsPerBuffer ) + dwPlayCursor) / m_bytesPerSecond;
}

float CAudioPump::GetPlaybackLength()
{
	return 0.0f;
}

// =========================================================================
// CFilePump: WAV file streaming via CAudioPump
// =========================================================================

CFilePump::CFilePump()
{
	m_buffer = NULL;
	m_file = INVALID_HANDLE_VALUE;
	m_startPos = 0;
    m_bufferSize = 0;
}

CFilePump::~CFilePump()
{
	if (m_file != INVALID_HANDLE_VALUE)
		CloseHandle(m_file);
}

HRESULT CFilePump::Initialize(HANDLE hFile, int nFileBytes, WAVEFORMATEX* pFormat)
{
	HRESULT hr = S_OK;
    m_bufferSize = (0x2000/pFormat->nBlockAlign) * pFormat->nBlockAlign;

	hr = CAudioPump::Initialize(8192, pFormat, m_bufferSize);
	if (FAILED(hr))
	{
		DbgPrint("CFilePump::Initialize - fail to init CAudioPump");
		return hr;
	}

	m_file = hFile;
	m_playbackLength = (float)nFileBytes / m_bytesPerSecond;
	m_startPos = SetFilePointer(m_file, 0, NULL, FILE_CURRENT);

	return hr;
}

void CFilePump::Stop()
{
	if (m_file == INVALID_HANDLE_VALUE)
		return;

	CAudioPump::Stop();

	SetFilePointer(m_file, m_startPos, NULL, FILE_BEGIN);
}

float CFilePump::GetPlaybackLength() { return m_playbackLength; }
void* CFilePump::GetSampleBuffer() { return m_buffer; }
DWORD CFilePump::GetSampleBufferSize() { return m_bufferSize; }

int CFilePump::GetData(BYTE* pbBuffer, int cbBuffer)
{
	DWORD dwRead;

	if (!ReadFile(m_file, pbBuffer, cbBuffer, &dwRead, NULL))
	{
		DbgPrint("CFilePump::GetData ReadFile failed (%d)\n", GetLastError());
		return -1;
	}

	if (m_loop && dwRead != (DWORD)cbBuffer)
	{
		SetFilePointer(m_file, m_startPos, NULL, FILE_BEGIN);

		DWORD dwRead2;
		if (!ReadFile(m_file, pbBuffer + dwRead, cbBuffer - dwRead, &dwRead2, NULL))
		{
			DbgPrint("CFilePump::GetData ReadFile failed (%d)\n", GetLastError());
			return -1;
		}

		dwRead += dwRead2;
	}

	m_buffer = pbBuffer;

	return (int)dwRead;
}

// =========================================================================
// CMP3Pump: streaming MP3 decode via minimp3
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
	HANDLE hFile = CreateFileA(szFilePath, GENERIC_READ, FILE_SHARE_READ,
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
// CAudioClip: XAP AudioClip node
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

CAudioClip::CAudioClip() :
	m_fade(0.0f),
	m_volume(1.0f),
	m_pan(0.0f),
	m_frequency(0.0f),
	m_url(NULL),
	m_transportMode(TRANSPORT_STOP)
{
	m_removeVoice = false;
    m_sendProgress = false;
    m_pause_on_moving = false;
    m_unpauseNeeded = false;
	m_progress = 0;

	m_dirty = true;
	m_lastTransportMode = -1;
	m_lastVolume = 1.0f;
	m_lastPan = 0.0f;
	m_lastFrequency = 0.0f;

	m_sound = NULL;
	m_visible = false;
	m_capturedLoop = false;
}

CAudioClip::~CAudioClip()
{
	Cleanup();
	delete [] m_url;
}

bool CAudioClip::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_sendProgress))
	{
		m_sendProgress = *(bool*)pvValue;
	}

	if (PTR2INT(pprd->pbOffset) == offsetof(m_volume))
	{
		if (m_fade != 0.0f)
		{
			float volume = *(float*)pvValue;
			CLerper::RemoveObject(this);
			new CLerper(this, &m_volume, volume, m_fade);
			return false;
		}
	}
	if (PTR2INT(pprd->pbOffset) == offsetof(m_pan))
	{
		if (m_fade != 0.0f)
		{
			float pan = *(float*)pvValue;
			CLerper::RemoveObject(this);
			new CLerper(this, &m_pan, pan, m_fade);
			return false;
		}
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_url))
	{
		m_dirty = true;
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_isActive))
	{
		m_isActive = *(bool*)pvValue;
		OnIsActiveChanged();
	}

	return true;
}

void CAudioClip::OnIsActiveChanged()
{
	if (m_dirty)
		Initialize();

	if (m_isActive)
		Play();
	else
		Stop();
}


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



HRESULT CAudioClip::Initialize()
{
	ASSERT(m_dirty);

	// Drop any prior sound buffer before we wire a new one in.
	Cleanup();
	m_dirty = false;

	if (m_url == NULL || m_url[0] == 0)
		return S_FALSE;

	// Dispatch on URL scheme: cd:* (legacy CD playback, removed),
	// st:<id> (MP3 soundtrack streamed from disk), or any other path
	// is treated as a sample-based wave file.
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

	{
		char dbgUrl[256];
		WideCharToMultiByte(CP_ACP, 0, m_url, -1, dbgUrl, sizeof(dbgUrl), NULL, NULL);
		DbgPrint("[AudioClip] url = \"%s\"\n", dbgUrl);
	}

	const TCHAR* ext = _tcsrchr(m_url, '.');
	if (ext != NULL && _tcsicmp(ext + 1, _T("wav")) == 0)
	{
		OpenWaveFile();
	}
	else
	{
		char dbgUrl[256];
		WideCharToMultiByte(CP_ACP, 0, m_url, -1, dbgUrl, sizeof(dbgUrl), NULL, NULL);
		DbgPrint("Invalid AudioClip url: %s\n", dbgUrl);
	}

	return S_OK;
}

struct WAVFILE1
{
	BYTE riff [4];
	DWORD dwSize;
	BYTE wave [4];
	BYTE fmt [4];
	DWORD dwFormatSize;
};

struct WAVFILE2
{
	BYTE data [4];
	DWORD dwDataSize;
};

typedef struct
{
    FOURCC  fccChunkId;
    DWORD   dwDataSize;
} RIFFHEADER, *LPRIFFHEADER;

#ifndef FOURCC_RIFF
#define FOURCC_RIFF 'FFIR'
#endif // FOURCC_RIFF

#ifndef FOURCC_WAVE
#define FOURCC_WAVE 'EVAW'
#endif // FOURCC_WAVE

#ifndef FOURCC_FORMAT
#define FOURCC_FORMAT ' tmf'
#endif // FOURCC_FORMAT

#ifndef FOURCC_DATA
#define FOURCC_DATA 'atad'
#endif // FOURCC_DATA

bool CAudioClip::OpenWaveFile()
{
	TCHAR szBuf [MAX_PATH];
	FindFilePath(szBuf, m_url);
	HANDLE hFile = TheseusCreateFile(szBuf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
 	    DbgPrint("\001Cannot open wave file: %s\n", m_url);
		return false;
	}

	DWORD dwRead;
	WAVFILE1 header;
    RIFFHEADER RiffHeader;

	if (!ReadFile(hFile, &header, sizeof (header), &dwRead, NULL) || dwRead != sizeof (header))
	{
		DbgPrint("\001%s is not a valid wave file...\n", m_url);
		CloseHandle(hFile);
		return false;
	}

	if (header.riff[0] != 'R' || header.riff[1] != 'I' || header.riff[2] != 'F' || header.riff[3] != 'F' ||
		header.wave[0] != 'W' || header.wave[1] != 'A' || header.wave[2] != 'V' || header.wave[3] != 'E' ||
		header.fmt[0] != 'f' || header.fmt[1] != 'm' || header.fmt[2] != 't' || header.fmt[3] != ' ')
	{
		DbgPrint("\001%s is not a valid wave file (bad header chunk)\n", m_url);
		CloseHandle(hFile);
		return false;
	}

	DWORD dwFormatSize = header.dwFormatSize;
	ZeroMemory(&m_format, sizeof(m_format));

	if (dwFormatSize > sizeof(m_format))
    {
		dwFormatSize = sizeof(m_format);
    }

	if (!ReadFile(hFile, &m_format, dwFormatSize, &dwRead, NULL) || dwRead != dwFormatSize)
	{
		DbgPrint("\001%s is not a valid wave file (bad format chunk)\n", m_url);
		CloseHandle(hFile);
		return false;
	}

    if (header.dwFormatSize != dwFormatSize)
    {
        SetFilePointer(hFile, header.dwFormatSize - dwFormatSize, 0, FILE_CURRENT);
    }

    DWORD dwDataSize = 0;

    ASSERT(m_format.wfx.wFormatTag == WAVE_FORMAT_PCM || m_format.wfx.wFormatTag == WAVE_FORMAT_XBOX_ADPCM);

	// Walk RIFF chunks until we hit FOURCC_DATA, skipping anything else.
	for (;;)
	{
		if (!ReadFile(hFile, &RiffHeader, sizeof(RiffHeader), &dwRead, NULL))
			break;

		if (RiffHeader.fccChunkId == FOURCC_DATA)
		{
			dwDataSize = RiffHeader.dwDataSize;
			break;
		}

		if (!SetFilePointer(hFile, RiffHeader.dwDataSize, 0, FILE_CURRENT))
			break;
	}

    if (dwDataSize == 0)
    {
        DbgPrint("\001%s is not a valid wave file (bad data chunk)\n", m_url);
        CloseHandle(hFile);
        return false;
    }

	HRESULT hr = S_OK;
	ASSERT(m_sound == NULL);
	if (dwDataSize > 65536)
	{
		CFilePump* pSound = new CFilePump;
		m_sound = pSound;
		hr = pSound->Initialize(hFile, dwDataSize, &m_format.wfx);
		if (FAILED(hr))
		{
			DbgPrint("Could not initialize CFilePump for %s\n", m_url);
			CloseHandle(hFile);
			delete pSound;
			m_sound = NULL;
			return false;
		}

		// NOTE: File will be closed by CFilePump when it's done...
	}
	else
	{
		m_sound = new CAudioBuf;
		hr = m_sound->Initialize(&m_format.wfx, dwDataSize);
		if(FAILED(hr))
		{
			DbgPrint("Could not initialize CAudioBuf for %s\n", m_url);
			CloseHandle(hFile);
			delete m_sound;
			m_sound = NULL;
			return false;
		}

		void* pvBuffer = m_sound->Lock();
		bool bError = !ReadFile(hFile, pvBuffer, dwDataSize, &dwRead, NULL) || dwRead != dwDataSize;
		m_sound->Unlock(pvBuffer);

		CloseHandle(hFile);

		if (bError)
		{
			DbgPrint("\001%s is not a valid wave file...\n", m_url);
			delete m_sound;
			m_sound = NULL;

			return false;
		}
	}

	return true;
}

// MP3 soundtrack via streaming pump (CMP3Pump). The pump owns the file
// data and runs decode on its worker thread; no full-song PCM ever lives
// in RAM.
extern const char* MusicCollection_FindSongByID(int songID);

bool CAudioClip::OpenMP3Soundtrack()
{
	int songID = _ttoi(m_url + 3); // skip "st:"
	const char* songPath = MusicCollection_FindSongByID(songID);
	if (!songPath)
	{
		DbgPrint("[AudioClip] st: song ID %d not found\n", songID);
		return false;
	}

	DbgPrint("[AudioClip] Loading MP3: %s\n", songPath);

	ASSERT(m_sound == NULL);
	CMP3Pump* pPump = new CMP3Pump;
	m_sound = pPump;

	HRESULT hr = pPump->Initialize(songPath);
	if (FAILED(hr))
	{
		DbgPrint("[AudioClip] CMP3Pump init failed for %s\n", songPath);
		delete pPump;
		m_sound = NULL;
		return false;
	}

	return true;
}

void CAudioClip::Advance(float nSeconds)
{
    if (m_dirty)
        Initialize();

    // Pause this audio clip if we are in the middle of level transition
    if (m_pause_on_moving && m_sound && m_volume != 0.0f)
    {
        if (g_bLevelTransition)
        {
            if (!m_sound->IsPaused())
            {
                m_sound->Pause(true);
                m_unpauseNeeded = true;
            }
            return;
        }

        if (!g_bLevelTransition && m_unpauseNeeded)
        {
            if (m_sound->IsPaused())
            {
                m_sound->Pause(false);
            }

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
			{
				TRACE(_T("Un-Pausing %s due to non-zero volume...\n"), m_url);
				Pause();
			}

			m_lastVolume = m_volume;

			m_sound->SetAttenuation((1.0f - m_volume) * 100.0f);

			if (m_volume == 0.0f && m_transportMode == TRANSPORT_PLAY)
			{
				TRACE(_T("Pausing %s due to zero volume...\n"), m_url);
				Pause();
			}
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

		// Update the script-visible progress fraction if the clip
		// has opted in via sendProgress. Skipped by default to avoid
		// per-frame property writes that would re-fire OnProgressChanged
		// every Advance.
        if (m_sendProgress)
		{
			float progress = 0.0f;

			float nLength = m_sound->GetPlaybackLength();
			if (nLength != 0.0f)
				progress = m_sound->GetPlaybackTime() / nLength;

			if (progress != m_progress)
			{
				m_progress = progress;
				CallFunction(this, _T("OnProgressChanged"));
			}
		}

		if (m_sound->IsPaused())
		{
			m_transportMode = TRANSPORT_PAUSE;
		}
		else if( m_sound->IsPlaying() )
		{
			m_transportMode = TRANSPORT_PLAY;

			if( m_sendProgress )
			{
				float progress = 0.0f;
				float nLength = m_sound->GetPlaybackLength();
				if (nLength != 0.0f)
				{
					progress = m_sound->GetPlaybackTime();
				}

				if(progress > nLength)
				{
					m_transportMode = TRANSPORT_STOP;
					m_isActive = false;
					CallFunction(this, _T("OnEndOfAudio"));
				}
			}
		}
		else
		{
			if (m_transportMode == TRANSPORT_PLAY)
			{
				m_transportMode = TRANSPORT_STOP;
				m_isActive = false;

				CallFunction(this, _T("OnEndOfAudio"));
			}
		}
	}

	if (m_transportMode != m_lastTransportMode)
	{
		m_lastTransportMode = m_transportMode;
		CallFunction(this, _T("OnTransportModeChanged"));
	}
}

void CAudioClip::PlayOrPause()
{
	switch (m_transportMode)
	{
	case TRANSPORT_PLAY:
	case TRANSPORT_PAUSE:
		Pause();
		break;

	default:
		Play();
		break;
	}
}

void CAudioClip::Play(/*bool bLoop*/)
{
	if (m_dirty)
		Initialize();

	m_capturedLoop = m_loop;

	if (m_sound != NULL)
	{
		m_sound->SetAttenuation((1.0f - m_volume) * 100.0f);
		m_sound->SetPan(m_pan);
		m_sound->SetFrequency(m_frequency);

		if (m_sound->Play(m_capturedLoop))
		{
			m_transportMode = TRANSPORT_PLAY;
			m_isActive = true;
		}
        else DbgPrint("Play failed!\n");
	}

}

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
	m_dirty = true; // so audio buffer is re-created and will start over
}

void CAudioClip::Pause()
{
	if (m_dirty)
		Initialize();

	if (m_transportMode == TRANSPORT_PAUSE)
	{
		m_transportMode = TRANSPORT_PLAY;

		if (m_sound != NULL)
			m_sound->Pause(false);
	}
	else
	{
		m_transportMode = TRANSPORT_PAUSE;

		if (m_sound != NULL)
			m_sound->Pause(true);
	}
}

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

void* CAudioClip::GetSampleBuffer()
{
	if (m_sound == NULL)
		return NULL;

	return m_sound->GetSampleBuffer();
}

int CAudioClip::GetSampleBufferSize()
{
	if (m_sound == NULL)
		return 0;

	return m_sound->GetSampleBufferSize();
}

void CAudioClip::SetUrl(const TCHAR* pszAudioFile)
{
	if(m_url)
	{
		delete [] m_url;
	}
	m_dirty = true;
	if(pszAudioFile)
	{
		m_url = new TCHAR[_tcslen(pszAudioFile)+1];
		_tcscpy(m_url, pszAudioFile);
	}

}

// =========================================================================
// CPeriodicAudioGroup: timed random audio clip playback
// =========================================================================

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

	XTIME m_timeOfNextEvent;
	int m_nextClipIndex;
    CAudioClip* m_currentClip;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("PeriodicAudioGroup", CPeriodicAudioGroup, CGroup)

START_NODE_PROPS(CPeriodicAudioGroup, CGroup)
	NODE_PROP(pt_boolean, CPeriodicAudioGroup, isActive)
	NODE_PROP(pt_number, CPeriodicAudioGroup, period)
	NODE_PROP(pt_number, CPeriodicAudioGroup, periodNoise)
END_NODE_PROPS()


CPeriodicAudioGroup::CPeriodicAudioGroup() :
	m_isActive(false),
    m_pause_on_moving(true),
	m_period(10.0f),
	m_periodNoise(0.0f)
{
	m_visible = false;

	m_timeOfNextEvent = 0.0f;
	m_nextClipIndex = -1;
    m_currentClip = NULL;
}

void CPeriodicAudioGroup::Advance(float nSeconds)
{
	CGroup::Advance(nSeconds);

    // Stop this background audio group if we are in the middle of level transition
    if (m_pause_on_moving && g_bLevelTransition)
    {
        if (m_currentClip && m_currentClip->IsKindOf(NODE_CLASS(CAudioClip)))
        {
            m_currentClip->Stop();
        }

        return;
    }

	if (!m_isActive)
	{
		m_timeOfNextEvent = 0.0f;
		return;
	}

	int nChildCount = m_children.GetLength();
	if (nChildCount == 0)
		return;

	if (m_nextClipIndex < 0)
		m_nextClipIndex = rand() % nChildCount;

	if (m_timeOfNextEvent == 0.0f)
	{
		m_timeOfNextEvent = TheseusGetNow() + m_period + rnd(m_periodNoise);
		TRACE(_T("Next clip will play in %0.2f seconds...\n"), m_timeOfNextEvent - TheseusGetNow());
		return;
	}

	if (TheseusGetNow() > m_timeOfNextEvent)
	{
		m_timeOfNextEvent = 0.0f;

		if (m_nextClipIndex >= nChildCount)
			m_nextClipIndex = 0;

		m_currentClip = (CAudioClip*)m_children.GetNode(m_nextClipIndex);
		m_nextClipIndex += 1;

		if (!m_currentClip->IsKindOf(NODE_CLASS(CAudioClip)))
		{
			TRACE(_T("CPeriodicAudioGroup: child %d is not an AudioClip!\n"), m_nextClipIndex-1);
			return;
		}

		TRACE(_T("CPeriodicAudioGroup: playing clip %d (%s)\n"), m_nextClipIndex-1, m_currentClip->m_url);
		m_currentClip->Play();
	}
}
