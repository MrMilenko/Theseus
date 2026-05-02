// widget_layer.cpp: always-visible widget layer. Renders every frame on
// top of the scene graph regardless of which scene is active (FTP widget,
// FPS / perf widget, ISO loader widget, etc). Separate from the modal
// Overlay so widgets stay visible during scene transitions. Theseus-
// original code, not in retail.

#include "std.h"
#include "theseus.h"
#include "widget_layer.h"
#include "widget_draw.h"

#define MAX_WIDGETS 16
#define WIDGET_SCREEN_MARGIN 12
#define WIDGET_STACK_SPACING 4

static Widget g_widgets[MAX_WIDGETS];
static int    g_widgetCount = 0;

bool RegisterWidget(const Widget& w)
{
    if (g_widgetCount >= MAX_WIDGETS)
        return false;
    g_widgets[g_widgetCount++] = w;
    return true;
}

Widget* FindWidget(const char* name)
{
    if (!name) return NULL;
    for (int i = 0; i < g_widgetCount; i++)
    {
        if (g_widgets[i].name && strcmp(g_widgets[i].name, name) == 0)
            return &g_widgets[i];
    }
    return NULL;
}

int GetWidgetCount()
{
    return g_widgetCount;
}

Widget* GetWidgetAt(int i)
{
    if (i < 0 || i >= g_widgetCount) return NULL;
    return &g_widgets[i];
}

void TickWidgets()
{
    for (int i = 0; i < g_widgetCount; i++)
    {
        if (g_widgets[i].enabled && g_widgets[i].tick)
            g_widgets[i].tick();
    }
}

static DWORD ComposeARGB(int opacity, DWORD tintRGB)
{
    if (opacity < 0) opacity = 0;
    if (opacity > 255) opacity = 255;
    return ((DWORD)opacity << 24) | (tintRGB & 0x00FFFFFF);
}

void DrawWidgets()
{
    if (!g_pD3DDev) return;
    if (g_widgetCount == 0) return;

    // Set up state once for the whole layer. Mirrors what DrawCubeOverlay
    // does on entry, since both layers want the same diffuse-only
    // alpha-blended setup.
    IDirect3DDevice8* dev = g_pD3DDev;
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

    const int sw = (int)g_nViewWidth;
    const int sh = (int)g_nViewHeight;

    // Per-anchor cursor: how far down (or up) we've stacked so far
    int cursor[4] = { 0, 0, 0, 0 };

    for (int i = 0; i < g_widgetCount; i++)
    {
        Widget& w = g_widgets[i];
        if (!w.enabled || !w.draw) continue;

        int x = 0, y = 0;
        switch (w.anchor)
        {
        case WIDGET_ANCHOR_TL:
            x = WIDGET_SCREEN_MARGIN;
            y = WIDGET_SCREEN_MARGIN + cursor[0];
            cursor[0] += w.reservedH + WIDGET_STACK_SPACING;
            break;
        case WIDGET_ANCHOR_TR:
            x = sw - WIDGET_SCREEN_MARGIN - w.reservedW;
            y = WIDGET_SCREEN_MARGIN + cursor[1];
            cursor[1] += w.reservedH + WIDGET_STACK_SPACING;
            break;
        case WIDGET_ANCHOR_BL:
            x = WIDGET_SCREEN_MARGIN;
            y = sh - WIDGET_SCREEN_MARGIN - w.reservedH - cursor[2];
            cursor[2] += w.reservedH + WIDGET_STACK_SPACING;
            break;
        case WIDGET_ANCHOR_BR:
            x = sw - WIDGET_SCREEN_MARGIN - w.reservedW;
            y = sh - WIDGET_SCREEN_MARGIN - w.reservedH - cursor[3];
            cursor[3] += w.reservedH + WIDGET_STACK_SPACING;
            break;
        }

        DWORD argb = ComposeARGB(w.opacity, w.tintRGB);
        w.draw(x, y, argb);
    }
}
