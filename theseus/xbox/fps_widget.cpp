// fps_widget.cpp: FPS / frame-time HUD widget. Renders into the
// always-on widget layer; reads g_fps and g_frameTimeMs from the
// dashboard's main loop. Theseus-original.

#include "std.h"
#include "theseus.h"
#include "widget_layer.h"
#include "widget_draw.h"

extern float g_fps;
extern float g_frameTimeMs;

namespace {

const int FPS_WIDGET_W = 130;
const int FPS_WIDGET_H = 52;

void fpsWidgetDraw(int x, int y, DWORD argb)
{
    DWORD opacity   = (argb >> 24) & 0xFF;
    DWORD bgAlpha   = opacity / 2;
    DWORD bgColor   = (bgAlpha << 24);
    DWORD borderRGB = ((opacity / 3) << 24) | (argb & 0x00FFFFFF);

    DrawSolidRect(x, y, FPS_WIDGET_W, FPS_WIDGET_H, bgColor);
    DrawSolidRect(x, y, FPS_WIDGET_W, 1, borderRGB);
    DrawSolidRect(x, y + FPS_WIDGET_H - 1, FPS_WIDGET_W, 1, borderRGB);
    DrawSolidRect(x, y, 1, FPS_WIDGET_H, borderRGB);
    DrawSolidRect(x + FPS_WIDGET_W - 1, y, 1, FPS_WIDGET_H, borderRGB);

    TCHAR line[32];
    _sntprintf(line, 32, _T("%.1f fps"), g_fps);
    line[31] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 6), 16.0f, argb);

    _sntprintf(line, 32, _T("%.2f ms"), g_frameTimeMs);
    line[31] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 28), 14.0f, argb);
}

} // namespace

void RegisterFpsWidget()
{
    Widget w;
    w.name      = "fps";
    w.anchor    = WIDGET_ANCHOR_BR;
    w.enabled   = true;
    w.opacity   = 200;
    w.tintRGB   = 0x00FFFFFF;
    w.reservedW = FPS_WIDGET_W;
    w.reservedH = FPS_WIDGET_H;
    w.tick      = NULL;          // values are computed in main.cpp's render loop
    w.draw      = fpsWidgetDraw;
    RegisterWidget(w);
}
