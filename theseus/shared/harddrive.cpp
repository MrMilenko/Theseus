// harddrive.cpp: hard drive folder / partition helpers. Inherited from
// the public XDK Installer sample; provides the FATX folder-create /
// folder-exists checks the dashboard uses for drive setup and recovery.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

class CHardDrive : public CNode
{
	DECLARE_NODE(CHardDrive, CNode)
public:
	CHardDrive();
	~CHardDrive();

	_int64 m_BytesToCopy;
	_int64 m_BytesCopied;
	int m_FilesToCopy;
	int m_FilesCopied;

	int GetFreeSpace(const TCHAR *szDrive);
	int GetTotalSpace(const TCHAR *szDrive);
	int ConvertMBToGB(int mbValue);

	int FileExists(const TCHAR *FileName);
	int GetThisFileSize(const TCHAR *szFilePath);
	void MoveThisFile(const TCHAR *szSrcPath, const TCHAR *szDestPath);
	void MoveThisDirectory(const TCHAR *szSrcPath, const TCHAR *szDestPath);
	int DeleteThisFile(const TCHAR *szPath);
	int DeleteThisDirectory(const TCHAR *szPath);
	void CopyThisFile(const TCHAR *szSrcPath, const TCHAR *szDestPath);
	void CopyThisDirectory(const TCHAR *szSrcPath, const TCHAR *szDestPath);
	void RenameThisFile(const TCHAR *szSrcPath, const TCHAR *szDestPath);
	void RenameThisDirectory(const TCHAR *szSrcPath, const TCHAR *szDestPath);
	int CreateThisDirectory(const TCHAR *szDir);
	int RemoveThisDirectory(const TCHAR *szDir);
	int CopyGame(const TCHAR *szDestPath);
	void MakePath(TCHAR *szBuf, const TCHAR *szDir, const TCHAR *szFile);

