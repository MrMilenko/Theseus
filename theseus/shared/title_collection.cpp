// title_collection.cpp: CTitleArray, the title-and-saved-game scanner.
// Walks installed title directories, parses TitleMeta.xbx /
// TitleImage.xbx / SaveMeta.xbx / SaveImage.xbx, supports background
// scanning with critical-section locking. Decompiled from the 5960
// retail XBE; see docs/decomp/TitleCollection.md.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "runner.h"
#include "settingsfile.h"
#include "title_collection.h"
#include "locale_node.h"



CTitleArray g_titles [9];

void TitleArray_Init()
{
	g_titles[8].SetRoot('c', false);
	// NOTE: Memory units will be set as they are mounted...
}

CTitleArray::CTitleArray()
{
    InitializeCriticalSection(&m_rootLock);

	m_root[0] = 0;
	m_root[1] = ':';
	m_root[2] = '\\';
	m_root[3] = 0;

	m_dirty = true;
	m_titleCount = 0;
	m_titleCapacity = 0;
	m_titles = NULL;
}

CTitleArray::~CTitleArray()
{
	DeleteAll(false);
}

void CTitleArray::DeleteAll(bool bUpdate /* = true */)
{
	for (int i = 0; i < m_titleCount; i += 1)
	{
		delete [] m_titles[i].m_id;
		delete [] m_titles[i].m_name;
		delete [] m_titles[i].m_saves;
	}

	delete [] m_titles;
	m_titles = NULL;

	m_titleCount = 0;
	m_titleCapacity = 0;
	m_dirty = true;

    if (bUpdate)
    {
        Update();
    }
}

void CTitleArray::SetRoot(TCHAR chNewRoot, bool bUpdate /* = true */)
{
    EnterCriticalSection(&m_rootLock);
	m_root[0] = chNewRoot;
    LeaveCriticalSection(&m_rootLock);
	DeleteAll(bUpdate);
}

static int __cdecl SortTitleCompare(const void *elem1, const void *elem2)
{
	const CTitle* pTitle1 = (const CTitle*)elem1;
	const CTitle* pTitle2 = (const CTitle*)elem2;
	return _tcsicmp(pTitle1->m_name, pTitle2->m_name);
}

void CTitleArray::AddTitle(const TCHAR* szTitleID)
{
	TCHAR szXbxFile[MAX_PATH];
    TCHAR szTranslate[MAX_TRANSLATE_LEN];
	MakePath(szXbxFile, GetUData(), szTitleID);
	MakePath(szXbxFile, szXbxFile, szTitleDataXBX);

	TCHAR szTitleName[64];

    TCHAR szLanguageCode[MAX_LANGUAGE_CODE_LEN];
    GetLanguageCode(szLanguageCode);

	CSettingsFile settings;
	bool broken = false;
	if (!settings.Open(szXbxFile) || !settings.GetValue(szLanguageCode, _T("TitleName"), szTitleName, countof(szTitleName)))
	{
		_tcscpy(szTitleName, Translate(_T("Broken Game"), szTranslate));
		broken = true;
	}

	if (m_titleCount == m_titleCapacity)
	{
		m_titleCapacity += 50;

		CTitle* rgtitle = new CTitle [m_titleCapacity];
		CopyMemory(rgtitle, m_titles, m_titleCount * sizeof (CTitle));
		delete [] m_titles;
		m_titles = rgtitle;
	}

	int cch = _tcslen(szTitleID) + 1;
	m_titles[m_titleCount].m_id = new TCHAR [cch];
	CopyChars(m_titles[m_titleCount].m_id, szTitleID, cch);

	cch = _tcslen(szTitleName) + 1;
	m_titles[m_titleCount].m_name = new TCHAR [cch];
	CopyChars(m_titles[m_titleCount].m_name, szTitleName, cch);

	m_titles[m_titleCount].m_savedGameCount = -1;
	m_titles[m_titleCount].m_savedGameBlocks = -1; // 'unknown'
	m_titles[m_titleCount].m_totalBlocks = -1; // 'unknown'
	m_titles[m_titleCount].m_saves = NULL;
	m_titles[m_titleCount].m_broken = broken;

	m_titleCount += 1;
}

