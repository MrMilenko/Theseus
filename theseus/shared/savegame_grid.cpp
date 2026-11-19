// savegame_grid.cpp: SavedGameGrid, the Memory blade's list of titles on a
// device with a grid of save icons under each. Lays the list out, moves
// the selection, sizes things on a worker thread and starts copies and
// deletes. Rebuilt from the 5960 retail XBE (0x47320..0x4bc60); see
// docs/decomp/SavedGameGrid.md. DLC items, the ContentMeta reader and the
// Live account calls below are Theseus's own.


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
	int m_sizedTitle;
	int m_sizedItem;
	int m_iconRowScroll;
	float m_smallIconSpacing;
	bool m_detachIcon;
	int m_curDevUnit;
	float m_copyProgress;
	int m_freeBlocks;
	int m_gameBlocks;
	bool m_isActive;
	bool m_listing;
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
	int m_shownLanguage;
	int m_deviceBlocks;
	float m_scrollTarget;
	XTIME m_scrollStart;
	int m_prevTitle;
	bool m_copyRunning;
	XTIME m_copyStart;
	bool m_copyFailed;
	int m_fadingTitle;
	XTIME m_fadeStart;
	int m_soundtrackRow;
	int m_soundtracks;
	int m_liveAccountRow; // 5960: index in title list where Live accounts start (-1 = none)

	int m_titleBlocks;
	int m_itemBlocks;

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

	DWORD m_dotsTick;
	int m_dots;
	TCHAR m_copyingText[MAX_TRANSLATE_LEN];

	DECLARE_NODE_PROPS();
	DECLARE_NODE_FUNCTIONS();

private:
	HANDLE m_listThread;
	HANDLE m_sizeThread;
	HANDLE m_sizeCancel;
	HANDLE m_sizeStart, m_sizeIdle;
	bool m_sizing;
	volatile bool m_sizeQuit;
	XTIME m_listEndAt;
	static void WINAPI TitlesEnumThread(CSavedGameGrid *p);
	static void WINAPI SavedGameEnumThread(CSavedGameGrid *p);

	// Background delete thread and flag
	HANDLE m_deleter;
	bool m_deleteRunning;
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
	bool GetDLCImagePath(int nTitle, int dlcIndex, TCHAR *out);

	// Device-unit classification. Dev units 0..7 are physical storage
	// (SLOT_HDD = HDD, 1..7 = MU slots). Dev unit 8 is the synthetic
	// "memory unit selector" view shown when the user is choosing which
	// MU to copy to/from.
	inline bool IsHDD() const         { return m_curDevUnit == SLOT_HDD; }
	inline bool IsMUSlot() const      { return m_curDevUnit > SLOT_HDD && m_curDevUnit < 8; }
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
	HANDLE hFind = TheseusFindFirstFile(searchPath, &findData);
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
	} while (TheseusFindNextFile(hFind, &findData));

	TheseusFindClose(hFind);
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

// =============================================================================
// Setup and title access
// =============================================================================

// CSavedGameGrid::CSavedGameGrid
// Reconstructed from 5960 retail XBE @ 0x47320. Every node pointer null,
// icons rendered four to a row, no device, title or item picked, sizes and
// counts unknown (-1), no scroll. Three events (start auto reset, stop and
// cancel manual reset) and the size worker thread are made up front.
// Theseus adds the focused pod matrix cache and the DLC count cache.
CSavedGameGrid::CSavedGameGrid()
{
	m_pod = m_podRing = m_podSavePanel = NULL;
	m_podXboxLiveAccountsPanel = m_XboxLiveAccountsIconPanel = NULL;
	m_iconBumpRing = m_iconArrow = m_iconCheck = NULL;
	m_podSoundtrackPanel = m_podHilite = NULL;
	m_MUheader = m_MUhiliteHeader = m_firstMURow = NULL;
	m_header = m_hiliteHeader = NULL;
	m_firstRow = m_secondRow = m_otherRow = NULL;
	m_smallIcon = m_SavedIconPanel = m_SoundtrackIconPanel = NULL;
	m_iconRing = m_smallIconHilite = NULL;
	m_moreUp = m_moreDown = NULL;

	m_renderIcons = true;
	m_iconsPerRow = 4;
	m_smallIconSpacing = 1.0f;
	m_detachIcon = false;
	m_isActive = false;
	m_listing = false;

	m_curDevUnit = -1;
	m_curTitle = -1;
	m_sizedTitle = -1;
	m_prevTitle = -1;
	m_curGridItem = -1;
	m_sizedItem = -1;
	m_nPrefColumn = -1;
	m_iconRowScroll = 0;
	m_scroll = 0.0f;
	m_scrollTarget = 0.0f;
	m_scrollStart = 0.0f;

	m_shownLanguage = 0;
	m_deviceBlocks = -1;
	m_freeBlocks = -1;
	m_gameBlocks = -1;
	m_titleBlocks = -1;
	m_itemBlocks = -1;

	m_copyRunning = false;
	m_copyStart = 0.0f;
	m_deleteRunning = false;
	m_deleter = NULL;
	m_fadingTitle = -1;
	m_fadeStart = 0.0f;

	m_soundtrackRow = -1;
	m_soundtracks = 0;
	m_liveAccountRow = -1;

	m_focusPodMatrixValid = false;
	D3DXMatrixIdentity(&m_focusPodMatrix);
	m_dlcCountCacheSize = 0;

	m_listThread = NULL;
	m_sizing = false;
	m_sizeQuit = false;
	m_sizeStart = CreateEvent(0, FALSE, FALSE, 0);
	m_sizeIdle = CreateEvent(0, TRUE, FALSE, 0);
	m_sizeCancel = CreateEvent(0, TRUE, FALSE, 0);
	m_sizeThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)SavedGameEnumThread, (LPVOID)this, 0, 0);
}

// CSavedGameGrid::~CSavedGameGrid
// Reconstructed from 5960 retail XBE @ 0x47520. Releases the 25 nodes the
// script handed it. If a size query is running the worker is told to quit
// and cancel; then it's waited for and the events and thread closed.
CSavedGameGrid::~CSavedGameGrid()
{
	CNode* held[] =
	{
		m_pod, m_podRing,
		m_podXboxLiveAccountsPanel, m_XboxLiveAccountsIconPanel, m_iconBumpRing, m_iconArrow, m_iconCheck,
		m_podSavePanel, m_podSoundtrackPanel, m_podHilite,
		m_MUheader, m_MUhiliteHeader, m_firstMURow, m_header, m_hiliteHeader,
		m_firstRow, m_secondRow, m_otherRow,
		m_smallIcon, m_SavedIconPanel, m_SoundtrackIconPanel, m_iconRing, m_smallIconHilite,
		m_moreUp, m_moreDown,
	};
	for (int i = 0; i < (int)countof(held); i++)
	{
		if (held[i] != NULL)
			held[i]->Release();
	}

	if (m_sizing)
	{
		m_sizeQuit = true;
		SetEvent(m_sizeCancel);
	}
	WaitForSingleObject(m_sizeThread, INFINITE);
	CloseHandle(m_sizeStart);
	CloseHandle(m_sizeIdle);
	CloseHandle(m_sizeCancel);
	CloseHandle(m_sizeThread);
}

// Whether a title index is a game rather than the soundtrack row.
#define IS_GAME_ROW(n) (m_soundtrackRow < 0 || (n) < m_soundtrackRow)

// CSavedGameGrid::GetTitleCount   [script fn 'GetTitleCount', FND 0x00187998, sig 2]
// Reconstructed from 5960 retail XBE @ 0x476f0. The device's titles, plus
// one soundtrack row after them on the hard drive when there are any
// soundtracks. 5960 also adds a Live accounts row; Theseus keeps those on
// the copy destination view instead.
int CSavedGameGrid::GetTitleCount()
{
	int titles = g_titles[m_curDevUnit].GetTitleCount();

	m_soundtracks = IsHDD() ? GetSoundtrackCount() : 0;
	m_soundtrackRow = IsHDD() ? titles : -1;

	if (IsHDD() && m_soundtracks > 0)
		titles++;
	return titles;
}

