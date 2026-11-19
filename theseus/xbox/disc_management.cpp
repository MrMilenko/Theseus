// disc_management.cpp: the DiscDrive node, disc type detection and
// Theseus's disc extraction helpers. The node and GetDiscType are rebuilt
// from the 5960 retail XBE (0x39d70..0x3a2b0); see docs/decomp/Disc.md.
// Tray control, the CD name lookups, the Discord relay and DiscManager
// are Theseus's.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "ntiosvc.h"
#include "runner.h"
#include "discord.h"
#include "xbe.h"
#include "config.h"
#include "harddrive.h"
#include "disc_manager.h"

#ifndef _countof
#define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif

extern CConfig *theConfig;

int GetDiscType();

// discType strings by DISC_ value (5960 table @ 0x0017453c).
static const TCHAR *rgszDiscType[] =
{
    _T("none"),
    _T("unknown"),
    _T("Title"),
    _T("Audio"),
    _T("Video"),
};

int g_nDiscType;

// Theseus: 5960 does this inline in CXApp::InitApp (0x0002d7e0).
void DiscDrive_Init()
{
    g_cdrom.Open(1);
    g_nDiscType = GetDiscType();
}

class CDiscDrive : public CNode
{
    DECLARE_NODE(CDiscDrive, CNode)
public:
    CDiscDrive();
    ~CDiscDrive();

    void Advance(float nSeconds);

    TCHAR *m_discType;

    int getTrackCount();
    CStrObject *FormatTotalTime();
    CStrObject *FormatTrackTime(int nTrack);

    void LaunchDisc();
    void OpenTray();
    void CloseTray();

    CStrObject *getArtist();
    CStrObject *getTitle();
    CStrObject *getTrackName(int nTrack);

    int m_nDiscType;
    bool m_locked;

    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()

    static CNodeArray c_drives;

private:
    ULONG m_trayState;
    bool m_bDeferNotification;

    void SetDiscTypeString();
    void NotifyDiscChange();
};

// Script events: OnDiscInserted, OnDiscRemoved.

CNodeArray CDiscDrive::c_drives;

IMPLEMENT_NODE("DiscDrive", CDiscDrive, CNode)

// 5960 PRD @ 0x00174550: discType (string, +0x1c), locked (bool, +0x24),
// bDeferNotification (bool, +0x2c). Scripts clear the deferred flag to drop
// a disc change that landed while the drive was locked.
START_NODE_PROPS(CDiscDrive, CNode)
NODE_PROP(pt_string, CDiscDrive, discType)
NODE_PROP(pt_boolean, CDiscDrive, locked)
NODE_PROP(pt_boolean, CDiscDrive, bDeferNotification)
END_NODE_PROPS()

#define _FND_CLASS CDiscDrive
START_NODE_FUN(CDiscDrive, CNode)
NODE_FUN_IV(getTrackCount)
NODE_FUN_SV(FormatTotalTime)
NODE_FUN_SI(FormatTrackTime)
NODE_FUN_VV(LaunchDisc)
NODE_FUN_VV(OpenTray)
NODE_FUN_VV(CloseTray)
NODE_FUN_SV(getArtist)
NODE_FUN_SV(getTitle)
NODE_FUN_SI(getTrackName)
END_NODE_FUN()
#undef _FND_CLASS

// Copies the name for m_nDiscType into the discType property.
void CDiscDrive::SetDiscTypeString()
{
    const TCHAR *szName = rgszDiscType[m_nDiscType];
    m_discType = new TCHAR[_tcslen(szName) + 1];
    _tcscpy(m_discType, szName);
}

void CDiscDrive::NotifyDiscChange()
{
    CallFunction(this, m_nDiscType != DISC_NONE ? _T("OnDiscInserted") : _T("OnDiscRemoved"));
}

// CDiscDrive::CDiscDrive()
// Reconstructed from 5960 retail XBE @ 0x00039d70: joins the drive list
// (and holds a reference), unlocked, nothing deferred. A dash started with
// launch data, or on a movie or unreadable disc, takes the boot disc type
// and the tray as they are. Otherwise it starts at "none" with an empty
// tray, so the first poll sees the disc and fires OnDiscInserted.
// A movie disc can't wait for that: playing one reboots, and it would loop.
CDiscDrive::CDiscDrive()
{
    m_locked = false;
    m_bDeferNotification = false;

    c_drives.AddNode(this);
    AddRef();

    if (TheseusHasLaunchData() || g_nDiscType == DISC_VIDEO || g_nDiscType == DISC_BAD)
    {
        m_nDiscType = g_nDiscType;
        HalReadSMCTrayState(&m_trayState, NULL);
    }
    else
    {
        m_nDiscType = DISC_NONE;
        m_trayState = SMC_TRAY_STATE_NO_MEDIA;
    }

    SetDiscTypeString();
}