void CTitleArray::Update()
{
    EnterCriticalSection(&m_rootLock);

	if (m_root[0] == 0)
	{
        LeaveCriticalSection(&m_rootLock);
		m_dirty = false;
		return;
	}

	TCHAR szFileName [MAX_PATH];
	WIN32_FIND_DATA fd;

    MakePath(szFileName, GetUData(), _T("*.*"));
    FSCHAR szBuf [MAX_PATH];
    Ansi(szBuf, szFileName, countof (szBuf));
    HANDLE hFind = FindFirstFile(szBuf, &fd);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        if (ERROR_DEVICE_NOT_CONNECTED != GetLastError())
        {
            m_dirty = false;
        }
        LeaveCriticalSection(&m_rootLock);
        return;
    }

    do
    {
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			continue;

		Unicode(szFileName, fd.cFileName, countof(szFileName));

		// Ignore the Xbox default title...
		if (_tcsicmp(szFileName, _T("fffe0000")) == 0)
			continue;

		AddTitle(szFileName);
    }
    while (FindNextFile(hFind, &fd));

    if (ERROR_DEVICE_NOT_CONNECTED == GetLastError())
    {
        FindClose(hFind);
        DeleteAll(false);
        LeaveCriticalSection(&m_rootLock);
        return;
    }

	FindClose(hFind);

	qsort(m_titles, m_titleCount, sizeof (CTitle), SortTitleCompare);

    // Enumerate saved game count
    for (int i=0; i<m_titleCount; i++)
    {
        int nSavedGameCount = 0;
        CSave* rgsaves = new CSave [MAX_SAVED_GAMES];
        nSavedGameCount =

        m_titles[i].m_savedGameCount = ScanSavedGames(GetUData(), m_titles[i].m_id, rgsaves, NULL);
        m_titles[i].m_saves = new CSave [nSavedGameCount];
        CopyMemory(m_titles[i].m_saves, rgsaves, sizeof (CSave) * nSavedGameCount);
        delete [] rgsaves;
    }

    LeaveCriticalSection(&m_rootLock);
	m_dirty = false;
}

int CTitleArray::GetTitleCount()
{
	if (m_dirty)
        return 0;

	return m_titleCount;
}

int CTitleArray::GetTitleCount2()
{
	if (m_dirty)
        return 0;

	return m_titleCount;
}

bool CTitleArray::IsBroken(int nTitle)
{
	return m_titles[nTitle].m_broken;
}

const TCHAR* CTitleArray::GetTitleID(int nTitle)
{
	const TCHAR* sz = _T("");
	
    if (!m_dirty && nTitle >= 0 && nTitle < m_titleCount)
		sz = m_titles[nTitle].m_id;

	return sz;
}

const TCHAR* CTitleArray::GetTitleName(int nTitle)
{
	const TCHAR* sz = _T("");
	
    if (!m_dirty && nTitle >= 0 && nTitle < m_titleCount)
		sz = m_titles[nTitle].m_name;

	return sz;
}
const TCHAR* CTitleArray::GetTitleName2(int nTitle)
{
	const TCHAR* sz = _T("");
	
    if (!m_dirty && nTitle >= 0 && nTitle < m_titleCount)
		sz = m_titles[nTitle].m_name;

	return sz;
}
int CTitleArray::GetSavedGameCount(int nTitle, HANDLE hCancelEvent /*= NULL*/)
{
	int nSavedGameCount = 0;

	if (!m_dirty && nTitle >= 0 && nTitle < m_titleCount)
	{
		nSavedGameCount = m_titles[nTitle].m_savedGameCount;

		if (nSavedGameCount == -1)
		{
			CSave* rgsaves = new CSave [MAX_SAVED_GAMES];

			nSavedGameCount = ScanSavedGames(GetUData(), m_titles[nTitle].m_id, rgsaves, hCancelEvent);

			m_titles[nTitle].m_savedGameCount = nSavedGameCount;
			m_titles[nTitle].m_saves = new CSave [nSavedGameCount];
			CopyMemory(m_titles[nTitle].m_saves, rgsaves, sizeof (CSave) * nSavedGameCount);

            delete [] rgsaves;
		}
	}

	return nSavedGameCount;
}

bool InitSave(CSave* pSave, const TCHAR* szRoot, const TCHAR* szTitleID, const TCHAR* szSaveDirName, FILETIME saveTime)
{
	ZeroMemory(pSave, sizeof (CSave));

	if (!pSave->SetDirName(szSaveDirName))
	{
		return false;
	}

	pSave->m_fileTime = saveTime;
	pSave->m_flags |= SAVEFLAG_UNKIMAGE;

	return true;
}

int CTitleArray::FindTitle(const TCHAR* szTitleID)
{
	int nTitle;
	for (nTitle = 0; nTitle < m_titleCount; nTitle += 1)
	{
		if (_tcsicmp(szTitleID, m_titles[nTitle].m_id) == 0)
			return nTitle;
	}

	return -1;
}

static int __cdecl SortSaveCompare(const void *elem1, const void *elem2)
{
	// Reversed these to go from newest -> oldest
	const CSave* pSave2 = (const CSave*)elem1;
	const CSave* pSave1 = (const CSave*)elem2;
	return CompareFileTime(&pSave1->m_fileTime, &pSave2->m_fileTime);
}

