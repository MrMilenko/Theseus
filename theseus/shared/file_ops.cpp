// file_ops.cpp: file operations, game copying, and copy-destination
// selection.
//
//   CGameCopier       Threaded game-save copier between storage devices
//   CCopyDestination  Node for selecting the copy destination device
//   CFile / CFolder   File-system browsing nodes
//
// Decompiled from the 5960 retail XBE; see docs/decomp/FileOps.md.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "date_node.h"
#include "runner.h"
#include "title_collection.h"
#include "copy_games.h"

extern const TCHAR* g_szCurTitleImage;

// ===== Constants =====

const TCHAR szTitleDataXBX [] = _T("TitleMeta.xbx");
const TCHAR szTitleImageXBX [] = _T("TitleImage.xbx");
const TCHAR szSaveDataXBX [] = _T("SaveMeta.xbx");
const TCHAR szSaveImageXBX [] = _T("SaveImage.xbx");

// ===== CCopyGame =====

struct CCopyGame
{
	TCHAR* m_titleId;
	TCHAR* m_gameDir;
	FILETIME m_time;
	bool m_needToAdd;
};

// ===== CGameCopier =====

CGameCopier::CGameCopier()
{
	m_thread = NULL;
	m_progress = 0.0f;
	m_internalError = false;
	m_alreadyExists = false;
	m_error = false;
	m_done = false;
	m_totalBlocks = 0;
	m_copiedBlocks = 0;
	m_destRoot = NULL;
	m_srcRoot = NULL;
	m_copyGames = NULL;
	m_copyGameCapacity = 0;
	m_copyGameCount = 0;
	m_buffer = NULL;
	m_srcDevUnit = -1;
	m_destDevUnit = -1;
}

CGameCopier::~CGameCopier()
{
	RemoveAll();
}

void CGameCopier::RemoveAll()
{
	delete [] m_destRoot;
	m_destRoot = NULL;

	delete [] m_srcRoot;
	m_srcRoot = NULL;

	int i;
	for (i = 0; i < m_copyGameCount; i += 1)
	{
		delete [] m_copyGames[i].m_titleId;
		delete [] m_copyGames[i].m_gameDir;
	}

	delete [] m_copyGames;
	m_copyGames = NULL;
	m_copyGameCapacity = 0;
	m_copyGameCount = 0;
	m_totalBlocks = 0;

	delete [] m_buffer;
	m_buffer = NULL;
}

void CGameCopier::Finish()
{
	for (int i = 0; i < m_copyGameCount; i += 1)
	{
		if (m_copyGames[i].m_needToAdd)
		{
			if (m_alreadyExists)
			{
				g_titles[m_destDevUnit].RemoveSavedGame(m_copyGames[i].m_titleId, m_copyGames[i].m_gameDir);
			}

			g_titles[m_destDevUnit].AddSavedGame(m_copyGames[i].m_titleId, m_copyGames[i].m_gameDir, m_copyGames[i].m_time);
			m_copyGames[i].m_needToAdd = false;
		}
	}

	RemoveAll();
}

DWORD CALLBACK CGameCopier::StartThread(LPVOID pvContext)
{
	CGameCopier *pThis = (CGameCopier*)pvContext;
	return pThis->ThreadProc();
}

