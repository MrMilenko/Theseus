// music_collection.cpp: directory-based music collection. Scans
// E:\Music\ for soundtrack folders containing audio files; populates
// the dashboard's music UI. MP3 decoding via minimp3 (CC0 public
// domain, https://github.com/lieff/minimp3).
//
// Folder structure: E:\Music\<SoundtrackName>\<song>.mp3

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

// minimp3 types and stdcall wrappers (implementation in minimp3_decode.cpp)
#define MINIMP3_NO_STDINT
#include "toolbox/xboxinternals.h"
#include "minimp3.h"

extern void mp3dec_init_s(mp3dec_t *dec);
extern int mp3dec_decode_frame_s(mp3dec_t *dec, const uint8_t *mp3,
    int mp3_bytes, mp3d_sample_t *pcm, mp3dec_frame_info_t *info);

// Legacy dashst.c API stubs. Referenced by savegame_grid.cpp for
// storage management; return empty data since we don't use a separate
// soundtrack database.
int GetSoundtrackCount() { return 0; }
const TCHAR* GetSoundtrackName(int) { return _T(""); }
int GetSoundtrackSize(int, HANDLE) { return 0; }
void DeleteSoundtrack(int) {}
void DeleteAllSoundtracks() {}

#define MUSIC_ROOT       "E:\\Music"
#define MAX_SOUNDTRACKS  64
#define MAX_SONGS        256
#define MAX_NAME_LEN     128
#define MAX_PATH_LEN     260

struct Song
{
	char name[MAX_NAME_LEN];
	char path[MAX_PATH_LEN];
	int  duration; // seconds
};

struct Soundtrack
{
	int  id;
	char name[MAX_NAME_LEN];
	Song songs[MAX_SONGS];
	int  songCount;
};

static Soundtrack g_soundtracks[MAX_SOUNDTRACKS];
static int g_soundtrackCount = 0;
static int g_nextID = 1;


// Helpers

static bool IsMP3(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot) return false;
	dot++;
	return (_stricmp(dot, "mp3") == 0);
}

static void StripExtension(const char* src, char* dst, int maxLen)
{
	strncpy(dst, src, maxLen - 1);
	dst[maxLen - 1] = 0;
	char* dot = strrchr(dst, '.');
	if (dot) *dot = 0;
}

static int ProbeMP3Duration(const char* path)
{
	HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return 0;

	DWORD fileSize = GetFileSize(hFile, NULL);
	if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)
	{
		CloseHandle(hFile);
		return 0;
	}

	// Read first chunk to find a valid frame and get bitrate
	BYTE buf[4096];
	DWORD bytesRead = 0;
	ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL);
	CloseHandle(hFile);

	if (bytesRead < 128) return 0;

	mp3dec_t dec;
	mp3dec_init_s(&dec);

	mp3dec_frame_info_t info;
	mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	mp3dec_decode_frame_s(&dec, buf, bytesRead, pcm, &info);

	if (info.bitrate_kbps <= 0)
	{
		// Fallback: estimate assuming 128kbps
		return (int)(fileSize / 16000);
	}

	// duration = file_size_bytes / (bitrate_kbps * 1000 / 8)
	return (int)(fileSize / (info.bitrate_kbps * 125));
}


// Scanner

static void ScanSoundtrackDir(const char* stPath, const char* dirName)
{
	if (g_soundtrackCount >= MAX_SOUNDTRACKS) return;

	Soundtrack* st = &g_soundtracks[g_soundtrackCount];
	st->id = g_nextID++;
	strncpy(st->name, dirName, MAX_NAME_LEN - 1);
	st->name[MAX_NAME_LEN - 1] = 0;
	st->songCount = 0;

	char searchPath[MAX_PATH_LEN];
	_snprintf(searchPath, MAX_PATH_LEN, "%s\\*", stPath);

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(searchPath, &fd);
	if (hFind == INVALID_HANDLE_VALUE) return;

	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (!IsMP3(fd.cFileName)) continue;
		if (st->songCount >= MAX_SONGS) break;

		Song* song = &st->songs[st->songCount];
		StripExtension(fd.cFileName, song->name, MAX_NAME_LEN);
		_snprintf(song->path, MAX_PATH_LEN, "%s\\%s", stPath, fd.cFileName);
		song->duration = ProbeMP3Duration(song->path);
		st->songCount++;
	}
	while (FindNextFileA(hFind, &fd));
	FindClose(hFind);

	if (st->songCount > 0)
		g_soundtrackCount++;
}

