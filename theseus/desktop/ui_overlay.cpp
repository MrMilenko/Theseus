// ui_overlay.cpp: the guide's drawing. Shapes and text are gathered in the
// 640x480 layout while a frame is built, then laid out in window pixels and
// drawn in one bgfx submit on their own view, after the scene and before
// ImGui.
//
// Each shape is a quad whose fragment shader works out a rounded rect's edge
// from the distance to it, so corners and outlines are smooth at any size.
// Text is glyphs packed into one atlas the first time they're drawn at a
// size. The plasma is the dashboard's look under a selected row: a tiling
// noise map sampled twice at two scrolling scales.

#include "ui_overlay.h"
#include "ui_font.h"
#include <bgfx/bgfx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bgfx::ShaderHandle theseus_bgfx_load_shader(const char* name);

enum {
    LAYOUT_W = 640,
    LAYOUT_H = 480,
    MAX_QUADS = 16384,
    ATLAS = 2048,
    MAX_GLYPHS = 4096,
    MAX_TEXT = 64 * 1024,
    MAX_TEXTS = 4096,
    MAX_LAYERS = 5,
    NOISE = 128,
};

// Marks in a quad's radius: what it is besides a plain shape.
static const float PLASMA_MARK = -4.0f;
static const float GLYPH_MARK  = -1.0f;

struct Quad {
    float x, y, w, h;
    float radius, thickness;
    unsigned int top, bottom;
    float u0, v0, u1, v1;   // u0 < 0: a shape
    int layer;
};

struct Vertex {
    float x, y;
    float u, v;
    float lx, ly, hw, hh;   // the point from the shape's middle, its half size
    float radius, thickness;
    unsigned char color[4];
};

struct Glyph {
    int font, pixelSize;
    unsigned int codepoint;
    short ax, ay, w, h, xoff, yoff;
    float advance;
};

struct Text {
    int font, align, layer;
    float size, x, y;
    unsigned int color;
    int offset;
};

static struct {
    bool ready, failed;
    bgfx::ProgramHandle program;
    bgfx::VertexLayout layout;
    bgfx::TextureHandle atlasTex, plasmaTex;
    bgfx::UniformHandle uScale, uShift, uThread, sAtlas, sPlasma;

    Quad quads[MAX_QUADS];
    int quadCount;
    Text texts[MAX_TEXTS];
    int textCount;
    char text[MAX_TEXT];
    int textUsed;
    int layer;

    Glyph glyphs[MAX_GLYPHS];
    int glyphCount;
    int shelfX, shelfY, shelfH;
    float lastScale;
    unsigned char* atlas;
    int dirtyTop, dirtyBottom;

    float shift[8];   // second map's place and scale, then the threads' color
    bool plasmaDrawn;

    int device;
    float scale, originX, originY;   // last present's layout -> pixels

    Vertex vertices[MAX_QUADS * 6];
} ov;

static unsigned int Fade(unsigned int color, float share)
{
    float a = (float)(color & 0xFF) * share;
    return (color & 0xFFFFFF00) | (unsigned int)(a < 0.0f ? 0.0f : a > 255.0f ? 255.0f : a);
}

static Quad* NewQuad()
{
    if (ov.quadCount >= MAX_QUADS) return NULL;
    Quad* q = &ov.quads[ov.quadCount++];
    memset(q, 0, sizeof(*q));
    q->u0 = -1.0f;
    q->layer = ov.layer;
    return q;
}

void ui_overlay_layer(int layer)
{
    ov.layer = layer < 0 ? 0 : layer >= MAX_LAYERS ? MAX_LAYERS - 1 : layer;
}

void ui_overlay_gradient(float x, float y, float w, float h, float radius, unsigned int top, unsigned int bottom)
{
    Quad* q = NewQuad();
    if (!q) return;
    q->x = x; q->y = y; q->w = w; q->h = h;
    q->radius = radius;
    q->top = top; q->bottom = bottom;
}

void ui_overlay_rect(float x, float y, float w, float h, float radius, unsigned int color)
{
    ui_overlay_gradient(x, y, w, h, radius, color, color);
}

void ui_overlay_outline(float x, float y, float w, float h, float radius, float thickness, unsigned int color)
{
    Quad* q = NewQuad();
    if (!q) return;
    q->x = x; q->y = y; q->w = w; q->h = h;
    q->radius = radius;
    q->thickness = thickness > 0.0f ? thickness : 1.0f;
    q->top = q->bottom = color;
}

