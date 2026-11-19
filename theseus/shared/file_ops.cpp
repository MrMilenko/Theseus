// file_ops.cpp: the memory screens' file work. CGameCopier copies saves
// from one device to another on a worker thread, CCopyDestination is the
// node that lists where they can go, and CFile / CFolder let scripts list
// a folder. Rebuilt from the 5960 retail XBE (copier 0x3ec90..0x3fd00,
// File/Folder 0x31800..0x32010, CopyDestination 0x4ce40..0x4d380); see
// docs/decomp/FileOps.md.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "date_node.h"
#include "runner.h"
#include "title_collection.h"
#include "copy_games.h"

// The metadata files every title and save folder may carry.
const TCHAR szTitleDataXBX [] = _T("TitleMeta.xbx");
const TCHAR szTitleImageXBX [] = _T("TitleImage.xbx");
const TCHAR szSaveDataXBX [] = _T("SaveMeta.xbx");
const TCHAR szSaveImageXBX [] = _T("SaveImage.xbx");

// One save queued for copying (0x14 bytes in 5960). copied is set once the
// save is on the destination and still needs adding to its title list.
struct CCopyGame
{
	TCHAR* titleID;
	TCHAR* folder;
	FILETIME written;
	bool copied;
};

static TCHAR* NewString(const TCHAR* s)
{
	TCHAR* copy = new TCHAR [_tcslen(s) + 1];
	_tcscpy(copy, s);
	return copy;
}

// =========================================================================
// CGameCopier
// =========================================================================

// 5960 builds the copier inline as part of its owner; everything starts
// empty with no devices picked.
CGameCopier::CGameCopier()
{
	m_progress = 0.0f;
	m_error = false;
	m_done = false;
	m_thread = NULL;
	m_srcDevUnit = -1;
	m_destDevUnit = -1;
	m_srcRoot = NULL;
	m_destRoot = NULL;
	m_copyGames = NULL;
	m_copyGameCount = 0;
	m_copyGameCapacity = 0;
	m_buffer = NULL;
	m_internalError = false;
	m_alreadyExists = false;
	m_totalBlocks = 0;
	m_copiedBlocks = 0;
}

CGameCopier::~CGameCopier()
{
	RemoveAll();
}

// CGameCopier::RemoveAll
// Reconstructed from 5960 retail XBE @ 0x3ec90: frees both roots, every
// queued save and the list, zeroes the counts and the block total, frees
// the copy buffer.
void CGameCopier::RemoveAll()
{
	delete [] m_destRoot;
	m_destRoot = NULL;
	delete [] m_srcRoot;
	m_srcRoot = NULL;

	for (CCopyGame* g = m_copyGames; g < m_copyGames + m_copyGameCount; g++)
	{
		delete [] g->titleID;
		delete [] g->folder;
	}
	delete [] m_copyGames;
	m_copyGames = NULL;
	m_copyGameCapacity = 0;
	m_copyGameCount = 0;
	m_totalBlocks = 0;

	delete [] m_buffer;
	m_buffer = NULL;
}

// CGameCopier::Finish
// Reconstructed from 5960 retail XBE @ 0x3ed10. Every save that made it
// across goes into the destination's title list (an overwritten one is
// taken out first so it isn't listed twice), then the queue is emptied.
void CGameCopier::Finish()
{
	CTitleArray& dest = g_titles[m_destDevUnit];
	for (CCopyGame* g = m_copyGames; g < m_copyGames + m_copyGameCount; g++)
	{
		if (!g->copied)
			continue;
		if (m_alreadyExists)
			dest.RemoveSavedGame(g->titleID, g->folder);
		dest.AddSavedGame(g->titleID, g->folder, g->written);
		g->copied = false;
	}
	RemoveAll();
}

// 5960 @ 0x3eda0: the thread entry just forwards to ThreadProc.
DWORD CALLBACK CGameCopier::StartThread(LPVOID pvContext)
{
	return ((CGameCopier*)pvContext)->ThreadProc();
}

