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
#include "host.h"

#include <stdint.h>

extern char g_qcowPath[512];
FILE* XboxFS_FATXReadFile(const char* xboxPath);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool IsWriteDisposition(DWORD access, DWORD disp) {
	return (access & GENERIC_WRITE) || disp == CREATE_ALWAYS || disp == CREATE_NEW ||
	       disp == OPEN_ALWAYS || disp == TRUNCATE_EXISTING;
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
		Host_MakeDir(dir);
		*p = '/';
	}
	Host_MakeDir(dir);
}

static DWORD ToFileAttributes(uint32_t attrs) {
	DWORD a = 0;
	if (attrs & HOST_ATTR_DIR)      a |= FILE_ATTRIBUTE_DIRECTORY;
	if (attrs & HOST_ATTR_HIDDEN)   a |= FILE_ATTRIBUTE_HIDDEN;
	if (attrs & HOST_ATTR_SYSTEM)   a |= FILE_ATTRIBUTE_SYSTEM;
	if (attrs & HOST_ATTR_READONLY) a |= FILE_ATTRIBUTE_READONLY;
	return a ? a : FILE_ATTRIBUTE_NORMAL;
}

static FILETIME ToFileTime(int64_t unixSeconds) {
	uint64_t t = (uint64_t)(unixSeconds + 11644473600LL) * 10000000ULL;
	FILETIME ft;
	ft.dwLowDateTime  = (DWORD)(t & 0xFFFFFFFF);
	ft.dwHighDateTime = (DWORD)(t >> 32);
	return ft;
}

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

	// File handles are FILE*s, so a temp file gone on close does it.
	FILE* f = Host_TempFile();
	if (!f) return INVALID_HANDLE_VALUE;
	fwrite(buf, 1, n, f);
	fseek(f, 0, SEEK_SET);
	HANDLE h = (HANDLE)f;
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
}

bool TheseusCreateDirectory(LPCTSTR lpPathName, LPSECURITY_ATTRIBUTES)
{
	if (!lpPathName) return false;
	char xboxPath[MAX_PATH];
	CleanFilePath(xboxPath, lpPathName);
	char host[512];
	if (!XboxPath_TranslateForWrite(xboxPath, host, sizeof(host)) || !host[0]) return false;
	return Host_MakeDir(host);
}

BOOL TheseusDeleteFile(LPCSTR path)
{
	char host[512];
	if (!XboxPath_TranslateForWrite(path, host, sizeof(host)) || !host[0]) return FALSE;
	return Host_RemoveFile(host);
}

BOOL TheseusRemoveDirectory(LPCSTR path)
{
	char host[512];
	if (!XboxPath_TranslateForWrite(path, host, sizeof(host)) || !host[0]) return FALSE;
	return Host_RemoveDir(host);
}

BOOL TheseusSetFileAttributes(LPCSTR path, DWORD attr)
{
	char host[512];
	if (!XboxPath_TranslateForWrite(path, host, sizeof(host)) || !host[0]) return FALSE;
	uint32_t a = 0;
	if (attr & FILE_ATTRIBUTE_HIDDEN)   a |= HOST_ATTR_HIDDEN;
	if (attr & FILE_ATTRIBUTE_SYSTEM)   a |= HOST_ATTR_SYSTEM;
	if (attr & FILE_ATTRIBUTE_READONLY) a |= HOST_ATTR_READONLY;
	return Host_SetAttrs(host, a);
}

DWORD TheseusGetFileAttributes(LPCSTR path)
{
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return INVALID_FILE_ATTRIBUTES;
	HostFileInfo info;
	if (!Host_Stat(host, &info)) return INVALID_FILE_ATTRIBUTES;
	return ToFileAttributes(info.attrs);
}

BOOL TheseusGetFileAttributesEx(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID out)
{
	(void)level;
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return FALSE;
	HostFileInfo info;
	if (!Host_Stat(host, &info)) return FALSE;
	if (out) {
		WIN32_FILE_ATTRIBUTE_DATA* fad = (WIN32_FILE_ATTRIBUTE_DATA*)out;
		memset(fad, 0, sizeof(*fad));
		fad->dwFileAttributes = ToFileAttributes(info.attrs);
		fad->nFileSizeLow = (DWORD)(info.size & 0xFFFFFFFF);
		fad->nFileSizeHigh = (DWORD)(info.size >> 32);
		fad->ftLastWriteTime = ToFileTime(info.mtime);
		fad->ftCreationTime = fad->ftLastAccessTime = fad->ftLastWriteTime;
	}
	return TRUE;
}

BOOL TheseusGetDiskFreeSpaceEx(LPCSTR path, ULARGE_INTEGER* pFree, ULARGE_INTEGER* pTotal, ULARGE_INTEGER* pTotalFree)
{
	char host[512];
	if (!XboxPath_Translate(path, host, sizeof(host)) || !host[0]) return FALSE;
	uint64_t freeBytes = 0, totalBytes = 0;
	if (!Host_DiskSpace(host, &freeBytes, &totalBytes)) return FALSE;
	if (pFree) pFree->QuadPart = freeBytes;
	if (pTotal) pTotal->QuadPart = totalBytes;
	if (pTotalFree) pTotalFree->QuadPart = freeBytes;
	return TRUE;
}

