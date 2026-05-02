// iso_widget.cpp: ISO loader status HUD widget. Shows current ISO
// title name, image, and load progress while the ISO loader is
// active. Theseus-original.

#include "std.h"
#include "theseus.h"
#include "widget_layer.h"
#include "widget_draw.h"

extern bool  g_isoTitleInfoLoaded;
extern char  g_isoTitleName[256];
extern DWORD g_isoTitleId;
extern DWORD g_isoGameRegion;
extern LPDIRECT3DTEXTURE8 g_pIsoTitleImage;

namespace {

const int ISO_WIDGET_W = 280;
const int ISO_WIDGET_H = 88;
const int ISO_ICON_SZ  = 64;

static const TCHAR* RegionShort(DWORD region)
{
    if ((region & 0x07) == 0x07)             return _T("World");
    if (region & 0x80000000)                 return _T("Debug");
    if (region & 0x00000001)                 return _T("NTSC-U");
    if (region & 0x00000002)                 return _T("NTSC-J");
    if (region & 0x00000004)                 return _T("PAL");
    return _T("?");
}

void isoWidgetDraw(int x, int y, DWORD argb)
{
    if (!g_isoTitleInfoLoaded)
        return; // hide entirely when nothing mounted

    DWORD opacity   = (argb >> 24) & 0xFF;
    DWORD bgAlpha   = opacity / 2;
    DWORD bgColor   = (bgAlpha << 24);
    DWORD borderRGB = ((opacity / 3) << 24) | (argb & 0x00FFFFFF);

    DrawSolidRect(x, y, ISO_WIDGET_W, ISO_WIDGET_H, bgColor);
    DrawSolidRect(x, y, ISO_WIDGET_W, 1, borderRGB);
    DrawSolidRect(x, y + ISO_WIDGET_H - 1, ISO_WIDGET_W, 1, borderRGB);
    DrawSolidRect(x, y, 1, ISO_WIDGET_H, borderRGB);
    DrawSolidRect(x + ISO_WIDGET_W - 1, y, 1, ISO_WIDGET_H, borderRGB);

    // Icon (left side). When the texture is loaded by the disc page's
    // mount flow we just point at it: no copy, no extra refcount.
    if (g_pIsoTitleImage)
        DrawTexturedRect(g_pIsoTitleImage, (float)(x + 8), (float)(y + 12), (float)ISO_ICON_SZ, (float)ISO_ICON_SZ);

    int textX = x + 8 + (g_pIsoTitleImage ? ISO_ICON_SZ + 8 : 0);

    OverlayFontDraw(_T("ISO MOUNTED"), (float)textX, (float)(y + 8), 12.0f, argb);

    // Title name (manual trim since OverlayFontDraw doesn't clip).
    TCHAR titleT[64];
    if (g_isoTitleName[0])
    {
        int maxChars = 24;
        int n = (int)strlen(g_isoTitleName);
        if (n > maxChars) n = maxChars;
        for (int i = 0; i < n; i++) titleT[i] = (TCHAR)g_isoTitleName[i];
        titleT[n] = 0;
        if ((int)strlen(g_isoTitleName) > maxChars)
        {
            titleT[maxChars - 2] = _T('.');
            titleT[maxChars - 1] = _T('.');
            titleT[maxChars]     = 0;
        }
    }
    else
    {
        _tcscpy(titleT, _T("(no title)"));
    }
    OverlayFontDraw(titleT, (float)textX, (float)(y + 28), 14.0f, argb);

    TCHAR meta[40];
    _sntprintf(meta, 40, _T("ID %08X"), (unsigned)g_isoTitleId);
    meta[39] = 0;
    OverlayFontDraw(meta, (float)textX, (float)(y + 50), 12.0f, argb);

    OverlayFontDraw(RegionShort(g_isoGameRegion), (float)textX, (float)(y + 68), 12.0f, argb);
}

} // namespace

void RegisterIsoWidget()
{
    Widget w;
    w.name      = "iso";
    w.anchor    = WIDGET_ANCHOR_TR;   // sits where FTP used to default; user can move
    w.enabled   = true;
    w.opacity   = 200;
    w.tintRGB   = 0x00FFFFFF;
    w.reservedW = ISO_WIDGET_W;
    w.reservedH = ISO_WIDGET_H;
    w.tick      = NULL;               // widget reads globals; no tick needed
    w.draw      = isoWidgetDraw;
    RegisterWidget(w);
}
