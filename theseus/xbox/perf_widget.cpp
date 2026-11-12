// perf_widget.cpp: per-phase performance HUD widget. Breaks the
// frame down into Advance / Scene / Setup / etc. timings so we can
// see which subsystem is slowing the dashboard down. Theseus-
// original; used during the 60fps work.

#include "std.h"
#include "theseus.h"
#include "widget_layer.h"
#include "widget_draw.h"

namespace {

const int PERF_WIDGET_W = 220;
const int PERF_WIDGET_H = 400;

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

    FrameSnapshot f;
    Frame_Read(&f);
    Frame_RequestGpuTiming();   // only while this widget is on screen

    TCHAR line[40];
    int yy = y + 6;
    const int rowH = 16;

    _sntprintf(line, 40, _T("%.0f fps  %.1f ms"), f.fps, f.frameMs);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 14.0f, argb); yy += 18;

    // Scene and HUD kept apart so the HUD doesn't count itself as scene.
    _sntprintf(line, 40, _T("scene %.0f+%.0f  hud %.0f+%.0f"),
               f.count[FC_DRAW_SCENE], f.count[FC_DRAW_SUBSET],
               f.count[FC_DRAW_SOLID], f.count[FC_DRAW_TEXT]);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("nodes %.0f visit / %.0f skip"),
               f.count[FC_NODE_VISITS], f.count[FC_NODE_SKIPS]);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("world %.0f  state %.0f/%.0f"),
               f.count[FC_UPDATE_WORLD], f.count[FC_STATE_CALLS], f.count[FC_STATE_REPEATS]);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("tris %.0fk  idx %.0fk"),
               f.count[FC_TRIANGLES] / 1000.0f, f.count[FC_INDICES] / 1000.0f);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;

    _sntprintf(line, 40, _T("disk %.1f  load %.0f/%.0f ms"),
               f.count[FC_DISK_QUERIES], f.lastLoadMs, f.maxLoadMs);
    line[39] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH + 4;

    static const struct { FramePhase phase; const TCHAR* label; } kRows[] = {
        { FP_ADVANCE,     _T("advance ") },
        { FP_SCENE_SETUP, _T("setup   ") },
        { FP_SCENE_TREE,  _T("tree    ") },
        { FP_TREE_MATERIAL, _T(" matl   ") },
        { FP_TREE_WORLD,  _T(" world  ") },
        { FP_TREE_DRAW,   _T(" draw   ") },
        { FP_DIP,         _T("  dip   ") },
        { FP_SCENE_END,   _T("end     ") },
        { FP_WIDGETS,     _T("widgets ") },
        { FP_WIDGET_TICK, _T(" tick   ") },
        { FP_UP_DRAW,     _T(" updraw ") },
        { FP_OVERLAY,     _T("overlay ") },
        { FP_PRESENT,     _T("present ") },
        { FP_GPU_WAIT,    _T("gpu wait") },
    };
    for (int i = 0; i < (int)(sizeof(kRows) / sizeof(kRows[0])); i++)
    {
        _sntprintf(line, 40, _T("%s  %5.2f"), kRows[i].label, f.phaseMs[kRows[i].phase]);
        line[39] = 0;
        OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;
    }

    // Heaviest shapes by triangles.
    yy += 4;
    for (int i = 0; i < 3; i++)
    {
        if (f.topMesh[i].tris <= 0.0f) break;
        _sntprintf(line, 40, _T("%.0fk %s"), f.topMesh[i].tris / 1000.0f, f.topMesh[i].label);
        line[39] = 0;
        OverlayFontDraw(line, (float)(x + 8), (float)yy, 12.0f, argb); yy += rowH;
    }
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
