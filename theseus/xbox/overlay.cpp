// overlay.cpp: modal overlay UI (cube menu). Renders on top of the
// active scene with its own input grab, used for the FTP toolbox,
// network status, modchip info, and feature toggles. Theseus-
// original; the retail dashboard had no equivalent overlay.

#include "std.h"
#include "theseus.h"
#include "config.h"
#include "xbe.h"
#include "node.h"
#include "activefile.h"
#include "runner.h"
#include "network.h"
#ifdef _MAC_CROSS_COMPILE
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;
typedef struct { DWORD dwFlags; IN_ADDR ina, inaMask, inaGateway, inaDnsPrimary, inaDnsSecondary, inaDhcpServer; char achPppServer[64][4]; } XNetConfigStatus;
extern "C" NTSTATUS WINAPI XNetGetConfigStatus(XNetConfigStatus* status);
#else
#include "toolbox/xboxinternals.h"
#endif

// Forward-declare driveManager methods we need (avoid including driveManager.h
// which pulls in <string> and conflicts with the debug new macro in Std.h)
class driveManager {
public:
    static bool getTotalNumberOfBytes(const char* mountPoint, uint64_t& totalSize);
    static bool getTotalFreeNumberOfBytes(const char* mountPoint, uint64_t& totalFree);
};

#include "file_util.h"
#include "disc_manager.h"
#include "input.h"
#include "discord.h"
#include "widget_draw.h"
#include "widget_layer.h"

// Forward decl for the file-manager launch path. Defined in
// xbox/theseus_launcher.cpp; pulling theseus_launcher.h in here
// causes a header re-include conflict (redefinition of CMember etc.).
extern "C" void LaunchFileFromOverlay(const TCHAR* szPath);

extern CConfig *theConfig;
// Globals from theseus.h are used directly
const TCHAR *DetectModchip();

#ifndef ARRAYSIZE
#define ARRAYSIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// ============================================================================
// Page system
// ============================================================================

enum OverlayPage
{
    OVERLAY_PAGE_SYSTEM,
    OVERLAY_PAGE_DISC,
    OVERLAY_PAGE_NETWORK,
    OVERLAY_PAGE_TOOLBOX,
    OVERLAY_PAGE_FILES,
    OVERLAY_PAGE_WIDGETS,
    OVERLAY_PAGE_MENU,
    OVERLAY_PAGE_COUNT
};

static const TCHAR *g_szPageNames[] = {_T("System"), _T("Disc"), _T("Network"), _T("Toolbox"), _T("Files"), _T("Widgets"), _T("Menu")};
static int g_nOverlayPage = OVERLAY_PAGE_SYSTEM;

// ============================================================================
// Overlay state
// ============================================================================

bool g_bShowOverlay = false;
bool g_bOverlayInputCapture = false;
bool g_bOverlayAlertActive = false;
static IDirect3DTexture8 *g_pOverlayTex = NULL;

// Alert popup state
#define MAX_ALERT_LEN 256
static TCHAR g_szAlertMsg[MAX_ALERT_LEN] = {0};

// Menu page state
static int g_nMenuIndex = 0;
static bool g_bMenuConfirmed = false;

// Disc page extraction path (set during dump)
static const char *extractPath = NULL;

// Log system
#define MAX_LOG_LINES 64
#define MAX_LOG_LINE_LENGTH 256
static WCHAR g_logLines[MAX_LOG_LINES][MAX_LOG_LINE_LENGTH];
static int g_logLineCount = 0;

// Font size
#define FONT_H 18.0f
#define LINE_H 26

// ============================================================================
// Drawing helpers
// ============================================================================

struct OverlayVertex
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};

static void LoadOverlayTexture()
{
    if (g_pOverlayTex)
        return;

    HRESULT hr = D3DXCreateTextureFromFile(g_pD3DDev, "Q:\\Textures\\Team_UIX.bmp", &g_pOverlayTex);
    if (FAILED(hr))
        OutputDebugStringA("[Overlay] Failed to load overlay texture.\n");
}

static void DrawOverlayImage(IDirect3DTexture8 *tex, float x, float y, float width, float height)
{
    if (!tex) return;
    IDirect3DDevice8 *dev = g_pD3DDev;

    // Switch to textured mode
    dev->SetTexture(0, tex);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    OverlayVertex quad[4] = {
        {x, y, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f},
        {x + width, y, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f},
        {x, y + height, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f},
        {x + width, y + height, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f}
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(OverlayVertex));

    // Restore diffuse-only state
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

// ============================================================================
// Utility functions
// ============================================================================

float ReadCPUTemp()
{
    static DWORD lastReadTime = 0;
    static float lastTemp = 0.0f;

    DWORD now = GetTickCount();
    if (now - lastReadTime > 1000)
    {
        ULONG data = 0;
        if (HalReadSMBusValue && HalReadSMBusValue(0x98, 0x01, FALSE, &data) == 0)
            lastTemp = static_cast<float>(data & 0xFF);
        lastReadTime = now;
    }
    return lastTemp;
}

float ReadGPUTemp()
{
    static DWORD lastReadTime = 0;
    static float lastTemp = 0.0f;

    DWORD now = GetTickCount();
    if (now - lastReadTime > 1000)
    {
        ULONG data = 0;
        if (HalReadSMBusValue && HalReadSMBusValue(0x98, 0x00, FALSE, &data) == 0)
            lastTemp = static_cast<float>(data & 0xFF);
        lastReadTime = now;
    }
    return lastTemp;
}

static void GetMemoryUsage(DWORD &usedMB, DWORD &freeMB)
{
    MEMORYSTATUS memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatus(&memStatus);
    DWORD totalMB = memStatus.dwTotalPhys / (1024 * 1024);
    DWORD availMB = memStatus.dwAvailPhys / (1024 * 1024);
    usedMB = totalMB - availMB;
    freeMB = availMB;
}

static void FormatIP(TCHAR* buf, int bufSize, uint32_t addr)
{
    _sntprintf(buf, bufSize, _T("%d.%d.%d.%d"),
               (addr) & 0xFF, (addr >> 8) & 0xFF,
               (addr >> 16) & 0xFF, (addr >> 24) & 0xFF);
}

// ============================================================================
// Log system (used by disc dump via DiscManager.cpp)
// ============================================================================

void OverlayLog(const WCHAR *msg)
{
    if (g_logLineCount < MAX_LOG_LINES)
    {
        wcsncpy(g_logLines[g_logLineCount], msg, MAX_LOG_LINE_LENGTH - 1);
        g_logLines[g_logLineCount][MAX_LOG_LINE_LENGTH - 1] = L'\0';
        g_logLineCount++;
    }
}

void OverlayLogf(const WCHAR *fmt, ...)
{
    if (g_logLineCount >= MAX_LOG_LINES)
    {
        for (int i = 1; i < MAX_LOG_LINES; ++i)
            lstrcpyW(g_logLines[i - 1], g_logLines[i]);
        g_logLineCount = MAX_LOG_LINES - 1;
    }

    va_list args;
    va_start(args, fmt);
    vswprintf(g_logLines[g_logLineCount], MAX_LOG_LINE_LENGTH, fmt, args);
    va_end(args);
    ++g_logLineCount;
}

// ============================================================================
// Alert popup
// ============================================================================

// Remembered across an alert's lifetime so we can restore the panel
// state when the user dismisses. Without this, an alert fired while
// the overlay was closed would leave the panel open after dismiss.
static bool s_overlayOpenBeforeAlert = false;

void OverlayAlert(const TCHAR* msg)
{
    if (!msg || !*msg)
        return;
    _tcsncpy(g_szAlertMsg, msg, MAX_ALERT_LEN - 1);
    g_szAlertMsg[MAX_ALERT_LEN - 1] = 0;

    // Capture the panel's pre-alert state once. Chained alerts
    // shouldn't overwrite this with the (already-true) intermediate
    // state.
    if (!g_bOverlayAlertActive)
        s_overlayOpenBeforeAlert = g_bShowOverlay;

    g_bOverlayAlertActive  = true;
    g_bShowOverlay         = true;   // alert rides the overlay's render path
    g_bOverlayInputCapture = true;
}

// Word-wrap g_szAlertMsg into individual lines that each fit within
// maxW. Greedy: append words to the current line while they fit, flush
// to the line table when they don't. Words longer than maxW are still
// emitted on their own line (will visually clip but at least won't
// hang the wrap loop). Returns the number of lines produced.
#define ALERT_MAX_LINES 8
static int WrapAlertText(TCHAR lines[ALERT_MAX_LINES][MAX_ALERT_LEN], float maxW, float pixelHeight)
{
    int nLines = 0;
    const TCHAR* p = g_szAlertMsg;

    while (*p && nLines < ALERT_MAX_LINES)
    {
        TCHAR* dst = lines[nLines];
        int dstLen = 0;
        dst[0] = 0;

        while (*p)
        {
            // Skip leading whitespace at the start of a wrapped line.
            if (dstLen == 0 && (*p == _T(' ') || *p == _T('\t')))
            {
                p++;
                continue;
            }

            // Find the next word boundary.
            const TCHAR* wordEnd = p;
            while (*wordEnd && *wordEnd != _T(' ') && *wordEnd != _T('\t') && *wordEnd != _T('\n'))
                wordEnd++;
            int wordLen = (int)(wordEnd - p);

            // Tentatively append (with leading space if this isn't the first word).
            int addLen = wordLen + (dstLen > 0 ? 1 : 0);
            if (dstLen + addLen >= MAX_ALERT_LEN)
                break;

            TCHAR trial[MAX_ALERT_LEN];
            _tcscpy(trial, dst);
            if (dstLen > 0)
                _tcscat(trial, _T(" "));
            _tcsncat(trial, p, wordLen);

            float trialW = OverlayFontMeasure(trial, pixelHeight);
            if (trialW > maxW && dstLen > 0)
                break; // current line is full; this word goes on next line

            // Commit the word.
            _tcscpy(dst, trial);
            dstLen += addLen;
            p = wordEnd;

            // Hard newline ends the line immediately.
            if (*p == _T('\n'))
            {
                p++;
                break;
            }
        }

        nLines++;
    }

    return nLines;
}

static void DrawAlertPopup(int sw, int sh)
{
    const int margin    = 24;
    const int pw        = 540;
    const int contentW  = pw - margin * 2;
    const int lineH     = (int)FONT_H + 6;
    const int footerGap = 12;

    // Wrap the message and size the box to fit.
    static TCHAR wrapped[ALERT_MAX_LINES][MAX_ALERT_LEN];
    int nLines = WrapAlertText(wrapped, (float)contentW, FONT_H);
    if (nLines < 1) nLines = 1;

    const int textBlock = nLines * lineH;
    const int ph = margin + textBlock + footerGap + (int)FONT_H + margin;

    const int px = (sw - pw) / 2;
    const int py = (sh - ph) / 2;

    // Background
    DrawSolidRect(px, py, pw, ph, D3DCOLOR_ARGB(230, 15, 15, 15));

    // Border
    DrawSolidRect(px,             py,             pw, 1,  D3DCOLOR_ARGB(120, 255, 255, 255));
    DrawSolidRect(px,             py + ph - 1,    pw, 1,  D3DCOLOR_ARGB(120, 255, 255, 255));
    DrawSolidRect(px,             py,             1,  ph, D3DCOLOR_ARGB(120, 255, 255, 255));
    DrawSolidRect(px + pw - 1,    py,             1,  ph, D3DCOLOR_ARGB(120, 255, 255, 255));

    // Message (each wrapped line centered horizontally)
    int y = py + margin;
    for (int i = 0; i < nLines; i++)
    {
        float w = OverlayFontMeasure(wrapped[i], FONT_H);
        float x = (float)(px + (pw - (int)w) / 2);
        OverlayFontDraw(wrapped[i], x, (float)y, FONT_H, 0xFFFFFFFF);
        y += lineH;
    }

    // Footer
    const TCHAR* hint = _T("B: Close");
    float hintW = OverlayFontMeasure(hint, FONT_H);
    OverlayFontDraw(hint, (float)(px + (pw - (int)hintW) / 2),
                    (float)(py + ph - margin - (int)FONT_H), FONT_H, 0xFF555555);
}

// ============================================================================
// Page: System (Diagnostics)
// ============================================================================

static int g_sysScrollOffset = 0;
static int g_sysLineCount = 0;

extern float g_fps; // computed in main.cpp render loop

static void DrawSystemPage(int cx, int cy, int cw, int ch)
{
    static bool bInfoInit = false;
    static const TCHAR *szIP = NULL, *szRegion = NULL, *szAVPack = NULL;

    if (!bInfoInit)
    {
        bInfoInit = true;

        static TCHAR szBufIP[32];
        XNetConfigStatus status = {};
        if (XNetGetConfigStatus(&status) == 0)
        {
            _sntprintf(szBufIP, countof(szBufIP), _T("%d.%d.%d.%d"),
                       status.ina.S_un.S_un_b.s_b1, status.ina.S_un.S_un_b.s_b2,
                       status.ina.S_un.S_un_b.s_b3, status.ina.S_un.S_un_b.s_b4);
            szIP = szBufIP;
        }
        else
            szIP = _T("Unavailable");

        CStrObject *objAV = theConfig->GetAVPackType();
        szAVPack = (objAV && objAV->GetSz() && *objAV->GetSz()) ? objAV->GetSz() : _T("Unavailable");

        CStrObject *objRegion = theConfig->GetGameRegion();
        szRegion = (objRegion && objRegion->GetSz() && *objRegion->GetSz()) ? objRegion->GetSz() : _T("Unavailable");
    }


    // Build all lines into a buffer for scrolling
    #define SYS_MAX_LINES 40
    struct SysLine {
        TCHAR text[128];
        DWORD color;
        bool  isSeparator;
    };
    static SysLine lines[SYS_MAX_LINES];
    int n = 0;

    const DWORD clr = 0xFFCCCCCC;
    const DWORD hdr = 0xFFFFFFFF;

    // System info
    _tcscpy(lines[n].text, _T("System")); lines[n].color = hdr; lines[n].isSeparator = false; n++;

    _stprintf(lines[n].text, _T("  Region:  %s"), szRegion); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  AV Pack: %s"), szAVPack); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  Modchip: %s"), DetectModchip()); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  IP:      %s"), szIP); lines[n].color = clr; lines[n].isSeparator = false; n++;

    lines[n].text[0] = 0; lines[n].color = 0; lines[n].isSeparator = true; n++;

    // Performance
    _tcscpy(lines[n].text, _T("Performance")); lines[n].color = hdr; lines[n].isSeparator = false; n++;

    _stprintf(lines[n].text, _T("  FPS: %.1f"), g_fps); lines[n].color = clr; lines[n].isSeparator = false; n++;

    DWORD usedMB = 0, freeMB = 0;
    GetMemoryUsage(usedMB, freeMB);
    _stprintf(lines[n].text, _T("  Memory: %lu MB Used / %lu MB Free"), usedMB, freeMB);
    lines[n].color = clr; lines[n].isSeparator = false; n++;

    _stprintf(lines[n].text, _T("  CPU: %.0f C  |  GPU: %.0f C"), ReadCPUTemp(), ReadGPUTemp());
    lines[n].color = clr; lines[n].isSeparator = false; n++;

    lines[n].text[0] = 0; lines[n].color = 0; lines[n].isSeparator = true; n++;

    // Storage
    _tcscpy(lines[n].text, _T("Storage")); lines[n].color = hdr; lines[n].isSeparator = false; n++;

    struct { const TCHAR* display; const char* mountPoint; bool* exists; } drives[] = {
        {_T("C"), "HDD0-C", NULL}, {_T("E"), "HDD0-E", NULL},
        {_T("F"), "HDD0-F", &g_fExists}, {_T("G"), "HDD0-G", &g_gExists},
        {_T("H"), "HDD0-H", &g_hExists}, {_T("I"), "HDD0-I", &g_iExists},
        {_T("J"), "HDD0-J", &g_jExists}, {_T("K"), "HDD0-K", &g_kExists},
        {_T("L"), "HDD0-L", &g_lExists}, {_T("M"), "HDD0-M", &g_mExists},
        {_T("N"), "HDD0-N", &g_nExists},
    };

    for (int d = 0; d < ARRAYSIZE(drives) && n < SYS_MAX_LINES - 2; d++)
    {
        if (drives[d].exists && !*drives[d].exists)
            continue;

        uint64_t totalBytes = 0, freeBytes = 0;
        if (driveManager::getTotalNumberOfBytes(drives[d].mountPoint, totalBytes) &&
            driveManager::getTotalFreeNumberOfBytes(drives[d].mountPoint, freeBytes))
        {
            uint32_t totalMB = (uint32_t)(totalBytes / (1024 * 1024));
            uint32_t freeMB  = (uint32_t)(freeBytes / (1024 * 1024));

            if (totalMB >= 1024)
                _stprintf(lines[n].text, _T("  %s: %.1f / %.1f GB free"),
                    drives[d].display, freeMB / 1024.0f, totalMB / 1024.0f);
            else
                _stprintf(lines[n].text, _T("  %s: %u / %u MB free"),
                    drives[d].display, freeMB, totalMB);
        }
        else
        {
            _stprintf(lines[n].text, _T("  %s: Unavailable"), drives[d].display);
        }
        lines[n].color = clr;
        lines[n].isSeparator = false;
        n++;
    }

    g_sysLineCount = n;

    // Scrolled rendering
    const int maxVisible = ch / LINE_H;

    if (g_sysScrollOffset > n - maxVisible)
        g_sysScrollOffset = max(0, n - maxVisible);

    int visEnd = min(g_sysScrollOffset + maxVisible, n);
    float y = (float)cy;

    for (int i = g_sysScrollOffset; i < visEnd; i++)
    {
        if (lines[i].isSeparator)
        {
            DrawSolidRect(cx, (int)y + LINE_H / 2, cw - 12, 1, D3DCOLOR_ARGB(64, 255, 255, 255));
        }
        else
        {
            OverlayFontDraw(lines[i].text, (float)cx, y, FONT_H, lines[i].color);
        }
        y += LINE_H;
    }

    // Scroll bar (right edge)
    if (n > maxVisible)
    {
        int barX = cx + cw - 6;
        int barH = ch;
        int thumbH = max(20, barH * maxVisible / n);
        int thumbY = cy + (barH - thumbH) * g_sysScrollOffset / (n - maxVisible);

        // Track
        DrawSolidRect(barX, cy, 4, barH, D3DCOLOR_ARGB(40, 255, 255, 255));
        // Thumb
        DrawSolidRect(barX, thumbY, 4, thumbH, D3DCOLOR_ARGB(160, 255, 255, 255));
    }
}

