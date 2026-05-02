// xbx_texture.h: XBX texture parser. Parses Xbox XPR0 (.xbx) texture
// format (DXT1 / DXT3 / DXT5 / linear, swizzled / unswizzled) and
// creates GL texture objects. Used by desktop/image.cpp.

#pragma once

#include "d3d8_sdl.h"

// XPR_HEADER and XPR_MAGIC_VALUE defined in sdl_platform.h (via std.h)

// Xbox D3D8 format field layout (from D3D8.h)
#define XBOX_D3DFORMAT_FORMAT_MASK   0x0000FF00
#define XBOX_D3DFORMAT_FORMAT_SHIFT  8
#define XBOX_D3DFORMAT_USIZE_MASK   0x00F00000
#define XBOX_D3DFORMAT_USIZE_SHIFT  20
#define XBOX_D3DFORMAT_VSIZE_MASK   0x0F000000
#define XBOX_D3DFORMAT_VSIZE_SHIFT  24

// Xbox D3D8 Common field
#define XBOX_D3DCOMMON_TYPE_MASK    0x00070000
#define XBOX_D3DCOMMON_TYPE_TEXTURE 0x00040000

// Xbox D3D8 Size field (for linear/non-power-of-2 textures)
#define XBOX_D3DSIZE_WIDTH_MASK    0x00000FFF
#define XBOX_D3DSIZE_HEIGHT_MASK   0x00FFF000
#define XBOX_D3DSIZE_HEIGHT_SHIFT  12

// Xbox D3D format values (from D3D8Types.h). NOT the same as PC D3D.
#define XBOX_D3DFMT_L8          0x00
#define XBOX_D3DFMT_AL8         0x01
#define XBOX_D3DFMT_A1R5G5B5    0x02
#define XBOX_D3DFMT_X1R5G5B5    0x03
#define XBOX_D3DFMT_A4R4G4B4    0x04
#define XBOX_D3DFMT_R5G6B5      0x05
#define XBOX_D3DFMT_A8R8G8B8    0x06
#define XBOX_D3DFMT_X8R8G8B8    0x07
#define XBOX_D3DFMT_P8          0x0B
#define XBOX_D3DFMT_DXT1        0x0C
#define XBOX_D3DFMT_DXT3        0x0E
#define XBOX_D3DFMT_DXT5        0x0F
#define XBOX_D3DFMT_LIN_R5G6B5      0x11
#define XBOX_D3DFMT_LIN_A8R8G8B8    0x12
#define XBOX_D3DFMT_LIN_L8          0x13
#define XBOX_D3DFMT_LIN_R8B8        0x16
#define XBOX_D3DFMT_LIN_G8B8        0x17
#define XBOX_D3DFMT_LIN_A8          0x19
#define XBOX_D3DFMT_LIN_A8L8        0x1A
#define XBOX_D3DFMT_LIN_AL8         0x1B
#define XBOX_D3DFMT_LIN_X1R5G5B5    0x1C
#define XBOX_D3DFMT_LIN_A4R4G4B4    0x1D
#define XBOX_D3DFMT_LIN_X8R8G8B8    0x1E
#define XBOX_D3DFMT_LIN_A8R8G8B8_ALT 0x12

// Bytes per pixel for Xbox formats
inline int XboxBytesPerPixel(DWORD fmt) {
    switch (fmt) {
        case XBOX_D3DFMT_A8R8G8B8: case XBOX_D3DFMT_X8R8G8B8:
        case XBOX_D3DFMT_LIN_A8R8G8B8: case XBOX_D3DFMT_LIN_X8R8G8B8:
            return 4;
        case XBOX_D3DFMT_R5G6B5: case XBOX_D3DFMT_A1R5G5B5: case XBOX_D3DFMT_X1R5G5B5:
        case XBOX_D3DFMT_A4R4G4B4:
        case XBOX_D3DFMT_LIN_R5G6B5: case XBOX_D3DFMT_LIN_A4R4G4B4:
        case XBOX_D3DFMT_LIN_X1R5G5B5:
        case XBOX_D3DFMT_LIN_A8L8: case XBOX_D3DFMT_LIN_R8B8: case XBOX_D3DFMT_LIN_G8B8:
            return 2;
        case XBOX_D3DFMT_L8: case XBOX_D3DFMT_AL8: case XBOX_D3DFMT_P8:
        case XBOX_D3DFMT_LIN_L8: case XBOX_D3DFMT_LIN_A8: case XBOX_D3DFMT_LIN_AL8:
            return 1;
        default:
            return 4; // assume 4 bpp for unknown
    }
}

