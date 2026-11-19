// title_collection.h: the per-device list of game titles and their saves
// that the memory screens read. One CTitleArray per device slot in g_titles:
// slots 0..7 are the memory units, slot 8 is the hard drive. Built by
// shared/title_collection.cpp; desktop links empty stubs instead.

#pragma once

// Device slots, as the memory screens and g_titles index them.
#define Dev1Unit1	0
#define Dev1Unit2	1
#define Dev2Unit1	2
#define Dev2Unit2	3
#define Dev3Unit1	4
#define Dev3Unit2	5
#define Dev4Unit1	6
#define Dev4Unit2	7
#define Dev0		8

// The dashboard counts space in 16 KB blocks on every device.
#define BLOCK_SIZE	16384

// Most saves one title folder is scanned for.
#define MAX_SAVED_GAMES 4096

extern const TCHAR szTitleDataXBX [];
extern const TCHAR szTitleImageXBX [];
extern const TCHAR szSaveDataXBX [];
extern const TCHAR szSaveImageXBX [];

// Save flag bits: the folder has its own SaveImage.xbx, or that's not known yet.
#define SAVEFLAG_HASIMAGE	0x00000001
#define SAVEFLAG_UNKIMAGE	0x00000002

// One save folder under a title.
struct CSave
{
	TCHAR m_folder[16];
	FILETIME m_written;
	DWORD m_flags;

	bool SetDirName(const TCHAR* szDirName);
	const TCHAR* GetDirName();
};

// One title folder and the saves found in it. Counts are -1 until scanned.
class CTitle
{
public:
	TCHAR* m_titleID;
	TCHAR* m_displayName;
	int m_saveCount;
	CSave* m_saveList;
	int m_saveBlocks;
	int m_blocks;
	bool m_broken;
};

class CTitleArray
{
public:
	CTitleArray();
	~CTitleArray();

	void SetRoot(TCHAR chNewRoot, bool bUpdate = true);
	void Update();
	void DeleteAll(bool bUpdate = true);

	bool IsValid() const;
	bool IsDirty() const;
	const TCHAR* GetTData() const;
	const TCHAR* GetUData() const;

	int GetTitleCount();
	int GetTitleCount2();
	bool IsBroken(int nTitle);
	const TCHAR* GetTitleID(int nTitle);
	const TCHAR* GetTitleName(int nTitle);
	const TCHAR* GetTitleName2(int nTitle);
	int GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent);
	bool IsPublisherExists(const TCHAR* szPublisherID) const;

	int GetSavedGameCount(int nTitle, HANDLE hCancelEvent = NULL);
	const TCHAR* GetSavedGameID(int nTitle, int nSavedGame);
	FILETIME GetSavedGameTime(int nTitle, int nSavedGame);
	void GetSavedGameImageName(int nTitle, int nSavedGame, TCHAR* szPath);

	void AddSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName, FILETIME saveTime);
	void RemoveTitle(int nTitle);
	void RemoveSavedGame(int nTitle, int nSavedGame);
	void RemoveSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName);

protected:
	int FindTitle(const TCHAR* szTitleID);
	void AddTitle(const TCHAR* szTitleID);

	TCHAR m_root[4];          // "x:\" with x = 0 when nothing is mounted
	bool m_dirty;             // list is stale until the next Update
	int m_count;
	int m_capacity;
	CTitle* m_list;
	CRITICAL_SECTION m_lock;  // guards m_root against the scan thread
};

extern int ScanSavedGames(const TCHAR* szRoot, const TCHAR* szTitleID, CSave* rgsaves, HANDLE hCancelEvent);
extern int ComputeTitleTotalBlocks(const TCHAR* szRoot, const TCHAR* szTitleID, HANDLE hCancelEvent);

extern CTitleArray g_titles [9];