// Blocks a title row takes: the title's folders, or every soundtrack.
// 5960 @ 0x477b0.
inline int CSavedGameGrid::GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent)
{
	if (nTitle < 0)
		return 0;
	if (IS_GAME_ROW(nTitle))
		return g_titles[m_curDevUnit].GetTitleTotalBlocks(nTitle, hCancelEvent);
	return GetSoundtrackSize(-1);
}

inline const TCHAR *CSavedGameGrid::GetTitleID2(int nTitle)
{
	return g_titles[m_curDevUnit].GetTitleID(nTitle);
}

// CSavedGameGrid::GetTitleID   [script fn 'GetTitleID', FND 0x00187848, sig 4]
// 5960 @ 0x478d0: the title array's id as a string.
inline CStrObject *CSavedGameGrid::GetTitleID(int nTitle)
{
	return new CStrObject(GetTitleID2(nTitle));
}

// CSavedGameGrid::GetTitleName2(n, out)
// Reconstructed from 5960 retail XBE @ 0x47970. The device row (-1) is
// "Xbox Hard Disk", or the memory unit's own name, or failing that
// "memory unit <port><A|B>" from its slot. The soundtrack row is
// "Soundtracks"; anything else is the title's name.
void CSavedGameGrid::GetTitleName2(int nTitle, TCHAR *szBuf)
{
	TCHAR translated[MAX_TRANSLATE_LEN];

	if (nTitle >= 0)
	{
		if (IS_GAME_ROW(nTitle))
			_tcscpy(szBuf, g_titles[m_curDevUnit].GetTitleName(nTitle));
		else
			_tcscpy(szBuf, Translate(_T("Soundtracks"), translated));
		return;
	}

	if (IsHDD())
	{
		_tcscpy(szBuf, Translate(_T("Xbox Hard Disk"), translated));
		return;
	}

	FormatDeviceName(m_curDevUnit, szBuf);
	if (szBuf[0] != 0)
		return;

	TCHAR slot = ((m_curDevUnit + 1) % 2) ? _T('A') : _T('B');
	Translate(_T("memory unit"), translated);
	swprintf(szBuf, _T("%s %d%c"), translated, m_curDevUnit / 2 + 1, slot);
}

// CSavedGameGrid::GetTitleName   [script fn 'GetTitleName', FND 0x00187830, sig 4]
// 5960 @ 0x47ab0.
inline CStrObject *CSavedGameGrid::GetTitleName(int nTitle)
{
	TCHAR name[256];
	GetTitleName2(nTitle, name);
	return new CStrObject(name);
}

inline const TCHAR *CSavedGameGrid::GetSavedGameID2(int nTitle, int nSavedGame)
{
	return g_titles[m_curDevUnit].GetSavedGameID(nTitle, nSavedGame);
}

inline FILETIME CSavedGameGrid::GetSavedGameTime(int nTitle, int nSavedGame)
{
	return g_titles[m_curDevUnit].GetSavedGameTime(nTitle, nSavedGame);
}

// CSavedGameGrid::TitlesEnumThread
// Reconstructed from 5960 retail XBE @ 0x47bb0. Busy while the device's
// title list is rebuilt; on the hard drive the soundtrack row goes after
// the titles if there are soundtracks.
void WINAPI CSavedGameGrid::TitlesEnumThread(CSavedGameGrid *p)
{
	p->m_listing = true;
	g_titles[p->m_curDevUnit].Update();

	if (p->IsHDD() && p->m_soundtrackRow == -1)
	{
		int soundtracks = GetSoundtrackCount();
		if (soundtracks > 0)
		{
			p->m_soundtrackRow = p->GetTitleCount();
			p->m_soundtracks = soundtracks;
		}
	}
	p->m_listing = false;
}

// CSavedGameGrid::SavedGameEnumThread
// 5960 @ 0x47c70. Sleeps until Advance asks (start event), signaling stop
// while it waits, then fills whichever size is missing: the selected save
// (or soundtrack), else the selected title. The cancel event cuts a count
// short.
void WINAPI CSavedGameGrid::SavedGameEnumThread(CSavedGameGrid *p)
{
	while (!p->m_sizeQuit)
	{
		SignalObjectAndWait(p->m_sizeIdle, p->m_sizeStart, INFINITE, FALSE);
		ResetEvent(p->m_sizeIdle);

		if (p->m_itemBlocks < 0 && p->m_curTitle >= 0 && p->m_curGridItem >= 0)
		{
			if (p->m_soundtrackRow >= 0 && p->m_curTitle >= p->m_soundtrackRow)
			{
				p->m_itemBlocks = GetSoundtrackSize(p->m_curGridItem);
				continue;
			}

			TCHAR dir[MAX_PATH];
			p->m_itemBlocks = p->GetSavedGamePath(dir, p->m_curTitle, p->m_curGridItem)
				? GetDirectoryBlocks(dir, BLOCK_SIZE, true, p->m_sizeCancel) : 0;
		}
		else if (p->m_titleBlocks < 0)
		{
			p->m_titleBlocks = p->GetTitleTotalBlocks(p->m_curTitle, p->m_sizeCancel);
		}
	}
}

// Theseus's size cache upkeep (5960 does the same work inline in Advance):
// collect a finished query, drop the sizes the selection change made stale,
// and start the worker when one is missing.
void CSavedGameGrid::RefreshSizeCaches()
{
	if (m_sizing && WaitForSingleObject(m_sizeIdle, 0) == WAIT_OBJECT_0)
		m_sizing = false;

	bool ask;
	if (m_curGridItem != m_sizedItem)
	{
		m_sizedItem = m_curGridItem;
		m_itemBlocks = -1;
		ask = true;
	}
	else if (m_curTitle != m_sizedTitle)
	{
		m_sizedTitle = m_curTitle;
		m_sizedItem = m_curGridItem;
		m_titleBlocks = -1;
		m_itemBlocks = -1;
		ask = true;
	}
	else
	{
		ask = !m_sizing && (m_titleBlocks < 0 || m_itemBlocks < 0);
	}

	if (!ask || m_curTitle < 0)
		return;
	if (m_titleBlocks >= 0 && (m_itemBlocks >= 0 || m_curGridItem < 0))
		return;

	if (m_sizing)
		SignalObjectAndWait(m_sizeCancel, m_sizeIdle, INFINITE, FALSE);
	ResetEvent(m_sizeCancel);
	SetEvent(m_sizeStart);
	m_sizing = true;
}