// <root>\<title> and <root>\<title>\<name>.
static void TitlePath(TCHAR* out, const TCHAR* root, const TCHAR* titleID, const TCHAR* name = NULL)
{
	MakePath(out, root, titleID);
	if (name != NULL)
		MakePath(out, out, name);
}

// CGameCopier::CopyGame(n)
// Reconstructed from 5960 retail XBE @ 0x3edb0. Makes the title folder on
// the destination; copies TitleImage.xbx if the title is new or its image
// is empty there, and SaveImage.xbx if the source has one and the
// destination's is empty. Those two don't count as failures. The save
// folder is created; if it's already there it's emptied and reused, any
// other failure stops this save. The folder gets the save's time, then
// its files are copied. A failed copy deletes what was written (the whole
// title folder when it was new). On success the queued folder name is
// replaced with what's on disk and TitleMeta.xbx is copied last, so a
// memory unit pulled mid copy doesn't end up with a title but no save.
// See docs/decomp/FileOps.md for the decompile.
void CGameCopier::CopyGame(int nCopyGame)
{
	CCopyGame& game = m_copyGames[nCopyGame];
	TCHAR src[MAX_PATH];
	TCHAR dest[MAX_PATH];

	TitlePath(dest, m_destRoot, game.titleID);
	bool newTitle = CreateDirectory(dest);

	TitlePath(dest, m_destRoot, game.titleID, szTitleImageXBX);
	if (newTitle || GetFileSize(dest) == 0)
	{
		TitlePath(src, m_srcRoot, game.titleID, szTitleImageXBX);
		CopyFile(src, dest);
	}

	TitlePath(dest, m_destRoot, game.titleID, szSaveImageXBX);
	TitlePath(src, m_srcRoot, game.titleID, szSaveImageXBX);
	if (DoesFileExist(src) && GetFileSize(dest) == 0)
		CopyFile(src, dest);

	m_internalError = false;
	m_alreadyExists = false;

	TitlePath(dest, m_destRoot, game.titleID, game.folder);
	if (!CreateDirectory(dest))
	{
		if (GetLastError() != ERROR_ALREADY_EXISTS)
		{
			m_internalError = true;
			return;
		}
		m_alreadyExists = true;
		DeleteDirectory(dest, false);
	}

	HANDLE folder = TheseusCreateFile(dest, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (folder != INVALID_HANDLE_VALUE)
	{
		SetFileTime(folder, &game.written, &game.written, &game.written);
		CloseHandle(folder);
	}

	TitlePath(src, m_srcRoot, game.titleID, game.folder);
	if (!CopyDirectory(src, dest))
	{
		if (newTitle)
			TitlePath(dest, m_destRoot, game.titleID);
		DeleteDirectory(dest);
		return;
	}

	const TCHAR* onDisk = _tcsrchr(dest, '\\') + 1;
	delete [] game.folder;
	game.folder = NewString(onDisk);

	TitlePath(dest, m_destRoot, game.titleID, szTitleDataXBX);
	if (newTitle || GetFileSize(dest) == 0)
	{
		TitlePath(src, m_srcRoot, game.titleID, szTitleDataXBX);
		CopyFile(src, dest);
		// The save is across; a missing meta file doesn't undo that.
		m_internalError = false;
	}
}

// CGameCopier::CreateDirectory(path)
// Reconstructed from 5960 retail XBE @ 0x3f420: narrow to 260 chars,
// CreateDirectoryA, true when it made one.
bool CGameCopier::CreateDirectory(const TCHAR* szDir)
{
	char narrow[MAX_PATH];
	Ansi(narrow, szDir, MAX_PATH);
	return TheseusCreateDirectory(narrow, NULL) != FALSE;
}

static bool IsDotEntry(const TCHAR* name)
{
	return _tcscmp(name, _T(".")) == 0 || _tcscmp(name, _T("..")) == 0;
}

// CGameCopier::CopyDirectory(src, dest)
// Reconstructed from 5960 retail XBE @ 0x3f480. Makes dest (an existing
// one is fine), then copies each entry of src except . and .., recursing
// into folders. The first failure closes the listing, flags the error and
// returns false.
bool CGameCopier::CopyDirectory(const TCHAR* szSrcPath, const TCHAR* szDestPath)
{
	CreateDirectory(szDestPath);

	TCHAR from[MAX_PATH];
	MakePath(from, szSrcPath, _T("*.*"));
	char pattern[MAX_PATH];
	Ansi(pattern, from, MAX_PATH);

	WIN32_FIND_DATA found;
	HANDLE find = TheseusFindFirstFile(pattern, &found);
	if (find == INVALID_HANDLE_VALUE)
		return true;

	do
	{
		TCHAR name[MAX_PATH];
		Unicode(name, found.cFileName, MAX_PATH);
		if (IsDotEntry(name))
			continue;

		TCHAR to[MAX_PATH];
		MakePath(from, szSrcPath, name);
		MakePath(to, szDestPath, name);

		bool ok = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? CopyDirectory(from, to) : CopyFile(from, to);
		if (!ok)
		{
			TheseusFindClose(find);
			m_internalError = true;
			return false;
		}
	}
	while (TheseusFindNextFile(find, &found));

	TheseusFindClose(find);
	return true;
}

// CGameCopier::CopyFile(src, dest)
// Reconstructed from 5960 retail XBE @ 0x3f700. Opens src (read, sequential)
// and creates dest, then moves 16 KB at a time, counting each block toward
// m_progress. A read or write failure flags the error. When it all went
// through, dest gets src's three file times. A failed close of dest also
// counts. Returns whether there was no error.
bool CGameCopier::CopyFile(const TCHAR* szSrcFile, const TCHAR* szDestFile)
{
	HANDLE in = TheseusCreateFile(szSrcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (in == INVALID_HANDLE_VALUE)
	{
		m_internalError = true;
		return false;
	}

	HANDLE out = TheseusCreateFile(szDestFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (out == INVALID_HANDLE_VALUE)
	{
		CloseHandle(in);
		m_internalError = true;
		return false;
	}

	for (;;)
	{
		DWORD got, put;
		if (!ReadFile(in, m_buffer, BLOCK_SIZE, &got, NULL))
		{
			m_internalError = true;
			break;
		}
		if (got == 0)
			break;
		if (!WriteFile(out, m_buffer, got, &put, NULL))
		{
			m_internalError = true;
			break;
		}

		m_copiedBlocks++;
		m_progress = (float)m_copiedBlocks / (float)m_totalBlocks;
	}

	if (!m_internalError)
	{
		FILETIME created, accessed, written;
		GetFileTime(in, &created, &accessed, &written);
		SetFileTime(out, &created, &accessed, &written);
	}

	CloseHandle(in);
	if (!CloseHandle(out))
		m_internalError = true;

	return !m_internalError;
}

// CGameCopier::DeleteFile(path)
// Reconstructed from 5960 retail XBE @ 0x3f830: clear the attributes so a
// read-only file goes too, then delete.
bool CGameCopier::DeleteFile(const TCHAR* szFile)
{
	char narrow[MAX_PATH];
	Ansi(narrow, szFile, MAX_PATH);
	TheseusSetFileAttributes(narrow, FILE_ATTRIBUTE_NORMAL);
	return TheseusDeleteFile(narrow) != FALSE;
}

// CGameCopier::DeleteDirectory(path, removeSelf)
// Reconstructed from 5960 retail XBE @ 0x3f8a0. Deletes everything inside
// (recursing into folders, . and .. skipped); the first failure flags the
// error and returns false. With removeSelf the emptied folder goes too.
bool CGameCopier::DeleteDirectory(const TCHAR* szDir, bool RemoveSelf)
{
	TCHAR path[MAX_PATH];
	MakePath(path, szDir, _T("*.*"));
	char narrow[MAX_PATH];
	Ansi(narrow, path, MAX_PATH);

	WIN32_FIND_DATA found;
	HANDLE find = TheseusFindFirstFile(narrow, &found);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			TCHAR name[MAX_PATH];
			Unicode(name, found.cFileName, MAX_PATH);
			if (IsDotEntry(name))
				continue;

			MakePath(path, szDir, name);
			bool ok = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DeleteDirectory(path) : DeleteFile(path);
			if (!ok)
			{
				TheseusFindClose(find);
				m_internalError = true;
				return false;
			}
		}
		while (TheseusFindNextFile(find, &found));
		TheseusFindClose(find);
	}

	if (!RemoveSelf)
		return true;

	Ansi(narrow, szDir, MAX_PATH);
	TheseusSetFileAttributes(narrow, FILE_ATTRIBUTE_NORMAL);
	return TheseusRemoveDirectory(narrow) != FALSE;
}

// CGameCopier::ThreadProc
// Reconstructed from 5960 retail XBE @ 0x3fae0:
//     while (cur < count) { CopyGame(cur); if (internalError) break;
//                           games[cur].copied = 1; cur++; }
//     done = 1; free(buffer); thread = 0; error = internalError; return 1;
DWORD CGameCopier::ThreadProc()
{
	for (; m_copyGameCur < m_copyGameCount; m_copyGameCur++)
	{
		CopyGame(m_copyGameCur);
		if (m_internalError)
			break;
		m_copyGames[m_copyGameCur].copied = true;
	}

	m_done = true;
	delete [] m_buffer;
	m_buffer = NULL;
	m_thread = NULL;
	m_error = m_internalError;
	return 1;
}

// SetSource / SetDestination(device)
// Reconstructed from 5960 retail XBE @ 0x3fb40 / 0x3fba0: remember the
// device and keep a copy of its user data root.
static void ReplaceString(TCHAR*& field, const TCHAR* value)
{
	delete [] field;
	field = NewString(value);
}

void CGameCopier::SetSource(int nDevUnit)
{
	m_srcDevUnit = nDevUnit;
	ReplaceString(m_srcRoot, g_titles[nDevUnit].GetUData());
}

void CGameCopier::SetDestination(int nDevUnit)
{
	m_destDevUnit = nDevUnit;
	ReplaceString(m_destRoot, g_titles[nDevUnit].GetUData());
}

// CGameCopier::AddGame(title, folder, time, blocks)
// Reconstructed from 5960 retail XBE @ 0x3fc00: queue grows 10 at a time;
// the save's blocks add to the total progress is measured against.
void CGameCopier::AddGame(const TCHAR* szTitleID, const TCHAR* szGameDir, FILETIME saveTime, int nBlocks)
{
	if (m_copyGameCount == m_copyGameCapacity)
	{
		m_copyGameCapacity += 10;
		CCopyGame* grown = new CCopyGame [m_copyGameCapacity];
		CopyMemory(grown, m_copyGames, m_copyGameCount * sizeof (CCopyGame));
		delete [] m_copyGames;
		m_copyGames = grown;
	}

	CCopyGame& g = m_copyGames[m_copyGameCount++];
	g.titleID = NewString(szTitleID);
	g.folder = NewString(szGameDir);
	g.written = saveTime;
	g.copied = false;
	m_totalBlocks += nBlocks;
}

// CGameCopier::Start
// Reconstructed from 5960 retail XBE @ 0x3fd00. Resets progress and flags,
// makes sure there's a 16 KB buffer and starts the copy thread; if no
// thread can be made the copy runs right here.
void CGameCopier::Start()
{
	m_progress = 0.0f;
	m_internalError = false;
	m_error = false;
	m_done = false;
	m_copiedBlocks = 0;
	m_copyGameCur = 0;

	if (m_buffer == NULL)
		m_buffer = new BYTE [BLOCK_SIZE];

	m_thread = CreateThread(NULL, 0, StartThread, this, 0, 0);
	if (m_thread != NULL)
		CloseHandle(m_thread);
	else
		StartThread(this);
}

// =========================================================================
// CCopyDestination
// =========================================================================

class CCopyDestination : public CNode
{
	DECLARE_NODE(CCopyDestination, CNode)
public:
	CCopyDestination();
	~CCopyDestination();

	void Advance(float nSeconds);
	void Render();
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	void selectUp();
	void selectDown();

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

	float m_scroll;          // current list offset
	int m_scrollTarget;      // row the list is easing toward
	XTIME m_scrollStart;     // 0 when not easing

	int m_units[9];          // devices listed, hard drive first
	int m_unitCount;

	DECLARE_NODE_PROPS();
	DECLARE_NODE_FUNCTIONS();

protected:
	void Pick(int row);
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

// CCopyDestination::CCopyDestination
// Reconstructed from 5960 retail XBE @ 0x4ce40: node pointers null,
// devices and selection -1, spacing 0.4, inactive, nothing listed.
CCopyDestination::CCopyDestination()
{
	m_pod = NULL;
	m_podIcon = NULL;
	m_panelMU = NULL;
	m_panelMUHilite = NULL;
	m_panelText = NULL;
	m_panelTextHilite = NULL;
	m_console = NULL;
	m_memoryUnit = NULL;
	m_curDevUnit = -1;
	m_selDevUnit = -1;
	m_sourceDevUnit = -1;
	m_spacing = 0.4f;
	m_isActive = false;
	m_select = -1;
	m_scroll = 0.0f;
	m_scrollTarget = 0;
	m_scrollStart = 0.0f;
	m_unitCount = 0;
}

// Reconstructed from 5960 retail XBE @ 0x4cee0: releases the seven nodes
// the script handed it.
CCopyDestination::~CCopyDestination()
{
	CNode* held[] = { m_pod, m_panelMU, m_panelMUHilite, m_panelText, m_panelTextHilite, m_console, m_memoryUnit };
	for (int i = 0; i < (int)countof(held); i++)
	{
		if (held[i] != NULL)
			held[i]->Release();
	}
}

// CCopyDestination::Render
// Reconstructed from 5960 retail XBE @ 0x4cf80. One pod per listed device,
// stacked downward by spacing from the scroll offset. Before each pod is
// drawn selDevUnit is set to that device and UpdateMemUnitText runs so
// the script can fill in its label. The current device gets the
// highlighted panel and text; the hard drive shows the console model,
// anything else the memory unit.
void CCopyDestination::Render()
{
	if (m_pod == NULL)
		return;

	float y = m_scroll;
	for (int i = 0; i < m_unitCount; i++, y -= m_spacing)
	{
		D3DXMATRIX offset;
		D3DXMatrixTranslation(&offset, 0.0f, y, 0.0f);
		TheseusPushWorld();
		TheseusMultWorld(&offset);
		TheseusUpdateWorld();

		m_pod->Render();

		m_selDevUnit = m_units[i];
		CallFunction(this, _T("UpdateMemUnitText"));

		bool current = m_units[i] == m_curDevUnit;
		(current ? m_panelMUHilite : m_panelMU)->Render();
		(current ? m_panelTextHilite : m_panelText)->Render();
		(m_selDevUnit == Dev0 ? m_console : m_memoryUnit)->Render();

		TheseusPopWorld();
	}
}

// CCopyDestination::Advance
// Reconstructed from 5960 retail XBE @ 0x4d110. While active it rebuilds
// the device list every frame: the hard drive, then memory units 0..7,
// leaving out the source and any unit that isn't mounted. If the current
// device moved rows the selection jumps with it; otherwise the list eases
// toward the selected row over a quarter second. When the selected device
// changes the script hears OnCurDevUnitChange, after
// OnDestinationUnitRemoved if the old one was pulled.
void CCopyDestination::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (!m_isActive)
		return;

	bool curRemoved = false;
	int curRow = -1;
	m_unitCount = 0;
	for (int i = 0; i < 9; i++)
	{
		int unit = (i == 0) ? Dev0 : i - 1;
		if (unit == m_sourceDevUnit)
			continue;

		if (unit != Dev0 && !g_titles[unit].IsValid())
		{
			if (unit == m_curDevUnit)
				curRemoved = true;
			continue;
		}

		if (unit == m_curDevUnit)
			curRow = m_unitCount;
		m_units[m_unitCount++] = unit;
	}

	if (curRow != -1 && curRow != m_select)
	{
		m_select = curRow;
		m_scrollTarget = curRow;
		m_scroll = m_spacing * curRow;
	}

	if (m_select < 0)
		m_select = 0;
	if (m_select > m_unitCount - 1)
		m_select = m_unitCount - 1;

	if (m_scrollTarget != m_select)
	{
		m_scrollTarget = m_select;
		m_scrollStart = TheseusGetNow();
	}
	else if (m_scrollStart != 0.0f)
	{
		float t = (float)(TheseusGetNow() - m_scrollStart) * 4.0f;
		if (t >= 1.0f)
		{
			t = 1.0f;
			m_scrollStart = 0.0f;
		}
		m_scroll = (1.0f - t) * m_scroll + t * (m_spacing * m_scrollTarget);
	}

	int unit = (m_select == -1) ? -1 : m_units[m_select];
	if (unit == m_curDevUnit)
		return;

	m_curDevUnit = unit;
	if (curRemoved)
		CallFunction(this, _T("OnDestinationUnitRemoved"));
	CallFunction(this, _T("OnCurDevUnitChange"));
}

// 5960 shares the plain CNode answer here (0x4d340: return true).
bool CCopyDestination::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	return CNode::OnSetProperty(pprd, pvValue);
}

