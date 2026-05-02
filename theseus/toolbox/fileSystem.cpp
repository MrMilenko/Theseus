// fileSystem.cpp: directory listing, file metadata, copy / move / delete
// helpers used by the FTP server and other toolbox consumers.

#include "fileSystem.h"
#include "xboxInternals.h"
#include "stringUtility.h"
#include "driveManager.h"
#include "pointerMap.h"

#include <cstring>
#include <ctime>
#include <io.h>

namespace 
{
	typedef struct FileContainer
	{
		void* file;
	} FileContainer;

	uint32_t mMaxFileContainerID = 0;

	pointerMap<FileContainer*>* mFileContainerMap = new pointerMap<FileContainer*>(true);

	uint32_t addFileContainer(FileContainer* fileContainer)
	{
		uint32_t result = mMaxFileContainerID;
		mFileContainerMap->add(result, fileContainer);
		mMaxFileContainerID++;
		return result;
	}

	FileContainer* getFileContainer(uint32_t fileHandle)
	{
		FileContainer* fileContainer = mFileContainerMap->get(fileHandle);
		return fileContainer;
	}

	void deleteFileContainer(uint32_t fileHandle)
	{
		mFileContainerMap->removeKey(fileHandle);
	}
}

fileSystem::FileInfoDetail* fileSystem::fileGetFileInfoDetail(const char* path)
{
	const DWORD invalidFileAttributes = (DWORD)0xFFFFFFFF;

	DWORD attributes = GetFileAttributesA(path);
	if (attributes == invalidFileAttributes)
	{
		return NULL;
	}

	FileInfoDetail* fileInfoDetail = new FileInfoDetail();
	fileInfoDetail->path = strdup(path);
	fileInfoDetail->isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	fileInfoDetail->isFile = (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	fileInfoDetail->accessTime.month = 1;
	fileInfoDetail->accessTime.day = 1;
	fileInfoDetail->accessTime.year = 2000;
	fileInfoDetail->accessTime.hour = 0;
	fileInfoDetail->accessTime.minute = 0;
	fileInfoDetail->accessTime.second = 0;
	fileInfoDetail->writeTime.month = 1;
	fileInfoDetail->writeTime.day = 1;
	fileInfoDetail->writeTime.year = 2000;
	fileInfoDetail->writeTime.hour = 0;
	fileInfoDetail->writeTime.minute = 0;
	fileInfoDetail->writeTime.second = 0;

	HANDLE fileHandle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, fileInfoDetail->isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
	if (fileHandle != INVALID_HANDLE_VALUE)
    {
		FILETIME fileTimeAccess;
		FILETIME fileTimeWrite;
		if (GetFileTime(fileHandle, NULL, &fileTimeAccess, &fileTimeWrite) == FALSE)
		{
			delete(fileInfoDetail);
			return NULL;
		}

		SYSTEMTIME systemTimeAccessLocal;
		if (FileTimeToSystemTime(&fileTimeAccess, &systemTimeAccessLocal) == FALSE)
		{
			delete(fileInfoDetail);
			return NULL;
		}

		fileInfoDetail->accessTime.month = systemTimeAccessLocal.wMonth;
		fileInfoDetail->accessTime.day = systemTimeAccessLocal.wDay;
		fileInfoDetail->accessTime.year = systemTimeAccessLocal.wYear;
		fileInfoDetail->accessTime.hour = systemTimeAccessLocal.wHour;
		fileInfoDetail->accessTime.minute = systemTimeAccessLocal.wMinute;
		fileInfoDetail->accessTime.second = systemTimeAccessLocal.wSecond;

		if (fileTimeWrite.dwHighDateTime != 0 || fileTimeWrite.dwLowDateTime)
		{
			FILETIME fileTimeWriteLocal;
			if (FileTimeToLocalFileTime(&fileTimeWrite, &fileTimeWriteLocal) == FALSE)
			{
				delete(fileInfoDetail);
				return NULL;
			}

			SYSTEMTIME systemTimeWriteLocal;
			if (FileTimeToSystemTime(&fileTimeWriteLocal, &systemTimeWriteLocal) == FALSE)
			{
				delete(fileInfoDetail);
				return NULL;
			}

			fileInfoDetail->writeTime.month = systemTimeWriteLocal.wMonth;
			fileInfoDetail->writeTime.day = systemTimeWriteLocal.wDay;
			fileInfoDetail->writeTime.year = systemTimeWriteLocal.wYear;
			fileInfoDetail->writeTime.hour = systemTimeWriteLocal.wHour;
			fileInfoDetail->writeTime.minute = systemTimeWriteLocal.wMinute;
			fileInfoDetail->writeTime.second = systemTimeAccessLocal.wSecond;
		}
	}

	DWORD fileSize = GetFileSize(fileHandle, NULL);
	fileInfoDetail->size = fileSize;
		
	CloseHandle(fileHandle);
	return fileInfoDetail;
}

pointerVector<fileSystem::FileInfoDetail*>* fileSystem::fileGetFileInfoDetails(const char* path)
{
	pointerVector<FileInfoDetail*>* fileInfoDetails = new pointerVector<FileInfoDetail*>(true);

	WIN32_FIND_DATAA findData;

	char* searchPath = combinePath(path, "*");
	HANDLE findHandle = FindFirstFileA(searchPath, &findData);
	free(searchPath);
	if (findHandle == INVALID_HANDLE_VALUE) 
	{     
		return fileInfoDetails;
	} 

	do 
	{ 
		if (findData.cFileName == "." || findData.cFileName == "..")
		{
			continue;
		}
		char* currentPath = combinePath(path, findData.cFileName);

		FileInfoDetail* fileInfoDetail = fileGetFileInfoDetail(currentPath);
		free(currentPath);

		if (fileInfoDetail == NULL)
		{
			FindClose(findHandle);
			delete(fileInfoDetails);
			return NULL;
		}
		fileInfoDetails->add(fileInfoDetail);			
	} 
	while (FindNextFile(findHandle, &findData)); 
	FindClose(findHandle);

	for (uint32_t i = 0; i < fileInfoDetails->count() - 1; i++) 
	{
		for (uint32_t j = i + 1; j < fileInfoDetails->count(); j++) 
		{
			FileInfoDetail* fileInfoDetail1 = fileInfoDetails->get(i);
			FileInfoDetail* fileInfoDetail2 = fileInfoDetails->get(j);
			if (stricmp(fileInfoDetail1->path, fileInfoDetail2->path) > 0) 
			{
				fileInfoDetails->swap(i, j);
			}
		}
	}

	return fileInfoDetails;
}

bool fileSystem::fileOpen(const char* path, FileMode const fileMode, uint32_t& fileHandle)
{
	char* access = "";
	if (fileMode == FileModeRead) {
		access = "rb";
	}
	else if (fileMode == FileModeWrite) {
		access = "wb";
	}
	else if (fileMode == FileModeAppend) {
		access = "ab";
	}
	else if (fileMode == FileModeReadUpdate) {
		access = "r+b";
	}
	else if (fileMode == FileModeWriteUpdate) {
		access = "w+b";
	}
	else if (fileMode == FileModeAppendUpdate) {
		access = "a+b";
	}
	FileContainer* fileContainer = new FileContainer();
	fileContainer->file = fopen(path, access);
	if (fileContainer->file == NULL)
	{
		delete(fileContainer);
		return false;
	}
	fileHandle = addFileContainer(fileContainer);
    return true;
}

bool fileSystem::fileRead(uint32_t fileHandle, char* readBuffer, uint32_t const bytesToRead, uint32_t& bytesRead)
{
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		return false;
	}
	bytesRead = (uint32_t)fread(readBuffer, 1, bytesToRead, (FILE*)fileContainer->file);
	return true;
}

