// socketUtility.h: thin BSD-socket-style wrappers around Xbox winsockx
// for the FTP server (create / bind / listen / accept / send / recv with
// timeouts and error reporting).

#pragma once

#include "xboxinternals.h"

#include <xtl.h>
#include <winsockx.h>

#define RECV_SOCKET_BUFFER_SIZE_IN_K 64
#define RECV_SOCKET_BUFFER_SIZE RECV_SOCKET_BUFFER_SIZE_IN_K * 1024
#define SEND_SOCKET_BUFFER_SIZE_IN_K 64
#define SEND_SOCKET_BUFFER_SIZE SEND_SOCKET_BUFFER_SIZE_IN_K * 1024

class socketUtility
{
public:
	static bool createSocket(int af, int type, int protocol, uint64_t& result);
	static uint64_t createSocket(sockaddr_in sockaddr_in, bool allow_reuse);
	static bool connectSocket(uint64_t socket, sockaddr_in* socket_addr_in);
	static bool connectSocket(uint64_t socket, sockaddr* socket_addr);
	static bool acceptSocket(uint64_t socket, uint64_t&result);
	static bool acceptSocket(uint64_t socket, sockaddr_in* socket_addr_in, uint64_t& result);
	static bool acceptSocket(uint64_t socket, sockaddr* socket_addr, uint64_t& result);
	static bool setSocketRecvSize(uint64_t socket, uint32_t &recv_size);
	static bool setSocketSendSize(uint64_t socket, uint32_t &send_size);
	static bool getReadQueueLength(uint64_t socket, int &queue_length);
	static bool bindSocket(uint64_t socket, sockaddr_in*socket_addr_in);
	static bool bindSocket(uint64_t socket, sockaddr* socket_addr);
	static bool listenSocket(uint64_t socket, int count);
	static bool closeSocket(uint64_t& socket);
	static bool getSocketName(uint64_t socket, sockaddr_in* socket_addr_in);
	static bool getSocketName(uint64_t socket, sockaddr* socket_addr);
	static bool WINAPI socketUtility::downloadThread(LPVOID lParam);
    static bool resolveHostname(const char* hostname, IN_ADDR* addr);
	static HANDLE downloadFile(const char* hostname, const char* path, const char* outputFile, CallbackFunction callback);
	static int getAvailableDataSize(const uint64_t socket);
	static int receiveSocketData(uint64_t socket, char* buffer, int size);
	static int sendSocketData(uint64_t socket, const char* buffer, int size);
	static int getReadStatus(const uint64_t socket);
	static int endBrokerSocket(uint64_t socket);
};
