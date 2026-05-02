// xiso.h: XISO reader public API. Extracts default.xbe from an Xbox
// ISO and reads its XBE certificate (title id, title name, $$XTIMAGE
// bitmap). Companion to desktop/xiso.cpp.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Result from parsing an XISO
typedef struct {
    uint32_t titleId;
    char     titleName[128];       // ASCII title name from XBE certificate
    uint8_t* titleImageRGBA;       // 128x128 RGBA decoded from DXT1 (caller must free)
    int      titleImageWidth;
    int      titleImageHeight;
    bool     valid;
} XisoTitleInfo;

// Parse an XISO file and extract title info from default.xbe
// Returns info with valid=true on success. Caller must free titleImageRGBA if non-NULL.
XisoTitleInfo XisoGetTitleInfo(const char* isoPath);

// Save the title image as a JPEG file (128x128)
// Returns true on success.
bool XisoSaveTitleImage(const XisoTitleInfo* info, const char* outputPath);

// Free resources in a XisoTitleInfo (just the RGBA image buffer)
void XisoFreeTitleInfo(XisoTitleInfo* info);

#ifdef __cplusplus
}
#endif