bool fileSystem::fileWrite(uint32_t fileHandle, char* writeBuffer, uint32_t bytesToWrite, uint32_t& bytesWritten)
{
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		bytesWritten = 0;
		return false;
	}
	bytesWritten = (uint32_t)fwrite(writeBuffer, 1, bytesToWrite, (FILE*)fileContainer->file);
	return bytesWritten == bytesToWrite;
}

bool fileSystem::fileWrite(const char* path, char* writeBuffer, uint32_t bytesToWrite, uint32_t& bytesWritten)
{
	uint32_t fileHandle;
	if (fileOpen(path, FileModeWrite, fileHandle) == false)
	{
		return false;
	}
	bool result = true;
	if (fileWrite(fileHandle, writeBuffer, bytesToWrite, bytesWritten) == false)
	{
		result = false;
	}
	return fileClose(fileHandle) && result;
}

bool fileSystem::fileClose(uint32_t fileHandle)
{
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		return false;
	}
	bool result = fclose((FILE*)fileContainer->file) == 0;
	deleteFileContainer(fileHandle);
	return result;
}

bool fileSystem::fileSeek(uint32_t fileHandle, FileSeekMode const fileSeekMode, uint32_t const offset)
{
	int seek = SEEK_CUR;
	if (fileSeekMode == FileSeekModeStart)
	{
		seek = SEEK_SET;
	}
	else if (fileSeekMode == FileSeekModeEnd)
	{
		seek = SEEK_END;
	}
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		return false;
	}
	FILE *file = (FILE*)fileContainer->file;
	fseek(file, offset, seek);
	return true;
}

