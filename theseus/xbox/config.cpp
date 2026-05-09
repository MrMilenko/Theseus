// config.cpp: CConfig XAP node. Reads / writes the dashboard's
// system config (timezone, language, av mode, parental ratings,
// audio mode) through the Xbox EEPROM via XConfig. Decompiled from
// the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

#include "xlaunch.h"
#include "cryptkeys.h"
#include "av.h"
#include "timezone.h"
#include "locale_node.h"
#include "xkflash.h"
#include "xbe.h"
#include "xip_archive.h"
#include "network.h"
#include "config.h"
CConfig* theConfig;

CXBoxFlash *mpFlash;
extern void Material_Init(bool bReloadSkinXBX);

void InitSkin();

// Cached AV info in Direct3D. Must be cleared before toggling widescreen.
extern "C"
{
	extern DWORD D3D__AvInfo;
}

IMPLEMENT_NODE("Config", CConfig, CNode)

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
NODE_FUN_SV(GetXboxIP)
NODE_FUN_SV(GetEncoder)
NODE_FUN_SV(GetXBOXVersion)
NODE_FUN_SV(GetMODCHIPVersion)
NODE_FUN_SS(GetXBETitleID)
NODE_FUN_SV(TestTitleID)
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
#undef _FND_CLASS

CConfig::CConfig() { }
CConfig::~CConfig() { }

// =========================================================================
// EEPROM helpers: read / write Xbox configuration values.
// =========================================================================

static DWORD QueryVideoFlags()
{
	DWORD dwFlags, dwType;
	VERIFY(!XQueryValue(XC_VIDEO_FLAGS, &dwType, &dwFlags, 4, NULL));
	return dwFlags;
}

static void SetVideoFlags(DWORD dwFlags)
{
	VERIFY(!XSetValue(XC_VIDEO_FLAGS, REG_DWORD, (DWORD *)&dwFlags, 4));
}

static DWORD QueryAudioFlags()
{
	DWORD dwFlags, dwType;
	VERIFY(!XQueryValue(XC_AUDIO_FLAGS, &dwType, &dwFlags, 4, NULL));
	return dwFlags;
}

static void SetAudioFlags(DWORD dwFlags)
{
	VERIFY(!XSetValue(XC_AUDIO_FLAGS, REG_DWORD, (DWORD *)&dwFlags, 4));
}

static DWORD QueryMiscFlags()
{
	DWORD dwType, value = 0;
	XQueryValue(XC_MISC_FLAGS, &dwType, &value, sizeof(value), NULL);
	return value;
}

static void SetMiscFlags(DWORD value)
{
	XSetValue(XC_MISC_FLAGS, REG_DWORD, (DWORD *)&value, sizeof(value));
}

// =========================================================================
// Language
// =========================================================================

int CConfig::GetLanguage()
{
	DWORD nLanguage, dwType;
	VERIFY(!XQueryValue(XC_LANGUAGE, &dwType, &nLanguage, 4, NULL));

	if (nLanguage == 0 || nLanguage > 6)
	{
		TRACE(_T("\001Invalid language, default to English\n"));
		nLanguage = 1;
	}

	return (int)nLanguage;
}

void CConfig::SetLanguage(int nLanguage)
{
	VERIFY(!XSetValue(XC_LANGUAGE, REG_DWORD, (DWORD *)&nLanguage, 4));
}

// =========================================================================
// AV pack / region identification
// =========================================================================

CStrObject *CConfig::GetAVPackType()
{
	const TCHAR *sz = NULL;
	switch (XGetAVPack())
	{
	case XC_AV_PACK_STANDARD: sz = _T("STANDARD"); break;
	case XC_AV_PACK_SVIDEO:   sz = _T("SVIDEO");   break;
	case XC_AV_PACK_RFU:      sz = _T("RFU");      break;
	case XC_AV_PACK_SCART:    sz = _T("SCART");    break;
	case XC_AV_PACK_HDTV:     sz = _T("HDTV");     break;
	case XC_AV_PACK_VGA:      sz = _T("VGA");      break;
	}
	return new CStrObject(sz);
}

CStrObject *CConfig::GetAVRegion()
{
	const TCHAR *sz = NULL;
	switch (XGetVideoStandard())
	{
	case XC_VIDEO_STANDARD_NTSC_M: sz = _T("NTSC_M"); break;
	case XC_VIDEO_STANDARD_NTSC_J: sz = _T("NTSC_J"); break;
	case XC_VIDEO_STANDARD_PAL_I:  sz = _T("PAL_I");  break;
	case XC_VIDEO_STANDARD_PAL_M:  sz = _T("PAL_M");  break;
	}
	return new CStrObject(sz);
}

CStrObject *CConfig::GetGameRegion()
{
	const TCHAR *sz = NULL;
	switch (g_nCurRegion)
	{
	case XC_GAME_REGION_NA:           sz = _T("NA");           break;
	case XC_GAME_REGION_JAPAN:        sz = _T("JAPAN");        break;
	case XC_GAME_REGION_RESTOFWORLD:  sz = _T("RESTOFWORLD");  break;
	}
	return new CStrObject(sz);
}

// =========================================================================
// Video mode: normal / letterbox / widescreen.
// =========================================================================

int CConfig::GetVideoMode()
{
	DWORD dwFlags = QueryVideoFlags();
	if (dwFlags & AV_FLAGS_LETTERBOX)  return 1;
	if (dwFlags & AV_FLAGS_WIDESCREEN) return 2;
	return 0;
}

void CConfig::SetVideoMode(int nVideoMode)
{
	DWORD dwFlags = QueryVideoFlags();
	dwFlags &= ~(AV_FLAGS_WIDESCREEN | AV_FLAGS_LETTERBOX);

	if (nVideoMode == 1)
		dwFlags |= AV_FLAGS_LETTERBOX;
	else if (nVideoMode == 2)
		dwFlags |= AV_FLAGS_WIDESCREEN;

	SetVideoFlags(dwFlags);

	bool bWideScreen = nVideoMode == 2;
	if (g_bStretchWidescreen != bWideScreen)
	{
		if (bWideScreen)
			g_pp.Flags |= D3DPRESENTFLAG_WIDESCREEN;
		else
			g_pp.Flags &= ~D3DPRESENTFLAG_WIDESCREEN;

		g_bStretchWidescreen = bWideScreen;
		D3D__AvInfo = 0;
		TheseusGetD3DDev()->Reset(&g_pp);
	}

	g_bProjectionDirty = true;
}

