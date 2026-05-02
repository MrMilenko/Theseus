// savegame_grid.cpp: CSavedGameGrid, the Memory panel's grid of saved-
// game tiles. Walks the title collection, paginates entries, resolves
// per-title icons, and dispatches selection / copy / delete actions.
// Decompiled from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "runner.h"
#include "asset_loader.h"
#include "locale_node.h"
#include "settingsfile.h"
#include "title_collection.h"
#include "copy_games.h"

#define VISIBLE_ICON_ROWS 3

extern void MakePath(TCHAR *szBuf, const TCHAR *szDir, const TCHAR *szFile);
extern void FlushTextureCache();

extern int GetSoundtrackCount();
extern const TCHAR *GetSoundtrackName(int nSoundtrack);
extern int GetSoundtrackSize(int nSoundtrack, HANDLE hCancelEvent = NULL);
extern void DeleteSoundtrack(int nSoundtrack);
extern void DeleteAllSoundtracks();

extern void FormatDeviceName(int devUnit, TCHAR *szBuf);

extern CNode *GetTextNode(const TCHAR *szText, float nWidth);
extern float g_nEffectAlpha;
extern XTIME g_pulseStartTime;
extern const TCHAR *g_szCurTitleImage;
extern const TCHAR *g_szSelTitleImage;
TCHAR szSelectionBuf[MAX_PATH];

CGameCopier theGameCopier;

class CSavedGameGrid : public CNode
{
	DECLARE_NODE(CSavedGameGrid, CNode)
public:
	CSavedGameGrid();
	~CSavedGameGrid();

	void Render();
	void Advance(float nSeconds);
	bool OnSetProperty(const PRD *pprd, const void *pvValue);
	bool m_isCurGridItemDLC;
	CNode *m_pod;
	CNode *m_podRing;
	CNode *m_podSavePanel;
	// Xbox Live Accounts Panel
	CNode *m_podXboxLiveAccountsPanel;
	CNode *m_XboxLiveAccountsIconPanel;
	CNode *m_iconBumpRing;
	CNode *m_iconArrow;
	CNode *m_iconCheck;
	int m_curGridItem;

	// 5960 selection-state queries (defined out-of-line below).
	int IsXboxLiveAccountSelected();
	int IsGameTitleSelected();
	int DoesUserContentExist();

	int GetDLContentCount(int nTitle);

	CNode *m_podSoundtrackPanel;
	CNode *m_podHilite;
	CNode *m_MUheader;
	CNode *m_MUhiliteHeader;
	CNode *m_firstMURow;
	CNode *m_header;
	CNode *m_hiliteHeader;
	CNode *m_firstRow;
	CNode *m_secondRow;
	CNode *m_otherRow;
	CNode *m_smallIcon;
	CNode *m_SavedIconPanel;
	CNode *m_SoundtrackIconPanel;
	CNode *m_iconRing;
	CNode *m_smallIconHilite;
	CNode *m_moreUp;
	CNode *m_moreDown;
	bool m_renderIcons;
	int m_iconsPerRow;
	float m_scroll;
	int m_curTitle;
	int m_curTitleCache;
	int m_curGridItemCache;
	int m_iconRowScroll;
	float m_smallIconSpacing;
	bool m_detachIcon;
	int m_curDevUnit;
	float m_copyProgress;
	int m_freeBlocks;
	int m_gameBlocks;
	bool m_isActive;
	bool m_busy;
	int m_nPrefColumn;

	void selectUp();
	void selectDown();
	void selectLeft();
	void selectRight();

	void setSelImage();

	// CStrObject* ReturnSavedGameImage();
	CStrObject *FormatGridItemName();
	CStrObject *FormatGridItemTime();
	CStrObject *FormatGridItemSize();
	CStrObject *FormatTitleSize();
	CStrObject *FormatTotalBlocks();
	CStrObject *FormatFreeBlocks();

	int GetTotalBlocks();

	CStrObject *GetSavedGamePath(int nTitle, int nSavedGame);
	CStrObject *GetUpdateString(void);

	void StartCopy(int destDevUnit);
	void StartDelete();
	int DoesSavedGameExists(int destDevUnit);

	int CanDetachIcon();

	int GetTitleCount();
	int CanCopy();
	int IsSoundtrackSelected();
	int IsDevUnitReady(int nUnit);

	// 5960 additions (Xbox Live content in save grid)
	int GetXboxLiveAccountsCount(int nTitle);
	int GetGridItemCount(int nTitle);
	int DoesXboxLiveAccountExists(int nTitle);
	int IsSavedGameSelected();
	int IsDLContentSelected();
	int IsDLContentPartial();
	void StartSavedGameCopy(int destDevUnit);
	void StartXboxLiveAccountCopy(int destDevUnit);

protected:
	int m_language;
	int m_totalBlocks;
	float m_scrollTo;
	XTIME m_timeScroll;
	int m_curTitleLast;
	bool m_copying;
	XTIME m_timeCopyingStarted;
	bool m_copyError;
	int m_deletedTitle;
	XTIME m_timeOfDelete;
	int m_soundtrackTitle;
	int m_soundtrackCount;
	int m_xboxLiveAccountTitle; // 5960: index in title list where Live accounts start (-1 = none)

	int m_cacheTitleSize;
	int m_cacheSavedGameSize;

	void RenderIconRow(D3DXMATRIX *pMatrix, float y, int nTitle, int nFirstSavedGame, int nSavedGameCount);
	float RenderLoop(bool bRender);
	void SelectTitle(int nTitle, bool bInstantScroll = false);
	bool GetSavedGamePath(TCHAR *szBuf, int nTitle, int nSavedGame);

	int GetSavedGameCount(int nTitle);
	int GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent);
	const TCHAR *GetTitleID2(int nTitle);
	void GetTitleName2(int nTitle, TCHAR *szBuf);
	const TCHAR *GetSavedGameID2(int nTitle, int nSavedGame);
	FILETIME GetSavedGameTime(int nTitle, int nSavedGame);

	CStrObject *GetTitleID(int nTitle);
	CStrObject *GetTitleName(int nTitle);

	DWORD m_lastUpdateTick;
	int m_periodStatus;
	TCHAR m_statusText[MAX_TRANSLATE_LEN];

	DECLARE_NODE_PROPS();
	DECLARE_NODE_FUNCTIONS();

private:
	HANDLE m_titlesEnumThread;
	HANDLE m_savedGameEnumThread;
	HANDLE m_cancelEvent;
	HANDLE m_startEvent, m_stopEvent;
	bool m_savedGameQueryPending;
	volatile bool m_done;
	XTIME m_timeToSendEnd;
	static void WINAPI TitlesEnumThread(CSavedGameGrid *p);
	static void WINAPI SavedGameEnumThread(CSavedGameGrid *p);

	// Background delete thread and flag
	HANDLE m_deleteThread;
	bool m_deleting;
	static void WINAPI DeleteThread(CSavedGameGrid *p);

	// DeleteThread helpers split by case.
	void DeleteCurrentTitle();
	void DeleteGameTitleFiles();
	void DeleteCurrentGridItem();

	// Fire OnSelChange and reset the pulse animation. Used by every
	// directional selectXxx method.
	void NotifySelectionChanged();

	// Per-frame helpers split out of Advance.
	void RefreshDiskFreeSpace();
	void RefreshSizeCaches();

	// RenderLoop helpers split by responsibility.
	void ResolveTitlePodImageName(int nTitle, TCHAR *outBuf);
	void RenderTitlePodAt(int nTitle, float y, const D3DXMATRIX *parentMat);

	// Per-property handlers invoked from OnSetProperty.
	void OnDevUnitChanged(int newDevUnit);
	void OnIsActiveChanged(bool active);

	// Block-count for a per-title support file (title data, title image,
	// or save image XBX). Returns the source-side block count if the
	// destination doesn't already have the file, or 0 if it does.
	int CountSupportFileIfMissing(int destDevUnit, const TCHAR *fileName);

	// Convert the title's wide-char ID to an 8-character ANSI string.
	// outBuf must be at least 9 bytes. Returns false if the title has no
	// ID or the converted string is not exactly 8 characters.
	bool GetTitleIDAnsi(int nTitle, char *outBuf, int outBufSize);

	// Cached helper used by all DLC paths: convert the title ID to ANSI
	// (filling outTitleIDChar) and locate the Nth DLC folder for that
	// title (filling outDLCFolder). Returns false if either step fails.
	bool ResolveDLCFolder(int nTitle, int dlcIndex,
	                      char *outTitleIDChar, int titleIDBufSize,
	                      char *outDLCFolder, int dlcFolderBufSize);

	// Device-unit classification. Dev units 0..7 are physical storage
	// (Dev0 = HDD, 1..7 = MU slots). Dev unit 8 is the synthetic
	// "memory unit selector" view shown when the user is choosing which
	// MU to copy to/from.
	inline bool IsHDD() const         { return m_curDevUnit == Dev0; }
	inline bool IsMUSlot() const      { return m_curDevUnit > Dev0 && m_curDevUnit < 8; }
	inline bool IsMUSelector() const  { return m_curDevUnit == 8; }
	inline bool IsValidDevUnit() const { return m_curDevUnit >= 0 && m_curDevUnit <= 8; }

	// World-matrix cache for the focused title's pod. RenderLoop captures
	// the matrix the first time it draws the selected pod so SelectTitle
	// and the delete animation can redraw it without recomputing.
	D3DXMATRIX m_focusPodMatrix;
	bool m_focusPodMatrixValid;

	int m_dlcCountCache[256];
	int m_dlcCountCacheSize;
	void InvalidateDLCCache();
};

IMPLEMENT_NODE("SavedGameGrid", CSavedGameGrid, CNode)

START_NODE_PROPS(CSavedGameGrid, CNode)
NODE_PROP(pt_node, CSavedGameGrid, pod)
NODE_PROP(pt_node, CSavedGameGrid, podRing)
// Xbox Live Accounts Panel
NODE_PROP(pt_node, CSavedGameGrid, podXboxLiveAccountsPanel)
NODE_PROP(pt_node, CSavedGameGrid, XboxLiveAccountsIconPanel)
NODE_PROP(pt_node, CSavedGameGrid, iconBumpRing)
NODE_PROP(pt_node, CSavedGameGrid, iconArrow)
NODE_PROP(pt_node, CSavedGameGrid, iconCheck)
NODE_PROP(pt_integer, CSavedGameGrid, curGridItem)