// Copy a single saved-game record from source device to destination,
// staging the surrounding title directory and metadata files alongside
// the game data. The order matters: the title meta file (TitleMeta.xbx)
// is the very last write so a hot-removed memory card mid-copy leaves
// the destination in a "no game here" state instead of a half-written
// title with no payload.
void CGameCopier::CopyGame(int nCopyGame)
{
	const TCHAR* szTitleID = m_copyGames[nCopyGame].m_titleId;
	const TCHAR* szGameDir = m_copyGames[nCopyGame].m_gameDir;

	TRACE(_T("CopyGame(%s, %s)\n"), szTitleID, szGameDir);
	ASSERT(!m_internalError);

	TCHAR szSrcPath [MAX_PATH];
	TCHAR szDestPath [MAX_PATH];

	// Stage 1: ensure the title's destination directory exists.
	MakePath(szDestPath, m_destRoot, szTitleID);
	bool bNewTitle = CreateDirectory(szDestPath);

	// Stage 2: copy the title image (TitleImage.xbx). Skip if already
	// present and non-empty (a previous copy session staged it).
	MakePath(szDestPath, m_destRoot, szTitleID);
	MakePath(szDestPath, szDestPath, szTitleImageXBX);
	if (bNewTitle || GetFileSize(szDestPath) == 0)
	{
		MakePath(szSrcPath, m_srcRoot, szTitleID);
		MakePath(szSrcPath, szSrcPath, szTitleImageXBX);
		CopyFile(szSrcPath, szDestPath);
	}

	// Stage 3: copy the title's default save image (SaveImage.xbx) if
	// the source has one and the destination doesn't.
	MakePath(szDestPath, m_destRoot, szTitleID);
	MakePath(szDestPath, szDestPath, szSaveImageXBX);
	MakePath(szSrcPath, m_srcRoot, szTitleID);
	MakePath(szSrcPath, szSrcPath, szSaveImageXBX);
	if (DoesFileExist(szSrcPath) && GetFileSize(szDestPath) == 0)
	{
		CopyFile(szSrcPath, szDestPath);
	}

	// Stages 2 and 3 are best-effort. A missing title image isn't a
	// reason to abort the actual save copy. Reset the error flags
	// before the load-bearing work below.
	m_internalError = false;
	m_alreadyExists = false;

	// Stage 4: create (or clear) the saved-game directory and stamp it
	// with the original save's timestamp. ERROR_ALREADY_EXISTS means
	// the user is overwriting an existing save: delete its contents
	// and reuse the directory; any other failure is fatal for this game.
	MakePath(szDestPath, m_destRoot, szTitleID);
	MakePath(szDestPath, szDestPath, szGameDir);
	if (!CreateDirectory(szDestPath))
	{
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			m_alreadyExists = true;
			DeleteDirectory(szDestPath, false);
		}
		else
		{
			m_internalError = true;
			return;
		}
	}

	TRACE(_T("Creating game in %s\n"), szDestPath);

	HANDLE hFile = TheseusCreateFile(szDestPath, GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		FILETIME* pTime = &m_copyGames[nCopyGame].m_time;
		VERIFY(SetFileTime(hFile, pTime, pTime, pTime));
		VERIFY(CloseHandle(hFile));
	}
#ifdef _DEBUG
	else
	{
		TRACE(_T("Cannot open directory (%s) to set times (%d)\n"), szDestPath, GetLastError());
	}
#endif

	// Stage 5: copy the actual save game file tree. If this fails the
	// destination directory we just created is partially populated --
	// roll it back, plus the parent title directory if we created it
	// fresh in stage 1.
	MakePath(szSrcPath, m_srcRoot, szTitleID);
	MakePath(szSrcPath, szSrcPath, szGameDir);
	if (!CopyDirectory(szSrcPath, szDestPath))
	{
		TRACE(_T("\001The copy didn't complete; removing what we did copy...\n"));
		if (bNewTitle)
		{
			TRACE(_T("it was a new title; deleting: %s %s\n"), m_destRoot, szTitleID);
			MakePath(szDestPath, m_destRoot, szTitleID);
		}
		DeleteDirectory(szDestPath);
		return;
	}

	// Stage 6: capture the destination's actual basename in case the
	// filesystem renamed it during creation (FATX truncation, etc) and
	// keep the in-memory copy table in sync with what's on disk.
	{
		TCHAR* pch = _tcsrchr(szDestPath, '\\');
		ASSERT(pch != NULL);
		pch += 1;

		delete [] m_copyGames[nCopyGame].m_gameDir;
		m_copyGames[nCopyGame].m_gameDir = new TCHAR [_tcslen(pch) + 1];
		_tcscpy(m_copyGames[nCopyGame].m_gameDir, pch);
	}

	// Stage 7 (last on purpose): copy the title's meta file. If a memory
	// card is pulled mid-copy *before* this write, the destination is
	// left looking like "no save here" rather than a half-installed
	// title we can't clean up.
	MakePath(szDestPath, m_destRoot, szTitleID);
	MakePath(szDestPath, szDestPath, szTitleDataXBX);
	if (bNewTitle || GetFileSize(szDestPath) == 0)
	{
		MakePath(szSrcPath, m_srcRoot, szTitleID);
		MakePath(szSrcPath, szSrcPath, szTitleDataXBX);
		CopyFile(szSrcPath, szDestPath);
	}
}