// Moves the selection to row when there's somewhere to move.
void CCopyDestination::Pick(int row)
{
	if (m_unitCount < 2 || row < 0 || row >= m_unitCount)
		return;
	m_select = row;
	m_curDevUnit = m_units[row];
	CallFunction(this, _T("OnCurDevUnitChange"));
}

// CCopyDestination::selectUp / selectDown   [script fns 'selectUp' / 'selectDown']
// Reconstructed from 5960 retail XBE @ 0x4d350 / 0x4d380:
//     if (count > 1 && 0 <= select -/+ 1 < count) { select = it;
//         curDevUnit = units[select]; CallFunction("OnCurDevUnitChange"); }
void CCopyDestination::selectUp()
{
	Pick(m_select - 1);
}

void CCopyDestination::selectDown()
{
	Pick(m_select + 1);
}

// =========================================================================
// CFile
// =========================================================================

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

// CFile::CFile
// Reconstructed from 5960 retail XBE @ 0x31800: no strings, no date,
// length -1 until a folder listing fills it in.
CFile::CFile()
{
	m_name = NULL;
	m_type = NULL;
	m_path = NULL;
	m_length = -1;
	m_date = NULL;
}

// Reconstructed from 5960 retail XBE @ 0x318e0: frees the three strings
// and deletes the date.
CFile::~CFile()
{
	delete [] m_name;
	delete [] m_type;
	delete [] m_path;
	delete m_date;
}

