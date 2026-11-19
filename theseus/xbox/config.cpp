// config.cpp: the Xbox Config node. Language, video, audio, time zone,
// parental controls, launch data and the other EEPROM settings are
// rebuilt from the 5960 retail XBE (0x3a430..0x3b9e0); see
// docs/decomp/Config.md. Skins, network, power, the SMBus readings, the
// encoder / revision / modchip probes and XBE title ids are Theseus's.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

#include "xlaunch.h"
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

// D3D caches the AV mode here; zero it so a Reset picks up a new one.
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

// Every EEPROM setting below is a whole DWORD read and written back. 5960
// ignores the results of XQueryValue / XSetValue throughout.
static DWORD ReadSetting(DWORD dwIndex)
{
	DWORD dwType, dwValue;
	XQueryValue(dwIndex, &dwType, &dwValue, sizeof(dwValue), NULL);
	return dwValue;
}

static void WriteSetting(DWORD dwIndex, DWORD dwValue)
{
	XSetValue(dwIndex, REG_DWORD, &dwValue, sizeof(dwValue));
}

static void WriteSettingBit(DWORD dwIndex, DWORD dwBit, bool bSet)
{
	DWORD dwValue = ReadSetting(dwIndex);
	if (bSet)
		dwValue |= dwBit;
	else
		dwValue &= ~dwBit;
	WriteSetting(dwIndex, dwValue);
}

// Theseus starts the misc flags at zero when the read fails.
static DWORD ReadMiscFlags()
{
	DWORD dwType, dwValue = 0;
	XQueryValue(XC_MISC_FLAGS, &dwType, &dwValue, sizeof(dwValue), NULL);
	return dwValue;
}

// CConfig::GetLanguage()   [script fn 'GetLanguage', FND 0x0017fe08, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003a610:
//     XQueryValue(XC_LANGUAGE, ..); max = GetFontVersion() ? 9 : 6;
//     if (lang == 0 || lang > max) lang = 1; return lang;
// An unset or unknown language reads as English. Theseus caps it at 6.
int CConfig::GetLanguage()
{
	DWORD dwLanguage = ReadSetting(XC_LANGUAGE);
	if (dwLanguage == 0 || dwLanguage > 6)
		dwLanguage = 1;
	return (int)dwLanguage;
}

// CConfig::SetLanguage(n)   [script fn 'SetLanguage', FND 0x0017fe20, sig 14]
// Reconstructed from 5960 retail XBE @ 0x0003a660: XSetValue(XC_LANGUAGE, n).
void CConfig::SetLanguage(int nLanguage)
{
	WriteSetting(XC_LANGUAGE, (DWORD)nLanguage);
}

// CConfig::GetAVPackType()   [script fn 'GetAVPackType', FND 0x00180018, sig 8]
// CConfig::GetAVRegion()     [script fn 'GetAVRegion', FND 0x00180030, sig 8]
// CConfig::GetGameRegion()   [script fn 'GetGameRegion', FND 0x00180048, sig 8]
// Reconstructed from 5960 retail XBE @ 0x0003a680, 0x0003a740, 0x0003a7e0:
// XGetAVPack(), XGetVideoStandard() and the game region, each switched to
// its name; an unknown value gives a string object with no text.
CStrObject *CConfig::GetAVPackType()
{
	const TCHAR *szName = NULL;
	switch (XGetAVPack())
	{
	case XC_AV_PACK_SCART:    szName = _T("SCART");    break;
	case XC_AV_PACK_HDTV:     szName = _T("HDTV");     break;
	case XC_AV_PACK_VGA:      szName = _T("VGA");      break;
	case XC_AV_PACK_RFU:      szName = _T("RFU");      break;
	case XC_AV_PACK_SVIDEO:   szName = _T("SVIDEO");   break;
	case XC_AV_PACK_STANDARD: szName = _T("STANDARD"); break;
	}
	return new CStrObject(szName);
}