bool CGameCopier::CreateDirectory(const TCHAR* szDir)
{
	TRACE(_T("CreateDirectory(%s)\n"), szDir);

	char szDir2 [MAX_PATH];
	Ansi(szDir2, szDir, MAX_PATH);

	return ::CreateDirectory(szDir2, NULL) != FALSE;
}

bool CGameCopier::CopyDirectory(const TCHAR* szSrcPath, const TCHAR* szDestPath)
{
	TRACE(_T("CopyDirectory(%s, %s)\n"), szSrcPath, szDestPath);

	CreateDirectory(szDestPath);

	TCHAR szBuf [MAX_PATH];
	MakePath(szBuf, szSrcPath, _T("*.*"));

	WIN32_FIND_DATA fd;
	char szBufX [MAX_PATH];
	Ansi(szBufX, szBuf, MAX_PATH);
	HANDLE h = FindFirstFile(szBufX, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			TCHAR szFileName [MAX_PATH];
			Unicode(szFileName, fd.cFileName, MAX_PATH);

			if (_tcscmp(szFileName, _T(".")) == 0 || _tcscmp(szFileName, _T("..")) == 0)
				continue;

			MakePath(szBuf, szSrcPath, szFileName);

			TCHAR szBuf2 [MAX_PATH];
			MakePath(szBuf2, szDestPath, szFileName);

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				if (!CopyDirectory(szBuf, szBuf2))
				{
					TRACE(_T("CopyDirectory(%s, %s) failed\n"), szBuf, szBuf2);
					FindClose(h);
					m_internalError = true;
					return false;
				}
			}
			else
			{
				if (!CopyFile(szBuf, szBuf2))
				{
					TRACE(_T("CopyFile(%s, %s) failed\n"), szBuf, szBuf2);
					FindClose(h);
					m_internalError = true;
					return false;
				}
			}
		}
		while (FindNextFile(h, &fd));
		FindClose(h);
	}

	return true;
}

bool CGameCopier::CopyFile(const TCHAR* szSrcFile, const TCHAR* szDestFile)
{
	TRACE(_T("CopyFile(%s, %s)\n"), szSrcFile, szDestFile);

	HANDLE hSrcFile = TheseusCreateFile(szSrcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hSrcFile == INVALID_HANDLE_VALUE)
	{
		TRACE(_T("OpenFile(%s) failed\n"), szSrcFile);
		m_internalError = true;
		return false;
	}

	HANDLE hDestFile = TheseusCreateFile(szDestFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (hDestFile == INVALID_HANDLE_VALUE)
	{
		CloseHandle(hSrcFile);
		TRACE(_T("CreateFile(%s) failed\n"), szDestFile);
		m_internalError = true;
		return false;
	}

	for (;;)
	{
		DWORD dwRead, dwWrite;

		if (!ReadFile(hSrcFile, m_buffer, BLOCK_SIZE, &dwRead, NULL))
		{
			TRACE(_T("ReadFile(%s) failed (%d)\n"), szSrcFile, GetLastError());
			m_internalError = true;
			break;
		}

		if (dwRead == 0)
			break;

		if (!WriteFile(hDestFile, m_buffer, dwRead, &dwWrite, NULL))
		{
			TRACE(_T("WriteFile(%s) failed (%d)\n"), szDestFile, GetLastError());
			m_internalError = true;
			break;
		}

		m_copiedBlocks += 1;
		m_progress = (float)m_copiedBlocks / (float)m_totalBlocks;
		TRACE(_T("copy progress: %f block %d of %d\n"), m_progress, m_copiedBlocks, m_totalBlocks);
	}

	if (!m_internalError)
	{
		FILETIME create, access, write;
		GetFileTime(hSrcFile, &create, &access, &write);
		SetFileTime(hDestFile, &create, &access, &write);
	}

	CloseHandle(hSrcFile);

	if (!CloseHandle(hDestFile))
	{
		TRACE(_T("CloseHandle(%s) failed\n"), szDestFile);
		m_internalError = true;
	}

	return !m_internalError;
}

bool CGameCopier::DeleteFile(const TCHAR* szFile)
{
	char szFileA [MAX_PATH];
	Ansi(szFileA, szFile, MAX_PATH);
	SetFileAttributes(szFileA, FILE_ATTRIBUTE_NORMAL);
#ifdef _XBOX
	return ::DeleteFile(szFileA) != FALSE;
#else
	return DeleteFile(szFileA) != FALSE;
#endif
}

bool CGameCopier::DeleteDirectory(const TCHAR* szDir, bool RemoveSelf /*= true*/)
{
	TCHAR szBuf [MAX_PATH];
	MakePath(szBuf, szDir, _T("*.*"));

	WIN32_FIND_DATA fd;
	char szBufX [MAX_PATH];
	Ansi(szBufX, szBuf, MAX_PATH);
	HANDLE h = FindFirstFile(szBufX, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			TCHAR szFileName [MAX_PATH];
			Unicode(szFileName, fd.cFileName, MAX_PATH);

			if (_tcscmp(szFileName, _T(".")) == 0 || _tcscmp(szFileName, _T("..")) == 0)
				continue;

			MakePath(szBuf, szDir, szFileName);

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				if (!DeleteDirectory(szBuf))
				{
					FindClose(h);
					m_internalError = true;
					return false;
				}
			}
			else
			{
				if (!DeleteFile(szBuf))
				{
					FindClose(h);
					m_internalError = true;
					return false;
				}
			}
		}
		while (FindNextFile(h, &fd));
		FindClose(h);
	}

	if (!RemoveSelf)
	{
		return true;
	}

	char szDirA [MAX_PATH];
	Ansi(szDirA, szDir, MAX_PATH);
	return ::RemoveDirectory(szDirA) != FALSE;
}