void CTitleArray::AddSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName, FILETIME saveTime)
{
    if (m_dirty)
        return;

	int nTitle = FindTitle(szTitleID);
	if (nTitle == -1)
	{
		AddTitle(szTitleID);

		m_titles[m_titleCount - 1].m_savedGameCount = 0;

		qsort(m_titles, m_titleCount, sizeof (CTitle), SortTitleCompare);

		// start search over because of sort...
		nTitle = FindTitle(szTitleID);
	}

	CTitle* pTitle = &m_titles[nTitle];

	if (pTitle->m_savedGameCount == -1)
		GetSavedGameCount(nTitle);

	CSave* rgsaves = new CSave [pTitle->m_savedGameCount + 1];
	CopyMemory(rgsaves, pTitle->m_saves, sizeof (CSave) * pTitle->m_savedGameCount);
	delete [] pTitle->m_saves;
	pTitle->m_saves = rgsaves;
	
	if (InitSave(&pTitle->m_saves[pTitle->m_savedGameCount], GetUData(), pTitle->m_id, szDirName, saveTime))
	{
		pTitle->m_savedGameCount += 1;
		qsort(pTitle->m_saves, pTitle->m_savedGameCount, sizeof (CSave), SortSaveCompare);
	}

	pTitle->m_totalBlocks = -1; // recalculate this
	pTitle->m_savedGameBlocks = -1; // recalculate this
}

void CTitleArray::RemoveTitle(int nTitle)
{
	CTitle* pTitle = &m_titles[nTitle];
	delete [] pTitle->m_id;
	delete [] pTitle->m_name;
	delete [] pTitle->m_saves;

	MoveMemory(&m_titles[nTitle], &m_titles[nTitle + 1], sizeof (CTitle) * (m_titleCount - nTitle - 1));
	m_titleCount -= 1;
}

void CTitleArray::RemoveSavedGame(int nTitle, int nSavedGame)
{
	CTitle* pTitle = &m_titles[nTitle];
	MoveMemory(&pTitle->m_saves[nSavedGame], &pTitle->m_saves[nSavedGame + 1], sizeof (CSave) * (pTitle->m_savedGameCount - nSavedGame - 1));
	pTitle->m_savedGameCount -= 1;

	pTitle->m_totalBlocks = -1; // recalculate this
	pTitle->m_savedGameBlocks = -1; // recalculate this
}

void CTitleArray::RemoveSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName)
{
    int nTitle = FindTitle(szTitleID);
    if (nTitle == -1)
    {
        return;
    }

    int i, nSavedGame = -1;
    CTitle* pTitle = &m_titles[nTitle];

    // Search for the matching saved game
    for (int i=0; i<pTitle->m_savedGameCount; i++)
    {
        if (_tcsicmp(pTitle->m_saves[i].m_dirName, szDirName) == 0)
        {
            nSavedGame = i;
            break;
        }
    }

    if (nSavedGame != -1)
    {
        RemoveSavedGame(nTitle, nSavedGame);
    }
}

const TCHAR* CTitleArray::GetSavedGameID(int nTitle, int nSavedGame)
{
	int nSavedGameCount = GetSavedGameCount(nTitle);
	if (nSavedGame < 0 || nSavedGame >= nSavedGameCount)
	{
		return _T("");
	}

	return m_titles[nTitle].m_saves[nSavedGame].GetDirName(); // NOTE: Temporary access!
}

FILETIME CTitleArray::GetSavedGameTime(int nTitle, int nSavedGame)
{
	int nSavedGameCount = GetSavedGameCount(nTitle);
	if (nSavedGame < 0 || nSavedGame >= nSavedGameCount)
	{
		static FILETIME filetime;
		return filetime;
	}

	return m_titles[nTitle].m_saves[nSavedGame].m_fileTime;
}

int CTitleArray::GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent)
{
	if (nTitle < 0 || nTitle >= m_titleCount)
		return 0;

	if (m_titles[nTitle].m_totalBlocks == -1)
	{
		m_titles[nTitle].m_totalBlocks = ComputeTitleTotalBlocks(GetUData(), m_titles[nTitle].m_id, hCancelEvent);
		if (m_root[0] == 'c' && m_titles[nTitle].m_totalBlocks >= 0)
			m_titles[nTitle].m_totalBlocks += ComputeTitleTotalBlocks(GetTData(), m_titles[nTitle].m_id, hCancelEvent);
	}

	return m_titles[nTitle].m_totalBlocks;
}



