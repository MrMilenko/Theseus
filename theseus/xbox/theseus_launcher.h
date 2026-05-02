// theseus_launcher.h: CTheseusLauncher XAP node and the
// Cerbios / Hermes virtual ISO attach IOCTLs. Companion to
// xbox/theseus_launcher.cpp.

#pragma once
#include "std.h"
#include "theseus.h"
#include "xbe.h"
#include "xlaunch.h"
#include "file_util.h"
#include "node.h"
#include "ntiosvc.h"
#include "runner.h"
#include "settingsfile.h"

#define MAX_ISO_SLICES 16

#define MAX_IMAGE_SLICES 8

typedef struct _ATTACH_SLICE_DATA_CERBIOS {
    UCHAR SliceCount;
    UCHAR DeviceType;
    UCHAR Reserved1;
    UCHAR Reserved2;
    ANSI_STRING SliceFile[MAX_IMAGE_SLICES];
    ANSI_STRING MountPoint;
} ATTACH_SLICE_DATA_CERBIOS;


// Legacy BIOS attach structure (SliceCount must be first per Hermes spec)
typedef struct _ATTACH_SLICE_DATA_LEGACY {
    ULONG SliceCount;
    ANSI_STRING SliceFile[MAX_IMAGE_SLICES];
} ATTACH_SLICE_DATA_LEGACY;

// Cerbios IOCTLs (known-good)
#define IOCTL_VIRTUAL_ATTACH         0xCE52B01
#define IOCTL_VIRTUAL_DETACH         0xCE52B02

#define IOCTL_VIRTUAL_CDROM_ATTACH   0x1EE7CD01
#define IOCTL_VIRTUAL_CDROM_DETACH   0x1EE7CD02

class CTheseusLauncher : public CNode {
    DECLARE_NODE(CTheseusLauncher, CNode)
public:
    CTheseusLauncher();
    ~CTheseusLauncher();
    CXBExecutable theXBE;
    void Launch(const TCHAR* szPath);

private:
    bool LaunchXBE(const char* szXBEPath);
    bool LaunchWithAttach(const char* szISOPath);
    bool AttachCerbios(const char* path, const char* file, bool isCCI);
    bool AttachLegacy(const char* path, const char* file, USHORT build);

    bool FileExists(const char* szPath);
    bool EndsWith(const char* str, const char* suffix);
protected:
    DECLARE_NODE_FUNCTIONS()
};