DWORD CGameCopier::ThreadProc()
{
	ASSERT(m_buffer != NULL);

	while (m_copyGameCur < m_copyGameCount)
	{
		CopyGame(m_copyGameCur);
		if (m_internalError)
			break;

		m_copyGames[m_copyGameCur].m_needToAdd = true;
		m_copyGameCur += 1;
	}

	m_done = true;

	delete [] m_buffer;
	m_buffer = NULL;

	m_thread = NULL;
	m_error = m_internalError;

	TRACE(_T("CGameCopier::ThreadProc terminating; error=%d\n"), m_error);

	return 1;
}

void CGameCopier::SetSource(int nDevUnit)
{
	m_srcDevUnit = nDevUnit;

	const TCHAR* szRoot = g_titles[nDevUnit].GetUData();

	TRACE(_T("SetSource(%s)\n"), szRoot);
	delete [] m_srcRoot;
	m_srcRoot = new TCHAR [_tcslen(szRoot) + 1];
	_tcscpy(m_srcRoot, szRoot);

	ASSERT(m_totalBlocks == 0);
	ASSERT(m_copyGameCount == 0);
}

void CGameCopier::SetDestination(int nDevUnit)
{
	m_destDevUnit = nDevUnit;

	const TCHAR* szRoot = g_titles[nDevUnit].GetUData();

	TRACE(_T("SetDestination(%s)\n"), szRoot);
	delete [] m_destRoot;
	m_destRoot = new TCHAR [_tcslen(szRoot) + 1];
	_tcscpy(m_destRoot, szRoot);

	ASSERT(m_totalBlocks == 0);
	ASSERT(m_copyGameCount == 0);
}