// Text

static unsigned int NextCodepoint(const char** cursor)
{
    const unsigned char* b = (const unsigned char*)*cursor;
    if (!*b) return 0;
    unsigned int cp;
    int extra;
    if (b[0] < 0x80)                { cp = b[0];        extra = 0; }
    else if ((b[0] & 0xE0) == 0xC0) { cp = b[0] & 0x1F; extra = 1; }
    else if ((b[0] & 0xF0) == 0xE0) { cp = b[0] & 0x0F; extra = 2; }
    else                            { cp = b[0] & 0x07; extra = 3; }
    *cursor += 1;
    while (extra-- > 0 && (**cursor & 0xC0) == 0x80) {
        cp = (cp << 6) | (unsigned int)(**cursor & 0x3F);
        *cursor += 1;
    }
    return cp;
}

// Regular has more scripts than Bold doesn't; a missing character falls back.
static int GlyphFont(int font, unsigned int cp)
{
    if (font != UI_FONT_REGULAR && cp > ' ' && !UiFont_Has(font, cp)) return UI_FONT_REGULAR;
    return font;
}

static float Measure(int font, float size, const char* text)
{
    const char* c = text;
    unsigned int cp, prev = 0;
    int prevFont = -1;
    float w = 0.0f;
    while ((cp = NextCodepoint(&c)) != 0) {
        int face = GlyphFont(font, cp);
        w += UiFont_Advance(face, size, cp, face == prevFont ? prev : 0);
        prev = cp;
        prevFont = face;
    }
    return w;
}

float ui_overlay_text_width(int font, float size, const char* text)
{
    return text ? Measure(font, size, text) : 0.0f;
}

static float AddText(int font, float size, float x, float y, int align, unsigned int color, const char* text)
{
    int len = (int)strlen(text);
    float w = Measure(font, size, text);
    if (ov.textCount >= MAX_TEXTS || ov.textUsed + len + 1 > MAX_TEXT) return w;
    Text& t = ov.texts[ov.textCount++];
    t.font = font; t.align = align; t.layer = ov.layer;
    t.size = size; t.x = x; t.y = y;
    t.color = color;
    t.offset = ov.textUsed;
    memcpy(ov.text + ov.textUsed, text, (size_t)len + 1);
    ov.textUsed += len + 1;
    return w;
}

float ui_overlay_text(int font, float size, float x, float y, int align, unsigned int color, const char* text)
{
    return text && *text ? AddText(font, size, x, y, align, color, text) : 0.0f;
}

float ui_overlay_text_glow(int font, float size, float x, float y, int align, unsigned int color,
                           unsigned int glow, const char* text)
{
    // The glow is the text again, faint, in a ring of offsets at two distances.
    static const float ring[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 0.7f, 0.7f }, { -0.7f, 0.7f }, { 0.7f, -0.7f }, { -0.7f, -0.7f },
    };
    if (!text || !*text) return 0.0f;
    float spread = size * 0.06f > 0.6f ? size * 0.06f : 0.6f;
    AddText(font, size, x + 0.8f, y + 1.0f, align, Fade(0x000000FF, (float)(color & 0xFF) / 255.0f * 0.7f), text);
    for (int i = 0; i < 8 && (glow & 0xFF); i++) {
        AddText(font, size, x + ring[i][0] * spread * 2, y + ring[i][1] * spread * 2, align, Fade(glow, 0.10f), text);
        AddText(font, size, x + ring[i][0] * spread, y + ring[i][1] * spread, align, Fade(glow, 0.16f), text);
    }
    return AddText(font, size, x, y, align, color, text);
}

// The plasma