// =========================================================================
// HDTV resolution support
// =========================================================================

int CConfig::Get480Support()  { return (QueryVideoFlags() & AV_FLAGS_HDTV_480p) != 0; }
int CConfig::Get720Support()  { return (QueryVideoFlags() & AV_FLAGS_HDTV_720p) != 0; }
int CConfig::Get1080Support() { return (QueryVideoFlags() & AV_FLAGS_HDTV_1080i) != 0; }
int CConfig::GetPAL60Support(){ return (QueryVideoFlags() & AV_FLAGS_60Hz) != 0; }

void CConfig::Set480Support(int bEnable)
{
	DWORD dwFlags = QueryVideoFlags();
	if (bEnable) dwFlags |= AV_FLAGS_HDTV_480p;
	else         dwFlags &= ~AV_FLAGS_HDTV_480p;
	SetVideoFlags(dwFlags);

	// If playing a video disc on HDTV and Macrovision is active, update the D3D present flags
	if (g_nDiscType == DISC_VIDEO && XGetAVPack() == XC_AV_PACK_HDTV && (XBOX_480P_MACROVISION_ENABLED & XboxHardwareInfo->Flags))
	{
		bool bUpdate = false;
		if ((g_pp.Flags & D3DPRESENTFLAG_PROGRESSIVE) && !bEnable)
		{
			g_pp.Flags &= ~D3DPRESENTFLAG_PROGRESSIVE;
			g_pp.Flags |= D3DPRESENTFLAG_INTERLACED;
			bUpdate = true;
		}
		else if (!(g_pp.Flags & D3DPRESENTFLAG_PROGRESSIVE) && bEnable)
		{
			g_pp.Flags |= D3DPRESENTFLAG_PROGRESSIVE;
			g_pp.Flags &= ~D3DPRESENTFLAG_INTERLACED;
			bUpdate = true;
		}

		if (bUpdate)
		{
			D3D__AvInfo = 0;
			TheseusGetD3DDev()->Reset(&g_pp);
		}
	}
}

void CConfig::Set720Support(int bEnable)
{
	DWORD dwFlags = QueryVideoFlags();
	if (bEnable) dwFlags |= AV_FLAGS_HDTV_720p;
	else         dwFlags &= ~AV_FLAGS_HDTV_720p;
	SetVideoFlags(dwFlags);
}

void CConfig::Set1080Support(int bEnable)
{
	DWORD dwFlags = QueryVideoFlags();
	if (bEnable) dwFlags |= AV_FLAGS_HDTV_1080i;
	else         dwFlags &= ~AV_FLAGS_HDTV_1080i;
	SetVideoFlags(dwFlags);
}

void CConfig::SetPAL60Support(int bEnable)
{
	DWORD dwFlags = QueryVideoFlags();
	if (bEnable) dwFlags |= AV_FLAGS_60Hz;
	else         dwFlags &= ~AV_FLAGS_60Hz;
	SetVideoFlags(dwFlags);
}

// =========================================================================
// Audio mode
// =========================================================================

int CConfig::GetAudioMode()
{
	switch (DSSPEAKER_BASIC(QueryAudioFlags()))
	{
	case DSSPEAKER_MONO:    return 0;
	case DSSPEAKER_STEREO:  return 1;
	default:                return 2;
	}
}

void CConfig::SetAudioMode(int nAudioMode)
{
	DWORD dwFlags = QueryAudioFlags();
	switch (nAudioMode)
	{
	case 0: dwFlags = DSSPEAKER_COMBINED(DSSPEAKER_MONO, dwFlags);      break;
	case 1: dwFlags = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, dwFlags);    break;
	case 2: dwFlags = DSSPEAKER_COMBINED(DSSPEAKER_SURROUND, dwFlags);  break;
	}
	SetAudioFlags(dwFlags);
}

int CConfig::GetDolbyDigitalSupport() { return (QueryAudioFlags() & DSSPEAKER_ENABLE_AC3) != 0; }

void CConfig::SetDolbyDigitalSupport(int bEnable)
{
	DWORD dwFlags = QueryAudioFlags();
	if (bEnable) dwFlags |= DSSPEAKER_ENABLE_AC3;
	else         dwFlags &= ~DSSPEAKER_ENABLE_AC3;
	SetAudioFlags(dwFlags);
}

int CConfig::GetDTSSupport() { return (QueryAudioFlags() & DSSPEAKER_ENABLE_DTS) != 0; }

void CConfig::SetDTSSupport(int bEnable)
{
	DWORD dwFlags = QueryAudioFlags();
	if (bEnable) dwFlags |= DSSPEAKER_ENABLE_DTS;
	else         dwFlags &= ~DSSPEAKER_ENABLE_DTS;
	SetAudioFlags(dwFlags);
}

// =========================================================================
// Auto power-off
// =========================================================================

int CConfig::GetAutoOff()
{
	BOOL bAutoOff;
	XAutoPowerDownGet(&bAutoOff);
	return bAutoOff;
}

void CConfig::SetAutoOff(int bAutoOff) { XAutoPowerDownSet(bAutoOff); }

// =========================================================================
// Skin
// =========================================================================

extern void ReloadSkin();
extern void FlushMeshCache();
void CConfig::ApplySkin() { ReloadSkin(); }
void CConfig::FlushMeshCache() { ::FlushMeshCache(); }

// =========================================================================
// Network
// =========================================================================

void CConfig::NetworkStartup()  { net::init(); }
void CConfig::NetworkShutdown() { net::shutdown(); }
void CConfig::NetworkReboot()   { net::restart(); }

// =========================================================================
// Power management and SMBus
// =========================================================================

void CConfig::PowerOff()    { HalInitiateShutdown(); }
void CConfig::Reset()       { HalReturnToFirmware(HalRebootRoutine); }
void CConfig::XBOXReset()   { HalWriteSMBusValue(0x20, 0x02, 0, 0x01); }
void CConfig::PowerCycle()  { HalWriteSMBusValue(0x20, 0x02, 0, 0x40); }

