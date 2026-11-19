// ntiosvc.cpp: the CD-ROM service behind audio CDs. Open, the table of
// contents, raw reads and the track lengths are rebuilt from the 5960
// retail XBE (0x64da0..0x650c0, g_discIoctl at 0x188780); see
// docs/decomp/NtIoSvc.md. The disc id and name strings are Theseus's.

#include "std.h"
#include "theseus.h"
#include "ntiosvc.h"

CNtIoctlCdromService g_discIoctl;

// A TOC address is four bytes: unused, minute, second, frame.
static DWORD AddressToFrames(const UCHAR* addr)
{
	return (addr[1] * 60 + addr[2]) * CDAUDIO_FRAMES_PER_SECOND + addr[3];
}

static void SplitFrames(DWORD dwFrames, int* pnMinutes, int* pnSeconds, int* pnFrames)
{
	DWORD dwInMinute = dwFrames % CDAUDIO_FRAMES_PER_MINUTE;
	if (pnMinutes != NULL)
		*pnMinutes = (BYTE)(dwFrames / CDAUDIO_FRAMES_PER_MINUTE);
	if (pnSeconds != NULL)
		*pnSeconds = dwInMinute / CDAUDIO_FRAMES_PER_SECOND;
	if (pnFrames != NULL)
		*pnFrames = dwInMinute % CDAUDIO_FRAMES_PER_SECOND;
}

// CNtIoctlCdromService::CNtIoctlCdromService()
// Reconstructed from 5960 retail XBE @ 0x00064da0:
//     m_hDrive = -1; m_trackTable.LastTrack = 0;
CNtIoctlCdromService::CNtIoctlCdromService()
{
	m_hDrive = INVALID_HANDLE_VALUE;
}

CNtIoctlCdromService::~CNtIoctlCdromService()
{
	Close();
}

// CNtIoctlCdromService::Close()
// Reconstructed from 5960 retail XBE @ 0x00064db0:
//     if (m_hDrive != -1) { CloseHandle(m_hDrive); m_hDrive = -1; }
//     m_trackTable.LastTrack = 0;
// Theseus also frees its name strings, which clears the track count too.
void CNtIoctlCdromService::Close()
{
	if (m_hDrive != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hDrive);
		m_hDrive = INVALID_HANDLE_VALUE;
	}
	m_trackTable.Delete();
}

// CNtIoctlCdromService::Open(drive)
// Reconstructed from 5960 retail XBE @ 0x00064dd0:
//     h = CreateFile(L"CDROM0:", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, NORMAL, 0);
//     if (h == -1) return HRESULT_FROM_WIN32(GetLastError());
//     if (!GetTableOfContents()) Close();
//     return S_OK;
// The drive number goes unused; there is only one drive.
HRESULT CNtIoctlCdromService::Open(DWORD dwDriveNumber)
{
	m_hDrive = TheseusCreateFile(_T("CDROM0:"), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (m_hDrive == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());

	if (!GetTableOfContents())
		Close();
	return S_OK;
}

// CNtIoctlCdromService::GetTableOfContents()
// Reconstructed from 5960 retail XBE @ 0x00064f20. One IOCTL_CDROM_READ_TOC
// (a drive that isn't ready fails it), then each entry's start frame goes
// into TrackAddr until the lead-out (track 0xAA) or a data track. A data
// track's start is pulled back by the 2:30 gap so the audio before it ends
// there. LastTrack ends up as the index it stopped at. See the doc for the
// loop.
bool CNtIoctlCdromService::GetTableOfContents()
{
	CDROM_TOC toc;
	if (FAILED(DeviceIoControl(IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc))))
		return false;

	if (toc.LastTrack < toc.FirstTrack)
		return false;

	DWORD nEntries = toc.LastTrack - toc.FirstTrack + 1;
	DWORD i = 0;
	for (;;)
	{
		UCHAR* addr = toc.TrackData[i].Address;
		m_trackTable.TrackAddr[i] = AddressToFrames(addr);

		if (toc.TrackData[i].TrackNumber == 0xAA)
			break;

		if (toc.TrackData[i].Control & 4)
		{
			// Back off two minutes thirty, borrowing a minute when the
			// seconds wrap.
			addr[1] -= 2;
			addr[2] += 30;
			if (addr[2] < 60)
				addr[1] -= 1;
			else
				addr[2] -= 60;
			m_trackTable.TrackAddr[i] = AddressToFrames(addr);
			break;
		}

		if (i + 1 > nEntries)
			break;
		i += 1;
	}

	m_trackTable.LastTrack = i;

	// Theseus: the disc id.
	if (m_trackTable.LastTrack > 0)
		m_trackTable.UpdateDiscID();

	return m_trackTable.LastTrack > 0;
}