// Smooth value noise in octaves, tiling, from a fixed seed: the same each run.
static unsigned char* PlasmaNoise()
{
    enum { CELLS = 4 };
    unsigned char* px = (unsigned char*)malloc(NOISE * NOISE * 4);
    if (!px) return NULL;
    static float lattice[3][32][32];
    unsigned int seed = 0x9E3779B9u;
    for (int o = 0; o < 3; o++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                seed = seed * 1664525u + 1013904223u;
                lattice[o][y][x] = (float)(seed >> 8) / 16777216.0f;
            }
    for (int y = 0; y < NOISE; y++)
        for (int x = 0; x < NOISE; x++) {
            float value = 0.0f, weight = 0.6f;
            for (int o = 0; o < 3; o++) {
                int cells = CELLS << o;
                float fx = (float)x * cells / NOISE, fy = (float)y * cells / NOISE;
                int x0 = (int)fx, y0 = (int)fy, x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
                float tx = fx - x0, ty = fy - y0;
                tx = tx * tx * (3.0f - 2.0f * tx);
                ty = ty * ty * (3.0f - 2.0f * ty);
                float a = lattice[o][y0][x0] + (lattice[o][y0][x1] - lattice[o][y0][x0]) * tx;
                float b = lattice[o][y1][x0] + (lattice[o][y1][x1] - lattice[o][y1][x0]) * tx;
                value += (a + (b - a) * ty) * weight;
                weight *= 0.5f;
            }
            value /= 1.05f;
            unsigned char* p = px + (y * NOISE + x) * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = (unsigned char)(value < 0.0f ? 0 : value > 1.0f ? 255 : value * 255.0f);
        }
    return px;
}

static float Scroll(unsigned int ms, float perSecond)
{
    return (float)fmod((double)perSecond * 0.001 * (double)ms, 1.0);
}

void ui_overlay_plasma(float x, float y, float w, float h, float radius, unsigned int color, unsigned int ms)
{
    // Two maps, one repeating every 311 layout pixels and one every 201, each
    // scrolling at its own pace.
    const float first = 311.0f, second = 201.0f;
    float fx = Scroll(ms, 0.03215434f), fy = Scroll(ms, 0.026795285f);
    float sx = -Scroll(ms, 0.035536603f), sy = -Scroll(ms, 0.031094525f);
    Quad* q = NewQuad();
    if (!q) return;
    q->x = x; q->y = y; q->w = w; q->h = h;
    q->radius = PLASMA_MARK;
    q->thickness = radius;
    q->top = q->bottom = color;
    q->u0 = x / first + fx;
    q->v0 = y / first + fy;
    q->u1 = (x + w) / first + fx;
    q->v1 = (y + h) / first + fy;
    ov.shift[0] = sx - fx * first / second;
    ov.shift[1] = sy - fy * first / second;
    ov.shift[2] = first / second;
    if (ov.shift[7] == 0.0f) ui_overlay_plasma_thread(0xB8F23CFF);
    ov.plasmaDrawn = true;
}

void ui_overlay_plasma_thread(unsigned int color)
{
    ov.shift[4] = (float)((color >> 24) & 0xFF) / 255.0f;
    ov.shift[5] = (float)((color >> 16) & 0xFF) / 255.0f;
    ov.shift[6] = (float)((color >> 8) & 0xFF) / 255.0f;
    ov.shift[7] = 1.0f;
}

// Buttons and keys

void ui_overlay_set_device(int device) { ov.device = device; }
int  ui_overlay_keyboard() { return ov.device == 0; }

static const char* const kPillLabels[2][UI_BUTTON_COUNT] = {
    { "ENTER", "ESC", "X", "Y", "F7", "BKSP", "Q", "E", "\xE2\x86\x90", "\xE2\x86\x92" },
    { "A", "B", "X", "Y", "START", "BACK", "LB", "RB", "\xE2\x80\xB9", "\xE2\x80\xBA" },
};

// The pad's face buttons, lit at the top and deep at the bottom.
static const unsigned int kFaceColors[4][2] = {
    { 0x9BE57AFF, 0x2C8A1CFF },   // A green
    { 0xFF8A7AFF, 0xB0201CFF },   // B red
    { 0x8FB4FFFF, 0x2448B8FF },   // X blue
    { 0xFFE680FF, 0xC48A0CFF },   // Y yellow
};

static float PillTextSize(float size) { return size * 0.52f; }

static float PillWidth(const char* label, float size)
{
    float w = Measure(UI_FONT_BOLD, PillTextSize(size), label) + size * 0.6f;
    return w > size * 1.3f ? w : size * 1.3f;
}