void CConfig::SetLED(int LEDMode)
{
	// LED register cycle bits: 0x01-0x08 = green, 0x10-0x80 = red
	static const UCHAR ledValues[] = {
		0x01, 0x02, 0x04, 0x08,  // green: cycle3, cycle2, cycle1, cycle0
		0x10, 0x20, 0x40, 0x80   // red:   cycle3, cycle2, cycle1, cycle0
	};

	UCHAR LED = (LEDMode >= 0 && LEDMode < 8) ? ledValues[LEDMode] : 0x08;
	HalWriteSMBusValue(0x20, 0x08, 0, LED);
	Sleep(10);
	HalWriteSMBusValue(0x20, 0x07, 0, 1);
}

int CConfig::GetFanSpeed()
{
	DWORD value = 0xCCCCCCCC;
	HalReadSMBusValue(0x21, 0x10, FALSE, &value);
	return (int)value;
}

void CConfig::SetFanSpeed(int speed)
{
	UCHAR temp = (UCHAR)(speed / 2);
	if (temp < 10) temp = 10;
	if (temp > 80) temp = 80;

	HalWriteSMBusValue(0x20, 0x06, 0, temp);
	Sleep(20);
	HalWriteSMBusValue(0x20, 0x05, 0, 1);
}

int CConfig::GetCPUTemp()
{
	ULONG val;
	HalReadSMBusValue(0x20, 0x09, FALSE, &val);
	return (int)val;
}

int CConfig::GetInternalTemp()
{
	ULONG val;
	HalReadSMBusValue(0x20, 0x0A, FALSE, &val);
	return (int)val;
}

// =========================================================================
// Timezone / DST
// =========================================================================

extern int GetTimeZoneIndex(const TIME_ZONE_INFORMATION *tzinfo);
extern bool GetTimeZoneInfo(int index, TIME_ZONE_INFORMATION *tzinfo);

int CConfig::GetTimeZone()
{
	TIME_ZONE_INFORMATION tzinfo;
	if ((XapipQueryTimeZoneInformation(&tzinfo, NULL) != ERROR_SUCCESS) ||
		(L'\0' == tzinfo.StandardName[0]))
	{
		// No valid timezone; return a default based on game region.
		switch (XGetGameRegion())
		{
		case XC_GAME_REGION_NA:    return NA_DEFAULT_TIMEZONE;
		case XC_GAME_REGION_JAPAN: return JAPAN_DEFAULT_TIMEZONE;
		default:                   return ROW_DEFAULT_TIMEZONE;
		}
	}

	int index = GetTimeZoneIndex(&tzinfo);
	return (index < 0) ? 0 : index;
}

void CConfig::SetTimeZone(int nTimeZone)
{
	TIME_ZONE_INFORMATION tzinfo;
	if (GetTimeZoneInfo(nTimeZone, &tzinfo))
		XapipSetTimeZoneInformation(&tzinfo);
}

int CConfig::GetDSTAllowed()
{
	TIME_ZONE_INFORMATION tzinfo;
	if (XapipQueryTimeZoneInformation(&tzinfo, NULL) != ERROR_SUCCESS)
		return 0;
	return (tzinfo.StandardDate.wMonth && tzinfo.DaylightDate.wMonth);
}

int CConfig::GetDST()
{
	TIME_ZONE_INFORMATION tzinfo;
	BOOL fUseDST;
	if (XapipQueryTimeZoneInformation(&tzinfo, &fUseDST) != ERROR_SUCCESS)
		return 0;
	return (tzinfo.StandardDate.wMonth && tzinfo.DaylightDate.wMonth && fUseDST);
}

void CConfig::SetDST(int bObserveDST)
{
#if DBG
	if (bObserveDST)
		ASSERT(GetDSTAllowed());
#endif

	ULONG type, size;
	DWORD flags;
	if (XQueryValue(XC_MISC_FLAGS, &type, &flags, sizeof(flags), &size) == ERROR_SUCCESS)
	{
		if (bObserveDST)
			flags &= ~XC_MISC_FLAG_DONT_USE_DST;
		else
			flags |= XC_MISC_FLAG_DONT_USE_DST;
		XSetValue(XC_MISC_FLAGS, REG_DWORD, &flags, sizeof(flags));
	}
}

// =========================================================================
// Parental controls
// =========================================================================

static DWORD EncodePassword(const TCHAR *szPassword)
{
	DWORD dw = 0;
	for (const TCHAR *pch = szPassword; *pch != 0; pch += 1)
	{
		dw <<= 4;
		switch (*pch)
		{
		case 'u': dw += 1;  break;  // up
		case 'd': dw += 2;  break;  // down
		case 'l': dw += 3;  break;  // left
		case 'r': dw += 4;  break;  // right
		case 'a': dw += 5;  break;  // A
		case 'b': dw += 6;  break;  // B
		case 'x': dw += 7;  break;  // X
		case 'y': dw += 8;  break;  // Y
		case 'B': dw += 9;  break;  // black
		case 'W': dw += 10; break;  // white
		case 'L': dw += 11; break;  // L trigger
		case 'R': dw += 12; break;  // R trigger
		default:  ASSERT(FALSE);
		}
	}
	return dw;
}

int CConfig::CheckParentPassword(const TCHAR *szCheckPassword)
{
	DWORD dwCheck = EncodePassword(szCheckPassword);
	DWORD dwStored, dwType;
	VERIFY(!XQueryValue(XC_PARENTAL_CONTROL_PASSWORD, &dwType, &dwStored, 4, NULL));
	return dwStored == dwCheck;
}

void CConfig::SetParentPassword(const TCHAR *szNewPassword)
{
	DWORD dw = EncodePassword(szNewPassword);
	VERIFY(!XSetValue(XC_PARENTAL_CONTROL_PASSWORD, REG_DWORD, &dw, 4));
}

int CConfig::GetGamePCFlags()
{
	DWORD dwFlags = 0, dwType;
	VERIFY(!XQueryValue(XC_PARENTAL_CONTROL_GAMES, &dwType, &dwFlags, 4, NULL));
	ASSERT(dwFlags <= 6);
	return (int)(6 - dwFlags); // stored inverted in EEPROM
}

void CConfig::SetGamePCFlags(int nFlags)
{
	ASSERT(nFlags <= 6);
	nFlags = 6 - nFlags;
	VERIFY(!XSetValue(XC_PARENTAL_CONTROL_GAMES, REG_DWORD, (DWORD *)&nFlags, 4));
}

int CConfig::GetMoviePCFlags()
{
	DWORD dwFlags = 0, dwType;
	VERIFY(!XQueryValue(XC_PARENTAL_CONTROL_MOVIES, &dwType, &dwFlags, 4, NULL));
	ASSERT(dwFlags <= 7);
	return (int)(7 - dwFlags);
}

