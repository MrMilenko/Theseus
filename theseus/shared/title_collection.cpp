// title_collection.cpp: CTitleArray, the list of title folders and their
// saves on one device (U:\ style root on a memory unit, e:\udata on the
// hard drive). Rebuilt function by function from the 5960 retail XBE
// (0x41dd0..0x44240); see docs/decomp/TitleCollection.md. Theseus keeps a
// slimmer CTitle than 5960 (no Live account or content lists; those live in
// the save grid) and reads the hard drive from e:\ instead of c:\.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "runner.h"
#include "settingsfile.h"
#include "title_collection.h"
#include "locale_node.h"

CTitleArray g_titles [9];

// TitleArray_Init()
// 5960 @ 0x41dd0 is the array constructor run over all nine slots at
// startup; the hard drive slot then gets its root. Memory units get theirs
// from the memory monitor as they're mounted.
void TitleArray_Init()
{
	g_titles[Dev0].SetRoot('c', false);
}

// CTitleArray::CTitleArray
// Reconstructed from 5960 retail XBE @ 0x41dd0:
//     InitializeCriticalSection(&lock); root = {0, ':', '\\', 0};
//     count = capacity = list = 0; dirty = 1;
CTitleArray::CTitleArray()
{
	InitializeCriticalSection(&m_lock);
	m_root[0] = 0;
	m_root[1] = ':';
	m_root[2] = '\\';
	m_root[3] = 0;
	m_dirty = true;
	m_count = 0;
	m_capacity = 0;
	m_list = NULL;
}

CTitleArray::~CTitleArray()
{
	DeleteAll(false);
}

// CTitleArray::DeleteAll(bUpdate)
// Reconstructed from 5960 retail XBE @ 0x41e40:
//     for each title: free(id); free(name); delete[] saves;
//     free(list); list = count = capacity = 0; dirty = 1; if (bUpdate) Update();
void CTitleArray::DeleteAll(bool bUpdate)
{
	for (CTitle* t = m_list; t < m_list + m_count; t++)
	{
		delete [] t->m_titleID;
		delete [] t->m_displayName;
		delete [] t->m_saveList;
	}
	delete [] m_list;
	m_list = NULL;
	m_count = 0;
	m_capacity = 0;
	m_dirty = true;

	if (bUpdate)
		Update();
}

// CTitleArray::SetRoot(drive, bUpdate)
// Inlined into its callers in 5960: swap the drive letter under the lock
// (the scan thread reads it), then drop the list.
void CTitleArray::SetRoot(TCHAR chNewRoot, bool bUpdate)
{
	EnterCriticalSection(&m_lock);
	m_root[0] = chNewRoot;
	LeaveCriticalSection(&m_lock);
	DeleteAll(bUpdate);
}

// Titles sort by display name, ignoring case.
static int __cdecl CompareTitleNames(const void* a, const void* b)
{
	return _tcsicmp(((const CTitle*)a)->m_displayName, ((const CTitle*)b)->m_displayName);
}

// Saves sort newest first.
static int __cdecl CompareSaveTimes(const void* a, const void* b)
{
	return CompareFileTime(&((const CSave*)b)->m_written, &((const CSave*)a)->m_written);
}

static TCHAR* DupString(const TCHAR* s)
{
	int n = (int)_tcslen(s) + 1;
	TCHAR* copy = new TCHAR [n];
	CopyChars(copy, s, n);
	return copy;
}

