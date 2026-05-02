// xbox_live.cpp: XboxLive_* C-style API. Brings up the XOnline
// subsystem on Xbox (keys, logon, friends, status poll loop) and
// exposes it to the dashboard UI. Theseus-original glue around the
// public XOnline APIs.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include <ntos.h>
#include "xcrypt.h"
#include <stddef.h>
#include <xonlinep.h>

static BYTE g_XboxHDKey[16] = {0};

#define SECTOR_SIZE 512
#define LIVE_ACCOUNT_START_SECTOR 0x1300

#define LIVE_ACCOUNT_STRUCT_SIZE sizeof(ONLINE_USER_ACCOUNT_STRUCT)
#define MAX_PROFILES 8
#define LIVE_ACCOUNT_SLOT_SIZE 0x6C
#define XC_SERVICE_DES3_CIPHER 1
#define XC_SERVICE_DECRYPT     0

#pragma pack(push, 1)
typedef struct
{
    unsigned long long XUID;     // 0x00
    unsigned int unknown;        // 0x08
    char Gamertag[0x10];         // 0x0C
    unsigned int Flags;          // 0x1C
    unsigned char Passcode[4];   // 0x20
    char Domain[0x14];           // 0x24
    char Realm[0x18];            // 0x38
    unsigned char Confounder[0x14];     // 0x50
    unsigned char Verification[8];      // 0x64
    BYTE padding[0x80 - 0x6C];   // 0x6C to 0x80
} ONLINE_USER_ACCOUNT_STRUCT;
#pragma pack(pop)
static bool VerifyOnlineUserSignature(ONLINE_USER_ACCOUNT_STRUCT* Account);
class CXboxLive : public CNode
{
    DECLARE_NODE(CXboxLive, CNode)

public:
    CXboxLive();
    ~CXboxLive();

    bool LoadFromRawDrive();
    CStrObject *GetGamertag();
    CStrObject *GetXUID();
    CStrObject *GetDomain();
    CStrObject *GetRealm();
    int IsVerified();

protected:
    DECLARE_NODE_FUNCTIONS();

public:
    ONLINE_USER_ACCOUNT_STRUCT m_account;
    bool m_verified;
};

IMPLEMENT_NODE("XboxLive", CXboxLive, CNode)

#define _FND_CLASS CXboxLive
START_NODE_FUN(CXboxLive, CNode)
NODE_FUN_SV(GetGamertag)
NODE_FUN_SV(GetXUID)
NODE_FUN_SV(GetDomain)
NODE_FUN_SV(GetRealm)
NODE_FUN_IV(IsVerified)
END_NODE_FUN()
#undef _FND_CLASS

// Debug print helper
void DebugPrint(const TCHAR *fmt, ...)
{
    TCHAR buffer[512];
    va_list args;
    va_start(args, fmt);
    _vsntprintf(buffer, countof(buffer) - 1, fmt, args);
    va_end(args);
    buffer[countof(buffer) - 1] = 0;
    OutputDebugString(buffer);
}
bool LoadXboxHDKey()
{
    memcpy(g_XboxHDKey, XboxHDKey, 16);
    return true;
}
CXboxLive::CXboxLive()
{
    ZeroMemory(&m_account, sizeof(m_account));
    m_verified = false;

    DebugPrint(_T("[XboxLive] Init XboxLive\n"));

    if (!LoadXboxHDKey())
        DebugPrint(_T("[XboxLive] Failed to load XboxHDKey\n"));

    LoadFromRawDrive();
}


CXboxLive::~CXboxLive() {}

static void PrintBytes(const char* label, const void* data, int length)
{
    DebugPrint(_T("[XboxLive] %hs:\n"), label);

    const BYTE* bytes = (const BYTE*)data;
    for (int i = 0; i < length; ++i)
    {
        DebugPrint(_T("%02X "), bytes[i]);
        if ((i + 1) % 16 == 0)
            DebugPrint(_T("\n"));
    }

    if (length % 16 != 0)
        DebugPrint(_T("\n"));
}