void CConfig::SetMoviePCFlags(int nFlags)
{
	ASSERT(nFlags <= 7);
	nFlags = 7 - nFlags;
	VERIFY(!XSetValue(XC_PARENTAL_CONTROL_MOVIES, REG_DWORD, (DWORD *)&nFlags, 4));
}

// =========================================================================
// Launch data
// =========================================================================

CStrObject *CConfig::GetLaunchReason()
{
	const TCHAR *sz = NULL;
	if (g_bHasLaunchData)
	{
		switch (g_dwLaunchReason)
		{
		case XLD_LAUNCH_DASHBOARD_ERROR:                  sz = _T("Error");                    break;
		case XLD_LAUNCH_DASHBOARD_MEMORY:                 sz = _T("Memory");                   break;
		case XLD_LAUNCH_DASHBOARD_SETTINGS:               sz = _T("Settings");                 break;
		case XLD_LAUNCH_DASHBOARD_MUSIC:                  sz = _T("Music");                    break;
		case XLD_LAUNCH_DASHBOARD_ONLINE_MENU:            sz = _T("FromOnlineDash");           break;
		case XLD_LAUNCH_DASHBOARD_NEW_ACCOUNT_SIGNUP:     sz = _T("NewAccountInternal");       break;
		case XLD_LAUNCH_DASHBOARD_NETWORK_CONFIGURATION:  sz = _T("NetworkSettingsInternal");  break;
		default:
			TRACE(_T("[LaunchReason] Unknown launch reason: %lu\n"), g_dwLaunchReason);
			break;
		}
	}
	return new CStrObject(sz ? sz : _T(""));
}

int CConfig::GetLaunchContext()    { return g_bHasLaunchData ? g_dwLaunchContext : 0; }
int CConfig::GetLaunchParameter1() { return g_bHasLaunchData ? g_dwLaunchParameter1 : 0; }
int CConfig::GetLaunchParameter2() { return g_bHasLaunchData ? g_dwLaunchParameter2 : 0; }
DWORD CConfig::GetTitleID()        { return g_bHasLaunchData ? g_dwTitleID : 0; }

int CConfig::CanDriveBeCleanup(int Drive)
{
	switch (toupper(Drive))
	{
	case 'T': case 'U':
	case 'H': case 'I': case 'J': case 'K':
	case 'L': case 'M': case 'N': case 'O':
		return true;
	default:
		return false;
	}
}

void CConfig::BackToLauncher()
{
	TheseusGetD3DDev()->PersistDisplay();

	if (CheckForcedSettings(XLD_SETTINGS_CLOCK | XLD_SETTINGS_TIMEZONE | XLD_SETTINGS_LANGUAGE))
	{
		HalReturnToFirmware(HalRebootRoutine);
	}
	else
	{
		LD_FROM_DASHBOARD fd;
		ZeroMemory(&fd, sizeof(fd));
		fd.dwContext = GetLaunchContext();
		XWriteTitleInfoAndReboot("default.xbe", "\\Device\\CdRom0", LDT_FROM_DASHBOARD, GetTitleID(), (PLAUNCH_DATA)&fd);
	}
}

// =========================================================================
// XBE title ID
// =========================================================================

CStrObject *CConfig::TestTitleID()
{
	const TCHAR *path = _T("F:\\Games\\Grand Theft Auto III (USA)");
	CStrObject *result = GetXBETitleID(path);
	return result ? result : new CStrObject(_T("00000000"));
}

CStrObject *CConfig::GetXBETitleID(const TCHAR *path)
{
	char ansiPath[MAX_PATH];
	Ansi(ansiPath, path, countof(ansiPath));

	char xbePath[MAX_PATH];
	sprintf(xbePath, "%s\\default.xbe", ansiPath);

	CXBExecutable theXBE;
	theXBE.ReadFile(xbePath);

	wchar_t buffer[11];
	swprintf(buffer, _T("%08X"), theXBE.m_ulTitleId);
	return new CStrObject(buffer);
}

// =========================================================================
// NT device path file check
// =========================================================================

int CConfig::NtFileExists(const TCHAR* FileName)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	TCHAR remappedPath[MAX_PATH];

	_tcsncpy(remappedPath, FileName, MAX_PATH);

	// Convert NT device paths to DOS drive letters
	if (_tcsnicmp(FileName, _T("\\Device\\Harddisk0\\Partition2"), _tcslen(_T("\\Device\\Harddisk0\\Partition2"))) == 0)
		_stprintf(remappedPath, _T("C:\%s"), FileName + _tcslen(_T("\\Device\\Harddisk0\\Partition2")));
	else if (_tcsnicmp(FileName, _T("\\Device\\Harddisk0\\Partition1"), _tcslen(_T("\\Device\\Harddisk0\\Partition1"))) == 0)
		_stprintf(remappedPath, _T("E:\%s"), FileName + _tcslen(_T("\\Device\\Harddisk0\\Partition1")));
	else if (_tcsnicmp(FileName, _T("\\Device\\Harddisk0\\Partition6"), _tcslen(_T("\\Device\\Harddisk0\\Partition6"))) == 0)
		_stprintf(remappedPath, _T("F:\%s"), FileName + _tcslen(_T("\\Device\\Harddisk0\\Partition6")));
	else if (_tcsnicmp(FileName, _T("\\Device\\Harddisk0\\Partition7"), _tcslen(_T("\\Device\\Harddisk0\\Partition7"))) == 0)
		_stprintf(remappedPath, _T("G:\%s"), FileName + _tcslen(_T("\\Device\\Harddisk0\\Partition7")));
	else if (_tcsnicmp(FileName, _T("\\Device\\Harddisk0\\Partition8"), _tcslen(_T("\\Device\\Harddisk0\\Partition8"))) == 0)
		_stprintf(remappedPath, _T("H:\%s"), FileName + _tcslen(_T("\\Device\\Harddisk0\\Partition8")));
	else if (_tcsnicmp(FileName, _T("\\Device\\Harddisk0\\Partition9"), _tcslen(_T("\\Device\\Harddisk0\\Partition9"))) == 0)
		_stprintf(remappedPath, _T("I:\%s"), FileName + _tcslen(_T("\\Device\\Harddisk0\\Partition9")));

	CHAR szFile[MAX_PATH];
	Ansi(szFile, remappedPath, MAX_PATH);

	return (GetFileAttributesEx(szFile, GetFileExInfoStandard, &fad) > 0) ? 1 : 0;
}

