// ui_overlay_style.cpp: the guide's look as pieces any screen can draw.
// Translucent glass panels and rows, the dashboard's plasma on the
// selection, a glow on the lit edges, section tabs with their bumpers and
// the prompt bar. Built only on ui_overlay.h, so it's the same on every
// renderer.

#include "ui_overlay.h"
#include <string.h>

// One table a theme. Dashboard Green is the default: dark green glass and
// a lime selection. Blue and Red swap the same palette.
static unsigned int s_palettes[UI_THEME_COUNT][UI_COLOR_COUNT];

static void FillPalettes()
{
    static bool done = false;
    if (done) return;
    done = true;
    unsigned int* g = s_palettes[UI_THEME_GREEN];
    g[UI_COLOR_NAME] = 0xFFFFFFFF;          g[UI_COLOR_STATUS] = 0xC8F09AFF;
    g[UI_COLOR_LABEL] = 0xA8E05AFF;         g[UI_COLOR_HEAD] = 0xA8E05AFF;
    g[UI_COLOR_HEAD_GLOW] = 0x7FCB2A40;     g[UI_COLOR_TEXT] = 0xFFFFFFFF;
    g[UI_COLOR_WARNING] = 0xFFCF6BFF;       g[UI_COLOR_SHADE] = 0x010802B4;
    g[UI_COLOR_GLASS_TOP] = 0x123C14F4;     g[UI_COLOR_GLASS_BOTTOM] = 0x061C08F8;
    g[UI_COLOR_PANE] = 0x04180AE0;          g[UI_COLOR_TITLE] = 0xB4F03CFF;
    g[UI_COLOR_TITLE_GLOW] = 0x9BDB2AC0;    g[UI_COLOR_NAV] = 0xA8E04AFF;
    g[UI_COLOR_NAV_GLOW] = 0x8CD22870;      g[UI_COLOR_SELECTED_GLOW] = 0xD8FF9C8C;
    g[UI_COLOR_TEXT_DIM] = 0xA9BB9AFF;      g[UI_COLOR_FAINT] = 0x8FB27AFF;
    g[UI_COLOR_GOOD] = 0x9FEA7AFF;          g[UI_COLOR_BAD] = 0xFF8A80FF;
    g[UI_COLOR_PROMPT] = 0xB4F03CFF;        g[UI_COLOR_PROMPT_GLOW] = 0x8CD22880;
    g[UI_COLOR_SEPARATOR] = 0xA8E05A5A;     g[UI_COLOR_PANEL_EDGE] = 0x5FA524FF;
    g[UI_COLOR_GLOW] = 0x8CD228FF;          g[UI_COLOR_RULE] = 0x9BDB2A5A;
    g[UI_COLOR_TAB] = 0xB9E68DFF;           g[UI_COLOR_TAB_ON] = 0x6FAE2A8C;
    g[UI_COLOR_SELECTED_EDGE] = 0xD8FF9CFF; g[UI_COLOR_ROW] = 0x0E3612E6;
    g[UI_COLOR_ROW_EDGE] = 0x9BDB2A40;      g[UI_COLOR_ROW_ON] = 0x7DBE2A70;
    g[UI_COLOR_PLASMA_THREAD] = 0xB8F23CFF;

    unsigned int* b = s_palettes[UI_THEME_BLUE];
    b[UI_COLOR_NAME] = 0xFFFFFFFF;          b[UI_COLOR_STATUS] = 0x8FC4FFFF;
    b[UI_COLOR_LABEL] = 0x6FB6FFFF;         b[UI_COLOR_HEAD] = 0x6FB6FFFF;
    b[UI_COLOR_HEAD_GLOW] = 0x3D8BFF40;     b[UI_COLOR_TEXT] = 0xFFFFFFFF;
    b[UI_COLOR_WARNING] = 0xFFCF6BFF;       b[UI_COLOR_SHADE] = 0x01040CB4;
    b[UI_COLOR_GLASS_TOP] = 0x0C2A58F6;     b[UI_COLOR_GLASS_BOTTOM] = 0x061430FA;
    b[UI_COLOR_PANE] = 0x03102AE0;          b[UI_COLOR_TITLE] = 0x3D9BFFFF;
    b[UI_COLOR_TITLE_GLOW] = 0x3D9BFFC0;    b[UI_COLOR_NAV] = 0x4FA6FFFF;
    b[UI_COLOR_NAV_GLOW] = 0x3D9BFF70;      b[UI_COLOR_SELECTED_GLOW] = 0x9FD4FF8C;
    b[UI_COLOR_TEXT_DIM] = 0x9AA9BBFF;      b[UI_COLOR_FAINT] = 0x7F9CBDFF;
    b[UI_COLOR_GOOD] = 0x7FE39AFF;          b[UI_COLOR_BAD] = 0xFF8A80FF;
    b[UI_COLOR_PROMPT] = 0x3D9BFFFF;        b[UI_COLOR_PROMPT_GLOW] = 0x3D8BFF80;
    b[UI_COLOR_SEPARATOR] = 0x6EB4FF5A;     b[UI_COLOR_PANEL_EDGE] = 0x2F7FD6FF;
    b[UI_COLOR_GLOW] = 0x2F8FFFFF;          b[UI_COLOR_RULE] = 0x50A0FF5A;
    b[UI_COLOR_TAB] = 0x7FB6EEFF;           b[UI_COLOR_TAB_ON] = 0x2F7FD68C;
    b[UI_COLOR_SELECTED_EDGE] = 0x9FD4FFFF; b[UI_COLOR_ROW] = 0x0A2246E6;
    b[UI_COLOR_ROW_EDGE] = 0x50A0FF40;      b[UI_COLOR_ROW_ON] = 0x2F7FD670;
    b[UI_COLOR_PLASMA_THREAD] = 0x3373FFFF;

    unsigned int* r = s_palettes[UI_THEME_RED];
    r[UI_COLOR_NAME] = 0xFFFFFFFF;          r[UI_COLOR_STATUS] = 0xFF9C9FFF;
    r[UI_COLOR_LABEL] = 0xFF8184FF;         r[UI_COLOR_HEAD] = 0xFF8184FF;
    r[UI_COLOR_HEAD_GLOW] = 0xFF505440;     r[UI_COLOR_TEXT] = 0xFFFFFFFF;
    r[UI_COLOR_WARNING] = 0xFFCF6BFF;       r[UI_COLOR_SHADE] = 0x0C0202B4;
    r[UI_COLOR_GLASS_TOP] = 0x581415F6;     r[UI_COLOR_GLASS_BOTTOM] = 0x300A0AFA;
    r[UI_COLOR_PANE] = 0x2A0607E0;          r[UI_COLOR_TITLE] = 0xFF5459FF;
    r[UI_COLOR_TITLE_GLOW] = 0xFF5459C0;    r[UI_COLOR_NAV] = 0xFF6569FF;
    r[UI_COLOR_NAV_GLOW] = 0xFF545970;      r[UI_COLOR_SELECTED_GLOW] = 0xFFACAF8C;
    r[UI_COLOR_TEXT_DIM] = 0xBB9E9EFF;      r[UI_COLOR_FAINT] = 0xBD8688FF;
    r[UI_COLOR_GOOD] = 0x7FE39AFF;          r[UI_COLOR_BAD] = 0xFFA060FF;
    r[UI_COLOR_PROMPT] = 0xFF5459FF;        r[UI_COLOR_PROMPT_GLOW] = 0xFF505480;
    r[UI_COLOR_SEPARATOR] = 0xFF80835A;     r[UI_COLOR_PANEL_EDGE] = 0xD64347FF;
    r[UI_COLOR_GLOW] = 0xFF474CFF;          r[UI_COLOR_RULE] = 0xFF64685A;
    r[UI_COLOR_TAB] = 0xEE8D90FF;           r[UI_COLOR_TAB_ON] = 0xD643478C;
    r[UI_COLOR_SELECTED_EDGE] = 0xFFACAFFF; r[UI_COLOR_ROW] = 0x461011E6;
    r[UI_COLOR_ROW_EDGE] = 0xFF646840;      r[UI_COLOR_ROW_ON] = 0xD6434770;
    r[UI_COLOR_PLASMA_THREAD] = 0xFF4346FF;
}