// CFile::readText   [script fn 'readText', FND 0x0017f918, sig 8]
// Reconstructed from 5960 retail XBE @ 0x00031960. Ghidra decompile:
//     s = new CStrObject;
//     if (m_path && file.Fetch(m_path)) { file.MakeUnicode(); s->Append(file.content); }
//     return s;
// MakeUnicode widens an ANSI file and strips the BOM off a UTF-16 one, so the
// text reads right either way.
CStrObject* CFile::readText()
{
	CStrObject* pText = new CStrObject;
	if (m_path == NULL)
		return pText;

	CActiveFile file;
	if (file.Fetch(m_path))
	{
		file.MakeUnicode();
		pText->Append((const TCHAR*)file.GetContent());
	}
	return pText;
}

// =========================================================================
// CFolder
// =========================================================================

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

// Reconstructed from 5960 retail XBE @ 0x31a50 / 0x31ab0: path and name
// start empty; the destructor frees them (the arrays release themselves).
CFolder::CFolder()
{
	m_path = NULL;
	m_name = NULL;
}

CFolder::~CFolder()
{
	delete [] m_path;
	delete [] m_name;
}

// CFolder::OnSetProperty
// Reconstructed from 5960 retail XBE @ 0x31b30: setting path lists that
// folder; the value is stored either way.
bool CFolder::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_path))
		Refresh(*(const TCHAR**)pvValue);
	return true;
}