int CConfig::FileExists(const TCHAR* FileName) { return NtFileExists(FileName); }

// =========================================================================
// System info
// =========================================================================

CStrObject *CConfig::GetRecoveryKey()
{
	CHAR RecoveryKey[RECOVERY_KEY_LEN];
	ComputeRecoveryKey((LPBYTE)XboxHDKey, RecoveryKey);
#ifdef _UNICODE
	TCHAR RecKey[RECOVERY_KEY_LEN];
	Unicode(RecKey, RecoveryKey, RECOVERY_KEY_LEN);
	return new CStrObject(RecKey);
#else
	return new CStrObject(RecoveryKey);
#endif
}

CStrObject *CConfig::GetROMVersion()
{
	TCHAR buf[64];
	_sntprintf(buf, countof(buf), _T("%d.%02d.%d.%02d"),
		XboxKrnlVersion->Major, XboxKrnlVersion->Minor,
		XboxKrnlVersion->Build, (XboxKrnlVersion->Qfe & 0x7FFF));
	return new CStrObject(buf);
}

CStrObject *CConfig::GetXdashVersion()
{
	return new CStrObject(_T("1.00.5960.01"));
}

CStrObject *CConfig::GetXboxIP()
{
	TCHAR buf[32];
	XNetGetTitleXnAddr(&xnaddr);
	_sntprintf(buf, countof(buf), _T("%d.%d.%d.%d"),
		xnaddr.ina.S_un.S_un_b.s_b1, xnaddr.ina.S_un.S_un_b.s_b2,
		xnaddr.ina.S_un.S_un_b.s_b3, xnaddr.ina.S_un.S_un_b.s_b4);
	return new CStrObject(buf);
}

CStrObject *CConfig::GetEncoder()
{
	ULONG iType;
	if (HalReadSMBusValue(0x8a, 0x00, 0, &iType) == 0)
		return new CStrObject(_T("Conexant"));
	if (HalReadSMBusValue(0xd4, 0x00, 0, &iType) == 0)
		return new CStrObject(_T("Focus"));
	if (HalReadSMBusValue(0xe0, 0x00, 0, &iType) == 0)
		return new CStrObject(_T("Xcalibur"));
	return new CStrObject(_T("Unknown Encoder"));
}

CStrObject *CConfig::GetXBOXVersion()
{
	char szVer[50];
	HalReadSMBusValue(0x20, 0x01, 0, (ULONG *)&szVer[0]);
	HalReadSMBusValue(0x20, 0x01, 0, (ULONG *)&szVer[1]);
	HalReadSMBusValue(0x20, 0x01, 0, (ULONG *)&szVer[2]);
	szVer[3] = '\0';

	struct { const char* code; const TCHAR* version; } revisions[] = {
		{ "DBG", _T("Debug Kit") },
		{ "01D", _T("Devkit") },
		{ "D01", _T("Devkit") },
		{ "1D0", _T("Devkit") },
		{ "0D1", _T("Devkit") },
		{ "P01", _T("1.0") },
		{ "P05", _T("1.1") },
		{ "P2L", _T("1.6") },
	};

	for (int i = 0; i < countof(revisions); i++)
	{
		if (strcmp(szVer, revisions[i].code) == 0)
			return new CStrObject(revisions[i].version);
	}

	// P11 needs encoder check to distinguish 1.2/1.3 from 1.4
	if (strcmp(szVer, "P11") == 0)
	{
		ULONG iType;
		if (HalReadSMBusValue(0x8a, 0x00, 0, &iType) == 0)
			return new CStrObject(_T("1.2/1.3"));
		return new CStrObject(_T("1.4"));
	}

	return new CStrObject(_T("Unknown"));
}

// =========================================================================
// Modchip flash detection
// =========================================================================

