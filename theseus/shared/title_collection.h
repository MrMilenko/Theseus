// title_collection.h: CTitleArray and the on-disk save-game record
// types used by the memory pane. CSave is one save directory entry,
// CTitle is one game-with-saves bucket, CTitleArray is the full
// per-device list. Companion to shared/title_collection.cpp.

#pragma once

#define MAX_SAVED_GAMES 4096

extern const TCHAR szTitleDataXBX [];
extern const TCHAR szTitleImageXBX [];
extern const TCHAR szSaveDataXBX [];
extern const TCHAR szSaveImageXBX [];


#define Dev1Unit1	0
#define Dev1Unit2	1
#define Dev2Unit1	2
#define Dev2Unit2	3
#define Dev3Unit1	4
#define Dev3Unit2	5
#define Dev4Unit1	6
#define Dev4Unit2	7
#define Dev0		8



#define BLOCK_SIZE	16384 // Should be max of hard drive and flash block size...


// One persisted save record on disk: a directory name (the FATX
// directory the save lives in), the file timestamp, and a small flags
// bitfield (currently SAVEFLAG_HASIMAGE / SAVEFLAG_UNKIMAGE).
struct CSave
{
	TCHAR m_dirName[16];
	FILETIME m_fileTime;
	DWORD m_flags;

	bool SetDirName(const TCHAR* szDirName);
	const TCHAR* GetDirName();
};

#define SAVEFLAG_HASIMAGE	0x00000001
#define SAVEFLAG_UNKIMAGE	0x00000002

// One title (game) the save-game grid knows about. Holds the title id
// and display name, the array of CSave records that belong to it, and
// some block-count caches the grid uses to render storage usage.
class CTitle
{
public:
	TCHAR* m_id;
	TCHAR* m_name;
	int m_savedGameCount;
	CSave* m_saves;
	int m_savedGameBlocks;
	int m_totalBlocks;
	bool m_broken;
};

class CTitleArray
{
public:
	CTitleArray();
	~CTitleArray();

	void Update();
	void DeleteAll(bool bUpdate = true);

	void SetRoot(TCHAR chNewRoot, bool bUpdate = true);

	int GetTitleCount();
	int GetTitleCount2();
	bool IsBroken(int nTitle);
	const TCHAR* GetTitleID(int nTitle);
	const TCHAR* GetTitleName(int nTitle);
	const TCHAR* GetTitleName2(int nTitle);
	int GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent);

	int GetSavedGameCount(int nTitle, HANDLE hCancelEvent = NULL);
	const TCHAR* GetSavedGameID(int nTitle, int nSavedGame);
	void GetSavedGameImageName(int nTitle, int nSavedGame, TCHAR* szPath/*[MAX_PATH]*/);
	FILETIME GetSavedGameTime(int nTitle, int nSavedGame);

	void AddSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName, FILETIME saveTime);

	bool IsValid() const;
	bool IsDirty() const;
	const TCHAR* GetTData() const;
	const TCHAR* GetUData() const;

	void RemoveTitle(int nTitle);
	void RemoveSavedGame(int nTitle, int nSavedGame);
	void RemoveSavedGame(const TCHAR* szTitleID, const TCHAR* szDirName);
    bool IsPublisherExists(const TCHAR* szPublisherID) const;

protected:
	int FindTitle(const TCHAR* szTitleID);
	void AddTitle(const TCHAR* szTitleID);

	TCHAR m_root [4];     // drive letter prefix, e.g. "U:\"
	bool m_dirty;         // titles need re-scan since last Update
	int m_titleCount;
	int m_titleCapacity;
	CTitle* m_titles;

    CRITICAL_SECTION m_rootLock;

	friend class CTitleCollection;
	friend void TitleArray_Init();
};

extern int ScanSavedGames(const TCHAR* szRoot, const TCHAR* szTitleID, CSave* rgsaves /*[MAX_SAVED_GAMES]*/, HANDLE hCancelEvent);
extern int ComputeTitleTotalBlocks(const TCHAR* szRoot, const TCHAR* szTitleID, HANDLE hCancelEvent);

extern CTitleArray g_titles [9];