void CGameCopier::AddGame(const TCHAR* szTitleID, const TCHAR* szGameDir, FILETIME saveTime, int nBlocks)
{
	TRACE(_T("CGameCopier::AddGame: %s %s %d blocks\n"), szTitleID, szGameDir, nBlocks);

	if (m_copyGameCount == m_copyGameCapacity)
	{
		m_copyGameCapacity += 10;
		CCopyGame* rgCopyGame = new CCopyGame [m_copyGameCapacity];
		CopyMemory(rgCopyGame, m_copyGames, m_copyGameCount * sizeof (CCopyGame));
		delete [] m_copyGames;
		m_copyGames = rgCopyGame;
	}

	m_copyGames[m_copyGameCount].m_titleId = new TCHAR [_tcslen(szTitleID) + 1];
	_tcscpy(m_copyGames[m_copyGameCount].m_titleId, szTitleID);

	m_copyGames[m_copyGameCount].m_gameDir = new TCHAR [_tcslen(szGameDir) + 1];
	_tcscpy(m_copyGames[m_copyGameCount].m_gameDir, szGameDir);

	m_copyGames[m_copyGameCount].m_time = saveTime;

	m_copyGames[m_copyGameCount].m_needToAdd = false;

	m_copyGameCount += 1;

	m_totalBlocks += nBlocks;
}

void CGameCopier::Start()
{
	ASSERT(m_srcDevUnit >= 0 && m_srcDevUnit <= 8);
	ASSERT(m_destDevUnit >= 0 && m_destDevUnit <= 8);
	ASSERT(m_srcDevUnit != m_destDevUnit);

	TRACE(_T("Start game copy: %d blocks\n"), m_totalBlocks);

	m_progress = 0.0f;
	m_internalError = false;
	m_error = false;
	m_done = false;
	m_copiedBlocks = 0;
	m_copyGameCur = 0;

	if (m_buffer == NULL)
		m_buffer = new BYTE [BLOCK_SIZE];

	m_thread = CreateThread(NULL, 0, StartThread, this, 0, 0);

	if (m_thread)
	{
		CloseHandle(m_thread);
	}
	else
	{
		StartThread(this);
	}
}

// ===== CCopyDestination =====

class CCopyDestination : public CNode
{
	DECLARE_NODE(CCopyDestination, CNode)
public:
	CCopyDestination();
	~CCopyDestination();

	void Render();
	void Advance(float nSeconds);
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	CNode* m_pod;
	CNode* m_podIcon;
	CNode* m_panelMU;
	CNode* m_panelMUHilite;
	CNode* m_panelText;
	CNode* m_panelTextHilite;
	CNode* m_console;
	CNode* m_memoryUnit;

	int m_curDevUnit;
	int m_selDevUnit;
	int m_sourceDevUnit;
	float m_spacing;
	bool m_isActive;
	int m_select;

	float m_nScroll;
	int m_nScrollTo;
	XTIME m_nScrollTime;

	void selectUp();
	void selectDown();

	int m_rgDevUnit [9];
	int m_nDevUnitCount;

	DECLARE_NODE_PROPS();
	DECLARE_NODE_FUNCTIONS();

protected:

	const TCHAR* GetTitleID2(int nTitle);
	CStrObject* GetTitleID(int nTitle);
};

IMPLEMENT_NODE("CopyDestination", CCopyDestination, CNode)

START_NODE_PROPS(CCopyDestination, CNode)
	NODE_PROP(pt_node, CCopyDestination, pod)
	NODE_PROP(pt_node, CCopyDestination, panelMU)
	NODE_PROP(pt_node, CCopyDestination, panelMUHilite)
	NODE_PROP(pt_node, CCopyDestination, panelText)
	NODE_PROP(pt_node, CCopyDestination, panelTextHilite)
	NODE_PROP(pt_node, CCopyDestination, console)
	NODE_PROP(pt_node, CCopyDestination, memoryUnit)
	NODE_PROP(pt_integer, CCopyDestination, curDevUnit)
	NODE_PROP(pt_integer, CCopyDestination, selDevUnit)
	NODE_PROP(pt_integer, CCopyDestination, sourceDevUnit)
	NODE_PROP(pt_number, CCopyDestination, spacing)
	NODE_PROP(pt_boolean, CCopyDestination, isActive)
	NODE_PROP(pt_integer, CCopyDestination, select)
END_NODE_PROPS()

#define _FND_CLASS CCopyDestination
START_NODE_FUN(CCopyDestination, CNode)
	NODE_FUN_VV(selectUp)
	NODE_FUN_VV(selectDown)
END_NODE_FUN()
#undef _FND_CLASS