CStrObject *CConfig::GetMODCHIPVersion()
{
	OutputDebugStringA("[MODCHIP] Initializing flash detection...\n");

	TCHAR result[MAX_PATH];
	mpFlash = new CXBoxFlash;

	OutputDebugStringA("[MODCHIP] Adding known flash chips...\n");

	// Unknown or TSOP
	mpFlash->AddFCI(0x09, 0x00, "Unknown/Locked", 0x00000);

	// Known modchip IDs
	mpFlash->AddFCI(0x01, 0xAD, "XECUTER 3", 0x100000);
	mpFlash->AddFCI(0x01, 0xD5, "XECUTER 2", 0x100000);
	mpFlash->AddFCI(0x01, 0xC4, "XENIUM", 0x100000);
	mpFlash->AddFCI(0x01, 0xC4, "XENIUM", 0x000000);
	mpFlash->AddFCI(0x04, 0xBA, "ALX2+ R3 FLASH", 0x40000);

	// AMD
	mpFlash->AddFCI(0x01, 0xb0, "AMD Am29F002BT/NBT", 0x40000);
	mpFlash->AddFCI(0x01, 0x34, "AMD Am29F002BB/NBB", 0x40000);
	mpFlash->AddFCI(0x01, 0x51, "AMD Am29F200BT", 0x40000);
	mpFlash->AddFCI(0x01, 0x57, "AMD Am29F200BB", 0x40000);
	mpFlash->AddFCI(0x01, 0x40, "AMD Am29LV002BT", 0x40000);
	mpFlash->AddFCI(0x01, 0xc2, "AMD Am29LV002BB", 0x40000);
	mpFlash->AddFCI(0x01, 0x3b, "AMD Am29LV200BT", 0x40000);
	mpFlash->AddFCI(0x01, 0xbf, "AMD Am29LV200BB", 0x40000);
	mpFlash->AddFCI(0x01, 0x0c, "AMD Am29DL400BT", 0x80000);
	mpFlash->AddFCI(0x01, 0x0f, "AMD Am29DL400BB", 0x80000);
	mpFlash->AddFCI(0x01, 0x77, "AMD Am29F004BT", 0x80000);
	mpFlash->AddFCI(0x01, 0x7b, "AMD Am29F004BB", 0x80000);
	mpFlash->AddFCI(0x01, 0xa4, "AMD Am29F040B", 0x80000);
	mpFlash->AddFCI(0x01, 0x23, "AMD Am29F400BT", 0x80000);
	mpFlash->AddFCI(0x01, 0xab, "AMD Am29F400BB", 0x80000);
	mpFlash->AddFCI(0x01, 0xb5, "AMD Am29LV004BT", 0x80000);
	mpFlash->AddFCI(0x01, 0xb6, "AMD Am29LV004BB", 0x80000);
	mpFlash->AddFCI(0x01, 0x4f, "AMD Am29LV040B", 0x80000);
	mpFlash->AddFCI(0x01, 0xb9, "AMD Am29LV400BT", 0x80000);
	mpFlash->AddFCI(0x01, 0xba, "AMD Am29LV400BB", 0x80000);
	mpFlash->AddFCI(0x01, 0x4a, "AMD Am29DL800BT", 0x100000);
	mpFlash->AddFCI(0x01, 0xcb, "AMD Am29DL800BB", 0x100000);
	mpFlash->AddFCI(0x01, 0xd5, "AMD Am29F080B", 0x100000);
	mpFlash->AddFCI(0x01, 0xd6, "AMD Am29F800BT", 0x100000);
	mpFlash->AddFCI(0x01, 0x58, "AMD Am29F800BB", 0x100000);
	mpFlash->AddFCI(0x01, 0x3e, "AMD Am29LV008BT", 0x100000);
	mpFlash->AddFCI(0x01, 0x37, "AMD Am29LV008BB", 0x100000);
	mpFlash->AddFCI(0x01, 0x38, "AMD Am29LV080B", 0x100000);
	mpFlash->AddFCI(0x01, 0xda, "AMD Am29LV800BT/DT", 0x100000);
	mpFlash->AddFCI(0x01, 0x5b, "AMD Am29LV800BB/DB", 0x100000);

	// AMIC
	mpFlash->AddFCI(0x37, 0x8c, "AMIC A29002T/290021T", 0x40000);
	mpFlash->AddFCI(0x37, 0x0d, "AMIC A29002U/290021U", 0x40000);
	mpFlash->AddFCI(0x37, 0x86, "AMIC A29040A", 0x80000);
	mpFlash->AddFCI(0x37, 0xb0, "AMIC A29400T/294001T", 0x80000);
	mpFlash->AddFCI(0x37, 0x31, "AMIC A29400U/294001U", 0x80000);
	mpFlash->AddFCI(0x37, 0x34, "AMIC A29L004T/A29L400T", 0x80000);
	mpFlash->AddFCI(0x37, 0xb5, "AMIC A29L004U/A29L400U", 0x80000);
	mpFlash->AddFCI(0x37, 0x92, "AMIC A29L040", 0x80000);
	mpFlash->AddFCI(0x37, 0x0e, "AMIC A29800T", 0x100000);
	mpFlash->AddFCI(0x37, 0x8f, "AMIC A29800U", 0x100000);
	mpFlash->AddFCI(0x37, 0x1a, "AMIC A29L008T/A29L800T", 0x100000);
	mpFlash->AddFCI(0x37, 0x9b, "AMIC A29L008U/A29L800U", 0x100000);

	// Atmel
	mpFlash->AddFCI(0x1f, 0x07, "Atmel AT49F002A", 0x40000);
	mpFlash->AddFCI(0x1f, 0x08, "Atmel AT49F002AT", 0x40000);

	// Fujitsu
	mpFlash->AddFCI(0x04, 0xb0, "Fujitsu MBM29F002TC", 0x40000);
	mpFlash->AddFCI(0x04, 0x34, "Fujitsu MBM29F002BC", 0x40000);
	mpFlash->AddFCI(0x04, 0x51, "Fujitsu MBM29F200TC", 0x40000);
	mpFlash->AddFCI(0x04, 0x57, "Fujitsu MBM29F200BC", 0x40000);
	mpFlash->AddFCI(0x04, 0x40, "Fujitsu MBM29LV002TC", 0x40000);
	mpFlash->AddFCI(0x04, 0xc2, "Fujitsu MBM29LV002BC", 0x40000);
	mpFlash->AddFCI(0x04, 0x3b, "Fujitsu MBM29LV200TC", 0x40000);
	mpFlash->AddFCI(0x04, 0xbf, "Fujitsu MBM29LV200BC", 0x40000);
	mpFlash->AddFCI(0x04, 0x0c, "Fujitsu MBM29DL400TC", 0x80000);
	mpFlash->AddFCI(0x04, 0x0f, "Fujitsu MBM29DL400BC", 0x80000);
	mpFlash->AddFCI(0x04, 0x77, "Fujitsu MBM29F004TC", 0x80000);
	mpFlash->AddFCI(0x04, 0x7b, "Fujitsu MBM29F004BC", 0x80000);
	mpFlash->AddFCI(0x04, 0xa4, "Fujitsu MBM29F040C", 0x80000);
	mpFlash->AddFCI(0x04, 0x23, "Fujitsu MBM29F400TC", 0x80000);
	mpFlash->AddFCI(0x04, 0xab, "Fujitsu MBM29F400BC", 0x80000);
	mpFlash->AddFCI(0x04, 0xb5, "Fujitsu MBM29LV004TC", 0x80000);
	mpFlash->AddFCI(0x04, 0xb6, "Fujitsu MBM29LV004BC", 0x80000);
	mpFlash->AddFCI(0x04, 0xb9, "Fujitsu MBM29LV400TC", 0x80000);
	mpFlash->AddFCI(0x04, 0xba, "Fujitsu MBM29LV400BC", 0x80000);
	mpFlash->AddFCI(0x04, 0x4a, "Fujitsu MBM29DL800TA", 0x100000);
	mpFlash->AddFCI(0x04, 0xcb, "Fujitsu MBM29DL800BA", 0x100000);
	mpFlash->AddFCI(0x04, 0xd5, "Fujitsu MBM29F080A", 0x100000);
	mpFlash->AddFCI(0x04, 0xd6, "Fujitsu MBM29F800TA", 0x100000);
	mpFlash->AddFCI(0x04, 0x58, "Fujitsu MBM29F800BA", 0x100000);
	mpFlash->AddFCI(0x04, 0x3e, "Fujitsu MBM29LV008TA", 0x100000);
	mpFlash->AddFCI(0x04, 0x37, "Fujitsu MBM29LV008BA", 0x100000);
	mpFlash->AddFCI(0x04, 0x38, "Fujitsu MBM29LV080A", 0x100000);
	mpFlash->AddFCI(0x04, 0xda, "Fujitsu MBM29LV800TA/TE", 0x100000);
	mpFlash->AddFCI(0x04, 0x5b, "Fujitsu MBM29LV800BA/BE", 0x100000);

	// Hynix
	mpFlash->AddFCI(0xad, 0xb0, "Hynix HY29F002", 0x40000);
	mpFlash->AddFCI(0xad, 0xa4, "Hynix HY29F040A", 0x80000);
	mpFlash->AddFCI(0xad, 0x23, "Hynix HY29F400T/AT", 0x80000);
	mpFlash->AddFCI(0xad, 0xab, "Hynix HY29F400B/AB", 0x80000);
	mpFlash->AddFCI(0xad, 0xb9, "Hynix HY29LV400T", 0x80000);
	mpFlash->AddFCI(0xad, 0xba, "Hynix HY29LV400B", 0x80000);
	mpFlash->AddFCI(0xad, 0xd5, "Hynix HY29F080", 0x100000);
	mpFlash->AddFCI(0xad, 0xd6, "Hynix HY29F800T/AT", 0x100000);
	mpFlash->AddFCI(0xad, 0x58, "Hynix HY29F800B/AB", 0x100000);
	mpFlash->AddFCI(0xad, 0xda, "Hynix HY29LV800T", 0x100000);
	mpFlash->AddFCI(0xad, 0x5b, "Hynix HY29LV800B", 0x100000);

	// Macronix
	mpFlash->AddFCI(0xc2, 0xb0, "Macronix MX29F002T/NT", 0x40000);
	mpFlash->AddFCI(0xc2, 0x34, "Macronix MX29F002B/NB", 0x40000);
	mpFlash->AddFCI(0xc2, 0x36, "Macronix MX29F022T/NT", 0x40000);
	mpFlash->AddFCI(0xc2, 0x37, "Macronix MX29F022B/NB", 0x40000);
	mpFlash->AddFCI(0xc2, 0x51, "Macronix MX29F200T", 0x40000);
	mpFlash->AddFCI(0xc2, 0x57, "Macronix MX29F200B", 0x40000);
	mpFlash->AddFCI(0xc2, 0x45, "Macronix MX29F004T", 0x80000);
	mpFlash->AddFCI(0xc2, 0x46, "Macronix MX29F004B", 0x80000);
	mpFlash->AddFCI(0xc2, 0xa4, "Macronix MX29F040", 0x80000);
	mpFlash->AddFCI(0xc2, 0x23, "Macronix MX29F400T", 0x80000);
	mpFlash->AddFCI(0xc2, 0xab, "Macronix MX29F400B", 0x80000);
	mpFlash->AddFCI(0xc2, 0xb5, "Macronix MX29LV004T", 0x80000);
	mpFlash->AddFCI(0xc2, 0xb6, "Macronix MX29LV004B", 0x80000);
	mpFlash->AddFCI(0xc2, 0x4f, "Macronix MX29LV040", 0x80000);
	mpFlash->AddFCI(0xc2, 0xb9, "Macronix MX29LV400T", 0x80000);
	mpFlash->AddFCI(0xc2, 0xba, "Macronix MX29LV400B", 0x80000);
	mpFlash->AddFCI(0xc2, 0xd5, "Macronix MX29F080", 0x100000);
	mpFlash->AddFCI(0xc2, 0xd6, "Macronix MX29F800T", 0x100000);
	mpFlash->AddFCI(0xc2, 0x58, "Macronix MX29F800B", 0x100000);
	mpFlash->AddFCI(0xc2, 0x3e, "Macronix MX29LV008T", 0x100000);
	mpFlash->AddFCI(0xc2, 0x37, "Macronix MX29LV008B", 0x100000);
	mpFlash->AddFCI(0xc2, 0x38, "Macronix MX29LV081", 0x100000);
	mpFlash->AddFCI(0xc2, 0xda, "Macronix MX29LV800T", 0x100000);
	mpFlash->AddFCI(0xc2, 0x5b, "Macronix MX29LV800B", 0x100000);

	// Sharp
	mpFlash->AddFCI(0xb0, 0xc9, "Sharp LHF00L02/L06/L07", 0x100000);
	mpFlash->AddFCI(0xb0, 0xcf, "Sharp LHF00L03/L04/L05", 0x100000);
	mpFlash->AddFCI(0x89, 0xa2, "Sharp LH28F008SA series", 0x100000);
	mpFlash->AddFCI(0x89, 0xa6, "Sharp LH28F008SC series", 0x100000);
	mpFlash->AddFCI(0xb0, 0xec, "Sharp LH28F008BJxx-PT series", 0x100000);
	mpFlash->AddFCI(0xb0, 0xed, "Sharp LH28F008BJxx-PB series", 0x100000);
	mpFlash->AddFCI(0xb0, 0x4b, "Sharp LH28F800BVxx-BTL series", 0x100000);
	mpFlash->AddFCI(0xb0, 0x4c, "Sharp LH28F800BVxx-TV series", 0x100000);
	mpFlash->AddFCI(0xb0, 0x4d, "Sharp LH28F800BVxx-BV series", 0x100000);

	// SST
	mpFlash->AddFCI(0xbf, 0x10, "SST 29EE020", 0x40000);
	mpFlash->AddFCI(0xbf, 0x12, "SST 29LE020/29VE020", 0x40000);
	mpFlash->AddFCI(0xbf, 0xd6, "SST 39LF020/39VF020", 0x40000);
	mpFlash->AddFCI(0xbf, 0xb6, "SST 39SF020A", 0x40000);
	mpFlash->AddFCI(0xbf, 0x57, "SST 49LF002A", 0x40000);
	mpFlash->AddFCI(0xbf, 0x57, "SST 49LF002A", 0x100000);
	mpFlash->AddFCI(0xbf, 0x52, "SST 49LF020A", 0x40000);
	mpFlash->AddFCI(0xbf, 0x1b, "SST 49LF003A", 0x60000);
	mpFlash->AddFCI(0xbf, 0x1c, "SST 49LF030A", 0x60000);
	mpFlash->AddFCI(0xbf, 0x61, "SST 49LF020", 0x40000);
	mpFlash->AddFCI(0xbf, 0x13, "SST 29SF040", 0x80000);
	mpFlash->AddFCI(0xbf, 0x14, "SST 29VF040", 0x80000);
	mpFlash->AddFCI(0xbf, 0xd7, "SST 39LF040/39VF040", 0x80000);
	mpFlash->AddFCI(0xbf, 0xb7, "SST 39SF040", 0x80000);
	mpFlash->AddFCI(0xbf, 0x60, "SST 49LF004A/B", 0x80000);
	mpFlash->AddFCI(0xbf, 0x51, "SST 49LF040", 0x80000);
	mpFlash->AddFCI(0xbf, 0xd8, "SST 39LF080/39VF080/39VF088", 0x100000);
	mpFlash->AddFCI(0xbf, 0x5a, "SST 49LF008A", 0x100000);
	mpFlash->AddFCI(0xbf, 0x5b, "SST 49LF080A", 0x100000);

	// ST
	mpFlash->AddFCI(0x20, 0xb0, "ST M29F002T/NT/BT/BNT", 0x40000);
	mpFlash->AddFCI(0x20, 0x34, "ST M29F002B/BB", 0x40000);
	mpFlash->AddFCI(0x20, 0xd3, "ST M29F200BT", 0x40000);
	mpFlash->AddFCI(0x20, 0xd4, "ST M29F200BB", 0x40000);
	mpFlash->AddFCI(0x20, 0xe2, "ST M29F040 series", 0x80000);
	mpFlash->AddFCI(0x20, 0xd5, "ST M29F400T/BT", 0x80000);
	mpFlash->AddFCI(0x20, 0xd6, "ST M29F400B/BB", 0x80000);
	mpFlash->AddFCI(0x20, 0xf1, "ST M29F080 series", 0x100000);
	mpFlash->AddFCI(0x20, 0xec, "ST M29F800DT", 0x100000);
	mpFlash->AddFCI(0x20, 0x58, "ST M29F800DB", 0x100000);

	// Winbond
	mpFlash->AddFCI(0xda, 0x45, "Winbond W29C020", 0x40000);
	mpFlash->AddFCI(0x09, 0x00, "Winbond W49F020T", 0x40000);
	mpFlash->AddFCI(0xda, 0xb5, "Winbond W39L020", 0x40000);
	mpFlash->AddFCI(0xda, 0x0b, "Winbond W49F002U", 0x40000);
	mpFlash->AddFCI(0xda, 0x8c, "Winbond W49F020", 0x40000);
	mpFlash->AddFCI(0xda, 0xb0, "Winbond W49V002A", 0x40000);
	mpFlash->AddFCI(0xda, 0x46, "Winbond W29C040", 0x40000);
	mpFlash->AddFCI(0xda, 0xb6, "Winbond W39L040", 0x80000);
	mpFlash->AddFCI(0xda, 0x3d, "Winbond W39V040A", 0x80000);

	OutputDebugStringA("[MODCHIP] Running CheckID2...\n");

	if (mpFlash->CheckID2() != 0)
	{
		const char* chipName = mpFlash->CheckID2()->text;
		char dbg[128];
		sprintf(dbg, "[MODCHIP] Detected via CheckID2: %s\n", chipName);
		OutputDebugStringA(dbg);
		swprintf((WCHAR *)result, _T(" %S"), chipName);
	}
	else if (mpFlash->CheckID() != 0)
	{
		const char* chipName = mpFlash->CheckID()->text;
		char dbg[128];
		sprintf(dbg, "[MODCHIP] Detected via CheckID: %s\n", chipName);
		OutputDebugStringA(dbg);
		swprintf((WCHAR *)result, _T(" %S"), chipName);
	}
	else
	{
		OutputDebugStringA("[MODCHIP] No chip detected. Returning 'Unknown'\n");
		swprintf((WCHAR *)result, _T("Unknown"));
	}

	delete mpFlash;
	mpFlash = NULL;

	return new CStrObject(result);
}