CStrObject *CConfig::GetAVRegion()
{
	const TCHAR *szName = NULL;
	switch (XGetVideoStandard())
	{
	case XC_VIDEO_STANDARD_NTSC_M: szName = _T("NTSC_M"); break;
	case XC_VIDEO_STANDARD_NTSC_J: szName = _T("NTSC_J"); break;
	case XC_VIDEO_STANDARD_PAL_I:  szName = _T("PAL_I");  break;
	case XC_VIDEO_STANDARD_PAL_M:  szName = _T("PAL_M");  break;
	}
	return new CStrObject(szName);
}

CStrObject *CConfig::GetGameRegion()
{
	const TCHAR *szName = NULL;
	switch (g_nCurRegion)
	{
	case XC_GAME_REGION_NA:          szName = _T("NA");          break;
	case XC_GAME_REGION_JAPAN:       szName = _T("JAPAN");       break;
	case XC_GAME_REGION_RESTOFWORLD: szName = _T("RESTOFWORLD"); break;
	}
	return new CStrObject(szName);
}

// The D3D present flags changed: drop the cached AV mode and reset.
static void ResetDisplay()
{
	D3D__AvInfo = 0;
	TheseusGetD3DDev()->Reset(&g_pp);
	TheseusInvalidateStateCache();
}

// CConfig::GetVideoMode()   [script fn 'GetVideoMode', FND 0x001800f0, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003a860:
//     if (flags & AV_FLAGS_LETTERBOX) return 1; return (flags >> 15) & 2;
// 0 normal, 1 letterbox, 2 widescreen; letterbox wins if both are set.
int CConfig::GetVideoMode()
{
	DWORD dwFlags = ReadSetting(XC_VIDEO_FLAGS);
	if (dwFlags & AV_FLAGS_LETTERBOX)
		return 1;
	return (dwFlags & AV_FLAGS_WIDESCREEN) ? 2 : 0;
}

// CConfig::SetVideoMode(mode)   [FND 0x00180108]
// Reconstructed from 5960 retail XBE @ 0x0003a8a0. Stores letterbox (1) or
// widescreen (2), clearing the other. Going in or out of widescreen flips
// the widescreen present flag and resets the device; the projection is
// rebuilt either way. Theseus also invalidates its render state cache.
void CConfig::SetVideoMode(int nVideoMode)
{
	DWORD dwFlags = ReadSetting(XC_VIDEO_FLAGS) & ~(AV_FLAGS_WIDESCREEN | AV_FLAGS_LETTERBOX);
	if (nVideoMode == 1)
		dwFlags |= AV_FLAGS_LETTERBOX;
	else if (nVideoMode == 2)
		dwFlags |= AV_FLAGS_WIDESCREEN;
	WriteSetting(XC_VIDEO_FLAGS, dwFlags);

	bool bWide = (nVideoMode == 2);
	if (bWide != g_bStretchWidescreen)
	{
		if (bWide)
			g_pp.Flags |= D3DPRESENTFLAG_WIDESCREEN;
		else
			g_pp.Flags &= ~D3DPRESENTFLAG_WIDESCREEN;
		g_bStretchWidescreen = bWide;
		ResetDisplay();
	}

	g_bProjectionDirty = true;
}

// Get/Set 480p, 720p, 1080i and PAL60   [FND 0x0017ff58..0x00180000]
// Reconstructed from 5960 retail XBE @ 0x0003a950..0x0003ab60: one bit each
// of XC_VIDEO_FLAGS (0x80000, 0x20000, 0x40000, 0x400000).
int CConfig::Get480Support()   { return (ReadSetting(XC_VIDEO_FLAGS) & AV_FLAGS_HDTV_480p) != 0; }
int CConfig::Get720Support()   { return (ReadSetting(XC_VIDEO_FLAGS) & AV_FLAGS_HDTV_720p) != 0; }
int CConfig::Get1080Support()  { return (ReadSetting(XC_VIDEO_FLAGS) & AV_FLAGS_HDTV_1080i) != 0; }
int CConfig::GetPAL60Support() { return (ReadSetting(XC_VIDEO_FLAGS) & AV_FLAGS_60Hz) != 0; }

void CConfig::Set720Support(int bEnable)   { WriteSettingBit(XC_VIDEO_FLAGS, AV_FLAGS_HDTV_720p, bEnable != 0); }
void CConfig::Set1080Support(int bEnable)  { WriteSettingBit(XC_VIDEO_FLAGS, AV_FLAGS_HDTV_1080i, bEnable != 0); }
void CConfig::SetPAL60Support(int bEnable) { WriteSettingBit(XC_VIDEO_FLAGS, AV_FLAGS_60Hz, bEnable != 0); }

