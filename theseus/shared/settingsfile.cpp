// settingsfile.cpp: CSettingsFile, the dashboard's INI parser. Reads
// .xbx and other key=value config files (UTF-16 with optional BOM),
// preserves section / key ordering for round-trip writes. Decompiled
// from the 5960 retail XBE; see docs/decomp/Settings.md.

#include "std.h"
#include "theseus.h"
#include "settingsfile.h"
#include "activefile.h"
#include "xap_compile.h"
// Get Program Path - No .xbe
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
	// Desktop: config files are in xboxfs/Q/
	strcpy(szBuffer, "Q:");
#endif
}
CSettingsFile::CSettingsFile()
{
	m_filePath = NULL;
	m_sections = NULL;
	m_dirty = false;
#ifdef _UNICODE
	m_unicode = false;
#endif
}

CSettingsFile::~CSettingsFile()
{
	Close();
}


bool CSettingsFile::OpenDir(const TCHAR* szDir)
{
	TCHAR szFile [MAX_PATH];
	_stprintf(szFile, _T("%s\\Xbox.xbx"), szDir);
	return Open(szFile);
}

bool CSettingsFile::Open(const TCHAR* szFile)
{
	CSettingsFileSection* pSection = NULL;

	m_filePath = new TCHAR [_tcslen(szFile) + 1];
	_tcscpy(m_filePath, szFile);

	CActiveFile file;
	if (!file.Fetch(m_filePath, false, true))
		return false;

#ifdef _UNICODE
	m_unicode = file.IsUnicode();
	file.MakeUnicode();
#endif

	const TCHAR* pch = (const TCHAR*)file.GetContent();
	while (*pch != 0)
	{
		pch = SkipWhite(pch, none);

		if (*pch == '[')
		{
			pch += 1;

			TCHAR szSectionName [256];
			TCHAR* pchName = szSectionName;

			while (*pch != 0 && *pch != '\r' && *pch != '\n' && *pch != ']')
			{
				if (pchName >= szSectionName + countof(szSectionName) - 1)
				{
						Close();
					return false;
				}
				*pchName++ = *pch++;
			}
			*pchName = 0;

			if (*pch == ']')
			{
				pch += 1;
				pSection = FindSection(szSectionName, true);
			}
		}
		else
		{
			TCHAR szName [256];
			TCHAR* pchName = szName;
			TCHAR szValue [1024];
			TCHAR* pchValue = szValue;

			while (*pch != '\0' && *pch != '\r' && *pch != '\n' && *pch != '=')
			{
				if (pchName >= szName + countof(szName) - 1)
				{
					Close();
					return false;
				}
				*pchName++ = *pch++;
			}
			*pchName = 0;

			if (*pch == '=')
			{
				pch += 1;

				while (*pch != '\0' && *pch != '\r' && *pch != '\n')
				{
					if (pchValue >= szValue + countof(szValue) - 1)
					{
						Close();
						return false;
					}
					*pchValue++ = *pch++;
				}
				*pchValue = 0;

				if (pSection == NULL)
					pSection = FindSection(_T("default"), true);

				pSection->SetValue(szName, szValue);
			}
		}
	}

	return true;
}

void CSettingsFile::Cancel()
{
	m_dirty = false;
	Close();
}