CCopyDestination::CCopyDestination() :
	m_pod(NULL),
	m_panelMU(NULL),
	m_panelMUHilite(NULL),
	m_panelText(NULL),
	m_panelTextHilite(NULL),
	m_console(NULL),
	m_memoryUnit(NULL),
	m_curDevUnit(-1),
	m_selDevUnit(-1),
	m_sourceDevUnit(-1),
	m_spacing(0.4f),
	m_isActive(false),
	m_select(-1)
{
	m_nScroll = 0.0f;
	m_nScrollTo = 0;
	m_nScrollTime = 0.0f;
	m_nDevUnitCount = 0;
}

CCopyDestination::~CCopyDestination()
{
	if (m_pod != NULL)
		m_pod->Release();

	if (m_panelMU != NULL)
		m_panelMU->Release();

	if (m_panelMUHilite != NULL)
		m_panelMUHilite->Release();

	if (m_panelText != NULL)
		m_panelText->Release();

	if (m_panelTextHilite != NULL)
		m_panelTextHilite->Release();

	if (m_console != NULL)
		m_console->Release();

	if (m_memoryUnit != NULL)
		m_memoryUnit->Release();
}

void CCopyDestination::Render()
{
	if (m_pod == NULL)
		return;

	float y = m_nScroll;
	int i;
	for (i = 0; i < m_nDevUnitCount; i += 1)
	{
		D3DXMATRIX mat;
		D3DXMatrixTranslation(&mat, 0.0f, y, 0.0f);
		TheseusPushWorld();
		TheseusMultWorld(&mat);
		TheseusUpdateWorld();
		m_pod->Render();

		// update the text rendered under each MU
		m_selDevUnit = m_rgDevUnit[i];
		CallFunction(this, _T("UpdateMemUnitText"));

		if (m_rgDevUnit[i] == m_curDevUnit)
		{
			if(m_selDevUnit == 8)
			{
				m_panelMUHilite->Render();
				m_panelTextHilite->Render();
				m_console->Render();
			}
			else
			{
				m_panelMUHilite->Render();
				m_panelTextHilite->Render();
				m_memoryUnit->Render();
			}
		}
		else
		{
			if(m_selDevUnit == 8)
			{
				m_panelMU->Render();
				m_panelText->Render();
				m_console->Render();
			}
			else
			{
				m_panelMU->Render();
				m_panelText->Render();
				m_memoryUnit->Render();
			}
		}

		TheseusPopWorld();

		y -= m_spacing;
	}
}

void CCopyDestination::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	bool fCurDevUnitRemoved = false;

	if (!m_isActive)
		return;

	int nDevUnitIndex = -1;

	// Look for memory units...
	m_nDevUnitCount = 0;
	for (int i = 0; i < 9; i += 1)
	{
		int devUnit = (i == 0) ? 8 : (i - 1);

		// Ignore the source unit
		if (devUnit == m_sourceDevUnit)
			continue;

		// Ignore missing units
		if (devUnit != 8 && !g_titles[devUnit].IsValid())
		{
			if(devUnit == m_curDevUnit)
			{
				fCurDevUnitRemoved = true;
			}
			continue;
		}

		m_rgDevUnit[m_nDevUnitCount] = devUnit;

		if (devUnit == m_curDevUnit)
			nDevUnitIndex = m_nDevUnitCount;

		m_nDevUnitCount += 1;
	}

	if (nDevUnitIndex != -1 && m_select != nDevUnitIndex)
	{
		// The current unit switched positions; scroll immediately
		m_select = nDevUnitIndex;
		m_nScrollTo = m_select;
		m_nScroll = m_spacing * m_select;
	}

	if (m_select < 0)
		m_select = 0;
	if (m_select > m_nDevUnitCount - 1)
		m_select = m_nDevUnitCount - 1;

	if (m_nScrollTo != m_select)
	{
		m_nScrollTo = m_select;
		m_nScrollTime = TheseusGetNow();
	}
	else if (m_nScrollTime != 0.0f)
	{
		float t = (float) (TheseusGetNow() - m_nScrollTime) / 0.25f;
		if (t >= 1.0f)
		{
			m_nScrollTime = 0.0f;
			t = 1.0f;
		}

		float t1 = 1.0f - t;
		m_nScroll = t1 * m_nScroll + t * (m_spacing * m_nScrollTo);
	}

	int curDevUnit = -1;
	if (m_select != -1)
		curDevUnit = m_rgDevUnit[m_select];

	if (curDevUnit != m_curDevUnit)
	{
		m_curDevUnit = curDevUnit;

		if(fCurDevUnitRemoved)
		{
			CallFunction(this, _T("OnDestinationUnitRemoved"));
		}
		CallFunction(this, _T("OnCurDevUnitChange"));
	}
}