static float Pill(const char* label, float size, float x, float y, unsigned int color, bool key)
{
    float share = (float)(color & 0xFF) / 255.0f;
    float w = PillWidth(label, size);
    float h = size * 0.78f, top = y + (size - h) * 0.5f;
    float r = h * (key ? 0.22f : 0.45f);
    float ls = PillTextSize(size);
    ui_overlay_rect(x, top + size * 0.06f, w, h, r, Fade(0x00000070, share));
    ui_overlay_gradient(x, top, w, h, r, Fade(key ? 0xF2F5FAFF : 0x5A606BFF, share),
                        Fade(key ? 0xA9B3C2FF : 0x16191FFF, share));
    ui_overlay_outline(x, top, w, h, r, 0.75f, Fade(key ? 0x5F6B7DFF : 0x9AA3B0A0, share));
    AddText(UI_FONT_BOLD, ls, x + w * 0.5f, top + (h - ls) * 0.5f + 0.2f, UI_ALIGN_CENTER,
            Fade(key ? 0x1A2230FF : 0xFFFFFFFF, share), label);
    return w;
}

float ui_overlay_button_width(int button, float size)
{
    if (button < 0 || button >= UI_BUTTON_COUNT) return 0.0f;
    if (ov.device == 1 && button <= UI_BUTTON_Y) return size;
    return PillWidth(kPillLabels[ov.device == 1][button], size);
}

float ui_overlay_button(int button, float size, float x, float y, unsigned int color)
{
    if (button < 0 || button >= UI_BUTTON_COUNT) return 0.0f;
    if (ov.device != 1 || button > UI_BUTTON_Y)
        return Pill(kPillLabels[ov.device == 1][button], size, x, y, color, ov.device != 1);

    // A glossy ball: lit to deep, a sheen on its upper half, its letter in white.
    float share = (float)(color & 0xFF) / 255.0f;
    float d = size * 0.96f, inset = (size - d) * 0.5f, letter = size * 0.66f;
    const char* label = kPillLabels[1][button];
    ui_overlay_rect(x + inset, y + inset + size * 0.05f, d, d, d * 0.5f, Fade(0x00000060, share));
    ui_overlay_gradient(x + inset, y + inset, d, d, d * 0.5f,
                        Fade(kFaceColors[button][0], share), Fade(kFaceColors[button][1], share));
    ui_overlay_gradient(x + inset + d * 0.16f, y + inset + d * 0.06f, d * 0.68f, d * 0.42f, d * 0.21f,
                        Fade(0xFFFFFF70, share), Fade(0xFFFFFF08, share));
    ui_overlay_outline(x + inset, y + inset, d, d, d * 0.5f, 0.75f, Fade(0x00000070, share));
    AddText(UI_FONT_BOLD, letter, x + size * 0.5f + 0.4f, y + (size - letter) * 0.5f + 0.9f, UI_ALIGN_CENTER,
            Fade(0x00000080, share), label);
    AddText(UI_FONT_BOLD, letter, x + size * 0.5f, y + (size - letter) * 0.5f + 0.3f, UI_ALIGN_CENTER,
            Fade(0xFFFFFFFF, share), label);
    return size;
}

float ui_overlay_key_width(const char* label, float size)
{
    return PillWidth(label ? label : "", size);
}

float ui_overlay_key(const char* label, float size, float x, float y, unsigned int color)
{
    return Pill(label ? label : "", size, x, y, color, true);
}

// Drawing with bgfx

static bool SetUp()
{
    if (ov.ready || ov.failed) return ov.ready;
    ov.failed = true;
    bgfx::ShaderHandle vs = theseus_bgfx_load_shader("vs_guide");
    bgfx::ShaderHandle fs = theseus_bgfx_load_shader("fs_guide");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        fprintf(stderr, "[Guide] no guide shaders for this renderer, the guide won't draw\n");
        return false;
    }
    ov.program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(ov.program)) return false;

    ov.layout.begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    ov.uScale  = bgfx::createUniform("u_guideScale",  bgfx::UniformType::Vec4);
    ov.uShift  = bgfx::createUniform("u_plasmaShift", bgfx::UniformType::Vec4);
    ov.uThread = bgfx::createUniform("u_plasmaThread", bgfx::UniformType::Vec4);
    ov.sAtlas  = bgfx::createUniform("s_atlas",  bgfx::UniformType::Sampler);
    ov.sPlasma = bgfx::createUniform("s_plasma", bgfx::UniformType::Sampler);

    ov.atlas = (unsigned char*)calloc(ATLAS, ATLAS);
    unsigned char* noise = PlasmaNoise();
    if (!ov.atlas || !noise) { free(noise); return false; }
    // No initial data: bgfx makes a texture created with data immutable, and
    // the glyphs arrive as updates.
    ov.atlasTex = bgfx::createTexture2D(ATLAS, ATLAS, false, 1, bgfx::TextureFormat::R8,
                                        BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    ov.plasmaTex = bgfx::createTexture2D(NOISE, NOISE, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE,
                                         bgfx::copy(noise, NOISE * NOISE * 4));
    free(noise);
    ov.dirtyTop = 0;
    ov.dirtyBottom = ATLAS;
    ov.shelfX = ov.shelfY = 2;
    ov.failed = false;
    ov.ready = true;
    return true;
}