// CNtIoctlCdromService::Read(start, count, buffer, retries)
// Reconstructed from 5960 retail XBE @ 0x000650c0:
//     rri = { (start - 150) * 2048, count, CDDA };
//     for (;;) { if (ioctl(RAW_READ, &rri, buf, count * 2352)) return S_OK;
//                hr = HRESULT_FROM_WIN32(GetLastError()); if (SUCCEEDED(hr)) break;
//                if (retries-- == 0) return hr; }
// The drive wants the offset as if every disc had the 150 frame lead in.
HRESULT CNtIoctlCdromService::Read(DWORD dwReadStart, DWORD dwReadLength, LPVOID pvBuffer, DWORD dwRetries)
{
	RAW_READ_INFO rri;
	rri.DiskOffset.QuadPart = (ULONGLONG)(dwReadStart - 150) * 2048;
	rri.SectorCount = dwReadLength;
	rri.TrackMode = CDDA;

	HRESULT hr;
	for (;;)
	{
		hr = DeviceIoControl(IOCTL_CDROM_RAW_READ, &rri, sizeof(rri), pvBuffer, dwReadLength * CDAUDIO_BYTES_PER_FRAME);
		if (SUCCEEDED(hr) || dwRetries == 0)
			break;
		dwRetries -= 1;
	}
	return hr;
}

// 5960 inlines this at each IOCTL: the call, then GetLastError as an HRESULT.
HRESULT CNtIoctlCdromService::DeviceIoControl(DWORD dwControlCode, LPVOID pvInBuffer, DWORD dwInBufferSize, LPVOID pvOutBuffer, DWORD dwOutBufferSize, LPDWORD pdwBytesReturned)
{
	DWORD dwIgnored;
	if (::DeviceIoControl(m_hDrive, dwControlCode, pvInBuffer, dwInBufferSize, pvOutBuffer, dwOutBufferSize,
	                      pdwBytesReturned != NULL ? pdwBytesReturned : &dwIgnored, NULL))
		return S_OK;
	return HRESULT_FROM_WIN32(GetLastError());
}

// Disc and track lengths. 5960 inlines these into FormatTotalTime @ 0x0003a060
// and FormatTrackTime @ 0x0003a150:
//     frames = TrackAddr[LastTrack] - TrackAddr[0];   (or [n + 1] - [n])
//     minutes = (BYTE)(frames / 4500); seconds = frames % 4500 / 75;
// A track number past the last audio track gets nothing.
bool CNtIoctlCdromService::GetTotalLength(int* pnMinutes, int* pnSeconds, int* pnFrames)
{
	if (!IsOpen())
		return false;
	SplitFrames(m_trackTable.TrackAddr[m_trackTable.LastTrack] - m_trackTable.TrackAddr[0], pnMinutes, pnSeconds, pnFrames);
	return true;
}

bool CNtIoctlCdromService::GetTrackLength(int nTrack, int* pnMinutes, int* pnSeconds, int* pnFrames)
{
	if (!IsOpen() || nTrack < 0 || nTrack >= m_trackTable.LastTrack)
		return false;
	SplitFrames(m_trackTable.TrackAddr[nTrack + 1] - m_trackTable.TrackAddr[nTrack], pnMinutes, pnSeconds, pnFrames);
	return true;
}

// Theseus: name lookups. Nothing fills these in yet, so they stay NULL.
const TCHAR* CNtIoctlCdromService::GetTitle()
{
	return IsOpen() ? m_trackTable.szTitle : NULL;
}

const TCHAR* CNtIoctlCdromService::GetArtist()
{
	return IsOpen() ? m_trackTable.szArtist : NULL;
}

const TCHAR* CNtIoctlCdromService::GetTrackName(int nTrack)
{
	return IsOpen() ? m_trackTable.rgszTrack[nTrack] : NULL;
}

// Theseus: the strings around the 5960 track list.
XCDROM_TOC::XCDROM_TOC()
{
	Clear();
}

XCDROM_TOC::~XCDROM_TOC()
{
	Delete();
}

void XCDROM_TOC::Delete()
{
	delete[] szTitle;
	delete[] szArtist;
	delete[] szID;
	for (int i = 0; i < 100; i++)
		delete[] rgszTrack[i];
	Clear();
}

void XCDROM_TOC::Clear()
{
	szTitle = szArtist = szID = NULL;
	memset(rgszTrack, 0, sizeof(rgszTrack));
	memset(TrackAddr, 0, sizeof(TrackAddr));
	LastTrack = 0;
}

// The track a frame falls in; anything past the end counts as the first.
int XCDROM_TOC::GetTrackFromFrame(DWORD dwPosition) const
{
	for (int track = 0; track < LastTrack; track++)
	{
		if (dwPosition < TrackAddr[track + 1])
			return track;
	}
	return 0;
}

// Theseus: the disc id is every track's start frame as six hex digits.
void XCDROM_TOC::UpdateDiscID()
{
	szID = new TCHAR[LastTrack * 6 + 1];
	szID[0] = 0;
	for (int track = 0; track < LastTrack; track++)
		_stprintf(szID + track * 6, _T("%06X"), TrackAddr[track]);
}