// Check if format is compressed (DXT)
inline bool XboxFormatIsDXT(DWORD fmt) {
    return fmt == XBOX_D3DFMT_DXT1 || fmt == XBOX_D3DFMT_DXT3 || fmt == XBOX_D3DFMT_DXT5;
}

// Check if format is linear (not swizzled)
inline bool XboxFormatIsLinear(DWORD fmt) {
    return fmt >= 0x10; // Linear formats start at 0x10
}

// -------------------------------------------------------
// Xbox Swizzler (from XDK XGraphics.h)
// Unswizzles Morton-coded (Z-order) texture data
// -------------------------------------------------------
class XboxSwizzler {
public:
    DWORD m_MaskU, m_MaskV;
    void Init(DWORD width, DWORD height) {
        m_MaskU = 0; m_MaskV = 0;
        DWORD i = 1, j = 1;
        DWORD k;
        do {
            k = 0;
            if (i < width)  { m_MaskU |= j; k = (j <<= 1); }
            if (i < height) { m_MaskV |= j; k = (j <<= 1); }
            i <<= 1;
        } while (k);
    }

    // Convert swizzled index to linear (u, v) coordinates
    void UnswizzleCoords(DWORD swizIdx, DWORD& u, DWORD& v) {
        u = 0; v = 0;
        DWORD srcU = swizIdx, srcV = swizIdx;
        DWORD j;
        j = 1;
        for (DWORD i = 1; i <= m_MaskU; i <<= 1) {
            if (m_MaskU & i) { u |= (srcU & j); j <<= 1; }
            else { srcU >>= 1; }
        }
        j = 1;
        for (DWORD i = 1; i <= m_MaskV; i <<= 1) {
            if (m_MaskV & i) { v |= (srcV & j); j <<= 1; }
            else { srcV >>= 1; }
        }
    }
};

// -------------------------------------------------------
// DXT decompression (simplified S3TC decoder)
// -------------------------------------------------------
inline void DXT_DecodeColorBlock(const BYTE* block, BYTE* outRGBA, int stride, bool hasAlpha) {
    WORD c0 = *(WORD*)(block + 0);
    WORD c1 = *(WORD*)(block + 2);
    DWORD bits = *(DWORD*)(block + 4);

    BYTE colors[4][4]; // [idx][R,G,B,A]
    // Decode 565 colors
    colors[0][0] = (BYTE)(((c0 >> 11) & 0x1F) * 255 / 31);
    colors[0][1] = (BYTE)(((c0 >> 5) & 0x3F) * 255 / 63);
    colors[0][2] = (BYTE)((c0 & 0x1F) * 255 / 31);
    colors[0][3] = 255;
    colors[1][0] = (BYTE)(((c1 >> 11) & 0x1F) * 255 / 31);
    colors[1][1] = (BYTE)(((c1 >> 5) & 0x3F) * 255 / 63);
    colors[1][2] = (BYTE)((c1 & 0x1F) * 255 / 31);
    colors[1][3] = 255;

    if (c0 > c1 || hasAlpha) {
        colors[2][0] = (BYTE)((2 * colors[0][0] + colors[1][0] + 1) / 3);
        colors[2][1] = (BYTE)((2 * colors[0][1] + colors[1][1] + 1) / 3);
        colors[2][2] = (BYTE)((2 * colors[0][2] + colors[1][2] + 1) / 3);
        colors[2][3] = 255;
        colors[3][0] = (BYTE)((colors[0][0] + 2 * colors[1][0] + 1) / 3);
        colors[3][1] = (BYTE)((colors[0][1] + 2 * colors[1][1] + 1) / 3);
        colors[3][2] = (BYTE)((colors[0][2] + 2 * colors[1][2] + 1) / 3);
        colors[3][3] = 255;
    } else {
        colors[2][0] = (BYTE)((colors[0][0] + colors[1][0]) / 2);
        colors[2][1] = (BYTE)((colors[0][1] + colors[1][1]) / 2);
        colors[2][2] = (BYTE)((colors[0][2] + colors[1][2]) / 2);
        colors[2][3] = 255;
        colors[3][0] = 0; colors[3][1] = 0; colors[3][2] = 0; colors[3][3] = 0; // transparent
    }

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = (bits >> (2 * (y * 4 + x))) & 3;
            BYTE* dst = outRGBA + y * stride + x * 4;
            dst[0] = colors[idx][0]; // R
            dst[1] = colors[idx][1]; // G
            dst[2] = colors[idx][2]; // B
            dst[3] = colors[idx][3]; // A
        }
    }
}

