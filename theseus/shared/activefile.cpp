// activefile.cpp: CActiveFile, a file pulled into memory by URL. Rebuilt
// function by function from the 5960 retail XBE (0x2e560..0x2e840).

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "activefile.h"
#include "xip_archive.h"

CActiveFile::CActiveFile()
{
	m_url = NULL;
	m_content = NULL;
	m_contentSize = 0;
	memset(&m_modifiedTime, 0, sizeof(m_modifiedTime));
	m_inXIP = false;
}

CActiveFile::~CActiveFile()
{
	Reset();
}

// CActiveFile::Reset()
// Reconstructed from 5960 retail XBE @ 0x0002e560: frees the URL and the
// content and zeroes the size and the time.
void CActiveFile::Reset()
{
	delete[] m_url;
	m_url = NULL;
	TheseusFreeMemory(m_content);
	m_content = NULL;
	m_contentSize = 0;
	memset(&m_modifiedTime, 0, sizeof(m_modifiedTime));
}

BYTE *CActiveFile::DetachContent()
{
	BYTE *content = m_content;
	m_content = NULL;
	return content;
}

// Takes url's content out of a loaded XIP if one has it.
static bool TakeFromXIP(const TCHAR *url, BYTE *&content, DWORD &size, bool &inXIP)
{
	if (!FindInXIPAndDetach(url, content, size))
		return false;
	inXIP = true;
	return true;
}

// CActiveFile::Fetch(url, searchAppDir, try)
// Reconstructed from 5960 retail XBE @ 0x0002e5a0. Resets and keeps a copy
// of url. An empty url succeeds with no content. "string:" (any case) makes
// the rest of the url the content. A relative url (not UNC, no drive) is
// resolved against the current folder and looked up in the XIPs; failing
// that, with searchAppDir set and no such file, it falls back to the app
// folder + url, and that becomes the url. Then the XIPs are tried with the
// final url, and last the disk through FetchFile.
bool CActiveFile::Fetch(const TCHAR *szURL, bool bSearchAppDir /*=false*/, bool bTry /*=false*/)
{
	Reset();
	m_url = new TCHAR[_tcslen(szURL) + 1];
	_tcscpy(m_url, szURL);
	if (m_url[0] == 0)
		return true;

	if (_tcsnicmp(m_url, _T("string:"), 7) == 0)
	{
		const TCHAR *text = szURL + 7;
		m_contentSize = _tcslen(text) * sizeof(TCHAR);
		m_content = (BYTE *)TheseusAllocMemory(m_contentSize + sizeof(TCHAR));
		_tcscpy((TCHAR *)m_content, text);
		return true;
	}

	bool absolute = (szURL[0] == '\\' && szURL[1] == '\\') || (szURL[0] != 0 && szURL[1] == ':');
#ifdef THESEUS_DESKTOP
	// Theseus: a host path (/Users/..., /home/...) is absolute as well.
	if (szURL[0] == '/')
		absolute = true;
#endif
	if (!absolute)
	{
		TCHAR resolved[MAX_PATH];
		MakeAbsoluteURL(resolved, szURL);
		if (TakeFromXIP(resolved, m_content, m_contentSize, m_inXIP))
			return true;

		if (bSearchAppDir && !DoesFileExist(resolved))
		{
			_tcscpy(resolved, TheseusGetAppDir());
			_tcscat(resolved, szURL);
		}
		delete[] m_url;
		m_url = new TCHAR[_tcslen(resolved) + 1];
		_tcscpy(m_url, resolved);
	}

	if (TakeFromXIP(m_url, m_content, m_contentSize, m_inXIP))
		return true;
	return FetchFile(bTry);
}

// CActiveFile::FetchFile(try)
// Reconstructed from 5960 retail XBE @ 0x0002e750. Opens the url for
// sequential reading. With try set it gives up unless the size plus 1 MB of
// physical memory is free. Reads it all, null terminates it (two zero
// bytes), and keeps it with its write time. Theseus closes the handle on
// the memory check too, and on a GetFileTime failure frees the new buffer
// (5960 frees the old content there).
bool CActiveFile::FetchFile(bool bTry /*=false*/)
{
	HANDLE file = TheseusCreateFile(m_url, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	DWORD size = GetFileSize(file, NULL);
	if (bTry)
	{
		MEMORYSTATUS memory;
		GlobalMemoryStatus(&memory);
		if (memory.dwAvailPhys < size + 1024 * 1024)
		{
			CloseHandle(file);
			return false;
		}
	}

	BYTE *content = (BYTE *)TheseusAllocMemory(size + sizeof(TCHAR));
	ReadFile(file, content, size, &size, NULL);
	memset(content + size, 0, sizeof(TCHAR));

	bool timed = GetFileTime(file, NULL, NULL, &m_modifiedTime) != 0;
	CloseHandle(file);
	if (!timed)
	{
		TheseusFreeMemory(content);
		return false;
	}

	TheseusFreeMemory(m_content);
	m_content = content;
	m_contentSize = size;
	m_inXIP = false;
	return true;
}

#ifdef _UNICODE
// Whether the content starts with the FF FE mark (and has more than it).
bool CActiveFile::IsUnicode()
{
	return m_content != NULL && m_contentSize > 2 && m_content[0] == 0xff && m_content[1] == 0xfe;
}

// CActiveFile::MakeUnicode()
// Reconstructed from 5960 retail XBE @ 0x0002e840. Content with the FF FE
// mark just has it moved off the front (so it no longer reads as Unicode).
// Anything else is widened byte by byte into a new buffer, terminated, and
// the size becomes (bytes + 1) * 2.
void CActiveFile::MakeUnicode()
{
	if (IsUnicode())
	{
		memmove(m_content, m_content + 2, m_contentSize);
		return;
	}

	TCHAR *wide = (TCHAR *)TheseusAllocMemory((m_contentSize + 1) * 2);
	Unicode(wide, (const char *)m_content, m_contentSize);
	wide[m_contentSize] = 0;
	TheseusFreeMemory(m_content);
	m_content = (BYTE *)wide;
	m_contentSize = (m_contentSize + 1) * 2;
	m_inXIP = false;
}
#endif
