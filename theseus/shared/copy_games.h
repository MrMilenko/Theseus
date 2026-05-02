// copy_games.h: CGameCopier, the cross-device save-game copier used
// by the Memory pane (HDD to MU). Background thread copies game
// directories, reports progress, supports cancel and already-exists
// detection. No matching .cpp; implementation lives in
// xbox/copy_games.cpp and desktop/desktop_nodes.cpp.

#pragma once

class CGameCopier
{
public:
	CGameCopier();
	~CGameCopier();

	void SetSource(int nDevUnit);
	void SetDestination(int nDevUnit);
	void AddGame(const TCHAR* szTitleID, const TCHAR* szGameDir, FILETIME saveTime, int nBlocks);

	void Start();
	void Finish();

	float m_progress;
	bool m_error;
	bool m_done;

	bool DeleteDirectory(const TCHAR* szDir, bool RemoveSelf = true);

private:
	HANDLE m_thread;
	int m_srcDevUnit;
	int m_destDevUnit;
	TCHAR* m_srcRoot;
	TCHAR* m_destRoot;
	struct CCopyGame* m_copyGames;
	int m_copyGameCount;
	int m_copyGameCapacity;
	BYTE* m_buffer;
	bool m_internalError;
	bool m_alreadyExists;

	int m_copyGameCur;
	int m_totalBlocks;
	int m_copiedBlocks;

	DWORD ThreadProc();
	void CopyGame(int nCopyGame);
	bool CreateDirectory(const TCHAR* szDir);
	bool CopyDirectory(const TCHAR* szSrcDir, const TCHAR* szDestDir);
	bool CopyFile(const TCHAR* szSrcFile, const TCHAR* szDestFile);
	bool DeleteFile(const TCHAR* szFile);
	void RemoveAll();

	static DWORD CALLBACK StartThread(LPVOID pvContext);
};