// =========================================================================
// LiveToday / legal info / misc flags
// =========================================================================

int CConfig::GetLiveToday()
{
	// Bit 2 is "disabled" flag, inverted
	return (~(QueryMiscFlags() >> 2)) & 1;
}

void CConfig::SetLiveToday(int bEnable)
{
	DWORD value = QueryMiscFlags();
	if (bEnable) value &= ~4;
	else         value |= 4;
	SetMiscFlags(value);
}

int CConfig::GetAcceptedLegalInfo()
{
	// Bit 3 is "accepted legal info" flag
	return (QueryMiscFlags() >> 3) & 1;
}

void CConfig::SetAcceptedLegalInfo(int nValue)
{
	DWORD value = QueryMiscFlags();
	if (nValue) value |= 8;
	else        value &= ~8;
	SetMiscFlags(value);
}

void CConfig::BackToLauncher2()
{
	TheseusGetD3DDev()->PersistDisplay();

	LD_FROM_DASHBOARD fd;
	ZeroMemory(&fd, sizeof(fd));
	fd.dwContext = 'CODA';
	XWriteTitleInfoAndReboot("settings_adoc.xip",
		"\\Device\\Harddisk0\\Partition2",
		LDT_FROM_DASHBOARD, 0xfffe0000, (PLAUNCH_DATA)&fd);
}