static void ScanMusicRoot()
{
	g_soundtrackCount = 0;
	g_nextID = 1;

	char searchPath[MAX_PATH_LEN];
	_snprintf(searchPath, MAX_PATH_LEN, "%s\\*", MUSIC_ROOT);

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(searchPath, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		OutputDebugStringA("[Music] No music directory found (this is OK)\n");
		return;
	}

	do
	{
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (fd.cFileName[0] == '.') continue;

		char stPath[MAX_PATH_LEN];
		_snprintf(stPath, MAX_PATH_LEN, "%s\\%s", MUSIC_ROOT, fd.cFileName);
		ScanSoundtrackDir(stPath, fd.cFileName);
	}
	while (FindNextFileA(hFind, &fd));
	FindClose(hFind);

	if (g_soundtrackCount > 0)
	{
		char msg[128];
		_snprintf(msg, sizeof(msg), "[Music] %d soundtracks loaded\n", g_soundtrackCount);
		OutputDebugStringA(msg);
	}
}


// Song path lookup (used by AudioClip for st: URLs)

const char* MusicCollection_GetSongPath(int soundtrackIndex, int songIndex)
{
	if (soundtrackIndex < 0 || soundtrackIndex >= g_soundtrackCount) return NULL;
	Soundtrack* st = &g_soundtracks[soundtrackIndex];
	if (songIndex < 0 || songIndex >= st->songCount) return NULL;
	return st->songs[songIndex].path;
}

// Look up a song by the ID encoded in st: URLs
// The XAP scripts use GetSoundtrackSongID which we return as "songIndex"
// and GetSoundtrackID which we return as the soundtrack ID.
// So an st: URL has the format st:<soundtrackID>:<songIndex> or just st:<songID>
const char* MusicCollection_FindSongByID(int songID)
{
	// Simple linear search; iterate all soundtracks and songs.
	// Song IDs are encoded as (soundtrackIndex * MAX_SONGS + songIndex).
	int stIdx = songID / MAX_SONGS;
	int songIdx = songID % MAX_SONGS;
	return MusicCollection_GetSongPath(stIdx, songIdx);
}


// Formatting

static void FormatTime(int totalSeconds, TCHAR* buf, int bufLen)
{
	int hours = totalSeconds / 3600;
	int minutes = (totalSeconds % 3600) / 60;
	int seconds = totalSeconds % 60;

	if (hours > 0)
		_sntprintf(buf, bufLen, _T("%d:%02d:%02d"), hours, minutes, seconds);
	else
		_sntprintf(buf, bufLen, _T("%d:%02d"), minutes, seconds);
}


// CMusicCollection node

class CMusicCollection : public CNode
{
	DECLARE_NODE(CMusicCollection, CNode)
public:
	CMusicCollection();
	~CMusicCollection();

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	float m_copyProgress;
	int   m_error;

	int GetSoundtrackCount();
	int GetSoundtrackID(int nSoundtrack);
	int GetSoundtrackIndexFromID(int nSoundtrackID);
	CStrObject* GetSoundtrackName(int nSoundtrack);
	CStrObject* FormatSoundtrackTime(int nSoundtrack);
	int GetSoundtrackSongCount(int nSoundtrack);
	CStrObject* GetSoundtrackSongID(int nSoundtrack, int nSong);
	CStrObject* GetSoundtrackSongName(int nSoundtrack, int nSong);
	CStrObject* FormatSoundtrackSongTime(int nSoundtrack, int nSong);

	int AddSoundtrack(const TCHAR* szName);
	void DeleteSoundtrack(int nSoundtrack);
	void ClearCopyList(int nCopySongCount);
	void AddSongToCopyList(int nSoundtrack, int nSong);
	void StartCopy(int nDestSoundtrack);
	void SetSongName(int nSoundtrack, int nSong, const TCHAR* szName);
	void SetSoundtrackName(int nSoundtrack, const TCHAR* szName);
	void MoveSongUp(int nSoundtrack, int nSong);
	void MoveSongDown(int nSoundtrack, int nSong);
	void DeleteSong(int nSoundtrack, int nSong);
	CStrObject* CreateSoundtrackName(const TCHAR* szBaseName);
	CStrObject* GetUpdateString();
};

IMPLEMENT_NODE("MusicCollection", CMusicCollection, CNode)

START_NODE_PROPS(CMusicCollection, CNode)
	NODE_PROP(pt_number, CMusicCollection, copyProgress)
	NODE_PROP(pt_integer, CMusicCollection, error)
END_NODE_PROPS()

#define _FND_CLASS CMusicCollection
START_NODE_FUN(CMusicCollection, CNode)
	NODE_FUN_IV(GetSoundtrackCount)
	NODE_FUN_II(GetSoundtrackID)
	NODE_FUN_II(GetSoundtrackIndexFromID)
	NODE_FUN_SI(GetSoundtrackName)
	NODE_FUN_SI(FormatSoundtrackTime)
	NODE_FUN_II(GetSoundtrackSongCount)
	NODE_FUN_SII(GetSoundtrackSongID)
	NODE_FUN_SII(GetSoundtrackSongName)
	NODE_FUN_SII(FormatSoundtrackSongTime)
	NODE_FUN_IS(AddSoundtrack)
	NODE_FUN_VI(DeleteSoundtrack)
	NODE_FUN_VI(ClearCopyList)
	NODE_FUN_VII(AddSongToCopyList)
	NODE_FUN_VI(StartCopy)
	NODE_FUN_VIIS(SetSongName)
	NODE_FUN_VIS(SetSoundtrackName)
	NODE_FUN_VII(MoveSongUp)
	NODE_FUN_VII(MoveSongDown)
	NODE_FUN_VII(DeleteSong)
	NODE_FUN_SS(CreateSoundtrackName)
	NODE_FUN_SV(GetUpdateString)
