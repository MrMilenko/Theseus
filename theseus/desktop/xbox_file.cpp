// xbox_file.cpp: the desktop side of the Theseus file calls in theseus.h.
// Every path in here is an Xbox path. It goes through xbox_path.h first,
// then hits the host. Host paths never come through here; code holding one
// calls the OS directly.
//
// Virtual games live here too: a folder that only exists in games.ini still
// lists, and its default.xbe opens as a synthesized header.

#include "std.h"
#include "theseus.h"
#include "file_util.h"

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#ifdef THESEUS_HOST_WIN
#include <direct.h>
#else
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#endif

extern char g_qcowPath[512];
FILE* XboxFS_FATXReadFile(const char* xboxPath);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool IsWriteDisposition(DWORD access, DWORD disp) {
	return (access & GENERIC_WRITE) || disp == CREATE_ALWAYS || disp == CREATE_NEW ||
	       disp == OPEN_ALWAYS || disp == TRUNCATE_EXISTING;
}

static void HostMkdir(const char* dir) {
#ifdef THESEUS_HOST_WIN
	_mkdir(dir);
#else
	mkdir(dir, 0755);
#endif
}

// Creates every missing parent of a translated host path.
static void MakeParents(const char* hostPath) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", hostPath);
	char* last = strrchr(dir, '/');
	if (!last) return;
	*last = '\0';
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		HostMkdir(dir);
		*p = '/';
	}
	HostMkdir(dir);
}

#ifdef THESEUS_HOST_WIN
static void ToBackslash(char* s) {
	for (; *s; s++) if (*s == '/') *s = '\\';
}
#endif

// Minimal fake XBE for a virtual game: a 0x178-byte header then a 0x1EC-byte
// certificate carrying the title name (UTF-16) and title ID. Title
// enumeration only reads those two structures.
static size_t FillFakeXBE(uint8_t* buf, size_t bufSize, const char* titleName, unsigned long titleID) {
	const size_t need = 0x178 + 0x1EC;
	if (bufSize < need) return 0;
	memset(buf, 0, need);

	buf[0] = 'X'; buf[1] = 'B'; buf[2] = 'E'; buf[3] = 'H';

	uint32_t base = 0x00010000;
	memcpy(buf + 0x104, &base, 4);
	uint32_t hdrSize = (uint32_t)need;
	memcpy(buf + 0x108, &hdrSize, 4);
	uint32_t certAddr = base + 0x178;
	memcpy(buf + 0x118, &certAddr, 4);

	uint8_t* cert = buf + 0x178;
	uint32_t certSize = 0x1EC;
	uint32_t tid = (uint32_t)titleID;
	memcpy(cert + 0x00, &certSize, 4);
	memcpy(cert + 0x08, &tid, 4);

	uint16_t* titleNameU = (uint16_t*)(cert + 0x0C);
	for (int i = 0; i < 40 && titleName[i]; i++)
		titleNameU[i] = (uint16_t)(unsigned char)titleName[i];

	return need;
}

