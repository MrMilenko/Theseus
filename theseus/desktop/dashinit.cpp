// dashinit.cpp: desktop dashboard init / shutdown. Boots the SDL +
// OpenGL platform layer, loads Config.ini, brings up the audio
// engine and asset preloader. Counterpart to the InitApp /
// CleanupApp pair in xbox/main.cpp.

#include "std.h"
#include "dashapp.h"
#include "dashlocale.h"
#include "settingsfile.h"
#include "network.h"
#include "discord.h"

extern bool g_bMovingScreen;

void InitSkin()
{
	char CurrentSkinFile[MAX_PATH] = {0};
	char WorkerString[MAX_PATH] = {0};
	char SkinString[MAX_PATH] = {0};

	CSettingsFile* pSkinXBX = new CSettingsFile;
	bool bSkinLoaded = false;

	if (pSkinXBX->Open("Q:\\System\\Config.ini"))
	{
		if (pSkinXBX->GetValue("Dashboard Settings", "Current Skin", CurrentSkinFile, MAX_PATH))
		{
			pSkinXBX->Close();

			sprintf(SkinString, "Q:\\Skins\\%s\\", CurrentSkinFile);
			sprintf(WorkerString, "%s%s.xbx", SkinString, CurrentSkinFile);

			if (pSkinXBX->Open(WorkerString))
			{
				bSkinLoaded = true;
			}
			else
			{
				OutputDebugString("[InitSkin] Failed to load custom skin, attempting fallback...\n");
			}
		}
		else
		{
			OutputDebugString("[InitSkin] Config.ini missing 'Current Skin' entry\n");
			pSkinXBX->Close();
		}
	}
	else
	{
		OutputDebugString("[InitSkin] Could not open Config.ini\n");
	}

	if (!bSkinLoaded)
	{
		strcpy(CurrentSkinFile, "Stock");
		sprintf(SkinString, "Q:\\Skins\\Stock\\");
		sprintf(WorkerString, "%sStock.xbx", SkinString);

		pSkinXBX->Close();
		if (!pSkinXBX->Open(WorkerString))
		{
			OutputDebugString("[InitSkin] FATAL: Failed to load fallback Stock skin\n");
			delete pSkinXBX;
			g_pSkinSettings = NULL;
			g_sSkinDir = NULL;
			return;
		}
	}

	g_sSkinDir = new char[strlen(SkinString) + 1];
	strcpy(g_sSkinDir, SkinString);
	g_pSkinSettings = pSkinXBX;

	char szNoisy[16];
	if (g_pSkinSettings->GetValue("Camera", "Noisy", szNoisy, sizeof(szNoisy)))
		g_bMovingScreen = (strcasecmp(szNoisy, "false") != 0);
}

extern void FlushTextureCache();
extern void Material_Init(bool bReloadSkinXBX);

void ReloadSkin()
{
	if (g_pSkinSettings)
	{
		g_pSkinSettings->Close();
		delete g_pSkinSettings;
		g_pSkinSettings = NULL;
	}
	if (g_sSkinDir)
	{
		delete [] g_sSkinDir;
		g_sSkinDir = NULL;
	}

	InitSkin();

	if (!g_pSkinSettings)
	{
		OutputDebugString("[ReloadSkin] InitSkin failed, skin not reloaded\n");
		return;
	}

	FlushTextureCache();
	Material_Init(true);

	OutputDebugString("[ReloadSkin] Skin reloaded successfully\n");
}

void InitDiscord()
{
	g_DiscordEnabled = false;
	InitDiscordConfig();

	if (!IsDiscordRelayEnabled())
	{
		return;
	}

	OutputDebugString("[Discord] Relay is enabled.\n");
	g_DiscordEnabled = true;

	const char* titleIdHex = "0ffeeff0";
	SendDiscordRelayFromConfig(titleIdHex);
}

void DashInit()
{
	// Desktop: drives are mounted via xboxfs path mapping; no symlinks needed.
	// Ensure music directory exists.
	CreateDirectory("e:\\TDATA", NULL);
	CreateDirectory("e:\\TDATA\\fffe0000", NULL);
	CreateDirectory("e:\\TDATA\\fffe0000\\music", NULL);

	// Set current region
	g_nCurRegion = 1; // NA

	// Initialize skin system
	InitSkin();

	// Init networking
	if (net::init())
	{
		IN_ADDR addr = {net::getAddress()};
		char ipStr[16];
		XNetInAddrToString(addr, ipStr, sizeof(ipStr));
		TRACE("Network Up. IP: %S\n", ipStr);
	}
	else
	{
		TRACE("Network failed to initialize.\n");
	}

	// Start Discord relay if enabled
	InitDiscord();
}