static const unsigned int* s_palette = s_palettes[UI_THEME_GREEN];
#define PALETTE(name) s_palette[UI_COLOR_##name]

unsigned int ui_style_color(int name)
{
    FillPalettes();
    return name >= 0 && name < UI_COLOR_COUNT ? s_palette[name] : PALETTE(NAME);
}

void ui_style_theme(int theme)
{
    FillPalettes();
    if (theme < 0 || theme >= UI_THEME_COUNT) theme = UI_THEME_GREEN;
    s_palette = s_palettes[theme];
    ui_overlay_plasma_thread(PALETTE(PLASMA_THREAD));
}

static unsigned int Fade(unsigned int color, float share)
{
    float a = (float)(color & 0xFF) * share;
    return (color & 0xFFFFFF00) | (unsigned int)(a < 0.0f ? 0.0f : a > 255.0f ? 255.0f : a);
}

static const struct { int font; float size; int color; int glow; } kText[] = {
    { UI_FONT_BOLD,    9.0f,  UI_COLOR_HEAD,    UI_COLOR_HEAD_GLOW },   // HEADING
    { UI_FONT_BOLD,    11.0f, UI_COLOR_NAME,    -1 },                   // NAME
    { UI_FONT_REGULAR, 8.5f,  UI_COLOR_STATUS,  -1 },                   // STATUS
    { UI_FONT_BOLD,    14.0f, UI_COLOR_NAME,    UI_COLOR_HEAD_GLOW },   // TITLE
    { UI_FONT_REGULAR, 10.0f, UI_COLOR_STATUS,  -1 },                   // LINE
    { UI_FONT_REGULAR, 9.0f,  UI_COLOR_LABEL,   -1 },                   // LABEL
    { UI_FONT_REGULAR, 9.0f,  UI_COLOR_WARNING, -1 },                   // WARNING
};