// <dir>\<name> in a fresh string.
static TCHAR* JoinPath(const TCHAR* dir, const TCHAR* name)
{
	TCHAR* joined = new TCHAR [_tcslen(dir) + 1 + _tcslen(name) + 1];
	_stprintf(joined, _T("%s\\%s"), dir, name);
	return joined;
}

// CFolder::Refresh(path)
// Reconstructed from 5960 retail XBE @ 0x31b50. Lists <path>\*.* (the path
// is cut to 255 chars). If that works, the folder's name becomes the part
// after the last '\'. Hidden, system and temporary entries are skipped.
// Each subfolder becomes a Folder with its path and name; each file a
// File with its write time as a Date, name without the extension, the
// extension lowercased as type, full path, and size.
void CFolder::Refresh(const TCHAR* szPath)
{
	TCHAR pattern[MAX_PATH];
	lstrcpyn(pattern, szPath, 255);
	_tcscat(pattern, _T("\\*.*"));
	char narrow[MAX_PATH];
	Ansi(narrow, pattern, MAX_PATH);

	WIN32_FIND_DATA found;
	HANDLE find = TheseusFindFirstFile(narrow, &found);
	if (find == INVALID_HANDLE_VALUE)
		return;

	const TCHAR* last = _tcsrchr(szPath, '\\');
	delete [] m_name;
	m_name = NewString(last != NULL ? last + 1 : _T(""));

	const DWORD skip = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY;
	do
	{
		if (found.dwFileAttributes & skip)
			continue;

		TCHAR entry[MAX_PATH];
		Unicode(entry, found.cFileName, countof(entry));

		if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			CFolder* sub = new CFolder;
			m_subFolders.AddNode(sub);
			sub->m_path = JoinPath(szPath, entry);
			sub->m_name = NewString(entry);
			continue;
		}

		CFile* file = new CFile;
		m_files.AddNode(file);

		file->m_date = new CDateObject;
		file->m_date->m_time = found.ftLastWriteTime;

		const TCHAR* dot = _tcsrchr(entry, '.');
		int stem = (int)_tcslen(entry) - (dot != NULL ? (int)_tcslen(dot) : 0);
		file->m_name = new TCHAR [stem + 1];
		CopyChars(file->m_name, entry, stem);
		file->m_name[stem] = 0;

		file->m_type = NewString(dot != NULL ? dot + 1 : _T(""));
		_tcslwr(file->m_type);

		file->m_path = JoinPath(szPath, entry);
		file->m_length = found.nFileSizeLow;
	}
	while (TheseusFindNextFile(find, &found));

	TheseusFindClose(find);
}

// CFolder::sortByName / Type / Date / Length   [script fns]
// Reconstructed from 5960 retail XBE @ 0x31f30..0x32010: qsort the files
// by name or type ignoring case, by write time, or by size.
static CFile* FileAt(const void* p)
{
	return *(CFile* const*)p;
}

static int __cdecl ByName(const void* a, const void* b)
{
	return _tcsicmp(FileAt(a)->m_name, FileAt(b)->m_name);
}

static int __cdecl ByType(const void* a, const void* b)
{
	return _tcsicmp(FileAt(a)->m_type, FileAt(b)->m_type);
}

static int __cdecl ByDate(const void* a, const void* b)
{
	return CompareFileTime(&FileAt(a)->m_date->m_time, &FileAt(b)->m_date->m_time);
}

static int __cdecl ByLength(const void* a, const void* b)
{
	return FileAt(a)->m_length - FileAt(b)->m_length;
}

void CFolder::sortByName()
{
	m_files.Sort(ByName);
}

void CFolder::sortByType()
{
	m_files.Sort(ByType);
}

void CFolder::sortByDate()
{
	m_files.Sort(ByDate);
}

void CFolder::sortByLength()
{
	m_files.Sort(ByLength);
}