// CConfig::Set480Support(on)   [FND 0x0017ff70]
// Reconstructed from 5960 retail XBE @ 0x0003a980. Stores the bit. While a
// DVD plays on an HDTV pack with 480p Macrovision support, the display also
// switches between progressive and interlaced on the spot when the setting
// disagrees with it.
void CConfig::Set480Support(int bEnable)
{
	WriteSettingBit(XC_VIDEO_FLAGS, AV_FLAGS_HDTV_480p, bEnable != 0);

	if (g_nDiscType != DISC_VIDEO || XGetAVPack() != XC_AV_PACK_HDTV ||
	    !(XboxHardwareInfo->Flags & XBOX_480P_MACROVISION_ENABLED))
		return;

	bool bProgressive = (g_pp.Flags & D3DPRESENTFLAG_PROGRESSIVE) != 0;
	if (bProgressive == (bEnable != 0))
		return;

	if (bEnable)
		g_pp.Flags = (g_pp.Flags & ~D3DPRESENTFLAG_INTERLACED) | D3DPRESENTFLAG_PROGRESSIVE;
	else
		g_pp.Flags = (g_pp.Flags & ~D3DPRESENTFLAG_PROGRESSIVE) | D3DPRESENTFLAG_INTERLACED;
	ResetDisplay();
}

// CConfig::GetAudioMode()   [script fn 'GetAudioMode', FND 0x00180120, sig 2]
// CConfig::SetAudioMode(n)  [script fn 'SetAudioMode', FND 0x00180138, sig 14]
// Reconstructed from 5960 retail XBE @ 0x0003abb0 and 0x0003abf0. Script
// numbers modes 0 mono, 1 stereo, 2 surround; the low word of the audio
// flags holds the speaker setup, the high word the encodings, which Set
// leaves alone. Any other speaker value reads as surround; any other mode
// writes nothing new.
int CConfig::GetAudioMode()
{
	switch (DSSPEAKER_BASIC(ReadSetting(XC_AUDIO_FLAGS)))
	{
	case DSSPEAKER_MONO:   return 0;
	case DSSPEAKER_STEREO: return 1;
	}
	return 2;
}

void CConfig::SetAudioMode(int nAudioMode)
{
	DWORD dwFlags = ReadSetting(XC_AUDIO_FLAGS);
	if (nAudioMode == 0)
		dwFlags = DSSPEAKER_COMBINED(DSSPEAKER_MONO, dwFlags);
	else if (nAudioMode == 1)
		dwFlags = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, dwFlags);
	else if (nAudioMode == 2)
		dwFlags = DSSPEAKER_COMBINED(DSSPEAKER_SURROUND, dwFlags);
	WriteSetting(XC_AUDIO_FLAGS, dwFlags);
}

// Dolby Digital and DTS   [FND 0x00180150..0x00180198]
// Reconstructed from 5960 retail XBE @ 0x0003ac60..0x0003ad10: bits 0x10000
// and 0x20000 of XC_AUDIO_FLAGS.
int CConfig::GetDolbyDigitalSupport() { return (ReadSetting(XC_AUDIO_FLAGS) & DSSPEAKER_ENABLE_AC3) != 0; }
int CConfig::GetDTSSupport()          { return (ReadSetting(XC_AUDIO_FLAGS) & DSSPEAKER_ENABLE_DTS) != 0; }
void CConfig::SetDolbyDigitalSupport(int bEnable) { WriteSettingBit(XC_AUDIO_FLAGS, DSSPEAKER_ENABLE_AC3, bEnable != 0); }
void CConfig::SetDTSSupport(int bEnable)          { WriteSettingBit(XC_AUDIO_FLAGS, DSSPEAKER_ENABLE_DTS, bEnable != 0); }

// CConfig::GetAutoOff / SetAutoOff   [FND 0x00180060, 0x00180078]
// Reconstructed from 5960 retail XBE @ 0x0003ad60 and 0x0003ad70: straight
// through to XAutoPowerDownGet / XAutoPowerDownSet.
int CConfig::GetAutoOff()
{
	BOOL bAutoOff;
	XAutoPowerDownGet(&bAutoOff);
	return bAutoOff;
}

