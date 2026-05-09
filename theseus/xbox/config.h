// config.h: CConfig XAP node. Exposes the dashboard's system config
// (language, video / audio mode, parental controls, timezone /
// DST, network, hardware status, launch context) to scripts.
// Companion to xbox/config.cpp.

#pragma once
#include "node.h"
#include "xbe.h"

class CConfig : public CNode
{
    DECLARE_NODE(CConfig, CNode)
public:
    CConfig();
    ~CConfig();
    CXBExecutable theXBE;

    // Language
    int GetLanguage();
    void SetLanguage(int nLanguage);

    // AV / video
    CStrObject *GetAVPackType();
    CStrObject *GetAVRegion();
    CStrObject *GetGameRegion();
    int GetVideoMode();
    void SetVideoMode(int nVideoMode);
    int Get480Support();
    void Set480Support(int bEnable);
    int Get720Support();
    void Set720Support(int bEnable);
    int Get1080Support();
    void Set1080Support(int bEnable);
    int GetPAL60Support();
    void SetPAL60Support(int bEnable);
    void ApplySkin();
    void FlushMeshCache();

    // Audio
    int GetAudioMode();
    void SetAudioMode(int nAudioMode);
    int GetDolbyDigitalSupport();
    void SetDolbyDigitalSupport(int bEnable);
    int GetDTSSupport();
    void SetDTSSupport(int bEnable);

    // Power / auto-off
    int GetAutoOff();
    void SetAutoOff(int bAutoOff);
    void PowerOff();
    void Reset();
    void XBOXReset();
    void PowerCycle();

    // SMBus hardware
    void SetLED(int LEDMode);
    void SetFanSpeed(int speed);
    int GetFanSpeed();
    int GetCPUTemp();
    int GetInternalTemp();

    // Timezone / DST
    int GetTimeZone();
    void SetTimeZone(int nTimeZone);
    int GetDSTAllowed();
    int GetDST();
    void SetDST(int bObserveDST);

    // Parental controls
    int CheckParentPassword(const TCHAR *szCheckPassword);
    void SetParentPassword(const TCHAR *szNewPassword);
    int GetGamePCFlags();
    void SetGamePCFlags(int nFlags);
    int GetMoviePCFlags();
    void SetMoviePCFlags(int nFlags);

    // Launch data
    CStrObject *GetLaunchReason();
    DWORD GetTitleID();
    int GetLaunchContext();
    int GetLaunchParameter1();
    int GetLaunchParameter2();
    int CanDriveBeCleanup(int Drive);
    void BackToLauncher();

    // Network
    DWORD NetStatus;
    DWORD XnAddrStatus;
    XNADDR xnaddr;
    void NetworkStartup();
    void NetworkShutdown();
    void NetworkReboot();

    // File system
    int NtFileExists(const TCHAR* szPath);
    int FileExists(const TCHAR* szPath);

    // System info
    CStrObject *GetRecoveryKey();
    CStrObject *GetROMVersion();
    CStrObject *GetXdashVersion();
    CStrObject *GetXboxIP();
    CStrObject *GetEncoder();
    CStrObject *GetXBOXVersion();
    CStrObject *GetMODCHIPVersion();
    CStrObject *GetXBETitleID(const TCHAR *szXBEPath);
    CStrObject *TestTitleID();

    // 5960 functions
    int GetAcceptedLegalInfo();
    void SetAcceptedLegalInfo(int value);
    void BackToLauncher2();
    void GoToXOnlineDash();
    int GetFontVersion();
    void ToggleNoisyCamera();
    int GetEthernetLinkStatus();
    int GetLiveToday();
    void SetLiveToday(int bLiveToday);

    int ForceSetLanguage() { return CheckForcedSettings(XLD_SETTINGS_LANGUAGE); }
    int ForceSetTimeZone() { return CheckForcedSettings(XLD_SETTINGS_TIMEZONE); }
    int ForceSetClock()    { return CheckForcedSettings(XLD_SETTINGS_CLOCK); }

protected:
    DECLARE_NODE_FUNCTIONS()

private:
    int CheckForcedSettings(int flag)
    {
        return ((TheseusGetLaunchReason() == XLD_LAUNCH_DASHBOARD_SETTINGS) &&
                (TheseusGetLaunchParameter1() & flag)) ? 1 : 0;
    }
};
