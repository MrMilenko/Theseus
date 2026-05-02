// theseus_launcher.cpp: CTheseusLauncher XAP node. Script-callable
// title launcher. Supports XBE paths, .uixshortcut files, and
// virtual entries from games.ini (VGames). Theseus-original.

#include "theseus_launcher.h"

IMPLEMENT_NODE("TheseusLauncher", CTheseusLauncher, CNode)
#define _FND_CLASS CTheseusLauncher
START_NODE_FUN(CTheseusLauncher, CNode)
NODE_FUN_VS(Launch)
END_NODE_FUN()
#undef _FND_CLASS

CTheseusLauncher::CTheseusLauncher()
{
}

CTheseusLauncher::~CTheseusLauncher()
{
}
void CTheseusLauncher::Launch(const TCHAR *szPath)
{
    char szAnsi[MAX_PATH];

#ifdef UNICODE
    WideCharToMultiByte(CP_ACP, 0, szPath, -1, szAnsi, MAX_PATH, NULL, NULL);
#else
    strncpy(szAnsi, szPath, MAX_PATH);
    szAnsi[MAX_PATH - 1] = '\0';
#endif

    // Print debug message with full path
    char szDebug[MAX_PATH + 64];
    sprintf(szDebug, "TheseusLauncher: Requested launch of [%s]\n", szAnsi);
    OutputDebugStringA(szDebug);

    if (EndsWith(szAnsi, ".xbe"))
    {
        LaunchXBE(szAnsi);
        return;
    }

    if (EndsWith(szAnsi, ".iso") || EndsWith(szAnsi, ".cci"))
    {
        LaunchWithAttach(szAnsi);
        return;
    }

    OutputDebugStringA("TheseusLauncher: Unsupported file type.\n");
}

bool CTheseusLauncher::LaunchXBE(const char *szPath)
{
    OutputDebugString(L"TheseusLauncher: Launching XBE...\n");
    return XLaunchNewImage(szPath, NULL) == S_OK;
}

bool CTheseusLauncher::LaunchWithAttach(const char *szISOPath)
{
    const bool isCCI = EndsWith(szISOPath, ".cci");
    const USHORT build = XboxKrnlVersion->Build;

    const char *path = szISOPath;
    const char *file = strrchr(szISOPath, '\\');
    if (file)
        file++;
    else
        file = szISOPath;

    if (build >= 8008)
    {

        OutputDebugString(L"TheseusLauncher: Detected Cerbios\n");
        return AttachCerbios(path, file, isCCI);
    }
    else
    {
        OutputDebugString(L"TheseusLauncher: Legacy BIOS detected\n");
        return AttachLegacy(path, file, build);
    }
}

bool CTheseusLauncher::AttachCerbios(const char *path, const char *file, bool isCCI)
{
    void *membuf = NULL;
    ULONG membuf_size = 1024 * 1024;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(&membuf, 0, &membuf_size, MEM_COMMIT | MEM_NOZERO, PAGE_READWRITE)))
        return false;

    ATTACH_SLICE_DATA_CERBIOS *asd = (ATTACH_SLICE_DATA_CERBIOS *)membuf;
    memset(asd, 0, sizeof(ATTACH_SLICE_DATA_CERBIOS));
    asd->DeviceType = isCCI ? 'd' : 'D';

    char *strbuf = (char *)membuf + sizeof(ATTACH_SLICE_DATA_CERBIOS);
    membuf_size -= sizeof(ATTACH_SLICE_DATA_CERBIOS);

    // Mount Point is always \Device\CdRom0
    _snprintf(strbuf, membuf_size, "\\Device\\CdRom0");
    RtlInitAnsiString(&asd->MountPoint, strbuf);
    asd->MountPoint.MaximumLength = asd->MountPoint.Length + 1;
    strbuf += asd->MountPoint.MaximumLength;
    membuf_size -= asd->MountPoint.MaximumLength;

    // Correct full path to ISO/CCI file
    _snprintf(strbuf, membuf_size, "%s", path);
    RtlInitAnsiString(&asd->SliceFile[0], strbuf);
    asd->SliceFile[0].MaximumLength = asd->SliceFile[0].Length + 1;
    asd->SliceCount = 1;

    // Debug
    OutputDebugStringA("TheseusLauncher: Cerbios full file path: ");
    OutputDebugStringA(strbuf);
    OutputDebugStringA("\r\n");

    // Open virtual device
    ANSI_STRING dev_name;
    RtlInitAnsiString(&dev_name, "\\Device\\Virtual0\\Image0");
    OBJECT_ATTRIBUTES obj_attr;
    InitializeObjectAttributes(&obj_attr, &dev_name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    IO_STATUS_BLOCK io_status;
    HANDLE h;
    if (!NT_SUCCESS(NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &obj_attr, &io_status,
                               FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE)))
        return false;

    // Detach
    NtDeviceIoControlFile(h, NULL, NULL, NULL, &io_status, IOCTL_VIRTUAL_DETACH, NULL, 0, NULL, 0);

    // Attach
    bool success = NT_SUCCESS(NtDeviceIoControlFile(h, NULL, NULL, NULL, &io_status,
                                                    IOCTL_VIRTUAL_ATTACH, asd, sizeof(ATTACH_SLICE_DATA_CERBIOS), NULL, 0));
    NtClose(h);

    if (success)
    {
        // Soft-launch into the freshly mounted virtual disc, matching the
        // overlay file manager's pattern. A hard HalReturnToFirmware works
        // for plain ISOs (BIOS re-detects on boot) but tears down Cerbios's
        // CCI decompression state mid-mount, producing a "dirty disc" error.
        OutputDebugString(L"TheseusLauncher: Cerbios attach OK, launching D:\\default.xbe\n");
        XLaunchNewImage("D:\\default.xbe", NULL);
    }

    return false;
}

