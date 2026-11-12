// settingsfile.cpp: CSettingsFile, the dashboard's ini reader and writer.
// [section] headers and name=value lines, kept in memory as linked lists,
// read through CActiveFile (so UTF-16 or ANSI) and written back whenever a
// value changes. Rebuilt function by function from the 5960 retail XBE
// (0x3bc00..0x3c260); see docs/decomp/Settings.md. The save path, the
// batching and the larger value buffer are Theseus's.

#include "std.h"
#include "theseus.h"
#include "settingsfile.h"
#include "activefile.h"
#include "xap_compile.h"

// Theseus: the folder the running XBE came from, without the file name.
void XI_GetProgramPath( char* szBuffer )
{
#ifdef _XBOX
	// Xbox: extract directory from XBE path via the kernel's XeImageFileName
	// ANSI string (e.g. "Q:\default.xbe" -> "Q:")
	char *p;
	PANSI_STRING pTemp;
	pTemp = (PANSI_STRING)XeImageFileName;
	sprintf( szBuffer, "%s", pTemp->Buffer );
	szBuffer[ pTemp->Length ] = '\0';
	if( (p = strrchr( szBuffer, '\\' )) != NULL )
		*p = '\0';
#else
	// Desktop: config files are in Data/
	strcpy(szBuffer, "Q:");
#endif
}
CSettingsFile::CSettingsFile()
{
	m_filePath = NULL;
	m_sections = NULL;
	m_dirty = false;
	m_batching = false;
#ifdef _UNICODE
	m_unicode = false;
#endif
}

CSettingsFile::~CSettingsFile()
{
	Close();
}

// Theseus: opens <dir>\Xbox.xbx.
bool CSettingsFile::OpenDir(const TCHAR* szDir)
{
	TCHAR path[MAX_PATH];
	_stprintf(path, _T("%s\\Xbox.xbx"), szDir);
	return Open(path);
}

// Copies from src into out up to the first null, CR, LF or stop character.
// False when out (cap characters with the null) would overflow.
static bool CopyField(const TCHAR*& src, TCHAR* out, int cap, TCHAR stop)
{
	TCHAR* end = out + cap - 1;
	while (*src != 0 && *src != '\r' && *src != '\n' && *src != stop)
	{
		if (out >= end)
			return false;
		*out++ = *src++;
	}
	*out = 0;
	return true;
}

// CSettingsFile::Open(path)
// Reconstructed from 5960 retail XBE @ 0x0003bc00. Keeps a copy of the
// path, fetches the file with CActiveFile and widens it, then reads it line
// by line after SkipWhite: "[name]" makes or finds that section (a header
// without its ']' is ignored), "name=value" goes into the current section,
// "default" when none came first, and anything else is skipped. A section
// or name past 255 characters, or a value past the buffer, closes the file
// and fails. 5960's value buffer is 1024; Theseus's is 16384 since
// cache.ini lines run long.
bool CSettingsFile::Open(const TCHAR* szFile)
{
	m_filePath = new TCHAR[_tcslen(szFile) + 1];
	_tcscpy(m_filePath, szFile);

	CActiveFile file;
	if (!file.Fetch(m_filePath, false, true))
		return false;
#ifdef _UNICODE
	m_unicode = file.IsUnicode();
	file.MakeUnicode();
#endif

	CSettingsFileSection* section = NULL;
	const TCHAR* p = (const TCHAR*)file.GetContent();
	while (*p != 0)
	{
		p = SkipWhite(p, none);
		if (*p == '[')
		{
			p++;
			TCHAR name[256];
			if (!CopyField(p, name, countof(name), ']'))
			{
				Close();
				return false;
			}
			if (*p == ']')
			{
				p++;
				section = FindSection(name, true);
			}
			continue;
		}

		TCHAR name[256];
		if (!CopyField(p, name, countof(name), '='))
		{
			Close();
			return false;
		}
		if (*p != '=')
			continue;
		p++;

		TCHAR value[16384];
		if (!CopyField(p, value, countof(value), 0))
		{
			Close();
			return false;
		}
		if (section == NULL)
			section = FindSection(_T("default"), true);
		section->SetValue(name, value);
	}
	return true;
}

