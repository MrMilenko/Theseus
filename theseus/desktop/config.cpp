// config.cpp: desktop CConfig node. Stubs the Xbox EEPROM /
// hardware queries (language, video, audio, parental controls,
// etc.) so XAP scripts that call into CConfig still resolve.
// Counterpart to xbox/config.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"

#include "timezone.h"
#include "dashlocale.h"
#include "xbe.h"
#include "network.h"
#include "config.h"
CConfig* theConfig;

extern void Material_Init(bool bReloadSkinXBX);

void InitSkin();

// ============================================================================
// Node Registration
// ============================================================================

IMPLEMENT_NODE("Config", CConfig, CNode)

#undef _FND_CLASS
#define _FND_CLASS CConfig
START_NODE_FUN(CConfig, CNode)
NODE_FUN_IV(GetLanguage)
NODE_FUN_VI(SetLanguage)
NODE_FUN_IV(GetGamePCFlags)
NODE_FUN_VI(SetGamePCFlags)
NODE_FUN_IV(GetMoviePCFlags)
NODE_FUN_VI(SetMoviePCFlags)
NODE_FUN_SV(GetLaunchReason)
NODE_FUN_IV(GetLaunchContext)
NODE_FUN_IV(GetLaunchParameter1)
NODE_FUN_IV(GetLaunchParameter2)
NODE_FUN_II(CanDriveBeCleanup)
NODE_FUN_VV(BackToLauncher)

// UIX extensions
NODE_FUN_VV(PowerCycle)
NODE_FUN_VV(XBOXReset)
NODE_FUN_VV(Reset)
NODE_FUN_VV(PowerOff)
NODE_FUN_IV(GetInternalTemp)
NODE_FUN_VI(SetLED)
NODE_FUN_VI(SetFanSpeed)
NODE_FUN_IV(GetFanSpeed)
NODE_FUN_IV(GetCPUTemp)

NODE_FUN_IV(Get480Support)
NODE_FUN_VI(Set480Support)
NODE_FUN_IV(Get720Support)
NODE_FUN_VI(Set720Support)
NODE_FUN_IV(Get1080Support)
NODE_FUN_VI(Set1080Support)
NODE_FUN_IV(GetPAL60Support)
NODE_FUN_VI(SetPAL60Support)
NODE_FUN_SV(GetAVPackType)
NODE_FUN_SV(GetAVRegion)
NODE_FUN_SV(GetGameRegion)
NODE_FUN_IV(GetAutoOff)
NODE_FUN_VI(SetAutoOff)
NODE_FUN_IV(GetVideoMode)
NODE_FUN_VI(SetVideoMode)
NODE_FUN_VV(ApplySkin)
NODE_FUN_VV(FlushMeshCache)
NODE_FUN_IV(GetAudioMode)
NODE_FUN_VI(SetAudioMode)
NODE_FUN_IV(GetDolbyDigitalSupport)
NODE_FUN_VI(SetDolbyDigitalSupport)
NODE_FUN_IV(GetDTSSupport)
NODE_FUN_VI(SetDTSSupport)
NODE_FUN_IS(CheckParentPassword)
NODE_FUN_VS(SetParentPassword)
NODE_FUN_IV(GetTimeZone)
NODE_FUN_VI(SetTimeZone)
NODE_FUN_IV(GetDSTAllowed)
NODE_FUN_IV(GetDST)
NODE_FUN_VI(SetDST)
NODE_FUN_IV(ForceSetLanguage)
NODE_FUN_IV(ForceSetTimeZone)
NODE_FUN_IV(ForceSetClock)
NODE_FUN_SV(GetRecoveryKey)
NODE_FUN_SV(GetROMVersion)
NODE_FUN_SV(GetXdashVersion)

// UIX extensions (XKUtils/XBMC)
NODE_FUN_SV(GetXboxIP)
NODE_FUN_SV(GetEncoder)
NODE_FUN_SV(GetXBOXVersion)
NODE_FUN_SV(GetMODCHIPVersion)
NODE_FUN_SS(GetXBETitleID)
NODE_FUN_VV(NetworkStartup)
NODE_FUN_VV(NetworkReboot)
NODE_FUN_VV(NetworkShutdown)
NODE_FUN_IS(NtFileExists)
NODE_FUN_IS(FileExists)
NODE_FUN_IV(GetLiveToday)
NODE_FUN_VI(SetLiveToday)
NODE_FUN_IV(GetAcceptedLegalInfo)
NODE_FUN_VI(SetAcceptedLegalInfo)
NODE_FUN_VV(BackToLauncher2)
NODE_FUN_VV(GoToXOnlineDash)
NODE_FUN_IV(GetFontVersion)
NODE_FUN_VV(ToggleNoisyCamera)
NODE_FUN_IV(GetEthernetLinkStatus)

