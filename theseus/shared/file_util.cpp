// file_util.cpp: the dashboard's path and file helpers. Joining a folder
// and a name, resolving a script URL against the current folder, cleaning
// a URL into a file path, and counting the blocks a file or folder takes.
// Rebuilt function by function from the 5960 retail XBE (0x2df60..0x2e520);
// see docs/decomp/FileUtil.md.

#include "std.h"
#include "theseus.h"
#include "file_util.h"

// 0x0017a480: the folder relative URLs resolve against.
TCHAR g_szCurDir[1024];

// DoesFileExist(path)
// Reconstructed from 5960 retail XBE @ 0x0002df60: narrows the path into
// 260 chars and asks GetFileAttributesExA.
bool DoesFileExist(const TCHAR* szFileName)
{
	CHAR narrow[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA info;
	Ansi(narrow, szFileName, MAX_PATH);
	return TheseusGetFileAttributesEx(narrow, GetFileExInfoStandard, &info) != 0;
}

// MakePath(out, dir, file)
// Reconstructed from 5960 retail XBE @ 0x0002dfc0:
//     n = wcslen(dir); if (out != dir) copy dir;
//     if (out[n-1] != '\\') out[n++] = '\\'; wcscpy(out + n, file);
// Theseus skips the separator check on an empty dir instead of reading
// before the buffer.
void MakePath(TCHAR* szBuf, const TCHAR* szDir, const TCHAR* szFile)
{
	int len = (int)_tcslen(szDir);
	if (szBuf != szDir)
		CopyChars(szBuf, szDir, len);
	if (len > 0 && szBuf[len - 1] != '\\')
		szBuf[len++] = '\\';
	_tcscpy(szBuf + len, szFile);
}

// CleanFilePath(out, url)
// Reconstructed from 5960 retail XBE @ 0x0002e010. Drops a leading "file:"
// (any case), turns '/' into '\\', stops at '?', narrows the rest.
template <typename OUTCHAR>
static void CleanPathInto(OUTCHAR* out, const TCHAR* src)
{
	if (_tcsnicmp(src, _T("file:"), 5) == 0)
		src += 5;
	for (; *src != 0 && *src != '?'; src++)
		*out++ = (*src == '/') ? (OUTCHAR)'\\' : (OUTCHAR)*src;
	*out = 0;
}

void CleanFilePath(char* szPath, const TCHAR* szSrcPath)
{
	CleanPathInto(szPath, szSrcPath);
}

#if defined(_UNICODE)
// Theseus also keeps a wide copy for wide callers.
void CleanFilePath(TCHAR* szPath, const TCHAR* szSrcPath)
{
	CleanPathInto(szPath, szSrcPath);
}
#endif

// The last '/' in path, else the last '\\', else NULL.
static TCHAR* LastSeparator(TCHAR* path)
{
	TCHAR* sep = _tcsrchr(path, '/');
	return sep != NULL ? sep : _tcsrchr(path, '\\');
}

// MakeAbsoluteURL2(out, base, url)
// Reconstructed from 5960 retail XBE @ 0x0002e060. url is copied as is when
// it already has a drive (':'), is a UNC path, there's no base, or base has
// neither ':' nor '/'. Otherwise out = base with its last part replaced by
// url, each leading "../" (or "..\") taking one more part off. When no
// separator is left the url lands over the last character. Theseus also
// copies the url when base has no separator at all, where the binary reads
// through a null pointer.
void MakeAbsoluteURL2(TCHAR* szBuf, const TCHAR* szBase, const TCHAR* szURL)
{
	bool absolute = _tcschr(szURL, ':') != NULL || (szURL[0] == '\\' && szURL[1] == '\\');
	if (absolute || szBase == NULL || (_tcschr(szBase, ':') == NULL && _tcschr(szBase, '/') == NULL))
	{
		_tcscpy(szBuf, szURL);
		return;
	}

	_tcscpy(szBuf, szBase);
	TCHAR* cut = LastSeparator(szBuf);
	if (cut == NULL)
	{
		cut = szBuf + _tcslen(szBuf) - 1;
	}
	else
	{
		while (szURL[0] == '.' && szURL[1] == '.' && (szURL[2] == '/' || szURL[2] == '\\'))
		{
			szURL += 3;
			*cut = 0;
			cut = LastSeparator(szBuf);
			if (cut == NULL)
			{
				cut = szBuf + _tcslen(szBuf) - 1;
				break;
			}
		}
	}
	*cut = '/';
	_tcscpy(cut + 1, szURL);
}

// MakeAbsoluteURL(out, url)
// Reconstructed from 5960 retail XBE @ 0x0002e200: the current folder
// resolved against the app folder, then url resolved against that.
void MakeAbsoluteURL(TCHAR* szBuf, const TCHAR* szURL)
{
	TCHAR folder[1024];
	MakeAbsoluteURL2(folder, TheseusGetAppDir(), g_szCurDir);
	MakeAbsoluteURL2(szBuf, folder, szURL);
}

// UpdateCurDirFromFile(url)
// Reconstructed from 5960 retail XBE @ 0x0002e170: resolves url the same
// way and keeps it as the current folder if it came out absolute.
void UpdateCurDirFromFile(const TCHAR* szURL)
{
	TCHAR resolved[1024];
	MakeAbsoluteURL(resolved, szURL);
	if (_tcschr(resolved, ':') != NULL || (resolved[0] == '\\' && resolved[1] == '\\'))
		_tcscpy(g_szCurDir, resolved);
}

// FindFilePath(out, file)
// Reconstructed from 5960 retail XBE @ 0x0002e260: the resolved path if a
// file is there, otherwise the app folder with file appended.
void FindFilePath(TCHAR* szFullPath, const TCHAR* szFile)
{
	MakeAbsoluteURL(szFullPath, szFile);
	if (DoesFileExist(szFullPath))
		return;
	_tcscpy(szFullPath, TheseusGetAppDir());
	_tcscat(szFullPath, szFile);
}

static bool Cancelled(HANDLE cancel)
{
	return cancel != NULL && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
}

// GetDirectoryBlocks(dir, blockSize, recursive, cancel)
// Reconstructed from 5960 retail XBE @ 0x0002e2d0. One block for the folder
// itself, each file rounded up to whole blocks, and with recursive set each
// subfolder not starting with '.' counted the same way. -1 when cancel is
// signaled before or during the walk, 0 when the folder can't be listed.
int GetDirectoryBlocks(const TCHAR* szDirPath, int nBlockSize, bool bRecursive, HANDLE hCancelEvent)
{
	if (Cancelled(hCancelEvent))
		return -1;

	TCHAR path[MAX_PATH];
	MakePath(path, szDirPath, _T("*.*"));
	char pattern[MAX_PATH];
	Ansi(pattern, path, MAX_PATH);

	WIN32_FIND_DATA found;
	HANDLE find = TheseusFindFirstFile(pattern, &found);
	if (find == INVALID_HANDLE_VALUE)
		return 0;

	int blocks = 1;
	bool stopped = false;
	do
	{
		if (Cancelled(hCancelEvent))
		{
			stopped = true;
			break;
		}

		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			blocks += (found.nFileSizeLow + nBlockSize - 1) / nBlockSize;
		}
		else if (bRecursive && found.cFileName[0] != '.')
		{
			TCHAR name[MAX_PATH];
			Unicode(name, found.cFileName, countof(name));
			MakePath(path, szDirPath, name);
			blocks += GetDirectoryBlocks(path, nBlockSize, true, hCancelEvent);
		}
	}
	while (TheseusFindNextFile(find, &found));
	TheseusFindClose(find);

	return stopped ? -1 : blocks;
}

// Not in 5960; the save grid sizes one file with it.
int GetFileBlocks(const TCHAR* szFilePath, int nBlockSize)
{
	HANDLE file = TheseusCreateFile(szFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	int bytes = GetFileSize(file, NULL);
	CloseHandle(file);
	return (bytes + nBlockSize - 1) / nBlockSize;
}

// GetFileSize(path)
// Reconstructed from 5960 retail XBE @ 0x0002e520: opens for reading with
// every share flag and backup semantics, 0 when it can't.
int GetFileSize(const TCHAR* szFilePath)
{
	HANDLE file = TheseusCreateFile(szFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	int size = GetFileSize(file, NULL);
	CloseHandle(file);
	return size;
}
