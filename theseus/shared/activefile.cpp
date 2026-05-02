// activefile.cpp: CActiveFile, the dashboard's "load file once into RAM"
// helper. Tracks the open path, in-memory buffer, and length, with a
// fallback chain that tries skin-override paths, the loose extracted
// directory, and the XIP archive in order. Decompiled from the 5960
// retail XBE.

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
	ZeroMemory(&m_modifiedTime, sizeof(m_modifiedTime));
	m_readPos = 0;
	m_inXIP = false;

	m_updatePeriod = 5.0f;
	m_nextUpdateTime = 0.0f;
}

CActiveFile::~CActiveFile()
{
	Reset();
}

void CActiveFile::Reset()
{
	delete[] m_url;

	TheseusFreeMemory(m_content);

	m_url = NULL;
	m_content = NULL;
	m_contentSize = 0;
	ZeroMemory(&m_modifiedTime, sizeof(m_modifiedTime));
}

BYTE *CActiveFile::DetachContent()
{
	BYTE *pbContent = m_content;
	m_content = NULL;
	return pbContent;
}

bool CActiveFile::Fetch(const TCHAR *szURL, bool bSearchAppDir /*=false*/, bool bTry /*=false*/)
{
	Reset();

	m_url = new TCHAR[_tcslen(szURL) + 1];
	_tcscpy(m_url, szURL);

	if (m_url[0] == '\0')
	{
		return true;
	}

	else if (_tcsnicmp(m_url, _T("string:"), 7) == 0)
	{
		szURL += 7; // skip 'string:'

		m_contentSize = _tcslen(szURL) * sizeof(TCHAR);
		m_content = (BYTE *)TheseusAllocMemory(m_contentSize + sizeof(TCHAR));
		_tcscpy((TCHAR *)m_content, szURL);
	}
	else // file system
	{
		if (!((szURL[0] == '\\' && szURL[1] == '\\') || (szURL[0] != 0 && szURL[1] == ':')))
		{
			// Make it absolute...
			TCHAR szBuf[MAX_PATH];
			MakeAbsoluteURL(szBuf, szURL);

			if (FindInXIPAndDetach(szBuf, m_content, m_contentSize))
			{
				m_inXIP = true;
				return true;
			}

			if (bSearchAppDir && !DoesFileExist(szBuf))
			{
				_tcscpy(szBuf, TheseusGetAppDir());
				_tcscat(szBuf, szURL);
			}

			delete[] m_url;
			m_url = new TCHAR[_tcslen(szBuf) + 1];
			_tcscpy(m_url, szBuf);
		}

		if (FindInXIPAndDetach(m_url, m_content, m_contentSize))
		{
			m_inXIP = true;
			return true;
		}

		if (!FetchFile(bTry))
			return false;
	}

	return true;
}

bool CActiveFile::FetchFile(bool bTry /*=false*/)
{
	BYTE *pbContent;
	DWORD cbContent;
	MEMORYSTATUS stat;

	HANDLE hFile;

	if ((hFile = TheseusCreateFile(m_url, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	cbContent = GetFileSize(hFile, NULL);

	if (bTry)
	{
		GlobalMemoryStatus(&stat);
		if (stat.dwAvailPhys < (cbContent + 1024 * 1024))
		{
			CloseHandle(hFile);
			return false;
		}
	}

	pbContent = (BYTE *)TheseusAllocMemory(cbContent + sizeof(TCHAR));
	ReadFile(hFile, pbContent, cbContent, &cbContent, NULL);

	pbContent[cbContent] = 0;
#ifdef _UNICODE
	pbContent[cbContent + 1] = 0;
#endif

	if (!GetFileTime(hFile, NULL, NULL, &m_modifiedTime))
	{
		CloseHandle(hFile);
		TheseusFreeMemory(pbContent);
		return false;
	}

	CloseHandle(hFile);

	TheseusFreeMemory(m_content);

	m_content = pbContent;
	m_contentSize = cbContent;
	m_inXIP = false;

	return true;
}

bool CActiveFile::Update()
{
	if (m_inXIP)
		return false;

	if (m_url == NULL || m_url[0] == '\0')
		return false;

	if (m_updatePeriod == 0.0f || TheseusGetNow() < m_nextUpdateTime)
		return false;

	m_nextUpdateTime = TheseusGetNow() + m_updatePeriod + rnd(1.0f);

	if (_tcsnicmp(m_url, _T("string:"), 7) == 0)
		return false;

	{
		HANDLE hFile;
		if ((hFile = TheseusCreateFile(m_url, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE)
		{
			FILETIME ft;
			VERIFY(GetFileTime(hFile, NULL, NULL, &ft));
			CloseHandle(hFile);

			if (ft.dwHighDateTime > m_modifiedTime.dwHighDateTime || (ft.dwHighDateTime == m_modifiedTime.dwHighDateTime && ft.dwLowDateTime > m_modifiedTime.dwLowDateTime))
			{
				m_modifiedTime = ft;
				return FetchFile();
			}
		}
		else
		{
		}
	}

	return false;
}

#ifdef _UNICODE
bool CActiveFile::IsUnicode()
{
	return (m_content != NULL && m_contentSize > 2 && m_content[0] == 0xff && m_content[1] == 0xfe);
}

void CActiveFile::MakeUnicode()
{
	if (IsUnicode())
	{
		// NOTE: Once this happens, IsUnicode will return false!
		MoveMemory(m_content, m_content + 2, m_contentSize);
		return;
	}

	TCHAR *wsz = (TCHAR *)TheseusAllocMemory((m_contentSize + 1) * 2);
	Unicode(wsz, (const char *)m_content, m_contentSize);
	wsz[m_contentSize] = 0;
	TheseusFreeMemory(m_content);
	m_content = (BYTE *)wsz;
	m_contentSize = (m_contentSize + 1) * 2;
	m_inXIP = false;
}
#endif