// Always replaces, like the MOVEFILE_REPLACE_EXISTING every caller passes.
BOOL TheseusMoveFile(LPCSTR src, LPCSTR dst, DWORD flags)
{
	(void)flags;
	char s[512], d[512];
	if (!XboxPath_TranslateForWrite(src, s, sizeof(s)) || !s[0]) return FALSE;
	if (!XboxPath_TranslateForWrite(dst, d, sizeof(d)) || !d[0]) return FALSE;
	return Host_RenameReplace(s, d);
}

BOOL TheseusCopyFile(LPCSTR src, LPCSTR dst, BOOL bFailIfExists)
{
	char s[512], d[512];
	if (!XboxPath_Translate(src, s, sizeof(s)) || !s[0]) return FALSE;
	if (!XboxPath_TranslateForWrite(dst, d, sizeof(d)) || !d[0]) return FALSE;
	return Host_CopyFile(s, d, bFailIfExists != FALSE);
}

// Swaps a finished szNew in for szFile with one rename, keeping what it
// replaces as <file>.old.
bool TheseusReplaceFile(const TCHAR* szNew, const TCHAR* szFile)
{
	if (!szNew || !szFile) return false;
	char xboxNew[MAX_PATH], xboxFile[MAX_PATH];
	CleanFilePath(xboxNew, szNew);
	CleanFilePath(xboxFile, szFile);
	char n[512], f[512];
	if (!XboxPath_TranslateForWrite(xboxNew, n, sizeof(n)) || !n[0]) return false;
	if (!XboxPath_TranslateForWrite(xboxFile, f, sizeof(f)) || !f[0]) return false;

	if (!Host_FlushToDisk(n)) return false;
	HostFileInfo info;
	if (Host_Stat(f, &info)) {
		char old[520];
		snprintf(old, sizeof(old), "%s.old", f);
		Host_CopyFile(f, old, false);
	}
	return Host_RenameReplace(n, f);
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
	HostDir* real;              // NULL once the real entries run out
	char dirPath[512];          // translated host dir
	char pattern[256];          // basename pattern from the call
	int  vgIndices[VGAMES_MAX];
	int  vgCount;
	int  vgPos;
};

// '*' any run, '?' one char, case-insensitive. Win32 rules, so every host
// matches the same names.
bool PatternMatch(const char* pattern, const char* name) {
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
}

bool OnDisk(const FindHandle* ffh, const char* name) {
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", ffh->dirPath, name);
	return Host_Stat(full, NULL);
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

BOOL NextReal(FindHandle* ffh, WIN32_FIND_DATAA* fd) {
	char name[260];
	HostFileInfo info;
	while (Host_ReadDir(ffh->real, name, sizeof(name), &info)) {
		// FATX has no . or .. entries, so Xbox code never expects them.
		if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
		if (!PatternMatch(ffh->pattern, name)) continue;
		memset(fd, 0, sizeof(*fd));
		strncpy(fd->cFileName, name, sizeof(fd->cFileName) - 1);
		fd->dwFileAttributes = ToFileAttributes(info.attrs);
		fd->nFileSizeLow = (DWORD)(info.size & 0xFFFFFFFF);
		fd->nFileSizeHigh = (DWORD)(info.size >> 32);
		fd->ftLastWriteTime = ToFileTime(info.mtime);
		fd->ftCreationTime = fd->ftLastAccessTime = fd->ftLastWriteTime;
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
	if (PatternMatch("*.xbe", pattern) || PatternMatch("default.xbe", pattern)) {
		name = "default.xbe";
		size = 4096;
	} else if (PatternMatch("default.uixshortcut", pattern) ||
	           PatternMatch("*.*", pattern) || strcmp(pattern, "*") == 0) {
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

	ffh->real = Host_OpenDir(dirPath);
	if (!ffh->real) {
		free(ffh);
		return FindInVirtualFolder(dirPath, pattern, fd);
	}
	if (NextReal(ffh, fd)) return (HANDLE)ffh;
	Host_CloseDir(ffh->real);
	ffh->real = NULL;

	if (NextVirtual(ffh, fd)) return (HANDLE)ffh;
	free(ffh);
	return INVALID_HANDLE_VALUE;
}

BOOL TheseusFindNextFile(HANDLE h, WIN32_FIND_DATAA* fd)
{
	if (!h || h == INVALID_HANDLE_VALUE || !fd) return FALSE;
	FindHandle* ffh = (FindHandle*)h;
	if (ffh->magic != kFindMagic) return FALSE;
	if (ffh->real) {
		if (NextReal(ffh, fd)) return TRUE;
		Host_CloseDir(ffh->real);
		ffh->real = NULL;
	}
	return NextVirtual(ffh, fd);
}

BOOL TheseusFindClose(HANDLE h)
{
	if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
	FindHandle* ffh = (FindHandle*)h;
	if (ffh->magic != kFindMagic) return FALSE;
	if (ffh->real) Host_CloseDir(ffh->real);
	free(ffh);
	return TRUE;
}