// CDiscDrive::~CDiscDrive()
// Reconstructed from 5960 retail XBE @ 0x00039ec0:
//     c_drives.RemoveNode(this); if (c_drives.count == 0) c_drives.free();
//     delete m_discType;
CDiscDrive::~CDiscDrive()
{
    c_drives.RemoveNode(this);
    if (c_drives.GetLength() == 0)
        c_drives.RemoveAll();
    delete[] m_discType;
}

// CDiscDrive::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x00039f20. A change held back while
// locked fires as soon as the drive unlocks. Every 0.04 s (one clock shared
// by all drives) the SMC tray state is read; when it moves, the screen saver
// wakes and the disc is typed again. A new type renames discType and fires
// OnDiscInserted or OnDiscRemoved, or is deferred while locked.
void CDiscDrive::Advance(float nSeconds)
{
    if (m_bDeferNotification && !m_locked)
    {
        m_bDeferNotification = false;
        NotifyDiscChange();
    }

    static XTIME lastPoll = 0.0f;
    if (TheseusGetNow() - lastPoll < 0.04f)
        return;
    lastPoll = TheseusGetNow();

    ULONG trayState;
    if (!NT_SUCCESS(HalReadSMCTrayState(&trayState, NULL)) || trayState == m_trayState)
        return;
    m_trayState = trayState;

    ResetScreenSaver();

    g_nDiscType = GetDiscType();
    if (g_nDiscType == m_nDiscType)
        return;

    m_nDiscType = g_nDiscType;
    delete[] m_discType;
    SetDiscTypeString();

    if (m_locked)
        m_bDeferNotification = true;
    else
        NotifyDiscChange();
}

// CDiscDrive::getTrackCount()   [script fn 'getTrackCount', FND 0x0017fd48, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003a030:
//     return g_BootType == DISC_AUDIO ? g_cdrom.GetTrackCount() : 0;
int CDiscDrive::getTrackCount()
{
    return g_nDiscType == DISC_AUDIO ? g_cdrom.GetTrackCount() : 0;
}

// "mm:ss", or "" when there's nothing to time.
static CStrObject *FormatMinutesSeconds(bool bHave, int nMinutes, int nSeconds)
{
    if (!bHave)
        return new CStrObject;
    TCHAR szTime[8];
    _stprintf(szTime, _T("%02d:%02d"), nMinutes, nSeconds);
    return new CStrObject(szTime);
}

// CDiscDrive::FormatTotalTime()   [script fn 'FormatTotalTime', FND 0x0017fd60, sig 8]
// CDiscDrive::FormatTrackTime(n)  [script fn 'FormatTrackTime', FND 0x0017fd78, sig 4]
// Reconstructed from 5960 retail XBE @ 0x0003a060 and 0x0003a150: the disc's
// or track n's length as "%02d:%02d", empty with no disc or a bad track.
// The frame math is in CNtIoctlCdromService (see docs/decomp/NtIoSvc.md).
CStrObject *CDiscDrive::FormatTotalTime()
{
    int nMinutes, nSeconds;
    bool bHave = g_cdrom.GetTotalLength(&nMinutes, &nSeconds, NULL);
    return FormatMinutesSeconds(bHave, nMinutes, nSeconds);
}

CStrObject *CDiscDrive::FormatTrackTime(int nTrack)
{
    int nMinutes, nSeconds;
    bool bHave = g_cdrom.GetTrackLength(nTrack, &nMinutes, &nSeconds, NULL);
    return FormatMinutesSeconds(bHave, nMinutes, nSeconds);
}

// Theseus: the CD's names, empty until something fills them in.
static CStrObject *StrOrEmpty(const TCHAR *sz)
{
    return new CStrObject(sz != NULL ? sz : _T(""));
}

CStrObject *CDiscDrive::getArtist()
{
    return StrOrEmpty(g_cdrom.GetArtist());
}