// ============================================================================
// Page: Disc / ISO Tools
// ============================================================================

// Virtual disc IOCTLs (from TheseusLauncher.h)
#define OVL_IOCTL_VIRTUAL_ATTACH       0xCE52B01
#define OVL_IOCTL_VIRTUAL_DETACH       0xCE52B02
#define OVL_IOCTL_VIRTUAL_CDROM_ATTACH 0x1EE7CD01
#define OVL_IOCTL_VIRTUAL_CDROM_DETACH 0x1EE7CD02

#define OVL_MAX_IMAGE_SLICES 8

typedef struct _OVL_ATTACH_CERBIOS {
    UCHAR SliceCount;
    UCHAR DeviceType;
    UCHAR Reserved1;
    UCHAR Reserved2;
    ANSI_STRING SliceFile[OVL_MAX_IMAGE_SLICES];
    ANSI_STRING MountPoint;
} OVL_ATTACH_CERBIOS;

typedef struct _OVL_ATTACH_LEGACY {
    ULONG SliceCount;
    ANSI_STRING SliceFile[OVL_MAX_IMAGE_SLICES];
} OVL_ATTACH_LEGACY;

// Disc page state
enum DiscPageMode
{
    DISC_MODE_IDLE,
    DISC_MODE_SCANNING,
    DISC_MODE_DUMPING,
    DISC_MODE_DUMP_DONE,
    DISC_MODE_ISO_BROWSE
};

static DiscPageMode g_discMode = DISC_MODE_IDLE;
static bool g_discScanned = false;
static bool g_discPresent = false;
static bool g_isoMounted = false;
static TCHAR g_discTitle[64] = {0};
static int g_discMenuIndex = 0;
static bool g_discMenuConfirmed = false;

// ISO browser state
#define ISO_MAX_FILES 128
#define ISO_MAX_PATH  260
#define ISO_MAX_DRIVES 8

struct IsoFileEntry
{
    char szPath[ISO_MAX_PATH];
    char szName[ISO_MAX_PATH];
    bool bIsDir;
};

static IsoFileEntry g_isoFiles[ISO_MAX_FILES];
static int g_isoFileCount = 0;
static int g_isoBrowseIndex = 0;
static char g_isoBrowseDir[ISO_MAX_PATH] = "";
static bool g_isoBrowsePickingDrive = true;

// Drive list for ISO browser
struct IsoDriveEntry
{
    char szRoot[8];
    TCHAR szLabel[32];
};

static IsoDriveEntry g_isoDrives[ISO_MAX_DRIVES];
static int g_isoDriveCount = 0;
static int g_isoDriveIndex = 0;

// Mounted ISO title info (read from D:\default.xbe after mount)
// Mounted-ISO state. Exposed (non-static) so the ISO widget can
// read it. Owners are still in this file; widget is read-only.
bool g_isoTitleInfoLoaded = false;
char g_isoTitleName[256] = {0};
DWORD g_isoTitleId = 0;
LPDIRECT3DTEXTURE8 g_pIsoTitleImage = NULL;
static CXBExecutable g_isoXBE;

static DWORD g_isoMediaFlag = 0;
DWORD g_isoGameRegion = 0;
static DWORD g_isoDiskNumber = 0;
static DWORD g_isoVersion = 0;


// Forward declarations
static void BuildDiscMenu();

// ISO launch delay state
static bool g_isoLaunching = false;
static DWORD g_isoLaunchTick = 0;
static bool g_isoLaunchIsCCI = false;
static TCHAR g_isoLaunchName[64] = {0};

static const char* GetRegionString(DWORD region)
{
    // XBE game region flags
    if (region & 0x00000001 && region & 0x00000002 && region & 0x00000004)
        return "World";
    if (region & 0x80000000)
        return "Debug";

    // Build combined string for multi-region
    static char buf[64];
    buf[0] = '\0';
    if (region & 0x00000001) strcat(buf, "NTSC-U ");
    if (region & 0x00000002) strcat(buf, "NTSC-J ");
    if (region & 0x00000004) strcat(buf, "PAL ");
    if (buf[0] == '\0') strcpy(buf, "Unknown");
    // Trim trailing space
    int len = (int)strlen(buf);
    if (len > 0 && buf[len-1] == ' ') buf[len-1] = '\0';
    return buf;
}

static const char* GetMediaTypeString(DWORD media)
{
    if (media & 0x00000001) return "HDD";
    if (media & 0x00000002) return "DVD-X2";
    if (media & 0x00000004) return "DVD-CD";
    if (media & 0x00000008) return "CD-ROM";
    if (media & 0x00000010) return "DVD-5 RO";
    if (media & 0x00000020) return "DVD-9 RO";
    if (media & 0x00000040) return "DVD-5 RW";
    if (media & 0x00000080) return "DVD-9 RW";
    if (media & 0x00000100) return "Dongle";
    if (media & 0x00000200) return "Media Board";
    return "Unknown";
}

static void ClearMountedIsoInfo()
{
    g_isoTitleInfoLoaded = false;
    g_isoTitleName[0] = '\0';
    g_isoTitleId = 0;
    g_isoMediaFlag = 0;
    g_isoGameRegion = 0;
    g_isoDiskNumber = 0;
    g_isoVersion = 0;
    if (g_pIsoTitleImage)
    {
        g_pIsoTitleImage->Release();
        g_pIsoTitleImage = NULL;
    }
}

// Load title info from mounted ISO via D: (works on boot when D: is valid)
static void LoadMountedIsoInfo()
{
    ClearMountedIsoInfo();
    g_isoXBE.Clear();
    if (g_isoXBE.ReadFile("D:\\default.xbe", true, false) == 1 && g_isoXBE.Valid())
    {
        g_isoTitleId = g_isoXBE.TitleId();
        g_isoMediaFlag = g_isoXBE.MediaFlag();
        g_isoGameRegion = g_isoXBE.GameRegion();
        g_isoDiskNumber = g_isoXBE.m_XBEInfo.Certificate.disk_number;
        g_isoVersion = g_isoXBE.m_XBEInfo.Certificate.version;

        strncpy(g_isoTitleName, g_isoXBE.InternalName(), sizeof(g_isoTitleName) - 1);
        g_isoTitleName[sizeof(g_isoTitleName) - 1] = '\0';
        g_pIsoTitleImage = g_isoXBE.TitleImage();
        g_isoXBE.m_pTitleImageTexture = NULL;

        g_isoTitleInfoLoaded = true;
    }
}

