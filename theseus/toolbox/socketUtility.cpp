// socketUtility.cpp: BSD-socket-style helper implementations.

#include "socketUtility.h"
#include "utils.h"

bool socketUtility::createSocket(int af, int type, int protocol, uint64_t& result)
{
    result = socket(af, type, protocol);
    if (result < 0) 
    {
	    utils::debugPrint("Error: Create socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

uint64_t socketUtility::createSocket(sockaddr_in sockaddr_in, bool allow_reuse)
{
    const uint64_t listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == INVALID_SOCKET)
    {
	    utils::debugPrint("Error: Create socket failed: %i", WSAGetLastError());
	    return NULL;
    }

    int result;

    unsigned long allowLocalAddressReuse = 1;
    result = setsockopt((SOCKET)listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&allowLocalAddressReuse, sizeof(allowLocalAddressReuse));
    if (result != 0)
    {
	    utils::debugPrint("Error: Set socket option SO_REUSEADDR failed: %i", WSAGetLastError());
	    return NULL;
    }

    unsigned long nonBlocking = 1;
    if (ioctlsocket((SOCKET)listen_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR)
    {
	    utils::debugPrint("Error: IO control socket non-blocking failed: %i", WSAGetLastError());
	    return NULL;
    }

    result = bind((SOCKET)listen_socket, (sockaddr*)&sockaddr_in, sizeof(sockaddr_in));
    if (result == SOCKET_ERROR)
    {
	    utils::debugPrint("Error: Socket bind failed: %i", WSAGetLastError());
	    return NULL;
    }

    result = listen((SOCKET)listen_socket, 10);
    if (result == SOCKET_ERROR)
    {
	    utils::debugPrint("Error: Socket listen failed: %i", WSAGetLastError());
	    return NULL;
    }

    return listen_socket;
}

bool socketUtility::connectSocket(uint64_t socket, sockaddr_in* socket_addr_in)
{
    const int result = connect((SOCKET)socket, (sockaddr*)socket_addr_in, sizeof(SOCKADDR_IN));
    if (result < 0)
    {
	    utils::debugPrint("Error: Connect socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::connectSocket(uint64_t socket, sockaddr* socket_addr)
{
    const int result = connect((SOCKET)socket, socket_addr, sizeof(sockaddr));
    if (result < 0)
    {
        utils::debugPrint("Error: Connect socket failed: %i", WSAGetLastError());
        return false;
    }
    return true;
}

bool socketUtility::acceptSocket(uint64_t socket, uint64_t& result)
{
    result = accept((SOCKET)socket, NULL, 0);
    if (result < 0)
    {
	    utils::debugPrint("Error: Accept socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::acceptSocket(uint64_t socket, sockaddr_in* socket_addr_in, uint64_t& result)
{
    int dw = sizeof(sockaddr_in);
    result = accept((SOCKET)socket, (sockaddr*)socket_addr_in, &dw);
    if (result < 0)
    {
	    utils::debugPrint("Error: Accept socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::acceptSocket(uint64_t socket, sockaddr* socket_addr, uint64_t& result)
{
    int dw = sizeof(sockaddr);
    result = accept((SOCKET)socket, socket_addr, &dw);
    if (result < 0)
    {
	    utils::debugPrint("Error: Accept socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::setSocketRecvSize(uint64_t socket, uint32_t &recv_size)
{	
    uint32_t recvBufferSize = RECV_SOCKET_BUFFER_SIZE;
    int result = setsockopt((SOCKET)socket, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufferSize, sizeof(uint32_t));
    if (result < 0)
    {
	    utils::debugPrint("Error: Set socket option SO_RCVBUF failed: %i", WSAGetLastError());
    }

    int isize = sizeof(recvBufferSize);
    result = getsockopt((SOCKET)socket, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufferSize, (int*)&isize);
    if (result < 0)
    {
	    recvBufferSize = 8192;
    }

    recv_size = recvBufferSize;
    return true;
}

bool socketUtility::setSocketSendSize(uint64_t socket, uint32_t &send_size)
{	
    uint32_t sendBufferSize = SEND_SOCKET_BUFFER_SIZE;
    int result = setsockopt((SOCKET)socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(uint32_t));
    if (result < 0)
    {
	    utils::debugPrint("Error: Set socket option SO_SNDBUF failed: %i", WSAGetLastError());
    }

    int isize = sizeof(sendBufferSize);
    result = getsockopt((SOCKET)socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, (int*)&isize);
    if (result < 0)
    {
	    sendBufferSize = 8192;
    }

    send_size = sendBufferSize;
    return true;
}

bool socketUtility::getReadQueueLength(uint64_t socket, int &queue_length)
{
    DWORD temp;
    int result = ioctlsocket((SOCKET)socket, FIONREAD, &temp);
    if (result < 0)
    {
	    utils::debugPrint("Error: Get read queue length failed: %i", WSAGetLastError());
	    queue_length = 0;
	    return false;
    }
    queue_length = temp;
    return true;
}

bool socketUtility::bindSocket(uint64_t socket, sockaddr_in* socket_addr_in)
{
    int result = bind((SOCKET)socket, (sockaddr*)socket_addr_in, sizeof(sockaddr_in));
    if (result < 0)
    {
	    utils::debugPrint("Error: Bind socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::bindSocket(uint64_t socket, sockaddr* socket_addr)
{
    int result = bind((SOCKET)socket, socket_addr, sizeof(sockaddr));
    if (result < 0)
    {
        utils::debugPrint("Error: Bind socket failed: %i", WSAGetLastError());
        return false;
    }
    return true;
}

bool socketUtility::listenSocket(uint64_t socket, int count)
{
    int result = listen((SOCKET)socket, count);
    if (result < 0)
    {
	    utils::debugPrint("Error: Listen socket failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::closeSocket(uint64_t& socket)
{
    if (!socket) 
    {
	    return true;
    }
    int result = closesocket((SOCKET)socket);
    if (result < 0)
    {
	    utils::debugPrint("Error: Close socket failed: %i", WSAGetLastError());
	    socket = 0;
	    return false;
    }
    socket = 0;
    return true;
}

bool socketUtility::getSocketName(uint64_t socket, sockaddr_in* socket_addr_in)
{
    int size = sizeof(sockaddr_in);
    int result = getsockname((SOCKET)socket, (sockaddr*)socket_addr_in, &size);
    if (result < 0)
    {
	    utils::debugPrint("Error: Get socket name failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::getSocketName(uint64_t socket, sockaddr* socket_addr)
{
    int size = sizeof(sockaddr);
    const int result = getsockname((SOCKET)socket, socket_addr, &size);
    if (result < 0)
    {
	    utils::debugPrint("Error: Get socket name failed: %i", WSAGetLastError());
	    return false;
    }
    return true;
}

bool socketUtility::resolveHostname(const char* hostname, IN_ADDR* addr)
{
    XNDNS* pDns = NULL;
    if (XNetDnsLookup(hostname, NULL, &pDns) != 0 || !pDns || pDns->iStatus != 0) {
        return false;
    }

    *addr = pDns->aina[0];
    XNetDnsRelease(pDns);
    return true;
}
bool WINAPI socketUtility::downloadThread(LPVOID lParam)
{
    struct ThreadData
    {
	    CallbackFunction callback;
	    const char* hostname;
	    const char* path;
	    const char* outputFile;
    }* Params = reinterpret_cast<ThreadData*>(lParam);

    WSAEVENT hEvent = WSACreateEvent();
    XNDNS* pDns = NULL;
    Params->callback((void*)"Resolving host.");

    if (XNetDnsLookup(Params->hostname, hEvent, &pDns) != 0 || WaitForSingleObject(hEvent, 5000) != WAIT_OBJECT_0 || !pDns || pDns->iStatus != 0)
    {
        Params->callback((void*)"Unable to resolve the host.");
	    if (pDns) XNetDnsRelease(pDns);
        delete Params;
	    return false;
    }

    IN_ADDR addr = pDns->aina[0];
    XNetDnsRelease(pDns);
    Params->callback((void*)"Host resolved.");

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    SOCKADDR_IN server = {};
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr = addr;

    if (connect(sock, (SOCKADDR*)&server, sizeof(server)) == SOCKET_ERROR)
    {
        closesocket(sock);
        Params->callback((void*)"Unable to connect to the host.");
	    delete Params;
	    return false;
    }

    char request[512];
    sprintf(request,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        Params->path, Params->hostname);

    Params->callback((void*)"Requesting file...");

    send(sock, request, strlen(request), 0);

    const int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    bool headersParsed = false;
    int httpStatus = 0;
    int contentLength = -1;
    	
    FILE* file = fopen(Params->outputFile, "wb");
    if (!file)
    {
        closesocket(sock);
	    Params->callback((void*)"Unable to create the destination file.");
	    delete Params;
        return false;
    }
    int bytes, totalBytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer), 0)) > 0)
    {
	    if (!headersParsed)
        {
		    char* headerEnd = strstr(buffer, "\r\n\r\n");
		    if (headerEnd)
            {
                char version[16] = {0};
                if (sscanf(buffer, "HTTP/%15s %d", version, &httpStatus) != 2)
                {
	                fclose(file);
	                remove(Params->outputFile);
	                closesocket(sock);
	                Params->callback((void*)"Invalid HTTP response from the host.");
	                delete Params;
	                return false;
                }

                // Map HTTP status code to a description
                const char* statusDesc = "Unknown Status";
                switch (httpStatus)
                {
	                case 200: statusDesc = "OK"; break;
	                case 301: statusDesc = "Moved Permanently"; break;
	                case 302: statusDesc = "Found"; break;
	                case 400: statusDesc = "Bad Request"; break;
	                case 403: statusDesc = "Forbidden"; break;
	                case 404: statusDesc = "Not Found"; break;
	                case 500: statusDesc = "Internal Server Error"; break;
	                // Add more as needed
                }

                if (httpStatus != 200)
                {
				    fclose(file);
				    remove(Params->outputFile);
				    closesocket(sock);
				    delete Params;
				    return false;
                }

                char* contentLengthStr = strstr(buffer, "Content-Length:");
                if (contentLengthStr)
                {
				    contentLengthStr += strlen("Content-Length:");
				    while (*contentLengthStr == ' ' || *contentLengthStr == '\t') contentLengthStr++;
                    contentLength = atoi(contentLengthStr);
                }

                // Write remainder after headers
                int headerLen = headerEnd - buffer + 4;
                totalBytes = bytes - headerLen;
                char progress[64];
                sprintf(progress, "Downloaded %dKB of %dKB", totalBytes / 1024, contentLength / 1024);
                Params->callback((void*)progress);
                fwrite(headerEnd + 4, 1, totalBytes, file);

                headersParsed = true;
            }
        }
        else
        {
		    totalBytes += bytes;
		    char progress[64];
		    sprintf(progress, "Downloaded %dKB of %dKB", totalBytes / 1024, contentLength / 1024);
		    Params->callback((void*)progress);
		    fwrite(buffer, 1, bytes, file);
        }
    }

    if (bytes < 0)
    {
        fclose(file);
        remove(Params->outputFile);
        closesocket(sock);
        Params->callback((void*)"Socket error during download.");
        delete Params;
        return false;
    }

    fclose(file);
    closesocket(sock);
    Params->callback((void*)"Download complete.");
    delete Params;
    return true;
}

HANDLE socketUtility::downloadFile(const char* hostname, const char* path, const char* outputFile, CallbackFunction callback)
{
    struct ThreadData
    {
	    CallbackFunction callback;
	    const char* hostname;
	    const char* path;
	    const char* outputFile;
    };

    ThreadData* Params = new ThreadData;
    Params->callback = callback;
    Params->hostname = hostname;
    Params->path = path;
    Params->outputFile = outputFile;

    HANDLE hThread = CreateThread(
	    NULL,
	    0,
	    (LPTHREAD_START_ROUTINE)downloadThread,
	    (void*)Params,
	    0,
	    NULL
    );

    if (hThread != NULL) 
    {
	    return hThread;
    } else {
	    return NULL;
    }
}

int socketUtility::getAvailableDataSize(const uint64_t socket)
{
	unsigned long available_data = 0;
	ioctlsocket((SOCKET)socket, FIONREAD, &available_data);
	return (int)available_data;
}

int socketUtility::receiveSocketData(const uint64_t socket, char* buffer, const int size)
{
	const int bytes_received = recv((SOCKET)socket, buffer, size, 0);
	if (bytes_received == SOCKET_ERROR)
	{
		utils::debugPrint("Error: Socket receive failed: %i\n", WSAGetLastError());
	}
	return bytes_received;
}

int socketUtility::sendSocketData(const uint64_t socket, const char* buffer, const int size)
{
	const int bytes_sent = send((SOCKET)socket, buffer, size, 0);
	if (bytes_sent == SOCKET_ERROR)
	{
		utils::debugPrint("Error: Socket send failed: %i\n", WSAGetLastError());
	}
	return bytes_sent;
}

int socketUtility::getReadStatus(const uint64_t socket)
{
	static const timeval instantSpeedPlease = { 0,0 };
	fd_set a = { 1, {(SOCKET)socket} };

	int result = select(0, &a, 0, 0, &instantSpeedPlease);
	if (result == SOCKET_ERROR)
	{
		result = WSAGetLastError();
	}

	if (result != 0 && result != 1)
	{
		utils::debugPrint("Error: getReadStatus failed: %i\n", result);
		return SOCKET_ERROR;
	}
	return result;
}

int socketUtility::endBrokerSocket(uint64_t socket)
{
	int result = shutdown((SOCKET)socket, SD_BOTH);
	if (result != 0)
	{
		utils::debugPrint("Error: Socket shutdown failed: %i", WSAGetLastError());
	}
	result = closesocket((SOCKET)socket);
	if (result != 0)
	{
		utils::debugPrint("Error: Close socket failed: %i", WSAGetLastError());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}