	int GetFilesToCopy() { return (int)m_FilesToCopy; }
	int GetFilesCopied() { return (int)m_FilesCopied; }
	int GetBytesToCopy() { return (int)m_BytesToCopy; }
	int GetBytesCopied() { return (int)m_BytesCopied; }

protected:
	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("HardDrive", CHardDrive, CNode)

#define _FND_CLASS CHardDrive
START_NODE_FUN(CHardDrive, CNode)
NODE_FUN_IS(GetFreeSpace)
NODE_FUN_IS(GetTotalSpace)
NODE_FUN_II(ConvertMBToGB)
NODE_FUN_IS(FileExists)
NODE_FUN_IS(GetThisFileSize)
NODE_FUN_VSS(MoveThisFile)
NODE_FUN_VSS(MoveThisDirectory)
NODE_FUN_IS(DeleteThisFile)
NODE_FUN_IS(DeleteThisDirectory)
NODE_FUN_VSS(CopyThisFile)
NODE_FUN_VSS(CopyThisDirectory)
NODE_FUN_VSS(RenameThisFile)
NODE_FUN_VSS(RenameThisDirectory)
NODE_FUN_IS(CreateThisDirectory)
NODE_FUN_IS(RemoveThisDirectory)
NODE_FUN_IS(CopyGame)
NODE_FUN_IV(GetBytesToCopy)
NODE_FUN_IV(GetBytesCopied)
END_NODE_FUN()
#undef _FND_CLASS

CHardDrive::CHardDrive()
{
}

CHardDrive::~CHardDrive()
{
}

int CHardDrive::GetFreeSpace(const TCHAR *szDrive)
{
	ULARGE_INTEGER lFreeBytesAvailable;
	ULARGE_INTEGER lTotalNumberOfBytes;
	ULARGE_INTEGER lTotalNumberOfFreeBytes;

	char FreeSpace[MAX_PATH];
	Ansi(FreeSpace, szDrive, countof(FreeSpace));

	if (GetDiskFreeSpaceEx(FreeSpace,
						   &lFreeBytesAvailable,
						   &lTotalNumberOfBytes,
						   &lTotalNumberOfFreeBytes) == 0)
	{
		return 0;
	}

	DWORD space = (DWORD)(lTotalNumberOfFreeBytes.QuadPart / 1024) / 1024;
	return (int)space;
}

int CHardDrive::GetTotalSpace(const TCHAR *szDrive)
{
	ULARGE_INTEGER lFreeBytesAvailable;
	ULARGE_INTEGER lTotalNumberOfBytes;
	ULARGE_INTEGER lTotalNumberOfFreeBytes;

	char FreeSpace[MAX_PATH];
	Ansi(FreeSpace, szDrive, countof(FreeSpace));

	if (GetDiskFreeSpaceEx(FreeSpace,
						   &lFreeBytesAvailable,
						   &lTotalNumberOfBytes,
						   &lTotalNumberOfFreeBytes) == 0)
	{
		return 0;
	}

	DWORD space = (DWORD)(lTotalNumberOfBytes.QuadPart / 1024) / 1024;
	return (int)space;
}

int CHardDrive::ConvertMBToGB(int mbValue)
{
	int tempInt = mbValue / 1024;
	return tempInt;
}

int CHardDrive::FileExists(const TCHAR *FileName)
{
	bool tempBool = false;
	CHAR szFile[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA fad;

	Ansi(szFile, FileName, MAX_PATH);
	int hr = GetFileAttributesEx(szFile, GetFileExInfoStandard, &fad);

	if (hr > 0)
	{
		tempBool = true;
	}
	return tempBool;
}

void CHardDrive::MoveThisFile(const TCHAR *szSrcPath, const TCHAR *szDestPath)
{
	char OriginalFile[MAX_PATH];
	char NewFile[MAX_PATH];
	Ansi(OriginalFile, szSrcPath, countof(OriginalFile));
	Ansi(NewFile, szDestPath, countof(NewFile));
	MoveFileEx(OriginalFile, NewFile, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
}

void CHardDrive::MoveThisDirectory(const TCHAR *szSrcPath, const TCHAR *szDestPath)
{
	CopyThisDirectory(szSrcPath, szDestPath);
	DeleteThisDirectory(szSrcPath);
}

int CHardDrive::DeleteThisFile(const TCHAR *FileName)
{
	bool tempBool = false;
	char theFile[MAX_PATH];
	Ansi(theFile, FileName, countof(theFile));
	int hr = DeleteFile(theFile);

	if (hr > 0)
	{
		tempBool = true;
	}
	return tempBool;
}

int CHardDrive::DeleteThisDirectory(const TCHAR *szPath)
{
	bool tempBool = false;
	char szBuf[MAX_PATH];
	char conversionString[MAX_PATH];
	Ansi(conversionString, szPath, countof(conversionString));
	sprintf(szBuf, "%s\\*.*", conversionString);

	WIN32_FIND_DATA fd;
	HANDLE h = FindFirstFile(szBuf, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
				continue;

			sprintf(szBuf, "%s\\%s", conversionString, fd.cFileName);

			TCHAR szFileName[MAX_PATH];
			Unicode(szFileName, szBuf, countof(szFileName));

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				if (!DeleteThisDirectory(szFileName))
					return false;
			}
			else
			{
				if (!DeleteFile(szBuf))
					return false;
			}
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
	int hr = RemoveDirectory(conversionString);

	if (hr > 0)
	{
		tempBool = true;
	}
	return tempBool;
}

void CHardDrive::CopyThisFile(const TCHAR *szSrcPath, const TCHAR *szDestPath)
{
	char source[MAX_PATH];
	char destination[MAX_PATH];
	Ansi(source, szSrcPath, countof(source));
	Ansi(destination, szDestPath, countof(destination));
	CopyFile(source, destination, false);
}

void CHardDrive::CopyThisDirectory(const TCHAR *szSrcPath, const TCHAR *szDestPath)
{
	char theSource[MAX_PATH];
	Ansi(theSource, szSrcPath, countof(theSource));

	char theDestination[MAX_PATH];
	Ansi(theDestination, szDestPath, countof(theDestination));

	CreateDirectory(theDestination, NULL);

	char szBuf[MAX_PATH];
	sprintf(szBuf, "%s\\*.*", theSource);

	WIN32_FIND_DATA fd;
	HANDLE h = FindFirstFile(szBuf, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
				continue;

			sprintf(szBuf, "%s\\%s", theSource, fd.cFileName);

			char szBuf2[MAX_PATH];
			sprintf(szBuf2, "%s\\%s", theDestination, fd.cFileName);

			TCHAR finalSource[MAX_PATH];
			Unicode(finalSource, szBuf, countof(finalSource));

			TCHAR finalDestination[MAX_PATH];
			Unicode(finalDestination, szBuf2, countof(finalDestination));

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				CopyThisDirectory(finalSource, finalDestination);
			}
			else
			{
				CopyFile(szBuf, szBuf2, FALSE);
			}
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
}

int CHardDrive::CreateThisDirectory(const TCHAR *szDir)
{
	bool tempBool = false;
	char theFile[MAX_PATH];
	Ansi(theFile, szDir, countof(theFile));
	int hr = CreateDirectory(theFile, NULL);

	if (hr > 0)
	{
		tempBool = true;
	}
	return tempBool;
}

int CHardDrive::RemoveThisDirectory(const TCHAR *szDir)
{
	bool tempBool = false;
	char theFile[MAX_PATH];
	Ansi(theFile, szDir, countof(theFile));
	int hr = RemoveDirectory(theFile);

	if (hr > 0)
	{
		tempBool = true;
	}
	return tempBool;
}

void CHardDrive::RenameThisFile(const TCHAR *szSrcPath, const TCHAR *szDestPath)
{
	_wrename(szSrcPath, szDestPath);
}

void CHardDrive::RenameThisDirectory(const TCHAR *szSrcPath, const TCHAR *szDestPath)
{
	_wrename(szSrcPath, szDestPath);
}

void CHardDrive::MakePath(TCHAR *szBuf, const TCHAR *szDir, const TCHAR *szFile)
{
	int cch = _tcslen(szDir);

	if (szBuf != szDir)
		CopyChars(szBuf, szDir, cch);

	ASSERT(cch > 0);
	if (szBuf[cch - 1] != '\\')
	{
		szBuf[cch] = '\\';
		cch += 1;
	}

	_tcscpy(szBuf + cch, szFile);
}

int CHardDrive::GetThisFileSize(const TCHAR *szFilePath)
{
	HANDLE hFile = TheseusCreateFile(szFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return 0;

	int nFileSize = GetFileSize(hFile, NULL);
	int t_FileSize = (nFileSize / 1024) / 1024;

	CloseHandle(hFile);

	return t_FileSize;
}

int CHardDrive::CopyGame(const TCHAR *szDestPath)
{
	int success = 0;
	char theDestination[MAX_PATH];
	Ansi(theDestination, szDestPath, countof(theDestination));

	CreateDirectory(theDestination, NULL);
	char szBuf[MAX_PATH];

	WIN32_FIND_DATA fd;
	HANDLE h = FindFirstFile("D:\\*.*", &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
				continue;

			sprintf(szBuf, "D:\\%s", fd.cFileName);

			char szBuf2[MAX_PATH];
			sprintf(szBuf2, "%s\\%s", theDestination, fd.cFileName);

			TCHAR finalSource[MAX_PATH];
			Unicode(finalSource, szBuf, countof(finalSource));

			TCHAR finalDestination[MAX_PATH];
			Unicode(finalDestination, szBuf2, countof(finalDestination));

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				CopyThisDirectory(finalSource, finalDestination);
			}
			else
			{
				CopyFile(szBuf, szBuf2, FALSE);
			}
		} while (FindNextFile(h, &fd));
		FindClose(h);
		success = 1;
	}

	return success;
}
