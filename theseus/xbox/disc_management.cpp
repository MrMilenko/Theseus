// disc_management.cpp: combined disc detection, tray state, CDiscDrive
// node, and disc extraction / file-copy / title-reading helpers.
// Decompiled from the 5960 retail XBE.

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

// =========================================================================
// Disc type table
// =========================================================================

int GetDiscType();

static const TCHAR *rgszDiscType[] =
    {
        _T("none"),
        _T("unknown"),
        _T("Title"),
        _T("Audio"),
        _T("Video"),
};

int g_nDiscType;

void DiscDrive_Init()
{
    g_cdrom.Open(1);
    g_nDiscType = GetDiscType();
}

// =========================================================================
// CDiscDrive node
// =========================================================================

class CDiscDrive : public CNode
{
    DECLARE_NODE(CDiscDrive, CNode)
public:
    CDiscDrive();
    ~CDiscDrive();

    void Advance(float nSeconds);

    TCHAR *m_discType; // none, unknown, Audio, Video, Photo, Title

    // Audio CD (CDDA) Information...
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
};

// Events:
//
//  OnDiscInserted
//  OnDiscRemoved

CNodeArray CDiscDrive::c_drives;

IMPLEMENT_NODE("DiscDrive", CDiscDrive, CNode)

START_NODE_PROPS(CDiscDrive, CNode)
NODE_PROP(pt_string, CDiscDrive, discType)
NODE_PROP(pt_boolean, CDiscDrive, locked)
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

CDiscDrive::CDiscDrive() : m_locked(false),
                           m_bDeferNotification(false)
{
    c_drives.AddNode(this);
    AddRef();

    //
    // Mark the disc type to DISC_NONE if it's not a DVD movie (bug 9014)
    // so that Advance will catch change notification and reboot to title.
    // For DVD movies, we have to mark as DISC_VIDEO because the way the
    // dash plays DVD is to reboot. If we let Advance to catch notification
    // the dash will end up reboot infinitely.
    //

    if (TheseusHasLaunchData() || g_nDiscType == DISC_VIDEO || g_nDiscType == DISC_BAD)
    {
        m_nDiscType = g_nDiscType;
        HalReadSMCTrayState(&m_trayState, 0);
    }
    else
    {
        m_nDiscType = DISC_NONE;
        m_trayState = SMC_TRAY_STATE_NO_MEDIA;
    }

    const TCHAR *szDiscType = rgszDiscType[m_nDiscType];

    m_discType = new TCHAR[_tcslen(szDiscType) + 1];
    _tcscpy(m_discType, szDiscType);

    TRACE(_T("CDiscDrive: %s\n"), szDiscType);
}

CDiscDrive::~CDiscDrive()
{
    c_drives.RemoveNode(this);
    if (c_drives.GetLength() == 0)
        c_drives.RemoveAll();

    delete[] m_discType;
}

void CDiscDrive::Advance(float nSeconds)
{
    // Check if we need to reboot
    if (m_bDeferNotification && !m_locked)
    {
        m_bDeferNotification = false;
        if (m_nDiscType != DISC_NONE)
            CallFunction(this, _T("OnDiscInserted"));
        else
            CallFunction(this, _T("OnDiscRemoved"));
    }

    static XTIME lastPoll = 0.0f;
    if (TheseusGetNow() - lastPoll < 0.04f)
        return;
    lastPoll = TheseusGetNow();

    // Poll state of the DVD tray by reading from SMC
    NTSTATUS Status;
    ULONG TrayState;

    Status = HalReadSMCTrayState(&TrayState, NULL);
    if (!NT_SUCCESS(Status))
        return;

    if (TrayState == m_trayState)
        return;
    m_trayState = TrayState;

    // Reset screen saver if tray state changes
    ResetScreenSaver();

    g_nDiscType = GetDiscType();

    if (m_nDiscType != g_nDiscType)
    {
        TRACE(_T("\001Disc type: %s\n"), rgszDiscType[g_nDiscType]);
        m_nDiscType = g_nDiscType;

        delete[] m_discType;
        m_discType = new TCHAR[_tcslen(rgszDiscType[m_nDiscType]) + 1];
        _tcscpy(m_discType, rgszDiscType[m_nDiscType]);

        if (!m_locked)
        {
            if (m_nDiscType != DISC_NONE)
                CallFunction(this, _T("OnDiscInserted"));
            else
                CallFunction(this, _T("OnDiscRemoved"));
        }
        else
        {
            m_bDeferNotification = true;
        }
    }
}

// =========================================================================
// Audio CD helpers
// =========================================================================

int CDiscDrive::getTrackCount()
{
    if (g_nDiscType == DISC_AUDIO)
        return g_cdrom.GetTrackCount();

    return 0;
}

CStrObject *CDiscDrive::FormatTotalTime()
{
    int nMinutes, nSeconds;
    if (!g_cdrom.GetTotalLength(&nMinutes, &nSeconds, NULL))
        return new CStrObject; // empty string

    TCHAR szBuf[8];
    _stprintf(szBuf, _T("%02d:%02d"), nMinutes, nSeconds);
    return new CStrObject(szBuf);
}

CStrObject *CDiscDrive::FormatTrackTime(int nTrack)
{
    int nMinutes, nSeconds;
    if (!g_cdrom.GetTrackLength(nTrack, &nMinutes, &nSeconds, NULL))
        return new CStrObject; // empty string

    TCHAR szBuf[8];
    _stprintf(szBuf, _T("%02d:%02d"), nMinutes, nSeconds);
    return new CStrObject(szBuf);
}