// CTitleArray::AddTitle(id)
// Reconstructed from 5960 retail XBE @ 0x41f20. Reads TitleName out of
// <udata>\<id>\TitleMeta.xbx (64 chars); if the file or the key is missing
// the title shows as the translated "Broken Game" and is flagged broken.
// The list grows 50 entries at a time. Counts start at -1 (not scanned).
// Theseus reads the name from the current language's section.
void CTitleArray::AddTitle(const TCHAR* szTitleID)
{
	TCHAR metaPath[MAX_PATH];
	MakePath(metaPath, GetUData(), szTitleID);
	MakePath(metaPath, metaPath, szTitleDataXBX);

	TCHAR lang[MAX_LANGUAGE_CODE_LEN];
	GetLanguageCode(lang);

	TCHAR name[64];
	bool broken = false;
	CSettingsFile meta;
	if (!meta.Open(metaPath) || !meta.GetValue(lang, _T("TitleName"), name, countof(name)))
	{
		TCHAR translated[MAX_TRANSLATE_LEN];
		_tcscpy(name, Translate(_T("Broken Game"), translated));
		broken = true;
	}

	if (m_count == m_capacity)
	{
		m_capacity += 50;
		CTitle* grown = new CTitle [m_capacity];
		CopyMemory(grown, m_list, m_count * sizeof (CTitle));
		delete [] m_list;
		m_list = grown;
	}

	CTitle& t = m_list[m_count++];
	t.m_titleID = DupString(szTitleID);
	t.m_displayName = DupString(name);
	t.m_saveCount = -1;
	t.m_saveList = NULL;
	t.m_saveBlocks = -1;
	t.m_blocks = -1;
	t.m_broken = broken;
}

// Scans one title's saves into a fresh array sized to fit.
static CSave* ScanSavesToFit(const TCHAR* root, const TCHAR* titleID, HANDLE cancel, int* count)
{
	CSave* scratch = new CSave [MAX_SAVED_GAMES];
	*count = ScanSavedGames(root, titleID, scratch, cancel);
	if (*count < 0)
	{
		// Cancelled: stays unscanned (-1) with no list.
		delete [] scratch;
		return NULL;
	}
	CSave* saves = new CSave [*count];
	CopyMemory(saves, scratch, *count * sizeof (CSave));
	delete [] scratch;
	return saves;
}

// CTitleArray::Update()
// Reconstructed from 5960 retail XBE @ 0x42230. With no root it just marks
// itself clean. Otherwise it lists <udata>\*.* and adds every folder as a
// title. A failed listing marks it clean unless the device is gone
// (ERROR_DEVICE_NOT_CONNECTED, 0x48f), so it's tried again; losing the
// device mid listing drops everything. Then the titles sort by name and
// each one's saves are scanned. All of it runs under the root lock.
// 5960 also loads Live accounts and content lists here; Theseus doesn't,
// and skips the fffe0000 dashboard folder.
void CTitleArray::Update()
{
	EnterCriticalSection(&m_lock);

	if (m_root[0] == 0)
	{
		LeaveCriticalSection(&m_lock);
		m_dirty = false;
		return;
	}

	TCHAR path[MAX_PATH];
	MakePath(path, GetUData(), _T("*.*"));
	FSCHAR pattern[MAX_PATH];
	Ansi(pattern, path, countof(pattern));

	WIN32_FIND_DATA found;
	HANDLE find = TheseusFindFirstFile(pattern, &found);
	if (find == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() != ERROR_DEVICE_NOT_CONNECTED)
			m_dirty = false;
		LeaveCriticalSection(&m_lock);
		return;
	}

	do
	{
		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			continue;
		Unicode(path, found.cFileName, countof(path));
		if (_tcsicmp(path, _T("fffe0000")) != 0)
			AddTitle(path);
	}
	while (TheseusFindNextFile(find, &found));

	bool unplugged = GetLastError() == ERROR_DEVICE_NOT_CONNECTED;
	TheseusFindClose(find);
	if (unplugged)
	{
		DeleteAll(false);
		LeaveCriticalSection(&m_lock);
		return;
	}

	qsort(m_list, m_count, sizeof (CTitle), CompareTitleNames);

	for (CTitle* t = m_list; t < m_list + m_count; t++)
		t->m_saveList = ScanSavesToFit(GetUData(), t->m_titleID, NULL, &t->m_saveCount);

	LeaveCriticalSection(&m_lock);
	m_dirty = false;
}

// CTitleArray::GetTitleCount / GetTitleCount2
// Nothing to show while the list is stale.
int CTitleArray::GetTitleCount()
{
	return m_dirty ? 0 : m_count;
}

int CTitleArray::GetTitleCount2()
{
	return m_dirty ? 0 : m_count;
}

bool CTitleArray::IsBroken(int nTitle)
{
	return m_list[nTitle].m_broken;
}