// From the end of 5960's Advance (0x47e50): the device's size and free
// space in blocks, rounded up, less the root folder's block. A failed
// query reads as one block, none free. A change (or a new language) is
// announced with OnTotalFreeChanged and OnSelChange.
void CSavedGameGrid::RefreshDiskFreeSpace()
{
#ifdef _XBOX
	FSCHAR root[MAX_PATH];
	Ansi(root, g_titles[m_curDevUnit].GetUData(), MAX_PATH);
#else
	const FSCHAR *root = g_titles[m_curDevUnit].GetUData();
#endif

	ULARGE_INTEGER avail, total, free;
	if (!TheseusGetDiskFreeSpaceEx(root, &avail, &total, &free))
	{
		free.QuadPart = 0;
		total.QuadPart = BLOCK_SIZE;
	}

	int totalBlocks = (int)((total.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE) - 1;
	int freeBlocks = (int)((free.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE);
	if (totalBlocks == m_deviceBlocks && freeBlocks == m_freeBlocks && g_nCurLanguage == m_shownLanguage)
		return;

	m_shownLanguage = g_nCurLanguage;
	m_deviceBlocks = totalBlocks;
	m_freeBlocks = freeBlocks;
	CallFunction(this, _T("OnTotalFreeChanged"));
	CallFunction(this, _T("OnSelChange"));
}

// Fires OnSelChange and restarts the highlight pulse.
void CSavedGameGrid::NotifySelectionChanged()
{
	CallFunction(this, _T("OnSelChange"));
	g_pulseStartTime = TheseusGetNow();
}

// CSavedGameGrid::Advance(seconds)
// Reconstructed from 5960 retail XBE @ 0x47e50. Nothing without a device or
// while inactive. A stale title list is rebuilt on a thread between
// OnUpdatingTitlesBegin and OnUpdatingTitlesEnd (the end held back at least
// a second; run inline if no thread). A copy reports progress and finishes
// with OnCopyError or, half a second in at the earliest, OnCopyComplete. A
// delete finishes with OnDeleteEnd and OnSelChange. A pulled device sends
// OnDeviceRemoved and drops it. Then sizes and free space are kept current
// and the list eases toward its scroll target over a quarter second.
void CSavedGameGrid::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_curDevUnit == -1 || !m_isActive)
		return;

	CTitleArray& titles = g_titles[m_curDevUnit];

	if (m_listThread != NULL)
	{
		if (TheseusGetNow() <= m_listEndAt || WaitForSingleObject(m_listThread, 0) != WAIT_OBJECT_0)
			return;
		CallFunction(this, _T("OnUpdatingTitlesEnd"));
		CloseHandle(m_listThread);
		m_listThread = NULL;
		InvalidateDLCCache();
	}
	else if (titles.IsDirty())
	{
		m_soundtrackRow = -1;
		m_soundtracks = 0;
		m_listEndAt = TheseusGetNow() + 1.0f;
		CallFunction(this, _T("OnUpdatingTitlesBegin"));

		m_listThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)TitlesEnumThread, (LPVOID)this, 0, 0);
		if (m_listThread != NULL)
			return;

		TitlesEnumThread(this);
		CallFunction(this, _T("OnUpdatingTitlesEnd"));
		InvalidateDLCCache();
	}

	if (m_copyRunning)
	{
		if (m_copyProgress != theGameCopier.m_progress)
		{
			m_copyProgress = theGameCopier.m_progress;
			CallFunction(this, _T("OnCopyProgressChanged"));
		}

		if (theGameCopier.m_error)
		{
			m_copyRunning = false;
			theGameCopier.Finish();
			CallFunction(this, _T("OnCopyError"));
		}
		else if (theGameCopier.m_done && TheseusGetNow() >= m_copyStart + 0.5f)
		{
			m_copyRunning = false;
			theGameCopier.Finish();
			CallFunction(this, _T("OnCopyComplete"));
		}
		return;
	}

	if (m_deleteRunning)
	{
		if (WaitForSingleObject(m_deleter, 0) == WAIT_OBJECT_0)
		{
			CloseHandle(m_deleter);
			m_deleter = NULL;
			m_deleteRunning = false;
			InvalidateDLCCache();
			CallFunction(this, _T("OnDeleteEnd"));
			NotifySelectionChanged();
		}
		return;
	}

	if (!titles.IsValid())
	{
		CallFunction(this, _T("OnDeviceRemoved"));
		m_curDevUnit = -1;
		return;
	}
	if (titles.IsDirty())
		return;

	RefreshSizeCaches();
	RefreshDiskFreeSpace();

	if (m_scrollStart == 0.0f)
		return;

	float t = (float)(TheseusGetNow() - m_scrollStart) / 0.25f;
	if (t >= 1.0f)
	{
		t = 1.0f;
		m_scrollStart = 0.0f;
	}
	m_scroll = (1.0f - t) * m_scroll + t * m_scrollTarget;
	m_focusPodMatrixValid = false;
}

// CSavedGameGrid::GetUpdateString   [script fn 'GetUpdateString', FND 0x001879b0, sig 8]
// Reconstructed from 5960 retail XBE @ 0x48450: every half second the
// translated "Copying" gets one more dot, cycling 0..3.
CStrObject *CSavedGameGrid::GetUpdateString(void)
{
	if (GetTickCount() - m_dotsTick > 500)
	{
		m_dotsTick = GetTickCount();
		m_dots = (m_dots + 1 > 3) ? 0 : m_dots + 1;

		Translate(_T("Copying"), m_copyingText);
		for (int dots = 0; dots < m_dots; dots++)
			_tcscat(m_copyingText, _T("."));
	}
	return new CStrObject(m_copyingText);
}

void CSavedGameGrid::InvalidateDLCCache()
{
	m_dlcCountCacheSize = 0;
}

// curDevUnit set: select the device row (the first title on the copy
// destination view) at once and forget cached sizes. 5960 @ 0x48530.
void CSavedGameGrid::OnDevUnitChanged(int newDevUnit)
{
	m_curDevUnit = newDevUnit;
	SelectTitle(IsMUSelector() ? 0 : -1, true);
	m_itemBlocks = -1;
	m_titleBlocks = -1;
	InvalidateDLCCache();
}

// isActive cleared on an active grid: cancel a running size query, forget
// sizes, and (Theseus) flush the texture cache so save icons don't stay.
void CSavedGameGrid::OnIsActiveChanged(bool active)
{
	if (active || !m_isActive)
		return;

	if (m_sizing)
	{
		SignalObjectAndWait(m_sizeCancel, m_sizeIdle, INFINITE, FALSE);
		m_sizing = false;
	}
	m_titleBlocks = -1;
	m_itemBlocks = -1;
	FlushTextureCache();
}

// CSavedGameGrid::OnSetProperty
// Reconstructed from 5960 retail XBE @ 0x48530: curDevUnit and curTitle go
// through their handlers instead of being stored; isActive is watched for
// going off.
bool CSavedGameGrid::OnSetProperty(const PRD *pprd, const void *pvValue)
{
	int field = PTR2INT(pprd->pbOffset);

	if (field == offsetof(m_curDevUnit))
	{
		OnDevUnitChanged(*(const int *)pvValue);
		return false;
	}
	if (field == offsetof(m_curTitle))
	{
		SelectTitle(*(const int *)pvValue);
		return false;
	}
	if (field == offsetof(m_isActive))
		OnIsActiveChanged(*(const bool *)pvValue);
	return true;
}

// =============================================================================
// Drawing
// =============================================================================

// Draws one node under an extra transform.
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

// Theseus: the ContentImage.xbx of a title's Nth DLC, false when there's
// no such folder.
bool CSavedGameGrid::GetDLCImagePath(int nTitle, int dlcIndex, TCHAR *out)
{
	char titleID[9];
	char folder[17];
	if (!ResolveDLCFolder(nTitle, dlcIndex, titleID, sizeof(titleID), folder, sizeof(folder)))
		return false;

	char path[MAX_PATH];
	_snprintf(path, MAX_PATH, "E:\\TDATA\\%s\\$c\\%s\\ContentImage.xbx", titleID, folder);
	MultiByteToWideChar(CP_ACP, 0, path, -1, out, MAX_PATH);
	return true;
}

// CSavedGameGrid::RenderIconRow(parent, y, title, first, count)
// Reconstructed from 5960 retail XBE @ 0x48690. One row of up to
// iconsPerRow small icons starting at item first, spaced smallIconSpacing
// apart. A save shows its image through SavedIconPanel, a soundtrack the
// soundtrack icon through SoundtrackIconPanel. The selected item gets the
// ring unless its icon is lifted out. Theseus: items past the saves are
// DLC, drawn from the content's image with a check mark.
void CSavedGameGrid::RenderIconRow(D3DXMATRIX *pMatrix, float y, int nTitle, int nFirstSavedGame, int nSavedGames)
{
	if (m_smallIcon == NULL || nTitle < 0)
		return;

	int end = nFirstSavedGame + m_iconsPerRow;
	if (end > nSavedGames)
		end = nSavedGames;

	bool isDLC = false;
	float x = 0.0f;
	for (int item = nFirstSavedGame; item < end; item++, x += m_smallIconSpacing)
	{
		D3DXMATRIX at;
		D3DXMatrixTranslation(&at, x, y, 0.0f);
		D3DXMatrixMultiply(&at, pMatrix, &at);

		TCHAR image[MAX_PATH];
		if (IS_GAME_ROW(nTitle))
		{
			int saves = GetSavedGameCount(nTitle);
			if (item < saves)
				g_titles[m_curDevUnit].GetSavedGameImageName(nTitle, item, image);
			else if (GetDLCImagePath(nTitle, item - saves, image))
				isDLC = true;
			else
				_tcscpy(image, _T("dlcitem64.xbx"));

			g_szCurTitleImage = image;
			RenderNodeAt(m_SavedIconPanel, &at);
		}
		else
		{
			_tcscpy(image, _T("soundtracksave64.tga"));
			g_szCurTitleImage = image;
			RenderNodeAt(m_SoundtrackIconPanel, &at);
		}
		g_szCurTitleImage = NULL;

		if (!m_detachIcon && m_smallIconHilite != NULL && nTitle == m_curTitle && item == m_curGridItem)
			RenderNodeAt(m_iconRing, &at);
		if (isDLC && m_iconCheck != NULL)
			RenderNodeAt(m_iconCheck, &at);
	}
}