void ui_overlay_shutdown()
{
    if (!ov.ready) return;
    bgfx::destroy(ov.program);
    bgfx::destroy(ov.atlasTex);
    bgfx::destroy(ov.plasmaTex);
    bgfx::destroy(ov.uScale);
    bgfx::destroy(ov.uShift);
    bgfx::destroy(ov.uThread);
    bgfx::destroy(ov.sAtlas);
    bgfx::destroy(ov.sPlasma);
    free(ov.atlas);
    ov.atlas = NULL;
    ov.ready = false;
}

static void ForgetGlyphs()
{
    ov.glyphCount = 0;
    ov.shelfX = ov.shelfY = 2;
    ov.shelfH = 0;
}

// A glyph at a pixel size, packed into the atlas the first time. NULL when
// the atlas is full; it starts over next frame.
static Glyph* GetGlyph(int font, int pixelSize, unsigned int cp, bool* full)
{
    for (int i = 0; i < ov.glyphCount; i++) {
        Glyph* g = &ov.glyphs[i];
        if (g->codepoint == cp && g->pixelSize == pixelSize && g->font == font) return g;
    }
    if (ov.glyphCount >= MAX_GLYPHS) { *full = true; return NULL; }
    int w, h, xo, yo;
    unsigned char* bmp = UiFont_Glyph(font, (float)pixelSize, cp, &w, &h, &xo, &yo);
    if (w > 0 && h > 0) {
        if (ov.shelfX + w + 2 > ATLAS) {
            ov.shelfX = 2;
            ov.shelfY += ov.shelfH + 2;
            ov.shelfH = 0;
        }
        if (ov.shelfY + h + 2 > ATLAS) {
            UiFont_Free(bmp);
            *full = true;
            return NULL;
        }
    }
    Glyph* g = &ov.glyphs[ov.glyphCount++];
    g->font = font; g->pixelSize = pixelSize; g->codepoint = cp;
    g->w = (short)w; g->h = (short)h; g->xoff = (short)xo; g->yoff = (short)yo;
    g->advance = UiFont_Advance(font, (float)pixelSize, cp, 0);
    g->ax = (short)ov.shelfX;
    g->ay = (short)ov.shelfY;
    if (bmp && w > 0 && h > 0) {
        // A clear texel all around, or filtering at a fraction of a pixel
        // pulls in whatever was packed there before.
        for (int row = -1; row <= h; row++) {
            unsigned char* dst = ov.atlas + (ov.shelfY + row) * ATLAS + ov.shelfX - 1;
            memset(dst, 0, (size_t)w + 2);
            if (row >= 0 && row < h) memcpy(dst + 1, bmp + row * w, (size_t)w);
        }
        if (ov.shelfY - 1 < ov.dirtyTop) ov.dirtyTop = ov.shelfY - 1;
        if (ov.shelfY + h + 1 > ov.dirtyBottom) ov.dirtyBottom = ov.shelfY + h + 1;
        ov.shelfX += w + 2;
        if (h > ov.shelfH) ov.shelfH = h;
    }
    if (bmp) UiFont_Free(bmp);
    return g;
}