// GetTitleID / GetTitleName
// 5960 inlines this test everywhere: "" unless the list is current and
// the index is in range.
const TCHAR* CTitleArray::GetTitleID(int nTitle)
{
	if (m_dirty || nTitle < 0 || nTitle >= m_count)
		return _T("");
	return m_list[nTitle].m_titleID;
}

const TCHAR* CTitleArray::GetTitleName(int nTitle)
{
	if (m_dirty || nTitle < 0 || nTitle >= m_count)
		return _T("");
	return m_list[nTitle].m_displayName;
}

const TCHAR* CTitleArray::GetTitleName2(int nTitle)
{
	return GetTitleName(nTitle);
}

// CTitleArray::GetSavedGameCount(title, cancel)
// Reconstructed from 5960 retail XBE @ 0x42740:
//     if (dirty || n out of range) return 0;
//     if (title.saveCount == -1) { scan into a 4096 entry scratch array,
//         store the count, copy into an array of exactly that size }
//     return title.saveCount;
int CTitleArray::GetSavedGameCount(int nTitle, HANDLE hCancelEvent)
{
	if (m_dirty || nTitle < 0 || nTitle >= m_count)
		return 0;

	CTitle& t = m_list[nTitle];
	if (t.m_saveCount == -1)
		t.m_saveList = ScanSavesToFit(GetUData(), t.m_titleID, hCancelEvent, &t.m_saveCount);
	return t.m_saveCount < 0 ? 0 : t.m_saveCount;
}

// Fills one save record; false when the folder name won't fit (16 chars
// with the terminator). The image is unknown until someone asks for it.
static bool InitSave(CSave* pSave, const TCHAR* szDirName, FILETIME written)
{
	ZeroMemory(pSave, sizeof (CSave));
	if (!pSave->SetDirName(szDirName))
		return false;
	pSave->m_written = written;
	pSave->m_flags |= SAVEFLAG_UNKIMAGE;
	return true;
}

// CTitleArray::FindTitle(id)
// Reconstructed from 5960 retail XBE @ 0x42c20: first title whose id
// matches ignoring case, else -1.
int CTitleArray::FindTitle(const TCHAR* szTitleID)
{
	for (int i = 0; i < m_count; i++)
	{
		if (_tcsicmp(szTitleID, m_list[i].m_titleID) == 0)
			return i;
	}
	return -1;
}

// CTitleArray::AddSavedGame(id, folder, time)
// Reconstructed from 5960 retail XBE @ 0x42cb0. Ignored while stale. A
// title not in the list yet is added with zero saves and the list resorted.
// The save list grows by one; if the folder name fits it's appended and
// the saves resorted newest first. Either way the title's sizes are reset
// so they get counted again.
void CTitleArray::AddSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName, FILETIME saveTime)
{
	if (m_dirty)
		return;

	int n = FindTitle(szTitleID);
	if (n == -1)
	{
		AddTitle(szTitleID);
		m_list[m_count - 1].m_saveCount = 0;
		qsort(m_list, m_count, sizeof (CTitle), CompareTitleNames);
		n = FindTitle(szTitleID);
	}

	CTitle& t = m_list[n];
	if (t.m_saveCount == -1)
		GetSavedGameCount(n);

	CSave* grown = new CSave [t.m_saveCount + 1];
	CopyMemory(grown, t.m_saveList, t.m_saveCount * sizeof (CSave));
	delete [] t.m_saveList;
	t.m_saveList = grown;

	if (InitSave(&t.m_saveList[t.m_saveCount], szDirName, saveTime))
	{
		t.m_saveCount++;
		qsort(t.m_saveList, t.m_saveCount, sizeof (CSave), CompareSaveTimes);
	}

	t.m_blocks = -1;
	t.m_saveBlocks = -1;
}

// CTitleArray::RemoveTitle(n)
// Reconstructed from 5960 retail XBE @ 0x42e40: free the title's strings
// and saves, close the gap, one fewer title.
void CTitleArray::RemoveTitle(int nTitle)
{
	CTitle& t = m_list[nTitle];
	delete [] t.m_titleID;
	delete [] t.m_displayName;
	delete [] t.m_saveList;
	MoveMemory(&m_list[nTitle], &m_list[nTitle + 1], (m_count - nTitle - 1) * sizeof (CTitle));
	m_count--;
}