NODE_PROP(pt_node, CSavedGameGrid, podSavePanel)
NODE_PROP(pt_node, CSavedGameGrid, podSoundtrackPanel)
NODE_PROP(pt_node, CSavedGameGrid, podHilite)
NODE_PROP(pt_node, CSavedGameGrid, MUheader)
NODE_PROP(pt_node, CSavedGameGrid, MUhiliteHeader)
NODE_PROP(pt_node, CSavedGameGrid, firstMURow)
NODE_PROP(pt_node, CSavedGameGrid, header)
NODE_PROP(pt_node, CSavedGameGrid, hiliteHeader)
NODE_PROP(pt_node, CSavedGameGrid, firstRow)
NODE_PROP(pt_node, CSavedGameGrid, secondRow)
NODE_PROP(pt_node, CSavedGameGrid, otherRow)
NODE_PROP(pt_integer, CSavedGameGrid, renderIcons)
NODE_PROP(pt_integer, CSavedGameGrid, iconsPerRow)
NODE_PROP(pt_number, CSavedGameGrid, scroll)
NODE_PROP(pt_integer, CSavedGameGrid, curTitle)
NODE_PROP(pt_node, CSavedGameGrid, smallIcon)
NODE_PROP(pt_node, CSavedGameGrid, SavedIconPanel)
NODE_PROP(pt_node, CSavedGameGrid, SoundtrackIconPanel)
NODE_PROP(pt_node, CSavedGameGrid, iconRing)
NODE_PROP(pt_node, CSavedGameGrid, smallIconHilite)
NODE_PROP(pt_number, CSavedGameGrid, smallIconSpacing)
NODE_PROP(pt_integer, CSavedGameGrid, iconRowScroll)
NODE_PROP(pt_node, CSavedGameGrid, moreUp)
NODE_PROP(pt_node, CSavedGameGrid, moreDown)
NODE_PROP(pt_boolean, CSavedGameGrid, detachIcon)
NODE_PROP(pt_boolean, CSavedGameGrid, isActive)
NODE_PROP(pt_integer, CSavedGameGrid, curDevUnit)
NODE_PROP(pt_number, CSavedGameGrid, copyProgress)
NODE_PROP(pt_integer, CSavedGameGrid, freeBlocks)
NODE_PROP(pt_integer, CSavedGameGrid, gameBlocks)
NODE_PROP(pt_integer, CSavedGameGrid, nPrefColumn)
END_NODE_PROPS()

#define _FND_CLASS CSavedGameGrid
START_NODE_FUN(CSavedGameGrid, CNode)
NODE_FUN_VV(selectUp)
NODE_FUN_VV(selectDown)
NODE_FUN_VV(selectLeft)
NODE_FUN_VV(selectRight)
NODE_FUN_VV(setSelImage)
NODE_FUN_SV(FormatGridItemName)
NODE_FUN_SV(FormatGridItemTime)
NODE_FUN_SV(FormatGridItemSize)
NODE_FUN_SV(FormatTitleSize)
NODE_FUN_SV(FormatTotalBlocks)
NODE_FUN_IV(GetTotalBlocks)
NODE_FUN_SV(FormatFreeBlocks)
NODE_FUN_IV(CanDetachIcon)
NODE_FUN_II(GetSavedGameCount)
NODE_FUN_SI(GetTitleName)
NODE_FUN_SI(GetTitleID)
NODE_FUN_VI(StartCopy)
NODE_FUN_VV(StartDelete)
NODE_FUN_II(DoesSavedGameExists)
NODE_FUN_SII(GetSavedGamePath)
NODE_FUN_IV(CanCopy)
NODE_FUN_IV(IsSoundtrackSelected)
NODE_FUN_II(IsDevUnitReady)
// Xbox Live Panel
NODE_FUN_IV(IsXboxLiveAccountSelected)
NODE_FUN_IV(IsGameTitleSelected)
NODE_FUN_IV(DoesUserContentExist)
NODE_FUN_II(GetDLContentCount)
// 5960 additions (Xbox Live content management in save grid)
NODE_FUN_II(GetXboxLiveAccountsCount)
NODE_FUN_II(GetGridItemCount)
NODE_FUN_II(DoesXboxLiveAccountExists)
NODE_FUN_IV(IsSavedGameSelected)
NODE_FUN_IV(IsDLContentSelected)
NODE_FUN_IV(IsDLContentPartial)
NODE_FUN_VI(StartSavedGameCopy)
NODE_FUN_VI(StartXboxLiveAccountCopy)

NODE_FUN_IV(GetTitleCount)
NODE_FUN_SV(GetUpdateString)
END_NODE_FUN()
#undef _FND_CLASS

// =============================================================================
// Internal helpers
// =============================================================================

// DLC folders for a title live at E:\TDATA\<titleid>\$c\<dlcid>\ where each
// <dlcid> is a 16-character folder name. This helper finds the Nth such folder
// (sorted by FindFirstFile order) and copies its name into outFolderName.
// Returns false if no Nth folder exists or the title has no DLC directory.
static bool FindDLCFolderByIndex(const char* titleIDChar, int dlcIndex,
                                 char* outFolderName, int outBufSize)
{
	if (!titleIDChar || !outFolderName || outBufSize < 17)
		return false;

	char searchPath[MAX_PATH];
	_snprintf(searchPath, MAX_PATH, "E:\\TDATA\\%s\\$c\\*", titleIDChar);

	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(searchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;

	int currentIndex = 0;
	bool found = false;
	do
	{
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    strcmp(findData.cFileName, ".") != 0 &&
		    strcmp(findData.cFileName, "..") != 0 &&
		    strlen(findData.cFileName) == 16)
		{
			if (currentIndex == dlcIndex)
			{
				strncpy(outFolderName, findData.cFileName, outBufSize - 1);
				outFolderName[outBufSize - 1] = '\0';
				found = true;
				break;
			}
			currentIndex++;
		}
	} while (FindNextFileA(hFind, &findData));

	FindClose(hFind);
	return found;
}

bool CSavedGameGrid::GetTitleIDAnsi(int nTitle, char *outBuf, int outBufSize)
{
	if (!outBuf || outBufSize < 9)
		return false;

	const TCHAR *titleID = GetTitleID2(nTitle);
	if (!titleID)
		return false;

	WideCharToMultiByte(CP_ACP, 0, titleID, -1, outBuf, outBufSize, NULL, NULL);
	return strlen(outBuf) == 8;
}

bool CSavedGameGrid::ResolveDLCFolder(int nTitle, int dlcIndex,
                                      char *outTitleIDChar, int titleIDBufSize,
                                      char *outDLCFolder, int dlcFolderBufSize)
{
	if (!GetTitleIDAnsi(nTitle, outTitleIDChar, titleIDBufSize))
		return false;
	return FindDLCFolderByIndex(outTitleIDChar, dlcIndex, outDLCFolder, dlcFolderBufSize);
}

CSavedGameGrid::CSavedGameGrid() : m_language(0),
								   m_pod(NULL),
								   m_podRing(NULL),
								   // Xbox Live Accounts Panel
								   m_podXboxLiveAccountsPanel(NULL),
								   m_XboxLiveAccountsIconPanel(NULL),
								   m_iconBumpRing(NULL),
								   m_iconArrow(NULL),
								   m_iconCheck(NULL),
								   m_curGridItem(-1),

								   m_podSavePanel(NULL),
								   m_podSoundtrackPanel(NULL),
								   m_podHilite(NULL),
								   m_MUheader(NULL),
								   m_MUhiliteHeader(NULL),
								   m_firstMURow(NULL),
								   m_header(NULL),
								   m_hiliteHeader(NULL),
								   m_firstRow(NULL),
								   m_secondRow(NULL),
								   m_otherRow(NULL),
								   m_moreUp(NULL),
								   m_moreDown(NULL),
								   m_renderIcons(true),
								   m_iconsPerRow(4),
								   m_scroll(0.0f),
								   m_curTitle(-1),
								   m_curTitleCache(-1),
								   m_curGridItemCache(-1),
								   m_smallIcon(NULL),
								   m_SavedIconPanel(NULL),
								   m_SoundtrackIconPanel(NULL),
								   m_iconRing(NULL),
								   m_smallIconHilite(NULL),
								   m_smallIconSpacing(1.0f),
								   m_detachIcon(false),
								   m_isActive(false),
								   m_busy(false),
								   m_deleteThread(NULL),
								   m_deleting(false),
								   m_titlesEnumThread(NULL),
								   m_savedGameEnumThread(NULL),
								   m_cacheSavedGameSize(-1),
								   m_cacheTitleSize(-1),
								   m_savedGameQueryPending(false),
								   m_done(false),
								   m_copying(false),
								   m_timeCopyingStarted(0.0f),
								   m_curDevUnit(-1),
								   m_iconRowScroll(0),
								   m_nPrefColumn(-1)
{
	m_scrollTo = 0.0f;
	m_timeScroll = 0.0f;
	m_curTitleLast = -1;
	m_totalBlocks = -1;
	m_freeBlocks = -1;
	m_gameBlocks = -1;
	m_deletedTitle = -1;
	m_timeOfDelete = 0.0f;
	m_soundtrackTitle = -1;
	m_soundtrackCount = 0;
	m_xboxLiveAccountTitle = -1;
	m_focusPodMatrixValid = false;
	D3DXMatrixIdentity(&m_focusPodMatrix);
	m_dlcCountCacheSize = 0;

	VERIFY(m_startEvent = CreateEvent(0, FALSE, FALSE, 0));
	VERIFY(m_stopEvent = CreateEvent(0, TRUE, FALSE, 0));
	VERIFY(m_cancelEvent = CreateEvent(0, TRUE, FALSE, 0));
	VERIFY(m_savedGameEnumThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)SavedGameEnumThread, (LPVOID)this, 0, 0));
}

CSavedGameGrid::~CSavedGameGrid()
{
	if (m_pod != NULL)
		m_pod->Release();

	if (m_podRing != NULL)
		m_podRing->Release();

	// Xbox Live Accounts Panel
	if (m_podXboxLiveAccountsPanel != NULL)
		m_podXboxLiveAccountsPanel->Release();
	if (m_XboxLiveAccountsIconPanel != NULL)
		m_XboxLiveAccountsIconPanel->Release();
	if (m_iconBumpRing != NULL)
		m_iconBumpRing->Release();
	if (m_iconArrow != NULL)
		m_iconArrow->Release();
	if (m_iconCheck != NULL)
		m_iconCheck->Release();

	if (m_podSavePanel != NULL)
		m_podSavePanel->Release();

	if (m_podSoundtrackPanel != NULL)
		m_podSoundtrackPanel->Release();

	if (m_podHilite != NULL)
		m_podHilite->Release();

	if (m_MUheader != NULL)
		m_MUheader->Release();

	if (m_MUhiliteHeader != NULL)
		m_MUhiliteHeader->Release();

	if (m_firstMURow != NULL)
		m_firstMURow->Release();

	if (m_header != NULL)
		m_header->Release();

	if (m_hiliteHeader != NULL)
		m_hiliteHeader->Release();

	if (m_firstRow != NULL)
		m_firstRow->Release();

	if (m_secondRow != NULL)
		m_secondRow->Release();

	if (m_otherRow != NULL)
		m_otherRow->Release();

	if (m_smallIcon != NULL)
		m_smallIcon->Release();

	if (m_SavedIconPanel != NULL)
		m_SavedIconPanel->Release();

	if (m_SoundtrackIconPanel != NULL)
		m_SoundtrackIconPanel->Release();

	if (m_iconRing != NULL)
		m_iconRing->Release();

	if (m_smallIconHilite != NULL)
		m_smallIconHilite->Release();

	if (m_moreUp != NULL)
		m_moreUp->Release();

	if (m_moreDown != NULL)
		m_moreDown->Release();

	// Tear down the background enumeration thread before closing its events.
	if (m_savedGameQueryPending)
	{
		m_done = true;
		SetEvent(m_cancelEvent);
	}

	WaitForSingleObject(m_savedGameEnumThread, INFINITE);

	VERIFY(CloseHandle(m_startEvent));
	VERIFY(CloseHandle(m_stopEvent));
	VERIFY(CloseHandle(m_cancelEvent));
	VERIFY(CloseHandle(m_savedGameEnumThread));
}

