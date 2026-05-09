// config.h: desktop CConfig node declarations. Counterpart to
// xbox/config.h.

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

    int GetLanguage();
    void SetLanguage(int nLanguage);
    int GetGamePCFlags();
    void SetGamePCFlags(int nLanguage);
    int GetMoviePCFlags();
    void SetMoviePCFlags(int nLanguage);
    CStrObject *GetLaunchReason();
    DWORD GetTitleID();
    int GetLaunchContext();
    int GetLaunchParameter1();
    int GetLaunchParameter2();
    int CanDriveBeCleanup(int Drive);
    void BackToLauncher();

    uint32_t NetStatus;

    int NtFileExists(const char* szPath);

    void Reset();
    void PowerOff();
    void PowerCycle();
    void XBOXReset();
    int GetInternalTemp();
    int GetCPUTemp();
    void SetFanSpeed(int speed);
    int GetFanSpeed();
    void SetLED(int LEDMode);
    void NetworkShutdown();
    void NetworkReboot();
    void NetworkStartup();

    CStrObject *GetAVPackType();
    CStrObject *GetAVRegion();
    CStrObject *GetGameRegion();
    int GetVideoMode();
    void SetVideoMode(int nVideoMode);
    void ApplySkin();
    void FlushMeshCache();
    int Get480Support();
    void Set480Support(int b480Support);
    int Get720Support();
    void Set720Support(int b720Support);
    int Get1080Support();
    void Set1080Support(int b1080Support);
    int GetPAL60Support();
    void SetPAL60Support(int bPAL60Support);
    int GetAudioMode();
    void SetAudioMode(int nVideoMode);
    int GetDolbyDigitalSupport();
    void SetDolbyDigitalSupport(int bDolbyDigitalSupport);
    int GetDTSSupport();
    void SetDTSSupport(int bDTSSupport);
    int GetAutoOff();
    void SetAutoOff(int bAutoOff);
    int GetLiveToday();
    void SetLiveToday(int bLiveToday);

    void SetParentPassword(const char *szNewPassword);
    int CheckParentPassword(const char *szCheckPassword);

    int GetTimeZone();
    void SetTimeZone(int nTimeZone);
    int GetDSTAllowed();
    int GetDST();
    void SetDST(int bObserveDST);

    int ForceSetLanguage() { return 0; } // No launch data on desktop
    int ForceSetTimeZone() { return 0; }
    int ForceSetClock()    { return 0; }

    CStrObject *GetRecoveryKey();
    CStrObject *GetROMVersion();
    CStrObject *GetXdashVersion();
    CStrObject *GetXboxIP();
    CStrObject *GetEncoder();
    CStrObject *GetXBOXVersion();
    CStrObject *GetMODCHIPVersion();
    CStrObject *GetXBETitleID(const char *szXBEPath);

    // 5960 functions (RE'd from retail binary)
    int GetAcceptedLegalInfo();
    void SetAcceptedLegalInfo(int value);
    void BackToLauncher2();
    void GoToXOnlineDash();
    int GetFontVersion();
    void ToggleNoisyCamera();
    int GetEthernetLinkStatus();
    int FileExists(const char* szPath);

protected:
    DECLARE_NODE_FUNCTIONS()

};