END_NODE_FUN()

// ============================================================================
// Construction
// ============================================================================

CConfig::CConfig()
{
}

CConfig::~CConfig()
{
}

// ============================================================================
// Language / Region (Desktop: hardcoded defaults)
// ============================================================================

int CConfig::GetLanguage()
{
	return 1; // English
}

void CConfig::SetLanguage(int nLanguage)
{
	(void)nLanguage;
}

CStrObject *CConfig::GetAVPackType()
{
	return new CStrObject("HDTV");
}

CStrObject *CConfig::GetAVRegion()
{
	return new CStrObject("NTSC_M");
}

CStrObject *CConfig::GetGameRegion()
{
	return new CStrObject("NA");
}

// ============================================================================
// Video / Audio Settings (Desktop: no-ops, window is freely resizable)
// ============================================================================

int CConfig::GetVideoMode()
{
	return 0;
}

void CConfig::SetVideoMode(int nVideoMode)
{
	(void)nVideoMode;
}

extern void ReloadSkin();
extern void FlushMeshCache();

void CConfig::ApplySkin()
{
	ReloadSkin();
}

void CConfig::FlushMeshCache()
{
	::FlushMeshCache();
}

int CConfig::Get480Support()
{
	return 1;
}

void CConfig::Set480Support(int b480Support)
{
	(void)b480Support;
}

int CConfig::Get720Support()
{
	return 1;
}

void CConfig::Set720Support(int b720Support)
{
	(void)b720Support;
}

int CConfig::Get1080Support()
{
	return 1;
}

void CConfig::Set1080Support(int b1080Support)
{
	(void)b1080Support;
}

int CConfig::GetPAL60Support()
{
	return 0;
}

void CConfig::SetPAL60Support(int bPAL60Support)
{
	(void)bPAL60Support;
}

int CConfig::GetAudioMode()
{
	return 1; // Stereo
}

void CConfig::SetAudioMode(int nAudioMode)
{
	(void)nAudioMode;
}

int CConfig::GetDolbyDigitalSupport()
{
	return 0;
}

void CConfig::SetDolbyDigitalSupport(int bDolbyDigitalSupport)
{
	(void)bDolbyDigitalSupport;
}

int CConfig::GetDTSSupport()
{
	return 0;
}

void CConfig::SetDTSSupport(int bDTSSupport)
{
	(void)bDTSSupport;
}

int CConfig::GetAutoOff()
{
	return 0;
}

void CConfig::SetAutoOff(int bAutoOff)
{
	(void)bAutoOff;
}

// ============================================================================
// Hardware Control Stubs (no-ops on desktop)
// ============================================================================

void CConfig::PowerOff()
{
	OutputDebugString("[Config] PowerOff() - no-op on desktop\n");
}

void CConfig::Reset()
{
	OutputDebugString("[Config] Reset() - no-op on desktop\n");
}

void CConfig::XBOXReset()
{
	OutputDebugString("[Config] XBOXReset() - no-op on desktop\n");
}

void CConfig::PowerCycle()
{
	OutputDebugString("[Config] PowerCycle() - no-op on desktop\n");
}

void CConfig::SetLED(int LEDMode)
{
	(void)LEDMode;
}

int CConfig::GetFanSpeed()
{
	return 0;
}

int CConfig::GetCPUTemp()
{
	return 0;
}

int CConfig::GetInternalTemp()
{
	return 0;
}

void CConfig::SetFanSpeed(int speed)
{
	(void)speed;
}

// ============================================================================
// Network
// ============================================================================

void CConfig::NetworkStartup()
{
	net::init();
}

void CConfig::NetworkShutdown()
{
	net::shutdown();
}

void CConfig::NetworkReboot()
{
	net::restart();
}

// ============================================================================
// Time / DST
// ============================================================================

int CConfig::GetTimeZone()
{
	// Desktop: default to NA Eastern
	return NA_DEFAULT_TIMEZONE;
}

void CConfig::SetTimeZone(int nTimeZone)
{
	(void)nTimeZone;
}

int CConfig::GetDSTAllowed()
{
	return 1;
}

int CConfig::GetDST()
{
	// Desktop: check host OS DST status
	time_t now = time(NULL);
	struct tm* lt = localtime(&now);
	return (lt && lt->tm_isdst > 0) ? 1 : 0;
}

void CConfig::SetDST(int bObserveDST)
{
	(void)bObserveDST;
}

// ============================================================================
// Parental Controls / Password
// ============================================================================

