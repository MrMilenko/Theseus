// ftpServer.h: lightweight FTP server class. Listens on port 21, accepts
// connections, drives auth + command parsing + data-channel transfers
// against the toolbox file system. Stays up in panic mode for remote
// recovery / file pushing.

#pragma once

#include "socketUtility.h"
#include "fileSystem.h"
#include <cstdio>
#include <xtl.h>
#include <winsockx.h>

class ftpServer
{
public:

	typedef enum _ReceiveStatus {
		ReceiveStatus_OK = 1,
		ReceiveStatus_Network_Error,
		ReceiveStatus_Timeout,
		ReceiveStatus_Invalid_Data,
		ReceiveStatus_Insufficient_Buffer
	} ReceiveStatus;

	static bool WINAPI connectionThread(uint64_t pParam);

	static bool WINAPI listenThread(LPVOID lParam);

	static bool init();
	static void close();

	// Live status (safe to call from any thread; returns torn reads on byte
	// counters but the FTP widget samples once per second so a stale word
	// here is fine).
	static bool isRunning();
	static int  getActiveConnections();
	static uint32_t getTotalBytesReceived();
	static uint32_t getTotalBytesSent();
	static bool socketSendString(uint64_t s, const char *psz);
	static ReceiveStatus socketReceiveString(uint64_t s, char *psz, uint32_t dwMaxChars, uint32_t* pdwCharsReceived);
	static ReceiveStatus socketReceiveLetter(uint64_t s, char* pch, uint32_t dwMaxChars, uint32_t* pdwCharsReceived);
	static ReceiveStatus socketReceiveData(uint64_t s, char *psz, uint32_t dwBytesToRead, uint32_t* pdwBytesRead);
	static uint64_t establishDataConnection(sockaddr_in* psaiData, uint64_t* psPasv);
	static bool receiveSocketFile(uint64_t sCmd, uint64_t sData, uint32_t fileHandle);
	static bool sendSocketFile(uint64_t sCmd, uint64_t sData, uint32_t fileHandle, uint32_t* pdwAbortFlag);
};