static bool VerifyOnlineUserSignature(ONLINE_USER_ACCOUNT_STRUCT* Account)
{
    static unsigned char seed_key_a[16] = {
        0x2B, 0xB8, 0xD9, 0xEF, 0xD2, 0x04, 0x6D, 0x9D,
        0x1F, 0x39, 0xB1, 0x5B, 0x46, 0x58, 0x01, 0xD7
    };
    static unsigned char seed_key_b[16] = {
        0x1E, 0x05, 0xD7, 0x3A, 0xA4, 0x20, 0x6A, 0x7B,
        0xA0, 0x5B, 0xCD, 0xDF, 0xAD, 0x26, 0xD3, 0xDE
    };
    static unsigned char iv[8] = {
        0x7B, 0x35, 0xA8, 0xB7, 0x27, 0xED, 0x43, 0x7A
    };

    const unsigned char* seed_data = g_XboxHDKey;
    const unsigned char* auth_key  = g_XboxHDKey;

    DebugPrint(_T("[XboxLive] ===== Begin Profile Verification =====\n"));

    // Dump XboxHDKey used for HMAC/3DES
    PrintBytes("XboxHDKey (EEPROM)", g_XboxHDKey, 16);

    // Dump full raw account struct
    PrintBytes("Raw Account Struct", (BYTE*)Account, 0x6C);

    // Build 3DES key
    unsigned char tempA[20], tempB[20], desKey[24], parityKey[24], keyTable[XC_SERVICE_DES3_TABLESIZE];
    XcHMAC((PUCHAR)seed_key_a, 16, (PUCHAR)seed_data, 16, NULL, 0, tempA);
    XcHMAC((PUCHAR)seed_key_b, 16, (PUCHAR)seed_data, 16, NULL, 0, tempB);
    memcpy(desKey,     tempA, 4);
    memcpy(desKey + 4, tempB, 20);
    memcpy(parityKey,  desKey, 24);
    XcDESKeyParity(parityKey, 24);
    XcKeyTable(XC_SERVICE_DES3_CIPHER, keyTable, parityKey);

    // Decrypt confounder at offset 0x50
    unsigned char encrypted[16], decrypted[16];
    memcpy(encrypted, &((BYTE*)Account)[0x50], 16);
    XcBlockCryptCBC(XC_SERVICE_DES3_CIPHER, 16, decrypted, encrypted, keyTable, XC_SERVICE_DECRYPT, (PUCHAR)iv);
    PrintBytes("Decrypted Confounder", decrypted, 16);

    // Compute HMAC digest over 0x00 to 0x63
    unsigned char raw[0x6C];
    memcpy(raw, Account, 0x6C);
    unsigned char digest[20];
    XcHMAC((PUCHAR)auth_key, 16, raw, 0x64, NULL, 0, digest);
    PrintBytes("Stored Verification", &raw[0x64], 8);
    PrintBytes("Computed Verification", digest, 8);

    // Validation checks
    if (memcmp(digest, &raw[0x64], 8) != 0) {
        DebugPrint(_T("[XboxLive] FAIL: Digest mismatch\n"));
        return false;
    }

    if (*(DWORD*)&raw[0x08] != 0) {
        DebugPrint(_T("[XboxLive] FAIL: Reserved DWORD at 0x08 is non-zero\n"));
        return false;
    }

    if (raw[0x1B] != 0) {
        DebugPrint(_T("[XboxLive] FAIL: Gamertag not null-terminated (0x1B)\n"));
        return false;
    }

    if (raw[0x37] != 0) {
        DebugPrint(_T("[XboxLive] FAIL: Domain not null-terminated (0x37)\n"));
        return false;
    }

    if (raw[0x4F] != 0) {
        DebugPrint(_T("[XboxLive] FAIL: Realm not null-terminated (0x4F)\n"));
        return false;
    }

    if ((raw[0x1F] & 0xF) != 0) {
        DebugPrint(_T("[XboxLive] FAIL: Reserved flags in 0x1F are non-zero\n"));
        return false;
    }

    DebugPrint(_T("[XboxLive] PASS: Profile is valid\n"));
    return true;
}



bool CXboxLive::LoadFromRawDrive()
{
    OutputDebugString(_T("[XboxLive] Trying XOnlineGetUsersFromHD...\n"));

    XONLINE_USER users[XONLINE_MAX_STORED_ONLINE_USERS] = {0};
    DWORD userCount = 0;

    HRESULT xr = XOnlineStartup(NULL);
    if (FAILED(xr))
    {
        DebugPrint(_T("[XboxLive] XOnlineStartup failed: 0x%08X\n"), xr);
        return false;
    }

    HRESULT hr = _XOnlineGetUsersFromHD(users, &userCount);
    if (FAILED(hr) || userCount == 0)
    {
        char msg[128];
        sprintf(msg, "[XboxLive] XOnlineGetUsersFromHD failed or returned no users. HRESULT: 0x%08X, Count: %lu\n", hr, userCount);
        OutputDebugStringA(msg);
        return false;
    }

    // Use first user (you can enhance this to allow multi-user selection)
    const XONLINE_USER& user = users[0];

    ZeroMemory(&m_account, sizeof(m_account));
    m_account.XUID = user.xuid.qwUserID;
    memcpy(m_account.Gamertag, user.name, sizeof(user.name));
    m_verified = true;

    char msg[128];
    sprintf(msg, "[XboxLive] Retrieved profile: %s (XUID: %llu)\n", m_account.Gamertag, m_account.XUID);
    OutputDebugStringA(msg);

    return true;
}


CStrObject *CXboxLive::GetGamertag()
{
#ifdef _UNICODE
    TCHAR wideGamertag[sizeof(m_account.Gamertag)];
    Unicode(wideGamertag, m_account.Gamertag, sizeof(m_account.Gamertag));
    return new CStrObject(wideGamertag);
#else
    return new CStrObject(m_account.Gamertag);
#endif
}

CStrObject *CXboxLive::GetXUID()
{
    TCHAR buffer[32];
    _sntprintf(buffer, countof(buffer), _T("%I64u"), m_account.XUID);
    return new CStrObject(buffer);
}

CStrObject *CXboxLive::GetDomain()
{
#ifdef _UNICODE
    TCHAR wideDomain[sizeof(m_account.Domain)];
    Unicode(wideDomain, m_account.Domain, sizeof(m_account.Domain));
    return new CStrObject(wideDomain);
#else
    return new CStrObject(m_account.Domain);
#endif
}

CStrObject *CXboxLive::GetRealm()
{
#ifdef _UNICODE
    TCHAR wideRealm[sizeof(m_account.Realm)];
    Unicode(wideRealm, m_account.Realm, sizeof(m_account.Realm));
    return new CStrObject(wideRealm);
#else
    return new CStrObject(m_account.Realm);
#endif
}

int CXboxLive::IsVerified()
{
    return m_verified ? 1 : 0;
}