int ComputeTitleTotalBlocks(const TCHAR* szRoot, const TCHAR* szTitleID, HANDLE hCancelEvent)
{
	TCHAR szDir [MAX_PATH];
	MakePath(szDir, szRoot, szTitleID);
	return GetDirectoryBlocks(szDir, BLOCK_SIZE, true, hCancelEvent);
}

bool InitSave(CSave* pSave, const TCHAR* szRoot, const TCHAR* szTitleID, const TCHAR* szSaveDirName, FILETIME saveTime);

int ScanSavedGames(const TCHAR* szRoot, const TCHAR* szTitleID, CSave* rgsaves /*[MAX_SAVED_GAMES]*/, HANDLE hCancelEvent)
{
    if (hCancelEvent && WaitForSingleObject(hCancelEvent, 0) == WAIT_OBJECT_0)
    {
        return -1;
    }

	int nSavedGameCount = 0;
    bool bCancel = false;

	// scan for saves...
	HANDLE hFind;
	WIN32_FIND_DATA fd;

	{
		TCHAR szTitleDirWild [MAX_PATH];
		MakePath(szTitleDirWild, szRoot, szTitleID);
		MakePath(szTitleDirWild, szTitleDirWild, _T("*.*"));

		char szBuf [MAX_PATH];
		Ansi(szBuf, szTitleDirWild, MAX_PATH);
		hFind = FindFirstFile(szBuf, &fd);
	}

	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
            if (hCancelEvent && WaitForSingleObject(hCancelEvent, 0) == WAIT_OBJECT_0)
            {
                bCancel = true;
                break;
            }

			if (nSavedGameCount == MAX_SAVED_GAMES)
			{
				break;
			}

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				continue;

			TCHAR szFileName [MAX_PATH];
			Unicode(szFileName, fd.cFileName, countof(szFileName));

			if (!InitSave(&rgsaves[nSavedGameCount], szRoot, szTitleID, szFileName, fd.ftLastWriteTime))
				continue;

			nSavedGameCount += 1;
		}
		while (FindNextFile(hFind, &fd));
		FindClose(hFind);
	}

    if (!bCancel)
    {
    	qsort(rgsaves, nSavedGameCount, sizeof (CSave), SortSaveCompare);
    }

	return bCancel ? -1 : nSavedGameCount;
}



const TCHAR* CTitleArray::GetTData() const
{
	if (m_root[0] != 'c')
		return m_root;

	return _T("e:\\tdata");
}

const TCHAR* CTitleArray::GetUData() const
{
	if (m_root[0] != 'c')
		return m_root;

	return _T("e:\\udata");
}

bool CTitleArray::IsValid() const
{
	return m_root[0] != 0;
}

bool CTitleArray::IsDirty() const
{
	return (m_root[0] && m_dirty);
}

void CTitleArray::GetSavedGameImageName(int nTitle, int nSavedGame, TCHAR* szPath/*[MAX_PATH]*/)
{
	if ((m_titles[nTitle].m_saves[nSavedGame].m_flags & SAVEFLAG_UNKIMAGE) != 0)
	{
		m_titles[nTitle].m_saves[nSavedGame].m_flags &= ~SAVEFLAG_UNKIMAGE;

		MakePath(szPath, GetUData(), GetTitleID(nTitle));
		MakePath(szPath, szPath, GetSavedGameID(nTitle, nSavedGame));
		MakePath(szPath, szPath, szSaveImageXBX);

		if (DoesFileExist(szPath))
		{
			m_titles[nTitle].m_saves[nSavedGame].m_flags |= SAVEFLAG_HASIMAGE;
			return;
		}
	}

	MakePath(szPath, GetUData(), GetTitleID(nTitle));

	if ((m_titles[nTitle].m_saves[nSavedGame].m_flags & SAVEFLAG_HASIMAGE) != 0)
	{
		// This save has its own image; use it...
		MakePath(szPath, szPath, GetSavedGameID(nTitle, nSavedGame));
	}

	MakePath(szPath, szPath, szSaveImageXBX);

	if (!DoesFileExist(szPath)) //saved game doesn't have any associated images
	{
		_tcscpy(szPath, _T("xboxlogo64.xbx"));
	}
}

bool CTitleArray::IsPublisherExists(const TCHAR* szPublisherID) const
{
	for (int i=0; i<m_titleCount; i++)
	{
        if (_tcsnicmp(szPublisherID, m_titles[i].m_id, 4) == 0)
        {
            return true;
        }
	}

    return false;
}

bool CSave::SetDirName(const TCHAR* szDirName)
{
	if (_tcslen(szDirName) + 1 > countof (m_dirName))
		return false;

	_tcscpy(m_dirName, szDirName);
	return true;
}

const TCHAR* CSave::GetDirName()
{
	return m_dirName;
}