int CSavedGameGrid::GetTitleCount()
{
	ASSERT(IsValidDevUnit());

	int nTitleCount = g_titles[m_curDevUnit].GetTitleCount();
	if (IsHDD())
	{
		m_soundtrackCount = GetSoundtrackCount();
		m_soundtrackTitle = nTitleCount;
	}
	else
	{
		m_soundtrackCount = 0;
		m_soundtrackTitle = -1;
	}

	if (IsHDD() && m_soundtrackCount > 0)
	{
		ASSERT(nTitleCount == m_soundtrackTitle);
		nTitleCount += 1;
	}

	return nTitleCount;
}

inline int CSavedGameGrid::GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent)
{
	ASSERT(IsValidDevUnit());

	if (nTitle < 0)
		return 0;

	if (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle)
		return g_titles[m_curDevUnit].GetTitleTotalBlocks(nTitle, hCancelEvent);

	return GetSoundtrackSize(-1);
}

inline const TCHAR *CSavedGameGrid::GetTitleID2(int nTitle)
{
	ASSERT(IsValidDevUnit());

	ASSERT(nTitle >= 0 && (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle));
	return g_titles[m_curDevUnit].GetTitleID(nTitle);
}

inline CStrObject *CSavedGameGrid::GetTitleID(int nTitle)
{
	ASSERT(nTitle >= 0 && (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle));
	return new CStrObject(GetTitleID2(nTitle));
}

void CSavedGameGrid::GetTitleName2(int nTitle, TCHAR *szBuf)
{
	TCHAR sz[MAX_TRANSLATE_LEN];
	ASSERT(IsValidDevUnit());

	if (nTitle < 0)
	{
		if (IsHDD())
			_tcscpy(szBuf, Translate(_T("Xbox Hard Disk"), sz));
		else
		{
			FormatDeviceName(m_curDevUnit, szBuf);
			if (szBuf[0] == 0)
			{
				TCHAR ch = _T('B');

				// Generate a name based on location of slot...
				int nGamePad = (m_curDevUnit + 1) % 2;
				if (nGamePad)
					ch = _T('A');

				Translate(_T("memory unit"), sz);
				swprintf(szBuf, _T("%s %d%c"), sz, (m_curDevUnit / 2) + 1, ch);
			}
		}
		return;
	}

	if (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle)
	{
		_tcscpy(szBuf, g_titles[m_curDevUnit].GetTitleName(nTitle));
		return;
	}

	_tcscpy(szBuf, Translate(_T("Soundtracks"), sz));
}

inline CStrObject *CSavedGameGrid::GetTitleName(int nTitle)
{
	TCHAR szBuf[256];
	GetTitleName2(nTitle, szBuf);
	return new CStrObject(szBuf);
}

inline const TCHAR *CSavedGameGrid::GetSavedGameID2(int nTitle, int nSavedGame)
{
	ASSERT(IsValidDevUnit());

	ASSERT(nTitle >= 0 && (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle));
	return g_titles[m_curDevUnit].GetSavedGameID(nTitle, nSavedGame);
}

inline FILETIME CSavedGameGrid::GetSavedGameTime(int nTitle, int nSavedGame)
{
	ASSERT(IsValidDevUnit());

	ASSERT(nTitle >= 0 && (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle));
	return g_titles[m_curDevUnit].GetSavedGameTime(nTitle, nSavedGame);
}

void WINAPI CSavedGameGrid::TitlesEnumThread(CSavedGameGrid *p)
{
	p->m_busy = true;

	// Enumerate all titles on current device unit
	g_titles[p->m_curDevUnit].Update();

	// Also enumerate soundtrack if it's a hard disk
	if (p->IsHDD() && p->m_soundtrackTitle == -1)
	{
		int nSoundtrackCount = GetSoundtrackCount();
		if (nSoundtrackCount > 0)
		{
			p->m_soundtrackTitle = p->GetTitleCount();
			p->m_soundtrackCount = nSoundtrackCount;
		}
	}

	p->m_busy = false;
}

void WINAPI CSavedGameGrid::SavedGameEnumThread(CSavedGameGrid *p)
{
	while (!p->m_done)
	{
		ASSERT(p->m_startEvent);
		ASSERT(p->m_stopEvent);

		SignalObjectAndWait(p->m_stopEvent, p->m_startEvent, INFINITE, FALSE);
		ResetEvent(p->m_stopEvent);

		ASSERT(!g_titles[p->m_curDevUnit].IsDirty());

		if (p->m_cacheSavedGameSize < 0 && p->m_curTitle >= 0 && p->m_curGridItem >= 0)
		{
			if (p->m_soundtrackTitle < 0 || p->m_curTitle < p->m_soundtrackTitle)
			{
				TCHAR szSavedGameDir[MAX_PATH];
				if (p->GetSavedGamePath(szSavedGameDir, p->m_curTitle, p->m_curGridItem))
				{
					p->m_cacheSavedGameSize = GetDirectoryBlocks(szSavedGameDir, BLOCK_SIZE, true, p->m_cancelEvent);
				}
				else
				{
					p->m_cacheSavedGameSize = 0;
				}
			}
			else
			{
				p->m_cacheSavedGameSize = GetSoundtrackSize(p->m_curGridItem);
			}
		}
		else if (p->m_cacheTitleSize < 0)
		{
			p->m_cacheTitleSize = p->GetTitleTotalBlocks(p->m_curTitle, p->m_cancelEvent);
		}
	}
}

void CSavedGameGrid::RefreshSizeCaches()
{
	// Drain a completed background size query if one was running.
	if (m_savedGameQueryPending && WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0)
		m_savedGameQueryPending = false;

	// Detect selection changes and invalidate the affected caches.
	bool needQuery = false;

	if (m_curGridItem != m_curGridItemCache)
	{
		m_curGridItemCache = m_curGridItem;
		m_cacheSavedGameSize = -1;
		needQuery = true;
	}
	else if (m_curTitle != m_curTitleCache)
	{
		m_curTitleCache = m_curTitle;
		m_curGridItemCache = m_curGridItem;
		m_cacheTitleSize = -1;
		m_cacheSavedGameSize = -1;
		needQuery = true;
	}
	else if (!m_savedGameQueryPending && (m_cacheTitleSize < 0 || m_cacheSavedGameSize < 0))
	{
		needQuery = true;
	}

	// Kick off a new background query if there's a missing cache and a
	// real title to query.
	if (!needQuery || m_curTitle < 0)
		return;

	const bool wantTitleSize = (m_cacheTitleSize < 0);
	const bool wantSaveSize = (m_cacheSavedGameSize < 0 && m_curGridItem >= 0);
	if (!wantTitleSize && !wantSaveSize)
		return;

	// Cancel any in-flight query before starting the new one.
	if (m_savedGameQueryPending)
		SignalObjectAndWait(m_cancelEvent, m_stopEvent, INFINITE, FALSE);

	ResetEvent(m_cancelEvent);
	SetEvent(m_startEvent);
	m_savedGameQueryPending = true;
}

void CSavedGameGrid::RefreshDiskFreeSpace()
{
#ifdef _XBOX
	FSCHAR path[MAX_PATH];
	Ansi(path, g_titles[m_curDevUnit].GetUData(), MAX_PATH);
#else
	const FSCHAR *path = g_titles[m_curDevUnit].GetUData();
#endif

	ULARGE_INTEGER availBytes, totalBytes, freeBytes;
	if (!GetDiskFreeSpaceEx(path, &availBytes, &totalBytes, &freeBytes))
	{
		// Treat query failures as a "full empty disk" so the UI shows
		// 0 free instead of stale values from the previous device.
		freeBytes.QuadPart = 0;
		totalBytes.QuadPart = BLOCK_SIZE;
		TRACE(_T("GetDiskFreeSpaceEx %s failed: %d\n"), g_titles[m_curDevUnit].GetUData(), GetLastError());
	}

	int totalBlocks = (int)((totalBytes.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE);
	int freeBlocks  = (int)((freeBytes.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE);

	// One block is always consumed by the volume root directory.
	totalBlocks -= 1;

	if (totalBlocks == m_totalBlocks && freeBlocks == m_freeBlocks && g_nCurLanguage == m_language)
		return;

	m_language = g_nCurLanguage;
	m_totalBlocks = totalBlocks;
	m_freeBlocks = freeBlocks;
	CallFunction(this, _T("OnTotalFreeChanged"));
	CallFunction(this, _T("OnSelChange"));
}

void CSavedGameGrid::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_curDevUnit == -1 || !m_isActive)
		return;

	ASSERT(IsValidDevUnit());

	// Enumerating all titles in UDATA
	if (m_titlesEnumThread)
	{
		if (TheseusGetNow() > m_timeToSendEnd && WaitForSingleObject(m_titlesEnumThread, 0) == WAIT_OBJECT_0)
		{
			VERIFY(CallFunction(this, _T("OnUpdatingTitlesEnd")));
			VERIFY(CloseHandle(m_titlesEnumThread));
			m_titlesEnumThread = NULL;
			InvalidateDLCCache();
		}
		else
		{
			return;
		}
	}
	else if (g_titles[m_curDevUnit].IsDirty())
	{
		if (!m_titlesEnumThread)
		{
			m_soundtrackTitle = -1;
			m_soundtrackCount = 0;

			m_timeToSendEnd = TheseusGetNow() + 1.0f;
			VERIFY(CallFunction(this, _T("OnUpdatingTitlesBegin")));

			m_titlesEnumThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)TitlesEnumThread, (LPVOID)this, 0, 0);

			// If the CreateThread failed, call it directly
			if (!m_titlesEnumThread)
			{
				TitlesEnumThread(this);
				VERIFY(CallFunction(this, _T("OnUpdatingTitlesEnd")));
				InvalidateDLCCache();
				goto next;
			}
		}
		return;
	}

