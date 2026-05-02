// xiso.h: Xbox ISO (XISO) parser.
//
// Walks the XISO directory table to find default.xbe, parses its XBE
// header / certificate to extract the title id and title name, then
// locates the $$XTIMAGE section and decodes the 128x128 DXT1 title image
// into RGBA.
//
// Both the Xbox dashboard (when an XISO is mounted as a virtual drive and
// a launcher entry needs to display its cover art) and the desktop
// launcher consume this. The implementation uses the Win32 file API
// (HANDLE / CreateFile / SetFilePointer / ReadFile) so 64-bit offsets work
// on both targets, since XISOs run up to ~7 GB, well past the 32-bit fseek
// limit in the Xbox CRT.
//
// Caller is responsible for writing the decoded title image to disk if it
// wants to. Host-specific encoding (PNG / JPEG via stb on the desktop
// side, XPR0 round-trips on Xbox, etc.) is intentionally outside the
// scope of the lib.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Title metadata extracted from a single XISO. titleImageRGBA is owned
// by the caller and must be released with XisoFreeTitleInfo.
typedef struct {
    uint32_t titleId;
    char     titleName[128];       // ASCII title name from the XBE certificate
    uint8_t* titleImageRGBA;       // 128x128 RGBA decoded from DXT1 (caller frees)
    int      titleImageWidth;
    int      titleImageHeight;
    bool     valid;
} XisoTitleInfo;

// Open an Xbox ISO and pull the title metadata out of its default.xbe.
// On success the returned info has valid=true; on any failure the
// struct is zeroed and valid is false. titleImageRGBA may be NULL even
// on success if the XBE has no $$XTIMAGE section.
XisoTitleInfo XisoGetTitleInfo(const char* isoPath);

// Release any heap allocations attached to a XisoTitleInfo. Safe to
// call on a zeroed struct.
void XisoFreeTitleInfo(XisoTitleInfo* info);

#ifdef __cplusplus
}
#endif