// The big icon on a title's pod: the memory unit for the device row, the
// soundtrack art, the lifted item's own image while an icon is detached,
// else the title's TitleImage.xbx or the Xbox logo. (5960 RenderLoop,
// 0x48910; the DLC images are Theseus's.)
void CSavedGameGrid::ResolveTitlePodImageName(int nTitle, TCHAR *outBuf)
{
	if (nTitle == -1)
	{
		_tcscpy(outBuf, _T("memoryUnit128.tga"));
		return;
	}
	if (!IS_GAME_ROW(nTitle))
	{
		_tcscpy(outBuf, _T("soundtracksave3.tga"));
		return;
	}

	if (nTitle == m_curTitle && m_detachIcon && m_curGridItem != -1)
	{
		int saves = GetSavedGameCount(nTitle);
		int dlc = GetDLContentCount(nTitle);
		if (m_curGridItem < saves)
			g_titles[m_curDevUnit].GetSavedGameImageName(nTitle, m_curGridItem, outBuf);
		else if (m_curGridItem - saves >= dlc)
			_tcscpy(outBuf, _T("xboxlogo128.xbx"));
		else if (!GetDLCImagePath(nTitle, m_curGridItem - saves, outBuf))
			_tcscpy(outBuf, _T("dlcitem128.xbx"));
		return;
	}

	MakePath(outBuf, g_titles[m_curDevUnit].GetUData(), GetTitleID2(nTitle));
	MakePath(outBuf, outBuf, szTitleImageXBX);
	if (!DoesFileExist(outBuf))
		_tcscpy(outBuf, _T("xboxlogo128.xbx"));
}

// A title's pod and its save (or soundtrack) panel. Once the list has
// settled, the selected pod is drawn from a remembered world matrix so it
// can be lifted independently of the list, with the ring when the title
// itself is selected. A title just deleted fades out over a second through
// the ordinary path. (5960 RenderLoop, 0x48910.)
void CSavedGameGrid::RenderTitlePodAt(int nTitle, float y, const D3DXMATRIX *parentMat)
{
	if (m_pod == NULL)
		return;

	TCHAR image[MAX_PATH];
	ResolveTitlePodImageName(nTitle, image);
	g_szCurTitleImage = image;

	const float alphaBefore = g_nEffectAlpha;
	CNode *panel = (nTitle == m_soundtrackRow) ? m_podSoundtrackPanel : m_podSavePanel;

	bool lifted = false;
	bool ring = false;
	bool plain = true;
	if (nTitle == m_curTitle && m_scroll == m_scrollTarget)
	{
		lifted = true;
		plain = false;
		ring = !m_detachIcon && m_podHilite != NULL && m_curGridItem == -1;

		if (nTitle > -1 && nTitle == m_fadingTitle)
		{
			float t = (float)(TheseusGetNow() - m_fadeStart) / 1.0f;
			if (t >= 1.0f)
			{
				m_fadeStart = 0.0f;
				m_fadingTitle = -1;
				m_focusPodMatrixValid = false;
			}
			else
			{
				g_nEffectAlpha *= 1.0f - t;
				g_szCurTitleImage = NULL;
				m_focusPodMatrixValid = true;
				ring = false;
				plain = true;
			}
		}
	}

	if (lifted)
	{
		TheseusPushWorld();
		if (m_focusPodMatrixValid)
		{
			TheseusIdentityWorld();
			TheseusMultWorld(&m_focusPodMatrix);
		}
		else
		{
			D3DXMATRIX at;
			D3DXMatrixTranslation(&at, -0.3292f, y, -0.0271f);
			D3DXMatrixMultiply(&at, parentMat, &at);
			TheseusMultWorld(&at);
			m_focusPodMatrix = *TheseusGetWorld();
			m_focusPodMatrixValid = true;
		}
		TheseusUpdateWorld();
		m_pod->Render();
		panel->Render();
		if (ring)
			m_podRing->Render();
		TheseusPopWorld();
	}

	g_nEffectAlpha = alphaBefore;
	g_szCurTitleImage = image;

	if (plain)
	{
		D3DXMATRIX at;
		D3DXMatrixTranslation(&at, -0.3292f, y, -0.0271f);
		D3DXMatrixMultiply(&at, parentMat, &at);
		RenderNodeAt(m_pod, &at);
		RenderNodeAt(panel, &at);
	}
	g_szCurTitleImage = NULL;
}

// CSavedGameGrid::Render
// Reconstructed from 5960 retail XBE @ 0x488a0: only with the row nodes
// set, a mounted and current device, active and not rebuilding.
void CSavedGameGrid::Render()
{
	if (m_header == NULL || m_firstRow == NULL || m_secondRow == NULL || m_otherRow == NULL)
		return;
	if (m_curDevUnit == -1 || !g_titles[m_curDevUnit].IsValid() || g_titles[m_curDevUnit].IsDirty())
		return;
	if (!m_isActive || m_listing)
		return;
	RenderLoop(true);
}