static uint32_t EncodePassword(const char *szPassword)
{
	uint32_t dwPassword = 0;
	for (const char *pch = szPassword; *pch != 0; pch += 1)
	{
		dwPassword <<= 4;

		switch (*pch)
		{
		case 'u': dwPassword += 1; break;  // up
		case 'd': dwPassword += 2; break;  // down
		case 'l': dwPassword += 3; break;  // left
		case 'r': dwPassword += 4; break;  // right
		case 'a': dwPassword += 5; break;  // A
		case 'b': dwPassword += 6; break;  // B
		case 'x': dwPassword += 7; break;  // X
		case 'y': dwPassword += 8; break;  // Y
		case 'B': dwPassword += 9; break;  // black
		case 'W': dwPassword += 10; break; // white
		case 'L': dwPassword += 11; break; // L trigger
		case 'R': dwPassword += 12; break; // R trigger
		default:
			ASSERT(FALSE);
		}
	}

	return dwPassword;
}

int CConfig::CheckParentPassword(const char *szCheckPassword)
{
	(void)szCheckPassword;
	return 1; // Desktop: always pass
}

void CConfig::SetParentPassword(const char *szNewPassword)
{
	(void)szNewPassword;
}

int CConfig::GetGamePCFlags()
{
	return 0; // No restriction
}

void CConfig::SetGamePCFlags(int nFlags)
{
	(void)nFlags;
}

int CConfig::GetMoviePCFlags()
{
	return 0; // No restriction
}

void CConfig::SetMoviePCFlags(int nFlags)
{
	(void)nFlags;
}

// ============================================================================
// Launch Data (Desktop: no kernel launch data)
// ============================================================================

CStrObject *CConfig::GetLaunchReason()
{
	return new CStrObject("");
}

int CConfig::GetLaunchContext()
{
	return g_bHasLaunchData ? g_dwLaunchContext : 0;
}

int CConfig::GetLaunchParameter1()
{
	return g_bHasLaunchData ? g_dwLaunchParameter1 : 0;
}

int CConfig::GetLaunchParameter2()
{
	return g_bHasLaunchData ? g_dwLaunchParameter2 : 0;
}

int CConfig::CanDriveBeCleanup(int Drive)
{
	switch (toupper(Drive))
	{
	case 'T':
	case 'U':
	case 'H':
	case 'I':
	case 'J':
	case 'K':
	case 'L':
	case 'M':
	case 'N':
	case 'O':
		return true;

	default:
		return false;
	}
}

DWORD CConfig::GetTitleID()
{
	return g_bHasLaunchData ? g_dwTitleID : 0;
}

void CConfig::BackToLauncher()
{
	OutputDebugString("[Config] BackToLauncher() - no-op on desktop\n");
}

// ============================================================================
// XBE / File Queries
// ============================================================================

CStrObject *CConfig::GetXBETitleID(const char *path)
{
	char ansiPath[MAX_PATH];
	Ansi(ansiPath, path, countof(ansiPath));

	char xbePath[MAX_PATH];
	sprintf(xbePath, "%s\\default.xbe", ansiPath);

	CXBExecutable theXBE;
	theXBE.ReadFile(xbePath);
	unsigned long title_id = theXBE.m_ulTitleId;

	char buffer[11];
	sprintf(buffer, "%08X", title_id);
	CStrObject *title_id_string = new CStrObject(buffer);

	return title_id_string;
}

