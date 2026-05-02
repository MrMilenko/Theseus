// ftpServer.cpp: FTP command processor and connection state machine.

#include "ftpServer.h"
#include "socketUtility.h"
#include "fileSystem.h"
#include "driveManager.h"
#include <algorithm>
#include <string>
#include "stringUtility.h"
#include "utils.h"

#include <xtl.h>

#define SERVERID "PrometheOS FTP, welcome..."

namespace {

	LONG activeConnections = 0;
	volatile LONG totalBytesReceived = 0;
	volatile LONG totalBytesSent = 0;

	int mMaxConnections;
	int mCommandTimeout;
	int mConnectTimeout;

	bool mStopRequested;
	bool mServerRunning;

	uint64_t sListen;
	sockaddr_in saiListen;

	HANDLE mListenThreadHandle;

	int incrementConnections()
	{
		return (int)_InterlockedIncrement(&activeConnections);
	}

	void decrementConnections()
	{
		_InterlockedDecrement(&activeConnections);
	}

	char* cleanVirtualPath(const char* virtualPath)
	{
		const char* in = virtualPath;
		char* buf = new char[strlen(in) + 4];
		buf[0] = '\0';
		buf[1] = '\0';
		buf[2] = '\0';
		char* out = buf + 3;
		do {
			*out = *in;
			if (*out == '\\') *out = '/'; // convert backslashes to forward slashes
			if ((*out == '\0') || (*out == '/')) {
				if (out[-1] == '.') { // output ends with "."
					if (out[-2] == '\0') --out; // entire output is "."
					else if (out[-2] == '/') { // output ends with "/."
						if (out[-3] == '\0') --out; // entire output is "/."
						else out -= 2;
					}
					else if (out[-2] == '.') { // output ends with ".."
						if (out[-3] == '\0') out -= 2; // entire output is ".."
						else if (out[-3] == '/') { // output ends with "/.."
							if (out[-4] == '\0') out -= 2; // entire output is "/.."
							else {
								out -= 3;
								while ((out[-1] != '\0') && (out[-1] != '/')) --out;
							}
						}
					}
					else ++in;
				}
				else {
					++in;
					if (out[-1] != '/') ++out;
				}
			}
			else ++in, ++out;
		} while (in[-1] != '\0');

		char* result = strdup(buf + 3);
		delete[] buf;
		return result;
	}

	char* resolveRelative(const char* currentVirtual, const char* relativeVirtual)
	{
		if (relativeVirtual[0] != '/') {
			char* newVirtual = stringUtility::formatString("%s\\%s", currentVirtual, relativeVirtual);
			char *result = cleanVirtualPath(newVirtual);
			free(newVirtual);
			return result;
		}
		else {
			char *result = cleanVirtualPath(relativeVirtual);
			return result;
		}
	}

	pointerVector<fileSystem::FileInfoDetail*>* getDirectoryListing(const char* virtualPath)
	{
		if (stringUtility::equals(virtualPath, "/", false) == true)
		{
			pointerVector<fileSystem::FileInfoDetail*>* fileInfoDetails = new pointerVector<fileSystem::FileInfoDetail*>(true);
			pointerVector<char*>* drives = driveManager::getMountedDrives();

			for (size_t i = 0; i < drives->count(); i++)
			{
				fileSystem::FileInfoDetail* fileInfoDetail = new fileSystem::FileInfoDetail();
				char* mountPoint = stringUtility::formatString("%s:", drives->get(i));
				char* mountAlias = driveManager::convertMountPointToMountPointAlias(mountPoint);
				mountAlias[strlen(mountAlias)-1] = '\0';

				fileInfoDetail->isDirectory = true;
				fileInfoDetail->isFile = false;
				fileInfoDetail->isVirtual = true;
				fileInfoDetail->path = mountAlias;
				fileInfoDetail->accessTime.day = 1;
				fileInfoDetail->accessTime.month = 1;
				fileInfoDetail->accessTime.year = 2000;
				fileInfoDetail->accessTime.minute = 0;
				fileInfoDetail->accessTime.hour = 0;
				fileInfoDetail->accessTime.second = 0;
				fileInfoDetail->writeTime.day = 1;
				fileInfoDetail->writeTime.month = 1;
				fileInfoDetail->writeTime.year = 2000;
				fileInfoDetail->writeTime.minute = 0;
				fileInfoDetail->writeTime.hour = 0;
				fileInfoDetail->writeTime.second = 0;
				fileInfoDetails->add(fileInfoDetail);

				free(mountPoint);
			}

			delete(drives);
			return fileInfoDetails;
		}

		if (driveManager::ftpPathMounted(virtualPath) == false)
		{
			return NULL;
		}

		char* ftpPath = driveManager::mapFtpPath(virtualPath);

		pointerVector<fileSystem::FileInfoDetail*>* fileInfoDetails = fileSystem::fileGetFileInfoDetails(ftpPath);;
		free(ftpPath);

		if (fileInfoDetails == NULL)
		{
			return NULL;
		}
		for (size_t i = 0; i < fileInfoDetails->count(); i++)
		{
			fileSystem::FileInfoDetail* fileInfoDetail = fileInfoDetails->get(i);
			fileInfoDetail->path =  fileSystem::getFileName(fileInfoDetail->path);
		}

		return fileInfoDetails;
	}
}