next:

	if (m_copying)
	{
		if (m_copyProgress != theGameCopier.m_progress)
		{
			m_copyProgress = theGameCopier.m_progress;
			CallFunction(this, _T("OnCopyProgressChanged"));
		}

		if (theGameCopier.m_error)
		{
			m_copying = false;
			theGameCopier.Finish();
			TRACE(_T("OnCopyError\n"));
			CallFunction(this, _T("OnCopyError"));
		}
		else if (theGameCopier.m_done && TheseusGetNow() >= m_timeCopyingStarted + 0.5f)
		{
			m_copying = false;
			theGameCopier.Finish();
			TRACE(_T("OnCopyComplete\n"));
			CallFunction(this, _T("OnCopyComplete"));
		}
		return;
	}

	if (m_deleting)
	{
		ASSERT(m_deleteThread);
		if (WaitForSingleObject(m_deleteThread, 0) == WAIT_OBJECT_0)
		{
			CloseHandle(m_deleteThread);
			m_deleteThread = NULL;
			m_deleting = false;
			InvalidateDLCCache();
			VERIFY(CallFunction(this, _T("OnDeleteEnd")));
			VERIFY(CallFunction(this, _T("OnSelChange")));
			g_pulseStartTime = TheseusGetNow();
		}
		return;
	}

	if (!g_titles[m_curDevUnit].IsValid())
	{
		CallFunction(this, _T("OnDeviceRemoved"));
		m_curDevUnit = -1;
		return;
	}

	if (g_titles[m_curDevUnit].IsDirty())
	{
		return;
	}

	RefreshSizeCaches();
	RefreshDiskFreeSpace();

	if (m_timeScroll != 0.0f)
	{
		float t = (float)(TheseusGetNow() - m_timeScroll) / 0.25f;
		if (t >= 1.0f)
		{
			m_timeScroll = 0.0f;
			t = 1.0f;
		}

		float t1 = 1.0f - t;
		m_scroll = t1 * m_scroll + t * m_scrollTo;

		m_focusPodMatrixValid = false;
	}
}

CStrObject *CSavedGameGrid::GetUpdateString(void)
{
	if ((GetTickCount() - m_lastUpdateTick) > 500)
	{
		m_lastUpdateTick = GetTickCount();
		m_periodStatus++;
		if (m_periodStatus > 3)
			m_periodStatus = 0;

		Translate(_T("Copying"), m_statusText);

		for (int i = 0; i < m_periodStatus; i++)
			_tcscat(m_statusText, _T("."));
	}

	return new CStrObject(m_statusText);
}

void CSavedGameGrid::InvalidateDLCCache()
{
	m_dlcCountCacheSize = 0;
}

void CSavedGameGrid::OnDevUnitChanged(int newDevUnit)
{
	m_curDevUnit = newDevUnit;

	// Focus the first row by default; on the MU selector view focus the
	// first MU slot instead.
	SelectTitle(IsMUSelector() ? 0 : -1, true);

	// Cached sizes belong to the previous device; drop them.
	m_cacheSavedGameSize = -1;
	m_cacheTitleSize = -1;
	InvalidateDLCCache();
}

void CSavedGameGrid::OnIsActiveChanged(bool active)
{
	if (active)
		return;

	// Only run deactivation cleanup on a real active->inactive transition,
	// not during initial scene setup when isActive is first set to false.
	if (!m_isActive)
		return;

	// Going inactive while a query is in flight: cancel it and let the
	// worker drop back to its idle wait state before we touch the cache.
	if (m_savedGameQueryPending)
	{
		SignalObjectAndWait(m_cancelEvent, m_stopEvent, INFINITE, FALSE);
		m_savedGameQueryPending = false;
	}

	m_cacheTitleSize = -1;
	m_cacheSavedGameSize = -1;

	// Flush the texture cache so save game thumbnails don't linger and
	// consume RAM after we leave the Memory blade.
	FlushTextureCache();
}

bool CSavedGameGrid::OnSetProperty(const PRD *pprd, const void *pvValue)
{
	const int offset = PTR2INT(pprd->pbOffset);

	if (offset == offsetof(m_curDevUnit))
	{
		OnDevUnitChanged(*(int *)pvValue);
		return false;
	}
	if (offset == offsetof(m_curTitle))
	{
		SelectTitle(*(int *)pvValue);
		return false;
	}
	if (offset == offsetof(m_isActive))
	{
		OnIsActiveChanged(*(bool *)pvValue);
	}

	return true;
}

static void RenderNodeAt(CNode *pNode, D3DXMATRIX *pMatrix)
{
	if (pNode == NULL)
		return;

	TheseusPushWorld();
	TheseusMultWorld(pMatrix);

	TheseusUpdateWorld();

	pNode->Render();

	TheseusPopWorld();
}

void CSavedGameGrid::RenderIconRow(D3DXMATRIX *pMatrix, float y, int nTitle, int nFirstSavedGame, int nSavedGames)
{
	bool isDLC = false;
	if (m_smallIcon == NULL)
		return;

	if (nTitle < 0)
	{
		// Device-level row (memory unit selector); no icon row to draw.
		return;
	}

	int nLim = nFirstSavedGame + m_iconsPerRow;
	if (nLim > nSavedGames)
		nLim = nSavedGames;

	ASSERT(nFirstSavedGame == 0 || nFirstSavedGame < nLim);

	float x = 0.0f;
	for (int i = nFirstSavedGame; i < nLim; i += 1)
	{
		D3DXMATRIX mat2;
		D3DXMatrixTranslation(&mat2, x, y, 0.0f);
		D3DXMatrixMultiply(&mat2, pMatrix, &mat2);

		TCHAR szBuf[MAX_PATH];
		if (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle)
		{
			int nSavedCount = GetSavedGameCount(nTitle);
			if (i < nSavedCount)
			{
				g_titles[m_curDevUnit].GetSavedGameImageName(nTitle, i, szBuf);
			}
			else
			{
				// DLC item: load the Nth DLC folder's ContentImage.xbx
				// (small icon variant), or fall back to the placeholder.
				char titleIDChar[9];
				char dlcFolder[17];
				if (ResolveDLCFolder(nTitle, i - nSavedCount,
				                     titleIDChar, sizeof(titleIDChar),
				                     dlcFolder, sizeof(dlcFolder)))
				{
					char dlcImagePath[MAX_PATH];
					_snprintf(dlcImagePath, MAX_PATH, "E:\\TDATA\\%s\\$c\\%s\\ContentImage.xbx",
					          titleIDChar, dlcFolder);
					MultiByteToWideChar(CP_ACP, 0, dlcImagePath, -1, szBuf, MAX_PATH);
					isDLC = true; // show m_iconCheck overlay
				}
				else
				{
					_tcscpy(szBuf, _T("dlcitem64.xbx"));
				}
			}

			g_szCurTitleImage = szBuf;
			RenderNodeAt(m_SavedIconPanel, &mat2);
		}
		else // is a soundtrack
		{
			_tcscpy(szBuf, _T("soundtracksave64.tga"));
			g_szCurTitleImage = szBuf;
			RenderNodeAt(m_SoundtrackIconPanel, &mat2);
		}

		g_szCurTitleImage = NULL;

		if (!m_detachIcon && m_smallIconHilite != NULL && nTitle == m_curTitle && i == m_curGridItem)
			RenderNodeAt(m_iconRing, &mat2);
		if (isDLC && m_iconCheck)
			RenderNodeAt(m_iconCheck, &mat2);
		x += m_smallIconSpacing;
	}
}

void CSavedGameGrid::ResolveTitlePodImageName(int nTitle, TCHAR *outBuf)
{
	// Memory unit row.
	if (nTitle == -1)
	{
		_tcscpy(outBuf, _T("memoryUnit128.tga"));
		return;
	}

	// Soundtrack row sits at the end of the title list on HDD.
	if (m_soundtrackTitle >= 0 && nTitle >= m_soundtrackTitle)
	{
		_tcscpy(outBuf, _T("soundtracksave3.tga"));
		return;
	}

	// Game title with detached icon: the lifted pod shows the icon for
	// whichever grid item is currently selected (saved game or DLC).
	if (nTitle == m_curTitle && m_detachIcon && m_curGridItem != -1)
	{
		const int nSavedGames = GetSavedGameCount(nTitle);
		const int nDLC = GetDLContentCount(nTitle);

		if (m_curGridItem < nSavedGames)
		{
			g_titles[m_curDevUnit].GetSavedGameImageName(nTitle, m_curGridItem, outBuf);
			return;
		}

		if (m_curGridItem - nSavedGames < nDLC)
		{
			char titleIDChar[9];
			char dlcFolder[17];
			if (ResolveDLCFolder(nTitle, m_curGridItem - nSavedGames,
			                     titleIDChar, sizeof(titleIDChar),
			                     dlcFolder, sizeof(dlcFolder)))
			{
				char contentImagePath[MAX_PATH];
				_snprintf(contentImagePath, MAX_PATH,
				          "E:\\TDATA\\%s\\$c\\%s\\ContentImage.xbx",
				          titleIDChar, dlcFolder);
				MultiByteToWideChar(CP_ACP, 0, contentImagePath, -1, outBuf, MAX_PATH);
				return;
			}
			_tcscpy(outBuf, _T("dlcitem128.xbx"));
			return;
		}

		// Out-of-range grid item: fall back to the generic logo.
		_tcscpy(outBuf, _T("xboxlogo128.xbx"));
		return;
	}

	// Default: the title's own 128px icon, falling back to the logo if
	// the title doesn't ship one.
	MakePath(outBuf, g_titles[m_curDevUnit].GetUData(), GetTitleID2(nTitle));
	MakePath(outBuf, outBuf, szTitleImageXBX);
	if (!DoesFileExist(outBuf))
		_tcscpy(outBuf, _T("xboxlogo128.xbx"));
}

void CSavedGameGrid::RenderTitlePodAt(int nTitle, float y, const D3DXMATRIX *parentMat)
{
	if (m_pod == NULL)
		return;

	TCHAR imageName[MAX_PATH];
	ResolveTitlePodImageName(nTitle, imageName);
	g_szCurTitleImage = imageName;

	const float effectAlphaSave = g_nEffectAlpha;

	bool renderNormal = true;
	bool renderHack = false;
	bool renderHilite = false;
	const bool renderSoundtrack = (nTitle == m_soundtrackTitle);

	// Selected pod takes the captured-matrix path so it can lift up
	// independent of the row scroll position.
	if (nTitle == m_curTitle && m_scroll == m_scrollTo)
	{
		renderNormal = false;
		renderHack = true;
		renderHilite = !m_detachIcon && m_podHilite != NULL && m_curGridItem == -1;

		// Mid-delete fade: collapse to the normal pod path with alpha
		// decay so the pod fades out instead of jumping.
		if (nTitle > -1 && nTitle == m_deletedTitle)
		{
			const float t = (float)(TheseusGetNow() - m_timeOfDelete) / 1.0f;
			if (t >= 1.0f)
			{
				m_timeOfDelete = 0.0f;
				m_deletedTitle = -1;
				m_focusPodMatrixValid = false;
			}
			else
			{
				g_nEffectAlpha *= 1.0f - t;
				g_szCurTitleImage = NULL;
				m_focusPodMatrixValid = true;
				renderHilite = false;
				renderNormal = true;
			}
		}
	}

	if (renderHack)
	{
		D3DXMATRIX mat;
		TheseusPushWorld();
		if (m_focusPodMatrixValid)
		{
			TheseusIdentityWorld();
			TheseusMultWorld(&m_focusPodMatrix);
		}
		else
		{
			D3DXMatrixTranslation(&mat, -0.3292f, y, -0.0271f);
			D3DXMatrixMultiply(&mat, parentMat, &mat);
			TheseusMultWorld(&mat);
			m_focusPodMatrix = *TheseusGetWorld();
			m_focusPodMatrixValid = true;
		}

		TheseusUpdateWorld();
		m_pod->Render();
		if (renderSoundtrack)
			m_podSoundtrackPanel->Render();
		else
			m_podSavePanel->Render();
		if (renderHilite)
			m_podRing->Render();
		TheseusPopWorld();
	}

	g_nEffectAlpha = effectAlphaSave;
	g_szCurTitleImage = imageName;

	if (renderNormal)
	{
		D3DXMATRIX mat;
		D3DXMatrixTranslation(&mat, -0.3292f, y, -0.0271f);
		D3DXMatrixMultiply(&mat, parentMat, &mat);
		RenderNodeAt(m_pod, &mat);
		if (renderSoundtrack)
			RenderNodeAt(m_podSoundtrackPanel, &mat);
		else
			RenderNodeAt(m_podSavePanel, &mat);
	}

	g_szCurTitleImage = NULL;
}