// A text's glyphs as quads already in window pixels.
static void LayOutText(int index, bool* full)
{
    const Text& t = ov.texts[index];
    const char* s = ov.text + t.offset;
    int px = (int)floorf(t.size * ov.scale + 0.5f);
    float ascent, descent;
    if (px < 4 || !UiFont_Metrics(t.font, (float)px, &ascent, &descent)) return;
    float w = Measure(t.font, (float)px, s);
    float penX = ov.originX + t.x * ov.scale;
    if (t.align == UI_ALIGN_CENTER) penX -= w * 0.5f;
    else if (t.align == UI_ALIGN_RIGHT) penX -= w;
    // y is the line's top: the baseline an ascent below, centered in the size
    float penY = ov.originY + t.y * ov.scale + ((float)px - (ascent + descent)) * 0.5f + ascent;
    penX = floorf(penX + 0.5f);
    penY = floorf(penY + 0.5f);
    unsigned int cp, prev = 0;
    int prevFont = -1;
    while ((cp = NextCodepoint(&s)) != 0) {
        int face = GlyphFont(t.font, cp);
        if (prev && face == prevFont)
            penX += UiFont_Advance(face, (float)px, cp, prev) - UiFont_Advance(face, (float)px, cp, 0);
        Glyph* g = GetGlyph(face, px, cp, full);
        if (!g) return;
        Quad* q;
        if (g->w > 0 && (q = NewQuad()) != NULL) {
            q->x = penX + g->xoff;
            q->y = penY + g->yoff;
            q->w = g->w;
            q->h = g->h;
            q->top = q->bottom = t.color;
            q->u0 = (float)g->ax / ATLAS;
            q->v0 = (float)g->ay / ATLAS;
            q->u1 = (float)(g->ax + g->w) / ATLAS;
            q->v1 = (float)(g->ay + g->h) / ATLAS;
            q->radius = GLYPH_MARK;
            q->layer = t.layer;
        }
        penX += g->advance;
        prev = cp;
        prevFont = face;
    }
}

static void Put(Vertex* v, float x, float y, float u, float t, float lx, float ly, float hw, float hh,
                float radius, float thickness, unsigned int color)
{
    v->x = x; v->y = y; v->u = u; v->v = t;
    v->lx = lx; v->ly = ly; v->hw = hw; v->hh = hh;
    v->radius = radius; v->thickness = thickness;
    v->color[0] = (unsigned char)(color >> 24);
    v->color[1] = (unsigned char)(color >> 16);
    v->color[2] = (unsigned char)(color >> 8);
    v->color[3] = (unsigned char)color;
}

// Each layer's shapes, then its text, then the next layer. Returns the count.
static int BuildVertices(int shapeCount)
{
    int count = 0;
    for (int layer = 0; layer < MAX_LAYERS; layer++)
    for (int pass = 0; pass < 2; pass++)
    for (int i = pass ? shapeCount : 0; i < (pass ? ov.quadCount : shapeCount) && count + 6 <= MAX_QUADS * 6; i++) {
        const Quad* q = &ov.quads[i];
        if (q->layer != layer) continue;
        Vertex* v = &ov.vertices[count];
        unsigned int tc = q->top, bc = q->bottom;
        if (pass == 0) {
            float l = ov.originX + q->x * ov.scale, t = ov.originY + q->y * ov.scale;
            float r = l + q->w * ov.scale, b = t + q->h * ov.scale;
            float hw = (r - l) * 0.5f, hh = (b - t) * 0.5f;
            if (q->radius == PLASMA_MARK) {
                float rad = q->thickness * ov.scale;
                Put(&v[0], l, t, q->u0, q->v0, -hw, -hh, hw, hh, PLASMA_MARK, rad, tc);
                Put(&v[1], r, t, q->u1, q->v0,  hw, -hh, hw, hh, PLASMA_MARK, rad, tc);
                Put(&v[2], l, b, q->u0, q->v1, -hw,  hh, hw, hh, PLASMA_MARK, rad, bc);
                Put(&v[3], r, t, q->u1, q->v0,  hw, -hh, hw, hh, PLASMA_MARK, rad, tc);
                Put(&v[4], r, b, q->u1, q->v1,  hw,  hh, hw, hh, PLASMA_MARK, rad, bc);
                Put(&v[5], l, b, q->u0, q->v1, -hw,  hh, hw, hh, PLASMA_MARK, rad, bc);
            } else {
                // A pixel larger all round for the smooth edge.
                float rad = q->radius * ov.scale;
                float th = q->thickness > 0.0f ? fmaxf(1.0f, q->thickness * ov.scale) : 0.0f;
                const float g = 1.0f;
                Put(&v[0], l - g, t - g, -1, 0, -hw - g, -hh - g, hw, hh, rad, th, tc);
                Put(&v[1], r + g, t - g, -1, 0,  hw + g, -hh - g, hw, hh, rad, th, tc);
                Put(&v[2], l - g, b + g, -1, 0, -hw - g,  hh + g, hw, hh, rad, th, bc);
                Put(&v[3], r + g, t - g, -1, 0,  hw + g, -hh - g, hw, hh, rad, th, tc);
                Put(&v[4], r + g, b + g, -1, 0,  hw + g,  hh + g, hw, hh, rad, th, bc);
                Put(&v[5], l - g, b + g, -1, 0, -hw - g,  hh + g, hw, hh, rad, th, bc);
            }
        } else {
            float l = q->x, t = q->y, r = l + q->w, b = t + q->h;
            Put(&v[0], l, t, q->u0, q->v0, 0, 0, 0, 0, 0, 0, tc);
            Put(&v[1], r, t, q->u1, q->v0, 0, 0, 0, 0, 0, 0, tc);
            Put(&v[2], l, b, q->u0, q->v1, 0, 0, 0, 0, 0, 0, tc);
            Put(&v[3], r, t, q->u1, q->v0, 0, 0, 0, 0, 0, 0, tc);
            Put(&v[4], r, b, q->u1, q->v1, 0, 0, 0, 0, 0, 0, tc);
            Put(&v[5], l, b, q->u0, q->v1, 0, 0, 0, 0, 0, 0, tc);
        }
        count += 6;
    }
    return count;
}

