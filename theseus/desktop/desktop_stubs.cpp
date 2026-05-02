// desktop_stubs.cpp: link-time stubs for symbols defined only in
// the Xbox build path. Bridges the missing references so the
// desktop binary links cleanly.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "titlecollection.h"
#include "discmanager.h"

// g_titles - from TitleCollection.cpp (removed)
CTitleArray g_titles[9];
CTitleArray::CTitleArray() {}
CTitleArray::~CTitleArray() {}
void CTitleArray::Update() {}
void CTitleArray::DeleteAll(bool) {}
void CTitleArray::SetRoot(TCHAR, bool) {}
int CTitleArray::GetTitleCount() { return 0; }
int CTitleArray::GetTitleCount2() { return 0; }
bool CTitleArray::IsBroken(int) { return false; }
const TCHAR* CTitleArray::GetTitleID(int) { return _T(""); }
const TCHAR* CTitleArray::GetTitleName(int) { return _T(""); }
const TCHAR* CTitleArray::GetTitleName2(int) { return _T(""); }
int CTitleArray::GetTitleTotalBlocks(int, HANDLE) { return 0; }
int CTitleArray::GetSavedGameCount(int, HANDLE) { return 0; }

// CTitleArray::IsValid - from TitleCollection.cpp (removed)
bool CTitleArray::IsValid() const { return false; }

// disc.cpp stubs (removed)
int GetDiscType() { return 0; }
void DiscDrive_Init() {}

// Discord stubs - from Discord.cpp (removed)
bool g_DiscordEnabled = false;
char g_DiscordIP[64] = {};
int g_DiscordPort = 0;

// TheseusMessageBox stub
static int s_nMessageBoxCount = 0;
int TheseusMessageBox(const TCHAR* szText, UINT uType) {
    if (s_nMessageBoxCount < 10) {
        fprintf(stderr, "[MessageBox] %s\n", szText);
        fflush(stderr);
        s_nMessageBoxCount++;
        if (s_nMessageBoxCount == 10)
            fprintf(stderr, "[MessageBox] (suppressing further messages)\n");
    }
    return 0;
}

// TitleArray_Init stub - from TitleCollection.cpp (removed)
void TitleArray_Init() {}

// NewFailed is now implemented in memutil.cpp

// XCDROM_TOC stubs removed; type no longer exists in desktop build.

// DiscManager stubs - from DiscManager.cpp (removed)
bool DiscManager::IsDiscPresent() { return false; }
bool DiscManager::ReadDiscTitle(WCHAR*, size_t) { return false; }
bool DiscManager::EstimateDiscSize(uint64_t*) { return false; }
bool DiscManager::StartExtraction(const char*) { return false; }
bool DiscManager::IsExtractionComplete() { return false; }
float DiscManager::GetExtractionProgress() { return 0.0f; }
const CHAR* DiscManager::GetFinalExtractPath() { return ""; }
bool DiscManager::BuildXISO(const char*, const char*) { return false; }
bool DiscManager::IsBuildComplete() { return false; }
float DiscManager::GetBuildProgress() { return 0.0f; }
const WCHAR* DiscManager::GetTitleName() { return L""; }
const CHAR* DiscManager::GetIsoTargetPath() { return ""; }

// driveManager stubs - from toolbox/driveManager.cpp (removed)
// Can't include driveManager.h (pulls xtl.h via Drive.h), so declare directly
class driveManager {
public:
    static bool getTotalNumberOfBytes(const char* mountPoint, uint64_t& totalSize);
    static bool getTotalFreeNumberOfBytes(const char* mountPoint, uint64_t& totalFree);
};
bool driveManager::getTotalNumberOfBytes(const char*, uint64_t& totalSize) { totalSize = 0; return false; }
bool driveManager::getTotalFreeNumberOfBytes(const char*, uint64_t& totalFree) { totalFree = 0; return false; }

// Discord stubs - from Discord.cpp (removed)
void InitDiscordConfig() {}
bool IsDiscordRelayEnabled() { return false; }
bool SendDiscordRelayFromConfig(const char*) { return false; }

// CXBExecutable stubs - from xbe.cpp (removed)
#include "xbe.h"
#include "virtual_games.h"
CXBExecutable::CXBExecutable() {
    memset(m_szInternalName, 0, sizeof(m_szInternalName));
    memset(m_szTitleName, 0, sizeof(m_szTitleName));
    memset(m_szFileName, 0, sizeof(m_szFileName));
    m_ulTitleId = 0;
    m_ulMediaFlag = 0;
    m_ulGameRegion = 0;
    m_pTitleImageTexture = NULL;
    m_iHeaderSize = 0;
    m_pHeader = NULL;
    m_iImageSize = 0;
    m_pImage = NULL;
    memset(&m_XBEInfo, 0, sizeof(m_XBEInfo));
}
CXBExecutable::~CXBExecutable() {}
int CXBExecutable::Clear() { return 0; }
int CXBExecutable::ReadFile(const char* szFileName, const bool, const bool) {
    if (!szFileName) return 0;
    strncpy(m_szFileName, szFileName, sizeof(m_szFileName) - 1);

    // Try to open the real file first
    const char* translated = XboxFS_TranslatePath(szFileName);
    FILE* f = fopen(translated, "rb");
    if (f) {
        // Real XBE file exists; read header for title info.
        fclose(f);
        return 0; // stub: don't actually parse, but at least we know it's real
    }

    // No real file; check if this is a virtual game.
    // Path looks like: xboxfs/E/Games/Name/default.xbe; strip the filename.
    char folderPath[512];
    strncpy(folderPath, translated, sizeof(folderPath) - 1);
    folderPath[sizeof(folderPath) - 1] = 0;
    char* lastSlash = strrchr(folderPath, '/');
    if (lastSlash) *lastSlash = 0;

    int vgIdx = VGames_MatchFolder(folderPath);
    if (vgIdx >= 0) {
        VirtualGame& vg = g_vgames.games[vgIdx];
        strncpy(m_szInternalName, vg.name, sizeof(m_szInternalName) - 1);
        strncpy(m_szTitleName, vg.name, sizeof(m_szTitleName) - 1);
        if (vg.titleID[0]) {
            m_ulTitleId = strtoul(vg.titleID, NULL, 16);
        }
        // Set magic so Valid() returns true
        m_XBEInfo.Header.magic = _XBE_HEADER_MAGIC;
    }
    return 0;
}

