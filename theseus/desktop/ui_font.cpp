// ui_font.cpp: the guide's fonts, read once from Data/Fonts.

#include "ui_font.h"
#include "ui_overlay.h"
#include "app_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/imstb_truetype.h"

static const char* const kFiles[UI_FONT_COUNT] = {
    "Data/Fonts/NotoSans-Regular.ttf",
    "Data/Fonts/NotoSans-Bold.ttf",
};

static struct {
    int state;   // 0 not tried, 1 ready, -1 missing
    stbtt_fontinfo info;
    unsigned char* data;
} s_fonts[UI_FONT_COUNT];

static stbtt_fontinfo* Face(int font)
{
    if (font < 0 || font >= UI_FONT_COUNT) font = UI_FONT_REGULAR;
    if (!s_fonts[font].state) {
        s_fonts[font].state = -1;
        FILE* f = fopen(AppPath(kFiles[font]), "rb");
        long size = -1;
        if (f && fseek(f, 0, SEEK_END) == 0) size = ftell(f);
        unsigned char* data = size > 0 && size < 32 * 1024 * 1024 ? (unsigned char*)malloc((size_t)size) : NULL;
        if (data && fseek(f, 0, SEEK_SET) == 0 && fread(data, 1, (size_t)size, f) == (size_t)size) {
            int offset = stbtt_GetFontOffsetForIndex(data, 0);
            if (offset >= 0 && stbtt_InitFont(&s_fonts[font].info, data, offset)) {
                s_fonts[font].data = data;
                s_fonts[font].state = 1;
                data = NULL;
            }
        }
        free(data);
        if (f) fclose(f);
        if (s_fonts[font].state < 0)
            fprintf(stderr, "[Guide] can't read %s, its text won't draw\n", kFiles[font]);
    }
    return s_fonts[font].state > 0 ? &s_fonts[font].info : NULL;
}

bool UiFont_Metrics(int font, float pixelHeight, float* ascent, float* descent)
{
    stbtt_fontinfo* info = Face(font);
    if (!info) return false;
    int a, d, gap;
    float scale = stbtt_ScaleForPixelHeight(info, pixelHeight);
    stbtt_GetFontVMetrics(info, &a, &d, &gap);
    *ascent = (float)a * scale;
    *descent = (float)-d * scale;
    return true;
}

bool UiFont_Has(int font, unsigned int codepoint)
{
    stbtt_fontinfo* info = Face(font);
    return info && stbtt_FindGlyphIndex(info, (int)codepoint) != 0;
}

float UiFont_Advance(int font, float pixelHeight, unsigned int codepoint, unsigned int previous)
{
    stbtt_fontinfo* info = Face(font);
    if (!info) return 0.0f;
    int advance, bearing;
    float scale = stbtt_ScaleForPixelHeight(info, pixelHeight);
    stbtt_GetCodepointHMetrics(info, (int)codepoint, &advance, &bearing);
    float w = (float)advance * scale;
    if (previous) w += (float)stbtt_GetCodepointKernAdvance(info, (int)previous, (int)codepoint) * scale;
    return w;
}

unsigned char* UiFont_Glyph(int font, float pixelHeight, unsigned int codepoint,
                            int* width, int* height, int* xOffset, int* yOffset)
{
    *width = *height = *xOffset = *yOffset = 0;
    stbtt_fontinfo* info = Face(font);
    if (!info) return NULL;
    return stbtt_GetCodepointBitmap(info, 0, stbtt_ScaleForPixelHeight(info, pixelHeight), (int)codepoint,
                                    width, height, xOffset, yOffset);
}

void UiFont_Free(unsigned char* bitmap)
{
    stbtt_FreeBitmap(bitmap, NULL);
}
