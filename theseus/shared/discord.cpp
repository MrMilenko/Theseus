// discord.cpp: Discord webhook relay. Reads a webhook URL from
// Config.ini and posts presence / event messages on a background thread
// so the dashboard never blocks on TCP. Theseus-original; not in retail.

#include "std.h"
#ifdef _XBOX
#include <xtl.h>
#include <winsockx.h>
#endif
#include <stdio.h>
#include <string.h>
#include "discord.h"
#include "settingsfile.h"
#include "theseus.h"

bool g_DiscordEnabled = false;
char g_DiscordIP[64] = "127.0.0.1";
int g_DiscordPort = 1103;


void InitDiscordConfig()
{
	TCHAR szIP[64] = {0};
	TCHAR szPort[16] = {0};
	TCHAR szEnabled[8] = {0};

	CSettingsFile* pConfig = new CSettingsFile;

	if (pConfig->Open(_T("Q:\\System\\Config.ini")))
	{
		bool bConfigLoaded = false;

		if (pConfig->GetValue(_T("Discord"), _T("Enabled"), szEnabled, countof(szEnabled)) &&
			pConfig->GetValue(_T("Discord"), _T("xbdStatsIP"), szIP, countof(szIP)) &&
			pConfig->GetValue(_T("Discord"), _T("xbdStatsPort"), szPort, countof(szPort)))
		{
			bConfigLoaded = true;
		}
		else
		{
			OutputDebugString(_T("[Discord] Config.ini missing Discord settings\n"));
		}

		pConfig->Close();
		delete pConfig;

		if (!bConfigLoaded)
		{
			g_DiscordEnabled = false;
			return;
		}

		// true/false or 1/0 depending on how we wanna implement into XIP's.
		g_DiscordEnabled = (_ttoi(szEnabled) != 0 || _tcsicmp(szEnabled, _T("true")) == 0);
		Ansi(g_DiscordIP, szIP, sizeof(g_DiscordIP));
		g_DiscordPort = _ttoi(szPort);

		OutputDebugString(_T("[Discord] Config loaded successfully\n"));
	}
	else
	{
		OutputDebugString(_T("[Discord] Could not open Config.ini\n"));
		delete pConfig;
		g_DiscordEnabled = false;
	}
}

bool SendDiscordRelay(const char* title_id_hex, const char* relay_ip, int port)
{
    char message[128];
    _snprintf(message, sizeof(message), "{\"id\":\"%s\"}", title_id_hex);

    if (port == 1102)  // UDP
    {
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) return false;

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(relay_ip);

        sendto(sock, message, strlen(message), 0, (struct sockaddr*)&addr, sizeof(addr));
        closesocket(sock);
        return true;
    }
    else if (port == 1103)  // TCP
    {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;

        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        server.sin_addr.s_addr = inet_addr(relay_ip);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR)
        {
            closesocket(sock);
            return false;
        }

        send(sock, message, strlen(message), 0);
        shutdown(sock, SD_SEND);
        closesocket(sock);
        return true;
    }
    else if (port == 1101)  // WebSocket
    {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;

        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        server.sin_addr.s_addr = inet_addr(relay_ip);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR)
        {
            closesocket(sock);
            return false;
        }

        char host_header[64];
        _snprintf(host_header, sizeof(host_header), "Host: %s:%d\r\n", relay_ip, port);

        const char* base_request =
            "GET / HTTP/1.1\r\n"
            "%s"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

        char upgrade_request[512];
        _snprintf(upgrade_request, sizeof(upgrade_request), base_request, host_header);
        send(sock, upgrade_request, strlen(upgrade_request), 0);

        char buffer[1024];
        int recv_len = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (recv_len <= 0)
        {
            closesocket(sock);
            return false;
        }
        buffer[recv_len] = '\0';
        if (!strstr(buffer, "101 Switching Protocols"))
        {
            closesocket(sock);
            return false;
        }

        int len = strlen(message);
        char frame[256];
        int frame_len = 0;
        frame[frame_len++] = 0x81;

        unsigned char mask_key[4] = {0x12, 0x34, 0x56, 0x78};

        if (len <= 125)
        {
            frame[frame_len++] = 0x80 | len;
        }
        else
        {
            frame[frame_len++] = 0x80 | 126;
            frame[frame_len++] = (len >> 8) & 0xFF;
            frame[frame_len++] = len & 0xFF;
        }

        memcpy(&frame[frame_len], mask_key, 4);
        frame_len += 4;

        for (int i = 0; i < len; i++)
        {
            frame[frame_len++] = message[i] ^ mask_key[i % 4];
        }

        send(sock, frame, frame_len, 0);
        shutdown(sock, SD_SEND);
        closesocket(sock);
        return true;
    }

    return false;
}
// Worker payload for the async Discord relay. Each call gets its own
// allocation so multiple in-flight relays don't trample each other.
struct DiscordRelayJob
{
    char title_id_hex[16];
    char relay_ip[64];
    int  port;
};

#ifdef _XBOX
static DWORD WINAPI DiscordRelayThread(LPVOID lParam)
{
    DiscordRelayJob* p = (DiscordRelayJob*)lParam;
    SendDiscordRelay(p->title_id_hex, p->relay_ip, p->port);
    delete p;
    return 0;
}
#endif

bool SendDiscordRelayFromConfig(const char* title_id_hex)
{
    if (!IsDiscordRelayEnabled())
        return false;

#ifdef _XBOX
    // Off the hot path: TCP/WebSocket modes do a synchronous connect()
    // that blocks the main thread for the OS TCP timeout (~20s) when
    // the relay endpoint is unreachable. Push the whole send onto a
    // worker thread so the caller (boot, scene transitions, FTP) keeps
    // moving regardless of network state.
    DiscordRelayJob* job = new DiscordRelayJob;
    _snprintf(job->title_id_hex, sizeof(job->title_id_hex), "%s", title_id_hex);
    _snprintf(job->relay_ip,     sizeof(job->relay_ip),     "%s", GetDiscordRelayIP());
    job->port = GetDiscordRelayPort();

    HANDLE h = CreateThread(NULL, 0, DiscordRelayThread, job, 0, NULL);
    if (h == NULL)
    {
        // Thread-create failure is unexpected; fall back to inline send
        // so the relay still happens (with the original blocking risk).
        SendDiscordRelay(job->title_id_hex, job->relay_ip, job->port);
        delete job;
    }
    else
    {
        CloseHandle(h); // fire-and-forget; we don't need to join
    }
    return true;
#else
    return SendDiscordRelay(title_id_hex, GetDiscordRelayIP(), GetDiscordRelayPort());
#endif
}
bool IsDiscordRelayEnabled()
{
    return g_DiscordEnabled;
}

const char* GetDiscordRelayIP()
{
    return g_DiscordIP;
}

int GetDiscordRelayPort()
{
    return g_DiscordPort;
}