void CConfig::SetAutoOff(int bAutoOff)
{
	XAutoPowerDownSet(bAutoOff);
}

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


extern int GetTimeZoneIndex(const TIME_ZONE_INFORMATION *tzinfo);
extern bool GetTimeZoneInfo(int index, TIME_ZONE_INFORMATION *tzinfo);

// CConfig::GetTimeZone()   [script fn 'GetTimeZone', FND 0x001801e0, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003ae80:
//     if (XapipQueryTimeZoneInformation(&tz, 0) == 0 && tz.StandardName[0])
//         return max(GetTimeZoneIndex(&tz), 0);
//     return region == NA ? 11 : region == JAPAN ? 60 : 25;
// With no zone set yet, the default is Eastern, Tokyo or London by region.
int CConfig::GetTimeZone()
{
	TIME_ZONE_INFORMATION tzinfo;
	if (XapipQueryTimeZoneInformation(&tzinfo, NULL) == ERROR_SUCCESS && tzinfo.StandardName[0] != 0)
	{
		int nIndex = GetTimeZoneIndex(&tzinfo);
		return nIndex < 0 ? 0 : nIndex;
	}

	switch (XGetGameRegion())
	{
	case XC_GAME_REGION_NA:    return NA_DEFAULT_TIMEZONE;
	case XC_GAME_REGION_JAPAN: return JAPAN_DEFAULT_TIMEZONE;
	}
	return ROW_DEFAULT_TIMEZONE;
}

// CConfig::SetTimeZone(n)   [script fn 'SetTimeZone', FND 0x001801f8, sig 14]
// Reconstructed from 5960 retail XBE @ 0x0003af30:
//     if (GetTimeZoneInfo(n, &tz)) XapipSetTimeZoneInformation(&tz);
void CConfig::SetTimeZone(int nTimeZone)
{
	TIME_ZONE_INFORMATION tzinfo;
	if (GetTimeZoneInfo(nTimeZone, &tzinfo))
		XapipSetTimeZoneInformation(&tzinfo);
}

// CConfig::GetDSTAllowed()   [script fn 'GetDSTAllowed', FND 0x00180210, sig 2]
// CConfig::GetDST()          [script fn 'GetDST', FND 0x00180228, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003af80 and 0x0003afe0: a zone
// allows daylight time when both its standard and daylight dates name a
// month; GetDST also needs the console's use-DST answer.
int CConfig::GetDSTAllowed()
{
	TIME_ZONE_INFORMATION tzinfo;
	if (XapipQueryTimeZoneInformation(&tzinfo, NULL) != ERROR_SUCCESS)
		return 0;
	return tzinfo.StandardDate.wMonth != 0 && tzinfo.DaylightDate.wMonth != 0;
}

int CConfig::GetDST()
{
	TIME_ZONE_INFORMATION tzinfo;
	BOOL bUseDST;
	if (XapipQueryTimeZoneInformation(&tzinfo, &bUseDST) != ERROR_SUCCESS)
		return 0;
	return tzinfo.StandardDate.wMonth != 0 && tzinfo.DaylightDate.wMonth != 0 && bUseDST;
}

// CConfig::SetDST(on)   [script fn 'SetDST', FND 0x00180240, sig 14]
// Reconstructed from 5960 retail XBE @ 0x0003b050: if the misc flags read,
// clear (on) or set (off) XC_MISC_FLAG_DONT_USE_DST and write them back.
void CConfig::SetDST(int bObserveDST)
{
	DWORD dwType, dwSize, dwFlags;
	if (XQueryValue(XC_MISC_FLAGS, &dwType, &dwFlags, sizeof(dwFlags), &dwSize) != ERROR_SUCCESS)
		return;
	if (bObserveDST)
		dwFlags &= ~XC_MISC_FLAG_DONT_USE_DST;
	else
		dwFlags |= XC_MISC_FLAG_DONT_USE_DST;
	XSetValue(XC_MISC_FLAGS, REG_DWORD, &dwFlags, sizeof(dwFlags));
}