void CSavedGameGrid::Render()
{
	if (m_header == NULL || m_firstRow == NULL || m_secondRow == NULL || m_otherRow == NULL)
		return;

	if (m_curDevUnit == -1 || !g_titles[m_curDevUnit].IsValid() || g_titles[m_curDevUnit].IsDirty())
		return;

	if (!m_isActive || m_busy)
		return;

	RenderLoop(true);
}

float CSavedGameGrid::RenderLoop(bool bRender)
{
	D3DXMATRIX mat, mat2, scrollMat;
	D3DXVECTOR3 v(-1.0f, 0.0f, 0.0f);
	D3DXMatrixRotationAxis(&mat, &v, -1.571f);
	D3DXMatrixScaling(&mat2, 0.05942f, 0.05942f, 0.05942f);
	D3DXMatrixMultiply(&mat, &mat, &mat2);

	float y = m_scroll;
	float yLimit = -1.25f;

	int nTitleCount = GetTitleCount();

	int initLoop;
	if (IsMUSelector()) // do we render the memory panel at the top of the memory list
		initLoop = 0;
	else
		initLoop = -1;

	int nTitle;
	for (nTitle = initLoop; nTitle < nTitleCount && (!bRender || y > yLimit); nTitle += 1)
	{
		float nEffectAlphaSave = g_nEffectAlpha;
		int nSavedGames = 0;

		// Check how many saved games (or soundtracks) this title has
		if (nTitle >= 0)
		{
			if (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle)
			{
				nSavedGames = GetSavedGameCount(nTitle);
				nSavedGames += GetDLContentCount(nTitle);
			}
			else
			{
				nSavedGames = m_soundtrackCount;
			}
		}

		int nRowCount = (nSavedGames + m_iconsPerRow - 1) / m_iconsPerRow;

		int nIconRowScroll = 0;
		if (nTitle == m_curTitle)
		{
			// we keep track of the scroll position for the current title
			nIconRowScroll = m_iconRowScroll;
		}
		else if (nTitle < m_curTitle)
		{
			// this title is above the current one; show it scrolled to its bottom
			nIconRowScroll = nRowCount - VISIBLE_ICON_ROWS;
			if (nIconRowScroll < 0)
				nIconRowScroll = 0;
		}

		if (bRender)
		{
			float nSelectedAmount = 0.0f;
			float t = (float)(TheseusGetNow() - m_timeScroll) / 0.25f;
			if (t > 1.0f)
				t = 1.0f;

			if (nTitle == m_curTitle)
			{
				if (m_timeScroll != 0.0f)
					nSelectedAmount = t;
				else
					nSelectedAmount = 1.0f;
			}
			else if (nTitle == m_curTitleLast)
			{
				if (m_timeScroll != 0.0f)
					nSelectedAmount = 1.0f - t;
			}

			g_nEffectAlpha *= 0.5f + (0.5f * nSelectedAmount);
		}
		else if (nTitle == m_curTitle)
		{
			return y - m_scroll;
		}

		// Title pod (the orb-and-panel widget for this title row).
		if (bRender && y - 0.25f < 0.5f)
			RenderTitlePodAt(nTitle, y, &mat);

		// Draw header
		{
			if (bRender && y - 0.25f < 0.5f)
			{
				D3DXMatrixTranslation(&mat2, -0.3292f, y, -0.0271f);
				D3DXMatrixMultiply(&mat2, &mat, &mat2);

				if (nTitle >= 0 && nTitle == m_curTitle) // is a game title or soundtrack
					RenderNodeAt(m_hiliteHeader, &mat2);
				else if (nTitle >= 0)
					RenderNodeAt(m_header, &mat2);
				else if (nTitle < 0 && nTitle == m_curTitle) // the title is the memory unit
					RenderNodeAt(m_MUhiliteHeader, &mat2);
				else if (nTitle < 0)
					RenderNodeAt(m_MUheader, &mat2);

				// Draw text
				if (m_renderIcons) // are we on the proper screen?
				{
					TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
					TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
					TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
					TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
					TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
					TheseusSetRenderState(D3DRS_TEXTUREFACTOR, (nTitle == m_curTitle) ? D3DCOLOR_RGBA(170, 170, 170, 255) : D3DCOLOR_RGBA(140, 201, 25, 255));

					D3DXMATRIX mat3;
					// D3DXMatrixScaling(&mat3, 0.05942f, 0.05942f, 0.05942f);
					D3DXMatrixScaling(&mat3, 0.06442f, 0.06442f, 0.06442f);
					D3DXMatrixTranslation(&mat2, -0.3292f - 0.05942f, y + 0.05942f, -0.0271f + (0.6f * 0.05942f));
					D3DXMatrixMultiply(&mat2, &mat3, &mat2);

					TCHAR szBuf[256];
					GetTitleName2(nTitle, szBuf);
					RenderNodeAt(GetTextNode(szBuf, -11.2f), &mat2);
				}

				if (nIconRowScroll > 0)
				{
					// Draw the up arrow
					D3DXMatrixTranslation(&mat2, -0.17f, (y - 0.07f), 0.0f);
					D3DXMatrixMultiply(&mat2, &mat, &mat2);
					scrollMat = mat2;
					// RenderNodeAt(m_moreUp, &mat2);
				}
			}

			y -= 0.0092f;
		}

		// Draw first icon row
		{
			if (bRender && y - 0.25f < 0.5f)
			{
				D3DXMatrixTranslation(&mat2, -0.3292f, y, 0.01144f);
				D3DXMatrixMultiply(&mat2, &mat, &mat2);
				if (nTitle >= 0) // is a game title or soundtrack
					RenderNodeAt(m_firstRow, &mat2);
				else // the title is a memory unit
					RenderNodeAt(m_firstMURow, &mat2);

				if (nIconRowScroll > 0)
				{
					// Draw the up arrow
					RenderNodeAt(m_moreUp, &scrollMat);
				}

				RenderIconRow(&mat, y, nTitle, nIconRowScroll * m_iconsPerRow, nSavedGames);
			}

			y -= 0.2455f;
		}

		if (nSavedGames > m_iconsPerRow)
		{
			if (bRender && y - 0.25f < 0.5f)
			{
				D3DXMatrixTranslation(&mat2, -0.3301f, y, 0.01144f);
				D3DXMatrixMultiply(&mat2, &mat, &mat2);
				RenderNodeAt(m_secondRow, &mat2);
				RenderIconRow(&mat, y, nTitle, (nIconRowScroll + 1) * m_iconsPerRow, nSavedGames);
			}

			y -= 0.24499f;

			if (nSavedGames > m_iconsPerRow * 2)
			{
				for (int nRow = 2; nRow < (nRowCount - nIconRowScroll) && nRow < VISIBLE_ICON_ROWS && (!bRender || y > yLimit); nRow += 1)
				{
					if (bRender && y - 0.25f < 0.5f)
					{
						D3DXMatrixTranslation(&mat2, -0.3412f, y, 0.01144f);
						D3DXMatrixMultiply(&mat2, &mat, &mat2);
						RenderNodeAt(m_otherRow, &mat2);
						if (nRow == VISIBLE_ICON_ROWS - 1 && (nIconRowScroll + nRow) < nRowCount - 1)
						{
							D3DXMatrixTranslation(&mat2, -0.17f, (y - 0.015f), 0.0f);
							D3DXMatrixMultiply(&mat2, &mat, &mat2);
							RenderNodeAt(m_moreDown, &mat2);
						}
						RenderIconRow(&mat, y, nTitle, (nIconRowScroll + nRow) * m_iconsPerRow, nSavedGames);
					}

					y -= 0.24499f;
				}
			}
		}

		// Prepare for next title area
		y -= 0.09f;
		g_nEffectAlpha = nEffectAlphaSave;
	}

	return 0.0f;
}

void CSavedGameGrid::SelectTitle(int nTitle, bool bInstantScroll /*=false*/)
{
	m_curTitleLast = m_curTitle;
	m_curTitle = nTitle;

	if (g_titles[m_curDevUnit].IsDirty())
	{
		return;
	}

	/*
		if (m_curTitle < 0)
		{
			m_curTitle = -1;
			m_curGridItem = -1;
			m_nPrefColumn = -1;
			m_scrollTo = 0.0f;
			m_scroll = 0.0f;
			m_timeScroll = 0.0f;
			m_iconRowScroll = 0;
			m_detachIcon = false;
			m_totalBlocks = -1;
			m_freeBlocks = -1;
		}
		else
	*/
	{
		int nTitleCount = GetTitleCount();
		if (m_curTitle >= nTitleCount)
			m_curTitle = nTitleCount - 1;

		int nSavedGames = GetSavedGameCount(m_curTitle);
		int nRowCount = (nSavedGames + m_iconsPerRow - 1) / m_iconsPerRow;

		{
			float y = -RenderLoop(false);

			if (y != m_scrollTo)
			{
				if (bInstantScroll)
				{
					m_scrollTo = m_scroll = y;
					m_timeScroll = 0.001f;
				}
				else
				{
					m_scrollTo = y;
					m_timeScroll = TheseusGetNow();
				}
			}
		}

		// Select the right save icon based on the preferred column and
		// the direction the selection moved..
		if ((m_nPrefColumn > -1) && (m_curGridItem != -1))
		{
			m_nPrefColumn = m_curGridItem % m_iconsPerRow;
		}
		else if (m_nPrefColumn != -1)
		{
			m_nPrefColumn = 0;
		}

		if (m_nPrefColumn == -1)
		{
			m_curGridItem = -1;
			m_iconRowScroll = 0;
		}
		else
		{
			if (m_curTitle > m_curTitleLast)
			{
				m_curGridItem = m_nPrefColumn;

				if (m_curGridItem > nSavedGames - 1)
					m_curGridItem = nSavedGames - 1;

				m_iconRowScroll = 0;
			}
			else
			{
				if (nSavedGames == 0)
				{
					m_curGridItem = -1;
				}
				else
				{
					m_curGridItem = m_nPrefColumn + (nRowCount - 1) * m_iconsPerRow;
					ASSERT(m_curGridItem >= 0);
					if (m_curGridItem > nSavedGames - 1)
						m_curGridItem = nSavedGames - 1;
				}

				m_iconRowScroll = nRowCount - VISIBLE_ICON_ROWS;
				if (m_iconRowScroll < 0)
					m_iconRowScroll = 0;
			}
		}
	}
}

void CSavedGameGrid::NotifySelectionChanged()
{
	CallFunction(this, _T("OnSelChange"));
	g_pulseStartTime = TheseusGetNow();
}