bool CSettingsFile::Save()
{
	if (!m_dirty)
		return true;

#ifdef _UNICODE
	if (m_unicode)
	{
		// Write out Unicode file...

		FILE* pFile = _tfopen(m_filePath, _T("w"));
		if (pFile == NULL)
			return false;

		CSettingsFileSection* pSection;
		for (pSection = m_sections; pSection != NULL; pSection = pSection->m_next)
		{
			_ftprintf(pFile, _T("[%s]\n"), pSection->m_name);

			for (CSettingsFileValue* pValue = pSection->m_values; pValue != NULL; pValue = pValue->m_next)
			{
				_ftprintf(pFile, _T("%s=%s\n"), pValue->m_name, pValue->m_value);
			}
		}
	
		fclose(pFile);
	}
	else
#endif // _UNICODE
	{
		// Convert our Unicode stuff to ANSI on the way out...

		HANDLE hFile = TheseusCreateFile(m_filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			return false;

		DWORD dw;
		CSettingsFileSection* pSection;
		for (pSection = m_sections; pSection != NULL; pSection = pSection->m_next)
		{
			char szBuf [1030];
			char* pch = szBuf;
			TCHAR* pwch = pSection->m_name;
			*pch++ = '[';
			while (*pwch != 0)
				*pch++ = (char)*pwch++;
			*pch++ = ']';
			*pch++ = '\r';
			*pch++ = '\n';
			WriteFile(hFile, szBuf, (DWORD)(pch - szBuf), &dw, NULL);

			for (CSettingsFileValue* pValue = pSection->m_values; pValue != NULL; pValue = pValue->m_next)
			{
				pch = szBuf;
				pwch = pValue->m_name;
				while (*pwch != 0)
					*pch++ = (char)*pwch++;
				*pch++ = '=';
				WriteFile(hFile, szBuf, (DWORD)(pch - szBuf), &dw, NULL);

				pch = szBuf;
				pwch = pValue->m_value;
				while (*pwch != 0)
					*pch++ = (char)*pwch++;
				*pch++ = '\r';
				*pch++ = '\n';
				WriteFile(hFile, szBuf, (DWORD)(pch - szBuf), &dw, NULL);
			}
		}

		CloseHandle(hFile);
	}

	m_dirty = false;

	return true;
}

bool CSettingsFile::Close()
{
	if (!Save())
		return false;

	CSettingsFileSection* pNextSection;
	CSettingsFileSection* pSection;
	for (pSection = m_sections; pSection != NULL; pSection = pNextSection)
	{
		pNextSection = pSection->m_next;
		delete pSection;
	}

	m_sections = NULL;

	delete [] m_filePath;
	m_filePath = NULL;

	return true;
}

CSettingsFileValue* CSettingsFileSection::FindValue(const TCHAR* szName)
{
	for (CSettingsFileValue* pValue = m_values; pValue != NULL; pValue = pValue->m_next)
	{
		if (_tcscmp(szName, pValue->m_name) == 0)
			return pValue;
	}

	return NULL;
}

CSettingsFileSection* CSettingsFile::FindSection(const TCHAR* szSection, bool bCreate/*=false*/)
{
	CSettingsFileSection* pSection;
	for (pSection = m_sections; pSection != NULL; pSection = pSection->m_next)
	{
		if (_tcscmp(szSection, pSection->m_name) == 0)
			return pSection;
	}

	if (bCreate)
	{
		pSection = new CSettingsFileSection;
		pSection->m_name = new TCHAR [_tcslen(szSection) + 1];
		_tcscpy(pSection->m_name, szSection);
		pSection->m_next = m_sections;
		m_sections = pSection;
		return pSection;
	}

	return NULL;
}

bool CSettingsFile::GetValue(const TCHAR* szSection, const TCHAR* szName, TCHAR* szValueBuf, int cchValueBuf)
{
	szValueBuf[0] = 0;

	if (szSection == NULL)
		szSection = _T("default");

	CSettingsFileSection* pSection = FindSection(szSection);
	if (pSection == NULL)
	{
		// Also try default...
		pSection = FindSection(_T("default"));
		if (pSection == NULL)
			return false;
	}

	CSettingsFileValue* pValue = pSection->FindValue(szName);
	if (pValue == NULL)
		return false;

	_tcsncpy(szValueBuf, pValue->m_value, cchValueBuf);
	szValueBuf[cchValueBuf - 1] = 0;

	return true;
}


void CSettingsFile::SetValue(const TCHAR* szSection, const TCHAR* szName, const TCHAR* szValue)
{
	if (szSection == NULL)
		szSection = _T("default");

	CSettingsFileSection* pSection = FindSection(szSection, true);

	if (pSection->SetValue(szName, szValue))
	{
		m_dirty = true;
		Save();
	}
}

bool CSettingsFileSection::SetValue(const TCHAR* szName, const TCHAR* szValue)
{
	CSettingsFileValue* pValue = FindValue(szName);
	if (pValue == NULL)
	{
		pValue = new CSettingsFileValue;
		pValue->m_name = new TCHAR [_tcslen(szName) + 1];
		_tcscpy(pValue->m_name, szName);

		pValue->m_next = m_values;
		m_values = pValue;
	}

	if (pValue->m_value != NULL && _tcscmp(pValue->m_value, szValue) == 0)
		return false;

	delete [] pValue->m_value;
	pValue->m_value = new TCHAR [_tcslen(szValue) + 1];
	_tcscpy(pValue->m_value, szValue);

	return true;
}

CSettingsFileSection::CSettingsFileSection()
{
	m_name = NULL;
	m_values = NULL;
}

CSettingsFileSection::~CSettingsFileSection()
{
	CSettingsFileValue* pNextValue;
	for (CSettingsFileValue* pValue = m_values; pValue != NULL; pValue = pNextValue)
	{
		pNextValue = pValue->m_next;
		delete pValue;
	}

	delete [] m_name;
}

CSettingsFileValue::CSettingsFileValue()
{
	m_name = NULL;
	m_value = NULL;
}

CSettingsFileValue::~CSettingsFileValue()
{
	delete [] m_name;
	delete [] m_value;
}