bool CCopyDestination::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	return CNode::OnSetProperty(pprd, pvValue);
}

void CCopyDestination::selectUp()
{
	if (m_nDevUnitCount < 2)
		return;

	int nSel = m_select - 1;
	if (nSel >= 0 && nSel < m_nDevUnitCount)
	{
		m_select = nSel;
		m_curDevUnit = m_rgDevUnit[nSel];
		CallFunction(this, _T("OnCurDevUnitChange"));
	}
}

void CCopyDestination::selectDown()
{
	if (m_nDevUnitCount < 2)
		return;

	int nSel = m_select + 1;
	if (nSel >= 0 && nSel < m_nDevUnitCount)
	{
		m_select = nSel;
		m_curDevUnit = m_rgDevUnit[nSel];
		CallFunction(this, _T("OnCurDevUnitChange"));
	}
}

// ===== CFile =====

class CFile : public CNode
{
	DECLARE_NODE(CFile, CNode)
public:
	CFile();
	~CFile();

	TCHAR* m_name;
	TCHAR* m_type;
	TCHAR* m_path;
	int m_length;
	CDateObject* m_date;

	CStrObject* readText();

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("File", CFile, CNode)

START_NODE_PROPS(CFile, CNode)
	NODE_PROP(pt_string, CFile, name)
	NODE_PROP(pt_string, CFile, type)
	NODE_PROP(pt_string, CFile, path)
	NODE_PROP(pt_integer, CFile, length)
	NODE_PROP(pt_node, CFile, date)
END_NODE_PROPS()

#define _FND_CLASS CFile
START_NODE_FUN(CFile, CNode)
	NODE_FUN_SV(readText)
END_NODE_FUN()
#undef _FND_CLASS

CFile::CFile() :
	m_name(NULL),
	m_type(NULL),
	m_path(NULL),
	m_length(-1),
	m_date(NULL)
{
}

CFile::~CFile()
{
	delete [] m_name;
	delete [] m_type;
	delete [] m_path;
	delete m_date;
}

CStrObject* CFile::readText()
{
	CStrObject* pStr = new CStrObject;

	if (m_path != NULL)
	{
		CActiveFile file;
		if (file.Fetch(m_path))
		{
			pStr->Append((TCHAR*)file.GetContent());
		}
	}

	return pStr;
}

// ===== CFolder =====

class CFolder : public CNode
{
	DECLARE_NODE(CFolder, CNode)
public:
	CFolder();
	~CFolder();

	TCHAR* m_path;
	TCHAR* m_name;
	CNodeArray m_files;
	CNodeArray m_subFolders;

	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	void Refresh(const TCHAR* szPath);

	void sortByName();
	void sortByType();
	void sortByDate();
	void sortByLength();

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("Folder", CFolder, CNode)

START_NODE_PROPS(CFolder, CNode)
	NODE_PROP(pt_string, CFolder, path)
	NODE_PROP(pt_string, CFolder, name)
	NODE_PROP(pt_nodearray, CFolder, files)
	NODE_PROP(pt_nodearray, CFolder, subFolders)
END_NODE_PROPS()

#define _FND_CLASS CFolder
START_NODE_FUN(CFolder, CNode)
	NODE_FUN_VV(sortByName)
	NODE_FUN_VV(sortByType)
	NODE_FUN_VV(sortByDate)
	NODE_FUN_VV(sortByLength)
END_NODE_FUN()
#undef _FND_CLASS

CFolder::CFolder() :
	m_path(NULL),
	m_name(NULL)
{
}

CFolder::~CFolder()
{
	delete [] m_path;
	delete [] m_name;
}

bool CFolder::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_path))
	{
		const TCHAR* szPath = *((const TCHAR**)pvValue);
		Refresh(szPath);
	}

	return true;
}