CStrObject *CDiscDrive::getTitle()
{
    return StrOrEmpty(g_cdrom.GetTitle());
}

CStrObject *CDiscDrive::getTrackName(int nTrack)
{
    return StrOrEmpty(g_cdrom.GetTrackName(nTrack));
}

// CDiscDrive::LaunchDisc()   [script fn 'LaunchDisc', FND 0x0017fd90, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0003a260:
//     PersistDisplay();
//     if (g_BootType == DISC_TITLE) HalReturnToFirmware(HalRebootRoutine);
//     XLaunchNewImage(NULL, NULL);
// 5960 reboots into a game disc. Theseus launches d:\default.xbe directly
// instead, after a short settle and the Discord relay with its title ID.
void CDiscDrive::LaunchDisc()
{
    TheseusGetD3DDev()->PersistDisplay();

    Sleep(500);

    CStrObject *pTitleId = theConfig->GetXBETitleID(_T("D:"));
    if (pTitleId != NULL)
    {
        char szTitleId[11];
        Ansi(szTitleId, pTitleId->GetSz(), sizeof(szTitleId));
        SendDiscordRelayFromConfig(szTitleId);
        delete pTitleId;
    }

    XLaunchNewImage(g_nDiscType == DISC_TITLE ? "d:\\default.xbe" : NULL, NULL);
}

// Theseus: the tray, through the SMC's tray register.
void CDiscDrive::OpenTray()
{
    HalWriteSMBusValue(0x20, 0x0C, 0, 0x00);
    Sleep(1);
}

void CDiscDrive::CloseTray()
{
    HalWriteSMBusValue(0x20, 0x0C, 0, 0x01);
    Sleep(1);
}

// The files that mark each disc type, checked in order (5960 table @ 0x000201c8).
struct DISCTYPECHECK
{
    const TCHAR *szPath;
    const TCHAR *szDiscType;
    int nDiscType;
};

static const DISCTYPECHECK rgddc[] =
{
    {_T("CDROM0:\\default.xbe"), _T("Title"), DISC_TITLE},
    {_T("CDROM0:\\video_ts\\video_ts.ifo"), _T("Video"), DISC_VIDEO},
    {_T("CDROM0:\\track01.cda"), _T("Audio"), DISC_AUDIO},
};

// GetDiscType()
// Reconstructed from 5960 retail XBE @ 0x0003a2b0. Closes the CD service
// and dismounts \??\CdRom0: so the next look is fresh. If the SMC says the
// tray is settled (not still detecting): an empty tray is told apart from an
// unreadable disc by asking the drive (IOCTL_CDROM_CHECK_VERIFY; success or
// ERROR_UNRECOGNIZED_MEDIA means a bad disc), anything else is no disc.
// Otherwise an audio TOC makes it Audio, then the marker files decide; a
// lookup failing with anything but not found gets one retry after 100 ms.
// See docs/decomp/Disc.md for the instruction walk.
int GetDiscType()
{
    if (g_cdrom.IsOpen())
        g_cdrom.Close();

    OBJECT_STRING cdrom;
    RtlInitObjectString(&cdrom, "\\??\\CdRom0:");
    IoDismountVolumeByName(&cdrom);

    ULONG trayState;
    if (NT_SUCCESS(HalReadSMCTrayState(&trayState, NULL)) && trayState != SMC_TRAY_STATE_MEDIA_DETECT)
    {
        if (trayState != SMC_TRAY_STATE_NO_MEDIA)
            return DISC_NONE;

        HANDLE hDrive = CreateFileA("cdrom0:", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDrive == INVALID_HANDLE_VALUE || hDrive == NULL)
            return DISC_NONE;

        DWORD cbReturned;
        bool bBad = DeviceIoControl(hDrive, IOCTL_CDROM_CHECK_VERIFY, NULL, 0, NULL, 0, &cbReturned, NULL) ||
                    GetLastError() == ERROR_UNRECOGNIZED_MEDIA;
        CloseHandle(hDrive);
        return bBad ? DISC_BAD : DISC_NONE;
    }

    g_cdrom.Open(1);
    if (g_cdrom.IsOpen())
        return DISC_AUDIO;

    bool bMayRetry = true;
    int i = 0;
    while (i < (int)countof(rgddc))
    {
        if (DoesFileExist(rgddc[i].szPath))
            return rgddc[i].nDiscType;

        DWORD dwError = GetLastError();
        if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND && bMayRetry)
        {
            bMayRetry = false;
            Sleep(100);
            continue;
        }

        i++;
        bMayRetry = true;
    }

    return DISC_BAD;
}