bool CTheseusLauncher::AttachLegacy(const char *path, const char *file, USHORT build)
{
    void *membuf = NULL;
    ULONG membuf_size = 1024 * 1024;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(&membuf, 0, &membuf_size, MEM_COMMIT | MEM_NOZERO, PAGE_READWRITE)))
        return false;

    ATTACH_SLICE_DATA_LEGACY *asd = (ATTACH_SLICE_DATA_LEGACY *)membuf;
    memset(asd, 0, sizeof(ATTACH_SLICE_DATA_LEGACY));

    char *strbuf = (char *)membuf + sizeof(ATTACH_SLICE_DATA_LEGACY);
    membuf_size -= sizeof(ATTACH_SLICE_DATA_LEGACY);

    _snprintf(strbuf, membuf_size, "%s", path);
    RtlInitAnsiString(&asd->SliceFile[0], strbuf);
    asd->SliceFile[0].MaximumLength = asd->SliceFile[0].Length + 1;
    asd->SliceCount = 1;

    ANSI_STRING dev_name;
    RtlInitAnsiString(&dev_name, "\\Device\\CdRom1");
    OBJECT_ATTRIBUTES obj_attr;
    InitializeObjectAttributes(&obj_attr, &dev_name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    IO_STATUS_BLOCK io_status;
    HANDLE h;
    if (!NT_SUCCESS(NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &obj_attr, &io_status, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE)))
        return false;

    NtDeviceIoControlFile(h, NULL, NULL, NULL, &io_status, IOCTL_VIRTUAL_CDROM_DETACH, NULL, 0, NULL, 0);
    NtClose(h);

    IoDismountVolumeByName(&dev_name);
    if (!NT_SUCCESS(NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &obj_attr, &io_status, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT)))
        return false;

    bool success = NT_SUCCESS(NtDeviceIoControlFile(h, NULL, NULL, NULL, &io_status, IOCTL_VIRTUAL_CDROM_ATTACH, asd, sizeof(ATTACH_SLICE_DATA_LEGACY), NULL, 0));

    if (build == 5003 || build == 5004)
    {
        STRING saMountPoint, saSystemPath;
        RtlInitAnsiString(&saMountPoint, "\\??\\D:");
        RtlInitAnsiString(&saSystemPath, "\\Device\\CdRom0");
        IoDeleteSymbolicLink(&saMountPoint);
        IoCreateSymbolicLink(&saMountPoint, &saSystemPath);
        XLaunchNewImage("D:\\default.xbe", NULL);
    }
    else if (success)
    {
        // Soft-launch into the mounted virtual disc instead of cold-resetting --
        // matches the overlay's working flow. HalReturnToFirmware preserved
        // ISO mounts on this build but tore down CCI mid-mount.
        OutputDebugString(L"TheseusLauncher: Legacy attach OK, launching D:\\default.xbe\n");
        XLaunchNewImage("D:\\default.xbe", NULL);
    }
    NtClose(h);
    return success;
}

bool CTheseusLauncher::FileExists(const char *szPath)
{
    ANSI_STRING path;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE hFile;

    RtlInitAnsiString(&path, szPath);
    InitializeObjectAttributes(&objAttr, &path, OBJ_CASE_INSENSITIVE, NULL, NULL);

    NTSTATUS status = NtOpenFile(&hFile, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus, FILE_SHARE_READ, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

    if (!NT_SUCCESS(status))
        return false;

    NtClose(hFile);
    return true;
}

bool CTheseusLauncher::EndsWith(const char *str, const char *suffix)
{
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return false;
    return _stricmp(str + lenstr - lensuffix, suffix) == 0;
}

// Free-function entry for callers that can't pull in theseus_launcher.h
// without header-include conflicts (overlay.cpp's file-manager launch
// path). Spins up a transient CTheseusLauncher and dispatches.
extern "C" void LaunchFileFromOverlay(const TCHAR *szPath)
{
    CTheseusLauncher launcher;
    launcher.Launch(szPath);
}