float ui_style_text_size(int kind)
{
    return kind >= 0 && kind <= UI_TEXT_WARNING ? kText[kind].size : 10.0f;
}

float ui_style_text_scaled(int kind, float scale, float x, float y, int align, const char* text)
{
    if (kind < 0 || kind > UI_TEXT_WARNING) kind = UI_TEXT_LINE;
    if (kText[kind].glow >= 0)
        return ui_overlay_text_glow(kText[kind].font, kText[kind].size * scale, x, y, align,
                                    s_palette[kText[kind].color], s_palette[kText[kind].glow], text);
    return ui_overlay_text(kText[kind].font, kText[kind].size * scale, x, y, align, s_palette[kText[kind].color], text);
}

float ui_style_text(int kind, float x, float y, int align, const char* text)
{
    return ui_style_text_scaled(kind, 1.0f, x, y, align, text);
}

static unsigned int s_clock;

void ui_style_clock(unsigned int ms) { s_clock = ms; }

void ui_style_surface(float x, float y, float w, float h, float radius, unsigned int color, unsigned int edge, int lit)
{
    if (lit)
        for (int ring = 1; ring <= 4; ring++)
            ui_overlay_outline(x - ring, y - ring, w + 2 * ring, h + 2 * ring, radius + ring, 1.0f,
                               Fade(PALETTE(GLOW), (float)(edge & 0xFF) / 255.0f * 0.30f / (float)ring));
    ui_overlay_plasma(x, y, w, h, radius, color, s_clock);
    if (edge & 0xFF) ui_overlay_outline(x, y, w, h, radius, 1.0f, edge);
}

void ui_style_glass(float x, float y, float w, float h, float radius, unsigned int color, unsigned int edge)
{
    ui_overlay_rect(x, y, w, h, radius, color);
    if (edge & 0xFF) ui_overlay_outline(x, y, w, h, radius, 1.0f, edge);
}

void ui_style_panel(float x, float y, float w, float h, float share)
{
    // Nearly opaque glass so nothing under it reads through, its edge
    // glowing. The plasma belongs to the selection alone.
    for (int ring = 1; ring <= 4; ring++)
        ui_overlay_outline(x - ring, y - ring, w + 2 * ring, h + 2 * ring, 12.0f + ring, 1.0f,
                           Fade(PALETTE(GLOW), share * 0.30f / (float)ring));
    ui_overlay_gradient(x, y, w, h, 12, Fade(PALETTE(GLASS_TOP), share), Fade(PALETTE(GLASS_BOTTOM), share));
    ui_overlay_outline(x, y, w, h, 12, 1.0f, Fade(PALETTE(PANEL_EDGE), share));
}

void ui_style_row(float x, float y, float w, float h, int selected)
{
    if (selected) ui_style_surface(x, y, w, h, 7, PALETTE(ROW_ON), PALETTE(SELECTED_EDGE), 1);
    else          ui_style_glass(x, y, w, h, 7, PALETTE(ROW), PALETTE(ROW_EDGE));
}