// CSavedGameGrid::RenderLoop(render)
// Reconstructed from 5960 retail XBE @ 0x48910. Lays the list out top
// down from the scroll offset in a frame turned a quarter turn about -x
// and scaled 0.05942: the device row first (not on the copy destination
// view), then each title. A title is its pod, a header (highlighted when
// selected) with its name tinted gray when selected and green otherwise,
// and up to three icon rows with more-up and more-down arrows. Rows above
// the selected one show their last icon rows. Unselected titles fade to
// half strength as the selection moves. With render off it only measures:
// the offset of the selected title, which SelectTitle scrolls to. Drawing
// stops below -1.25. Spacing constants and the layout are in
// docs/decomp/SavedGameGrid.md.
float CSavedGameGrid::RenderLoop(bool bRender)
{
	D3DXMATRIX frame, at, upArrow;
	D3DXVECTOR3 axis(-1.0f, 0.0f, 0.0f);
	D3DXMatrixRotationAxis(&frame, &axis, -1.571f);
	D3DXMatrixScaling(&at, 0.05942f, 0.05942f, 0.05942f);
	D3DXMatrixMultiply(&frame, &frame, &at);

	const float bottom = -1.25f;
	float y = m_scroll;
	int titleCount = GetTitleCount();

	for (int nTitle = IsMUSelector() ? 0 : -1; nTitle < titleCount && (!bRender || y > bottom); nTitle++)
	{
		const float alphaBefore = g_nEffectAlpha;

		int items = 0;
		if (nTitle >= 0)
			items = IS_GAME_ROW(nTitle) ? GetSavedGameCount(nTitle) + GetDLContentCount(nTitle) : m_soundtracks;
		int rows = (items + m_iconsPerRow - 1) / m_iconsPerRow;

		int firstRow = 0;
		if (nTitle == m_curTitle)
			firstRow = m_iconRowScroll;
		else if (nTitle < m_curTitle && rows > VISIBLE_ICON_ROWS)
			firstRow = rows - VISIBLE_ICON_ROWS;

		if (!bRender)
		{
			if (nTitle == m_curTitle)
				return y - m_scroll;
		}
		else
		{
			float t = (float)(TheseusGetNow() - m_scrollStart) / 0.25f;
			if (t > 1.0f)
				t = 1.0f;

			float selected = 0.0f;
			if (nTitle == m_curTitle)
				selected = (m_scrollStart != 0.0f) ? t : 1.0f;
			else if (nTitle == m_prevTitle && m_scrollStart != 0.0f)
				selected = 1.0f - t;
			g_nEffectAlpha *= 0.5f + (0.5f * selected);
		}

		bool onScreen = bRender && y - 0.25f < 0.5f;

		if (onScreen)
			RenderTitlePodAt(nTitle, y, &frame);

		// Header and name.
		if (onScreen)
		{
			D3DXMatrixTranslation(&at, -0.3292f, y, -0.0271f);
			D3DXMatrixMultiply(&at, &frame, &at);
			bool selectedRow = nTitle == m_curTitle;
			if (nTitle >= 0)
				RenderNodeAt(selectedRow ? m_hiliteHeader : m_header, &at);
			else
				RenderNodeAt(selectedRow ? m_MUhiliteHeader : m_MUheader, &at);

			if (m_renderIcons)
			{
				TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
				TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
				TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
				TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
				TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
				TheseusSetRenderState(D3DRS_TEXTUREFACTOR, selectedRow ? D3DCOLOR_RGBA(170, 170, 170, 255) : D3DCOLOR_RGBA(140, 201, 25, 255));

				D3DXMATRIX textScale;
				D3DXMatrixScaling(&textScale, 0.06442f, 0.06442f, 0.06442f);
				D3DXMatrixTranslation(&at, -0.3292f - 0.05942f, y + 0.05942f, -0.0271f + (0.6f * 0.05942f));
				D3DXMatrixMultiply(&at, &textScale, &at);

				TCHAR name[256];
				GetTitleName2(nTitle, name);
				RenderNodeAt(GetTextNode(name, -11.2f), &at);
			}

			if (firstRow > 0)
			{
				D3DXMatrixTranslation(&at, -0.17f, (y - 0.07f), 0.0f);
				D3DXMatrixMultiply(&upArrow, &frame, &at);
			}
		}
		y -= 0.0092f;

		// First icon row (the device row has its own).
		if (bRender && y - 0.25f < 0.5f)
		{
			D3DXMatrixTranslation(&at, -0.3292f, y, 0.01144f);
			D3DXMatrixMultiply(&at, &frame, &at);
			RenderNodeAt(nTitle >= 0 ? m_firstRow : m_firstMURow, &at);
			if (firstRow > 0)
				RenderNodeAt(m_moreUp, &upArrow);
			RenderIconRow(&frame, y, nTitle, firstRow * m_iconsPerRow, items);
		}
		y -= 0.2455f;

		if (items > m_iconsPerRow)
		{
			if (bRender && y - 0.25f < 0.5f)
			{
				D3DXMatrixTranslation(&at, -0.3301f, y, 0.01144f);
				D3DXMatrixMultiply(&at, &frame, &at);
				RenderNodeAt(m_secondRow, &at);
				RenderIconRow(&frame, y, nTitle, (firstRow + 1) * m_iconsPerRow, items);
			}
			y -= 0.24499f;

			if (items > m_iconsPerRow * 2)
			{
				for (int row = 2; row < rows - firstRow && row < VISIBLE_ICON_ROWS && (!bRender || y > bottom); row++)
				{
					if (bRender && y - 0.25f < 0.5f)
					{
						D3DXMatrixTranslation(&at, -0.3412f, y, 0.01144f);
						D3DXMatrixMultiply(&at, &frame, &at);
						RenderNodeAt(m_otherRow, &at);
						if (row == VISIBLE_ICON_ROWS - 1 && firstRow + row < rows - 1)
						{
							D3DXMatrixTranslation(&at, -0.17f, (y - 0.015f), 0.0f);
							D3DXMatrixMultiply(&at, &frame, &at);
							RenderNodeAt(m_moreDown, &at);
						}
						RenderIconRow(&frame, y, nTitle, (firstRow + row) * m_iconsPerRow, items);
					}
					y -= 0.24499f;
				}
			}
		}

		y -= 0.09f;
		g_nEffectAlpha = alphaBefore;
	}

	return 0.0f;
}

// =============================================================================
// Selection
// =============================================================================

// CSavedGameGrid::SelectTitle(n, instant)
// Reconstructed from 5960 retail XBE @ 0x49410. Remembers the last title
// and takes the new one (clamped to the list) unless the list is stale.
// Scrolls so it's at the top, at once or eased from now. Inside the title
// the item follows the preferred column: none keeps the title itself
// selected; moving down lands on that column in the first row, moving up
// on that column in the last row, clamped to the last item, with the icon
// rows scrolled to show it. 5960 sizes the grid from all of its items;
// Theseus from the saves alone.
void CSavedGameGrid::SelectTitle(int nTitle, bool bInstantScroll)
{
	m_prevTitle = m_curTitle;
	m_curTitle = nTitle;
	if (g_titles[m_curDevUnit].IsDirty())
		return;

	int titleCount = GetTitleCount();
	if (m_curTitle >= titleCount)
		m_curTitle = titleCount - 1;

	int items = GetSavedGameCount(m_curTitle);
	int rows = (items + m_iconsPerRow - 1) / m_iconsPerRow;

	float target = -RenderLoop(false);
	if (target != m_scrollTarget)
	{
		m_scrollTarget = target;
		if (bInstantScroll)
		{
			m_scroll = target;
			m_scrollStart = 0.001f;
		}
		else
		{
			m_scrollStart = TheseusGetNow();
		}
	}

	if (m_nPrefColumn != -1)
		m_nPrefColumn = (m_curGridItem != -1) ? m_curGridItem % m_iconsPerRow : 0;

	if (m_nPrefColumn == -1)
	{
		m_curGridItem = -1;
		m_iconRowScroll = 0;
		return;
	}

	if (m_curTitle > m_prevTitle)
	{
		m_curGridItem = m_nPrefColumn;
		if (m_curGridItem > items - 1)
			m_curGridItem = items - 1;
		m_iconRowScroll = 0;
		return;
	}

	if (items == 0)
	{
		m_curGridItem = -1;
	}
	else
	{
		m_curGridItem = m_nPrefColumn + (rows - 1) * m_iconsPerRow;
		if (m_curGridItem > items - 1)
			m_curGridItem = items - 1;
	}
	m_iconRowScroll = rows > VISIBLE_ICON_ROWS ? rows - VISIBLE_ICON_ROWS : 0;
}

// Saves plus DLC under a title (Theseus counts DLC as grid items).
#define GRID_ITEMS(n) (GetSavedGameCount(n) + GetDLContentCount(n))

// CSavedGameGrid::selectUp   [script fn 'selectUp', FND 0x00187698, sig 1]
// Reconstructed from 5960 retail XBE @ 0x49590. Up a row inside the grid
// (keeping the column, scrolling the rows if needed); from the top row or
// the title itself, the title above. Either way OnSelChange fires and the
// column is remembered.
void CSavedGameGrid::selectUp()
{
	if (m_curGridItem != -1 && m_curGridItem / m_iconsPerRow > 0)
	{
		int row = m_curGridItem / m_iconsPerRow - 1;
		m_curGridItem = row * m_iconsPerRow + m_nPrefColumn;
		if (row < m_iconRowScroll)
			m_iconRowScroll = row;
		m_nPrefColumn = m_curGridItem % m_iconsPerRow;
		NotifySelectionChanged();
		return;
	}

	if (m_curTitle > (IsMUSelector() ? 0 : -1))
	{
		SelectTitle(m_curTitle - 1);
		NotifySelectionChanged();
	}
	m_nPrefColumn = m_curGridItem % m_iconsPerRow;
}