void CSettingsFile::Cancel()
{
	m_dirty = false;
	Close();
}

// Writes <file>.new and swaps it in, so a crash or power cut mid-save leaves
// the old file whole. Only when .new can't be created does it write in place.
bool CSettingsFile::Save()
{
	if (!m_dirty)
		return true;

	int result = -1;
	if (_tcslen(m_filePath) + 5 <= MAX_PATH)
	{
		TCHAR szNew [MAX_PATH];
		_stprintf(szNew, _T("%s.new"), m_filePath);
		result = WriteTo(szNew);
		if (result > 0 && !TheseusReplaceFile(szNew, m_filePath))
			result = 0;
		if (result == 0)
		{
			FSCHAR sszNew [MAX_PATH];
			CleanFilePath(sszNew, szNew);
			TheseusDeleteFile(sszNew);
		}
	}
	if (result < 0)
		result = WriteTo(m_filePath);
	if (result <= 0)
		return false;

	m_dirty = false;

	return true;
}

// 1 when the whole file went out, 0 when a write failed, -1 when the file
// couldn't be created.
int CSettingsFile::WriteTo(const TCHAR* szPath)
{
#ifdef _UNICODE
	if (m_unicode)
	{
		// Write out Unicode file...

		FILE* pFile = TheseusFOpen(szPath, _T("w"));
		if (pFile == NULL)
			return -1;

		CSettingsFileSection* pSection;
		for (pSection = m_sections; pSection != NULL; pSection = pSection->m_next)
		{
			_ftprintf(pFile, _T("[%s]\n"), pSection->m_name);

			for (CSettingsFileValue* pValue = pSection->m_values; pValue != NULL; pValue = pValue->m_next)
			{
				_ftprintf(pFile, _T("%s=%s\n"), pValue->m_name, pValue->m_value);
			}
		}
	
		bool ok = fflush(pFile) == 0 && !ferror(pFile);
		return (fclose(pFile) == 0 && ok) ? 1 : 0;
	}
	else
#endif // _UNICODE
	{
		// Convert our Unicode stuff to ANSI on the way out...

		HANDLE hFile = TheseusCreateFile(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			return -1;

		DWORD dw;
		bool ok = true;
		CSettingsFileSection* pSection;
		// Stream wide -> ANSI to disk in chunks. The old code reused a
		// 1030-byte stack buffer for the whole "value\r\n" line, which
		// blew up on cache.ini segments (25 joined titles can easily
		// exceed 1KB) and smashed the stack.
		char szBuf [1024];
		auto FlushBuf = [&](char*& pch) {
			if (pch > szBuf) {
				if (!WriteFile(hFile, szBuf, (DWORD)(pch - szBuf), &dw, NULL) || dw != (DWORD)(pch - szBuf))
					ok = false;
				pch = szBuf;
			}
		};
		auto AppendWide = [&](char*& pch, const TCHAR* pwch) {
			while (*pwch != 0) {
				if (pch >= szBuf + sizeof(szBuf)) FlushBuf(pch);
				*pch++ = (char)*pwch++;
			}
		};
		auto AppendChar = [&](char*& pch, char c) {
			if (pch >= szBuf + sizeof(szBuf)) FlushBuf(pch);
			*pch++ = c;
		};
		for (pSection = m_sections; pSection != NULL; pSection = pSection->m_next)
		{
			char* pch = szBuf;
			AppendChar(pch, '[');
			AppendWide(pch, pSection->m_name);
			AppendChar(pch, ']');
			AppendChar(pch, '\r');
			AppendChar(pch, '\n');
			FlushBuf(pch);

			for (CSettingsFileValue* pValue = pSection->m_values; pValue != NULL; pValue = pValue->m_next)
			{
				AppendWide(pch, pValue->m_name);
				AppendChar(pch, '=');
				AppendWide(pch, pValue->m_value);
				AppendChar(pch, '\r');
				AppendChar(pch, '\n');
				FlushBuf(pch);
			}
		}

		if (!FlushFileBuffers(hFile))
			ok = false;
		CloseHandle(hFile);
		return ok ? 1 : 0;
	}
}

// CSettingsFile::Close()
// Reconstructed from 5960 retail XBE @ 0x0003c100: saves (and stays open
// if that fails), then frees every section and the path.
bool CSettingsFile::Close()
{
	if (!Save())
		return false;

	while (m_sections != NULL)
	{
		CSettingsFileSection* next = m_sections->m_next;
		delete m_sections;
		m_sections = next;
	}
	delete[] m_filePath;
	m_filePath = NULL;
	return true;
}

CSettingsFileValue* CSettingsFileSection::FindValue(const TCHAR* szName)
{
	CSettingsFileValue* value = m_values;
	while (value != NULL && _tcscmp(szName, value->m_name) != 0)
		value = value->m_next;
	return value;
}

static TCHAR* CopyString(const TCHAR* sz)
{
	TCHAR* copy = new TCHAR[_tcslen(sz) + 1];
	_tcscpy(copy, sz);
	return copy;
}

// CSettingsFile::FindSection(name, create)
// Reconstructed from 5960 retail XBE @ 0x0003c150: a linear search by
// name; with create set a missing section is made and pushed on the front.
CSettingsFileSection* CSettingsFile::FindSection(const TCHAR* szSection, bool bCreate/*=false*/)
{
	for (CSettingsFileSection* section = m_sections; section != NULL; section = section->m_next)
	{
		if (_tcscmp(szSection, section->m_name) == 0)
			return section;
	}
	if (!bCreate)
		return NULL;

	CSettingsFileSection* made = new CSettingsFileSection;
	made->m_name = CopyString(szSection);
	made->m_next = m_sections;
	m_sections = made;
	return made;
}

// CSettingsFile::GetValue(section, name, out, size)
// Reconstructed from 5960 retail XBE @ 0x0003c1e0. Clears out, looks in
// the section (NULL means "default"), falling back to "default" when the
// section doesn't exist, and copies at most size - 1 characters.
bool CSettingsFile::GetValue(const TCHAR* szSection, const TCHAR* szName, TCHAR* szValueBuf, int cchValueBuf)
{
	szValueBuf[0] = 0;

	CSettingsFileSection* section = FindSection(szSection != NULL ? szSection : _T("default"));
	if (section == NULL)
		section = FindSection(_T("default"));
	if (section == NULL)
		return false;

	CSettingsFileValue* value = section->FindValue(szName);
	if (value == NULL)
		return false;
	_tcsncpy(szValueBuf, value->m_value, cchValueBuf);
	szValueBuf[cchValueBuf - 1] = 0;
	return true;
}

// Sets a value, creating the section, and saves right away unless a batch
// is open. NULL section means "default".
void CSettingsFile::SetValue(const TCHAR* szSection, const TCHAR* szName, const TCHAR* szValue)
{
	CSettingsFileSection* section = FindSection(szSection != NULL ? szSection : _T("default"), true);
	if (!section->SetValue(szName, szValue))
		return;
	m_dirty = true;
	if (!m_batching)
		Save();
}

// CSettingsFileSection::SetValue(name, value)
// Reconstructed from 5960 retail XBE @ 0x0003c260. Finds the name or pushes
// a new entry on the front, then false if the value is already that,
// otherwise replaces it and returns true.
bool CSettingsFileSection::SetValue(const TCHAR* szName, const TCHAR* szValue)
{
	CSettingsFileValue* value = FindValue(szName);
	if (value == NULL)
	{
		value = new CSettingsFileValue;
		value->m_name = CopyString(szName);
		value->m_next = m_values;
		m_values = value;
	}
	if (value->m_value != NULL && _tcscmp(value->m_value, szValue) == 0)
		return false;

	delete[] value->m_value;
	value->m_value = CopyString(szValue);
	return true;
}

CSettingsFileSection::CSettingsFileSection()
{
	m_next = NULL;
	m_name = NULL;
	m_values = NULL;
}

CSettingsFileSection::~CSettingsFileSection()
{
	while (m_values != NULL)
	{
		CSettingsFileValue* next = m_values->m_next;
		delete m_values;
		m_values = next;
	}
	delete[] m_name;
}

CSettingsFileValue::CSettingsFileValue()
{
	m_next = NULL;
	m_name = NULL;
	m_value = NULL;
}

CSettingsFileValue::~CSettingsFileValue()
{
	delete[] m_name;
	delete[] m_value;
}