static void BuildIsoDriveList()
{
    g_isoDriveCount = 0;
    g_isoDriveIndex = 0;

    struct { const char* root; const TCHAR* label; } candidates[] = {
        {"C:\\", _T("C: (System)")},
        {"E:\\", _T("E: (Data)")},
        {"F:\\", _T("F: (Games)")},
        {"G:\\", _T("G: (Extended)")},
        {"R:\\", _T("R: (Extended)")},
        {"S:\\", _T("S: (Extended)")},
    };

    for (int i = 0; i < ARRAYSIZE(candidates); ++i)
    {
        if (g_isoDriveCount >= ISO_MAX_DRIVES)
            break;

        // Check if drive exists by trying to query free space
        ULARGE_INTEGER free = {}, total = {}, dummy;
        if (GetDiskFreeSpaceExA(candidates[i].root, &free, &total, &dummy))
        {
            IsoDriveEntry* e = &g_isoDrives[g_isoDriveCount++];
            strncpy(e->szRoot, candidates[i].root, 7);
            e->szRoot[7] = '\0';
            _tcsncpy(e->szLabel, candidates[i].label, 31);
            e->szLabel[31] = 0;
        }
    }
}

static bool EndsWith(const char* str, const char* suffix)
{
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) return false;
    return _stricmp(str + lenstr - lensuffix, suffix) == 0;
}

static void IsoBrowseDir(const char* dir)
{
    g_isoFileCount = 0;
    g_isoBrowseIndex = 0;
    strncpy(g_isoBrowseDir, dir, ISO_MAX_PATH - 1);
    g_isoBrowseDir[ISO_MAX_PATH - 1] = '\0';

    // Add ".." entry if not at root
    if (strlen(dir) > 3) // e.g. "F:\" is root
    {
        IsoFileEntry* e = &g_isoFiles[g_isoFileCount++];
        strcpy(e->szName, "..");
        strcpy(e->szPath, "..");
        e->bIsDir = true;
    }

    char searchPath[ISO_MAX_PATH];
    _snprintf(searchPath, ISO_MAX_PATH, "%s*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (g_isoFileCount >= ISO_MAX_FILES)
            break;

        // Skip . and ..
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isIso = !isDir && (EndsWith(fd.cFileName, ".iso") || EndsWith(fd.cFileName, ".cci"));

        // Only show directories and ISO/CCI files
        if (!isDir && !isIso)
            continue;

        IsoFileEntry* e = &g_isoFiles[g_isoFileCount++];
        strncpy(e->szName, fd.cFileName, ISO_MAX_PATH - 1);
        e->szName[ISO_MAX_PATH - 1] = '\0';
        _snprintf(e->szPath, ISO_MAX_PATH, "%s%s", dir, fd.cFileName);
        e->bIsDir = isDir;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

// Convert DOS drive path (e.g. "F:\foo\bar.iso") to NT device path
// (e.g. "\Device\Harddisk0\Partition6\foo\bar.iso")
static bool DosToNtPath(const char* dosPath, char* ntPath, int ntPathSize)
{
    if (!dosPath || strlen(dosPath) < 3 || dosPath[1] != ':' || dosPath[2] != '\\')
        return false;

    // Static drive letter to NT device path mapping (matches Xbox.cpp)
    struct { char letter; const char* ntDev; } driveMap[] = {
        {'C', "\\Device\\Harddisk0\\partition2"},
        {'E', "\\Device\\Harddisk0\\partition1"},
        {'F', "\\Device\\Harddisk0\\partition6"},
        {'G', "\\Device\\Harddisk0\\partition7"},
        {'R', "\\Device\\Harddisk0\\partition8"},
        {'S', "\\Device\\Harddisk0\\partition9"},
        {'X', "\\Device\\Harddisk0\\partition5"},
        {'Y', "\\Device\\Harddisk0\\partition4"},
        {'Z', "\\Device\\Harddisk0\\partition3"},
    };

    char driveLetter = dosPath[0];
    if (driveLetter >= 'a' && driveLetter <= 'z')
        driveLetter -= 32; // uppercase

    const char* ntDev = NULL;
    for (int i = 0; i < ARRAYSIZE(driveMap); ++i)
    {
        if (driveMap[i].letter == driveLetter)
        {
            ntDev = driveMap[i].ntDev;
            break;
        }
    }

    if (!ntDev)
        return false;

    // Append the rest of the DOS path (after "X:\")
    const char* remainder = dosPath + 3;
    if (*remainder)
        _snprintf(ntPath, ntPathSize, "%s\\%s", ntDev, remainder);
    else
        _snprintf(ntPath, ntPathSize, "%s", ntDev);

    return true;
}

static bool AttachIso(const char* isoPath)
{
#ifdef _XBOX
    // Convert DOS path to NT device path
    char ntDevPath[ISO_MAX_PATH];
    if (!DosToNtPath(isoPath, ntDevPath, ISO_MAX_PATH))
    {
        OverlayAlert(_T("Failed to resolve drive path."));
        return false;
    }

    OutputDebugStringA("[Overlay] AttachIso NT path: ");
    OutputDebugStringA(ntDevPath);
    OutputDebugStringA("\r\n");

    const bool isCCI = EndsWith(isoPath, ".cci");
    const USHORT build = XboxKrnlVersion->Build;

    if (build >= 8008)
    {
        // Cerbios attach
        void* membuf = NULL;
        ULONG membuf_size = 1024 * 1024;
        if (!NT_SUCCESS(NtAllocateVirtualMemory(&membuf, 0, &membuf_size, MEM_COMMIT | MEM_NOZERO, PAGE_READWRITE)))
        {
            OverlayAlert(_T("Attach failed: memory alloc."));
            return false;
        }

        OVL_ATTACH_CERBIOS* asd = (OVL_ATTACH_CERBIOS*)membuf;
        memset(asd, 0, sizeof(OVL_ATTACH_CERBIOS));
        asd->DeviceType = isCCI ? 'd' : 'D';

        char* strbuf = (char*)membuf + sizeof(OVL_ATTACH_CERBIOS);
        membuf_size -= sizeof(OVL_ATTACH_CERBIOS);

        _snprintf(strbuf, membuf_size, "\\Device\\CdRom0");
        RtlInitAnsiString(&asd->MountPoint, strbuf);
        asd->MountPoint.MaximumLength = asd->MountPoint.Length + 1;
        strbuf += asd->MountPoint.MaximumLength;
        membuf_size -= asd->MountPoint.MaximumLength;

        _snprintf(strbuf, membuf_size, "%s", ntDevPath);
        RtlInitAnsiString(&asd->SliceFile[0], strbuf);
        asd->SliceFile[0].MaximumLength = asd->SliceFile[0].Length + 1;
        asd->SliceCount = 1;

        ANSI_STRING devName;
        RtlInitAnsiString(&devName, "\\Device\\Virtual0\\Image0");
        OBJECT_ATTRIBUTES objAttr;
        objAttr.RootDirectory = NULL;
        objAttr.ObjectName = &devName;
        objAttr.Attributes = OBJ_CASE_INSENSITIVE;

        IO_STATUS_BLOCK ioStatus;
        HANDLE h;
        if (!NT_SUCCESS(NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus,
                                    FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE)))
        {
            NtFreeVirtualMemory(&membuf, &membuf_size, MEM_RELEASE);
            OverlayAlert(_T("Attach failed: can't open virtual device."));
            return false;
        }

        // Detach first
        NtDeviceIoControlFile(h, NULL, NULL, NULL, &ioStatus, OVL_IOCTL_VIRTUAL_DETACH, NULL, 0, NULL, 0);

        bool success = NT_SUCCESS(NtDeviceIoControlFile(h, NULL, NULL, NULL, &ioStatus,
                                                         OVL_IOCTL_VIRTUAL_ATTACH, asd, sizeof(OVL_ATTACH_CERBIOS), NULL, 0));
        NtClose(h);
        NtFreeVirtualMemory(&membuf, &membuf_size, MEM_RELEASE);

        if (success)
        {
            return true;
        }
        OverlayAlert(_T("Attach IOCTL failed."));
        return false;
    }
    else
    {
        // Legacy attach
        void* membuf = NULL;
        ULONG membuf_size = 1024 * 1024;
        if (!NT_SUCCESS(NtAllocateVirtualMemory(&membuf, 0, &membuf_size, MEM_COMMIT | MEM_NOZERO, PAGE_READWRITE)))
        {
            OverlayAlert(_T("Attach failed: memory alloc."));
            return false;
        }

        OVL_ATTACH_LEGACY* asd = (OVL_ATTACH_LEGACY*)membuf;
        memset(asd, 0, sizeof(OVL_ATTACH_LEGACY));

        char* strbuf = (char*)membuf + sizeof(OVL_ATTACH_LEGACY);
        membuf_size -= sizeof(OVL_ATTACH_LEGACY);

        _snprintf(strbuf, membuf_size, "%s", ntDevPath);
        RtlInitAnsiString(&asd->SliceFile[0], strbuf);
        asd->SliceFile[0].MaximumLength = asd->SliceFile[0].Length + 1;
        asd->SliceCount = 1;

        ANSI_STRING devName;
        RtlInitAnsiString(&devName, "\\Device\\CdRom1");
        OBJECT_ATTRIBUTES objAttr;
        objAttr.RootDirectory = NULL;
        objAttr.ObjectName = &devName;
        objAttr.Attributes = OBJ_CASE_INSENSITIVE;

        IO_STATUS_BLOCK ioStatus;
        HANDLE h;
        if (!NT_SUCCESS(NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus,
                                    FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE)))
        {
            NtFreeVirtualMemory(&membuf, &membuf_size, MEM_RELEASE);
            OverlayAlert(_T("Attach failed: can't open virtual device."));
            return false;
        }

        NtDeviceIoControlFile(h, NULL, NULL, NULL, &ioStatus, OVL_IOCTL_VIRTUAL_CDROM_DETACH, NULL, 0, NULL, 0);
        NtClose(h);

        ANSI_STRING devName2;
        RtlInitAnsiString(&devName2, "\\Device\\CdRom1");
        IoDismountVolumeByName(&devName2);

        if (!NT_SUCCESS(NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus,
                                    FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT)))
        {
            NtFreeVirtualMemory(&membuf, &membuf_size, MEM_RELEASE);
            return false;
        }

        bool success = NT_SUCCESS(NtDeviceIoControlFile(h, NULL, NULL, NULL, &ioStatus,
                                                         OVL_IOCTL_VIRTUAL_CDROM_ATTACH, asd, sizeof(OVL_ATTACH_LEGACY), NULL, 0));

        // Dismount after attach (matches Hermes sequence)
        IoDismountVolumeByName(&devName2);

        NtClose(h);
        NtFreeVirtualMemory(&membuf, &membuf_size, MEM_RELEASE);

        if (success)
        {
            return true;
        }
        OverlayAlert(_T("Legacy attach IOCTL failed."));
        return false;
    }
#else
    return false;
#endif
}