void CSavedGameGrid::selectUp()
{
	// First try moving up within the current title's icon grid.
	if (m_curGridItem != -1)
	{
		const int curRow = m_curGridItem / m_iconsPerRow;
		if (curRow > 0)
		{
			const int newRow = curRow - 1;
			m_curGridItem = newRow * m_iconsPerRow + m_nPrefColumn;
			if (newRow < m_iconRowScroll)
				m_iconRowScroll = newRow;
			m_nPrefColumn = m_curGridItem % m_iconsPerRow;
			NotifySelectionChanged();
			return;
		}
	}

	// At the top of the grid (or no grid item selected): walk up to the
	// previous title row. The MU selector view starts at row 0 instead
	// of -1 because there's no "device" pseudo-row above it.
	const int topRow = IsMUSelector() ? 0 : -1;
	if (m_curTitle > topRow)
	{
		SelectTitle(m_curTitle - 1);
		NotifySelectionChanged();
	}
	m_nPrefColumn = m_curGridItem % m_iconsPerRow;
}

void CSavedGameGrid::selectDown()
{
	// First try moving down within the current title's icon grid.
	if (m_curGridItem != -1)
	{
		const int totalItems = GetSavedGameCount(m_curTitle) + GetDLContentCount(m_curTitle);
		const int curRow = m_curGridItem / m_iconsPerRow;
		const int rowCount = (totalItems + m_iconsPerRow - 1) / m_iconsPerRow;

		if (curRow < rowCount - 1)
		{
			const int newRow = curRow + 1;
			m_curGridItem += m_iconsPerRow;
			// Bottom row may be partially filled; clamp to last valid item.
			if (m_curGridItem > totalItems - 1)
				m_curGridItem = totalItems - 1;
			if (newRow >= m_iconRowScroll + VISIBLE_ICON_ROWS - 1)
				m_iconRowScroll = newRow - (VISIBLE_ICON_ROWS - 1);
			m_nPrefColumn = m_curGridItem % m_iconsPerRow;
			NotifySelectionChanged();
			return;
		}
	}

	// At the bottom of the grid: walk down to the next title row.
	if (m_curTitle < GetTitleCount() - 1)
	{
		SelectTitle(m_curTitle + 1);
		NotifySelectionChanged();
	}
	m_nPrefColumn = m_curGridItem % m_iconsPerRow;
}

void CSavedGameGrid::selectLeft()
{
	const int oldTitle = m_curTitle;
	const int oldGridItem = m_curGridItem;

	if (m_curGridItem >= 0)
	{
		if (m_nPrefColumn == 0)
		{
			// Leftmost column: jump out of the grid back to the title orb.
			m_nPrefColumn = -1;
			m_curGridItem = -1;
			m_iconRowScroll = 0;
		}
		else
		{
			m_curGridItem -= 1;
			m_nPrefColumn = m_curGridItem % m_iconsPerRow;
		}
	}

	if (m_curTitle != oldTitle || m_curGridItem != oldGridItem)
		NotifySelectionChanged();
}

void CSavedGameGrid::selectRight()
{
	const int oldTitle = m_curTitle;
	const int oldGridItem = m_curGridItem;

	const int totalItems = GetSavedGameCount(m_curTitle) + GetDLContentCount(m_curTitle);
	if (m_curGridItem < totalItems - 1 && m_nPrefColumn < VISIBLE_ICON_ROWS)
	{
		m_curGridItem += 1;
		m_nPrefColumn = m_curGridItem % m_iconsPerRow;

		// Scroll the icon view if we just moved off the bottom-visible row.
		if (m_curGridItem >= (m_iconRowScroll + VISIBLE_ICON_ROWS) * m_iconsPerRow)
			m_iconRowScroll = m_curGridItem / m_iconsPerRow - (VISIBLE_ICON_ROWS - 1);
	}

	if (m_curTitle != oldTitle || m_curGridItem != oldGridItem)
		NotifySelectionChanged();
}

void CSavedGameGrid::setSelImage()
{
	g_szSelTitleImage = NULL;

	if (m_curTitle == -1) // Memory Unit
	{
		_tcscpy(szSelectionBuf, _T("memoryUnit128.tga"));
	}
	else if (m_curTitle >= 0 && (m_soundtrackTitle < 0 || m_curTitle < m_soundtrackTitle)) // Game title
	{
		if (m_curGridItem != -1)
		{
			int nSavedGames = GetSavedGameCount(m_curTitle);
			if (m_curGridItem < nSavedGames)
			{
				g_titles[m_curDevUnit].GetSavedGameImageName(m_curTitle, m_curGridItem, szSelectionBuf);
			}
			else
			{
				// DLC item: locate the Nth DLC folder and load its 128px image.
				char titleIDChar[9];
				char dlcFolder[17];
				if (ResolveDLCFolder(m_curTitle, m_curGridItem - nSavedGames,
				                     titleIDChar, sizeof(titleIDChar),
				                     dlcFolder, sizeof(dlcFolder)))
				{
					char dlcImagePath[MAX_PATH];
					_snprintf(dlcImagePath, MAX_PATH, "E:\\TDATA\\%s\\$c\\%s\\ContentImage.xbx",
					          titleIDChar, dlcFolder);
					MultiByteToWideChar(CP_ACP, 0, dlcImagePath, -1, szSelectionBuf, MAX_PATH);
				}
				else
				{
					_tcscpy(szSelectionBuf, _T("dlcitem128.xbx"));
				}
			}
		}
		else // Title image
		{
			MakePath(szSelectionBuf, g_titles[m_curDevUnit].GetUData(), GetTitleID2(m_curTitle));
			MakePath(szSelectionBuf, szSelectionBuf, szTitleImageXBX);

			if (!DoesFileExist(szSelectionBuf))
			{
				_tcscpy(szSelectionBuf, _T("xboxlogo128.xbx"));
			}
		}
	}
	else // soundtrack
	{
		_tcscpy(szSelectionBuf, _T("soundtracksave3.tga"));
	}

	g_szSelTitleImage = szSelectionBuf;
}

// ContentMeta.xbx is a UTF-16 INI-like blob with [language] sections each
// containing a Name= entry. This walks the file as raw 16-bit chars,
// finds the section matching the current dashboard language (or "default"),
// and copies the matching Name value into outName. Returns false if no
// matching section is found.
bool ContentMetaParse(const TCHAR *wszMetaPath, TCHAR *outName, int outLen)
{
	FILE *fp = _wfopen(wszMetaPath, L"rb");
	if (!fp)
		return false;

	BYTE buf[2048];
	size_t bytesRead = fread(buf, 1, sizeof(buf), fp);
	fclose(fp);

	TCHAR szLangCode[MAX_LANGUAGE_CODE_LEN];
	GetLanguageCode(szLangCode); // use dash language code, fallback to "default". this might be an issue if no default name is set.

	char langCodeA[32];
	WideCharToMultiByte(CP_ACP, 0, szLangCode, -1, langCodeA, sizeof(langCodeA), NULL, NULL);
	_strlwr(langCodeA); // to lowercase

	bool inSection = false;
	bool matchLang = false;

	for (size_t i = 0; i < bytesRead - 2; i += 2)
	{
		if (buf[i + 1] == 0 && buf[i] >= 32 && buf[i] <= 126)
		{
			size_t start = i;
			while (i + 1 < bytesRead && buf[i + 1] == 0 && buf[i] >= 32 && buf[i] <= 126)
				i += 2;

			size_t len = i - start;
			char asciiBuf[256] = {};
			for (size_t j = 0; j < len && j < sizeof(asciiBuf) - 1; j += 2)
				asciiBuf[j / 2] = buf[start + j];

			asciiBuf[len / 2] = '\0';
			const char *str = asciiBuf;

			if (str[0] == '[' && str[strlen(str) - 1] == ']')
			{
				// Normalize and compare
				char sectionLang[32] = {};
				strncpy(sectionLang, str + 1, strlen(str) - 2); // strip brackets
				sectionLang[strlen(str) - 2] = '\0';
				_strlwr(sectionLang);

				matchLang = (_stricmp(langCodeA, sectionLang) == 0 || strcmp(sectionLang, "default") == 0);
				inSection = true;
			}
			else if (matchLang && inSection && _strnicmp(str, "Name=", 5) == 0)
			{
				const char *nameVal = str + 5;
				MultiByteToWideChar(CP_ACP, 0, nameVal, -1, outName, outLen);
				return true;
			}
		}
	}

	return false;
}


CStrObject *CSavedGameGrid::FormatGridItemName()
{
	if (m_curGridItem < 0)
		return new CStrObject; // empty string

	if (m_soundtrackTitle >= 0 && m_curTitle == m_soundtrackTitle)
		return new CStrObject(GetSoundtrackName(m_curGridItem));

	// Calculate total saved games
	int savedGameCount = GetSavedGameCount(m_curTitle);
	int dlcCount = GetDLContentCount(m_curTitle);

	// Set DLC flag based on grid index
	m_isCurGridItemDLC = (m_curGridItem >= savedGameCount && m_curGridItem < savedGameCount + dlcCount);

	// If it's a DLC grid item, locate the Nth DLC folder and parse its
	// ContentMeta.xbx for a localized name. Three fallbacks: parse failure
	// returns "Unnamed DLC", missing folder returns "Downloadable Content N".
	if (m_isCurGridItemDLC)
	{
		int dlcIndex = m_curGridItem - savedGameCount;
		char titleIDChar[9];
		char dlcFolder[17];

		if (ResolveDLCFolder(m_curTitle, dlcIndex,
		                     titleIDChar, sizeof(titleIDChar),
		                     dlcFolder, sizeof(dlcFolder)))
		{
			char metaPath[MAX_PATH];
			_snprintf(metaPath, MAX_PATH, "E:\\TDATA\\%s\\$c\\%s\\ContentMeta.xbx",
			          titleIDChar, dlcFolder);

			TCHAR wszMetaPath[MAX_PATH];
			MultiByteToWideChar(CP_ACP, 0, metaPath, -1, wszMetaPath, MAX_PATH);

			TCHAR szSaveName[64];
			if (ContentMetaParse(wszMetaPath, szSaveName, countof(szSaveName)))
				return new CStrObject(szSaveName);

			TCHAR szTranslate[MAX_TRANSLATE_LEN] = _T("");
			return new CStrObject(Translate(_T("Unnamed DLC"), szTranslate));
		}

		// Fallback: numbered placeholder when no DLC folder exists for this index
		TCHAR szBuf[64];
		_stprintf(szBuf, _T("Downloadable Content %d"), dlcIndex + 1);
		return new CStrObject(szBuf);
	}

	// Normal saved game
	TCHAR szSavedGamePath[MAX_PATH];
	if (!GetSavedGamePath(szSavedGamePath, m_curTitle, m_curGridItem))
	{
		//OutputDebugStringA("[SavedGameGrid] FormatGridItemName: Failed to get save path\n");
		return new CStrObject; // empty string
	}

	MakePath(szSavedGamePath, szSavedGamePath, szSaveDataXBX);

	TCHAR szSaveName[64];
	TCHAR szLangCode[MAX_LANGUAGE_CODE_LEN];
	TCHAR szTranslate[MAX_TRANSLATE_LEN];
	CSettingsFile settings;
	if (!settings.Open(szSavedGamePath) || !settings.GetValue(GetLanguageCode(szLangCode), _T("Name"), szSaveName, countof(szSaveName)))
	{
		//OutputDebugStringA("[SavedGameGrid] FormatGridItemName: Broken or missing name field\n");
		return new CStrObject(Translate(_T("Broken Save"), szTranslate));
	}

	return new CStrObject(szSaveName);
}