// =========================================================================
// DiscManager: extraction, file copying, title reading.
// =========================================================================

#define COPY_BUFFER_SIZE 8192

// Forward declarations
extern void OverlayLog(const WCHAR *msg);
extern void OverlayLogf(const WCHAR *fmt, ...);

// Simple file entry
struct IsoFileEntry
{
    char fullPath[MAX_PATH];
    char relativePath[MAX_PATH];
    LARGE_INTEGER size;
    DWORD lba;
};

// Global state
static WCHAR g_TitleName[64] = L"<Unknown>";
static CHAR g_TitleFolder[128] = "F:\\Games\\Unknown\\";
static BOOL g_DiscPresent = FALSE;
static BOOL g_ExtractComplete = FALSE;

static float g_ExtractProgress = 0.0f;

CXBExecutable xbe;

// Forward declarations
static DWORD CountFilesInFolder(const char *path);
static bool CopySingleFile(const char *srcPath, const char *dstPath);

static void SanitizeTitleForFolderSafe(const WCHAR *title, CHAR *outAscii, size_t maxLen, DWORD fallbackTitleID)
{
    if (!outAscii || !title)
        return;

    CHAR temp[128] = {0};
    // Remove WC_NO_BEST_FIT_CHARS
    int result = WideCharToMultiByte(CP_ACP, 0, title, -1, temp, sizeof(temp) - 1, NULL, NULL);

    if (result == 0 || temp[0] == '\0')
    {
        // Fallback: use title ID as hex if conversion fails or result is empty
        sprintf(outAscii, "%08X", fallbackTitleID);
        return;
    }

    // Sanitize output for filesystem safety
    size_t j = 0;
    for (size_t i = 0; temp[i] != '\0' && j < maxLen - 1; ++i)
    {
        CHAR c = temp[i];
        if (c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
        {
            outAscii[j++] = '_';
        }
        else if ((unsigned char)c < 0x20) // Skip control chars
        {
            continue;
        }
        else
        {
            outAscii[j++] = c;
        }
    }

    outAscii[j] = '\0';

    if (j == 0)
    {
        // Still garbage? Fallback again.
        sprintf(outAscii, "%08X", fallbackTitleID);
    }
}

// =========================================================================
// DiscManager public interface
// =========================================================================

// Disc presence check
bool DiscManager::IsDiscPresent()
{
    g_DiscPresent = (GetFileAttributesA("D:\\default.xbe") != 0xFFFFFFFF);
    return g_DiscPresent;
}

bool DiscManager::ReadDiscTitle(WCHAR *outBuf, size_t maxLen)
{
    ZeroMemory(g_TitleName, sizeof(g_TitleName));
    ZeroMemory(g_TitleFolder, sizeof(g_TitleFolder));

    if (!g_DiscPresent)
        return false;

    if (!xbe.ReadFile("D:\\default.xbe", false, false))
        return false;
    unsigned long fallbackTitleID = xbe.TitleId();
    // Get wide title name from XBE
    MultiByteToWideChar(CP_ACP, 0, xbe.InternalName(), -1, g_TitleName, 64);
    g_TitleName[63] = 0;
    OutputDebugStringA("InternalName: ");
    OutputDebugStringA(xbe.InternalName() ? xbe.InternalName() : "<null>");
    OutputDebugStringA("\n");
    {
        char buf[32];
        _snprintf(buf, sizeof(buf) - 1, "TitleId: %08X\n", xbe.TitleId());
        buf[sizeof(buf) - 1] = '\0';
        OutputDebugStringA(buf);
    }
    // Sanitize and convert to ASCII folder-safe name
    CHAR folderSafeName[96] = {0};
    SanitizeTitleForFolderSafe(g_TitleName, folderSafeName, sizeof(folderSafeName), fallbackTitleID);

    if (folderSafeName[0] == '\0')
        sprintf(folderSafeName, "%08X", fallbackTitleID);

    // Create title folder path
    sprintf(g_TitleFolder, "F:\\Games\\%s\\", folderSafeName);

    if (outBuf && maxLen > 0)
        wcsncpy(outBuf, g_TitleName, maxLen - 1);

    {
        char buf[256];
        _snprintf(buf, sizeof(buf) - 1, "[DiscManager] Title: %S | Folder: %s\n", g_TitleName, g_TitleFolder);
        buf[sizeof(buf) - 1] = '\0';
        OutputDebugStringA(buf);
    }
    return true;
}

// Placeholder for estimating disc size
bool DiscManager::EstimateDiscSize(uint64_t *outSizeBytes)
{
    if (outSizeBytes)
        *outSizeBytes = 0;
    return true;
}

bool MoveFolder(const char *src, const char *dst)
{
    return MoveFileExA(src, dst, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
}

bool DeleteFolderRecursive(const char *path)
{
    CHAR searchPath[MAX_PATH];
    WIN32_FIND_DATA findData;
    HANDLE hFind;

    sprintf(searchPath, "%s*.*", path);
    hFind = FindFirstFile(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    do
    {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        CHAR fullPath[MAX_PATH];
        sprintf(fullPath, "%s%s", path, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            strcat(fullPath, "\\");
            DeleteFolderRecursive(fullPath);
            RemoveDirectory(fullPath);
        }
        else
        {
            DeleteFile(fullPath);
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return RemoveDirectory(path);
}

bool DiscManager::StartExtraction(const char *workingDir)
{
    {
        char buf[256];
        _snprintf(buf, sizeof(buf) - 1, "[DiscManager] Starting extraction from D:\\ to %s...\n", workingDir);
        buf[sizeof(buf) - 1] = '\0';
        OutputDebugStringA(buf);
    }
    WCHAR logBuf[256];
swprintf(logBuf, L"Starting extraction to: %S", workingDir);

    OverlayLog(logBuf);

    g_ExtractProgress = 0.0f;
    g_ExtractComplete = FALSE;

    // Step 1: Delete and recreate DiscDump folder
    if (!workingDir || strlen(workingDir) == 0)
    {
        OutputDebugStringA("[DiscManager] Invalid working directory specified!\n");
        return false;
    }
    DeleteFolderRecursive(workingDir);
    CreateDirectory(workingDir, NULL);

    // Step 2: Extract from disc to workingDir
    if (!CopyFolderRecursive("D:\\", workingDir, &g_ExtractProgress))
    {
        OutputDebugStringA("[DiscManager] Extraction failed!\n");
        return false;
    }

    // Step 3: Move from DiscDump to title folder
    if (!CreateDirectory(g_TitleFolder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        {
            char buf[256];
            _snprintf(buf, sizeof(buf) - 1, "[DiscManager] Failed to create title folder: %s\n", g_TitleFolder);
            buf[sizeof(buf) - 1] = '\0';
            OutputDebugStringA(buf);
        }
        return false;
    }
    MoveFolder(workingDir, g_TitleFolder);

    g_ExtractComplete = TRUE;
    OutputDebugStringA("[DiscManager] Extraction complete.\n");
    return true;
}

bool DiscManager::IsExtractionComplete()
{
    return g_ExtractComplete;
}

float DiscManager::GetExtractionProgress()
{
    if (g_ExtractProgress < 0.0f)
        return 0.0f;
    if (g_ExtractProgress > 1.0f)
        return 1.0f;
    return g_ExtractProgress;
}

const WCHAR *DiscManager::GetTitleName()
{
    return g_TitleName;
}

const CHAR *DiscManager::GetIsoTargetPath()
{
    static CHAR fullPath[256];
    sprintf(fullPath, "%sdefault.iso", g_TitleFolder);
    return fullPath;
}

const char *DiscManager::GetFinalExtractPath()
{
    return g_TitleFolder;
}

// =========================================================================
// Static helpers: file counting, copying, folder recursion.
// =========================================================================

// Recursively counts all files in a folder tree
static DWORD CountFilesInFolder(const char *path)
{
    CHAR searchPath[MAX_PATH];
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    DWORD count = 0;

    sprintf(searchPath, "%s*.*", path);
    hFind = FindFirstFile(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            CHAR subPath[MAX_PATH];
            sprintf(subPath, "%s%s\\", path, findData.cFileName);
            count += CountFilesInFolder(subPath);
        }
        else
        {
            count++;
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return count;
}

// Copies one file using buffered chunked IO
static bool CopySingleFile(const char *srcPath, const char *dstPath)
{
    HANDLE hSrc = CreateFile(srcPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hSrc == INVALID_HANDLE_VALUE)
        return false;

    HANDLE hDst = CreateFile(dstPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hDst == INVALID_HANDLE_VALUE)
    {
        CloseHandle(hSrc);
        return false;
    }

    BYTE *buffer = (BYTE *)LocalAlloc(LPTR, COPY_BUFFER_SIZE);
    if (!buffer)
    {
        CloseHandle(hSrc);
        CloseHandle(hDst);
        return false;
    }

    DWORD bytesRead = 0, bytesWritten = 0;
    BOOL success = TRUE;

    while (ReadFile(hSrc, buffer, COPY_BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0)
    {
        if (!WriteFile(hDst, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead)
        {
            success = FALSE;
            break;
        }
    }

    LocalFree(buffer);
    CloseHandle(hSrc);
    CloseHandle(hDst);
    return success;
}

// Recursively copies all files and folders with progress tracking
bool CopyFolderRecursive(const char *sourcePath, const char *destPath, float *progressOut)
{
    CHAR searchPath[MAX_PATH];
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    DWORD totalFiles = CountFilesInFolder(sourcePath);
    DWORD copiedFiles = 0;

    if (progressOut)
        *progressOut = 0.0f;
    CreateDirectory(destPath, NULL);

    sprintf(searchPath, "%s*.*", sourcePath);
    hFind = FindFirstFile(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    do
    {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        CHAR srcFull[MAX_PATH], dstFull[MAX_PATH];
        sprintf(srcFull, "%s%s", sourcePath, findData.cFileName);
        sprintf(dstFull, "%s%s", destPath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            strcat(srcFull, "\\");
            strcat(dstFull, "\\");

            CreateDirectory(dstFull, NULL);

            if (!CopyFolderRecursive(srcFull, dstFull, progressOut))
                return false;
        }
        else
        {
            if (!CopySingleFile(srcFull, dstFull))
                return false;

            // Compute relative path (D:\ is assumed to be sourcePath)
            const char *relativePath = srcFull + strlen(sourcePath);

            // Convert to wide string and log
            WCHAR wbuf[256];
            const char* fileName = srcFull + strlen(sourcePath);
MultiByteToWideChar(CP_ACP, 0, fileName, -1, wbuf, _countof(wbuf));
OverlayLogf(L"Extracting: %s", wbuf);

            copiedFiles++;
            if (progressOut && totalFiles > 0)
                *progressOut = (float)copiedFiles / (float)totalFiles;
        }

    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);

    if (progressOut)
        *progressOut = 1.0f;
    return true;
}

static BOOL CollectIsoFileList(const char *baseFolder, const char *currentPath, IsoFileEntry *list, DWORD *count, DWORD maxFiles)
{
    CHAR searchPath[MAX_PATH];
    WIN32_FIND_DATA findData;
    HANDLE hFind;

    sprintf(searchPath, "%s*.*", currentPath);
    hFind = FindFirstFile(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (!strcmp(findData.cFileName, ".") || !strcmp(findData.cFileName, ".."))
            continue;

        CHAR fullPath[MAX_PATH];
        sprintf(fullPath, "%s%s", currentPath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            _snprintf(fullPath, MAX_PATH - 1, "%s%s\\", currentPath, findData.cFileName);
            fullPath[MAX_PATH - 1] = '\0';

            if (!CollectIsoFileList(baseFolder, fullPath, list, count, maxFiles))
            {
                FindClose(hFind);
                return FALSE;
            }
        }
        else if (*count < maxFiles)
        {
            IsoFileEntry *entry = &list[(*count)++];
            strncpy(entry->fullPath, fullPath, MAX_PATH - 1);
            entry->fullPath[MAX_PATH - 1] = '\0';

            ULARGE_INTEGER size;
            size.HighPart = findData.nFileSizeHigh;
            size.LowPart = findData.nFileSizeLow;
            entry->size.QuadPart = size.QuadPart;

            const char *relStart = fullPath + strlen(baseFolder);
            if (relStart >= fullPath + MAX_PATH)
                relStart = fullPath; // fallback

            strncpy(entry->relativePath, relStart, MAX_PATH - 1);
            entry->relativePath[MAX_PATH - 1] = '\0'; // ensure null-termination
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return TRUE;
}