static bool DetectIsoMounted()
{
#ifdef _XBOX
    const USHORT build = XboxKrnlVersion->Build;
    ANSI_STRING devName;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE h;

    if (build >= 8008)
        RtlInitAnsiString(&devName, "\\Device\\Virtual0\\Image0");
    else
        RtlInitAnsiString(&devName, "\\Device\\CdRom1");

    InitializeObjectAttributes(&objAttr, &devName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    NTSTATUS status = NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus,
                                  FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (NT_SUCCESS(status))
    {
        NtClose(h);
        return true;
    }
#endif
    return false;
}

static bool DetachIso()
{
#ifdef _XBOX
    const USHORT build = XboxKrnlVersion->Build;
    ANSI_STRING devName;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE h;
    DWORD ioctl;

    if (build >= 8008)
    {
        RtlInitAnsiString(&devName, "\\Device\\Virtual0\\Image0");
        ioctl = OVL_IOCTL_VIRTUAL_DETACH;
    }
    else
    {
        RtlInitAnsiString(&devName, "\\Device\\CdRom1");
        ioctl = OVL_IOCTL_VIRTUAL_CDROM_DETACH;
    }

    InitializeObjectAttributes(&objAttr, &devName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    NTSTATUS status = NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus,
                                  FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(status))
        return false;

    bool ok = NT_SUCCESS(NtDeviceIoControlFile(h, NULL, NULL, NULL, &ioStatus, ioctl, NULL, 0, NULL, 0));
    NtClose(h);

    if (ok)
    {
        g_isoMounted = false;
        g_discPresent = false;
        g_discTitle[0] = 0;
    }
    return ok;
#else
    return false;
#endif
}

static void ScanDisc()
{
    g_discPresent = DiscManager::IsDiscPresent();
    g_isoMounted = DetectIsoMounted();

    if (g_discPresent)
    {
        WCHAR wTitle[64];
        if (DiscManager::ReadDiscTitle(wTitle, ARRAYSIZE(wTitle)))
        {
#ifdef _UNICODE
            _tcsncpy(g_discTitle, wTitle, 63);
#else
            WideCharToMultiByte(CP_ACP, 0, wTitle, -1, g_discTitle, 64, NULL, NULL);
#endif
        }
        else
            _tcscpy(g_discTitle, _T("<Unknown>"));
    }
    else
    {
        _tcscpy(g_discTitle, _T("<No Disc>"));
    }

    g_discScanned = true;

    // If an ISO is mounted, try to load title info
    if (g_isoMounted && !g_isoTitleInfoLoaded)
        LoadMountedIsoInfo();
}

// Menu items built dynamically based on state
#define DISC_MAX_MENU 5

struct DiscMenuItem
{
    const TCHAR* label;
    bool enabled;
};

static DiscMenuItem g_discMenu[DISC_MAX_MENU];
static int g_discMenuCount = 0;

enum DiscAction
{
    DISC_ACTION_DUMP = 0,
    DISC_ACTION_DETACH,
    DISC_ACTION_LAUNCH,
    DISC_ACTION_BROWSE_ISO,
    DISC_ACTION_RESCAN
};

static int g_discMenuActions[DISC_MAX_MENU];

static void BuildDiscMenu()
{
    g_discMenuCount = 0;

    if (g_discPresent && !g_isoMounted)
    {
        g_discMenu[g_discMenuCount].label = _T("Dump Disc");
        g_discMenu[g_discMenuCount].enabled = true;
        g_discMenuActions[g_discMenuCount] = DISC_ACTION_DUMP;
        g_discMenuCount++;
    }

    if (g_isoMounted)
    {
        g_discMenu[g_discMenuCount].label = _T("Detach ISO");
        g_discMenu[g_discMenuCount].enabled = true;
        g_discMenuActions[g_discMenuCount] = DISC_ACTION_DETACH;
        g_discMenuCount++;
    }

    if (g_discPresent)
    {
        g_discMenu[g_discMenuCount].label = _T("Launch Disc");
        g_discMenu[g_discMenuCount].enabled = true;
        g_discMenuActions[g_discMenuCount] = DISC_ACTION_LAUNCH;
        g_discMenuCount++;
    }

    g_discMenu[g_discMenuCount].label = _T("Browse ISOs");
    g_discMenu[g_discMenuCount].enabled = true;
    g_discMenuActions[g_discMenuCount] = DISC_ACTION_BROWSE_ISO;
    g_discMenuCount++;

    g_discMenu[g_discMenuCount].label = _T("Rescan");
    g_discMenu[g_discMenuCount].enabled = true;
    g_discMenuActions[g_discMenuCount] = DISC_ACTION_RESCAN;
    g_discMenuCount++;

    if (g_discMenuIndex >= g_discMenuCount)
        g_discMenuIndex = 0;
}

static void DrawIsoDrivePicker(int cx, int cy, int cw, int ch)
{
    float y = (float)cy;

    OverlayFontDraw(_T("Select Drive"), (float)cx, y, FONT_H, 0xFFFFFFFF);
    y += LINE_H + 4;

    DrawSolidRect(cx, (int)y, cw, 1, D3DCOLOR_ARGB(64, 255, 255, 255));
    y += 8;

    if (g_isoDriveCount == 0)
    {
        OverlayFontDraw(_T("No drives available"), (float)cx, y, FONT_H, 0xFF888888);
        return;
    }

    for (int i = 0; i < g_isoDriveCount; ++i)
    {
        DWORD color = (i == g_isoDriveIndex) ? 0xFFFFFFFF : 0xFF888888;
        if (i == g_isoDriveIndex)
            DrawSolidRect(cx, (int)y - 2, 300, LINE_H + 4, D3DCOLOR_ARGB(160, 60, 60, 180));
        OverlayFontDraw(g_isoDrives[i].szLabel, (float)(cx + 10), y, FONT_H, color);
        y += LINE_H + 6;
    }
}

static void DrawIsoBrowser(int cx, int cy, int cw, int ch)
{
    // Drive picker first
    if (g_isoBrowsePickingDrive)
    {
        DrawIsoDrivePicker(cx, cy, cw, ch);
        return;
    }

    float y = (float)cy;
    TCHAR buf[ISO_MAX_PATH + 32];

    // Show current directory
    _stprintf(buf, _T("Dir: %hs"), g_isoBrowseDir);
    OverlayFontDraw(buf, (float)cx, y, FONT_H, 0xFF88CCFF);
    y += LINE_H + 4;

    DrawSolidRect(cx, (int)y, cw, 1, D3DCOLOR_ARGB(64, 255, 255, 255));
    y += 8;

    if (g_isoFileCount == 0)
    {
        OverlayFontDraw(_T("No ISO/CCI files found"), (float)cx, y, FONT_H, 0xFF888888);
        y += LINE_H;
        OverlayFontDraw(_T("B: Back"), (float)cx, y, FONT_H, 0xFF555555);
        return;
    }

    // Scrolling window
    const int maxVisible = 10;
    int scrollStart = 0;
    if (g_isoBrowseIndex >= maxVisible)
        scrollStart = g_isoBrowseIndex - maxVisible + 1;

    int visEnd = min(scrollStart + maxVisible, g_isoFileCount);

    for (int i = scrollStart; i < visEnd; ++i)
    {
        DWORD color = (i == g_isoBrowseIndex) ? 0xFFFFFFFF : 0xFF888888;
        if (i == g_isoBrowseIndex)
            DrawSolidRect(cx, (int)y - 2, cw, LINE_H + 4, D3DCOLOR_ARGB(160, 60, 60, 180));

        if (g_isoFiles[i].bIsDir)
            _stprintf(buf, _T("[%hs]"), g_isoFiles[i].szName);
        else
            _stprintf(buf, _T("%hs"), g_isoFiles[i].szName);

        OverlayFontDraw(buf, (float)(cx + 10), y, FONT_H, color);
        y += LINE_H + 2;
    }

    // Scroll indicator
    if (g_isoFileCount > maxVisible)
    {
        _stprintf(buf, _T("%d / %d"), g_isoBrowseIndex + 1, g_isoFileCount);
        OverlayFontDraw(buf, (float)(cx + cw - 100), (float)cy, FONT_H, 0xFF666666);
    }
}

static void DrawDiscPage(int cx, int cy, int cw, int ch)
{
    TCHAR buf[256];
    float y = (float)cy;
    const DWORD clr = 0xFFCCCCCC;

    // First time: scan
    if (!g_discScanned)
    {
        ScanDisc();
        BuildDiscMenu();
    }

    // ISO launching with delay
    if (g_isoLaunching)
    {
        OverlayFontDraw(g_isoLaunchName, (float)cx, y, FONT_H, 0xFFFFCC44);
        if (GetTickCount() >= g_isoLaunchTick)
        {
            g_isoLaunching = false;
#ifdef _XBOX
            const USHORT build = XboxKrnlVersion->Build;
            if (build == 5003 || build == 5004)
            {
                STRING sMountPoint, sDevicePath;
                RtlInitAnsiString(&sMountPoint, "\\??\\D:");
                RtlInitAnsiString(&sDevicePath, "\\Device\\CdRom0");
                IoDeleteSymbolicLink(&sMountPoint);
                IoCreateSymbolicLink(&sMountPoint, &sDevicePath);
                XLaunchNewImage("D:\\default.xbe", NULL);
            }
            else
            {
                HalReturnToFirmware((FIRMWARE_REENTRY)2);
            }
#endif
        }
        return;
    }

    // ISO browser mode
    if (g_discMode == DISC_MODE_ISO_BROWSE)
    {
        DrawIsoBrowser(cx, cy, cw, ch);
        return;
    }

    // Idle / menu mode
    if (g_discMode == DISC_MODE_IDLE)
    {
        // Status header with title icon for mounted ISOs
        if (g_isoMounted && g_isoTitleInfoLoaded)
        {
            float iconSize = 80.0f;
            float textX = (float)cx;
            float infoY = y;

            // Draw title icon if available
            if (g_pIsoTitleImage)
            {
                DrawOverlayImage(g_pIsoTitleImage, (float)cx, y, iconSize, iconSize);
                textX = (float)cx + iconSize + 12.0f;
            }

            // Title name (bright white, slightly larger feel)
            _stprintf(buf, _T("%hs"), g_isoTitleName);
            OverlayFontDraw(buf, textX, infoY, FONT_H, 0xFFFFFFFF);
            infoY += LINE_H;

            // Title ID
            _stprintf(buf, _T("Title ID:  %08X"), g_isoTitleId);
            OverlayFontDraw(buf, textX, infoY, FONT_H, 0xFF88CCFF);
            infoY += LINE_H;

            // Region
            _stprintf(buf, _T("Region:    %hs"), GetRegionString(g_isoGameRegion));
            OverlayFontDraw(buf, textX, infoY, FONT_H, 0xFFCCCCCC);
            infoY += LINE_H;

            // Media type + disc number
            if (g_isoDiskNumber > 0)
                _stprintf(buf, _T("Media:     %hs  (Disc %d)"), GetMediaTypeString(g_isoMediaFlag), g_isoDiskNumber);
            else
                _stprintf(buf, _T("Media:     %hs"), GetMediaTypeString(g_isoMediaFlag));
            OverlayFontDraw(buf, textX, infoY, FONT_H, 0xFFCCCCCC);
            infoY += LINE_H;

            // Status badge
            OverlayFontDraw(_T("ISO Mounted"), textX, infoY, FONT_H, 0xFF44FF44);

            // Advance past the icon area or info area, whichever is taller
            float infoHeight = infoY + LINE_H - y;
            y += (iconSize > infoHeight ? iconSize : infoHeight) + 10.0f;
        }
        else if (g_isoMounted)
        {
            _stprintf(buf, _T("ISO Mounted: %s"), g_discTitle);
            OverlayFontDraw(buf, (float)cx, y, FONT_H, 0xFF88CCFF);
            y += LINE_H + 8;
        }
        else if (g_discPresent)
        {
            _stprintf(buf, _T("Disc: %s"), g_discTitle);
            OverlayFontDraw(buf, (float)cx, y, FONT_H, 0xFF44FF44);
            y += LINE_H + 8;
        }
        else
        {
            OverlayFontDraw(_T("No disc detected"), (float)cx, y, FONT_H, 0xFF888888);
            y += LINE_H + 8;
        }

        DrawSolidRect(cx, (int)y, cw, 1, D3DCOLOR_ARGB(64, 255, 255, 255));
        y += 10;

        // Menu
        for (int i = 0; i < g_discMenuCount; ++i)
        {
            DWORD color = (i == g_discMenuIndex) ? 0xFFFFFFFF : 0xFF888888;
            if (i == g_discMenuIndex)
                DrawSolidRect(cx, (int)y - 2, 300, LINE_H + 4, D3DCOLOR_ARGB(160, 60, 60, 180));
            OverlayFontDraw(g_discMenu[i].label, (float)(cx + 10), y, FONT_H, color);
            y += LINE_H + 6;
        }

        // Handle action
        if (g_discMenuConfirmed && g_discMenuIndex < g_discMenuCount)
        {
            switch (g_discMenuActions[g_discMenuIndex])
            {
            case DISC_ACTION_DUMP:
                g_logLineCount = 0;
                g_discMode = DISC_MODE_DUMPING;
                extractPath = DiscManager::GetFinalExtractPath();
                DiscManager::StartExtraction(extractPath);
                break;
            case DISC_ACTION_DETACH:
                if (DetachIso())
                {
                    ClearMountedIsoInfo();
                    // Force rescan to update all state
                    g_discScanned = false;
                    ScanDisc();
                    BuildDiscMenu();
                    g_discMenuIndex = 0;
                }
                else
                {
                    OverlayLogf(L"Detach failed.");
                }
                break;
            case DISC_ACTION_LAUNCH:
                // Send Discord presence on actual launch
                if (g_isoTitleInfoLoaded && g_isoTitleId != 0)
                {
                    char hexId[16];
                    _snprintf(hexId, sizeof(hexId), "%08X", g_isoTitleId);
                    SendDiscordRelayFromConfig(hexId);
                }
                // D: is valid here — ISO was mounted at boot
                XLaunchNewImage("D:\\default.xbe", NULL);
                break;
            case DISC_ACTION_BROWSE_ISO:
                g_discMode = DISC_MODE_ISO_BROWSE;
                g_isoBrowsePickingDrive = true;
                BuildIsoDriveList();
                break;
            case DISC_ACTION_RESCAN:
                g_discScanned = false;
                ScanDisc();
                BuildDiscMenu();
                break;
            }
            g_discMenuConfirmed = false;
        }
    }
    // Dumping mode
    else if (g_discMode == DISC_MODE_DUMPING)
    {
        _stprintf(buf, _T("Dumping: %s"), g_discTitle);
        OverlayFontDraw(buf, (float)cx, y, FONT_H, 0xFFFFCC44);
        y += LINE_H;

        if (extractPath)
        {
            _stprintf(buf, _T("To: %hs"), extractPath);
            OverlayFontDraw(buf, (float)cx, y, FONT_H, clr);
            y += LINE_H;
        }

        if (DiscManager::IsExtractionComplete())
            g_discMode = DISC_MODE_DUMP_DONE;

        // Log lines
        y += 8;
        const int maxVisible = 8;
        int visible = min(g_logLineCount, maxVisible);
        int start = (g_logLineCount > maxVisible) ? g_logLineCount - maxVisible : 0;

        for (int i = start; i < g_logLineCount; ++i)
        {
            OverlayFontDraw(g_logLines[i], (float)cx, y, FONT_H, 0xFFAAAAAA);
            y += LINE_H;
        }
    }
    // Dump complete
    else if (g_discMode == DISC_MODE_DUMP_DONE)
    {
        OverlayFontDraw(_T("Dump complete!"), (float)cx, y, FONT_H, 0xFF44FF44);
        y += LINE_H;

        if (extractPath)
        {
            _stprintf(buf, _T("Saved to: %hs"), extractPath);
            OverlayFontDraw(buf, (float)cx, y, FONT_H, clr);
            y += LINE_H;
        }

        y += LINE_H;
        OverlayFontDraw(_T("Press B to return"), (float)cx, y, FONT_H, 0xFF666666);
    }
}

// ============================================================================
// Page: Network
// ============================================================================

static int g_netScrollOffset = 0;
static int g_netLineCount = 0;

static void DrawNetworkPage(int cx, int cy, int cw, int ch)
{
    static bool bNetInfoInit = false;
    static TCHAR szIP[32], szSubnet[32], szGateway[32], szDNS1[32], szDNS2[32];

    if (!bNetInfoInit)
    {
        bNetInfoInit = true;

        XNetConfigStatus status = {};
        if (XNetGetConfigStatus(&status) == 0)
        {
            _sntprintf(szIP, countof(szIP), _T("%d.%d.%d.%d"),
                       status.ina.S_un.S_un_b.s_b1, status.ina.S_un.S_un_b.s_b2,
                       status.ina.S_un.S_un_b.s_b3, status.ina.S_un.S_un_b.s_b4);

            _sntprintf(szSubnet, countof(szSubnet), _T("%d.%d.%d.%d"),
                       status.inaMask.S_un.S_un_b.s_b1, status.inaMask.S_un.S_un_b.s_b2,
                       status.inaMask.S_un.S_un_b.s_b3, status.inaMask.S_un.S_un_b.s_b4);

            _sntprintf(szGateway, countof(szGateway), _T("%d.%d.%d.%d"),
                       status.inaGateway.S_un.S_un_b.s_b1, status.inaGateway.S_un.S_un_b.s_b2,
                       status.inaGateway.S_un.S_un_b.s_b3, status.inaGateway.S_un.S_un_b.s_b4);

            _sntprintf(szDNS1, countof(szDNS1), _T("%d.%d.%d.%d"),
                       status.inaDnsPrimary.S_un.S_un_b.s_b1, status.inaDnsPrimary.S_un.S_un_b.s_b2,
                       status.inaDnsPrimary.S_un.S_un_b.s_b3, status.inaDnsPrimary.S_un.S_un_b.s_b4);

            _sntprintf(szDNS2, countof(szDNS2), _T("%d.%d.%d.%d"),
                       status.inaDnsSecondary.S_un.S_un_b.s_b1, status.inaDnsSecondary.S_un.S_un_b.s_b2,
                       status.inaDnsSecondary.S_un.S_un_b.s_b3, status.inaDnsSecondary.S_un.S_un_b.s_b4);
        }
        else
        {
            _tcscpy(szIP, _T("Unavailable"));
            _tcscpy(szSubnet, _T("N/A"));
            _tcscpy(szGateway, _T("N/A"));
            _tcscpy(szDNS1, _T("N/A"));
            _tcscpy(szDNS2, _T("N/A"));
        }
    }

    // Build all lines into a buffer for scrolling
    #define NET_MAX_LINES 30
    struct NetLine {
        TCHAR text[128];
        DWORD color;
        bool  isSeparator;
    };
    static NetLine lines[NET_MAX_LINES];
    int n = 0;

    const DWORD clr = 0xFFCCCCCC;
    const DWORD hdr = 0xFFFFFFFF;

    // Network section
    _tcscpy(lines[n].text, _T("Network")); lines[n].color = hdr; lines[n].isSeparator = false; n++;

    _stprintf(lines[n].text, _T("  IP Address: %s"), szIP); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  Subnet:     %s"), szSubnet); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  Gateway:    %s"), szGateway); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  DNS 1:      %s"), szDNS1); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _stprintf(lines[n].text, _T("  DNS 2:      %s"), szDNS2); lines[n].color = clr; lines[n].isSeparator = false; n++;

    lines[n].text[0] = 0; lines[n].color = 0; lines[n].isSeparator = true; n++;

    // FTP section
    _tcscpy(lines[n].text, _T("FTP Server")); lines[n].color = hdr; lines[n].isSeparator = false; n++;

    bool ftpUp = net::isFtpRunning();
    _stprintf(lines[n].text, _T("  Status:   %s"), ftpUp ? _T("Running") : _T("Stopped"));
    lines[n].color = ftpUp ? 0xFF88FF88 : 0xFFFF8888; lines[n].isSeparator = false; n++;

    _tcscpy(lines[n].text, _T("  Port:     21")); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _tcscpy(lines[n].text, _T("  Username: xbox")); lines[n].color = clr; lines[n].isSeparator = false; n++;
    _tcscpy(lines[n].text, _T("  Password: xbox")); lines[n].color = clr; lines[n].isSeparator = false; n++;

    lines[n].text[0] = 0; lines[n].color = 0; lines[n].isSeparator = true; n++;

    // Discord section
    _tcscpy(lines[n].text, _T("Discord RPC")); lines[n].color = hdr; lines[n].isSeparator = false; n++;

    _stprintf(lines[n].text, _T("  Enabled: %s"), g_DiscordEnabled ? _T("Yes") : _T("No"));
    lines[n].color = clr; lines[n].isSeparator = false; n++;

    if (g_DiscordEnabled)
    {
        _stprintf(lines[n].text, _T("  Relay IP:   %hs"), g_DiscordIP);
        lines[n].color = clr; lines[n].isSeparator = false; n++;

        _stprintf(lines[n].text, _T("  Relay Port: %d"), g_DiscordPort);
        lines[n].color = clr; lines[n].isSeparator = false; n++;
    }

    g_netLineCount = n;

    // Scrolled rendering
    const int maxVisible = ch / LINE_H;

    if (g_netScrollOffset > n - maxVisible)
        g_netScrollOffset = max(0, n - maxVisible);

    int visEnd = min(g_netScrollOffset + maxVisible, n);
    float y = (float)cy;

    for (int i = g_netScrollOffset; i < visEnd; i++)
    {
        if (lines[i].isSeparator)
        {
            DrawSolidRect(cx, (int)y + LINE_H / 2, cw - 12, 1, D3DCOLOR_ARGB(64, 255, 255, 255));
        }
        else
        {
            OverlayFontDraw(lines[i].text, (float)cx, y, FONT_H, lines[i].color);
        }
        y += LINE_H;
    }

    // Scroll bar (right edge)
    if (n > maxVisible)
    {
        int barX = cx + cw - 6;
        int barH = ch;
        int thumbH = max(20, barH * maxVisible / n);
        int thumbY = cy + (barH - thumbH) * g_netScrollOffset / (n - maxVisible);

        // Track
        DrawSolidRect(barX, cy, 4, barH, D3DCOLOR_ARGB(40, 255, 255, 255));
        // Thumb
        DrawSolidRect(barX, thumbY, 4, thumbH, D3DCOLOR_ARGB(160, 255, 255, 255));
    }
}

