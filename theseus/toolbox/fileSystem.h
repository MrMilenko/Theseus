// fileSystem.h: directory traversal and file metadata API used by the
// FTP server and drive manager. Wraps the Xbox FATX file APIs into a
// POSIX-style listing interface.

#pragma once

#include "xboxinternals.h"
#include "pointerVector.h"

#include <string>

class fileSystem
{
public:

	typedef enum FileMode
	{
		FileModeRead = 0,
		FileModeWrite = 1,
		FileModeAppend = 2,
		FileModeReadUpdate = 3,
		FileModeWriteUpdate = 4,
		FileModeAppendUpdate = 5
	} FileMode;

	typedef enum FileSeekMode
	{
		FileSeekModeStart = 0,
		FileSeekModeEnd = 1,
		FileSeekModeCurrent = 2
	} FileSeekMode;

	typedef struct FileTime
	{
		uint16_t month;
		uint16_t day;
		uint16_t year;
		uint16_t hour;
		uint16_t minute;
		uint16_t second;
	} FileTime;
    
	typedef struct FileInfoDetail
    {
		bool isDirectory;
		bool isFile;
		bool isVirtual;
		char* path;
		uint32_t size;
		FileTime accessTime;
		FileTime writeTime;	

		FileInfoDetail() 
		{
			isDirectory = false;
			isFile = false;
			isVirtual = false;
			path = NULL;
			size = 0;
			memset(&accessTime, 0, sizeof(FileTime));
			memset(&writeTime, 0, sizeof(FileTime));
		}

		~FileInfoDetail()
		{
			delete(path);
		}

    } FileInfoDetail;
	
	static fileSystem::FileInfoDetail* fileGetFileInfoDetail(const char* path);
	static pointerVector<fileSystem::FileInfoDetail*>* fileGetFileInfoDetails(const char* path);
	
	static bool fileOpen(const char* path, FileMode const fileMode, uint32_t& fileHandle);
	static bool fileRead(uint32_t fileHandle, char* readBuffer, uint32_t const bytesToRead, uint32_t& bytesRead);
	static bool fileWrite(uint32_t fileHandle, char* writeBuffer, uint32_t bytesToWrite, uint32_t& bytesWritten);
	static bool fileWrite(const char* path, char* writeBuffer, uint32_t bytesToWrite, uint32_t& bytesWritten);
	static bool fileClose(uint32_t fileHandle);
	static bool fileSeek(uint32_t fileHandle, FileSeekMode const fileSeekMode, uint32_t const offset);
	static bool filePosition(uint32_t fileHandle, uint32_t& position);
	static bool fileTruncate(uint32_t fileHandle, uint32_t& position);
	static bool setFileTime(const char* path, const FileTime fileTime);

	static bool directoryCreate(const char* path);
	static bool directoryDelete(const char* path, bool const recursive);
	static bool fileDelete(const char* path);
	static bool fileCopy(const char* sourcePath, const char* destPath);
	static bool fileMove(const char* sourcePath, const char* destPath);
	static bool fileSize(uint32_t fileHandle, uint32_t& size);
	static bool fileExists(const char* path, bool& exists);
	static bool directoryExists(const char* path, bool& exists);

	static char* getDriveLetter(const char* path);
	static char* getRootPath(const char* path);
	static char* getFileName(const char* path);
	static char* getFileNameWithoutExtension(const char* path);
	static char* getExtension(const char* path);
	static char* getDirectory(const char* path);
	static char* combinePath(const char* first, const char* second);
};

