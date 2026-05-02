// title_scanner.h: CTitleScanner XAP node. Native title browser
// declarations. Companion to xbox/title_scanner.cpp.

#pragma once
#include "std.h"
#include "theseus.h"
#include "xbe.h"
#include "file_util.h"
#include "node.h"
#include "ntiosvc.h"
#include "settingsfile.h"

// Native title scanner. The slow part of UIX-Lite-style title browsing is
// the in-XAP filesystem walk (FindFirstFile + NtFileExists per folder per
// partition per menu); for hundreds of titles this is the dominant cost on
// dashboard boot. CTitleScanner does the same walk in native code, parses
// XBE certificates for the title-id, recognises XBE / ISO / CCI launchables
// alongside the traditional default.xbe, and writes a UIX-Lite-compatible
// cache.ini that scripted harddrive.xap reads directly.
//
// XAP side wires it like this:
//
//     theTitleScanner.ClearMenus();
//     for (var i = 0; i < arrMenuNames.length; i = i + 1)
//         theTitleScanner.AddMenu(arrMenuNames[i], arrMenuPaths[i]);
//     theTitleScanner.Rebuild();
//
// On Theseus the cache lives at `C:\UIX Configs\cache.ini`.
class CTitleScanner : public CNode
{
    DECLARE_NODE(CTitleScanner, CNode)
public:
    CTitleScanner();
    ~CTitleScanner();

    void ClearMenus();
    void AddMenu(const TCHAR* szName, const TCHAR* szPathSpec);
    void Rebuild();

protected:
    DECLARE_NODE_FUNCTIONS()
};