// CTitleArray::RemoveSavedGame(n, save)
// Reconstructed from 5960 retail XBE @ 0x42ef0: close the gap, one fewer
// save, sizes reset to -1.
void CTitleArray::RemoveSavedGame(int nTitle, int nSavedGame)
{
	CTitle& t = m_list[nTitle];
	MoveMemory(&t.m_saveList[nSavedGame], &t.m_saveList[nSavedGame + 1], (t.m_saveCount - nSavedGame - 1) * sizeof (CSave));
	t.m_saveCount--;
	t.m_blocks = -1;
	t.m_saveBlocks = -1;
}

// CTitleArray::RemoveSavedGame(id, folder)
// Reconstructed from 5960 retail XBE @ 0x42f40: find the title, then the
// save by folder name ignoring case, and remove it if both are there.
void CTitleArray::RemoveSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName)
{
	int n = FindTitle(szTitleID);
	if (n == -1)
		return;

	CTitle& t = m_list[n];
	for (int i = 0; i < t.m_saveCount; i++)
	{
		if (_tcsicmp(t.m_saveList[i].m_folder, szDirName) == 0)
		{
			RemoveSavedGame(n, i);
			return;
		}
	}
}

// GetSavedGameID / GetSavedGameTime
// Both scan the title first if needed; out of range gives "" or a zero time.
const TCHAR* CTitleArray::GetSavedGameID(int nTitle, int nSavedGame)
{
	if (nSavedGame < 0 || nSavedGame >= GetSavedGameCount(nTitle))
		return _T("");
	return m_list[nTitle].m_saveList[nSavedGame].GetDirName();
}

FILETIME CTitleArray::GetSavedGameTime(int nTitle, int nSavedGame)
{
	if (nSavedGame < 0 || nSavedGame >= GetSavedGameCount(nTitle))
	{
		FILETIME never = { 0, 0 };
		return never;
	}
	return m_list[nTitle].m_saveList[nSavedGame].m_written;
}

static bool CancelSignaled(HANDLE cancel)
{
	return cancel != NULL && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
}

// CTitleArray::GetTitleTotalBlocks(n, cancel)
// Reconstructed from 5960 retail XBE @ 0x43390. Counted once and cached:
// the title's udata folder, plus its tdata folder on the hard drive.
// A cancel during the count leaves it unknown.
int CTitleArray::GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent)
{
	if (nTitle < 0 || nTitle >= m_count)
		return 0;

	CTitle& t = m_list[nTitle];
	if (t.m_blocks == -1)
	{
		t.m_blocks = ComputeTitleTotalBlocks(GetUData(), t.m_titleID, hCancelEvent);
		if (m_root[0] == 'c' && t.m_blocks >= 0)
			t.m_blocks += ComputeTitleTotalBlocks(GetTData(), t.m_titleID, hCancelEvent);
	}
	// A count cut short by cancel is taken again next time.
	if (CancelSignaled(hCancelEvent))
		t.m_blocks = -1;
	return t.m_blocks;
}

// ComputeTitleTotalBlocks(root, id, cancel)
// Reconstructed from 5960 retail XBE @ 0x43460: <root>\<id> counted in
// 16 KB blocks, subfolders included.
int ComputeTitleTotalBlocks(const TCHAR* szRoot, const TCHAR* szTitleID, HANDLE hCancelEvent)
{
	TCHAR dir[MAX_PATH];
	MakePath(dir, szRoot, szTitleID);
	return GetDirectoryBlocks(dir, BLOCK_SIZE, true, hCancelEvent);
}

