// dsound_manager.cpp: the Xbox DirectSound device, its work thread and
// the buffer helpers the audio code uses. Init and buffer creation are
// rebuilt from the 5960 retail XBE (0x626b0, 0x62750); the singleton,
// cleanup and the lock / notify helpers are Theseus's.

#include "std.h"
#include "theseus.h"
#include "dsound_manager.h"
#include <dsound.h>
#include <dsstdfx.h>

extern "C" extern DWORD g_dwDirectSoundOverrideSpeakerConfig;

#define DISC_DVD 4
extern int g_discKind;

DSoundManager* DSoundManager::pinstance = NULL;

DSoundManager* DSoundManager::Instance()
{
	if (pinstance == NULL)
		pinstance = new DSoundManager;
	return pinstance;
}

DSoundManager::DSoundManager()
{
	m_directSound = NULL;
	m_hDirectSoundThread = NULL;
	m_bShutdown = false;
}

DSoundManager::~DSoundManager()
{
	Cleanup();
}

// The DirectSound work thread. 5960 (0x00062680, not split out by the
// decompiler) is:
//     while (g_pDirectSound != NULL) { DirectSoundDoWork(); Sleep(100); }
// Theseus stops on its own shutdown flag, set before the device goes away.
static DWORD WINAPI DirectSoundWorker(LPVOID pvParameter)
{
	DSoundManager* pManager = (DSoundManager*)pvParameter;
	while (!pManager->m_bShutdown)
	{
		DirectSoundDoWork();
		Sleep(100);
	}
	return 0;
}

// DSoundManager::Initialize()
// Reconstructed from 5960 retail XBE @ 0x00062750:
//     if (g_BootType == DISC_DVD) return false;
//     g_dwDirectSoundOverrideSpeakerConfig = (XGetAudioFlags() basic == MONO);
//     if (DirectSoundCreate(0, &g_pDirectSound, 0) < 0) return false;
//     for (bin = 0; bin < 31; bin++) SetMixBinHeadroom(bin, 0);
//     return (g_hThread = CreateThread(0, 0, worker, 0, 0, &id)) != 0;
// The DVD player owns the audio hardware, so a movie disc gets no DirectSound.
// Theseus zeroes only the I3DL2 bin's headroom and returns an HRESULT.
HRESULT DSoundManager::Initialize()
{
	if (m_directSound != NULL)
		return S_FALSE;

	if (g_discKind == DISC_DVD)
		return E_FAIL;

	// Mono stays mono; everything else mixes as stereo and lets the
	// hardware encoder handle surround.
	if (DSSPEAKER_BASIC(XAudioGetSpeakerConfig()) == DSSPEAKER_MONO)
		g_dwDirectSoundOverrideSpeakerConfig = DSSPEAKER_COMBINED(DSSPEAKER_MONO, 0);
	else
		g_dwDirectSoundOverrideSpeakerConfig = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 0);

	HRESULT hr = DirectSoundCreate(NULL, &m_directSound, NULL);
	if (FAILED(hr))
	{
		LogComError(hr, "DirectSoundCreate");
		return hr;
	}

	hr = m_directSound->SetMixBinHeadroom(DSMIXBIN_I3DL2, 0);
	if (FAILED(hr))
		LogComError(hr, "SetMixBinHeadroom");

	DWORD dwThreadId;
	m_hDirectSoundThread = CreateThread(NULL, 0, DirectSoundWorker, this, 0, &dwThreadId);
	if (m_hDirectSoundThread == NULL)
	{
		hr = HRESULT_FROM_WIN32(GetLastError());
		LogComError(hr, "CreateThread");
	}
	return hr;
}

// Theseus: shut the worker down and drop the device.
HRESULT DSoundManager::Cleanup()
{
	m_bShutdown = true;
	RELEASENULL(m_directSound);
	if (m_hDirectSoundThread != NULL)
	{
		CloseHandle(m_hDirectSoundThread);
		m_hDirectSoundThread = NULL;
	}
	return S_OK;
}

// DSoundManager::DSoundCreateSoundBuffer(format, bytes, flags, out)
// Reconstructed from 5960 retail XBE @ 0x000626b0:
//     if (!g_pDirectSound && !DSound_Init()) return NULL;
//     DSBUFFERDESC desc = { 0x18, flags, bytes, format };
//     if (CreateSoundBuffer(&desc, &buf, 0) >= 0) buf->SetHeadroom(1200);
//     return buf;
// Theseus hands back the HRESULT and the buffer through pDirectBuf.
HRESULT DSoundManager::DSoundCreateSoundBuffer(IN WAVEFORMATEX* pwfx, IN int nByteCount, IN DWORD dwFlags, OUT LPDIRECTSOUNDBUFFER* pDirectBuf)
{
	*pDirectBuf = NULL;

	if (m_directSound == NULL)
	{
		HRESULT hrInit = Initialize();
		if (FAILED(hrInit))
			return hrInit;
	}

	DSBUFFERDESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwFlags = dwFlags;
	desc.dwBufferBytes = nByteCount;
	desc.lpwfxFormat = pwfx;

	LPDIRECTSOUNDBUFFER pBuffer = NULL;
	HRESULT hr = m_directSound->CreateSoundBuffer(&desc, &pBuffer, NULL);
	if (SUCCEEDED(hr))
		pBuffer->SetHeadroom(1200);

	*pDirectBuf = pBuffer;
	return hr;
}

// Theseus: copy data into a buffer.
HRESULT DSoundManager::DSoundSetSoundBufferData(LPDIRECTSOUNDBUFFER pDirectSoundBuffer, UINT nByteOffset, UINT nByteCount, const void* pvData)
{
	LPVOID pvLocked;
	DWORD dwLocked;
	HRESULT hr = pDirectSoundBuffer->Lock(nByteOffset, nByteCount, &pvLocked, &dwLocked, NULL, NULL, 0);
	if (FAILED(hr))
		return hr;
	memcpy(pvLocked, pvData, nByteCount);
	return pDirectSoundBuffer->Unlock(pvLocked, dwLocked, NULL, 0);
}

// Theseus: position notifications.
HRESULT DSoundManager::DSoundSetSoundBufferNotify(LPDIRECTSOUNDBUFFER pDirectSoundBuffer, int nPositionCount, DSBPOSITIONNOTIFY* positions)
{
	return pDirectSoundBuffer->SetNotificationPositions(nPositionCount, positions);
}
