#pragma once

// Shared 2D drawing primitives used by both the modal overlay (overlay.cpp)
// and the always-on widget layer (widget_layer.cpp). All draw calls assume
// the caller has already configured the device for diffuse-only RHW
// rendering (alpha blend on, lighting off, no Z, no texture). The modal
// overlay sets that state on entry; the widget layer does the same.

void DrawSolidRect(int x, int y, int width, int height, D3DCOLOR color);

// Draw a textured quad in screen space at full white tint. Used for
// widget icons and overlay images. Restores diffuse-only stage state
// on exit so subsequent solid/text draws keep working.
void DrawTexturedRect(IDirect3DTexture8* tex, float x, float y, float width, float height);

// Implemented in render/text.cpp. Returns the X advance after drawing.
extern float OverlayFontDraw(const TCHAR* text, float screenX, float screenY,
                             float pixelHeight, D3DCOLOR color);
extern float OverlayFontMeasure(const TCHAR* text, float pixelHeight);
