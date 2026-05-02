// titlecollection.h: desktop CTitleArray declarations. Counterpart
// to shared/title_collection.h; some Xbox-side fields are stubbed
// or backed by qcow2 reads through xbox_hdd.cpp.

#pragma once

#define MAX_SAVED_GAMES 4096

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


struct CSave
{
//	uint32_t m_dwID;
	char m_szDirName[16];
	FILETIME m_filetime;
	uint32_t m_dwFlags;

	bool SetDirName(const char* szDirName);
	const char* GetDirName();
};

#define SAVEFLAG_HASIMAGE	0x00000001
#define SAVEFLAG_UNKIMAGE	0x00000002

class CTitle
{
public:
	char* m_szID;
	char* m_szName;
	int m_nSavedGameCount;
	CSave* m_rgsaves;
	int m_nSavedGameBlocks;
	int m_nTotalBlocks;
	bool m_bBroken;
};

class CTitleArray
{
public:
	CTitleArray();
	~CTitleArray();

	void Update();
	void DeleteAll(bool bUpdate = true);

	void SetRoot(char chNewRoot, bool bUpdate = true);

	int GetTitleCount();
	int GetTitleCount2();
	bool IsBroken(int nTitle);
	const char* GetTitleID(int nTitle);
	const char* GetTitleName(int nTitle);
	const char* GetTitleName2(int nTitle);
	int GetTitleTotalBlocks(int nTitle, HANDLE hCancelEvent);

	int GetSavedGameCount(int nTitle, HANDLE hCancelEvent = NULL);
	const char* GetSavedGameID(int nTitle, int nSavedGame);
	void GetSavedGameImageName(int nTitle, int nSavedGame, char* szPath/*[MAX_PATH]*/);
	FILETIME GetSavedGameTime(int nTitle, int nSavedGame);

	void AddSavedGame(const char* szTitleID, const char* szDirName, FILETIME saveTime);

	bool IsValid() const;
	bool IsDirty() const;
	const char* GetTData() const;
	const char* GetUData() const;

	void RemoveTitle(int nTitle);
	void RemoveSavedGame(int nTitle, int nSavedGame);
	void RemoveSavedGame(const char* szTitleID, const char* szDirName);
    bool IsPublisherExists(const char* szPublisherID) const;

protected:
	int FindTitle(const char* szTitleID);
	void AddTitle(const char* szTitleID);

	char m_szRoot [4];
	bool m_bDirty;
	int m_nTitleCount;
	int m_nTitleAlloc;
	CTitle* m_rgtitle;

    CRITICAL_SECTION m_RootLock;

	friend class CTitleCollection;
	friend void TitleArray_Init();
};

/*
class CTitleCollection : public CNode
{
	DECLARE_NODE(CTitleCollection, CNode)
public:
	CTitleCollection();
	~CTitleCollection();

	void Advance(float nSeconds);

	void SetLanguage(int nLanguage);
	int GetTitleCount();
	CStrObject* GetTitleID(int nTitle);
	CStrObject* GetTitleName(int nTitle);

	int GetSavedGameCount(int nTitle);
	CStrObject* GetSavedGameID(int nTitle, int nSavedGame);

//	int GetTitleSavedGameBlocks(int nTitle);
	int GetTitleTotalBlocks(int nTitle);

protected:
	int m_nCurLanguage;

	DECLARE_NODE_FUNCTIONS()
};
*/


extern CTitleArray g_titles [9];