// If hostPath is <virtual game folder>/default.xbe, hand back a fake one.
static HANDLE OpenVirtualXBE(const char* hostPath) {
	char folder[512];
	snprintf(folder, sizeof(folder), "%s", hostPath);
	for (char* p = folder; *p; p++) if (*p == '\\') *p = '/';
	char* last = strrchr(folder, '/');
	if (!last) return INVALID_HANDLE_VALUE;
	const char* fileName = last + 1;
	*last = '\0';
	if (strcasecmp(fileName, "default.xbe") != 0) return INVALID_HANDLE_VALUE;

	int vgIdx = VGames_MatchFolder(folder);
	if (vgIdx < 0) return INVALID_HANDLE_VALUE;
	const VirtualGame& vg = g_vgames.games[vgIdx];
	unsigned long tid = strtoul(vg.titleID, NULL, 16);

	uint8_t buf[0x178 + 0x1EC];
	size_t n = FillFakeXBE(buf, sizeof(buf), vg.name, tid);
	if (!n) return INVALID_HANDLE_VALUE;

#ifdef THESEUS_HOST_WIN
	// Real HANDLEs and FILE*s aren't interchangeable here, so back it with a
	// temp file the OS deletes on close.
	char tempDir[MAX_PATH], tempFile[MAX_PATH];
	DWORD len = GetTempPathA(MAX_PATH, tempDir);
	if (len == 0 || len > MAX_PATH) return INVALID_HANDLE_VALUE;
	if (GetTempFileNameA(tempDir, "xbe", 0, tempFile) == 0) return INVALID_HANDLE_VALUE;
	HANDLE h = CreateFileA(tempFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
	                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
	DWORD written = 0;
	WriteFile(h, buf, (DWORD)n, &written, NULL);
	SetFilePointer(h, 0, NULL, FILE_BEGIN);
#else
	// POSIX handles are FILE*, so an anonymous tmpfile does it.
	FILE* f = tmpfile();
	if (!f) return INVALID_HANDLE_VALUE;
	fwrite(buf, 1, n, f);
	fseek(f, 0, SEEK_SET);
	HANDLE h = (HANDLE)f;
#endif
	fprintf(stderr, "[xboxfs]   Virtual XBE for '%s' (TID=%08lX)\n", vg.name, tid);
	return h;
}

// ---------------------------------------------------------------------------
// Files and folders
// ---------------------------------------------------------------------------

HANDLE TheseusCreateFile(const TCHAR* szFileName, DWORD access, DWORD share,
                         LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE templ)
{
	if (!szFileName) return INVALID_HANDLE_VALUE;
	char xboxPath[MAX_PATH];
	CleanFilePath(xboxPath, szFileName);

	bool writes = IsWriteDisposition(access, disp);
	char host[512];
	if (writes) XboxPath_TranslateForWrite(xboxPath, host, sizeof(host));
	else        XboxPath_Translate(xboxPath, host, sizeof(host));
	if (!host[0]) return INVALID_HANDLE_VALUE;

#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	// Xbox flags desktop Windows can't honour: NO_BUFFERING wants sector
	// aligned reads, OVERLAPPED wants async setup.
	flags &= ~(FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED);
	HANDLE h = CreateFileA(host, access, share, sa, disp, flags, templ);
	if (h != INVALID_HANDLE_VALUE) return h;
	return writes ? INVALID_HANDLE_VALUE : OpenVirtualXBE(host);
#else
	(void)share; (void)sa; (void)flags; (void)templ;
	const char* mode = "rb";
	if (access & GENERIC_WRITE) mode = (disp == CREATE_ALWAYS) ? "wb" : "r+b";
	if (writes) MakeParents(host);
	FILE* f = fopen(host, mode);
	if (!f && (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS))
		f = fopen(host, "wb");
	if (f) return (HANDLE)f;
	if (writes) return INVALID_HANDLE_VALUE;

	HANDLE fake = OpenVirtualXBE(host);
	if (fake != INVALID_HANDLE_VALUE) return fake;

	// Save data can also come off a mounted qcow2 FATX image.
	if (g_qcowPath[0] && (strstr(xboxPath, "UDATA") || strstr(xboxPath, "TDATA"))) {
		f = XboxFS_FATXReadFile(xboxPath);
		if (f) {
			fprintf(stderr, "[xboxfs]   Loaded from qcow2 FATX\n");
			return (HANDLE)f;
		}
	}
	return INVALID_HANDLE_VALUE;
#endif
}

bool TheseusCreateDirectory(LPCTSTR lpPathName, LPSECURITY_ATTRIBUTES)
{
	if (!lpPathName) return false;
	char xboxPath[MAX_PATH];
	CleanFilePath(xboxPath, lpPathName);
	char host[512];
	if (!XboxPath_TranslateForWrite(xboxPath, host, sizeof(host)) || !host[0]) return false;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return CreateDirectoryA(host, NULL) != FALSE;
#else
	return mkdir(host, 0755) == 0 || errno == EEXIST;
#endif
}

BOOL TheseusDeleteFile(LPCSTR path)
{
	char host[512];
	if (!XboxPath_TranslateForWrite(path, host, sizeof(host)) || !host[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return DeleteFileA(host);
#else
	return remove(host) == 0;
#endif
}

BOOL TheseusRemoveDirectory(LPCSTR path)
{
	char host[512];
	if (!XboxPath_TranslateForWrite(path, host, sizeof(host)) || !host[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return RemoveDirectoryA(host);
#else
	return rmdir(host) == 0;
#endif
}

BOOL TheseusSetFileAttributes(LPCSTR path, DWORD attr)
{
	char host[512];
	if (!XboxPath_TranslateForWrite(path, host, sizeof(host)) || !host[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return SetFileAttributesA(host, attr);
#else
	(void)attr;   // FATX attribute bits have no POSIX meaning worth keeping
	return TRUE;
#endif
}

DWORD TheseusGetFileAttributes(LPCSTR path)
{
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return INVALID_FILE_ATTRIBUTES;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return GetFileAttributesA(host);
#else
	struct stat st;
	if (stat(host, &st) != 0) return INVALID_FILE_ATTRIBUTES;
	return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
#endif
}

BOOL TheseusGetFileAttributesEx(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return GetFileAttributesExA(host, level, info);
#else
	(void)level;
	struct stat st;
	if (stat(host, &st) != 0) return FALSE;
	if (info) {
		WIN32_FILE_ATTRIBUTE_DATA* fad = (WIN32_FILE_ATTRIBUTE_DATA*)info;
		memset(fad, 0, sizeof(*fad));
		fad->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
		fad->nFileSizeLow = (DWORD)(st.st_size & 0xFFFFFFFF);
		fad->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
	}
	return TRUE;
#endif
}

BOOL TheseusGetDiskFreeSpaceEx(LPCSTR path, ULARGE_INTEGER* pFree, ULARGE_INTEGER* pTotal, ULARGE_INTEGER* pTotalFree)
{
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(host);
	return GetDiskFreeSpaceExA(host, pFree, pTotal, pTotalFree);
#else
	// No real free-space query on POSIX yet; report empty like it always has.
	if (pFree) pFree->QuadPart = 0;
	if (pTotal) pTotal->QuadPart = 0;
	if (pTotalFree) pTotalFree->QuadPart = 0;
	return TRUE;
#endif
}

BOOL TheseusMoveFile(LPCSTR src, LPCSTR dst, DWORD flags)
{
	char s[512], d[512];
	if (!XboxPath_TranslateForWrite(src, s, sizeof(s)) || !s[0]) return FALSE;
	if (!XboxPath_TranslateForWrite(dst, d, sizeof(d)) || !d[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(s);
	ToBackslash(d);
	return MoveFileExA(s, d, flags);
#else
	(void)flags;
	return rename(s, d) == 0;
#endif
}

BOOL TheseusCopyFile(LPCSTR src, LPCSTR dst, BOOL bFailIfExists)
{
	char s[512], d[512];
	if (!XboxPath_Translate(src, s, sizeof(s)) || !s[0]) return FALSE;
	if (!XboxPath_TranslateForWrite(dst, d, sizeof(d)) || !d[0]) return FALSE;
#ifdef THESEUS_HOST_WIN
	ToBackslash(s);
	ToBackslash(d);
	return CopyFileA(s, d, bFailIfExists);
#else
	struct stat st;
	if (bFailIfExists && stat(d, &st) == 0) return FALSE;
	FILE* in = fopen(s, "rb");
	if (!in) return FALSE;
	FILE* out = fopen(d, "wb");
	if (!out) { fclose(in); return FALSE; }
	char buf[64 * 1024];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	fclose(in);
	if (fclose(out) != 0) ok = false;
	return ok;
#endif
}

FILE* TheseusFOpen(const TCHAR* path, const TCHAR* mode)
{
	if (!path || !mode) return NULL;
	// A bare relative name has no drive to map.
	if (!strchr(path, ':')) return fopen(path, mode);

	bool writes = strpbrk(mode, "wa+") != NULL;
	char host[512];
	if (writes) XboxPath_TranslateForWrite(path, host, sizeof(host));
	else        XboxPath_Translate(path, host, sizeof(host));
	if (!host[0]) return NULL;
	if (mode[0] == 'w' || mode[0] == 'a') MakeParents(host);
	return fopen(host, mode);
}

// ---------------------------------------------------------------------------
// Directory listing. Real entries first, then virtual games for the same
// folder, minus any that also exist on disk.
// ---------------------------------------------------------------------------

namespace {

const DWORD kFindMagic = 0x46464858;   // 'XHFF'

struct FindHandle {
	DWORD magic;
#ifdef THESEUS_HOST_WIN
	HANDLE real;                // Win32 find handle, or INVALID_HANDLE_VALUE
#else
	DIR* real;
#endif
	char dirPath[512];          // translated host dir
	char pattern[256];          // basename pattern from the call
	int  vgIndices[VGAMES_MAX];
	int  vgCount;
	int  vgPos;
};

bool PatternMatch(const char* pattern, const char* name) {
#ifdef THESEUS_HOST_WIN
	// '*' any run, '?' one char, case-insensitive.
	while (*pattern && *name) {
		if (*pattern == '*') {
			pattern++;
			if (!*pattern) return true;
			for (; *name; name++)
				if (PatternMatch(pattern, name)) return true;
			return false;
		}
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name))
			return false;
		pattern++;
		name++;
	}
	while (*pattern == '*') pattern++;
	return *pattern == 0 && *name == 0;
#else
	return fnmatch(pattern, name, FNM_CASEFOLD) == 0;
#endif
}

bool OnDisk(const FindHandle* ffh, const char* name) {
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", ffh->dirPath, name);
	struct stat st;
	return stat(full, &st) == 0;
}

BOOL NextVirtual(FindHandle* ffh, WIN32_FIND_DATAA* fd) {
	while (ffh->vgPos < ffh->vgCount) {
		int idx = ffh->vgIndices[ffh->vgPos++];
		if (idx < 0 || idx >= VGAMES_MAX) continue;
		const VirtualGame& vg = g_vgames.games[idx];
		if (!vg.valid) continue;
		if (!PatternMatch(ffh->pattern, vg.name)) continue;
		if (OnDisk(ffh, vg.name)) continue;   // a real folder wins
		memset(fd, 0, sizeof(*fd));
		strncpy(fd->cFileName, vg.name, sizeof(fd->cFileName) - 1);
		fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		return TRUE;
	}
	return FALSE;
}

#ifndef THESEUS_HOST_WIN
BOOL NextReal(FindHandle* ffh, WIN32_FIND_DATAA* fd) {
	struct dirent* entry;
	while ((entry = readdir(ffh->real)) != NULL) {
		if (!PatternMatch(ffh->pattern, entry->d_name)) continue;
		memset(fd, 0, sizeof(*fd));
		strncpy(fd->cFileName, entry->d_name, sizeof(fd->cFileName) - 1);
		char full[768];
		snprintf(full, sizeof(full), "%s/%s", ffh->dirPath, entry->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			if (S_ISDIR(st.st_mode)) fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
			fd->nFileSizeLow = (DWORD)st.st_size;
		}
		return TRUE;
	}
	return FALSE;
}

// A folder that only exists in games.ini still answers for its default.xbe
// (or its shortcut), so title enumeration finds the game.
HANDLE FindInVirtualFolder(const char* dirPath, const char* pattern, WIN32_FIND_DATAA* fd) {
	if (VGames_MatchFolder(dirPath) < 0) return INVALID_HANDLE_VALUE;
	const char* name = NULL;
	DWORD size = 0;
	if (fnmatch("*.xbe", pattern, FNM_CASEFOLD) == 0 || fnmatch("default.xbe", pattern, FNM_CASEFOLD) == 0) {
		name = "default.xbe";
		size = 4096;
	} else if (fnmatch("default.uixshortcut", pattern, FNM_CASEFOLD) == 0 ||
	           fnmatch("*.*", pattern, FNM_CASEFOLD) == 0 || strcmp(pattern, "*") == 0) {
		name = "default.uixshortcut";
		size = 256;
	}
	if (!name) return INVALID_HANDLE_VALUE;

	FindHandle* ffh = (FindHandle*)calloc(1, sizeof(FindHandle));
	if (!ffh) return INVALID_HANDLE_VALUE;
	ffh->magic = kFindMagic;
	snprintf(ffh->dirPath, sizeof(ffh->dirPath), "%s", dirPath);
	snprintf(ffh->pattern, sizeof(ffh->pattern), "%s", pattern);
	memset(fd, 0, sizeof(*fd));
	strcpy(fd->cFileName, name);
	fd->nFileSizeLow = size;
	return (HANDLE)ffh;
}
#endif

} // namespace

HANDLE TheseusFindFirstFile(LPCSTR path, WIN32_FIND_DATAA* fd)
{
	if (!path || !fd) return INVALID_HANDLE_VALUE;

	// "Q:\Fonts\*.xtf" -> "<data>/Fonts/*.xtf". A drive with no host analog
	// has nothing to list.
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return INVALID_HANDLE_VALUE;

	char dirPath[512], pattern[256];
	const char* sep = strrchr(host, '/');
	if (sep) {
		snprintf(dirPath, sizeof(dirPath), "%.*s", (int)(sep - host), host);
		snprintf(pattern, sizeof(pattern), "%s", sep + 1);
	} else {
		strcpy(dirPath, ".");
		snprintf(pattern, sizeof(pattern), "%s", host);
	}
	// "*.*" means everything on Win32, dotless names included.
	if (strcmp(pattern, "*.*") == 0) strcpy(pattern, "*");

	FindHandle* ffh = (FindHandle*)calloc(1, sizeof(FindHandle));
	if (!ffh) return INVALID_HANDLE_VALUE;
	ffh->magic = kFindMagic;
	snprintf(ffh->dirPath, sizeof(ffh->dirPath), "%s", dirPath);
	snprintf(ffh->pattern, sizeof(ffh->pattern), "%s", pattern);

	char drive[4] = {0}, cat[32] = {0};
	if (VGames_ParseDir(dirPath, drive, cat, sizeof(cat)))
		ffh->vgCount = VGames_GetForDirectory(drive, cat, ffh->vgIndices, VGAMES_MAX);

#ifdef THESEUS_HOST_WIN
	char winPath[512];
	snprintf(winPath, sizeof(winPath), "%s", host);
	ToBackslash(winPath);
	ffh->real = FindFirstFileA(winPath, fd);
	if (ffh->real != INVALID_HANDLE_VALUE) return (HANDLE)ffh;
#else
	ffh->real = opendir(dirPath);
	if (!ffh->real) {
		free(ffh);
		return FindInVirtualFolder(dirPath, pattern, fd);
	}
	if (NextReal(ffh, fd)) return (HANDLE)ffh;
	closedir(ffh->real);
	ffh->real = NULL;
#endif

	if (NextVirtual(ffh, fd)) return (HANDLE)ffh;
	free(ffh);
	return INVALID_HANDLE_VALUE;
}

BOOL TheseusFindNextFile(HANDLE h, WIN32_FIND_DATAA* fd)
{
	if (!h || h == INVALID_HANDLE_VALUE || !fd) return FALSE;
	FindHandle* ffh = (FindHandle*)h;
	if (ffh->magic != kFindMagic) return FALSE;
#ifdef THESEUS_HOST_WIN
	if (ffh->real != INVALID_HANDLE_VALUE) {
		if (FindNextFileA(ffh->real, fd)) return TRUE;
		FindClose(ffh->real);
		ffh->real = INVALID_HANDLE_VALUE;
	}
#else
	if (ffh->real) {
		if (NextReal(ffh, fd)) return TRUE;
		closedir(ffh->real);
		ffh->real = NULL;
	}
#endif
	return NextVirtual(ffh, fd);
}

BOOL TheseusFindClose(HANDLE h)
{
	if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
	FindHandle* ffh = (FindHandle*)h;
	if (ffh->magic != kFindMagic) return FALSE;
#ifdef THESEUS_HOST_WIN
	if (ffh->real != INVALID_HANDLE_VALUE) FindClose(ffh->real);
#else
	if (ffh->real) closedir(ffh->real);
#endif
	free(ffh);
	return TRUE;
}