END_NODE_FUN()
#undef _FND_CLASS


// Implementation

CMusicCollection::CMusicCollection()
{
	m_copyProgress = 0.0f;
	m_error = 0;
	ScanMusicRoot();
}

CMusicCollection::~CMusicCollection()
{
}

int CMusicCollection::GetSoundtrackCount()
{
	return g_soundtrackCount;
}

int CMusicCollection::GetSoundtrackID(int nSoundtrack)
{
	if (nSoundtrack < 0 || nSoundtrack >= g_soundtrackCount) return -1;
	return g_soundtracks[nSoundtrack].id;
}

int CMusicCollection::GetSoundtrackIndexFromID(int nSoundtrackID)
{
	for (int i = 0; i < g_soundtrackCount; i++)
		if (g_soundtracks[i].id == nSoundtrackID) return i;
	return -1;
}

CStrObject* CMusicCollection::GetSoundtrackName(int nSoundtrack)
{
	if (nSoundtrack < 0 || nSoundtrack >= g_soundtrackCount)
		return new CStrObject;

	TCHAR buf[MAX_NAME_LEN];
	Unicode(buf, g_soundtracks[nSoundtrack].name, MAX_NAME_LEN);
	return new CStrObject(buf);
}

CStrObject* CMusicCollection::FormatSoundtrackTime(int nSoundtrack)
{
	if (nSoundtrack < 0 || nSoundtrack >= g_soundtrackCount)
		return new CStrObject(_T("0:00"));

	int total = 0;
	Soundtrack* st = &g_soundtracks[nSoundtrack];
	for (int i = 0; i < st->songCount; i++)
		total += st->songs[i].duration;

	TCHAR buf[32];
	FormatTime(total, buf, 32);
	return new CStrObject(buf);
}

int CMusicCollection::GetSoundtrackSongCount(int nSoundtrack)
{
	if (nSoundtrack < 0 || nSoundtrack >= g_soundtrackCount) return 0;
	return g_soundtracks[nSoundtrack].songCount;
}

CStrObject* CMusicCollection::GetSoundtrackSongID(int nSoundtrack, int nSong)
{
	// Encode as (soundtrackIndex * MAX_SONGS + songIndex) so
	// MusicCollection_FindSongByID can decode it back
	TCHAR buf[16];
	_sntprintf(buf, 16, _T("%d"), nSoundtrack * MAX_SONGS + nSong);
	return new CStrObject(buf);
}

CStrObject* CMusicCollection::GetSoundtrackSongName(int nSoundtrack, int nSong)
{
	if (nSoundtrack < 0 || nSoundtrack >= g_soundtrackCount)
		return new CStrObject;
	Soundtrack* st = &g_soundtracks[nSoundtrack];
	if (nSong < 0 || nSong >= st->songCount)
		return new CStrObject;

	TCHAR buf[MAX_NAME_LEN];
	Unicode(buf, st->songs[nSong].name, MAX_NAME_LEN);
	return new CStrObject(buf);
}

CStrObject* CMusicCollection::FormatSoundtrackSongTime(int nSoundtrack, int nSong)
{
	if (nSoundtrack < 0 || nSoundtrack >= g_soundtrackCount)
		return new CStrObject(_T("0:00"));
	Soundtrack* st = &g_soundtracks[nSoundtrack];
	if (nSong < 0 || nSong >= st->songCount)
		return new CStrObject(_T("0:00"));

	TCHAR buf[32];
	FormatTime(st->songs[nSong].duration, buf, 32);
	return new CStrObject(buf);
}

// Editing operations: read-only for now.
int CMusicCollection::AddSoundtrack(const TCHAR*) { return -1; }
void CMusicCollection::DeleteSoundtrack(int) {}
void CMusicCollection::ClearCopyList(int) {}
void CMusicCollection::AddSongToCopyList(int, int) {}
void CMusicCollection::StartCopy(int) {}
void CMusicCollection::SetSongName(int, int, const TCHAR*) {}
void CMusicCollection::SetSoundtrackName(int, const TCHAR*) {}
void CMusicCollection::MoveSongUp(int, int) {}
void CMusicCollection::MoveSongDown(int, int) {}
void CMusicCollection::DeleteSong(int, int) {}
CStrObject* CMusicCollection::CreateSoundtrackName(const TCHAR*) { return new CStrObject(_T("New Soundtrack")); }
CStrObject* CMusicCollection::GetUpdateString() { return new CStrObject; }
