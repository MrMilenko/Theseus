#include "icon_compress.h"
#include "virtual_games.h"   // VGAMES_ICONS

// Both implementations already live elsewhere in the build (stb_image in
// sdl_main.cpp, stb_image_write in xiso.cpp), so take declarations only.
#include "stb_image.h"
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "host.h"

static size_t FileSize(const char* path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? (size_t)st.st_size : 0;
}

// Box filter. Covers drop by 4x or more, so bilinear would alias badly.
static void Downscale(const unsigned char* src, int sw, int sh,
                      unsigned char* dst, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
            if (x1 <= x0) x1 = x0 + 1;
            int r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++) {
                const unsigned char* row = src + (size_t)sy * sw * 3;
                for (int sx = x0; sx < x1; sx++) {
                    r += row[sx * 3 + 0];
                    g += row[sx * 3 + 1];
                    b += row[sx * 3 + 2];
                    n++;
                }
            }
            unsigned char* o = dst + ((size_t)y * dw + x) * 3;
            o[0] = (unsigned char)(r / n);
            o[1] = (unsigned char)(g / n);
            o[2] = (unsigned char)(b / n);
        }
    }
}

static bool CompressOne(const char* dir, const char* name, int maxDim, int quality,
                        size_t* before, size_t* after)
{
    const char* ext = strrchr(name, '.');
    if (!ext) return false;
    bool isPng = strcasecmp(ext, ".png") == 0;
    if (!isPng && strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".jpeg") != 0)
        return false;

    char src[1024];
    snprintf(src, sizeof(src), "%s/%s", dir, name);
    size_t sizeBefore = FileSize(src);

    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(src, &w, &h, &ch, 3);
    if (!pixels || w <= 0 || h <= 0) { if (pixels) stbi_image_free(pixels); return false; }

    int big = (w > h) ? w : h;
    if (big <= maxDim && !isPng) {
        // Already small and already a jpeg: nothing to gain.
        stbi_image_free(pixels);
        *before += sizeBefore;
        *after  += sizeBefore;
        return false;
    }

    int dw = w, dh = h;
    unsigned char* small = pixels;
    if (big > maxDim) {
        dw = (w * maxDim + big - 1) / big;
        dh = (h * maxDim + big - 1) / big;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        small = (unsigned char*)malloc((size_t)dw * dh * 3);
        if (!small) { stbi_image_free(pixels); return false; }
        Downscale(pixels, w, h, small, dw, dh);
    }

    // Always land on .jpg so the icon redirect finds it on its first probe.
    char stem[1024];
    snprintf(stem, sizeof(stem), "%s", name);
    char* dot = strrchr(stem, '.');
    if (dot) *dot = '\0';

    char dst[1024], tmp[1024];
    snprintf(dst, sizeof(dst), "%s/%s.jpg", dir, stem);
    snprintf(tmp, sizeof(tmp), "%s/%s.jpg.tmp", dir, stem);

    int ok = stbi_write_jpg(tmp, dw, dh, 3, small, quality);
    if (small != pixels) free(small);
    stbi_image_free(pixels);
    if (!ok) { remove(tmp); return false; }

    remove(dst);
    if (rename(tmp, dst) != 0) { remove(tmp); return false; }
    // A .png source has been replaced by the .jpg, so drop the original.
    if (isPng) remove(src);

    *before += sizeBefore;
    *after  += FileSize(dst);
    return true;
}

IconCompressResult Icons_Compress(int maxDim, int quality)
{
    IconCompressResult r;
    memset(&r, 0, sizeof(r));
    if (maxDim < 32)   maxDim = 32;
    if (quality < 30)  quality = 30;
    if (quality > 100) quality = 100;

    const char* dir = VGAMES_ICONS;

    // Snapshot the listing first, since the loop deletes and renames.
    static char names[8192][256];
    int count = 0;

    HostDir* d = Host_OpenDir(dir);
    if (d) {
        char name[256];
        while (Host_ReadDir(d, name, sizeof(name), NULL)) {
            if (name[0] == '.') continue;
            if (count < 8192) snprintf(names[count++], 256, "%s", name);
        }
        Host_CloseDir(d);
    }

    for (int i = 0; i < count; i++) {
        r.scanned++;
        if (CompressOne(dir, names[i], maxDim, quality, &r.bytesBefore, &r.bytesAfter))
            r.converted++;
    }
    return r;
}