bool WINAPI ftpServer::connectionThread(uint64_t sCmd)
{
	uint64_t sData = 0;
	uint64_t sPasv = 0;
	char szPeerName[64];
	char szOutput[1024];
	char szCmd[512];
	char* pszParam;
	char* user = NULL;
	char* currentVirtual = NULL;
	char* rnfr = NULL;
	uint32_t dw = 0;
	uint32_t dwRestOffset = 0;
	ReceiveStatus status = ReceiveStatus_OK;
	bool isLoggedIn = false;
	fileSystem::FileTime fileTime;
	UINT_PTR i;

	SOCKADDR_IN saiCmd;
	memset(&saiCmd, 0, sizeof(SOCKADDR_IN));
	SOCKADDR_IN saiCmdPeer;
	memset(&saiCmdPeer, 0, sizeof(SOCKADDR_IN));
	SOCKADDR_IN saiPasv;
	memset(&saiPasv, 0, sizeof(SOCKADDR_IN));
	SOCKADDR_IN saiData;
	memset(&saiData, 0, sizeof(SOCKADDR_IN));

	// Get peer address
	dw=sizeof(SOCKADDR_IN);
	getpeername((SOCKET)sCmd, (SOCKADDR *)&saiCmdPeer, (int *)&dw);
	sprintf(szPeerName, "%u.%u.%u.%u",
		saiCmdPeer.sin_addr.S_un.S_un_b.s_b1,
		saiCmdPeer.sin_addr.S_un.S_un_b.s_b2, 
		saiCmdPeer.sin_addr.S_un.S_un_b.s_b3, 
		saiCmdPeer.sin_addr.S_un.S_un_b.s_b4);

	// Send greeting
	sprintf(szOutput, "220-%s\r\n220-You are connecting from %s:%u.\r\n220 Proceed with login.\r\n", SERVERID, szPeerName, ntohs(saiCmdPeer.sin_port));
	socketSendString(sCmd, szOutput);

	socketUtility::getSocketName(sCmd, &saiCmd);

	// Command processing loop
	while (mStopRequested == false) {

		const int result = socketUtility::getReadStatus(sCmd);
		if (result != 1)
		{
			Sleep(50);
			continue;
		}

		status = socketReceiveString(sCmd, szCmd, 512, &dw);

		if (status == ReceiveStatus_Network_Error) {
			socketSendString(sCmd, "421 Network error.\r\n");
			break;
		} else if (status == ReceiveStatus_Timeout) {
			socketSendString(sCmd, "421 Connection timed out.\r\n");
			break;
		} else if (status == ReceiveStatus_Invalid_Data) {
			socketSendString(sCmd, "500 Malformed request.\r\n");
			continue;
		} else if (status == ReceiveStatus_Insufficient_Buffer) {
			socketSendString(sCmd, "500 Command line too long.\r\n");
			continue;
		}

		if (pszParam = strchr(szCmd, ' ')) {
			*(pszParam++) = 0;
		} else {
			pszParam = szCmd + strlen(szCmd);
		}

		if (!stricmp(szCmd, "USER")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
				continue;
			} else if (isLoggedIn) {
				socketSendString(sCmd, "503 Already logged in. Use REIN to change users.\r\n");
				continue;
			} else {
				free(user);
				user = strdup(pszParam);
				if (stringUtility::equals(user, "xbox", false) == true)
				{
					sprintf(szOutput, "331 Need password for user \"%s\".\r\n", user);
					socketSendString(sCmd, szOutput);
					continue;
				}
			}
		}

		if (!stricmp(szCmd, "PASS")) {
			if (user == NULL) {
				socketSendString(sCmd, "503 Bad sequence of commands. Send USER first.\r\n");
			} else if (isLoggedIn) {
				socketSendString(sCmd, "503 Already logged in. Use REIN to change users.\r\n");
			} else {
				if (stringUtility::equals(user, "xbox", false) && stringUtility::equals(pszParam, "xbox", false)) {
					if (incrementConnections() <= mMaxConnections) {
						isLoggedIn = true;
						free(currentVirtual);
						currentVirtual = strdup("/");
						sprintf(szOutput, "230 User \"%s\" logged in.\r\n", user);
						socketSendString(sCmd, szOutput);
					} else {
						decrementConnections();
						socketSendString(sCmd, "421 Your login was refused due to a server connection limit.\r\n");
						break;
					}
				} else {
					socketSendString(sCmd, "530 Incorrect password.\r\n");
				}
			}
		}

		else if (!stricmp(szCmd, "REIN")) {
			if (isLoggedIn) {
				isLoggedIn = false;
				decrementConnections();
				sprintf(szOutput, "220-User \"%s\" logged out.\r\n", user);
				socketSendString(sCmd, szOutput);
				free(user);
				user = NULL;
			}
			socketSendString(sCmd, "220 REIN command successful.\r\n");
		}

		else if (!stricmp(szCmd, "HELP")) {
			socketSendString(sCmd, "214 For help, please cry to mom.\r\n");
		}

		else if (!stricmp(szCmd, "FEAT")) {
			socketSendString(sCmd, "211-Extensions supported:\r\n SIZE\r\n REST STREAM\r\n MDTM\r\n TVFS\r\n XCRC\r\n211 END\r\n");
		}

		//else if (!stricmp(szCmd, "SYST")) {
		//	sprintf(szOutput, "215 Windows_NT\r\n");
		//	socketSendString(sCmd, szOutput);
		//}

		else if (!stricmp(szCmd, "QUIT")) {
			if (isLoggedIn) {
				isLoggedIn = false;
				decrementConnections();
				sprintf(szOutput, "221-User \"%s\" logged out.\r\n", user);
				socketSendString(sCmd, szOutput);
			}
			socketSendString(sCmd, "221 Goodbye!\r\n");
			break;
		}

		else if (!stricmp(szCmd, "NOOP")) {
			socketSendString(sCmd, "200 NOOP command successful.\r\n");
		}

		else if (!stricmp(szCmd, "PWD") || !stricmp(szCmd, "XPWD")) {
			if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				sprintf(szOutput, "257 \"%s\" is current directory.\r\n", currentVirtual);
				socketSendString(sCmd, szOutput);
			}
		}

		else if (!stricmp(szCmd, "CWD") || !stricmp(szCmd, "XCWD")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				bool isFolder = false;
				if (stringUtility::equals(newVirtual, "/", false))
				{
					isFolder = true;
				}
				else
				{
					char* ftpPath = driveManager::mapFtpPath(newVirtual);
					if (stringUtility::endsWith(ftpPath, ":", false))
					{
						isFolder = true;
					}
					else
					{
						fileSystem::directoryExists(ftpPath, isFolder);
					}
					free(ftpPath);
				}

				if (isFolder == true) 
				{
					free(currentVirtual);
					currentVirtual = strdup(newVirtual);
					sprintf(szOutput, "250 \"%s\" is now current directory.\r\n", currentVirtual);
					socketSendString(sCmd, szOutput);
				} 
				else 
				{
					sprintf(szOutput, "550 %s failed. \"%s\": directory not found.\r\n", szCmd, newVirtual);
					socketSendString(sCmd, szOutput);
				}

				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "CDUP") || !stricmp(szCmd, "XCUP")) {
			if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {

				char* tempCurrentVirtual = resolveRelative(currentVirtual, "..");
				free(currentVirtual);
				currentVirtual = tempCurrentVirtual;

				sprintf(szOutput, "250 \"%s\" is now current directory.\r\n", currentVirtual);
				socketSendString(sCmd, szOutput);
			}
		}

		else if (!stricmp(szCmd, "TYPE")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				socketSendString(sCmd, "200 TYPE command successful.\r\n");
			}
		}

		else if (!stricmp(szCmd, "REST")) {
			if (!*pszParam || (!(dw = stringUtility::toInt(pszParam)) && (*pszParam != '0'))) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				dwRestOffset = dw;
				sprintf(szOutput, "350 Ready to resume transfer at %u bytes.\r\n", dwRestOffset);
				socketSendString(sCmd, szOutput);
			}
		}

		else if (!stricmp(szCmd, "PORT")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				memset(&saiData, 0, sizeof(SOCKADDR_IN));
				saiData.sin_family = AF_INET;

				unsigned short h1 = 0;
				unsigned short h2 = 0;
				unsigned short h3 = 0;
				unsigned short h4 = 0;
				unsigned short p1 = 0;
				unsigned short p2 = 0;

				dw = sscanf(pszParam, "%hu,%hu,%hu,%hu,%hu,%hu", &h1, &h2, &h3, &h4, &p1, &p2);
				
				saiData.sin_addr.S_un.S_un_b.s_b1 = h1;
				saiData.sin_addr.S_un.S_un_b.s_b2 = h2;
				saiData.sin_addr.S_un.S_un_b.s_b3 = h3;
				saiData.sin_addr.S_un.S_un_b.s_b4 = h4;
				saiData.sin_port = (p2 << 8) + p1;

				if (dw == 6) {
					socketUtility::closeSocket(sPasv);
					socketSendString(sCmd, "200 PORT command successful.\r\n");
				} else {
					socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
					memset(&saiData, 0, sizeof(SOCKADDR_IN));
				}
			}
		}

		else if (!stricmp(szCmd, "PASV")) {
			if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {

				if (sPasv) {
					socketUtility::closeSocket(sPasv);
				}

				static u_short portTest = 6000;

				XNADDR addr;
				XNetGetTitleXnAddr(&addr);

				bool found = false;
				while (!found) {		
					memset(&saiPasv, 0, sizeof(SOCKADDR_IN));
					saiPasv.sin_family = AF_INET;
					saiPasv.sin_addr.s_addr = INADDR_ANY;
					saiPasv.sin_port = htons(portTest);		
					if (socketUtility::createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, sPasv))
					{
						found = true;
					}
					else
					{
						utils::debugPrint("Error: Could not create socket on port %u, trying to find next available.\n", portTest);
					}
					portTest++;
					if (portTest > 6999)
					{
						portTest = 6000;
					}
				}

				socketUtility::bindSocket(sPasv, &saiPasv);
				socketUtility::listenSocket(sPasv, 1);
				socketUtility::getSocketName(sPasv, &saiPasv);

				sprintf(szOutput, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n", 
					addr.ina.S_un.S_un_b.s_b1,
					addr.ina.S_un.S_un_b.s_b2,
					addr.ina.S_un.S_un_b.s_b3,
					addr.ina.S_un.S_un_b.s_b4,	
					((unsigned char *)&saiPasv.sin_port)[0], 
					((unsigned char *)&saiPasv.sin_port)[1]);
				socketSendString(sCmd, szOutput);
			}
		}

		else if (!stricmp(szCmd, "LIST") || !stricmp(szCmd, "NLST")) 
		{
			if (!isLoggedIn) 
			{
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} 
			else 
			{
				if (*pszParam == '-') 
				{
					if (pszParam = strchr(pszParam, ' ')) 
					{
						pszParam++;
					}
				}
				char* newVirtual = pszParam && *pszParam ? resolveRelative(currentVirtual, pszParam) : strdup(currentVirtual);
				
				pointerVector<fileSystem::FileInfoDetail*>* fileInfoDetails = getDirectoryListing(newVirtual);
				if (fileInfoDetails != NULL)
				{
					sData = establishDataConnection(&saiData, &sPasv);
					if (sData) 
                    {
						sprintf(szOutput, "150 Opened %s mode data connection for \"%s\".\r\n", sPasv ? "passive" : "active", newVirtual);
						socketSendString(sCmd, szOutput);
						const char * months[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
						for (size_t i = 0; i < fileInfoDetails->count(); i++)
						{
							char szLine[512];
							fileSystem::FileInfoDetail* fileInfoDetail = fileInfoDetails->get(i);
							if (stricmp(szCmd, "NLST") == 0)
							{
								strcpy(szLine, fileInfoDetail->path);
							}
							else
							{
								sprintf(szLine, "%c--------- 1 ftp ftp %llu ", fileInfoDetail->isDirectory ? 'd' : '-', fileInfoDetail->size);
								strcat(szLine, months[fileInfoDetail->writeTime.month - 1]);
								sprintf(szLine + strlen(szLine), " %2i %.2i:%.2i ", fileInfoDetail->writeTime.day, fileInfoDetail->writeTime.hour, fileInfoDetail->writeTime.minute);
								strcat(szLine, fileInfoDetail->path);
							}
							//if (fileInfoDetail->fileInfoDetail->isDirectory == true)
							//{
							//	strcat(szLine, "/");
							//}
							strcat(szLine, "\r\n");
							socketSendString(sData, szLine);
						}
						socketUtility::closeSocket(sData);
						sprintf(szOutput, "226 %s command successful.\r\n", stricmp(szCmd, "NLST") ? "LIST" : "NLST");
						socketSendString(sCmd, szOutput);
					}
					else
					{
						socketSendString(sCmd, "425 Can't open data connection.\r\n");
					}
				}
				else 
				{
					sprintf(szOutput, "550 \"%s\": Path not found.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}
				free(newVirtual);
				delete(fileInfoDetails);
			}
		}

		else if (!stricmp(szCmd, "STAT")) {
			if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				if (*pszParam == '-') 
				{
					if (pszParam = strchr(pszParam, ' ')) 
					{
						pszParam++;
					}
				}

				char* newVirtual = pszParam && *pszParam ? resolveRelative(currentVirtual, pszParam) : strdup(currentVirtual);

				pointerVector<fileSystem::FileInfoDetail*>* fileInfoDetails = getDirectoryListing(newVirtual);
				if (fileInfoDetails != NULL)
				{
					sprintf(szOutput, "212-Sending directory listing of \"%s\".\r\n", newVirtual);
					socketSendString(sCmd,szOutput);
					const char * months[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
					for (size_t i = 0; i < fileInfoDetails->count(); i++)
					{
						char szLine[512];
						fileSystem::FileInfoDetail* fileInfoDetail = fileInfoDetails->get(i);
						sprintf(szLine, "%c--------- 1 ftp ftp %llu ", fileInfoDetail->isDirectory ? 'd' : '-', fileInfoDetail->size);
						strcat(szLine, months[fileInfoDetail->writeTime.month - 1]);
						sprintf(szLine + strlen(szLine), " %2i %.2i:%.2i ", fileInfoDetail->writeTime.day, fileInfoDetail->writeTime.hour, fileInfoDetail->writeTime.minute);
						strcat(szLine, fileInfoDetail->path);
						if (fileInfoDetail->isDirectory == true)
						{
							strcat(szLine, "/");
						}
						strcat(szLine, "\r\n");
						socketSendString(sCmd, szLine);
					}
				}
				else
				{
					sprintf(szOutput, "550 \"%s\": Path not found.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}

				free(newVirtual);
				delete(fileInfoDetails);
			}
		}

		else if (!stricmp(szCmd, "RETR")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {

				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);

				uint32_t fileHandle;
				if (fileSystem::fileOpen(ftpPath, fileSystem::FileModeRead, fileHandle) == true)
				{
					if (dwRestOffset) 
					{
						fileSystem::fileSeek(fileHandle, fileSystem::FileSeekModeStart, dwRestOffset);
						dwRestOffset = 0;
					}
					sData = establishDataConnection(&saiData, &sPasv);
					if (sData) 
					{
						sprintf(szOutput, "150 Opened %s mode data connection for \"%s\".\r\n", sPasv ? "passive" : "active", newVirtual);
						socketSendString(sCmd, szOutput);
						if (sendSocketFile(sCmd, sData, fileHandle, &dw)) 
						{
							sprintf(szOutput, "226 \"%s\" transferred successfully.\r\n", newVirtual);
							socketSendString(sCmd, szOutput);
						} 
						else 
						{
							socketSendString(sCmd, "426 Connection closed; transfer aborted.\r\n");
							if (dw) 
							{
								socketSendString(sCmd, "226 ABOR command successful.\r\n");
							}
						}
						socketUtility::closeSocket(sData);
					} 
					
					else 
					{
						socketSendString(sCmd, "425 Can't open data connection.\r\n");
					}
					fileSystem::fileClose(fileHandle);
				}
				else
				{
					sprintf(szOutput, "550 \"%s\": Unable to open file.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}

				free(ftpPath);
				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "STOR") || !stricmp(szCmd, "APPE")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char *newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);

				uint32_t fileHandle;
				if (fileSystem::fileOpen(ftpPath, fileSystem::FileModeWriteUpdate, fileHandle) == true)
				{
					if (stricmp(szCmd, "APPE") == 0) 
					{
						fileSystem::fileSeek(fileHandle, fileSystem::FileSeekModeEnd, 0);
					} 
					else 
					{
						fileSystem::fileSeek(fileHandle, fileSystem::FileSeekModeStart, dwRestOffset);
						fileSystem::fileTruncate(fileHandle, dwRestOffset);
					}
					dwRestOffset = 0;
					sData = establishDataConnection(&saiData, &sPasv);
					if (sData) 
                    {
						sprintf(szOutput, "150 Opened %s mode data connection for \"%s\".\r\n", sPasv ? "passive" : "active", newVirtual);
						socketSendString(sCmd, szOutput);
						if (receiveSocketFile(sCmd, sData, fileHandle)) 
						{
							sprintf(szOutput, "226 \"%s\" transferred successfully.\r\n", newVirtual);
							socketSendString(sCmd, szOutput);
						} 
						else 
						{
							socketSendString(sCmd, "426 Connection closed; transfer aborted.\r\n");
						}
						socketUtility::closeSocket(sData);
					} else {
						socketSendString(sCmd, "425 Can't open data connection.\r\n");
					}
					fileSystem::fileClose(fileHandle);
				}
				else
				{
					sprintf(szOutput, "550 \"%s\": Unable to open file.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}

				free(newVirtual);
				free(ftpPath);
			}
		}

		else if (!stricmp(szCmd, "ABOR")) {
		socketUtility::closeSocket(sPasv);
			dwRestOffset = 0;
			socketSendString(sCmd, "200 ABOR command successful.\r\n");
		}

		else if (!stricmp(szCmd, "SIZE")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);

				uint32_t fileHandle;
				if (fileSystem::fileOpen(ftpPath, fileSystem::FileModeRead, fileHandle) == true)
				{
					uint32_t fileSize = 0;
					fileSystem::fileSize(fileHandle, fileSize);
					sprintf(szOutput, "213 %llu\r\n", fileSize);
					socketSendString(sCmd, szOutput);
					fileSystem::fileClose(fileHandle);
				}
				else
				{
					sprintf(szOutput, "550 \"%s\": File not found.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}

				free(newVirtual);
				free(ftpPath);
			}
		}

		else if (!stricmp(szCmd, "MDTM")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				for (i = 0; i < 14; i++) {
					if ((pszParam[i] < '0') || (pszParam[i] > '9')) {
						break;
					}
				}
				if ((i == 14) && (pszParam[14] == ' ')) {
					strncpy(szOutput, pszParam, 4);
					szOutput[4] = 0;
					fileTime.year = (WORD)stringUtility::toInt(szOutput);
					strncpy(szOutput, pszParam + 4, 2);
					szOutput[2] = 0;
					fileTime.month = (WORD)stringUtility::toInt(szOutput);
					strncpy(szOutput, pszParam + 6, 2);
					fileTime.day = (WORD)stringUtility::toInt(szOutput);
					strncpy(szOutput, pszParam + 8, 2);
					fileTime.hour = (WORD)stringUtility::toInt(szOutput);
					strncpy(szOutput, pszParam + 10, 2);
					fileTime.minute = (WORD)stringUtility::toInt(szOutput);
					strncpy(szOutput, pszParam + 12, 2);
					fileTime.second = (WORD)stringUtility::toInt(szOutput);
					pszParam += 15;
					dw = 1;
				} else {
					dw = 0;
				}

				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				if (dw == 1) {

					char* ftpPath = driveManager::mapFtpPath(newVirtual);
					if (fileSystem::setFileTime(ftpPath, fileTime) == true)
					{
						socketSendString(sCmd, "250 MDTM command successful.\r\n");
					}
					else
					{
						sprintf(szOutput, "550 \"%s\": File not found.\r\n", newVirtual);
						socketSendString(sCmd, szOutput);
					}
					free(ftpPath);
			
				} else {
		
					char* ftpPath = driveManager::mapFtpPath(newVirtual);
					fileSystem::FileInfoDetail* fileInfoDetail = fileSystem::fileGetFileInfoDetail(ftpPath);
					if (fileInfoDetail != NULL)
					{
						sprintf(szOutput, "213 %04u%02u%02u%02u%02u%02u\r\n", fileInfoDetail->writeTime.year, fileInfoDetail->writeTime.month, fileInfoDetail->writeTime.day, fileInfoDetail->writeTime.hour, fileInfoDetail->writeTime.minute, fileInfoDetail->writeTime.second);
						socketSendString(sCmd, szOutput);
					}
					else
					{
						sprintf(szOutput, "550 \"%s\": File not found.\r\n", newVirtual);
						socketSendString(sCmd, szOutput);
					}
					delete(fileInfoDetail);
					free(ftpPath);
				}
				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "DELE")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);
				if (fileSystem::fileDelete(ftpPath) == true)
				{
					sprintf(szOutput, "250 \"%s\" deleted successfully.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				} 
				else 
				{
					sprintf(szOutput, "550 \"%s\": File not found.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}
				free(ftpPath);
				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "RNFR")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);
				bool exists;
				if ((fileSystem::fileExists(ftpPath, exists) == true && exists == true) || (fileSystem::directoryExists(ftpPath, exists) == true && exists == true))
				{
					free(rnfr);
					rnfr = strdup(ftpPath);
					sprintf(szOutput, "350 \"%s\": File/Directory exists; proceed with RNTO.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				} else {
					sprintf(szOutput, "550 \"%s\": File/Directory not found.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}
				free(ftpPath);
				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "RNTO")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else if (rnfr == NULL) {
				socketSendString(sCmd, "503 Bad sequence of commands. Send RNFR first.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);

				if (fileSystem::fileMove(rnfr, ftpPath) == true)
				{
					socketSendString(sCmd, "250 RNTO command successful.\r\n");
					free(rnfr);
					rnfr = NULL;
				} 
				else 
				{
					sprintf(szOutput, "553 \"%s\": Unable to rename file.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}
				free(newVirtual);
				free(ftpPath);
			}
		}

		else if (!stricmp(szCmd, "MKD") || !stricmp(szCmd, "XMKD")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);
				if (fileSystem::directoryCreate(ftpPath) == true)
				{
					sprintf(szOutput, "250 \"%s\" created successfully.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				} 
				else 
				{
					sprintf(szOutput, "550 \"%s\": Unable to create directory.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}
				free(ftpPath);
				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "RMD") || !stricmp(szCmd, "XRMD")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				socketSendString(sCmd, "530 Not logged in.\r\n");
			} else {
				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);
				if (fileSystem::directoryDelete(ftpPath, true) == true)
				{
					sprintf(szOutput, "250 \"%s\" removed successfully.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				} 
				else 
				{
					sprintf(szOutput, "550 \"%s\": Unable to remove directory.\r\n", newVirtual);
					socketSendString(sCmd, szOutput);
				}
				free(ftpPath);
				free(newVirtual);
			}
		}

		else if (!stricmp(szCmd, "OPTS")) {
			if (!*pszParam) {
				socketSendString(sCmd, "501 Syntax error in parameters or arguments.\r\n");
			} else if (!stricmp(pszParam, "UTF8 On")) {
				socketSendString(sCmd, "200 Always in UTF8 mode.\r\n");
			} else {
				socketSendString(sCmd, "501 Option not understood.\r\n");
			}
		}

		else if (!stricmp(szCmd, "AVBL")) {
			if (!*pszParam) {				
				socketSendString(sCmd, "550 Syntax error in parameters or arguments.\r\n");
			} else {

				char* newVirtual = resolveRelative(currentVirtual, pszParam);
				char* ftpPath = driveManager::mapFtpPath(newVirtual);

				uint64_t totalFree;
				if (driveManager::getTotalFreeNumberOfBytes(ftpPath, totalFree) == true)
				{
					sprintf(szOutput, "213 %llu\r\n", totalFree);
					socketSendString(sCmd, szOutput);
				}
				else 
				{
					sprintf(szOutput, "550 \"%s\": not found.\r\n", pszParam);
					socketSendString(sCmd, szOutput);
				}

				free(ftpPath);
				free(newVirtual);
			}
		}

		else {
			sprintf(szOutput, "500 Syntax error, command \"%s\" unrecognized.\r\n",szCmd);
			socketSendString(sCmd,szOutput);
		}

	}

	socketUtility::closeSocket(sPasv);
	socketUtility::closeSocket(sCmd);

	if (isLoggedIn) {
		decrementConnections();
	}

	return false;
}

bool WINAPI ftpServer::listenThread(LPVOID lParam)
{
	uint64_t sIncoming = 0;

	while (mStopRequested == false) 
	{
		const int result = socketUtility::getReadStatus(sListen);
		if (result == SOCKET_ERROR)
		{
			utils::debugPrint("Error: Socket status failed: %i\n", WSAGetLastError());
			break;
		}

		if (result == 1 && socketUtility::acceptSocket(sListen, sIncoming))
		{
			HANDLE connectionThreadHandle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)connectionThread, (void*)sIncoming, 0, NULL);
			if (connectionThreadHandle != NULL) {
				SetThreadPriority(connectionThreadHandle, 2);
				CloseHandle(connectionThreadHandle);
			}
		}
		else
		{
			Sleep(50);
		}
	}

	socketUtility::closeSocket(sListen);
	socketUtility::closeSocket(sIncoming);
	return false;
}

bool ftpServer::init()
{
	mStopRequested = false;
	mServerRunning = false;

	mListenThreadHandle = NULL;

	XSetFileCacheSize(4*1024*1024);

	memset(&saiListen, 0, sizeof(SOCKADDR_IN));
	saiListen.sin_family = AF_INET;
	saiListen.sin_addr.S_un.S_addr = INADDR_ANY;
	saiListen.sin_port = htons(21);

	mMaxConnections = 20;
	mCommandTimeout = 300;
	mConnectTimeout = 15;

	socketUtility::createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, sListen);

	if (!socketUtility::bindSocket(sListen, &saiListen)) {
		socketUtility::closeSocket(sListen);
		return false;
	}

	socketUtility::listenSocket(sListen, SOMAXCONN);

	mListenThreadHandle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)listenThread, 0, 0, NULL);
	if (mListenThreadHandle != NULL) {
		SetThreadPriority(mListenThreadHandle, 2);
	}

	mServerRunning = true;
	return true;
}

void ftpServer::close()
{
	mStopRequested = true;
	WaitForSingleObject(mListenThreadHandle, INFINITE);
	CloseHandle(mListenThreadHandle);
	mServerRunning = false;
}

bool ftpServer::isRunning()
{
	return mServerRunning;
}

int ftpServer::getActiveConnections()
{
	return (int)activeConnections;
}

uint32_t ftpServer::getTotalBytesReceived()
{
	return (uint32_t)totalBytesReceived;
}

uint32_t ftpServer::getTotalBytesSent()
{
	return (uint32_t)totalBytesSent;
}

bool ftpServer::socketSendString(uint64_t s, const char *psz)
{		
	bool bSuccess = send((SOCKET)s, psz, strlen(psz), 0) != SOCKET_ERROR;
	return bSuccess;
}

ftpServer::ReceiveStatus ftpServer::socketReceiveString(uint64_t s, char *psz, uint32_t dwMaxChars, uint32_t* pdwCharsReceived)
{
	uint32_t dwChars = 0;
	ReceiveStatus status, statusError;
	char buf[2];
	uint32_t dw;

	for (;;) {
		if (dwChars == dwMaxChars) {
			statusError = ReceiveStatus_Insufficient_Buffer;
			break;
		}

		status = socketReceiveLetter(s, psz, dwMaxChars - dwChars, &dw);
		if (status == ReceiveStatus_OK) {
			dwChars += dw;
			if (*psz == '\r') *psz = 0;
			else if (*psz == '\n') {
				*psz = 0;
				*pdwCharsReceived = dwChars;
				return ReceiveStatus_OK;
			}
			psz += dw;
		}
		else if (status == ReceiveStatus_Invalid_Data || status == ReceiveStatus_Insufficient_Buffer) {
			statusError = status;
			break;
		}
		else {
			return status;
		}
	}

	// A non-critical error occurred, read until end of line
	for (;;) {
		status = socketReceiveLetter(s, buf, sizeof(buf) / sizeof(char), &dw);
		if (status == ReceiveStatus_OK) {
			if (*buf == '\n') {
				return statusError;
			}
		}
		else if (status == ReceiveStatus_Invalid_Data || status == ReceiveStatus_Insufficient_Buffer) {
			// Go on...
		}
		else {
			return status;
		}
	}
}

ftpServer::ReceiveStatus ftpServer::socketReceiveLetter(uint64_t s, char* pch, uint32_t dwMaxChars, uint32_t* pdwCharsReceived)
{
	char buf[4];
	uint32_t dw;
	TIMEVAL tv;
	fd_set fds;

	tv.tv_sec = mCommandTimeout;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET((SOCKET)s, &fds);
	dw = select(0, &fds, 0, 0, &tv);
	if (dw == SOCKET_ERROR || dw == 0) return ReceiveStatus_Timeout;
	dw = recv((SOCKET)s, &buf[0], 1, 0);
	if (dw == SOCKET_ERROR || dw == 0) return ReceiveStatus_Network_Error;

	if (dwMaxChars == 0) {
		return ReceiveStatus_Insufficient_Buffer;
	}

	pch[0] = buf[0];

	*pdwCharsReceived = 1;
	return ReceiveStatus_OK;
}

ftpServer::ReceiveStatus ftpServer::socketReceiveData(uint64_t s, char *psz, uint32_t dwBytesToRead, uint32_t* pdwBytesRead)
{
	uint32_t dw;
	TIMEVAL tv;
	fd_set fds;

	tv.tv_sec = mConnectTimeout;
	tv.tv_usec = 0;
	memset(&fds, 0, sizeof(fd_set));	
	FD_SET((SOCKET)s, &fds);
	dw = select(0, &fds, 0, 0, &tv);
	if (dw == SOCKET_ERROR || dw == 0)  {
		utils::debugPrint("Error: SocketReceiveData failed with Timeout.\n");
		return ReceiveStatus_Timeout;
	}
	dw = recv((SOCKET)s, psz, dwBytesToRead, 0);
	if (dw == SOCKET_ERROR) {
		utils::debugPrint("Error: SocketReceiveData failed with Network Error.\n");
		return ReceiveStatus_Network_Error;
	}
	*pdwBytesRead = dw;
	return ReceiveStatus_OK;
}

uint64_t ftpServer::establishDataConnection(sockaddr_in *psaiData, uint64_t*psPasv)
{
	uint64_t sData;
	uint32_t dw;
	TIMEVAL tv;
	fd_set fds;

	if (psPasv && *psPasv) {
		tv.tv_sec = mConnectTimeout;
		tv.tv_usec = 0;
		memset(&fds, 0, sizeof(fd_set));
		FD_SET((SOCKET)*psPasv,&fds);
		dw = select(0,&fds,0,0,&tv);
		if (dw && dw != SOCKET_ERROR) {
			socketUtility::acceptSocket(*psPasv, psaiData, sData);
		} else {
			sData=0;
		}
		socketUtility::closeSocket(*psPasv);
		return sData;
	} else {
		
		socketUtility::createSocket(AF_INET,SOCK_STREAM, IPPROTO_TCP, sData);
		if (!socketUtility::connectSocket(sData, psaiData)) {
			socketUtility::closeSocket(sData);
			return INVALID_SOCKET;
		} else {
			return sData;
		}
	}
}

bool ftpServer::receiveSocketFile(uint64_t sCmd, uint64_t sData, uint32_t fileHandle)
{
	uint32_t socketBufferSize = RECV_SOCKET_BUFFER_SIZE;
	socketUtility::setSocketRecvSize(sData, socketBufferSize);

	int driveSectorBufferSize = 128 * 1024;
	int combinedBufferSize = socketBufferSize + driveSectorBufferSize;
	char *combinedBuffer = (char*)malloc(combinedBufferSize);

	bool fileComplete = false;

	uint32_t totalBytesToWrite = 0;
	uint32_t totalWritten = 0;

	while (true)
	{

		while (!fileComplete && totalBytesToWrite < (uint32_t)driveSectorBufferSize)
		{
			uint32_t bytesRead = 0;
			if (socketReceiveData(sData, combinedBuffer + totalBytesToWrite, socketBufferSize, &bytesRead) != ReceiveStatus_OK)
			{
				free(combinedBuffer);
				return false;
			}
			totalBytesToWrite += bytesRead;
			if (bytesRead > 0) {
				_InterlockedExchangeAdd(&totalBytesReceived, (LONG)bytesRead);
			}
			if (bytesRead == 0) {
				fileComplete = true;
				break;
			}
		}
		
		if (fileComplete && totalBytesToWrite == 0)
		{
			free(combinedBuffer);
			return true;
		}

		uint32_t bytesWritten = 0;
		uint32_t bytesToWrite = min((uint32_t)driveSectorBufferSize, totalBytesToWrite);
		if (!fileSystem::fileWrite(fileHandle, combinedBuffer, bytesToWrite, bytesWritten))
		{
			utils::debugPrint("Error: WriteFile failed: %i", GetLastError());
			free(combinedBuffer);
			return false;
		}

		totalBytesToWrite -= bytesWritten;
		totalWritten += bytesWritten;
		memmove(combinedBuffer, combinedBuffer + bytesWritten, totalBytesToWrite);
		
	}
}

bool ftpServer::sendSocketFile(uint64_t sCmd, uint64_t sData, uint32_t fileHandle, uint32_t* pdwAbortFlag)
{
	uint32_t bufferSize = SEND_SOCKET_BUFFER_SIZE;
	socketUtility::setSocketSendSize(sData, bufferSize);
	char *szBuffer = (char*)malloc(bufferSize);

	while (true)
	{
		uint32_t bytesRead;
		if (fileSystem::fileRead(fileHandle, szBuffer, bufferSize, bytesRead) == false)
		{
			utils::debugPrint("Error: ReadFile failed: %i", GetLastError());
			free(szBuffer);
			return false;
		}

		if (bytesRead == 0)
		{
			free(szBuffer);
			return true;
		}

		int bytesToSend = bytesRead;
		int bufferOffset = 0;

		while (bytesToSend > 0)
		{
			int sent = send((SOCKET)sData, szBuffer + bufferOffset, bytesToSend, 0);
			if (sent > 0) {
				_InterlockedExchangeAdd(&totalBytesSent, (LONG)sent);
			}
			int queueLength = 0;
			socketUtility::getReadQueueLength(sCmd, queueLength);
			if (queueLength > 0) {
				char szCmd[512];
				uint32_t bytes_received = 0;
				if (socketReceiveString(sCmd, szCmd, 512, &bytes_received) == ReceiveStatus_OK) {
					if (!stricmp(szCmd, "ABOR")) {
						*pdwAbortFlag = 1;
						free(szBuffer);
						return false;
					}
					else {
						socketSendString(sCmd, "500 Only command allowed at this time is ABOR.\r\n");
					}
				}
			}
			bytesToSend -= sent;
			bufferOffset += (int)sent;
			if (sent < 1)
			{
				free(szBuffer);
				return false;
			}
		}
	}
	return true;
}