// ============================================================================
// Page: Toolbox
// ============================================================================

static int g_toolboxIndex = 0;
static bool g_toolboxConfirmed = false;

enum ToolboxAction
{
    TOOLBOX_DOWNLOAD_ICONS,
    TOOLBOX_DOWNLOAD_SKINS,
    TOOLBOX_DOWNLOAD_FONTS,
    TOOLBOX_CLEAR_CACHE,
    TOOLBOX_REBOOT,
    TOOLBOX_SHUTDOWN,
    TOOLBOX_COUNT
};

static const TCHAR* g_szToolboxItems[] = {
    _T("Download Icons"),
    _T("Download Skins"),
    _T("Download Fonts"),
    _T("Clear Cache"),
    _T("Reboot"),
    _T("Shutdown")
};

static void ClearXboxCache()
{
    // Delete cache partition contents
    // X, Y, Z are cache drives on Xbox
    const char* cacheDrives[] = {"X:\\", "Y:\\", "Z:\\"};
    for (int d = 0; d < 3; ++d)
    {
        char searchPath[MAX_PATH];
        _snprintf(searchPath, MAX_PATH, "%s*", cacheDrives[d]);

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;

        do
        {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;

            char fullPath[MAX_PATH];
            _snprintf(fullPath, MAX_PATH, "%s%s", cacheDrives[d], fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                // Skip directory removal for safety - just delete files
            }
            else
            {
                DeleteFileA(fullPath);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static void DrawToolboxPage(int cx, int cy, int cw, int ch)
{
    float y = (float)cy;

    for (int i = 0; i < TOOLBOX_COUNT; ++i)
    {
        DWORD color = (i == g_toolboxIndex) ? 0xFFFFFFFF : 0xFF888888;
        if (i == g_toolboxIndex)
            DrawSolidRect(cx, (int)y - 2, 300, LINE_H + 4, D3DCOLOR_ARGB(160, 60, 60, 180));
        OverlayFontDraw(g_szToolboxItems[i], (float)(cx + 10), y, FONT_H, color);
        y += LINE_H + 6;
    }

    if (g_toolboxConfirmed)
    {
        switch (g_toolboxIndex)
        {
        case TOOLBOX_DOWNLOAD_ICONS:
            // TODO: Implement icon download
            OverlayAlert(_T("Downloading icons is not yet implemented."));
            break;
        case TOOLBOX_DOWNLOAD_SKINS:
            OverlayAlert(_T("Not compatible with this build."));
            break;
        case TOOLBOX_DOWNLOAD_FONTS:
            OverlayAlert(_T("Not compatible with this build."));
            break;
        case TOOLBOX_CLEAR_CACHE:
            ClearXboxCache();
            OverlayAlert(_T("Cache cleared."));
            break;
        case TOOLBOX_REBOOT:
            HalReturnToFirmware((FIRMWARE_REENTRY)2);
            break;
        case TOOLBOX_SHUTDOWN:
            HalReturnToFirmware((FIRMWARE_REENTRY)5);
            break;
        }
        g_toolboxConfirmed = false;
    }
}

// ============================================================================
// Page: Files (general-purpose file browser + .xbe launcher)
// ============================================================================
//
// Two modes: drive picker and file browser. Pressing A on a drive
// enters it. Pressing A on a directory entry enters it. Pressing A on
// a .xbe launches it via XLaunchNewImage. Pressing B from the file
// browser goes up one directory; at the root of a drive, B returns to
// the drive picker. Cursor auto-scrolls so it stays in view.

#define FILES_MAX_DRIVES  16
#define FILES_MAX_ENTRIES 512
#define FILES_PATH_LEN    260

struct FileEntry
{
    char  szName[FILES_PATH_LEN];
    char  szPath[FILES_PATH_LEN];
    bool  bIsDir;
    DWORD size;
};

struct FileDriveEntry
{
    char  szRoot[8];
    TCHAR szLabel[32];
};

static FileDriveEntry g_filesDrives[FILES_MAX_DRIVES];
static int  g_filesDriveCount   = 0;
static int  g_filesDriveIndex   = 0;

static FileEntry g_filesEntries[FILES_MAX_ENTRIES];
static int  g_filesEntryCount   = 0;
static int  g_filesIndex        = 0;
static int  g_filesScrollOffset = 0;
static char g_filesDir[FILES_PATH_LEN] = "";
static bool g_filesPickingDrive = true;

// Pending-launch confirmation state. We never call XLaunchNewImage
// straight from the user's A press; we stage the path here and ask
// for a second confirmation, so a misclick or a default.xbe in an
// unexpected place doesn't bounce them out of Theseus.
static bool g_filesConfirmingLaunch    = false;
static char g_filesPendingLaunchPath[FILES_PATH_LEN] = "";

static void FilesBuildDriveList()
{
    g_filesDriveCount = 0;

    struct { const char* root; const TCHAR* label; } candidates[] = {
        {"C:\\", _T("C: System")},
        {"E:\\", _T("E: Data")},
        {"F:\\", _T("F: Games")},
        {"G:\\", _T("G: Extended")},
        {"X:\\", _T("X: Cache")},
        {"Y:\\", _T("Y: Cache")},
        {"Z:\\", _T("Z: Cache")},
        {"R:\\", _T("R: Extended")},
        {"S:\\", _T("S: Extended")},
        {"D:\\", _T("D: DVD")},
    };

    for (int i = 0; i < ARRAYSIZE(candidates); ++i)
    {
        if (g_filesDriveCount >= FILES_MAX_DRIVES)
            break;

        ULARGE_INTEGER free = {}, total = {}, dummy;
        if (GetDiskFreeSpaceExA(candidates[i].root, &free, &total, &dummy))
        {
            FileDriveEntry* e = &g_filesDrives[g_filesDriveCount++];
            strncpy(e->szRoot, candidates[i].root, 7);
            e->szRoot[7] = '\0';
            _tcsncpy(e->szLabel, candidates[i].label, 31);
            e->szLabel[31] = 0;
        }
    }
}

static int __cdecl FilesEntryCompare(const void* a, const void* b)
{
    const FileEntry* fa = (const FileEntry*)a;
    const FileEntry* fb = (const FileEntry*)b;
    // ".." always first
    if (strcmp(fa->szName, "..") == 0) return -1;
    if (strcmp(fb->szName, "..") == 0) return  1;
    // Directories before files
    if (fa->bIsDir != fb->bIsDir)
        return fa->bIsDir ? -1 : 1;
    // Same kind: alphabetic
    return _stricmp(fa->szName, fb->szName);
}

static void FilesBrowseDir(const char* dir)
{
    g_filesEntryCount   = 0;
    g_filesIndex        = 0;
    g_filesScrollOffset = 0;
    strncpy(g_filesDir, dir, FILES_PATH_LEN - 1);
    g_filesDir[FILES_PATH_LEN - 1] = '\0';

    // ".." entry unless at drive root (e.g. "F:\")
    if (strlen(dir) > 3)
    {
        FileEntry* e = &g_filesEntries[g_filesEntryCount++];
        strcpy(e->szName, "..");
        strcpy(e->szPath, "..");
        e->bIsDir = true;
        e->size   = 0;
    }

    char searchPath[FILES_PATH_LEN];
    _snprintf(searchPath, FILES_PATH_LEN, "%s*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (g_filesEntryCount >= FILES_MAX_ENTRIES)
            break;

        // Skip "." and ".." (we add our own ".." above)
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        FileEntry* e = &g_filesEntries[g_filesEntryCount++];
        strncpy(e->szName, fd.cFileName, FILES_PATH_LEN - 1);
        e->szName[FILES_PATH_LEN - 1] = '\0';
        _snprintf(e->szPath, FILES_PATH_LEN, "%s%s", dir, fd.cFileName);
        e->bIsDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->size   = fd.nFileSizeLow;
    }
    while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    qsort(g_filesEntries, g_filesEntryCount, sizeof(FileEntry), FilesEntryCompare);
}

static void FilesGoUp()
{
    // Strip trailing slash
    int n = (int)strlen(g_filesDir);
    if (n > 0 && (g_filesDir[n - 1] == '\\' || g_filesDir[n - 1] == '/'))
        g_filesDir[n - 1] = 0;

    char* lastSlash = strrchr(g_filesDir, '\\');
    if (!lastSlash)
        lastSlash = strrchr(g_filesDir, '/');

    if (!lastSlash || lastSlash <= g_filesDir + 2)
    {
        // We were at "X:\..." with one slash; go back to drive picker
        g_filesPickingDrive = true;
        return;
    }

    *(lastSlash + 1) = 0;
    FilesBrowseDir(g_filesDir);
}

static void FilesActivateEntry(const FileEntry& e)
{
    if (strcmp(e.szName, "..") == 0)
    {
        FilesGoUp();
        return;
    }

    if (e.bIsDir)
    {
        char newDir[FILES_PATH_LEN];
        _snprintf(newDir, FILES_PATH_LEN, "%s\\", e.szPath);
        FilesBrowseDir(newDir);
        return;
    }

    // .xbe -> stage for launch and ask for confirmation. ISO/CCI mounting
    // is owned by the Disc page (proper Cerbios/legacy attach UI lives
    // there), so the file manager intentionally doesn't try to handle it.
    int len = (int)strlen(e.szName);
    if (len >= 4 && _stricmp(e.szName + len - 4, ".xbe") == 0)
    {
        strncpy(g_filesPendingLaunchPath, e.szPath, FILES_PATH_LEN - 1);
        g_filesPendingLaunchPath[FILES_PATH_LEN - 1] = 0;
        g_filesConfirmingLaunch = true;
        return;
    }

    // Other file types: not handled yet. Could add file info popup later.
}

static void FormatFileSize(DWORD bytes, TCHAR* buf, int bufChars)
{
    if (bytes < 1024)
        _sntprintf(buf, bufChars, _T("%u B"), (unsigned)bytes);
    else if (bytes < 1024 * 1024)
        _sntprintf(buf, bufChars, _T("%.1f KB"), bytes / 1024.0f);
    else if (bytes < 1024u * 1024u * 1024u)
        _sntprintf(buf, bufChars, _T("%.1f MB"), bytes / (1024.0f * 1024.0f));
    else
        _sntprintf(buf, bufChars, _T("%.2f GB"), bytes / (1024.0f * 1024.0f * 1024.0f));
    buf[bufChars - 1] = 0;
}

static void DrawFilesDrivePicker(int cx, int cy, int cw, int ch)
{
    if (g_filesDriveCount == 0)
        FilesBuildDriveList();

    OverlayFontDraw(_T("Select drive"), (float)cx, (float)cy, FONT_H, 0xFFFFFFFF);

    if (g_filesDriveCount == 0)
    {
        OverlayFontDraw(_T("No drives available"), (float)(cx + 10), (float)(cy + LINE_H + 8), FONT_H, 0xFF888888);
        return;
    }

    if (g_filesDriveIndex >= g_filesDriveCount) g_filesDriveIndex = g_filesDriveCount - 1;
    if (g_filesDriveIndex < 0)                  g_filesDriveIndex = 0;

    const int rowH = LINE_H + 4;
    int y = cy + LINE_H + 8;

    for (int i = 0; i < g_filesDriveCount; ++i)
    {
        bool selected = (i == g_filesDriveIndex);
        DWORD color = selected ? 0xFFFFFFFF : 0xFFAAAAAA;

        if (selected)
            DrawSolidRect(cx, y - 2, cw - 12, rowH, D3DCOLOR_ARGB(160, 60, 60, 180));

        OverlayFontDraw(g_filesDrives[i].szLabel, (float)(cx + 10), (float)y, FONT_H, color);

        ULARGE_INTEGER free = {}, total = {}, dummy;
        if (GetDiskFreeSpaceExA(g_filesDrives[i].szRoot, &free, &total, &dummy) && total.QuadPart > 0)
        {
            TCHAR sizeStr[40];
            _sntprintf(sizeStr, 40, _T("%.1f / %.1f GB"),
                       free.QuadPart  / (1024.0f * 1024.0f * 1024.0f),
                       total.QuadPart / (1024.0f * 1024.0f * 1024.0f));
            sizeStr[39] = 0;
            OverlayFontDraw(sizeStr, (float)(cx + cw - 160), (float)y, FONT_H, 0xFF888888);
        }

        y += rowH;
    }
}

static void DrawFilesBrowser(int cx, int cy, int cw, int ch)
{
    // Path header (truncate from left if too long)
    TCHAR pathBuf[FILES_PATH_LEN];
    Unicode(pathBuf, g_filesDir, FILES_PATH_LEN);
    OverlayFontDraw(pathBuf, (float)cx, (float)cy, FONT_H, 0xFF88CCFF);

    int contentY = cy + LINE_H + 8;
    int contentH = ch - (LINE_H + 8);
    const int rowH = LINE_H + 4;

    if (g_filesEntryCount == 0)
    {
        OverlayFontDraw(_T("(empty)"), (float)(cx + 10), (float)contentY, FONT_H, 0xFF888888);
        return;
    }

    if (g_filesIndex >= g_filesEntryCount) g_filesIndex = g_filesEntryCount - 1;
    if (g_filesIndex < 0)                  g_filesIndex = 0;

    int maxVisible = contentH / rowH;
    if (maxVisible < 1) maxVisible = 1;

    if (g_filesIndex < g_filesScrollOffset)
        g_filesScrollOffset = g_filesIndex;
    else if (g_filesIndex >= g_filesScrollOffset + maxVisible)
        g_filesScrollOffset = g_filesIndex - maxVisible + 1;

    if (g_filesScrollOffset > g_filesEntryCount - maxVisible)
        g_filesScrollOffset = max(0, g_filesEntryCount - maxVisible);
    if (g_filesScrollOffset < 0)
        g_filesScrollOffset = 0;

    int visEnd = min(g_filesScrollOffset + maxVisible, g_filesEntryCount);
    int y = contentY;

    for (int i = g_filesScrollOffset; i < visEnd; ++i)
    {
        const FileEntry& e = g_filesEntries[i];
        bool selected = (i == g_filesIndex);

        DWORD color;
        if (e.bIsDir)
            color = selected ? 0xFFFFFFFF : 0xFFC0E0FF;
        else
            color = selected ? 0xFFFFFFFF : 0xFFCCCCCC;

        if (selected)
            DrawSolidRect(cx, y - 2, cw - 12, rowH, D3DCOLOR_ARGB(160, 60, 60, 180));

        TCHAR nameBuf[FILES_PATH_LEN + 4];
        if (e.bIsDir && strcmp(e.szName, "..") != 0)
            _sntprintf(nameBuf, FILES_PATH_LEN + 4, _T("[%hs]"), e.szName);
        else
            _sntprintf(nameBuf, FILES_PATH_LEN + 4, _T("%hs"), e.szName);
        nameBuf[FILES_PATH_LEN + 3] = 0;
        OverlayFontDraw(nameBuf, (float)(cx + 10), (float)y, FONT_H, color);

        if (!e.bIsDir)
        {
            TCHAR sizeStr[24];
            FormatFileSize(e.size, sizeStr, 24);
            OverlayFontDraw(sizeStr, (float)(cx + cw - 110), (float)y, FONT_H, 0xFF888888);
        }

        y += rowH;
    }

    // Scroll bar when content overflows
    if (g_filesEntryCount > maxVisible)
    {
        int barX   = cx + cw - 6;
        int barH   = contentH;
        int thumbH = max(20, barH * maxVisible / g_filesEntryCount);
        int thumbY = contentY + (barH - thumbH) * g_filesScrollOffset / (g_filesEntryCount - maxVisible);

        DrawSolidRect(barX, contentY, 4, barH,   D3DCOLOR_ARGB(40,  255, 255, 255));
        DrawSolidRect(barX, thumbY,   4, thumbH, D3DCOLOR_ARGB(160, 255, 255, 255));
    }
}

static void DrawFilesLaunchConfirm(int sw, int sh)
{
    const int margin = 24;
    const int pw     = 600;
    const int lineH  = (int)FONT_H + 6;

    const int contentLines = 3; // title, path, hint
    const int ph = margin + contentLines * lineH + margin + 32;
    const int px = (sw - pw) / 2;
    const int py = (sh - ph) / 2;

    DrawSolidRect(px, py, pw, ph, D3DCOLOR_ARGB(230, 15, 15, 15));
    DrawSolidRect(px,          py,          pw, 1,  D3DCOLOR_ARGB(120, 255, 255, 255));
    DrawSolidRect(px,          py + ph - 1, pw, 1,  D3DCOLOR_ARGB(120, 255, 255, 255));
    DrawSolidRect(px,          py,          1,  ph, D3DCOLOR_ARGB(120, 255, 255, 255));
    DrawSolidRect(px + pw - 1, py,          1,  ph, D3DCOLOR_ARGB(120, 255, 255, 255));

    int y = py + margin;

    const TCHAR* title = _T("Launch this XBE?");
    float tw = OverlayFontMeasure(title, FONT_H);
    OverlayFontDraw(title, (float)(px + (pw - (int)tw) / 2), (float)y, FONT_H, 0xFFFFFFFF);
    y += lineH + 6;

    TCHAR pathBuf[FILES_PATH_LEN + 4];
    Unicode(pathBuf, g_filesPendingLaunchPath, FILES_PATH_LEN + 4);
    float pw2 = OverlayFontMeasure(pathBuf, FONT_H);
    OverlayFontDraw(pathBuf, (float)(px + (pw - (int)pw2) / 2), (float)y, FONT_H, 0xFF88CCFF);
    y += lineH + 6;

    const TCHAR* hint = _T("A: Launch     B: Cancel");
    float hw = OverlayFontMeasure(hint, FONT_H);
    OverlayFontDraw(hint, (float)(px + (pw - (int)hw) / 2), (float)y, FONT_H, 0xFFAAAAAA);
}

static void DrawFilesPage(int cx, int cy, int cw, int ch)
{
    if (g_filesDriveCount == 0)
        FilesBuildDriveList();

    if (g_filesPickingDrive)
        DrawFilesDrivePicker(cx, cy, cw, ch);
    else
        DrawFilesBrowser(cx, cy, cw, ch);
}

// ============================================================================
// Page: Widgets (HUD widget configuration)
// ============================================================================
//
// Lists every widget registered with widget_layer at boot. For each widget
// the user can flip Enabled / Anchor / Opacity / Color. Per-widget settings
// are not persisted yet; changes apply for the rest of the session.
//
// Cursor is a flat index over interactive rows (4 per widget). Header rows
// showing the widget name are non-interactive and skipped during nav.

static const TCHAR* g_widgetAnchorNames[] = {
    _T("Top Left"), _T("Top Right"), _T("Bottom Left"), _T("Bottom Right")
};

struct WidgetColorPreset { const TCHAR* name; DWORD rgb; };
static const WidgetColorPreset g_widgetColors[] = {
    { _T("White"),      0x00FFFFFF },
    { _T("Xbox Green"), 0x0070C924 },
    { _T("Red"),        0x00E25C5C },
    { _T("Amber"),      0x00FFC04A },
    { _T("Cyan"),       0x0066CCFF },
    { _T("Magenta"),    0x00E060C0 },
};
static const int g_widgetColorCount = ARRAYSIZE(g_widgetColors);

#define WIDGETS_SETTINGS_PER_WIDGET 4

static int g_widgetsCursor       = 0;
static int g_widgetsScrollOffset = 0;

static int FindColorIndex(DWORD rgb)
{
    for (int i = 0; i < g_widgetColorCount; i++)
        if (g_widgetColors[i].rgb == (rgb & 0x00FFFFFF))
            return i;
    return 0; // unknown -> White
}

static int OpacityToTier(int opacity)
{
    // Opacity 0..255 -> tier 0..9 representing 10%..100% in 10% steps.
    int pct = (opacity * 100 + 127) / 255;
    int tier = (pct - 1) / 10;
    if (tier < 0) tier = 0;
    if (tier > 9) tier = 9;
    return tier;
}

static int TierToOpacity(int tier)
{
    if (tier < 0) tier = 0;
    if (tier > 9) tier = 9;
    int pct = (tier + 1) * 10;
    return (pct * 255) / 100;
}

static void WidgetsCycleSetting(Widget* w, int setting)
{
    if (!w) return;
    switch (setting)
    {
    case 0: // Enabled
        w->enabled = !w->enabled;
        break;
    case 1: // Anchor
        w->anchor = (WidgetAnchor)((w->anchor + 1) % 4);
        break;
    case 2: // Opacity
        w->opacity = TierToOpacity((OpacityToTier(w->opacity) + 1) % 10);
        break;
    case 3: // Color
        w->tintRGB = g_widgetColors[(FindColorIndex(w->tintRGB) + 1) % g_widgetColorCount].rgb;
        break;
    }
}

static int WidgetsInteractiveRowCount()
{
    return GetWidgetCount() * WIDGETS_SETTINGS_PER_WIDGET;
}

static void DecodeWidgetCursor(int cursor, int& widx, int& sidx)
{
    widx = cursor / WIDGETS_SETTINGS_PER_WIDGET;
    sidx = cursor % WIDGETS_SETTINGS_PER_WIDGET;
}

static void DrawWidgetsPage(int cx, int cy, int cw, int ch)
{
    int count = GetWidgetCount();
    if (count == 0)
    {
        OverlayFontDraw(_T("No widgets registered."), (float)(cx + 10), (float)cy, FONT_H, 0xFF888888);
        return;
    }

    int total = WidgetsInteractiveRowCount();
    if (g_widgetsCursor >= total) g_widgetsCursor = total - 1;
    if (g_widgetsCursor < 0)      g_widgetsCursor = 0;

    int curWidx, curSidx;
    DecodeWidgetCursor(g_widgetsCursor, curWidx, curSidx);

    const int rowH      = LINE_H + 4;
    const int labelW    = 120;
    const int valueX    = cx + 10 + labelW;
    const int rowW      = 360;

    static const TCHAR* settingNames[WIDGETS_SETTINGS_PER_WIDGET] = {
        _T("Enabled"), _T("Anchor"), _T("Opacity"), _T("Color")
    };

    // Each widget contributes (1 header + 4 settings) = 5 rendered rows.
    const int rowsPerWidget = 1 + WIDGETS_SETTINGS_PER_WIDGET;
    const int totalRows     = count * rowsPerWidget;

    // Cursor's row index in the rendered list: header row of widget wi
    // is at wi*5; settings are wi*5 + 1 + si.
    const int cursorRow = curWidx * rowsPerWidget + 1 + curSidx;

    // Auto-scroll: keep cursorRow visible.
    int maxVisible = ch / rowH;
    if (maxVisible < 1) maxVisible = 1;

    if (cursorRow < g_widgetsScrollOffset)
        g_widgetsScrollOffset = cursorRow;
    else if (cursorRow >= g_widgetsScrollOffset + maxVisible)
        g_widgetsScrollOffset = cursorRow - maxVisible + 1;

    if (g_widgetsScrollOffset > totalRows - maxVisible)
        g_widgetsScrollOffset = max(0, totalRows - maxVisible);
    if (g_widgetsScrollOffset < 0)
        g_widgetsScrollOffset = 0;

    int visEnd = min(g_widgetsScrollOffset + maxVisible, totalRows);

    float y = (float)cy;

    for (int row = g_widgetsScrollOffset; row < visEnd; row++)
    {
        int wi = row / rowsPerWidget;
        int sub = row % rowsPerWidget; // 0 = header, 1..4 = settings
        Widget* w = GetWidgetAt(wi);
        if (!w) { y += rowH; continue; }

        if (sub == 0)
        {
            TCHAR title[64];
            // Widget names are ASCII, %hs is wide-char printf compatible.
            _sntprintf(title, 64, _T("%hs"), w->name ? w->name : "(unnamed)");
            title[63] = 0;
            OverlayFontDraw(title, (float)(cx + 4), y, FONT_H, 0xFFC0C0FF);
        }
        else
        {
            int si = sub - 1;
            bool selected = (wi == curWidx && si == curSidx);
            DWORD labelColor = selected ? 0xFFFFFFFF : 0xFF888888;
            DWORD valueColor = selected ? 0xFFFFFFFF : 0xFFAAAAAA;

            if (selected)
                DrawSolidRect(cx, (int)y - 2, rowW, rowH, D3DCOLOR_ARGB(160, 60, 60, 180));

            OverlayFontDraw(settingNames[si], (float)(cx + 18), y, FONT_H, labelColor);

            TCHAR valueStr[32];
            switch (si)
            {
            case 0:
                _sntprintf(valueStr, 32, w->enabled ? _T("On") : _T("Off"));
                break;
            case 1:
                _sntprintf(valueStr, 32, _T("%s"), g_widgetAnchorNames[w->anchor & 3]);
                break;
            case 2:
                _sntprintf(valueStr, 32, _T("%d%%"), (OpacityToTier(w->opacity) + 1) * 10);
                break;
            case 3:
                _sntprintf(valueStr, 32, _T("%s"), g_widgetColors[FindColorIndex(w->tintRGB)].name);
                break;
            }
            valueStr[31] = 0;
            OverlayFontDraw(valueStr, (float)valueX, y, FONT_H, valueColor);
        }

        y += rowH;
    }

    // Scroll bar (right edge) when content overflows
    if (totalRows > maxVisible)
    {
        int barX = cx + cw - 6;
        int barH = ch;
        int thumbH = max(20, barH * maxVisible / totalRows);
        int thumbY = cy + (barH - thumbH) * g_widgetsScrollOffset / (totalRows - maxVisible);

        DrawSolidRect(barX, cy,     4, barH,   D3DCOLOR_ARGB(40,  255, 255, 255));
        DrawSolidRect(barX, thumbY, 4, thumbH, D3DCOLOR_ARGB(160, 255, 255, 255));
    }
}

// ============================================================================
// Page: Menu
// ============================================================================

static const TCHAR *g_szMenuItems[] = {
    _T("Close Overlay")
};
static const int g_nMenuItemCount = ARRAYSIZE(g_szMenuItems);

static void DrawMenuPage(int cx, int cy, int cw, int ch)
{
    float y = (float)cy;

    for (int i = 0; i < g_nMenuItemCount; ++i)
    {
        DWORD color = (i == g_nMenuIndex) ? 0xFFFFFFFF : 0xFF888888;
        if (i == g_nMenuIndex)
            DrawSolidRect(cx, (int)y - 2, 300, LINE_H + 4, D3DCOLOR_ARGB(160, 60, 60, 180));
        OverlayFontDraw(g_szMenuItems[i], (float)(cx + 10), y, FONT_H, color);
        y += LINE_H + 6;
    }

    // Handle confirmed action
    if (g_bMenuConfirmed)
    {
        switch (g_nMenuIndex)
        {
        case 0: // Close Overlay
            g_bShowOverlay = false;
            g_bOverlayInputCapture = false;
            break;
        }
        g_bMenuConfirmed = false;
    }
}

// ============================================================================
// Input routing (called from Joystick.cpp)
// ============================================================================

void OverlayOnUp()
{
    if (g_bOverlayAlertActive)
        return;

    if (g_nOverlayPage == OVERLAY_PAGE_SYSTEM)
    {
        if (g_sysScrollOffset > 0) g_sysScrollOffset--;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_MENU)
        g_nMenuIndex = (g_nMenuIndex - 1 + g_nMenuItemCount) % g_nMenuItemCount;
    else if (g_nOverlayPage == OVERLAY_PAGE_DISC)
    {
        if (g_discMode == DISC_MODE_ISO_BROWSE)
        {
            if (g_isoBrowsePickingDrive && g_isoDriveCount > 0)
                g_isoDriveIndex = (g_isoDriveIndex - 1 + g_isoDriveCount) % g_isoDriveCount;
            else if (!g_isoBrowsePickingDrive && g_isoFileCount > 0)
                g_isoBrowseIndex = (g_isoBrowseIndex - 1 + g_isoFileCount) % g_isoFileCount;
        }
        else if (g_discMode == DISC_MODE_IDLE && g_discMenuCount > 0)
            g_discMenuIndex = (g_discMenuIndex - 1 + g_discMenuCount) % g_discMenuCount;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_NETWORK)
    {
        if (g_netScrollOffset > 0) g_netScrollOffset--;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_TOOLBOX)
        g_toolboxIndex = (g_toolboxIndex - 1 + TOOLBOX_COUNT) % TOOLBOX_COUNT;
    else if (g_nOverlayPage == OVERLAY_PAGE_FILES)
    {
        if (g_filesPickingDrive && g_filesDriveCount > 0)
            g_filesDriveIndex = (g_filesDriveIndex - 1 + g_filesDriveCount) % g_filesDriveCount;
        else if (!g_filesPickingDrive && g_filesEntryCount > 0)
            g_filesIndex = (g_filesIndex - 1 + g_filesEntryCount) % g_filesEntryCount;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_WIDGETS)
    {
        int total = WidgetsInteractiveRowCount();
        if (total > 0)
            g_widgetsCursor = (g_widgetsCursor - 1 + total) % total;
    }
}

void OverlayOnDown()
{
    if (g_bOverlayAlertActive)
        return;

    if (g_nOverlayPage == OVERLAY_PAGE_SYSTEM)
    {
        g_sysScrollOffset++;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_MENU)
        g_nMenuIndex = (g_nMenuIndex + 1) % g_nMenuItemCount;
    else if (g_nOverlayPage == OVERLAY_PAGE_DISC)
    {
        if (g_discMode == DISC_MODE_ISO_BROWSE)
        {
            if (g_isoBrowsePickingDrive && g_isoDriveCount > 0)
                g_isoDriveIndex = (g_isoDriveIndex + 1) % g_isoDriveCount;
            else if (!g_isoBrowsePickingDrive && g_isoFileCount > 0)
                g_isoBrowseIndex = (g_isoBrowseIndex + 1) % g_isoFileCount;
        }
        else if (g_discMode == DISC_MODE_IDLE && g_discMenuCount > 0)
            g_discMenuIndex = (g_discMenuIndex + 1) % g_discMenuCount;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_NETWORK)
    {
        g_netScrollOffset++;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_TOOLBOX)
        g_toolboxIndex = (g_toolboxIndex + 1) % TOOLBOX_COUNT;
    else if (g_nOverlayPage == OVERLAY_PAGE_FILES)
    {
        if (g_filesPickingDrive && g_filesDriveCount > 0)
            g_filesDriveIndex = (g_filesDriveIndex + 1) % g_filesDriveCount;
        else if (!g_filesPickingDrive && g_filesEntryCount > 0)
            g_filesIndex = (g_filesIndex + 1) % g_filesEntryCount;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_WIDGETS)
    {
        int total = WidgetsInteractiveRowCount();
        if (total > 0)
            g_widgetsCursor = (g_widgetsCursor + 1) % total;
    }
}

void OverlayOnLeft()
{
    if (g_bOverlayAlertActive)
        return;
    g_nOverlayPage = (g_nOverlayPage - 1 + OVERLAY_PAGE_COUNT) % OVERLAY_PAGE_COUNT;
}

void OverlayOnRight()
{
    if (g_bOverlayAlertActive)
        return;
    g_nOverlayPage = (g_nOverlayPage + 1) % OVERLAY_PAGE_COUNT;
}

void OverlayOnA()
{
    if (g_bOverlayAlertActive)
        return;

    // Block input during ISO launch sequence
    if (g_isoLaunching)
        return;

    if (g_nOverlayPage == OVERLAY_PAGE_MENU)
        g_bMenuConfirmed = true;
    else if (g_nOverlayPage == OVERLAY_PAGE_DISC)
    {
        if (g_discMode == DISC_MODE_ISO_BROWSE)
        {
            // Drive picker
            if (g_isoBrowsePickingDrive)
            {
                if (g_isoDriveIndex < g_isoDriveCount)
                {
                    IsoBrowseDir(g_isoDrives[g_isoDriveIndex].szRoot);
                    g_isoBrowsePickingDrive = false;
                }
                return;
            }

            // File browser selection
            if (g_isoBrowseIndex < g_isoFileCount)
            {
                IsoFileEntry* sel = &g_isoFiles[g_isoBrowseIndex];
                if (sel->bIsDir)
                {
                    if (strcmp(sel->szName, "..") == 0)
                    {
                        // Navigate up
                        char* lastSlash = strrchr(g_isoBrowseDir, '\\');
                        if (lastSlash && lastSlash != g_isoBrowseDir)
                        {
                            // Find second-to-last backslash
                            *lastSlash = '\0';
                            char* prevSlash = strrchr(g_isoBrowseDir, '\\');
                            if (prevSlash)
                                *(prevSlash + 1) = '\0';
                            else
                                strcat(g_isoBrowseDir, "\\");
                        }
                        IsoBrowseDir(g_isoBrowseDir);
                    }
                    else
                    {
                        // Navigate into directory
                        char newDir[ISO_MAX_PATH];
                        _snprintf(newDir, ISO_MAX_PATH, "%s%s\\", g_isoBrowseDir, sel->szName);
                        IsoBrowseDir(newDir);
                    }
                }
                else
                {
                    // Selected an ISO file - attach and launch with brief delay
                    if (AttachIso(sel->szPath))
                    {
                        g_isoLaunchIsCCI = EndsWith(sel->szPath, ".cci");
                        _sntprintf(g_isoLaunchName, 64, _T("Launching %s..."),
                                   g_isoLaunchIsCCI ? _T("CCI") : _T("ISO"));
                        g_isoLaunching = true;
                        g_isoLaunchTick = GetTickCount() + 1500;
                        g_discMode = DISC_MODE_IDLE;
                    }
                }
            }
        }
        else if (g_discMode == DISC_MODE_IDLE)
            g_discMenuConfirmed = true;
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_TOOLBOX)
        g_toolboxConfirmed = true;
    else if (g_nOverlayPage == OVERLAY_PAGE_FILES)
    {
        if (g_filesConfirmingLaunch)
        {
            // A on the launch-confirm popup commits the launch. Route
            // through CTheseusLauncher so we get the same path the
            // dashboard uses for game/title launches (XBE direct,
            // ISO/CCI via Cerbios or legacy attach).
            OutputDebugStringA("[Files] Launching ");
            OutputDebugStringA(g_filesPendingLaunchPath);
            OutputDebugStringA("\n");

            TCHAR pathT[FILES_PATH_LEN];
            Unicode(pathT, g_filesPendingLaunchPath, FILES_PATH_LEN);

            LaunchFileFromOverlay(pathT);

            // If we returned, launch failed; clear state and stay put.
            g_filesConfirmingLaunch     = false;
            g_filesPendingLaunchPath[0] = 0;
        }
        else if (g_filesPickingDrive)
        {
            if (g_filesDriveIndex >= 0 && g_filesDriveIndex < g_filesDriveCount)
            {
                FilesBrowseDir(g_filesDrives[g_filesDriveIndex].szRoot);
                g_filesPickingDrive = false;
            }
        }
        else
        {
            if (g_filesIndex >= 0 && g_filesIndex < g_filesEntryCount)
                FilesActivateEntry(g_filesEntries[g_filesIndex]);
        }
    }
    else if (g_nOverlayPage == OVERLAY_PAGE_WIDGETS)
    {
        int widx, sidx;
        DecodeWidgetCursor(g_widgetsCursor, widx, sidx);
        WidgetsCycleSetting(GetWidgetAt(widx), sidx);
        extern void SaveWidgetConfig();
        SaveWidgetConfig();
    }
}

void OverlayOnB()
{
    // Block input during ISO launch sequence
    if (g_isoLaunching)
        return;

    // Dismiss alert popup. Restore the panel to whatever state it was
    // in before the alert fired. If it was closed (alert came from
    // outside, e.g. FTP), close it again instead of leaving it open.
    if (g_bOverlayAlertActive)
    {
        g_bOverlayAlertActive = false;
        g_szAlertMsg[0]       = 0;
        g_bShowOverlay        = s_overlayOpenBeforeAlert;
        if (!g_bShowOverlay)
            g_bOverlayInputCapture = false;
        return;
    }

    // If browsing ISOs, go back to drive picker or disc menu
    if (g_nOverlayPage == OVERLAY_PAGE_DISC && g_discMode == DISC_MODE_ISO_BROWSE)
    {
        if (!g_isoBrowsePickingDrive)
        {
            // Go back to drive picker
            g_isoBrowsePickingDrive = true;
            return;
        }
        // Already at drive picker, exit to disc menu
        g_discMode = DISC_MODE_IDLE;
        return;
    }

    // If on disc page in dump-done state, return to idle
    if (g_nOverlayPage == OVERLAY_PAGE_DISC && g_discMode == DISC_MODE_DUMP_DONE)
    {
        g_discMode = DISC_MODE_IDLE;
        g_discScanned = false; // rescan on return
        return;
    }

    // Files page: B cancels a pending launch first, then navigates up
    // the directory tree, then back to the drive picker, then closes
    // the overlay.
    if (g_nOverlayPage == OVERLAY_PAGE_FILES)
    {
        if (g_filesConfirmingLaunch)
        {
            g_filesConfirmingLaunch     = false;
            g_filesPendingLaunchPath[0] = 0;
            return;
        }
        if (!g_filesPickingDrive)
        {
            FilesGoUp();
            return;
        }
        // already at drive picker -> fall through to close overlay
    }

    g_bShowOverlay = false;
    g_bOverlayInputCapture = false;
}

// ============================================================================
// Init / Update
// ============================================================================

void InitCubeOverlay()
{
}

void UpdateCubeOverlay()
{
    // Reserved for future animations
}

// ============================================================================
// Main overlay draw
// ============================================================================

void DrawCubeOverlay()
{
    if (!g_bShowOverlay || !g_pD3DDev)
    {
        g_bOverlayInputCapture = false;
        return;
    }

    g_bOverlayInputCapture = true;

    // One-time texture load
    static bool bTexLoaded = false;
    if (!bTexLoaded) { LoadOverlayTexture(); bTexLoaded = true; }

    IDirect3DDevice8 *dev = g_pD3DDev;

    // Full D3D state setup. Override any leftover scene state.
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    const int sw = g_nViewWidth;
    const int sh = g_nViewHeight;

    // Alert mode: just draw the popup, nothing else
    if (g_bOverlayAlertActive)
    {
        DrawAlertPopup(sw, sh);
        return;
    }

    const int pw = 720, ph = 480;
    const int px = (sw - pw) / 2;
    const int py = (sh - ph) / 2;
    const int margin = 20;

    // Panel background
    DrawSolidRect(px, py, pw, ph, D3DCOLOR_ARGB(210, 10, 10, 10));

    // Border
    DrawSolidRect(px, py, pw, 1, D3DCOLOR_ARGB(80, 255, 255, 255));
    DrawSolidRect(px, py + ph - 1, pw, 1, D3DCOLOR_ARGB(80, 255, 255, 255));
    DrawSolidRect(px, py, 1, ph, D3DCOLOR_ARGB(80, 255, 255, 255));
    DrawSolidRect(px + pw - 1, py, 1, ph, D3DCOLOR_ARGB(80, 255, 255, 255));

    int y = py + margin;

    // Logo (centered)
    if (g_pOverlayTex)
    {
        D3DSURFACE_DESC desc;
        g_pOverlayTex->GetLevelDesc(0, &desc);
        float scale = 64.0f / desc.Width;
        int lw = 64, lh = (int)(desc.Height * scale);
        DrawOverlayImage(g_pOverlayTex, (float)(px + (pw - lw) / 2), (float)y, (float)lw, (float)lh);
        y += lh + 10;
    }

    // Tab bar
    {
        float tabX = (float)(px + margin);
        for (int i = 0; i < OVERLAY_PAGE_COUNT; i++)
        {
            DWORD color = (i == g_nOverlayPage) ? 0xFFFFFFFF : 0xFF666666;
            OverlayFontDraw(g_szPageNames[i], tabX, (float)y, FONT_H, color);
            float tw = OverlayFontMeasure(g_szPageNames[i], FONT_H);
            if (i == g_nOverlayPage)
                DrawSolidRect((int)tabX, y + (int)FONT_H + 4, (int)tw, 2, D3DCOLOR_ARGB(255, 255, 255, 255));
            tabX += tw + 40;
        }
        y += (int)FONT_H + 14;
    }

    // Divider under tabs
    DrawSolidRect(px + 10, y, pw - 20, 1, D3DCOLOR_ARGB(100, 255, 255, 255));
    y += 12;

    // Content area
    int cx = px + margin;
    int cw = pw - margin * 2;
    int contentH = (py + ph - margin - 24) - y;

    switch (g_nOverlayPage)
    {
    case OVERLAY_PAGE_SYSTEM:  DrawSystemPage(cx, y, cw, contentH); break;
    case OVERLAY_PAGE_DISC:    DrawDiscPage(cx, y, cw, contentH); break;
    case OVERLAY_PAGE_NETWORK: DrawNetworkPage(cx, y, cw, contentH); break;
    case OVERLAY_PAGE_TOOLBOX: DrawToolboxPage(cx, y, cw, contentH); break;
    case OVERLAY_PAGE_FILES:   DrawFilesPage(cx, y, cw, contentH); break;
    case OVERLAY_PAGE_WIDGETS: DrawWidgetsPage(cx, y, cw, contentH); break;
    case OVERLAY_PAGE_MENU:    DrawMenuPage(cx, y, cw, contentH); break;
    }

    // Footer
    const TCHAR *footer = _T("Left/Right: Switch Tab  |  B: Close");
    int fy = py + ph - margin - 16;
    float fw = OverlayFontMeasure(footer, FONT_H);
    OverlayFontDraw(footer, (float)(px + (pw - (int)fw) / 2), (float)fy, FONT_H, 0xFF555555);

    // Modal confirmations layered on top of the page (currently just
    // the Files page's pre-launch confirm). Drawn last so it covers
    // whatever was below.
    if (g_filesConfirmingLaunch && g_nOverlayPage == OVERLAY_PAGE_FILES)
        DrawFilesLaunchConfirm(sw, sh);
}