// EncodePassword(text)
// Reconstructed from 5960 retail XBE @ 0x0003b0a0. A pass code is typed on
// the pad, one nibble per press:
//     u 1  d 2  l 3  r 4  a 5  b 6  x 7  y 8  B 9  W 10  L 11  R 12
// (d-pad, A B X Y, black, white, triggers). Anything else adds a zero nibble.
static DWORD EncodePassword(const TCHAR *szPassword)
{
	static const TCHAR keys[] = _T("udlrabxyBWLR");
	DWORD dwCode = 0;
	for (; *szPassword != 0; szPassword++)
	{
		const TCHAR *pKey = _tcschr(keys, *szPassword);
		dwCode = dwCode * 16 + (pKey != NULL ? (DWORD)(pKey - keys) + 1 : 0);
	}
	return dwCode;
}

// CConfig::CheckParentPassword(text)   [script fn 'CheckParentPassword', FND 0x001801b0, sig 25]
// CConfig::SetParentPassword(text)     [script fn 'SetParentPassword', FND 0x001801c8, sig 12]
// Reconstructed from 5960 retail XBE @ 0x0003b1b0 and 0x0003b1f0: the
// encoded code against, or into, XC_PARENTAL_CONTROL_PASSWORD.
int CConfig::CheckParentPassword(const TCHAR *szCheckPassword)
{
	return ReadSetting(XC_PARENTAL_CONTROL_PASSWORD) == EncodePassword(szCheckPassword);
}

void CConfig::SetParentPassword(const TCHAR *szNewPassword)
{
	WriteSetting(XC_PARENTAL_CONTROL_PASSWORD, EncodePassword(szNewPassword));
}

// Game and movie ratings   [FND 0x0017fe38..0x0017fe80]
// Reconstructed from 5960 retail XBE @ 0x0003b210..0x0003b2a0. The EEPROM
// keeps them upside down: script's game level n is stored as 6 - n, movie
// level n as 7 - n; an unreadable value counts as stored 0. Theseus CHECKs
// the range both ways, since scripts write these straight into the EEPROM.
int CConfig::GetGamePCFlags()
{
	DWORD dwType, dwStored = 0;
	XQueryValue(XC_PARENTAL_CONTROL_GAMES, &dwType, &dwStored, sizeof(dwStored), NULL);
	if (!CHECK(dwStored <= 6))
		dwStored = 0;
	return 6 - (int)dwStored;
}

void CConfig::SetGamePCFlags(int nFlags)
{
	if (!CHECK(nFlags >= 0 && nFlags <= 6))
		return;
	WriteSetting(XC_PARENTAL_CONTROL_GAMES, (DWORD)(6 - nFlags));
}

int CConfig::GetMoviePCFlags()
{
	DWORD dwType, dwStored = 0;
	XQueryValue(XC_PARENTAL_CONTROL_MOVIES, &dwType, &dwStored, sizeof(dwStored), NULL);
	if (!CHECK(dwStored <= 7))
		dwStored = 0;
	return 7 - (int)dwStored;
}

void CConfig::SetMoviePCFlags(int nFlags)
{
	if (!CHECK(nFlags >= 0 && nFlags <= 7))
		return;
	WriteSetting(XC_PARENTAL_CONTROL_MOVIES, (DWORD)(7 - nFlags));
}

