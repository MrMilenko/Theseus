// ntiosvc.h: NT IOCTL CD-ROM service. CDDA TOC parsing, raw-frame
// reads, CD-Text title / artist / track lookup. Used by the music
// player when a regular audio CD is inserted. Decompiled from the
// 5960 retail XBE.

#pragma once

// CD audio type conversions

#define CDAUDIO_BYTES_PER_FRAME         2352
#define CDAUDIO_BYTES_PER_SECOND        176400
#define CDAUDIO_BYTES_PER_MINUTE        10584000

#define CDAUDIO_FRAMES_PER_SECOND       75
#define CDAUDIO_FRAMES_PER_MINUTE       4500

// MCI time format conversion macros

#define MCI_MSF_MINUTE(msf)             ((BYTE)(msf))
#define MCI_MSF_SECOND(msf)             ((BYTE)(((WORD)(msf)) >> 8))
#define MCI_MSF_FRAME(msf)              ((BYTE)((msf)>>16))

#define MCI_MAKE_MSF(m, s, f)           ((DWORD)(((BYTE)(m) | \
                                        ((WORD)(s)<<8)) | \
                                        (((DWORD)(BYTE)(f))<<16)))

__inline DWORD MsfToFrames(DWORD dwMsf)
{
    return MCI_MSF_MINUTE(dwMsf) * CDAUDIO_FRAMES_PER_MINUTE +
           MCI_MSF_SECOND(dwMsf) * CDAUDIO_FRAMES_PER_SECOND +
           MCI_MSF_FRAME(dwMsf);
}

__inline DWORD FramesToMsf(DWORD dwFrames)
{
    return MCI_MAKE_MSF(
        dwFrames / CDAUDIO_FRAMES_PER_MINUTE,
        (dwFrames % CDAUDIO_FRAMES_PER_MINUTE) / CDAUDIO_FRAMES_PER_SECOND,
        (dwFrames % CDAUDIO_FRAMES_PER_MINUTE) % CDAUDIO_FRAMES_PER_SECOND);
}

__inline DWORD TocValToMsf(LPBYTE ab)
{
    return MCI_MAKE_MSF(ab[1], ab[2], ab[3]);
}

__inline DWORD TocValToFrames(LPBYTE ab)
{
    return MsfToFrames(TocValToMsf(ab));
}

struct XCDROM_TOC
{
public:
	XCDROM_TOC();
	~XCDROM_TOC();

	void Delete();
	int GetTrackFromFrame(DWORD dwPosition) const;

    int LastTrack;
    DWORD TrackAddr [100];

	void UpdateDiscID();

	TCHAR* rgszTrack [100];
	TCHAR* szTitle;
	TCHAR* szArtist;
	TCHAR* szID;

protected:
	void Clear();
};

typedef XCDROM_TOC* PXCDROM_TOC;

// Read in 1-second chunks: small enough that music can stop on a
// chunk boundary, large enough that older drives stay happy.
#define FRAMES_PER_CHUNK (CDAUDIO_FRAMES_PER_SECOND)

#define CD_AUDIO_SEGMENTS_PER_BUFFER ((4*CDAUDIO_FRAMES_PER_SECOND)/FRAMES_PER_CHUNK)

#define BYTES_PER_CHUNK (FRAMES_PER_CHUNK * CDAUDIO_BYTES_PER_FRAME)

class CNtIoctlCdromService
{
private:
	HANDLE m_hDevice;

public:
	CNtIoctlCdromService();
	~CNtIoctlCdromService();

	HRESULT Open(DWORD dwDriveNumber);
	void Close();
	HRESULT Read(DWORD dwReadStart, DWORD dwReadLength, LPVOID pvBuffer, DWORD dwRetries = 0);

	inline bool IsOpen() const
	{
		return m_hDevice != INVALID_HANDLE_VALUE;
	}

	inline int GetTrackCount() const
	{
		if (!IsOpen())
			return 0;

		return m_toc.LastTrack;
	}

	bool GetTotalLength(int* pnMinutes, int* pnSeconds, int* pnFrames);
	bool GetTrackLength(int nTrack, int* pnMinutes, int* pnSeconds, int* pnFrames);

	const TCHAR* GetTitle();
	const TCHAR* GetArtist();
	const TCHAR* GetTrackName(int nTrack);

	inline DWORD GetTrackFrame(int nTrack) const
	{
		if (!IsOpen())
			return 0;

		return m_toc.TrackAddr[nTrack];
	}

	inline int GetTrackFromFrame(DWORD dwFrame) const
	{
		return m_toc.GetTrackFromFrame(dwFrame);
	}

protected:
	XCDROM_TOC m_toc;
	bool GetTableOfContents();
	HRESULT DeviceIoControl(DWORD dwControlCode, LPVOID pvInBuffer = NULL, DWORD dwInBufferSize = 0, LPVOID pvOutBuffer = NULL, DWORD dwOutBufferSize = 0, LPDWORD pdwBytesReturned = NULL);
};

extern CNtIoctlCdromService g_cdrom;
extern bool OpenCDROM();