CStrObject *CSavedGameGrid::FormatGridItemTime()
{
	if (m_soundtrackTitle >= 0 && m_curTitle == m_soundtrackTitle)
		return new CStrObject; // empty string

	TCHAR szPath[MAX_PATH];
	if (m_curGridItem < 0 || !GetSavedGamePath(szPath, m_curTitle, m_curGridItem))
		return new CStrObject; // empty string

	HANDLE hFile = TheseusCreateFile(szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return new CStrObject; // empty string

	FILETIME ft;
	VERIFY(GetFileTime(hFile, NULL, NULL, &ft));
	CloseHandle(hFile);

	FILETIME lft;
	VERIFY(FileTimeToLocalFileTime(&ft, &lft));

	SYSTEMTIME st;
	VERIFY(FileTimeToSystemTime(&lft, &st));

	TCHAR szBuf[32];
	FormatTime(szBuf, countof(szBuf), &st);

	return new CStrObject(szBuf);
}

int CSavedGameGrid::GetSavedGameCount(int nTitle)
{
	ASSERT(IsValidDevUnit());

	if (nTitle < 0)
		return 0;

	if (g_titles[m_curDevUnit].IsDirty())
	{
		return 0;
	}

	if (m_soundtrackTitle < 0 || nTitle < m_soundtrackTitle)
		return g_titles[m_curDevUnit].GetSavedGameCount(nTitle);

	return m_soundtrackCount;
}

CStrObject *CSavedGameGrid::FormatGridItemSize()
{
	int nBlocks;

	if (m_soundtrackTitle >= 0 && m_curTitle == m_soundtrackTitle)
	{
		nBlocks = GetSoundtrackSize(m_curGridItem);
	}
	else
	{
		TCHAR szSavedGameDir[MAX_PATH];

		if (m_curGridItem < 0 || !GetSavedGamePath(szSavedGameDir, m_curTitle, m_curGridItem))
			return new CStrObject; // empty string

		nBlocks = m_cacheSavedGameSize;
	}

	m_gameBlocks = nBlocks;

	TCHAR szBuf[16];
	FormatInteger(szBuf, nBlocks);
	return new CStrObject(szBuf);
}

CStrObject *CSavedGameGrid::FormatTitleSize()
{
	int nBlocks = m_cacheTitleSize;

	TCHAR szBuf[16];
	FormatInteger(szBuf, nBlocks);
	return new CStrObject(szBuf);
}

bool CSavedGameGrid::GetSavedGamePath(TCHAR *szBuf, int nTitle, int nSavedGame)
{
	if (!g_titles[m_curDevUnit].IsValid())
	{
		return false;
	}

	MakePath(szBuf, g_titles[m_curDevUnit].GetUData(), GetTitleID2(nTitle));
	MakePath(szBuf, szBuf, GetSavedGameID2(nTitle, nSavedGame));
	return true;
}

CStrObject *CSavedGameGrid::GetSavedGamePath(int nTitle, int nSavedGame)
{
	TCHAR szBuf[MAX_PATH];

	szBuf[0] = IsHDD() ? 'U' : g_titles[m_curDevUnit].GetUData()[0];
	szBuf[1] = ':';
	szBuf[2] = '\\';
	_tcscpy(szBuf + 3, GetSavedGameID2(nTitle, nSavedGame));

	return new CStrObject(szBuf);
}

CStrObject *CSavedGameGrid::FormatFreeBlocks()
{
	TCHAR szBuf[16];
	FormatBlocks(szBuf, m_freeBlocks);
	return new CStrObject(szBuf);
}

CStrObject *CSavedGameGrid::FormatTotalBlocks()
{
	TCHAR szBuf[16];
	FormatBlocks(szBuf, m_totalBlocks);
	return new CStrObject(szBuf);
}

int CSavedGameGrid::GetTotalBlocks()
{
	return m_totalBlocks;
}

int CSavedGameGrid::CanDetachIcon()
{
	return m_timeScroll == 0.0f && m_focusPodMatrixValid;
}

int CSavedGameGrid::CountSupportFileIfMissing(int destDevUnit, const TCHAR *fileName)
{
	TCHAR path[MAX_PATH];

	MakePath(path, g_titles[destDevUnit].GetUData(), GetTitleID2(m_curTitle));
	MakePath(path, path, fileName);

	if (DoesFileExist(path))
		return 0;

	MakePath(path, g_titles[m_curDevUnit].GetUData(), GetTitleID2(m_curTitle));
	MakePath(path, path, fileName);
	return GetFileBlocks(path, BLOCK_SIZE);
}

void CSavedGameGrid::StartCopy(int destDevUnit)
{
	if (m_curGridItem == -1)
	{
		TRACE(_T("Attempted to copy a title!\n"));
		CallFunction(this, _T("OnCopyComplete"));
		return;
	}

	// Total block budget for the copy: the saved game directory itself,
	// plus any per-title support XBX files the destination is missing.
	TCHAR szBuf[MAX_PATH];
	VERIFY(GetSavedGamePath(szBuf, m_curTitle, m_curGridItem));
	int nBlocks = GetDirectoryBlocks(szBuf, BLOCK_SIZE, true, NULL);

	nBlocks += CountSupportFileIfMissing(destDevUnit, szTitleDataXBX);
	nBlocks += CountSupportFileIfMissing(destDevUnit, szTitleImageXBX);
	nBlocks += CountSupportFileIfMissing(destDevUnit, szSaveImageXBX);

	m_copying = true;
	m_timeCopyingStarted = TheseusGetNow();
	m_copyProgress = 0.0f;
	m_lastUpdateTick = GetTickCount();
	m_periodStatus = 0;
	m_statusText[0] = 0;
	Translate(_T("Copying"), m_statusText);

	theGameCopier.SetSource(m_curDevUnit);
	theGameCopier.SetDestination(destDevUnit);
	theGameCopier.AddGame(GetTitleID2(m_curTitle),
	                      GetSavedGameID2(m_curTitle, m_curGridItem),
	                      GetSavedGameTime(m_curTitle, m_curGridItem),
	                      nBlocks);
	theGameCopier.Start();
}

static DWORD ParseTitleID(const TCHAR *szID)
{
	if (_tcslen(szID) != 8)
		return 0xffffffff; // Invalid ID

	DWORD dw = 0;
	const TCHAR *pch = szID;
	while (*pch != 0)
	{
		DWORD dwDigit;

		if (*pch >= '0' && *pch <= '9')
			dwDigit = *pch - '0';
		else if (*pch >= 'a' && *pch <= 'f')
			dwDigit = 10 + *pch - 'a';
		else if (*pch >= 'A' && *pch <= 'F')
			dwDigit = 10 + *pch - 'A';
		else
			return 0xffffffff; // Invalid ID

		dw = (dw << 4) + dwDigit;

		pch += 1;
	}

	return dw;
}

void CSavedGameGrid::DeleteGameTitleFiles()
{
	TCHAR titleID[MAX_PATH];
	TCHAR publisherID[16];
	TCHAR path[MAX_PATH];

	lstrcpyn(titleID, GetTitleID2(m_curTitle), countof(titleID));
	lstrcpyn(publisherID, titleID, countof(publisherID));

	// Truncate to the 4-character publisher prefix.
	publisherID[4] = 0;

	g_titles[m_curDevUnit].RemoveTitle(m_curTitle);

	// If this was the last title from this publisher, sweep the shared
	// publisher directory ("<pub>ffff" under TDATA).
	if (!g_titles[m_curDevUnit].IsPublisherExists(publisherID))
	{
		lstrcat(publisherID, _T("ffff"));
		MakePath(path, g_titles[m_curDevUnit].GetTData(), publisherID);
		theGameCopier.DeleteDirectory(path);
	}

	// Delete the title's UDATA tree (saved games + title metadata).
	MakePath(path, g_titles[m_curDevUnit].GetUData(), titleID);
	theGameCopier.DeleteDirectory(path);

	if (IsHDD())
	{
		// HDD only: also wipe TDATA and the title's cache partition.
		MakePath(path, g_titles[m_curDevUnit].GetTData(), titleID);
		theGameCopier.DeleteDirectory(path);
		XapiDeleteCachePartition(ParseTitleID(titleID));
	}

	if (m_soundtrackTitle >= 0)
		m_soundtrackTitle -= 1;
}

void CSavedGameGrid::DeleteCurrentTitle()
{
	if (m_curTitle == m_soundtrackTitle)
	{
		DeleteAllSoundtracks();
		m_soundtrackTitle = -1;
		m_soundtrackCount = 0;
	}
	else
	{
		DeleteGameTitleFiles();
	}

	m_timeOfDelete = TheseusGetNow();
	m_deletedTitle = m_curTitle;
	ASSERT(m_focusPodMatrixValid);

	SelectTitle(m_curTitle, true);
}

void CSavedGameGrid::DeleteCurrentGridItem()
{
	if (m_curTitle == m_soundtrackTitle)
	{
		DeleteSoundtrack(m_curGridItem);
		m_soundtrackCount -= 1;
	}
	else
	{
		TCHAR path[MAX_PATH];
		VERIFY(GetSavedGamePath(path, m_curTitle, m_curGridItem));

		theGameCopier.m_error = false;
		theGameCopier.DeleteDirectory(path);

		g_titles[m_curDevUnit].RemoveSavedGame(m_curTitle, m_curGridItem);
	}

	// Fixup selection within the (now smaller) title.
	int nSavedGameCount = GetSavedGameCount(m_curTitle);
	if (m_curGridItem > nSavedGameCount - 1)
		m_curGridItem = nSavedGameCount - 1;
	m_nPrefColumn = (m_curGridItem % m_iconsPerRow);

	if (m_iconRowScroll && ((m_iconRowScroll + VISIBLE_ICON_ROWS - 1) * m_iconsPerRow >= nSavedGameCount))
	{
		m_iconRowScroll--;
	}
}

void WINAPI CSavedGameGrid::DeleteThread(CSavedGameGrid *p)
{
	ASSERT(p->m_curTitle >= 0);

	if (p->m_curTitle == p->m_soundtrackTitle && p->m_soundtrackCount == 1)
		p->m_curGridItem = -1;

	if (p->m_curGridItem == -1)
		p->DeleteCurrentTitle();
	else
		p->DeleteCurrentGridItem();

	p->m_cacheTitleSize = -1;
	p->m_cacheSavedGameSize = -1;
}

void CSavedGameGrid::StartDelete()
{
	ASSERT(m_deleteThread == NULL);
	ASSERT(!m_deleting);

	// Cancel previous pending query
	if (m_savedGameQueryPending)
	{
		SignalObjectAndWait(m_cancelEvent, m_stopEvent, INFINITE, FALSE);
	}

	m_deleteThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)DeleteThread, this, 0, 0);

	if (!m_deleteThread)
	{
		DeleteThread(this);
		VERIFY(CallFunction(this, _T("OnDeleteEnd")));
		VERIFY(CallFunction(this, _T("OnSelChange")));
		g_pulseStartTime = TheseusGetNow();
	}
	else
	{
		m_deleting = true;
	}
}