// CConfig::GetLaunchReason()   [script fn 'GetLaunchReason', FND 0x0017fe98, sig 8]
// Reconstructed from 5960 retail XBE @ 0x0003b2d0: with launch data, the
// reason as a name (jump table @ 0x0003b3ac); otherwise, or for a reason
// it doesn't name, an empty string. Theseus also names the online menu,
// account signup and network settings by this XDK's older numbering.
CStrObject *CConfig::GetLaunchReason()
{
	const TCHAR *szReason = _T("");
	if (g_bHasLaunchData)
	{
		switch (g_dwLaunchReason)
		{
		case XLD_LAUNCH_DASHBOARD_ERROR:                 szReason = _T("Error");                   break;
		case XLD_LAUNCH_DASHBOARD_MEMORY:                szReason = _T("Memory");                  break;
		case XLD_LAUNCH_DASHBOARD_SETTINGS:              szReason = _T("Settings");                break;
		case XLD_LAUNCH_DASHBOARD_MUSIC:                 szReason = _T("Music");                   break;
		case XLD_LAUNCH_DASHBOARD_ONLINE_MENU:           szReason = _T("FromOnlineDash");          break;
		case XLD_LAUNCH_DASHBOARD_NEW_ACCOUNT_SIGNUP:    szReason = _T("NewAccountInternal");      break;
		case XLD_LAUNCH_DASHBOARD_NETWORK_CONFIGURATION: szReason = _T("NetworkSettingsInternal"); break;
		case 12: szReason = _T("FromOnlineDash");            break;
		case 13: szReason = _T("FriendsInternal");           break;
		case 14: szReason = _T("VoiceChatInternal");         break;
		case 15: szReason = _T("MessagesInternal");          break;
		case 16: szReason = _T("AccountManagementInternal"); break;
		case 17: szReason = _T("NewAccountInternal");        break;
		case 18: szReason = _T("AccountRecoveryInternal");   break;
		case 19: szReason = _T("NetworkSettingsInternal");   break;
		}
	}
	return new CStrObject(szReason);
}

// GetLaunchContext / GetLaunchParameter1 / GetLaunchParameter2   [FND 0x0017feb0..0x0017fee0]
// Reconstructed from 5960 retail XBE @ 0x0003b400..0x0003b440: the launch
// data's field, or 0 with none. GetTitleID is the same for the title id.
int CConfig::GetLaunchContext()    { return g_bHasLaunchData ? (int)g_dwLaunchContext : 0; }
int CConfig::GetLaunchParameter1() { return g_bHasLaunchData ? (int)g_dwLaunchParameter1 : 0; }
int CConfig::GetLaunchParameter2() { return g_bHasLaunchData ? (int)g_dwLaunchParameter2 : 0; }
DWORD CConfig::GetTitleID()        { return g_bHasLaunchData ? g_dwTitleID : 0; }

// CConfig::CanDriveBeCleanup(drive)   [script fn 'CanDriveBeCleanup', FND 0x0017fef8, sig 3]
// Reconstructed from 5960 retail XBE @ 0x0003b460: true for the drives a
// game may ask the dash to clean up, any case. 5960 takes F to M, T and U;
// Theseus takes H to O, T and U, keeping the big F and G partitions out.
int CConfig::CanDriveBeCleanup(int Drive)
{
	int letter = toupper(Drive);
	return letter == 'T' || letter == 'U' || (letter >= 'H' && letter <= 'O');
}

