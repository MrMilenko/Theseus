#pragma once

// Always-visible widget layer. Widgets render every frame regardless of
// the modal overlay's visibility, except they're suppressed while the
// modal overlay is open (it covers most of the screen anyway and we
// don't want depth-sort quirks).
//
// Lifecycle:
//   - Widgets self-register at boot via RegisterWidget()
//   - main render loop calls TickWidgets() then DrawWidgets() once per
//     frame, after TheseusEndScene() and before DrawCubeOverlay()

enum WidgetAnchor
{
    WIDGET_ANCHOR_TL,
    WIDGET_ANCHOR_TR,
    WIDGET_ANCHOR_BL,
    WIDGET_ANCHOR_BR,
};

// Draw callback signature. Widget renders itself within its reserved
// bounding box at (x, y). argb is the precomputed colour with the
// configured opacity already folded in -- widgets pass it straight to
// DrawSolidRect / OverlayFontDraw without further alpha math.
typedef void (*WidgetDrawFn)(int x, int y, DWORD argb);
typedef void (*WidgetTickFn)();

struct Widget
{
    const char*   name;
    WidgetAnchor  anchor;
    bool          enabled;
    int           opacity;     // 0..255
    DWORD         tintRGB;     // 0x00RRGGBB
    int           reservedW;
    int           reservedH;
    WidgetTickFn  tick;        // optional, may be NULL
    WidgetDrawFn  draw;
};

// Returns false if the widget table is full.
bool RegisterWidget(const Widget& w);

// Find a widget by name. Returns NULL if not found. Useful for the
// overlay's config page to flip enabled / anchor / opacity / tint.
Widget* FindWidget(const char* name);

// Iteration for the overlay's config page. Returns NULL for out-of-range.
int     GetWidgetCount();
Widget* GetWidgetAt(int i);

void TickWidgets();
void DrawWidgets();
