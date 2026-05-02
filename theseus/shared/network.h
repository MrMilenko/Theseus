// network.h: net:: namespace for the Xbox network stack lifecycle and
// IP / DNS introspection. Used by the FTP toolbox and the desktop
// network status widget. Forked from PrometheOS.

#pragma once

#ifdef _XBOX
#include <xtl.h>
#include <xonline.h>
#include <winsockx.h>
#endif

namespace net
{
	bool init();
	void shutdown();
	void restart();
	bool isReady();
	bool isFtpRunning();

	DWORD getAddress();
	DWORD getSubnet();
	DWORD getGateway();
	DWORD getPrimaryDns();
	DWORD getSecondaryDns();
}