// CConfig::BackToLauncher()   [script fn 'BackToLauncher', FND 0x0017ff10, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0003b4b0:
//     PersistDisplay();
//     if (reason == SETTINGS && (param1 & (CLOCK | TIMEZONE | LANGUAGE))) HalReturnToFirmware(reboot);
//     LD_FROM_DASHBOARD fd = {0}; fd.dwContext = GetLaunchContext();
//     XWriteTitleInfoAndReboot("default.xbe", "\\Device\\CdRom0", LDT_FROM_DASHBOARD, GetTitleID(), &fd);
// A game that sent the player to set the clock, zone or language gets a
// clean reboot; otherwise it's relaunched from the disc with its context.
void CConfig::BackToLauncher()
{
	TheseusGetD3DDev()->PersistDisplay();

	if (CheckForcedSettings(XLD_SETTINGS_CLOCK | XLD_SETTINGS_TIMEZONE | XLD_SETTINGS_LANGUAGE))
	{
		HalReturnToFirmware(HalRebootRoutine);
		return;
	}

	LD_FROM_DASHBOARD fd;
	ZeroMemory(&fd, sizeof(fd));
	fd.dwContext = GetLaunchContext();
	XWriteTitleInfoAndReboot("default.xbe", "\\Device\\CdRom0", LDT_FROM_DASHBOARD, GetTitleID(), (PLAUNCH_DATA)&fd);
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


// CConfig::NtFileExists(path)   [script fn 'NtFileExists', FND 0x00180300, sig 25]
// 5960 @ 0x0003b910 opens the raw NT path (NtOpenFile, read access) and
// answers whether that worked. Theseus maps the hard disk partitions it
// knows to their drive letters and asks for the file's attributes instead.
// Paths it doesn't know go through unchanged.
int CConfig::NtFileExists(const TCHAR* FileName)
{
	static const struct { const TCHAR *szDevice; TCHAR chDrive; } partitions[] =
	{
		{ _T("\\Device\\Harddisk0\\Partition2"), 'C' },
		{ _T("\\Device\\Harddisk0\\Partition1"), 'E' },
		{ _T("\\Device\\Harddisk0\\Partition6"), 'F' },
		{ _T("\\Device\\Harddisk0\\Partition7"), 'G' },
		{ _T("\\Device\\Harddisk0\\Partition8"), 'H' },
		{ _T("\\Device\\Harddisk0\\Partition9"), 'I' },
	};

	TCHAR szPath[MAX_PATH];
	_tcsncpy(szPath, FileName, MAX_PATH);

	for (int i = 0; i < (int)countof(partitions); i++)
	{
		size_t cch = _tcslen(partitions[i].szDevice);
		if (_tcsnicmp(FileName, partitions[i].szDevice, cch) == 0)
		{
			_stprintf(szPath, _T("%c:%s"), partitions[i].chDrive, FileName + cch);
			break;
		}
	}

	char szAnsi[MAX_PATH];
	Ansi(szAnsi, szPath, MAX_PATH);
	WIN32_FILE_ATTRIBUTE_DATA fad;
	return GetFileAttributesEx(szAnsi, GetFileExInfoStandard, &fad) ? 1 : 0;
}

// CConfig::FileExists(path)   [script fn 'FileExists', FND 0x001802e8, sig 25]
// 5960 @ 0x0003b900 is DoesFileExist(path). Theseus routes it through
// NtFileExists so either kind of path works.
int CConfig::FileExists(const TCHAR* FileName)
{
	return NtFileExists(FileName);
}

// CConfig::GetRecoveryKey()   [script fn 'GetRecoveryKey', FND 0x001802a0, sig 8]
// 5960 @ 0x0003b6b0 derives a four press pad code (letters "AXYUDLR") from
// an HMAC of the console's hard disk key. Theseus returns it empty: a
// parental lock recovery code means nothing on modified hardware.
CStrObject *CConfig::GetRecoveryKey()
{
	return new CStrObject(_T(""));
}

// CConfig::GetROMVersion()     [script fn 'GetROMVersion', FND 0x001802b8, sig 8]
// CConfig::GetXdashVersion()   [script fn 'GetXdashVersion', FND 0x001802d0, sig 8]
// Reconstructed from 5960 retail XBE @ 0x0003b7b0 and 0x0003b860: both
// print "%d.%02d.%d.%02d", the kernel's version (QFE without its top bit)
// and the dash's own 1, 0, 5960, 1.
static CStrObject *FormatVersion(int major, int minor, int build, int qfe)
{
	TCHAR szVersion[64];
	_sntprintf(szVersion, countof(szVersion), _T("%d.%02d.%d.%02d"), major, minor, build, qfe);
	szVersion[countof(szVersion) - 1] = 0;
	return new CStrObject(szVersion);
}

CStrObject *CConfig::GetROMVersion()
{
	return FormatVersion(XboxKrnlVersion->Major, XboxKrnlVersion->Minor, XboxKrnlVersion->Build, XboxKrnlVersion->Qfe & 0x7FFF);
}

CStrObject *CConfig::GetXdashVersion()
{
	return FormatVersion(1, 0, 5960, 1);
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


// Live today, the accepted legal notice and daylight time all live in
// XC_MISC_FLAGS: bit 2 is "don't show Live today" (so read inverted), bit 3
// "legal notice accepted".
// CConfig::GetLiveToday / SetLiveToday            [FND 0x00180090, 0x001800a8]
// CConfig::GetAcceptedLegalInfo / SetAccepted...  [FND 0x001800c0, 0x001800d8]
// Reconstructed from 5960 retail XBE @ 0x0003ad80..0x0003ae30.
int CConfig::GetLiveToday()
{
	return (ReadMiscFlags() & 4) == 0;
}

void CConfig::SetLiveToday(int bEnable)
{
	DWORD dwFlags = ReadMiscFlags();
	if (bEnable)
		dwFlags &= ~4;
	else
		dwFlags |= 4;
	WriteSetting(XC_MISC_FLAGS, dwFlags);
}

int CConfig::GetAcceptedLegalInfo()
{
	return (ReadMiscFlags() & 8) != 0;
}

void CConfig::SetAcceptedLegalInfo(int nValue)
{
	DWORD dwFlags = ReadMiscFlags();
	if (nValue)
		dwFlags |= 8;
	else
		dwFlags &= ~8;
	WriteSetting(XC_MISC_FLAGS, dwFlags);
}

// CConfig::BackToLauncher2()   [script fn 'BackToLauncher2', FND 0x0017ff28, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0003b550: reboots into
// settings_adoc.xip as title 0xFFFE0000 with 'ADOC' as the launch context.
// 5960 asks the object manager where the dash itself lives and launches
// from there; Theseus names the C partition outright.
void CConfig::BackToLauncher2()
{
	TheseusGetD3DDev()->PersistDisplay();

	LD_FROM_DASHBOARD fd;
	ZeroMemory(&fd, sizeof(fd));
	fd.dwContext = 'CODA';
	XWriteTitleInfoAndReboot("settings_adoc.xip", "\\Device\\Harddisk0\\Partition2",
	                         LDT_FROM_DASHBOARD, 0xfffe0000, (PLAUNCH_DATA)&fd);
}

// CConfig::GoToXOnlineDash()
// 5960 @ 0x0003b640 reboots into C:\XODash\xonlinedash.xbe with reason 0x0C.
// Theseus has no online dashboard to go to, so it only logs the call.
void CConfig::GoToXOnlineDash()
{
	OutputDebugString(_T("[Config] GoToXOnlineDash() called\n"));
}

// CConfig::GetFontVersion()   [script fn 'GetFontVersion', FND 0x0017fdf0, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003a520: opens the font file
// (5960: "%sfonts\Xbox.xtf", then "%sXbox.xtf" under the app dir), reads its
// first DWORD and takes the top byte as a digit; cached after the first
// call. Theseus reads the system fonts, C:\Fonts\xbox_<language>.xtf then
// C:\Fonts\xbox.xtf, wants all four bytes, and calls a missing font 0.
int CConfig::GetFontVersion()
{
	static int nVersion = -1;
	if (nVersion != -1)
		return nVersion;

	char szPath[MAX_PATH];
	_snprintf(szPath, MAX_PATH, "C:\\Fonts\\xbox_%d.xtf", GetLanguage());
	HANDLE hFont = CreateFileA(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFont == INVALID_HANDLE_VALUE)
		hFont = CreateFileA("C:\\Fonts\\xbox.xtf", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	nVersion = 0;
	if (hFont != INVALID_HANDLE_VALUE)
	{
		DWORD dwHeader = 0, dwRead = 0;
		if (ReadFile(hFont, &dwHeader, sizeof(dwHeader), &dwRead, NULL) && dwRead == sizeof(dwHeader))
			nVersion = (int)(dwHeader >> 24) - '0';
		CloseHandle(hFont);
	}
	if (nVersion == -1)
		nVersion = 0;
	return nVersion;
}

extern bool g_bMovingScreen;

// CConfig::ToggleNoisyCamera()
// 5960 @ 0x0003b9e0 flips the camera drift by creating or deleting
// T:\NoisyCamera. Theseus keeps it in the skin's settings instead.
void CConfig::ToggleNoisyCamera()
{
	g_bMovingScreen = !g_bMovingScreen;
	if (g_pSkinSettings)
		g_pSkinSettings->SetValue(_T("Camera"), _T("Noisy"), g_bMovingScreen ? _T("true") : _T("false"));
}

// CConfig::GetEthernetLinkStatus()   [script fn 'GetEthernetLinkStatus', FND 0x00180318, sig 2]
// 5960 @ 0x0003b9d0 is XNetGetEthernetLinkStatus() & 1, the cable's link
// bit. Theseus answers whether XNet has started looking for an address.
int CConfig::GetEthernetLinkStatus()
{
	XNADDR xna;
	return XNetGetTitleXnAddr(&xna) != 0 ? 1 : 0;
}
