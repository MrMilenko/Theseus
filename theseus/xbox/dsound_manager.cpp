// dsound_manager.cpp: DirectSound initialization and buffer
// management. Singleton owns the DirectSound device and worker
// thread; provides buffer creation, lock / unlock, and notify
// operations. Xbox-only; desktop has its own SDL audio backend.

#include "std.h"
#include "theseus.h"
#include "dsound_manager.h"
#include <dsound.h>
#include <dsstdfx.h>

extern "C" extern DWORD g_dwDirectSoundOverrideSpeakerConfig;

#define DISC_VIDEO 4
extern int g_nDiscType;

DSoundManager* DSoundManager::pinstance = 0;

DSoundManager* DSoundManager::Instance()
{
	if (pinstance == 0)
	{
		pinstance = new DSoundManager;
	}
	return pinstance;
}

DSoundManager::DSoundManager() : m_directSound(NULL), m_hDirectSoundThread(NULL), m_bShutdown(false)
{
}

DSoundManager::~DSoundManager()
{
	Cleanup();
}

static DWORD WINAPI DirectSoundThreadProc(LPVOID pvParameter)
{
	DSoundManager *pThis = static_cast<DSoundManager*>(pvParameter);

	while (!pThis->m_bShutdown)
	{
		DirectSoundDoWork();
		Sleep(100);
	}

	return 0;
}

HRESULT DSoundManager::Initialize()
{
	HRESULT hr = S_OK;

	if (m_directSound != NULL)
		return S_FALSE;

	if (DISC_VIDEO == g_nDiscType)
		return E_FAIL;

	// Configure speaker output based on AV pack
	DWORD dwSpeakerConfig = XAudioGetSpeakerConfig();

	if (DSSPEAKER_MONO == DSSPEAKER_BASIC(dwSpeakerConfig))
		g_dwDirectSoundOverrideSpeakerConfig = DSSPEAKER_COMBINED(DSSPEAKER_MONO, 0);
	else
		g_dwDirectSoundOverrideSpeakerConfig = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 0);

	hr = DirectSoundCreate(NULL, &m_directSound, NULL);
	if (FAILED(hr))
	{
		LogComError(hr, "DSound_Init: DirectSoundCreate");
		return hr;
	}

	if (FAILED(hr = m_directSound->SetMixBinHeadroom(DSMIXBIN_I3DL2, 0)))
	{
		LogComError(hr, "DSound_Init: SetMixBinHeadroom");
	}

	// Spawn the DirectSoundDoWork polling thread
	DWORD dwThreadId;
	m_hDirectSoundThread = CreateThread(NULL, 0, DirectSoundThreadProc, this, 0, &dwThreadId);
	if (!m_hDirectSoundThread)
	{
		hr = HRESULT_FROM_WIN32(GetLastError());
		LogComError(hr, "DSound_Init: failed to create DirectSound worker thread");
		return hr;
	}

	return hr;
}

HRESULT DSoundManager::Cleanup()
{
	m_bShutdown = true;

	if (m_directSound != NULL)
	{
		m_directSound->Release();
		m_directSound = NULL;
	}

	if (m_hDirectSoundThread != NULL)
	{
		CloseHandle(m_hDirectSoundThread);
		m_hDirectSoundThread = NULL;
	}

	return S_OK;
}

HRESULT DSoundManager::DSoundCreateSoundBuffer(IN WAVEFORMATEX* pwfx, IN int nByteCount, IN DWORD dwFlags, OUT LPDIRECTSOUNDBUFFER* pDirectBuf)
{
	HRESULT hr = S_OK;
	*pDirectBuf = NULL;

	if (m_directSound == NULL)
		hr = Initialize();

	if (FAILED(hr))
		return hr;

	LPDIRECTSOUNDBUFFER lpDirectSoundBuffer = NULL;
	DSBUFFERDESC dsbd;
	ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
	dsbd.dwSize = sizeof(DSBUFFERDESC);
	dsbd.dwFlags = dwFlags;
	dsbd.dwBufferBytes = nByteCount;
	dsbd.lpwfxFormat = pwfx;

	hr = m_directSound->CreateSoundBuffer(&dsbd, &lpDirectSoundBuffer, NULL);

	if (SUCCEEDED(hr))
		lpDirectSoundBuffer->SetHeadroom(1200);

	*pDirectBuf = lpDirectSoundBuffer;
	return hr;
}

HRESULT DSoundManager::DSoundSetSoundBufferData(LPDIRECTSOUNDBUFFER pDirectSoundBuffer, UINT nByteOffset, UINT nByteCount, const void* pvData)
{
	LPVOID pbBuffer;
	DWORD dwBufferLength;

	ASSERT(pDirectSoundBuffer != NULL);

	HRESULT hr = pDirectSoundBuffer->Lock(nByteOffset, nByteCount, &pbBuffer, &dwBufferLength, NULL, NULL, 0L);
	if (FAILED(hr))
		return hr;

	CopyMemory(pbBuffer, pvData, nByteCount);

	hr = pDirectSoundBuffer->Unlock(pbBuffer, dwBufferLength, NULL, 0);
	return hr;
}

HRESULT DSoundManager::DSoundSetSoundBufferNotify(LPDIRECTSOUNDBUFFER pDirectSoundBuffer, int nPositionCount, DSBPOSITIONNOTIFY* positions)
{
	return pDirectSoundBuffer->SetNotificationPositions(nPositionCount, positions);
}
