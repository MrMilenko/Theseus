// ntiosvc.h: the CD-ROM service behind audio CDs. Opens the drive, reads
// the table of contents into frame offsets and does raw CDDA reads.
// Rebuilt from the 5960 retail XBE; see docs/decomp/NtIoSvc.md.

#pragma once

// Red Book audio: 75 frames a second, 2352 bytes a frame.
#define CDAUDIO_FRAMES_PER_SECOND   75
#define CDAUDIO_FRAMES_PER_MINUTE   (60 * CDAUDIO_FRAMES_PER_SECOND)
#define CDAUDIO_BYTES_PER_FRAME     2352

// What the drive told us about the disc. 5960 keeps just the track count
// and the start frames; the name strings and disc id are Theseus's.
struct XCDROM_TOC
{
public:
	XCDROM_TOC();
	~XCDROM_TOC();

	void Delete();
	int GetTrackFromFrame(DWORD dwPosition) const;

	// TrackAddr[LastTrack] is where the last audio track ends.
	int LastTrack;
	DWORD TrackAddr[100];

	void UpdateDiscID();

	TCHAR* rgszTrack[100];
	TCHAR* szTitle;
	TCHAR* szArtist;
	TCHAR* szID;

protected:
	void Clear();
};

typedef XCDROM_TOC* PXCDROM_TOC;

class CNtIoctlCdromService
{
private:
	HANDLE m_hDrive;

public:
	CNtIoctlCdromService();
	~CNtIoctlCdromService();

	HRESULT Open(DWORD dwDriveNumber);
	void Close();
	HRESULT Read(DWORD dwReadStart, DWORD dwReadLength, LPVOID pvBuffer, DWORD dwRetries = 0);

	bool IsOpen() const { return m_hDrive != INVALID_HANDLE_VALUE; }
	int GetTrackCount() const { return IsOpen() ? m_trackTable.LastTrack : 0; }

	bool GetTotalLength(int* pnMinutes, int* pnSeconds, int* pnFrames);
	bool GetTrackLength(int nTrack, int* pnMinutes, int* pnSeconds, int* pnFrames);

	const TCHAR* GetTitle();
	const TCHAR* GetArtist();
	const TCHAR* GetTrackName(int nTrack);

	DWORD GetTrackFrame(int nTrack) const { return IsOpen() ? m_trackTable.TrackAddr[nTrack] : 0; }
	int GetTrackFromFrame(DWORD dwFrame) const { return m_trackTable.GetTrackFromFrame(dwFrame); }

protected:
	XCDROM_TOC m_trackTable;
	bool GetTableOfContents();
	HRESULT DeviceIoControl(DWORD dwControlCode, LPVOID pvInBuffer = NULL, DWORD dwInBufferSize = 0, LPVOID pvOutBuffer = NULL, DWORD dwOutBufferSize = 0, LPDWORD pdwBytesReturned = NULL);
};

extern CNtIoctlCdromService g_discIoctl;
