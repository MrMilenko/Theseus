// network.cpp: net:: namespace implementation. The Xbox path drives
// the dashboard's FTP server and the real XNet stack; the desktop
// path stubs the namespace so callers (Discord relay, FTP widget,
// IP display) compile without a real network backend.
#include "std.h"
#include "network.h"

#ifdef _XBOX
#include "toolbox/ftpServer.h"
#include "toolbox/driveManager.h"
#include <xtl.h>
#include <xonline.h>
#include <winsockx.h>
#include <stdio.h>

static bool g_networkInitialized = false;
static bool g_ftpRunning = false;

namespace net
{

	bool init()
	{
		if (g_networkInitialized)
			return true;

		if (!(XNetGetEthernetLinkStatus() & XNET_ETHERNET_LINK_ACTIVE))
		{
			OutputDebugStringA("[net] Ethernet not active.\n");
			return false;
		}

		XNetStartupParams xnsp = {};
		xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
		xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
		xnsp.cfgPrivatePoolSizeInPages = 64;
		xnsp.cfgEnetReceiveQueueLength = 16;
		xnsp.cfgIpFragMaxSimultaneous = 16;
		xnsp.cfgIpFragMaxPacketDiv256 = 32;
		xnsp.cfgSockMaxSockets = 64;
		xnsp.cfgSockDefaultRecvBufsizeInK = 128;
		xnsp.cfgSockDefaultSendBufsizeInK = 128;

		if (XNetStartup(&xnsp) != 0)
		{
			OutputDebugStringA("[net] XNetStartup failed.\n");
			return false;
		}

		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			OutputDebugStringA("[net] WSAStartup failed.\n");
			XNetCleanup();
			return false;
		}

		OutputDebugStringA("[net] Initialized using system config.\n");
		g_networkInitialized = true;

		// Mount drives and start FTP server
		driveManager::init();
		if (ftpServer::init())
		{
			OutputDebugStringA("[net] FTP server started on port 21.\n");
			g_ftpRunning = true;
		}
		else
		{
			OutputDebugStringA("[net] FTP server failed to start.\n");
		}

		return true;
	}

	void shutdown()
	{
		if (!g_networkInitialized)
			return;

		if (g_ftpRunning)
		{
			ftpServer::close();
			g_ftpRunning = false;
			OutputDebugStringA("[net] FTP server stopped.\n");
		}

		WSACleanup();
		XNetCleanup();

		OutputDebugStringA("[net] Shutdown complete.\n");
		g_networkInitialized = false;
	}

	void restart()
	{
		shutdown();
		init();
	}

	bool isReady()
	{
		if (!g_networkInitialized)
			return false;

		XNADDR addr = {};
		DWORD state = XNetGetTitleXnAddr(&addr);
		return (state != XNET_GET_XNADDR_PENDING);
	}

	bool isFtpRunning()
	{
		return g_ftpRunning;
	}

	DWORD getAddress()
	{
		XNetConfigStatus status = {};
		XNetGetConfigStatus(&status);
		return status.ina.S_un.S_addr;
	}

	DWORD getSubnet()
	{
		XNetConfigStatus status = {};
		XNetGetConfigStatus(&status);
		return status.inaMask.S_un.S_addr;
	}

	DWORD getGateway()
	{
		XNetConfigStatus status = {};
		XNetGetConfigStatus(&status);
		return status.inaGateway.S_un.S_addr;
	}

	DWORD getPrimaryDns()
	{
		XNetConfigStatus status = {};
		XNetGetConfigStatus(&status);
		return status.inaDnsPrimary.S_un.S_addr;
	}

	DWORD getSecondaryDns()
	{
		XNetConfigStatus status = {};
		XNetGetConfigStatus(&status);
		return status.inaDnsSecondary.S_un.S_addr;
	}
}

#else // desktop: no FTP UI today; stubs let callers link.

namespace net
{
	bool init()           { return true; }
	void shutdown()       {}
	void restart()        {}
	bool isReady()        { return false; }
	bool isFtpRunning()   { return false; }
	DWORD getAddress()    { return 0; }
	DWORD getSubnet()     { return 0; }
	DWORD getGateway()    { return 0; }
	DWORD getPrimaryDns() { return 0; }
	DWORD getSecondaryDns(){ return 0; }
}

#endif // _XBOX