inline void DXT_DecompressDXT1(const BYTE* src, BYTE* dst, DWORD w, DWORD h, int srcSize) {
    const BYTE* srcEnd = src + srcSize;
    for (DWORD by = 0; by < h; by += 4) {
        for (DWORD bx = 0; bx < w; bx += 4) {
            if (src + 8 > srcEnd) return; // bounds check
            BYTE block[4*4*4]; // 4x4 block, RGBA
            DXT_DecodeColorBlock(src, block, 16, false);
            src += 8;
            for (int y = 0; y < 4 && (by+y) < h; y++) {
                for (int x = 0; x < 4 && (bx+x) < w; x++) {
                    BYTE* d = dst + ((by+y)*w + (bx+x)) * 4;
                    BYTE* s = block + (y*4+x) * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
        }
    }
}

inline void DXT_DecompressDXT3(const BYTE* src, BYTE* dst, DWORD w, DWORD h, int srcSize) {
    const BYTE* srcEnd = src + srcSize;
    for (DWORD by = 0; by < h; by += 4) {
        for (DWORD bx = 0; bx < w; bx += 4) {
            if (src + 16 > srcEnd) return; // bounds check (8 alpha + 8 color)
            const BYTE* alphaBlock = src;
            src += 8;
            BYTE block[4*4*4];
            DXT_DecodeColorBlock(src, block, 16, true);
            src += 8;
            for (int y = 0; y < 4; y++) {
                WORD alphaRow = *(WORD*)(alphaBlock + y * 2);
                for (int x = 0; x < 4; x++) {
                    int a4 = (alphaRow >> (x * 4)) & 0xF;
                    block[(y*4+x)*4+3] = (BYTE)(a4 * 255 / 15);
                }
            }
            for (int y = 0; y < 4 && (by+y) < h; y++) {
                for (int x = 0; x < 4 && (bx+x) < w; x++) {
                    BYTE* d = dst + ((by+y)*w + (bx+x)) * 4;
                    BYTE* s = block + (y*4+x) * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
        }
    }
}

inline void DXT_DecompressDXT5(const BYTE* src, BYTE* dst, DWORD w, DWORD h, int srcSize) {
    const BYTE* srcEnd = src + srcSize;
    for (DWORD by = 0; by < h; by += 4) {
        for (DWORD bx = 0; bx < w; bx += 4) {
            if (src + 16 > srcEnd) return; // bounds check (8 alpha + 8 color)
            BYTE a0 = src[0], a1 = src[1];
            BYTE alphas[8];
            alphas[0] = a0; alphas[1] = a1;
            if (a0 > a1) {
                for (int i = 0; i < 6; i++) alphas[i+2] = (BYTE)(((6-i)*a0 + (i+1)*a1) / 7);
            } else {
                for (int i = 0; i < 4; i++) alphas[i+2] = (BYTE)(((4-i)*a0 + (i+1)*a1) / 5);
                alphas[6] = 0; alphas[7] = 255;
            }
            BYTE alphaIdx[16];
            {
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= ((uint64_t)src[2+i]) << (8*i);
                for (int i = 0; i < 16; i++) alphaIdx[i] = (bits >> (3*i)) & 7;
            }
            src += 8;

            BYTE block[4*4*4];
            DXT_DecodeColorBlock(src, block, 16, true);
            src += 8;

            for (int i = 0; i < 16; i++) block[i*4+3] = alphas[alphaIdx[i]];

            for (int y = 0; y < 4 && (by+y) < h; y++) {
                for (int x = 0; x < 4 && (bx+x) < w; x++) {
                    BYTE* d = dst + ((by+y)*w + (bx+x)) * 4;
                    BYTE* s = block + (y*4+x) * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
        }
    }
}

// -------------------------------------------------------
// Main XBX parsing function
// Takes raw XBX file data, returns IDirect3DTexture8 with SDL texture
// -------------------------------------------------------
inline IDirect3DTexture8* XBX_ParseTexture(const BYTE* pbContent, int cbContent) {
    if (!pbContent || cbContent < (int)sizeof(XPR_HEADER)) return NULL;

    const XPR_HEADER* pxprh = (const XPR_HEADER*)pbContent;
    if (pxprh->dwMagic != XPR_MAGIC_VALUE) {
        fprintf(stderr, "[XBX] Bad magic: 0x%08X (expected 0x%08X)\n", pxprh->dwMagic, XPR_MAGIC_VALUE);
        return NULL;
    }

    int cbHeaders = pxprh->dwHeaderSize - sizeof(XPR_HEADER);
    int cbData = pxprh->dwTotalSize - pxprh->dwHeaderSize;

    // Match Xbox ParseTexture validation (Image.cpp:88-110)
    if (cbHeaders < 16 || cbData <= 0 || (UINT)cbContent < pxprh->dwHeaderSize ||
        (UINT)cbContent < pxprh->dwHeaderSize + (UINT)cbData) {
        fprintf(stderr, "[XBX] Invalid sizes: totalSize=%u headerSize=%u cbContent=%d cbHeaders=%d cbData=%d\n",
            pxprh->dwTotalSize, pxprh->dwHeaderSize, cbContent, cbHeaders, cbData);
        return NULL;
    }

    // Validate it's a texture resource
    const DWORD* pResource = (const DWORD*)(pbContent + sizeof(XPR_HEADER));
    DWORD common = pResource[0];
    if ((common & XBOX_D3DCOMMON_TYPE_MASK) != XBOX_D3DCOMMON_TYPE_TEXTURE) {
        fprintf(stderr, "[XBX] Not a texture resource (Common=0x%08X)\n", common);
        return NULL;
    }

    // Extract format and dimensions from Format DWORD
    // D3DPixelContainer layout: Common(+0), Data(+4), Lock(+8), Format(+12), Size(+16)
    DWORD formatDW = pResource[3]; // Format field
    DWORD sizeDW = pResource[4];   // Size field

    DWORD xboxFmt = (formatDW & XBOX_D3DFORMAT_FORMAT_MASK) >> XBOX_D3DFORMAT_FORMAT_SHIFT;

    // Dimension extraction
    static const DWORD exptbl[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    DWORD width, height;

    if (sizeDW != 0) {
        // Non-power-of-2 or linear: dimensions stored in Size field
        width  = (sizeDW & XBOX_D3DSIZE_WIDTH_MASK) + 1;
        height = ((sizeDW & XBOX_D3DSIZE_HEIGHT_MASK) >> XBOX_D3DSIZE_HEIGHT_SHIFT) + 1;
    } else {
        // Power-of-2 swizzled: dimensions encoded as log2 in Format
        DWORD uIdx = (formatDW & XBOX_D3DFORMAT_USIZE_MASK) >> XBOX_D3DFORMAT_USIZE_SHIFT;
        DWORD vIdx = (formatDW & XBOX_D3DFORMAT_VSIZE_MASK) >> XBOX_D3DFORMAT_VSIZE_SHIFT;
        width  = (uIdx < 13) ? exptbl[uIdx] : 1;
        height = (vIdx < 13) ? exptbl[vIdx] : 1;
    }


    const BYTE* srcData = pbContent + pxprh->dwHeaderSize;

    // Allocate RGBA output buffer
    BYTE* rgba = (BYTE*)calloc(width * height, 4);
    if (!rgba) return NULL;

    if (XboxFormatIsDXT(xboxFmt)) {
        // DXT compressed: not swizzled, just decompress.
        if (xboxFmt == XBOX_D3DFMT_DXT1)
            DXT_DecompressDXT1(srcData, rgba, width, height, cbData);
        else if (xboxFmt == XBOX_D3DFMT_DXT3)
            DXT_DecompressDXT3(srcData, rgba, width, height, cbData);
        else if (xboxFmt == XBOX_D3DFMT_DXT5)
            DXT_DecompressDXT5(srcData, rgba, width, height, cbData);
    }
    else if (XboxFormatIsLinear(xboxFmt)) {
        // Linear (not swizzled): convert pixel format with proper pitch.
        int bpp = XboxBytesPerPixel(xboxFmt);
        // Xbox NV2A aligns linear texture pitch to 64 bytes
        int srcPitch = (width * bpp + 63) & ~63;
        for (DWORD y = 0; y < height; y++) {
            for (DWORD x = 0; x < width; x++) {
                const BYTE* src = srcData + y * srcPitch + x * bpp;
                BYTE* dst = rgba + (y * width + x) * 4;
                switch (xboxFmt) {
                    case XBOX_D3DFMT_LIN_A8R8G8B8:
                        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3]; // ARGB→RGBA
                        break;
                    case XBOX_D3DFMT_LIN_X8R8G8B8:
                        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255;
                        break;
                    case XBOX_D3DFMT_LIN_R5G6B5: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 11) & 0x1F) * 255 / 31);
                        dst[1] = (BYTE)(((px >> 5) & 0x3F) * 255 / 63);
                        dst[2] = (BYTE)((px & 0x1F) * 255 / 31);
                        dst[3] = 255;
                        break;
                    }
                    case XBOX_D3DFMT_LIN_L8:
                        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255;
                        break;
                    case XBOX_D3DFMT_LIN_A8:
                        dst[0] = dst[1] = dst[2] = 255; dst[3] = src[0];
                        break;
                    case XBOX_D3DFMT_LIN_AL8:
                        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[0]; // luminance=alpha
                        break;
                    case XBOX_D3DFMT_LIN_A8L8: {
                        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]; // L=byte0, A=byte1
                        break;
                    }
                    case XBOX_D3DFMT_LIN_X1R5G5B5: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 10) & 0x1F) * 255 / 31);
                        dst[1] = (BYTE)(((px >> 5) & 0x1F) * 255 / 31);
                        dst[2] = (BYTE)((px & 0x1F) * 255 / 31);
                        dst[3] = 255;
                        break;
                    }
                    case XBOX_D3DFMT_LIN_A4R4G4B4: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 8) & 0xF) * 255 / 15);
                        dst[1] = (BYTE)(((px >> 4) & 0xF) * 255 / 15);
                        dst[2] = (BYTE)((px & 0xF) * 255 / 15);
                        dst[3] = (BYTE)(((px >> 12) & 0xF) * 255 / 15);
                        break;
                    }
                    default:
                        dst[0] = dst[1] = dst[2] = 128; dst[3] = 255; // fallback gray
                        break;
                }
            }
        }
    }
    else {
        // Swizzled: unswizzle then convert.
        int bpp = XboxBytesPerPixel(xboxFmt);
        XboxSwizzler swiz;
        swiz.Init(width, height);

        for (DWORD sy = 0; sy < height; sy++) {
            for (DWORD sx = 0; sx < width; sx++) {
                // Calculate swizzled index for this (sx, sy) coordinate
                // Use the Swizzler algorithm from XGraphics.h
                DWORD swizIdx = 0;
                {
                    DWORD u = sx, v = sy;
                    DWORD maskU = swiz.m_MaskU, maskV = swiz.m_MaskV;
                    DWORD su = 0, sv = 0;
                    for (DWORD bit = 1; bit <= (maskU | maskV); bit <<= 1) {
                        if (maskU & bit) { if (u & 1) su |= bit; u >>= 1; }
                        if (maskV & bit) { if (v & 1) sv |= bit; v >>= 1; }
                    }
                    swizIdx = su | sv;
                }

                if (swizIdx * bpp + bpp > (DWORD)cbData) continue; // bounds check

                const BYTE* src = srcData + swizIdx * bpp;
                BYTE* dst = rgba + (sy * width + sx) * 4;

                switch (xboxFmt) {
                    case XBOX_D3DFMT_A8R8G8B8:
                        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
                        break;
                    case XBOX_D3DFMT_X8R8G8B8:
                        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255;
                        break;
                    case XBOX_D3DFMT_R5G6B5: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 11) & 0x1F) * 255 / 31);
                        dst[1] = (BYTE)(((px >> 5) & 0x3F) * 255 / 63);
                        dst[2] = (BYTE)((px & 0x1F) * 255 / 31);
                        dst[3] = 255;
                        break;
                    }
                    case XBOX_D3DFMT_A4R4G4B4: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 8) & 0xF) * 255 / 15);
                        dst[1] = (BYTE)(((px >> 4) & 0xF) * 255 / 15);
                        dst[2] = (BYTE)((px & 0xF) * 255 / 15);
                        dst[3] = (BYTE)(((px >> 12) & 0xF) * 255 / 15);
                        break;
                    }
                    case XBOX_D3DFMT_A1R5G5B5: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 10) & 0x1F) * 255 / 31);
                        dst[1] = (BYTE)(((px >> 5) & 0x1F) * 255 / 31);
                        dst[2] = (BYTE)((px & 0x1F) * 255 / 31);
                        dst[3] = (px & 0x8000) ? 255 : 0;
                        break;
                    }
                    case XBOX_D3DFMT_L8:
                        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255;
                        break;
                    case XBOX_D3DFMT_P8:
                        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255; // no palette, just grayscale
                        break;
                    case XBOX_D3DFMT_AL8:
                        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[0]; // luminance=alpha
                        break;
                    case XBOX_D3DFMT_X1R5G5B5: {
                        WORD px = *(WORD*)src;
                        dst[0] = (BYTE)(((px >> 10) & 0x1F) * 255 / 31);
                        dst[1] = (BYTE)(((px >> 5) & 0x1F) * 255 / 31);
                        dst[2] = (BYTE)((px & 0x1F) * 255 / 31);
                        dst[3] = 255;
                        break;
                    }
                    default:
                        dst[0] = dst[1] = dst[2] = 128; dst[3] = 255;
                        break;
                }
            }
        }
    }

    // Create SDL texture from RGBA data
    IDirect3DTexture8* pTexture = new IDirect3DTexture8();
    if (!pTexture->CreateFromRGBA(width, height, rgba)) {
        fprintf(stderr, "[XBX] Failed to create SDL texture\n");
        free(rgba);
        pTexture->Release();
        return NULL;
    }

    free(rgba);
    return pTexture;
}
