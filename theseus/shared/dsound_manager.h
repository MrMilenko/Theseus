// dsound_manager.h: DSoundManager singleton. Owns the IDirectSound8
// device and hands out sound buffers to the audio system. Cross-
// platform shim; the desktop build implements this on top of SDL
// audio.

#pragma once

class DSoundManager
{
public:
	static DSoundManager* Instance();
	~DSoundManager();

	HRESULT Initialize();
	HRESULT Cleanup();
	HRESULT DSoundCreateSoundBuffer(IN WAVEFORMATEX* pwfx, IN int nByteCount, IN DWORD dwFlags, OUT LPDIRECTSOUNDBUFFER* pDirectBuf);
	HRESULT DSoundSetSoundBufferData(LPDIRECTSOUNDBUFFER pDirectSoundBuffer, UINT nByteOffset, UINT nByteCount, const void* pvData);
	HRESULT DSoundSetSoundBufferNotify(LPDIRECTSOUNDBUFFER pDirectSoundBuffer, int nPositionCount, DSBPOSITIONNOTIFY* positions);

	bool m_bShutdown;

protected:
	DSoundManager();
	DSoundManager(const DSoundManager&);
	DSoundManager& operator=(const DSoundManager&);

private:
	static DSoundManager* pinstance;
	LPDIRECTSOUND8 m_directSound;
	HANDLE m_hDirectSoundThread;
};
