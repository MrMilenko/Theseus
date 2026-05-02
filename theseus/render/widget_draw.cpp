// widget_draw.cpp: 2D drawing primitives shared by the modal Overlay
// (overlay.cpp) and the always-visible widget layer (widget_layer.cpp).
// Solid rects, text labels, alpha blending, simple geometry. Theseus-
// original code, not in retail.

#include "std.h"
#include "theseus.h"
#include "widget_draw.h"

void DrawSolidRect(int x, int y, int width, int height, D3DCOLOR color)
{
    struct Vertex { float x, y, z, rhw; D3DCOLOR color; };

    Vertex verts[4] = {
        {(float)x, (float)y, 0.0f, 1.0f, color},
        {(float)(x + width), (float)y, 0.0f, 1.0f, color},
        {(float)x, (float)(y + height), 0.0f, 1.0f, color},
        {(float)(x + width), (float)(y + height), 0.0f, 1.0f, color},
    };

    IDirect3DDevice8 *dev = g_pD3DDev;
    if (!dev) return;
    extern int g_drawCallsThisFrame;
    extern int g_drawCallsSolidFrame;
    g_drawCallsThisFrame++;
    g_drawCallsSolidFrame++;
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(Vertex));
}

void DrawTexturedRect(IDirect3DTexture8* tex, float x, float y, float width, float height)
{
    if (!tex) return;
    IDirect3DDevice8* dev = g_pD3DDev;
    if (!dev) return;

    struct TVert { float x, y, z, rhw; DWORD color; float u, v; };

    dev->SetTexture(0, tex);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    TVert quad[4] = {
        {x,         y,          0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f},
        {x + width, y,          0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f},
        {x,         y + height, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f},
        {x + width, y + height, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f},
    };

    extern int g_drawCallsThisFrame;
    extern int g_drawCallsSolidFrame;
    g_drawCallsThisFrame++;
    g_drawCallsSolidFrame++;
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TVert));

    // Restore diffuse-only stage state so subsequent DrawSolidRect /
    // text draws keep working without each having to re-establish it.
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}