void CFolder::Refresh(const TCHAR* szPath)
{
	TCHAR szWild [MAX_PATH];
	WIN32_FIND_DATA fd;
	HANDLE h;

	lstrcpyn(szWild, szPath, MAX_PATH - sizeof("\\*.*"));
	_tcscat(szWild, _T("\\*.*"));

	char szWildA [MAX_PATH];
	Ansi(szWildA, szWild, MAX_PATH);
	h = FindFirstFile(szWildA, &fd);

	if (h == INVALID_HANDLE_VALUE)
	{
		TRACE(_T("\001Cannot read directory: \"%s\" (%d)\n"), szPath, GetLastError());
		return;
	}

	// Cache the trailing path component as the folder's display name.
	const TCHAR* pchName = _tcsrchr(szPath, '\\');
	pchName = (pchName == NULL) ? _T("") : pchName + 1;

	delete [] m_name;
	m_name = new TCHAR [_tcslen(pchName) + 1];
	_tcscpy(m_name, pchName);


	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)
			continue;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
			continue;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY)
			continue;

		TCHAR szFileName [MAX_PATH];
		Unicode(szFileName, fd.cFileName, countof(szFileName));

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			CFolder* pFolder = new CFolder;
			m_subFolders.AddNode(pFolder);

			pFolder->m_path = new TCHAR [_tcslen(szPath) + 1 + _tcslen(szFileName) + 1];
			_stprintf(pFolder->m_path, _T("%s\\%s"), szPath, szFileName);

			pFolder->m_name = new TCHAR [_tcslen(szFileName) + 1];
			_tcscpy(pFolder->m_name, szFileName);
		}
		else
		{
			CFile* pFile = new CFile;
			m_files.AddNode(pFile);

			pFile->m_date = new CDateObject;
			pFile->m_date->m_time = fd.ftLastWriteTime;

			const TCHAR* pch = _tcsrchr(szFileName, '.');

			int cchName = _tcslen(szFileName);
			if (pch != NULL)
				cchName -= _tcslen(pch);
			pFile->m_name = new TCHAR [cchName + 1];
			CopyChars(pFile->m_name, szFileName, cchName);
			pFile->m_name[cchName] = 0;

			if (pch != NULL)
				pch += 1;
			else
				pch = _T("");
			pFile->m_type = new TCHAR [_tcslen(pch) + 1];
			_tcscpy(pFile->m_type, pch);
			_tcslwr(pFile->m_type);

			pFile->m_path = new TCHAR [_tcslen(szPath) + 1 + _tcslen(szFileName) + 1];
			_stprintf(pFile->m_path, _T("%s\\%s"), szPath, szFileName);

			pFile->m_length = fd.nFileSizeLow;
		}
	}
	while (FindNextFile(h, &fd));

	FindClose(h);
}

static int __cdecl CmpName(const void* p1, const void* p2)
{
	const CFile* pFile1 = *(CFile**)p1;
	const CFile* pFile2 = *(CFile**)p2;

	return _tcsicmp(pFile1->m_name, pFile2->m_name);
}

void CFolder::sortByName()
{
	m_files.Sort(&CmpName);
}

static int __cdecl CmpType(const void* p1, const void* p2)
{
	const CFile* pFile1 = *(CFile**)p1;
	const CFile* pFile2 = *(CFile**)p2;

	return _tcsicmp(pFile1->m_type, pFile2->m_type);
}

void CFolder::sortByType()
{
	m_files.Sort(CmpType);
}

static int __cdecl CmpDate(const void* p1, const void* p2)
{
	const CFile* pFile1 = *(CFile**)p1;
	const CFile* pFile2 = *(CFile**)p2;

	return CompareFileTime(&pFile1->m_date->m_time, &pFile2->m_date->m_time);
}

void CFolder::sortByDate()
{
	m_files.Sort(CmpDate);
}

static int __cdecl CmpLength(const void* p1, const void* p2)
{
	const CFile* pFile1 = *(CFile**)p1;
	const CFile* pFile2 = *(CFile**)p2;

	return pFile1->m_length - pFile2->m_length;
}

void CFolder::sortByLength()
{
	m_files.Sort(CmpLength);
}