static void Place(int width, int height)
{
    // 480 layout lines fill the height; a wider window has margins either side.
    ov.scale = (float)height / LAYOUT_H;
    ov.originX = ((float)width - LAYOUT_W * ov.scale) * 0.5f;
    ov.originY = 0.0f;
}

void ui_overlay_to_layout(float px, float py, float* x, float* y)
{
    float s = ov.scale > 0.0f ? ov.scale : 1.0f;
    *x = (px - ov.originX) / s;
    *y = (py - ov.originY) / s;
}

float ui_overlay_layout_left()  { return ov.scale > 0.0f ? -ov.originX / ov.scale : 0.0f; }
float ui_overlay_layout_right() { return ov.scale > 0.0f ? LAYOUT_W + ov.originX / ov.scale : (float)LAYOUT_W; }

void ui_overlay_present(unsigned short view, int width, int height)
{
    if (width > 0 && height > 0) Place(width, height);
    if ((!ov.quadCount && !ov.textCount) || width <= 0 || height <= 0 || !SetUp()) {
        ov.quadCount = ov.textCount = ov.textUsed = 0;
        ov.layer = 0;
        ov.plasmaDrawn = false;
        return;
    }
    if (ov.scale != ov.lastScale) {
        ForgetGlyphs();
        ov.lastScale = ov.scale;
    }
    bool full = false;
    int shapes = ov.quadCount;
    for (int i = 0; i < ov.textCount; i++) LayOutText(i, &full);
    int count = BuildVertices(shapes);

    if (ov.dirtyBottom > ov.dirtyTop)
        bgfx::updateTexture2D(ov.atlasTex, 0, 0, 0, (uint16_t)ov.dirtyTop, ATLAS,
                              (uint16_t)(ov.dirtyBottom - ov.dirtyTop),
                              bgfx::copy(ov.atlas + (size_t)ov.dirtyTop * ATLAS,
                                         (uint32_t)((ov.dirtyBottom - ov.dirtyTop) * ATLAS)));
    ov.dirtyTop = ATLAS;
    ov.dirtyBottom = 0;

    if (count > 0 && bgfx::getAvailTransientVertexBuffer((uint32_t)count, ov.layout) >= (uint32_t)count) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, (uint32_t)count, ov.layout);
        memcpy(tvb.data, ov.vertices, (size_t)count * sizeof(Vertex));
        bgfx::setViewRect(view, 0, 0, (uint16_t)width, (uint16_t)height);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        float scale[4] = { 2.0f / (float)width, -2.0f / (float)height, 0.0f, 0.0f };
        bgfx::setUniform(ov.uScale, scale);
        bgfx::setUniform(ov.uShift, ov.shift);
        bgfx::setUniform(ov.uThread, ov.shift + 4);
        bgfx::setTexture(0, ov.sAtlas, ov.atlasTex);
        bgfx::setTexture(1, ov.sPlasma, ov.plasmaTex);
        bgfx::setVertexBuffer(0, &tvb, 0, (uint32_t)count);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                      BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::submit(view, ov.program);
    }
    ov.quadCount = ov.textCount = ov.textUsed = 0;
    ov.layer = 0;
    ov.plasmaDrawn = false;
    if (full) ForgetGlyphs();
}