void ui_style_setting(float x, float y, float w, float h, float scale, const char* label, const char* badge,
                      const char* value, int selected)
{
    float size = ui_style_text_size(UI_TEXT_NAME) * scale;
    float textY = y + (h - size) * 0.5f, right = x + w - size * 1.2f;
    float valueW = ui_overlay_text_width(UI_FONT_BOLD, size, value);

    ui_style_row(x, y, w, h, selected);
    float labelEnd = x + size * 1.1f + ui_style_text_scaled(UI_TEXT_NAME, scale, x + size * 1.1f, textY,
                                                            UI_ALIGN_LEFT, label);
    if (badge && badge[0]) {
        float bs = scale * 0.75f, bSize = ui_style_text_size(UI_TEXT_WARNING) * bs;
        float bw = ui_overlay_text_width(UI_FONT_REGULAR, bSize, badge) + bSize;
        float bx = labelEnd + size * 0.6f, bh = bSize * 1.6f;
        ui_overlay_outline(bx, y + (h - bh) * 0.5f, bw, bh, 3.0f, 0.8f, Fade(PALETTE(WARNING), 0.7f));
        ui_style_text_scaled(UI_TEXT_WARNING, bs, bx + bSize * 0.5f, y + (h - bSize) * 0.5f, UI_ALIGN_LEFT, badge);
    }
    // The value between its arrows, the way the dashboard's settings show it.
    ui_overlay_text(UI_FONT_BOLD, size, right, textY, UI_ALIGN_RIGHT, PALETTE(TAB), "\xE2\x80\xBA");
    ui_style_text_scaled(UI_TEXT_NAME, scale, right - size * 1.2f, textY, UI_ALIGN_RIGHT, value);
    ui_overlay_text(UI_FONT_BOLD, size, right - size * 1.8f - valueW, textY, UI_ALIGN_RIGHT, PALETTE(TAB),
                    "\xE2\x80\xB9");
}

static float Bumper(int button, const char* key, float size, float x, float y, bool measureOnly)
{
    if (ui_overlay_keyboard() && key)
        return measureOnly ? ui_overlay_key_width(key, size) : ui_overlay_key(key, size, x, y, 0xFFFFFFFF);
    return measureOnly ? ui_overlay_button_width(button, size) : ui_overlay_button(button, size, x, y, 0xFFFFFFFF);
}

void ui_style_tabs(float left, float right, float y, float h, const char* const* names, int count, int selected,
                   const char* leftKey, const char* rightKey, float* starts, float* ends)
{
    float size = h * 0.45f, pad = h * 0.4f, gap = 3.0f, total = 0.0f, glyph = h * 0.64f;
    for (int i = 0; i < count; i++)
        total += ui_overlay_text_width(UI_FONT_BOLD, size, names[i]) + 2 * pad + gap;
    total -= gap;
    Bumper(UI_BUTTON_LEFT_SHOULDER, leftKey, glyph, left, y + (h - glyph) * 0.5f, false);
    Bumper(UI_BUTTON_RIGHT_SHOULDER, rightKey, glyph,
           right - Bumper(UI_BUTTON_RIGHT_SHOULDER, rightKey, glyph, 0, 0, true), y + (h - glyph) * 0.5f, false);
    float x = (left + right) * 0.5f - total * 0.5f;
    for (int i = 0; i < count; i++) {
        float w = ui_overlay_text_width(UI_FONT_BOLD, size, names[i]) + 2 * pad;
        if (i == selected) {
            ui_style_surface(x, y, w, h, 6, PALETTE(TAB_ON), PALETTE(SELECTED_EDGE), 1);
            ui_overlay_text_glow(UI_FONT_BOLD, size, x + w * 0.5f, y + (h - size) * 0.5f, UI_ALIGN_CENTER,
                                 0xFFFFFFFF, PALETTE(PROMPT_GLOW), names[i]);
        } else {
            ui_overlay_text(UI_FONT_BOLD, size, x + w * 0.5f, y + (h - size) * 0.5f, UI_ALIGN_CENTER,
                            PALETTE(TAB), names[i]);
        }
        if (starts) starts[i] = x;
        if (ends) ends[i] = x + w;
        x += w + gap;
    }
}

static float PromptGlyph(const ui_style_prompt* p, float size, float x, float y, bool measureOnly)
{
    if (p->key && ui_overlay_keyboard())
        return measureOnly ? ui_overlay_key_width(p->key, size) : ui_overlay_key(p->key, size, x, y, 0xFFFFFFFF);
    return measureOnly ? ui_overlay_button_width(p->button, size) : ui_overlay_button(p->button, size, x, y, 0xFFFFFFFF);
}

void ui_style_prompts(float right, float y, float size, const ui_style_prompt* prompts, int count,
                      float* starts, float* ends)
{
    float gap = size, total = 0.0f;
    for (int i = 0; i < count; i++) {
        total += PromptGlyph(&prompts[i], size, 0, 0, true) + 4.0f +
                 ui_overlay_text_width(UI_FONT_BOLD, size * 0.72f, prompts[i].word);
        if (i) total += gap;
    }
    float x = right - total;
    for (int i = 0; i < count; i++) {
        if (starts) starts[i] = x;
        x += PromptGlyph(&prompts[i], size, x, y, false) + 4.0f;
        x += ui_overlay_text_glow(UI_FONT_BOLD, size * 0.72f, x, y + size * 0.14f, UI_ALIGN_LEFT, PALETTE(PROMPT),
                                  PALETTE(PROMPT_GLOW), prompts[i].word);
        if (ends) ends[i] = x;
        x += gap;
    }
}
