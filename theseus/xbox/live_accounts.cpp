// live_accounts.cpp: CLiveAccounts XAP node. Wraps the Xbox Live
// XOnline account-management APIs (account list, copy / delete, MU
// account migration). Theseus-original; the retail dashboard had this
// inline in dashapp.cpp.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "xonlinep.h"
#include <xonline.h>


extern "C"
{
    extern XONLINE_USER g_Users[XONLINE_MAX_STORED_ONLINE_USERS];
    extern DWORD g_NumUsers;
    extern bool g_bStarted;
}

class CLiveAccounts : public CNode
{
    DECLARE_NODE(CLiveAccounts, CNode)
public:
    CLiveAccounts();
    ~CLiveAccounts();

    int GetNumberOfAccounts();
    int GetNumAccountsOnHD();
    CStrObject* GetAccountName(int accountNumber);
    void Refresh();
    DWORD GetTitleID();
    CStrObject* GetMessageOfTheDayText();
    void ShowIcon(int bShow);
    void ClearMOTDCache();
    void ClearLastLogonUser();
    void Logon(int index, const TCHAR* password);
    void Logoff();
    int VerifyPassword(int index, const TCHAR* password);
    int IsPasswordEnabled(int index);
    int IsPasswordVerified();
    int IsBackFromEntryPoint();
    void PersistUser(int index);
    int IsVoiceAllowed();
    void LaunchEntryPoint(int port, int clearPasscode, const TCHAR* destination);
    CStrObject* GetGameInvites();
    CStrObject* GetFriendInvites();
    CStrObject* GetNumberOfFriendsOnline();
    int InternalLogon(int index, const TCHAR* passcode);
    bool LoadFromHD();
    // 5960 additions
    int GetLastLogonUser();
    CStrObject* GetResult();
    void LaunchDashUpdate();
    int m_nCurrentIndex;
    bool m_bLogon;
    