CStrObject *CDiscDrive::getArtist()
{
    const TCHAR *szArtist = g_cdrom.GetArtist();
    if (szArtist == NULL)
        szArtist = _T("");
    return new CStrObject(szArtist);
}

CStrObject *CDiscDrive::getTitle()
{
    const TCHAR *szTitle = g_cdrom.GetTitle();
    if (szTitle == NULL)
        szTitle = _T("");
    return new CStrObject(szTitle);
}

CStrObject *CDiscDrive::getTrackName(int nTrack)
{
    const TCHAR *szTrackName = g_cdrom.GetTrackName(nTrack);
    if (szTrackName == NULL)
        szTrackName = _T("");
    return new CStrObject(szTrackName);
}

// =========================================================================
// Disc launch
// =========================================================================

void CDiscDrive::LaunchDisc()
{
    TRACE(_T("[Discord] CDiscDrive::LaunchDisc() triggered\n"));
    TheseusGetD3DDev()->PersistDisplay();
    ASSERT(g_nDiscType != DISC_AUDIO);

    Sleep(500); // optional delay, since we have CCI's and ISO's loading.
    OutputDebugString(_T("[Discord] Attempting to read title ID from \\Device\\CdRom0\\\n"));

    CStrObject *titleIdStr = theConfig->GetXBETitleID(_T("D:"));
    if (titleIdStr)
    {
        const TCHAR *szTitleId = titleIdStr->GetSz();
        char szAnsiTitleId[11]; // 8 hex digits + null terminator
        Ansi(szAnsiTitleId, szTitleId, sizeof(szAnsiTitleId));
        // Report TitleID to OutputDebugString
        TCHAR szDebugMsg[64];
        _stprintf(szDebugMsg, _T("[Discord] Title ID: %s\n"), szTitleId);
        OutputDebugString(szDebugMsg);

        OutputDebugString(_T("[Discord] Sending relay...\n"));
        SendDiscordRelayFromConfig(szAnsiTitleId);

        delete titleIdStr;
    }
    else
    {
        OutputDebugString(_T("[Discord] Discord disabled. Launching normally.\n"));
    }


    XLaunchNewImage(g_nDiscType == DISC_TITLE ? "d:\\default.xbe" : NULL, NULL);
}

// =========================================================================
// Tray control
// =========================================================================

void CDiscDrive::OpenTray()
{
    // Tray Open
    HalWriteSMBusValue(0x20, 0x0C, 0, 0x00);
    Sleep(1);
}

void CDiscDrive::CloseTray()
{
    // Tray Close
    HalWriteSMBusValue(0x20, 0x0C, 0, 0x01);
    Sleep(1);
}

// =========================================================================
// GetDiscType: detect what kind of media is in the drive.
// =========================================================================

struct DISCTYPECHECK
{
    TCHAR *szPath;
    TCHAR *szDiscType;
    int nDiscType;
};

static const DISCTYPECHECK rgddc[] =
    {
        {_T("CDROM0:\\default.xbe"), _T("Title"), DISC_TITLE},
        {_T("CDROM0:\\video_ts\\video_ts.ifo"), _T("Video"), DISC_VIDEO},
        {_T("CDROM0:\\track01.cda"), _T("Audio"), DISC_AUDIO},
};

int GetDiscType()
{
    int nDiscType = DISC_BAD;
    bool bRetry = true;

    if (g_cdrom.IsOpen())
        g_cdrom.Close();

    OBJECT_STRING DeviceName;
    RtlInitObjectString(&DeviceName, "\\??\\CdRom0:");
    IoDismountVolumeByName(&DeviceName);

    NTSTATUS Status;
    ULONG TrayState;
    Status = HalReadSMCTrayState(&TrayState, NULL);
    if (NT_SUCCESS(Status) && TrayState != SMC_TRAY_STATE_MEDIA_DETECT)
    {
        if (TrayState == SMC_TRAY_STATE_NO_MEDIA)
        {
            HANDLE hDevice;

            // At this point, the drive has told the SMC that media could not be
            // detected.  To decide whether this means that the tray is empty
            // versus the tray have unrecognized media, we need to send an IOCTL
            // to the device.

            hDevice = CreateFileA("cdrom0:", GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

            if (hDevice != NULL)
            {
                BOOL fReturn;
                DWORD cbReturned;

                fReturn = DeviceIoControl(hDevice, IOCTL_CDROM_CHECK_VERIFY,
                                          NULL, 0, NULL, 0, &cbReturned, NULL);

                // If the device reports back that the unit is ready (which it
                // shouldn't since the SMC thinks the tray is empty) or if the
                // media is unrecognized, then the disc is bad.
                if (fReturn || (GetLastError() == ERROR_UNRECOGNIZED_MEDIA))
                {
                    CloseHandle(hDevice);
                    return DISC_BAD;
                }

                CloseHandle(hDevice);
            }
        }

        return DISC_NONE;
    }

    g_cdrom.Open(1);

    if (g_cdrom.IsOpen())
        return DISC_AUDIO;

    for (int i = 0; i < sizeof(rgddc) / sizeof(DISCTYPECHECK);)
    {
        if (DoesFileExist(rgddc[i].szPath))
        {
            nDiscType = rgddc[i].nDiscType;
            break;
        }

        DWORD dwError = GetLastError();

        if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND)
        {
            TRACE(_T("\001Error %d on %s\n"), dwError, rgddc[i].szPath);

            if (bRetry)
            {
                TRACE(_T("Retrying...\n"));
                bRetry = false;
                Sleep(100);
                continue;
            }
        }

        i += 1;
        bRetry = true;
    }

    return nDiscType;
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