bool fileSystem::filePosition(uint32_t fileHandle, uint32_t& position)
{
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		return false;
	}
	FILE *file = (FILE*)fileContainer->file;
	position = ftell(file);
	return true;
}

bool fileSystem::fileTruncate(uint32_t fileHandle, uint32_t& position)
{
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		return false;
	}
	FILE *file = (FILE*)fileContainer->file;
	return _chsize(_fileno(file), position) == 0;
}

bool fileSystem::setFileTime(const char* path, const FileTime fileTime)
{
	SYSTEMTIME systemTime;
	systemTime.wYear = fileTime.year;
	systemTime.wMonth = fileTime.month;
	systemTime.wDay = fileTime.day;
	systemTime.wDayOfWeek = 0;
	systemTime.wHour = fileTime.hour;
	systemTime.wMinute = fileTime.minute;
	systemTime.wSecond = fileTime.second;
	systemTime.wMilliseconds = 0;

	FILETIME tempFileTime;
	SystemTimeToFileTime(&systemTime, &tempFileTime);

	HANDLE fileHandle = CreateFile(path, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

	::SetFileTime(fileHandle, NULL, NULL, &tempFileTime);
	CloseHandle(fileHandle);
	return true;
}

bool fileSystem::directoryCreate(const char* path)
{	
	bool exists;
	if (directoryExists(path, exists) == false) 
	{
		return false;
	}
	if (exists) 
	{
		return true;
	}
    return CreateDirectoryA(path, NULL) == TRUE;
}

bool fileSystem::directoryDelete(const char* path, bool const recursive)
{
	pointerVector<FileInfoDetail*>* fileInfoDetails = fileGetFileInfoDetails(path);
	if (fileInfoDetails == NULL)
	{
		return false;
	}

	for (uint32_t i = 0; i < fileInfoDetails->count(); i++)
	{
		FileInfoDetail* fileInfoDetail = fileInfoDetails->get(i);
		if (fileInfoDetail->isDirectory) 
		{
			const char* directoryToDelete = fileInfoDetail->path;
			if (recursive && directoryDelete(directoryToDelete, true) == false) 
			{
				return false;
			}
		}
		else
		{
			const char* fileToDelete = fileInfoDetail->path;
			if (fileDelete(fileToDelete) == false)
			{
				return false;
			}
		}
	}

	delete(fileInfoDetails);
    return RemoveDirectoryA(path) == TRUE;
}

bool fileSystem::fileDelete(const char* path)
{
	return remove(path) == 0;
}

bool fileSystem::fileCopy(const char* sourcePath, const char* destPath)
{
	uint32_t sourceFileHandle;
	fileOpen(sourcePath, FileModeRead, sourceFileHandle);
	
	uint32_t destFileHandle;
	fileOpen(destPath, FileModeWrite, destFileHandle);
	
	char *buffer = (char*)malloc(32758);

	uint32_t bytesRead;
	if (fileRead(sourceFileHandle, buffer, 32768, bytesRead) == false)
	{
		fileClose(sourceFileHandle);
		fileClose(destFileHandle);
		return false;
	}
	
	while (bytesRead > 0)
	{
		uint32_t bytesWritten;
		if (fileWrite(destFileHandle, buffer, bytesRead, bytesWritten) == false || bytesWritten != bytesRead)
		{
			fileClose(sourceFileHandle);
			fileClose(destFileHandle);
			return false;
		}
		if (fileRead(sourceFileHandle, buffer, 32768, bytesRead) == false)
		{
			fileClose(sourceFileHandle);
			fileClose(destFileHandle);
			return false;
		}
	}
	
	free(buffer);
	
	fileClose(sourceFileHandle);
	fileClose(destFileHandle);
	return true;
}

bool fileSystem::fileMove(const char* sourcePath, const char* destPath)
{
	BOOL result = MoveFileA(sourcePath, destPath);
    return result == TRUE;
}

bool fileSystem::fileSize(uint32_t fileHandle, uint32_t& size)
{
	FileContainer* fileContainer = getFileContainer(fileHandle);
	if (fileContainer == NULL) {
		return false;
	}
	FILE* file = (FILE*)fileContainer->file;
	uint32_t filePosition = ftell(file);
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, filePosition, SEEK_SET);
    return true;
}

