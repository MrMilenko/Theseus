// ui_overlay.h: the guide's immediate mode UI. Shapes and text gathered in a
// 640x480 layout during the frame and drawn over the dashboard at the
// window's resolution, so corners and text stay sharp at any size.
// Shapes are rounded rects worked out per pixel from their distance to the
// edge; text comes from a glyph atlas. ui_overlay.cpp draws, and
// ui_overlay_style.cpp is the guide's look built on top.
#pragma once

enum { UI_FONT_REGULAR, UI_FONT_BOLD, UI_FONT_COUNT };

enum { UI_ALIGN_LEFT, UI_ALIGN_CENTER, UI_ALIGN_RIGHT };

// A pad button, drawn the way the device used last names it: the pad's
// colored face buttons and dark pills, or the keyboard's key caps.
enum {
    UI_BUTTON_A, UI_BUTTON_B, UI_BUTTON_X, UI_BUTTON_Y,
    UI_BUTTON_START, UI_BUTTON_BACK,
    UI_BUTTON_LEFT_SHOULDER, UI_BUTTON_RIGHT_SHOULDER,
    UI_BUTTON_DPAD_LEFT, UI_BUTTON_DPAD_RIGHT,
    UI_BUTTON_COUNT
};

// Colors are 0xRRGGBBAA. Everything is in the 640x480 layout; a wide
// window has room either side, which x below 0 or past 640 reaches.
void  ui_overlay_rect(float x, float y, float w, float h, float radius, unsigned int color);
void  ui_overlay_gradient(float x, float y, float w, float h, float radius, unsigned int top, unsigned int bottom);
void  ui_overlay_outline(float x, float y, float w, float h, float radius, float thickness, unsigned int color);

// One line of UTF-8, size its height, y its top. Returns its width.
float ui_overlay_text(int font, float size, float x, float y, int align, unsigned int color, const char* text);
float ui_overlay_text_width(int font, float size, const char* text);
// Text with a drop shadow and a soft glow around it (glow alpha 0: shadow only).
float ui_overlay_text_glow(int font, float size, float x, float y, int align, unsigned int color,
                           unsigned int glow, const char* text);

// The dashboard's plasma: a noise map twice, scrolling at two scales, tinted
// by color, in a rounded rect. ms is the clock it scrolls by.
void  ui_overlay_plasma(float x, float y, float w, float h, float radius, unsigned int color, unsigned int ms);
void  ui_overlay_plasma_thread(unsigned int color);

// Later layers draw over earlier ones (0 to 4); text sits over its own
// layer's shapes. Each frame starts at 0.
void  ui_overlay_layer(int layer);

// Which device the prompts show: 0 keyboard (and mouse), 1 pad.
void  ui_overlay_set_device(int device);
int   ui_overlay_keyboard();
float ui_overlay_button(int button, float size, float x, float y, unsigned int color);
float ui_overlay_button_width(int button, float size);
float ui_overlay_key(const char* label, float size, float x, float y, unsigned int color);
float ui_overlay_key_width(const char* label, float size);

// Layout <-> window pixels for the current backbuffer, for the pointer.
void  ui_overlay_to_layout(float px, float py, float* x, float* y);
float ui_overlay_layout_left();
float ui_overlay_layout_right();

// Draws what was gathered into a bgfx view over the backbuffer
// (width x height pixels), then starts the next frame empty.
void  ui_overlay_present(unsigned short view, int width, int height);
void  ui_overlay_shutdown();

// The guide's look (ui_overlay_style.cpp)

enum {
    UI_COLOR_NAME, UI_COLOR_STATUS, UI_COLOR_LABEL, UI_COLOR_HEAD, UI_COLOR_HEAD_GLOW,
    UI_COLOR_TEXT, UI_COLOR_WARNING, UI_COLOR_SHADE,
    UI_COLOR_GLASS_TOP, UI_COLOR_GLASS_BOTTOM, UI_COLOR_PANE,
    UI_COLOR_TITLE, UI_COLOR_TITLE_GLOW, UI_COLOR_NAV, UI_COLOR_NAV_GLOW,
    UI_COLOR_SELECTED_GLOW, UI_COLOR_TEXT_DIM, UI_COLOR_FAINT, UI_COLOR_GOOD, UI_COLOR_BAD,
    UI_COLOR_PROMPT, UI_COLOR_PROMPT_GLOW, UI_COLOR_SEPARATOR,
    UI_COLOR_PANEL_EDGE, UI_COLOR_GLOW, UI_COLOR_RULE, UI_COLOR_TAB, UI_COLOR_TAB_ON,
    UI_COLOR_SELECTED_EDGE, UI_COLOR_ROW, UI_COLOR_ROW_EDGE, UI_COLOR_ROW_ON,
    UI_COLOR_PLASMA_THREAD,
    UI_COLOR_COUNT
};
unsigned int ui_style_color(int name);

// Dashboard green is the default; blue and red are there to pick.
enum { UI_THEME_GREEN, UI_THEME_BLUE, UI_THEME_RED, UI_THEME_COUNT };
void ui_style_theme(int theme);

enum { UI_TEXT_HEADING, UI_TEXT_NAME, UI_TEXT_STATUS, UI_TEXT_TITLE, UI_TEXT_LINE, UI_TEXT_LABEL, UI_TEXT_WARNING };
float ui_style_text(int kind, float x, float y, int align, const char* text);
float ui_style_text_scaled(int kind, float scale, float x, float y, int align, const char* text);
float ui_style_text_size(int kind);

void ui_style_clock(unsigned int ms);
// Plasma with an edge (alpha 0: none) and, lit, a glow around it.
void ui_style_surface(float x, float y, float w, float h, float radius, unsigned int color, unsigned int edge, int lit);
// The big glass panel a screen sits on, faded by share (0 to 1).
void ui_style_panel(float x, float y, float w, float h, float share);
void ui_style_glass(float x, float y, float w, float h, float radius, unsigned int color, unsigned int edge);
// A row's capsule: glass, the selected one lit with the plasma.
void ui_style_row(float x, float y, float w, float h, int selected);
// A setting's row: label, an optional tag after it, the value between arrows.
void ui_style_setting(float x, float y, float w, float h, float scale, const char* label, const char* badge,
                      const char* value, int selected);
// Section tabs with the bumpers at the ends; each tab's span for the pointer.
void ui_style_tabs(float left, float right, float y, float h, const char* const* names, int count, int selected,
                   const char* leftKey, const char* rightKey, float* starts, float* ends);

struct ui_style_prompt { int button; const char* key; const char* word; };
// Right aligned at right; each prompt's span for the pointer.
void ui_style_prompts(float right, float y, float size, const ui_style_prompt* prompts, int count,
                      float* starts, float* ends);