int CSavedGameGrid::CanCopy()
{
	if (m_curTitle < 0 || IsSoundtrackSelected())
		return false; // can't copy devices or soundtracks

	TCHAR szSavedGamePath[MAX_PATH];

	if (m_curGridItem < 0 || !GetSavedGamePath(szSavedGamePath, m_curTitle, m_curGridItem))
		return false; // can't copy saves when one isn't selected

	MakePath(szSavedGamePath, szSavedGamePath, szSaveDataXBX);

	CSettingsFile settings;
	if (!settings.Open(szSavedGamePath))
		return false; // can't copy broken saves

	TCHAR szSaveName[64];
	TCHAR szLangCode[MAX_LANGUAGE_CODE_LEN];
	if (!settings.GetValue(GetLanguageCode(szLangCode), _T("Name"), szSaveName, countof(szSaveName)))
		return false; // can't copy broken saved game

	if (g_titles[m_curDevUnit].IsBroken(m_curTitle))
		return false; // can't copy a saved game in a broken title

	TCHAR szNoCopy[64];
	if (!settings.GetValue(GetLanguageCode(szLangCode), _T("NoCopy"), szNoCopy, countof(szNoCopy)))
		return true; // can copy if it doesn't say otherwise

	return _ttoi(szNoCopy) == 0;
}

int CSavedGameGrid::IsSoundtrackSelected()
{
	return (m_soundtrackTitle >= 0 && m_curTitle == m_soundtrackTitle);
}

int CSavedGameGrid::IsDevUnitReady(int nUnit)
{
	return !g_titles[nUnit].IsDirty();
}

int CSavedGameGrid::DoesSavedGameExists(int destDevUnit)
{
	if (m_curGridItem < 0)
	{
		return false;
	}

	const TCHAR *szRoot = g_titles[destDevUnit].GetUData();
	ASSERT(szRoot[0] != 0);

	TCHAR szBuf[MAX_PATH];
	MakePath(szBuf, g_titles[destDevUnit].GetUData(), GetTitleID2(m_curTitle));
	MakePath(szBuf, szBuf, GetSavedGameID2(m_curTitle, m_curGridItem));

	return DoesFileExist(szBuf);
}
static int ScanDLContentCount(const char *titleID)
{
	char dlcPath[MAX_PATH];
	_snprintf(dlcPath, MAX_PATH, "E:\\TDATA\\%s\\$c\\*", titleID);

	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(dlcPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;

	int dlcCount = 0;
	do
	{
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    strcmp(findData.cFileName, ".") != 0 &&
		    strcmp(findData.cFileName, "..") != 0 &&
		    strlen(findData.cFileName) == 16 &&
		    strncmp(findData.cFileName, titleID, 8) == 0)
		{
			dlcCount++;
		}
	} while (FindNextFileA(hFind, &findData));

	FindClose(hFind);
	return dlcCount;
}

int CSavedGameGrid::GetDLContentCount(int nTitle)
{
	if (nTitle < 0 || m_curDevUnit != Dev0)
		return 0;

	if (m_dlcCountCacheSize == 0)
	{
		int titleCount = g_titles[m_curDevUnit].GetTitleCount();
		if (titleCount > 256)
			titleCount = 256;

		for (int i = 0; i < titleCount; i++)
		{
			char titleID[9];
			if (GetTitleIDAnsi(i, titleID, sizeof(titleID)))
				m_dlcCountCache[i] = ScanDLContentCount(titleID);
			else
				m_dlcCountCache[i] = 0;
		}
		m_dlcCountCacheSize = titleCount;
	}

	if (nTitle >= m_dlcCountCacheSize)
		return 0;

	return m_dlcCountCache[nTitle];
}

// =============================================================================
// 5960 additions: Xbox Live content management in the save grid
// =============================================================================
// The 5960 grid layout for each title is: [saves][DLC][Live accounts]
// Grid item indices are sequential across all three content types.
// Live accounts are loaded by CLiveAccounts and live in g_Users[]/g_NumUsers.

extern "C" { extern DWORD g_NumUsers; }

int CSavedGameGrid::IsXboxLiveAccountSelected()
{
	// Returns 1 if curTitle points to the Xbox Live accounts entry.
	// m_xboxLiveAccountTitle is the title index where Live accounts appear,
	// or -1 if no Live accounts entry exists on the current device.
	return (m_xboxLiveAccountTitle >= 0 && m_curTitle == m_xboxLiveAccountTitle) ? 1 : 0;
}

int CSavedGameGrid::IsGameTitleSelected()
{
	// Returns 1 if curTitle is a real game title (not soundtracks, not Live accounts).
	if (m_curTitle < 0)
		return 0;
	if (m_soundtrackTitle >= 0 && m_curTitle == m_soundtrackTitle)
		return 0;
	if (m_xboxLiveAccountTitle >= 0 && m_curTitle == m_xboxLiveAccountTitle)
		return 0;
	return 1;
}

int CSavedGameGrid::DoesUserContentExist()
{
	// Returns 1 if the current title has any user content (saves or DLC).
	if (m_curTitle < 0)
		return 0;
	int nSaves = GetSavedGameCount(m_curTitle);
	int nDLC = GetDLContentCount(m_curTitle);
	return (nSaves + nDLC > 0) ? 1 : 0;
}

int CSavedGameGrid::GetXboxLiveAccountsCount(int nTitle)
{
	// On retail 5960: reads from the in-memory title enumeration struct.
	// Xbox Live accounts are system-wide (not per-title), loaded by
	// CLiveAccounts via _XOnlineGetUsersFromHD into g_Users[]/g_NumUsers.
	// The grid only surfaces the Live accounts entry on the synthetic
	// MU-selector view (devUnit 8), where the user picks a destination.
	if (!IsMUSelector())
		return 0;

	return (int)g_NumUsers;
}

int CSavedGameGrid::GetGridItemCount(int nTitle)
{
	// Returns total grid items for a title: saves + DLC + Live accounts.
	// This is what the grid uses to determine how many items to show.
	if (nTitle < 0)
		return 0;

	// Don't count for soundtracks (they use a different system)
	if (m_soundtrackTitle >= 0 && nTitle >= m_soundtrackTitle)
		return 0;

	int nCount = GetSavedGameCount(nTitle);
	nCount += GetDLContentCount(nTitle);
	nCount += GetXboxLiveAccountsCount(nTitle);
	return nCount;
}

int CSavedGameGrid::DoesXboxLiveAccountExists(int nTitle)
{
	// Returns 1 if any Xbox Live accounts are stored on the device.
	// Scripts use this to show/hide the "Xbox Live Accounts" panel.
	// Accounts are system-wide, so nTitle is ignored; just check g_NumUsers.
	if (!IsMUSelector())
		return 0;

	return (g_NumUsers > 0) ? 1 : 0;
}

int CSavedGameGrid::IsSavedGameSelected()
{
	// Returns 1 if the currently selected grid item is a saved game
	// (not DLC and not a Live account).
	// Grid layout: [0..nSaves-1] = saves, [nSaves..nSaves+nDLC-1] = DLC,
	//              [nSaves+nDLC..] = Live accounts
	if (m_curTitle < 0 || m_curGridItem < 0)
		return 0;

	if (IsSoundtrackSelected())
		return 0;

	int nSaveCount = GetSavedGameCount(m_curTitle);
	return (m_curGridItem < nSaveCount) ? 1 : 0;
}

int CSavedGameGrid::IsDLContentSelected()
{
	// Returns 1 if the currently selected grid item is downloadable content.
	if (m_curTitle < 0 || m_curGridItem < 0)
		return 0;

	if (IsSoundtrackSelected())
		return 0;

	int nSaveCount = GetSavedGameCount(m_curTitle);
	int nDLCCount = GetDLContentCount(m_curTitle);

	return (m_curGridItem >= nSaveCount &&
	        m_curGridItem < nSaveCount + nDLCCount) ? 1 : 0;
}

int CSavedGameGrid::IsDLContentPartial()
{
	// Returns 1 if the selected DLC is an incomplete/partial download.
	// On retail: checks XONLINEOFFERING_PARTIAL flag on the content.
	// Here: a complete DLC has ContentMeta.xbx alongside its data, a
	// partial download is missing it.
	if (!IsDLContentSelected())
		return 0;

	int nSaveCount = GetSavedGameCount(m_curTitle);
	int nDLCIndex = m_curGridItem - nSaveCount;

	char titleIDChar[9];
	char dlcFolder[17];
	if (!ResolveDLCFolder(m_curTitle, nDLCIndex,
	                      titleIDChar, sizeof(titleIDChar),
	                      dlcFolder, sizeof(dlcFolder)))
		return 0;

	// Check if ContentMeta.xbx exists for this DLC folder.
	char metaPath[MAX_PATH];
	_snprintf(metaPath, MAX_PATH, "E:\\TDATA\\%s\\$c\\%s\\ContentMeta.xbx",
	          titleIDChar, dlcFolder);

	WIN32_FIND_DATAA metaData;
	HANDLE hMeta = FindFirstFileA(metaPath, &metaData);
	if (hMeta == INVALID_HANDLE_VALUE)
		return 1; // No ContentMeta = partial download
	FindClose(hMeta);
	return 0; // ContentMeta exists = complete
}

void CSavedGameGrid::StartSavedGameCopy(int destDevUnit)
{
	// 5960 renamed StartCopy to StartSavedGameCopy for save-specific copying.
	// Same implementation; copies the selected saved game to destination device.
	StartCopy(destDevUnit);
}

void CSavedGameGrid::StartXboxLiveAccountCopy(int destDevUnit)
{
	// Copies an Xbox Live account to the destination device (MU).
	// On retail: uses XOnline APIs to write account credentials to MU.
	// The account data (XONLINE_USER struct) is in g_Users[].
	if (m_curGridItem == -1)
	{
		CallFunction(this, _T("OnCopyError"));
		return;
	}

	int nSaveCount = GetSavedGameCount(m_curTitle);
	int nDLCCount = GetDLContentCount(m_curTitle);
	int nAccountIndex = m_curGridItem - nSaveCount - nDLCCount;

	if (nAccountIndex < 0 || nAccountIndex >= (int)g_NumUsers)
	{
		OutputDebugString(_T("[SavedGameGrid] StartXboxLiveAccountCopy: invalid account index\n"));
		CallFunction(this, _T("OnCopyError"));
		return;
	}

	// TODO: Write g_Users[nAccountIndex] to destination MU via XOnline APIs
	// For Insignia, this would persist the account to the MU filesystem
	TCHAR buf[128];
	_sntprintf(buf, countof(buf), _T("[SavedGameGrid] StartXboxLiveAccountCopy: account %d -> devUnit %d (not yet implemented)\n"),
	           nAccountIndex, destDevUnit);
	OutputDebugString(buf);
	CallFunction(this, _T("OnCopyError"));
}