// Desktop: translates Xbox NT device paths to local xboxfs paths,
// with fallback to .uixshortcut files and virtual games database
int CConfig::NtFileExists(const char* FileName)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    char remappedPath[MAX_PATH];

    strncpy(remappedPath, FileName, MAX_PATH);

    // Convert NT device paths to DOS-style drive letters
    if (strncasecmp(FileName, "\\Device\\Harddisk0\\Partition2", strlen("\\Device\\Harddisk0\\Partition2")) == 0)
        sprintf(remappedPath, "C:\\%s", FileName + strlen("\\Device\\Harddisk0\\Partition2"));
    else if (strncasecmp(FileName, "\\Device\\Harddisk0\\Partition1", strlen("\\Device\\Harddisk0\\Partition1")) == 0)
        sprintf(remappedPath, "E:\\%s", FileName + strlen("\\Device\\Harddisk0\\Partition1"));
    else if (strncasecmp(FileName, "\\Device\\Harddisk0\\Partition6", strlen("\\Device\\Harddisk0\\Partition6")) == 0)
        sprintf(remappedPath, "F:\\%s", FileName + strlen("\\Device\\Harddisk0\\Partition6"));
    else if (strncasecmp(FileName, "\\Device\\Harddisk0\\Partition7", strlen("\\Device\\Harddisk0\\Partition7")) == 0)
        sprintf(remappedPath, "G:\\%s", FileName + strlen("\\Device\\Harddisk0\\Partition7"));
    else if (strncasecmp(FileName, "\\Device\\Harddisk0\\Partition8", strlen("\\Device\\Harddisk0\\Partition8")) == 0)
        sprintf(remappedPath, "H:\\%s", FileName + strlen("\\Device\\Harddisk0\\Partition8"));
    else if (strncasecmp(FileName, "\\Device\\Harddisk0\\Partition9", strlen("\\Device\\Harddisk0\\Partition9")) == 0)
        sprintf(remappedPath, "I:\\%s", FileName + strlen("\\Device\\Harddisk0\\Partition9"));

    char szFile[MAX_PATH];
    Ansi(szFile, remappedPath, MAX_PATH);

    // Translate Xbox path to local filesystem
    {
        const char* translated = XboxFS_TranslatePath(szFile);
        char szLocal[MAX_PATH];
        if (translated) {
            strncpy(szLocal, translated, MAX_PATH - 1);
            szLocal[MAX_PATH - 1] = 0;
        } else {
            strncpy(szLocal, szFile, MAX_PATH - 1);
            szLocal[MAX_PATH - 1] = 0;
        }
        for (char* p = szLocal; *p; p++) { if (*p == '\\') *p = '/'; }

        int result = (access(szLocal, F_OK) == 0) ? 1 : 0;

        // Desktop: also check for .uixshortcut so PC shortcuts appear as Xbox games
        if (!result) {
            char szShortcut[MAX_PATH];
            strncpy(szShortcut, szLocal, MAX_PATH - 1);
            szShortcut[MAX_PATH - 1] = 0;
            char* pXbe = strstr(szShortcut, "default.xbe");
            if (!pXbe) pXbe = strstr(szShortcut, "Default.xbe");
            if (!pXbe) pXbe = strstr(szShortcut, "DEFAULT.XBE");
            if (pXbe && (size_t)(pXbe - szShortcut) + 20 < MAX_PATH) {
                strcpy(pXbe, "default.uixshortcut");
                result = (access(szShortcut, F_OK) == 0) ? 1 : 0;
            }
        }

        // Desktop: check virtual games database (games.ini entries with no real files)
        if (!result) {
            char vgFolder[MAX_PATH];
            strncpy(vgFolder, szLocal, MAX_PATH - 1);
            vgFolder[MAX_PATH - 1] = 0;
            char* lastSl = strrchr(vgFolder, '/');
            if (lastSl) *lastSl = 0;
            extern int VGames_MatchFolder(const char*);
            if (VGames_MatchFolder(vgFolder) >= 0)
                result = 1;
        }

        return result;
    }
}

// ============================================================================
// System Info Strings
// ============================================================================

CStrObject *CConfig::GetRecoveryKey()
{
	return new CStrObject("N/A");
}

CStrObject *CConfig::GetROMVersion()
{
	return new CStrObject("1.00.5838.01");
}

CStrObject *CConfig::GetXdashVersion()
{
	return new CStrObject("1.00.5960.01");
}

CStrObject *CConfig::GetXboxIP()
{
	char ipaddr[32];
	XNetConfigStatus status;
	XNetGetConfigStatus(&status);
	DWORD a = status.ina;
	snprintf(ipaddr, countof(ipaddr), "%d.%d.%d.%d",
		a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF);
	return new CStrObject(ipaddr);
}

CStrObject *CConfig::GetEncoder()
{
	return new CStrObject("N/A (Desktop)");
}

CStrObject *CConfig::GetXBOXVersion()
{
	return new CStrObject("Desktop");
}

CStrObject *CConfig::GetMODCHIPVersion()
{
	return new CStrObject("N/A (Desktop)");
}

// ============================================================================
// Functions recovered via Ghidra from the 5960 retail dashboard binary.
// EEPROM flag layouts, bit masks, and return semantics all matched against
// the disassembly.
// ============================================================================

int CConfig::GetLiveToday()
{
    return 0;
}

void CConfig::SetLiveToday(int value)
{
	int bLiveToday = (value != 0);
    TRACE("[Config] SetLiveToday(%d) called (stubbed)\n", value);
}

int CConfig::GetAcceptedLegalInfo()
{
	return 1; // Always accepted on desktop
}

void CConfig::SetAcceptedLegalInfo(int value)
{
	// On retail: reads XC_MISC_FLAGS (0x11), sets/clears bit 3
}

void CConfig::BackToLauncher2()
{
	OutputDebugString("[Config] BackToLauncher2() - no-op on desktop\n");
}

void CConfig::GoToXOnlineDash()
{
	// Desktop: no XOnline dash to navigate to
}

int CConfig::GetFontVersion()
{
	return 0;
}

extern bool g_bMovingScreen;

void CConfig::ToggleNoisyCamera()
{
	g_bMovingScreen = !g_bMovingScreen;
	if (g_pSkinSettings)
		g_pSkinSettings->SetValue("Camera", "Noisy", g_bMovingScreen ? "true" : "false");
}

int CConfig::GetEthernetLinkStatus()
{
	return 1; // Always connected on desktop
}

int CConfig::FileExists(const char* FileName)
{
	return NtFileExists(FileName);
}