void CConfig::GoToXOnlineDash()
{
	OutputDebugString(_T("[Config] GoToXOnlineDash() called\n"));
}

int CConfig::GetFontVersion()
{
	static int cachedVersion = -1;
	if (cachedVersion != -1)
		return cachedVersion;

	char szPath[MAX_PATH];
	DWORD dwRead, dwData = 0;

	_snprintf(szPath, MAX_PATH, "C:\\Fonts\\xbox_%d.xtf", GetLanguage());
	HANDLE hFile = CreateFileA(szPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
		hFile = CreateFileA("C:\\Fonts\\xbox.xtf", GENERIC_READ,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		if (ReadFile(hFile, &dwData, sizeof(dwData), &dwRead, NULL) && dwRead == 4)
			cachedVersion = (int)((dwData >> 24) - '0');
		CloseHandle(hFile);
	}

	if (cachedVersion == -1)
		cachedVersion = 0;
	return cachedVersion;
}

extern bool g_bMovingScreen;

void CConfig::ToggleNoisyCamera()
{
	g_bMovingScreen = !g_bMovingScreen;
	if (g_pSkinSettings)
		g_pSkinSettings->SetValue(_T("Camera"), _T("Noisy"), g_bMovingScreen ? _T("true") : _T("false"));
}

int CConfig::GetEthernetLinkStatus()
{
	XNADDR xna;
	DWORD dwState = XNetGetTitleXnAddr(&xna);
	return (dwState != 0) ? 1 : 0;
}