// CSavedGameGrid::selectDown   [script fn 'selectDown', FND 0x001876b0, sig 1]
// Reconstructed from 5960 retail XBE @ 0x49650. Down a row inside the grid
// (clamped to the last item, scrolling the rows), else the next title.
void CSavedGameGrid::selectDown()
{
	if (m_curGridItem != -1)
	{
		int items = GRID_ITEMS(m_curTitle);
		int row = m_curGridItem / m_iconsPerRow;
		if (row < (items + m_iconsPerRow - 1) / m_iconsPerRow - 1)
		{
			row++;
			m_curGridItem += m_iconsPerRow;
			if (m_curGridItem > items - 1)
				m_curGridItem = items - 1;
			if (row >= m_iconRowScroll + VISIBLE_ICON_ROWS - 1)
				m_iconRowScroll = row - (VISIBLE_ICON_ROWS - 1);
			m_nPrefColumn = m_curGridItem % m_iconsPerRow;
			NotifySelectionChanged();
			return;
		}
	}

	if (m_curTitle < GetTitleCount() - 1)
	{
		SelectTitle(m_curTitle + 1);
		NotifySelectionChanged();
	}
	m_nPrefColumn = m_curGridItem % m_iconsPerRow;
}

// CSavedGameGrid::selectLeft   [script fn 'selectLeft', FND 0x001876c8, sig 1]
// Reconstructed from 5960 retail XBE @ 0x49740: from the first column back
// out to the title, else one item left. OnSelChange only on a change.
void CSavedGameGrid::selectLeft()
{
	int before = m_curGridItem;
	if (m_curGridItem >= 0)
	{
		if (m_nPrefColumn == 0)
		{
			m_nPrefColumn = -1;
			m_curGridItem = -1;
			m_iconRowScroll = 0;
		}
		else
		{
			m_curGridItem--;
			m_nPrefColumn = m_curGridItem % m_iconsPerRow;
		}
	}
	if (m_curGridItem != before)
		NotifySelectionChanged();
}

// CSavedGameGrid::selectRight   [script fn 'selectRight', FND 0x001876e0, sig 1]
// Reconstructed from 5960 retail XBE @ 0x497b0: one item right while there
// is one and the column is under 3, scrolling the rows to keep it shown.
void CSavedGameGrid::selectRight()
{
	int before = m_curGridItem;
	if (m_curGridItem < GRID_ITEMS(m_curTitle) - 1 && m_nPrefColumn < VISIBLE_ICON_ROWS)
	{
		m_curGridItem++;
		m_nPrefColumn = m_curGridItem % m_iconsPerRow;
		if (m_curGridItem >= (m_iconRowScroll + VISIBLE_ICON_ROWS) * m_iconsPerRow)
			m_iconRowScroll = m_curGridItem / m_iconsPerRow - (VISIBLE_ICON_ROWS - 1);
	}
	if (m_curGridItem != before)
		NotifySelectionChanged();
}

