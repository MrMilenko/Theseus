// perf_widget.cpp: per-phase performance HUD widget. Breaks the
// frame down into Advance / Scene / Setup / etc. timings so we can
// see which subsystem is slowing the dashboard down. Theseus-
// original; used during the 60fps work.

#include "std.h"
#include "theseus.h"
#include "widget_layer.h"
#include "widget_draw.h"

extern float g_fps;
extern float g_frameTimeMs;
extern float g_phaseAdvanceMs;
extern float g_phaseSceneMs;
extern float g_phaseSceneSetupMs;
extern float g_phaseSceneTreeMs;
extern float g_phaseWidgetsMs;
extern float g_phaseOverlayMs;
extern float g_phasePresentMs;
extern float g_drawCallsAvg;
extern float g_drawCallsSceneAvg;
extern float g_drawCallsSolidAvg;
extern float g_drawCallsTextAvg;
extern float g_nodeVisitsAvg;
extern float g_nodeSkipsAvg;

namespace {

const int PERF_WIDGET_W = 220;
const int PERF_WIDGET_H = 240;

void perfWidgetDraw(int x, int y, DWORD argb)
{
    DWORD opacity   = (argb >> 24) & 0xFF;
    DWORD bgAlpha   = opacity / 2;
    DWORD bgColor   = (bgAlpha << 24);
    DWORD borderRGB = ((opacity / 3) << 24) | (argb & 0x00FFFFFF);

    DrawSolidRect(x, y, PERF_WIDGET_W, PERF_WIDGET_H, bgColor);
    DrawSolidRect(x, y, PERF_WIDGET_W, 1, borderRGB);
    DrawSolidRect(x, y + PERF_WIDGET_H - 1, PERF_WIDGET_W, 1, borderRGB);
    DrawSolidRect(x, y, 1, PERF_WIDGET_H, borderRGB);
    DrawSolidRect(x + PERF_WIDGET_W - 1, y, 1, PERF_WIDGET_H, borderRGB);

    TCHAR line[40];

    _sntprintf(line, 40, _T("%.0f fps  %.1f ms"), g_fps, g_frameTimeMs);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 6), 14.0f, argb);

    _sntprintf(line, 40, _T("draws %.0f  s%.0f u%.0f t%.0f"),
               g_drawCallsAvg, g_drawCallsSceneAvg,
               g_drawCallsSolidAvg, g_drawCallsTextAvg);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 24), 12.0f, argb);

    _sntprintf(line, 40, _T("nodes %.0f visit / %.0f skip"),
               g_nodeVisitsAvg, g_nodeSkipsAvg);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 40), 12.0f, argb);

    int yy = y + 60;
    const int rowH = 16;

    _sntprintf(line, 40, _T("advance    %5.2f"), g_phaseAdvanceMs);    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("scene      %5.2f"), g_phaseSceneMs);      line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("  setup    %5.2f"), g_phaseSceneSetupMs); line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("  tree     %5.2f"), g_phaseSceneTreeMs);  line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("widgets    %5.2f"), g_phaseWidgetsMs);    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("overlay    %5.2f"), g_phaseOverlayMs);    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("present    %5.2f"), g_phasePresentMs);    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb);
}

} // namespace

void RegisterPerfWidget()
{
    Widget w;
    w.name      = "perf";
    w.anchor    = WIDGET_ANCHOR_TL;
    w.enabled   = false;          // off by default; flip on via Widgets overlay page when investigating perf
    w.opacity   = 200;
    w.tintRGB   = 0x00FFFFFF;
    w.reservedW = PERF_WIDGET_W;
    w.reservedH = PERF_WIDGET_H;
    w.tick      = NULL;
    w.draw      = perfWidgetDraw;
    RegisterWidget(w);
}