bool fileSystem::fileExists(const char* path, bool& exists)
{
	FileInfoDetail* fileInfoDetail = fileGetFileInfoDetail(path);
	if (fileInfoDetail == NULL) 
	{
		exists = false;
		return true;
	}
	exists = fileInfoDetail->isFile;
	delete(fileInfoDetail);
	return true;
}

bool fileSystem::directoryExists(const char* path, bool& exists)
{
	FileInfoDetail* fileInfoDetail = fileGetFileInfoDetail(path);
	if (fileInfoDetail == NULL) 
	{
		exists = false;
		return true;
	}
	exists = fileInfoDetail->isDirectory;
	delete(fileInfoDetail);
	return true;
}

// Tested
char* fileSystem::getFileName(const char* path)
{
	uint32_t length = 0;
	for (int i = (uint32_t)strlen(path); i > 0; i--)
	{
		if (path[i - 1] == '\\')
		{
			break;
		}
		length++;
	}
	
	char* result = (char*)malloc(length + 1);
	if (result == NULL)
	{
		return NULL;
	}

	strncpy(result, path + strlen(path) - length, length);
	result[length] = 0;
	return result;
}

char* fileSystem::getRootPath(const char* path)
{
	if(!path) {
		return strdup("");
	}

	if(path[0] == '\\') {
		return strdup(path);
	}

	uint32_t length = 0;
	size_t pathLen = strlen(path);

	for (int i = 0; i < pathLen; i++) {
		if (path[i] == ':') {
			break;
		}
		length++;
	}

	char* result = (char*)malloc(pathLen + 1);
	if (result == NULL)
	{
		return NULL;
	}

	strncpy(result, path + length + 1, pathLen - length - 1);
	result[pathLen - length - 1] = 0;
	return result;
}

char* fileSystem::getDriveLetter(const char* path)
{
	uint32_t length = 0;

	if(!path || path[0] == '\\') {
		return strdup("");
	}

	for (int i = 0; i < strlen(path); i++) {
		if (path[i] == ':') {
			break;
		}
		length++;
	}

	char* result = (char*)malloc(length + 1);
	if (result == NULL)
	{
		return NULL;
	}

	strncpy(result, path, length);
	result[length] = 0;
	return result;
}

char* fileSystem::getFileNameWithoutExtension(const char* path)
{
	char* fileName = getFileName(path);
	char* extension = getExtension(fileName);
	char* result = stringUtility::substr(fileName, 0, strlen(fileName) - strlen(extension));
	free(extension);
	free(fileName);
	return result;
}

// Tested
char* fileSystem::getExtension(const char* path)
{
	uint32_t length = 0;
	for (int i = (uint32_t)strlen(path); i > 0; i--)
	{
		if (path[i - 1] == '.')
		{
			length++;
			break;
		}
		if (path[i - 1] == '\\' || path[i - 1] == '.')
		{
			break;
		}
		length++;
	}

	char* result = (char*)malloc(length + 1);
	if (result == NULL)
	{
		return NULL;
	}

	strncpy(result, path + strlen(path) - length, length);
	result[length] = 0;
	return result;
}

// Tested
char* fileSystem::getDirectory(const char* path)
{
	int32_t length = 0;
	for (int i = (uint32_t)strlen(path); i > 0; i--)
	{
		if (path[i - 1] == '\\')
		{
			break;
		}
		length++;
	}

	length = length == 0 ? 0 : strlen(path) - length - 1;
	if (length < 0)
	{
		return strdup("");
	}

	char* result = (char*)malloc(length + 1);
	if (result == NULL)
	{
		return NULL;
	}

	strncpy(result, path, length);
	result[length] = 0;
	return result;
}

// Tested
char* fileSystem::combinePath(const char* first, const char* second)
{
	if (strlen(first) == 0)
	{
		return strdup(second);
	}

	if (strlen(second) == 0)
	{
		return strdup(first);
	}

	char* trimmedFirst = stringUtility::rightTrim(first, '\\');
	char* trimmedSecond = stringUtility::leftTrim(second, '\\');

	uint32_t length = strlen(trimmedFirst) + 1 + strlen(trimmedSecond) + 1;
	char* result = (char*)malloc(length);
	if (result == NULL)
	{
		return NULL;
	}

	strcpy(result, trimmedFirst);
	strcpy(result + strlen(trimmedFirst), "\\");
	strcpy(result + strlen(trimmedFirst) + 1, trimmedSecond);
	result[length - 1] = 0;
	return result;
}