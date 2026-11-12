// ui_font.h: the guide's TrueType faces (Data/Fonts, Noto Sans), measured
// and rasterized with stb_truetype for ui_overlay.cpp's glyph atlas.
#pragma once

bool  UiFont_Metrics(int font, float pixelHeight, float* ascent, float* descent);
bool  UiFont_Has(int font, unsigned int codepoint);
float UiFont_Advance(int font, float pixelHeight, unsigned int codepoint, unsigned int previous);
// An 8 bit coverage bitmap, freed with UiFont_Free. NULL for a blank glyph.
unsigned char* UiFont_Glyph(int font, float pixelHeight, unsigned int codepoint,
                            int* width, int* height, int* xOffset, int* yOffset);
void  UiFont_Free(unsigned char* bitmap);
