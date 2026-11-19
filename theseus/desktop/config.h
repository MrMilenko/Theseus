// config.h: the desktop build's Config node. Same script surface as
// xbox/config.h; console-only answers are stubbed or faked on the PC.

#pragma once
#include "node.h"
#include "xbe.h"

class CConfig : public CNode
{
	DECLARE_NODE(CConfig, CNode)
	DECLARE_NODE_FUNCTIONS()
public:
	CConfig();
	~CConfig();

	// Launch data
	CStrObject* GetLaunchReason();
	DWORD GetTitleID();
	int GetLaunchContext();
	int GetLaunchParameter1();
	int GetLaunchParameter2();
	void BackToLauncher();
	void BackToLauncher2();
	void GoToXOnlineDash();
	int ForceSetLanguage() { return 0; }	// no launch data on desktop
	int ForceSetTimeZone() { return 0; }
	int ForceSetClock() { return 0; }

	// Language, region, parental control
	int GetLanguage();
	void SetLanguage(int lang);
	int GetGamePCFlags();
	void SetGamePCFlags(int flags);
	int GetMoviePCFlags();
	void SetMoviePCFlags(int flags);
	void SetParentPassword(const char* password);
	int CheckParentPassword(const char* password);
	CStrObject* GetAVPackType();
	CStrObject* GetAVRegion();
	CStrObject* GetGameRegion();

	// Video and audio
	int GetVideoMode();
	void SetVideoMode(int mode);
	int Get480Support();
	void Set480Support(int on);
	int Get720Support();
	void Set720Support(int on);
	int Get1080Support();
	void Set1080Support(int on);
	int GetPAL60Support();
	void SetPAL60Support(int on);
	int GetAudioMode();
	void SetAudioMode(int mode);
	int GetDolbyDigitalSupport();
	void SetDolbyDigitalSupport(int on);
	int GetDTSSupport();
	void SetDTSSupport(int on);

	// Clock
	int GetTimeZone();
	void SetTimeZone(int zone);
	int GetDSTAllowed();
	int GetDST();
	void SetDST(int on);

	// Dashboard options
	int GetAutoOff();
	void SetAutoOff(int on);
	int GetLiveToday();
	void SetLiveToday(int on);
	int GetAcceptedLegalInfo();
	void SetAcceptedLegalInfo(int value);
	int GetFontVersion();
	void ToggleNoisyCamera();
	void ApplySkin();
	void FlushMeshCache();

	// Power, fan, LED, network
	void Reset();
	void PowerOff();
	void PowerCycle();
	void XBOXReset();
	int GetInternalTemp();
	int GetCPUTemp();
	void SetFanSpeed(int speed);
	int GetFanSpeed();
	void SetLED(int mode);
	void NetworkShutdown();
	void NetworkReboot();
	void NetworkStartup();
	int GetEthernetLinkStatus();

	// Version and identity strings
	CStrObject* GetRecoveryKey();
	CStrObject* GetROMVersion();
	CStrObject* GetXdashVersion();
	CStrObject* GetXboxIP();
	CStrObject* GetEncoder();
	CStrObject* GetXBOXVersion();
	CStrObject* GetMODCHIPVersion();
	CStrObject* GetXBETitleID(const char* path);

	// Files and drives
	int FileExists(const char* path);
	int NtFileExists(const char* path);
	int CanDriveBeCleanup(int drive);

	CXBExecutable theXBE;
	uint32_t NetStatus;
};