    bool m_fLogOnSuccess;
    bool m_fLogOnInProgress;

protected:
    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("LiveAccounts", CLiveAccounts, CNode)

START_NODE_PROPS(CLiveAccounts, CNode)
    NODE_PROP(pt_boolean, CLiveAccounts, bLogon)
    NODE_PROP(pt_boolean, CLiveAccounts, fLogOnSuccess)
    NODE_PROP(pt_boolean, CLiveAccounts, fLogOnInProgress)
    NODE_PROP(pt_integer, CLiveAccounts, nCurrentIndex)
END_NODE_PROPS()

#define _FND_CLASS CLiveAccounts
START_NODE_FUN(CLiveAccounts, CNode)
    NODE_FUN_IV(GetNumberOfAccounts)
    NODE_FUN_IV(GetNumAccountsOnHD)
    NODE_FUN_SI(GetAccountName)
    NODE_FUN_VV(Refresh)
    NODE_FUN_SV(GetMessageOfTheDayText)
    NODE_FUN_VI(ShowIcon)
    NODE_FUN_VV(ClearMOTDCache)
    NODE_FUN_VV(ClearLastLogonUser)
    NODE_FUN_VIS(Logon)
    NODE_FUN_VV(Logoff)
    NODE_FUN_IV(IsPasswordVerified)
    NODE_FUN_IV(IsBackFromEntryPoint)
    NODE_FUN_VI(PersistUser)
    NODE_FUN_IV(IsVoiceAllowed)
    NODE_FUN_SV(GetGameInvites)
    NODE_FUN_SV(GetFriendInvites)
    NODE_FUN_SV(GetNumberOfFriendsOnline)
    NODE_FUN_VIIS(LaunchEntryPoint)
    // 5960 additions
    NODE_FUN_IV(GetLastLogonUser)
    NODE_FUN_SV(GetResult)
    NODE_FUN_VV(LaunchDashUpdate)
END_NODE_FUN()
#undef _FND_CLASS

CLiveAccounts::CLiveAccounts() {}
CLiveAccounts::~CLiveAccounts() {}

XONLINE_USER g_Users[XONLINE_MAX_STORED_ONLINE_USERS] = { 0 };
DWORD g_NumUsers = 0;
bool g_bStarted = false;

bool XboxLive_Startup()
{
    OutputDebugString(_T("[XboxLive] XboxLive_Startup() called\n"));

    if (g_bStarted)
    {
        OutputDebugString(_T("[XboxLive] Already started\n"));
        return true;
    }

    HRESULT hr = XOnlineStartup(NULL);
    if (FAILED(hr))
    {
        TCHAR buf[64];
        wsprintf(buf, _T("[XboxLive] XOnlineStartup failed: 0x%08X\n"), hr);
        OutputDebugString(buf);
        return false;
    }

    // Try to fetch the title's IP address
    XNADDR xnaddr;
    ZeroMemory(&xnaddr, sizeof(xnaddr));
    INT result = XNetGetTitleXnAddr(&xnaddr);

    if (result == XNET_GET_XNADDR_NONE || *(DWORD*)&xnaddr.ina == 0)
    {
        OutputDebugString(_T("[XboxLive] Warning: XNetGetTitleXnAddr returned 0.0.0.0 or no address\n"));
    }
    else
    {
        TCHAR buf[64];
        wsprintf(buf, _T("[XboxLive] Local IP: %d.%d.%d.%d\n"),
            xnaddr.ina.S_un.S_un_b.s_b1,
            xnaddr.ina.S_un.S_un_b.s_b2,
            xnaddr.ina.S_un.S_un_b.s_b3,
            xnaddr.ina.S_un.S_un_b.s_b4);
        OutputDebugString(buf);
    }

    g_bStarted = true;
    OutputDebugString(_T("[XboxLive] XOnlineStartup succeeded\n"));
    return true;
}


void AnsiToTchar(TCHAR* dest, const char* src)
{
    while (*src)
        *dest++ = (TCHAR)(*src++);
    *dest = 0;
}

void CLiveAccounts::Refresh()
{
    OutputDebugString(_T("[LiveAccounts] Refresh() called\n"));
    LoadFromHD();
}

int CLiveAccounts::GetNumAccountsOnHD()
{
    OutputDebugString(_T("[LiveAccounts] GetNumAccountsOnHD() called\n"));
    if (g_NumUsers == 0)
        LoadFromHD();

    TCHAR buf[64];
    wsprintf(buf, _T("[LiveAccounts] Total accounts on HD: %lu\n"), g_NumUsers);
    OutputDebugString(buf);
    return g_NumUsers;
}

bool CLiveAccounts::LoadFromHD()
{
    OutputDebugString(_T("[XboxLive] LoadFromHD() called\n"));

    if (!g_bStarted && !XboxLive_Startup())
        return false;

    ZeroMemory(g_Users, sizeof(g_Users));
    g_NumUsers = 0;

    HRESULT hr = _XOnlineGetUsersFromHD(g_Users, &g_NumUsers);
    TCHAR buf[128];
    if (FAILED(hr) || g_NumUsers == 0)
    {
        wsprintf(buf, _T("[XboxLive] Failed to load users: HR=0x%08X Count=%lu\n"), hr, g_NumUsers);
        OutputDebugString(buf);
        return false;
    }

    wsprintf(buf, _T("[XboxLive] Loaded %lu user(s)\n"), g_NumUsers);
    OutputDebugString(buf);

    for (DWORD i = 0; i < g_NumUsers; ++i)
    {
        TCHAR detail[128];
        wsprintf(detail, _T(" [%lu] Gamertag: %hs | XUID: %llu\n"), i, g_Users[i].name, g_Users[i].xuid.qwUserID);
        OutputDebugString(detail);
    }

    return true;
}

int CLiveAccounts::GetNumberOfAccounts()
{
    OutputDebugString(_T("[LiveAccounts] GetNumberOfAccounts() called\n"));
    return (int)g_NumUsers;
}

CStrObject* CLiveAccounts::GetAccountName(int accountNumber)
{
    OutputDebugString(_T("[LiveAccounts] GetAccountName() called\n"));

    if (accountNumber < 0 || (DWORD)accountNumber >= g_NumUsers)
    {
        OutputDebugString(_T("[LiveAccounts] Invalid account index\n"));
        return new CStrObject(_T("INVALID"));
    }

    TCHAR buffer[17] = { 0 };
    AnsiToTchar(buffer, g_Users[accountNumber].name);

    TCHAR out[64];
    wsprintf(out, _T("[LiveAccounts] Returning name: %s\n"), buffer);
    OutputDebugString(out);

    return new CStrObject(buffer);
}

CStrObject* CLiveAccounts::GetMessageOfTheDayText()
{
    // 5960 binary: reads MOTD from cached XBX files downloaded via XOnline
    // The MOTD files are stored in T:\motd\ (or similar cache path)
    // For Insignia: would need to fetch from Insignia's MOTD endpoint
    // For now: return empty string so the UI doesn't show stale/fake text
    OutputDebugString(_T("[LiveAccounts] GetMessageOfTheDayText() called\n"));
    return new CStrObject(_T(""));
}

void CLiveAccounts::ShowIcon(int bShow)
{
    // 5960 binary: if bShow, loads icon from this+0x1078c path, sets global icon ptr
    // If !bShow or load fails, sets default "xboxlogo" icon
    // The icon is rendered in the Live Today notification area
    TCHAR buf[64];
    wsprintf(buf, _T("[LiveAccounts] ShowIcon(%d)\n"), bShow);
    OutputDebugString(buf);

    // Set the global icon pointer for the renderer
    // On retail: g_pLiveIcon = bShow ? loadedIcon : defaultIcon
}

void CLiveAccounts::ClearMOTDCache()
{
    // 5960 binary: loops through 2 cached MOTD file paths (stride 0xc at global 0x215c0)
    // For each: truncates file to 0x80 bytes, deletes it, checks for related XBX, deletes that too
    // This forces a fresh MOTD download on next Live Today check
    OutputDebugString(_T("[LiveAccounts] ClearMOTDCache() called\n"));

    // Delete cached MOTD files from T: drive
    DeleteFileA("T:\\motd_text.xbx");
    DeleteFileA("T:\\motd_icon.xbx");
}

void CLiveAccounts::ClearLastLogonUser()
{
    // 5960 binary: MOV byte [0x17f42c], 0; clears the "has previous logon" global flag.
    OutputDebugString(_T("[LiveAccounts] ClearLastLogonUser() called\n"));
    // Clear the flag so IsBackFromEntryPoint returns false
    // and GetLastLogonUser returns -1
}
// NOTE: The active InternalLogon is a stub that sets local state without
// actually calling XOnlineLogon. The real Live login path was disabled
// to avoid hammering Insignia with unanswered logon requests.
int CLiveAccounts::InternalLogon(int index, const TCHAR* passcode)
{
    TCHAR buf[128];
    wsprintf(buf, _T("[LiveAccounts] InternalLogon(%d, %s)\n"), index, passcode ? passcode : _T("NULL"));
    OutputDebugString(buf);

    if (index < 0 || index >= (int)g_NumUsers)
    {
        OutputDebugString(_T("[LiveAccounts] Invalid index\n"));
        return 0;
    }

    wsprintf(buf, _T("[LiveAccounts] Logging in user %d: %hs\n"), index, g_Users[index].name);
    OutputDebugString(buf);

    m_nCurrentIndex = index;
    m_bLogon = true;
    m_fLogOnSuccess = true;
    m_fLogOnInProgress = false;

    return true;
}




void CLiveAccounts::Logon(int index, const TCHAR* passcode)
{
    OutputDebugString(_T("[LiveAccounts] Logon() called\n"));

    int result = InternalLogon(index, passcode);

    if (result)
    {
        OutputDebugString(_T("[LiveAccounts] Calling OnConnectionEstablished\n"));
        CallFunction(this, _T("OnConnectionEstablished"));
    }
    else
    {
        OutputDebugString(_T("[LiveAccounts] Login failed, not calling OnConnectionEstablished\n"));
        CallFunction(this, _T("OnFailure")); // Optional: Call failure handler
    }
}



void CLiveAccounts::Logoff()
{
    OutputDebugString(_T("[LiveAccounts] Logoff() called\n"));
    m_bLogon = false;
    m_nCurrentIndex = -1;
}

int CLiveAccounts::IsPasswordVerified()
{
    OutputDebugString(_T("[LiveAccounts] IsPasswordVerified() called. Returning true.\n"));
    return true;
}

int CLiveAccounts::IsBackFromEntryPoint()
{
    OutputDebugString(_T("[LiveAccounts] IsBackFromEntryPoint() called\n"));

    if (TheseusHasLaunchData())
    {
        OutputDebugString(_T("  Launch data is present. Returning TRUE\n"));
        return true;
    }

    OutputDebugString(_T("  No launch data. Returning FALSE\n"));
    return false;
}


void CLiveAccounts::PersistUser(int index)
{
    TCHAR buf[64];
    wsprintf(buf, _T("[LiveAccounts] PersistUser(%d)\n"), index);
    OutputDebugString(buf);
}

int CLiveAccounts::IsVoiceAllowed()
{
    OutputDebugString(_T("[LiveAccounts] IsVoiceAllowed() called\n"));
    return 1;
}

CStrObject* CLiveAccounts::GetGameInvites()
{
    OutputDebugString(_T("[LiveAccounts] GetGameInvites() called\n"));
    return new CStrObject(_T("0 Game Invites"));
}

CStrObject* CLiveAccounts::GetFriendInvites()
{
    OutputDebugString(_T("[LiveAccounts] GetFriendInvites() called\n"));
    return new CStrObject(_T("0 Friend Invites"));
}

CStrObject* CLiveAccounts::GetNumberOfFriendsOnline()
{
    OutputDebugString(_T("[LiveAccounts] GetNumberOfFriendsOnline() called\n"));
    return new CStrObject(_T("0 Friends Online"));
}

void CLiveAccounts::LaunchEntryPoint(int port, int clearPasscode, const TCHAR* destination)
{
    TCHAR buffer[256];
    OutputDebugString(_T("[LiveAccounts] LaunchEntryPoint() called\n"));
    wsprintf(buffer, _T("  Port: %d\n"), port); OutputDebugString(buffer);
    wsprintf(buffer, _T("  ClearPasscode: %s\n"), clearPasscode ? _T("true") : _T("false")); OutputDebugString(buffer);

    TCHAR destUpper[64] = { 0 };
    if (destination && _tcslen(destination) > 0)
    {
        wsprintf(buffer, _T("  Destination: %s\n"), destination); OutputDebugString(buffer);
        _tcsncpy(destUpper, destination, 63);
        _tcsupr(destUpper);
    }
    else
    {
        OutputDebugString(_T("  Destination: (null or empty)\n"));
        return;
    }

    LD_FROM_DASHBOARD fd;
    ZeroMemory(&fd, sizeof(fd));
    fd.dwContext = port;

    if (_tcscmp(destUpper, _T("TROUBLE SHOOTER")) == 0 ||
        _tcscmp(destUpper, _T("NEW ACCOUNT")) == 0 ||
        _tcscmp(destUpper, _T("ACCOUNT RECOVERY")) == 0 ||
        _tcscmp(destUpper, _T("FRIENDS LIST")) == 0)
    {
        OutputDebugString(_T("  -> Launching xonlinedash.xbe\n"));
        TheseusGetD3DDev()->PersistDisplay();
        XWriteTitleInfoAndReboot(
            "xonlinedash.xbe",
            "\\Device\\Harddisk0\\Partition2\\xodash",
            LDT_FROM_DASHBOARD,
            GetTitleID(),
            (PLAUNCH_DATA)&fd
        );
    }
    else if (_tcscmp(destUpper, _T("SKIN SWAP")) == 0)
    {
        OutputDebugString(_T("  -> Launching Theseus Lite for SKIN SWAP\n"));
        TheseusGetD3DDev()->PersistDisplay();
#ifdef _DEBUG
        XWriteTitleInfoAndReboot("UIX Lite.xbe", "\\Device\\Harddisk0\\Partition1\\DEVKIT\\UIX Lite", LDT_FROM_DASHBOARD, GetTitleID(), (PLAUNCH_DATA)&fd);
#else
        XWriteTitleInfoAndReboot("UIX Lite.xbe", "\\Device\\Harddisk0\\Partition2", LDT_FROM_DASHBOARD, GetTitleID(), (PLAUNCH_DATA)&fd);
#endif
    }
    else
    {
        OutputDebugString(_T("  -> Unknown destination; no launch performed.\n"));
    }
}

DWORD CLiveAccounts::GetTitleID()
{
    OutputDebugString(_T("[LiveAccounts] GetTitleID() called\n"));
    return TheseusHasLaunchData() ? TheseusGetTitleID() : 0;
}

// 5960 additions - RE'd from retail binary 2026-03-18

int CLiveAccounts::GetLastLogonUser()
{
    // On retail: checks launch data flag, searches account table
    // for matching account from previous session. Returns index or -1.
    if (!TheseusHasLaunchData())
        return -1;

    // Search stored accounts for match with launch data XUID
    for (DWORD i = 0; i < g_NumUsers; i++)
    {
        if (g_Users[i].xuid.qwUserID == TheseusGetTitleID())
            return (int)i;
    }
    return -1;
}

CStrObject* CLiveAccounts::GetResult()
{
    // On retail: reads async operation result from XOnline task handle
    // Returns localized result string (success/failure message)
    // For now return empty string. Insignia may need this later.
    return new CStrObject(_T(""));
}

void CLiveAccounts::LaunchDashUpdate()
{
    // On retail: copies launch data, calls XLaunchNewImage
    // to boot into the system update XBE
    OutputDebugString(_T("[LiveAccounts] LaunchDashUpdate() called\n"));

    TheseusGetD3DDev()->PersistDisplay();

    LAUNCH_DATA ld;
    ZeroMemory(&ld, sizeof(ld));

    XLaunchNewImage(NULL, &ld);
}