// CSavedGameGrid::setSelImage   [script fn 'setSelImage']
// Reconstructed from 5960 retail XBE @ 0x49840: points the selection image
// at the memory unit art, the selected save's image, the title's
// TitleImage.xbx (or the logo), or the soundtrack art. Theseus adds DLC.
void CSavedGameGrid::setSelImage()
{
	g_szSelTitleImage = NULL;

	if (m_curTitle == -1)
	{
		_tcscpy(szSelectionBuf, _T("memoryUnit128.tga"));
	}
	else if (m_curTitle < 0 || !IS_GAME_ROW(m_curTitle))
	{
		_tcscpy(szSelectionBuf, _T("soundtracksave3.tga"));
	}
	else if (m_curGridItem == -1)
	{
		MakePath(szSelectionBuf, g_titles[m_curDevUnit].GetUData(), GetTitleID2(m_curTitle));
		MakePath(szSelectionBuf, szSelectionBuf, szTitleImageXBX);
		if (!DoesFileExist(szSelectionBuf))
			_tcscpy(szSelectionBuf, _T("xboxlogo128.xbx"));
	}
	else
	{
		int saves = GetSavedGameCount(m_curTitle);
		if (m_curGridItem < saves)
			g_titles[m_curDevUnit].GetSavedGameImageName(m_curTitle, m_curGridItem, szSelectionBuf);
		else if (!GetDLCImagePath(m_curTitle, m_curGridItem - saves, szSelectionBuf))
			_tcscpy(szSelectionBuf, _T("dlcitem128.xbx"));
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

	for (size_t i = 0; i + 1 < bytesRead; i += 2)
	{
		if (buf[i + 1] == 0 && buf[i] >= 32 && buf[i] <= 126)
		{
			size_t start = i;
			while (i + 1 < bytesRead && buf[i + 1] == 0 && buf[i] >= 32 && buf[i] <= 126)
				i += 2;

			size_t len = i - start;
			char asciiBuf[256] = {};
			size_t nOut = 0;
			for (size_t j = 0; j < len && nOut < sizeof(asciiBuf) - 1; j += 2)
				asciiBuf[nOut++] = buf[start + j];

			asciiBuf[nOut] = '\0';
			const char *str = asciiBuf;

			if (str[0] == '[' && str[strlen(str) - 1] == ']')
			{
				// Normalize and compare
				char sectionLang[32] = {};
				size_t cbLang = strlen(str) - 2; // strip brackets
				if (cbLang > sizeof(sectionLang) - 1)
					cbLang = sizeof(sectionLang) - 1;
				strncpy(sectionLang, str + 1, cbLang);
				sectionLang[cbLang] = '\0';
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


// =============================================================================
// What the panel shows
// =============================================================================

// CSavedGameGrid::FormatGridItemName   [script fn 'FormatGridItemName']
// Reconstructed from 5960 retail XBE @ 0x49940 / 0x49aa0. Nothing without
// an item; a soundtrack's name; otherwise the Name in the save's
// SaveMeta.xbx for the current language, or "Broken Save". Theseus names
// DLC from its ContentMeta.xbx.
CStrObject *CSavedGameGrid::FormatGridItemName()
{
	if (m_curGridItem < 0)
		return new CStrObject;
	if (IsSoundtrackSelected())
		return new CStrObject(GetSoundtrackName(m_curGridItem));

	int saves = GetSavedGameCount(m_curTitle);
	int dlc = GetDLContentCount(m_curTitle);
	m_isCurGridItemDLC = m_curGridItem >= saves && m_curGridItem < saves + dlc;

	if (m_isCurGridItemDLC)
	{
		int dlcIndex = m_curGridItem - saves;
		char titleID[9];
		char folder[17];
		if (!ResolveDLCFolder(m_curTitle, dlcIndex, titleID, sizeof(titleID), folder, sizeof(folder)))
		{
			TCHAR numbered[64];
			_stprintf(numbered, _T("Downloadable Content %d"), dlcIndex + 1);
			return new CStrObject(numbered);
		}

		char metaPath[MAX_PATH];
		_snprintf(metaPath, MAX_PATH, "E:\\TDATA\\%s\\$c\\%s\\ContentMeta.xbx", titleID, folder);
		TCHAR wideMeta[MAX_PATH];
		MultiByteToWideChar(CP_ACP, 0, metaPath, -1, wideMeta, MAX_PATH);

		TCHAR name[64];
		if (ContentMetaParse(wideMeta, name, countof(name)))
			return new CStrObject(name);

		TCHAR translated[MAX_TRANSLATE_LEN] = _T("");
		return new CStrObject(Translate(_T("Unnamed DLC"), translated));
	}

	TCHAR meta[MAX_PATH];
	if (!GetSavedGamePath(meta, m_curTitle, m_curGridItem))
		return new CStrObject;
	MakePath(meta, meta, szSaveDataXBX);

	TCHAR name[64];
	TCHAR lang[MAX_LANGUAGE_CODE_LEN];
	TCHAR translated[MAX_TRANSLATE_LEN];
	CSettingsFile settings;
	if (!settings.Open(meta) || !settings.GetValue(GetLanguageCode(lang), _T("Name"), name, countof(name)))
		return new CStrObject(Translate(_T("Broken Save"), translated));
	return new CStrObject(name);
}

// CSavedGameGrid::FormatGridItemTime   [script fn 'FormatGridItemTime']
// Reconstructed from 5960 retail XBE @ 0x49ca0: the save folder's last
// write time in local time, formatted for the current locale. Empty for
// soundtracks, no item, or a folder that won't open.
CStrObject *CSavedGameGrid::FormatGridItemTime()
{
	if (IsSoundtrackSelected())
		return new CStrObject;

	TCHAR path[MAX_PATH];
	if (m_curGridItem < 0 || !GetSavedGamePath(path, m_curTitle, m_curGridItem))
		return new CStrObject;

	HANDLE folder = TheseusCreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (folder == INVALID_HANDLE_VALUE)
		return new CStrObject;

	FILETIME written, local;
	GetFileTime(folder, NULL, NULL, &written);
	CloseHandle(folder);
	FileTimeToLocalFileTime(&written, &local);

	SYSTEMTIME when;
	FileTimeToSystemTime(&local, &when);
	TCHAR text[32];
	FormatTime(text, countof(text), &when);
	return new CStrObject(text);
}

// CSavedGameGrid::GetSavedGameCount   [script fn 'GetSavedGameCount']
// Reconstructed from 5960 retail XBE @ 0x49e70: 0 for the device row or a
// stale list, the soundtrack count for the soundtrack row, else the
// title's saves.
int CSavedGameGrid::GetSavedGameCount(int nTitle)
{
	if (nTitle < 0 || g_titles[m_curDevUnit].IsDirty())
		return 0;
	if (IS_GAME_ROW(nTitle))
		return g_titles[m_curDevUnit].GetSavedGameCount(nTitle);
	return m_soundtracks;
}

// CSavedGameGrid::FormatGridItemSize   [script fn 'FormatGridItemSize']
// Reconstructed from 5960 retail XBE @ 0x49f70: the soundtrack's size, or
// the cached size of the selected save (empty with none). Kept in
// gameBlocks for the scripts.
CStrObject *CSavedGameGrid::FormatGridItemSize()
{
	int blocks;
	if (IsSoundtrackSelected())
	{
		blocks = GetSoundtrackSize(m_curGridItem);
	}
	else
	{
		TCHAR path[MAX_PATH];
		if (m_curGridItem < 0 || !GetSavedGamePath(path, m_curTitle, m_curGridItem))
			return new CStrObject;
		blocks = m_itemBlocks;
	}

	m_gameBlocks = blocks;
	TCHAR text[16];
	FormatInteger(text, blocks);
	return new CStrObject(text);
}

// CSavedGameGrid::FormatTitleSize   [script fn 'FormatTitleSize']
// 5960 @ 0x4a3e0: the cached size of the selected title.
CStrObject *CSavedGameGrid::FormatTitleSize()
{
	TCHAR text[16];
	FormatInteger(text, m_titleBlocks);
	return new CStrObject(text);
}

// <udata>\<title>\<save> on the current device; false when unmounted.
bool CSavedGameGrid::GetSavedGamePath(TCHAR *szBuf, int nTitle, int nSavedGame)
{
	CTitleArray& titles = g_titles[m_curDevUnit];
	if (!titles.IsValid())
		return false;
	MakePath(szBuf, titles.GetUData(), GetTitleID2(nTitle));
	MakePath(szBuf, szBuf, GetSavedGameID2(nTitle, nSavedGame));
	return true;
}

// CSavedGameGrid::GetSavedGamePath   [script fn 'GetSavedGamePath']
// Reconstructed from 5960 retail XBE @ 0x4a700: the save as the game sees
// it, "<drive>:\<save>", U: on the hard drive.
CStrObject *CSavedGameGrid::GetSavedGamePath(int nTitle, int nSavedGame)
{
	TCHAR path[MAX_PATH];
	path[0] = IsHDD() ? 'U' : g_titles[m_curDevUnit].GetUData()[0];
	path[1] = ':';
	path[2] = '\\';
	_tcscpy(path + 3, GetSavedGameID2(nTitle, nSavedGame));
	return new CStrObject(path);
}

// CSavedGameGrid::FormatFreeBlocks / FormatTotalBlocks / GetTotalBlocks
// 5960 @ 0x4a820 / 0x4a8d0 / 0x4a980: the device's free and usable blocks.
CStrObject *CSavedGameGrid::FormatFreeBlocks()
{
	TCHAR text[16];
	FormatBlocks(text, m_freeBlocks);
	return new CStrObject(text);
}

CStrObject *CSavedGameGrid::FormatTotalBlocks()
{
	TCHAR text[16];
	FormatBlocks(text, m_deviceBlocks);
	return new CStrObject(text);
}

int CSavedGameGrid::GetTotalBlocks()
{
	return m_deviceBlocks;
}

// CSavedGameGrid::CanDetachIcon   [script fn 'CanDetachIcon']
// 5960 @ 0x4a990: once the list has stopped scrolling. Theseus also waits
// for the lifted pod's matrix.
int CSavedGameGrid::CanDetachIcon()
{
	return m_scrollStart == 0.0f && m_focusPodMatrixValid;
}

// =============================================================================
// Copy and delete
// =============================================================================

// Blocks a title file adds to a copy: none if the destination has it.
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

// CSavedGameGrid::StartCopy(dest)   [script fn 'StartCopy']
// Reconstructed from 5960 retail XBE @ 0x4aac0 (StartSavedGameCopy). With
// no save selected it just reports OnCopyComplete. Otherwise the copy is
// sized (the save folder plus whichever title files the destination
// lacks), the status text reset, and the copier started on the one save.
void CSavedGameGrid::StartCopy(int destDevUnit)
{
	if (m_curGridItem == -1)
	{
		CallFunction(this, _T("OnCopyComplete"));
		return;
	}

	TCHAR folder[MAX_PATH];
	GetSavedGamePath(folder, m_curTitle, m_curGridItem);
	int blocks = GetDirectoryBlocks(folder, BLOCK_SIZE, true, NULL);
	blocks += CountSupportFileIfMissing(destDevUnit, szTitleDataXBX);
	blocks += CountSupportFileIfMissing(destDevUnit, szTitleImageXBX);
	blocks += CountSupportFileIfMissing(destDevUnit, szSaveImageXBX);

	m_copyRunning = true;
	m_copyStart = TheseusGetNow();
	m_copyProgress = 0.0f;
	m_dotsTick = GetTickCount();
	m_dots = 0;
	m_copyingText[0] = 0;
	Translate(_T("Copying"), m_copyingText);

	theGameCopier.SetSource(m_curDevUnit);
	theGameCopier.SetDestination(destDevUnit);
	theGameCopier.AddGame(GetTitleID2(m_curTitle), GetSavedGameID2(m_curTitle, m_curGridItem),
		GetSavedGameTime(m_curTitle, m_curGridItem), blocks);
	theGameCopier.Start();
}

// 8 hex digits to a title id, 0xffffffff when it isn't one. 5960 @ 0x4b160.
static DWORD ParseTitleID(const TCHAR *szID)
{
	if (_tcslen(szID) != 8)
		return 0xffffffff;

	DWORD id = 0;
	for (const TCHAR *p = szID; *p != 0; p++)
	{
		int digit;
		if (*p >= '0' && *p <= '9')
			digit = *p - '0';
		else if (*p >= 'a' && *p <= 'f')
			digit = *p - 'a' + 10;
		else if (*p >= 'A' && *p <= 'F')
			digit = *p - 'A' + 10;
		else
			return 0xffffffff;
		id = id * 16 + digit;
	}
	return id;
}

// From 5960's DeleteThread (0x4b1d0): a whole title goes from the list and
// from <udata>; the publisher's shared <pub>ffff folder in tdata too when
// it was that publisher's last title; on the hard drive also its tdata
// folder and cache partition. The soundtrack row moves up one.
void CSavedGameGrid::DeleteGameTitleFiles()
{
	CTitleArray& titles = g_titles[m_curDevUnit];
	TCHAR titleID[MAX_PATH];
	TCHAR publisher[16];
	TCHAR path[MAX_PATH];

	lstrcpyn(titleID, GetTitleID2(m_curTitle), countof(titleID));
	lstrcpyn(publisher, titleID, countof(publisher));
	publisher[4] = 0;

	titles.RemoveTitle(m_curTitle);

	if (!titles.IsPublisherExists(publisher))
	{
		lstrcat(publisher, _T("ffff"));
		MakePath(path, titles.GetTData(), publisher);
		theGameCopier.DeleteDirectory(path);
	}

	MakePath(path, titles.GetUData(), titleID);
	theGameCopier.DeleteDirectory(path);

	if (IsHDD())
	{
		MakePath(path, titles.GetTData(), titleID);
		theGameCopier.DeleteDirectory(path);
		XapiDeleteCachePartition(ParseTitleID(titleID));
	}

	if (m_soundtrackRow >= 0)
		m_soundtrackRow--;
}

// Deleting the title row: all soundtracks, or the game's files. The pod
// fades out from now and the list reselects in place.
void CSavedGameGrid::DeleteCurrentTitle()
{
	if (m_curTitle == m_soundtrackRow)
	{
		DeleteAllSoundtracks();
		m_soundtrackRow = -1;
		m_soundtracks = 0;
	}
	else
	{
		DeleteGameTitleFiles();
	}

	m_fadeStart = TheseusGetNow();
	m_fadingTitle = m_curTitle;
	SelectTitle(m_curTitle, true);
}

// Deleting one item: a soundtrack, or a save's folder and list entry. The
// selection stays in range and the rows scroll back if the last one went.
void CSavedGameGrid::DeleteCurrentGridItem()
{
	if (m_curTitle == m_soundtrackRow)
	{
		DeleteSoundtrack(m_curGridItem);
		m_soundtracks--;
	}
	else
	{
		TCHAR folder[MAX_PATH];
		GetSavedGamePath(folder, m_curTitle, m_curGridItem);
		theGameCopier.m_error = false;
		theGameCopier.DeleteDirectory(folder);
		g_titles[m_curDevUnit].RemoveSavedGame(m_curTitle, m_curGridItem);
	}

	int saves = GetSavedGameCount(m_curTitle);
	if (m_curGridItem > saves - 1)
		m_curGridItem = saves - 1;
	m_nPrefColumn = m_curGridItem % m_iconsPerRow;

	if (m_iconRowScroll != 0 && (m_iconRowScroll + VISIBLE_ICON_ROWS - 1) * m_iconsPerRow >= saves)
		m_iconRowScroll--;
}

// CSavedGameGrid::DeleteThread
// Reconstructed from 5960 retail XBE @ 0x4b1d0: the last soundtrack takes
// the whole row with it; no item means the whole title; then the sizes are
// counted again.
void WINAPI CSavedGameGrid::DeleteThread(CSavedGameGrid *p)
{
	if (p->m_curTitle == p->m_soundtrackRow && p->m_soundtracks == 1)
		p->m_curGridItem = -1;

	if (p->m_curGridItem == -1)
		p->DeleteCurrentTitle();
	else
		p->DeleteCurrentGridItem();

	p->m_titleBlocks = -1;
	p->m_itemBlocks = -1;
}

// CSavedGameGrid::StartDelete   [script fn 'StartDelete']
// Reconstructed from 5960 retail XBE @ 0x4b630: cancel a running size
// query, then delete on a thread; without one, delete here and report
// OnDeleteEnd and OnSelChange straight away.
void CSavedGameGrid::StartDelete()
{
	if (m_sizing)
		SignalObjectAndWait(m_sizeCancel, m_sizeIdle, INFINITE, FALSE);

	m_deleter = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)DeleteThread, this, 0, 0);
	if (m_deleter != NULL)
	{
		m_deleteRunning = true;
		return;
	}

	DeleteThread(this);
	CallFunction(this, _T("OnDeleteEnd"));
	NotifySelectionChanged();
}

// CSavedGameGrid::CanCopy   [script fn 'CanCopy', FND 0x001878f0, sig 2]
// Reconstructed from 5960 retail XBE @ 0x4b6b0. Not the device row, not
// soundtracks, and only a save whose SaveMeta.xbx has a Name; then yes
// unless its NoCopy is nonzero. Theseus also refuses saves of a broken
// title.
int CSavedGameGrid::CanCopy()
{
	if (m_curTitle < 0 || IsSoundtrackSelected())
		return false;

	TCHAR meta[MAX_PATH];
	if (m_curGridItem < 0 || !GetSavedGamePath(meta, m_curTitle, m_curGridItem))
		return false;
	MakePath(meta, meta, szSaveDataXBX);

	CSettingsFile settings;
	if (!settings.Open(meta))
		return false;

	TCHAR lang[MAX_LANGUAGE_CODE_LEN];
	TCHAR value[64];
	if (!settings.GetValue(GetLanguageCode(lang), _T("Name"), value, countof(value)))
		return false;
	if (g_titles[m_curDevUnit].IsBroken(m_curTitle))
		return false;
	if (!settings.GetValue(GetLanguageCode(lang), _T("NoCopy"), value, countof(value)))
		return true;
	return _ttoi(value) == 0;
}

// CSavedGameGrid::IsSoundtrackSelected   [script fn 'IsSoundtrackSelected']
// 5960 @ 0x4b870.
int CSavedGameGrid::IsSoundtrackSelected()
{
	return m_soundtrackRow >= 0 && m_curTitle == m_soundtrackRow;
}

// CSavedGameGrid::IsDevUnitReady   [script fn 'IsDevUnitReady']
// 5960 @ 0x4bab0: the device's list isn't being rebuilt.
int CSavedGameGrid::IsDevUnitReady(int nUnit)
{
	return !g_titles[nUnit].IsDirty();
}

// CSavedGameGrid::DoesSavedGameExists   [script fn 'DoesSavedGameExists']
// 5960 @ 0x4bc60: whether the selected save is already on the destination.
int CSavedGameGrid::DoesSavedGameExists(int destDevUnit)
{
	if (m_curGridItem < 0)
		return false;

	TCHAR path[MAX_PATH];
	MakePath(path, g_titles[destDevUnit].GetUData(), GetTitleID2(m_curTitle));
	MakePath(path, path, GetSavedGameID2(m_curTitle, m_curGridItem));
	return DoesFileExist(path);
}

static int ScanDLContentCount(const char *titleID)
{
	char dlcPath[MAX_PATH];
	_snprintf(dlcPath, MAX_PATH, "E:\\TDATA\\%s\\$c\\*", titleID);

	WIN32_FIND_DATAA findData;
	HANDLE hFind = TheseusFindFirstFile(dlcPath, &findData);
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
	} while (TheseusFindNextFile(hFind, &findData));

	TheseusFindClose(hFind);
	return dlcCount;
}

int CSavedGameGrid::GetDLContentCount(int nTitle)
{
	if (nTitle < 0 || m_curDevUnit != SLOT_HDD)
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
	// m_liveAccountRow is the title index where Live accounts appear,
	// or -1 if no Live accounts entry exists on the current device.
	return (m_liveAccountRow >= 0 && m_curTitle == m_liveAccountRow) ? 1 : 0;
}

int CSavedGameGrid::IsGameTitleSelected()
{
	// Returns 1 if curTitle is a real game title (not soundtracks, not Live accounts).
	if (m_curTitle < 0)
		return 0;
	if (m_soundtrackRow >= 0 && m_curTitle == m_soundtrackRow)
		return 0;
	if (m_liveAccountRow >= 0 && m_curTitle == m_liveAccountRow)
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
	if (m_soundtrackRow >= 0 && nTitle >= m_soundtrackRow)
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
	HANDLE hMeta = TheseusFindFirstFile(metaPath, &metaData);
	if (hMeta == INVALID_HANDLE_VALUE)
		return 1; // No ContentMeta = partial download
	TheseusFindClose(hMeta);
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