// ScanSavedGames(root, id, out, cancel)
// Reconstructed from 5960 retail XBE @ 0x434f0. Lists <root>\<id>\*.* and
// records every folder whose name fits, up to 4096, with its last write
// time and the image marked unknown. -1 if cancel is signaled before or
// during the walk; otherwise the saves are sorted newest first.
int ScanSavedGames(const TCHAR* szRoot, const TCHAR* szTitleID, CSave* rgsaves, HANDLE hCancelEvent)
{
	if (CancelSignaled(hCancelEvent))
		return -1;

	TCHAR path[MAX_PATH];
	MakePath(path, szRoot, szTitleID);
	MakePath(path, path, _T("*.*"));
	char pattern[MAX_PATH];
	Ansi(pattern, path, MAX_PATH);

	int count = 0;
	WIN32_FIND_DATA found;
	HANDLE find = TheseusFindFirstFile(pattern, &found);
	if (find != INVALID_HANDLE_VALUE)
	{
		bool cancelled = false;
		do
		{
			if (CancelSignaled(hCancelEvent))
			{
				cancelled = true;
				break;
			}
			if (count == MAX_SAVED_GAMES)
				break;
			if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				continue;

			Unicode(path, found.cFileName, countof(path));
			if (InitSave(&rgsaves[count], path, found.ftLastWriteTime))
				count++;
		}
		while (TheseusFindNextFile(find, &found));
		TheseusFindClose(find);

		if (cancelled)
			return -1;
	}

	qsort(rgsaves, count, sizeof (CSave), CompareSaveTimes);
	return count;
}

// CTitleArray::GetTData / GetUData
// Reconstructed from 5960 retail XBE @ 0x43c30: a memory unit's root is
// its drive; the hard drive keeps title data and user data in two folders.
// 5960 uses c:\tdata and c:\udata, Theseus the e:\ ones.
const TCHAR* CTitleArray::GetTData() const
{
	return m_root[0] == 'c' ? _T("e:\\tdata") : m_root;
}

const TCHAR* CTitleArray::GetUData() const
{
	return m_root[0] == 'c' ? _T("e:\\udata") : m_root;
}

bool CTitleArray::IsValid() const
{
	return m_root[0] != 0;
}

// CTitleArray::IsDirty
// Reconstructed from 5960 retail XBE @ 0x43c50: mounted and stale.
bool CTitleArray::IsDirty() const
{
	return m_root[0] != 0 && m_dirty;
}

// CTitleArray::GetSavedGameImageName(n, save, out)
// Reconstructed from 5960 retail XBE @ 0x43d00. The first time a save is
// asked about, look for <udata>\<id>\<save>\SaveImage.xbx and remember
// whether it's there. Saves without their own image use the title's
// <udata>\<id>\SaveImage.xbx, and when that's missing too the Xbox logo.
void CTitleArray::GetSavedGameImageName(int nTitle, int nSavedGame, TCHAR* szPath)
{
	DWORD& flags = m_list[nTitle].m_saveList[nSavedGame].m_flags;

	if (flags & SAVEFLAG_UNKIMAGE)
	{
		flags &= ~SAVEFLAG_UNKIMAGE;
		MakePath(szPath, GetUData(), GetTitleID(nTitle));
		MakePath(szPath, szPath, GetSavedGameID(nTitle, nSavedGame));
		MakePath(szPath, szPath, szSaveImageXBX);
		if (DoesFileExist(szPath))
		{
			flags |= SAVEFLAG_HASIMAGE;
			return;
		}
	}

	MakePath(szPath, GetUData(), GetTitleID(nTitle));
	if (flags & SAVEFLAG_HASIMAGE)
		MakePath(szPath, szPath, GetSavedGameID(nTitle, nSavedGame));
	MakePath(szPath, szPath, szSaveImageXBX);

	if (!DoesFileExist(szPath))
		_tcscpy(szPath, _T("xboxlogo64.xbx"));
}

// CTitleArray::IsPublisherExists(publisher)
// Reconstructed from 5960 retail XBE @ 0x44240: any title id starting with
// the same four hex digits, ignoring case.
bool CTitleArray::IsPublisherExists(const TCHAR* szPublisherID) const
{
	for (int i = 0; i < m_count; i++)
	{
		if (_tcsnicmp(szPublisherID, m_list[i].m_titleID, 4) == 0)
			return true;
	}
	return false;
}

// CSave::SetDirName / GetDirName
// 5960 inlines this: the name plus its terminator must fit in 16 chars.
bool CSave::SetDirName(const TCHAR* szDirName)
{
	if (_tcslen(szDirName) + 1 > countof(m_folder))
		return false;
	_tcscpy(m_folder, szDirName);
	return true;
}

const TCHAR* CSave::GetDirName()
{
	return m_folder;
}
