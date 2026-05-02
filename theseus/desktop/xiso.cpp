// xiso.cpp: XISO reader. Extracts default.xbe from Xbox ISO images;
// parses the XISO directory table and reads the XBE header /
// certificate / $$XTIMAGE bitmap. Used by the desktop title scanner.

#include "xiso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _MSC_VER
#define fseeko _fseeki64
typedef __int64 off_t;
#endif

// stb_image_write for JPEG output
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---------------------------------------------------------------------------
// XBE structures (from xbe.h; only what we need for parsing)
// ---------------------------------------------------------------------------

#define XBE_HEADER_MAGIC  0x48454258  // "XBEH"
#define XPR_HEADER_MAGIC  0x30525058  // "XPR0"

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint8_t  digsig[256];
    uint32_t base;
    uint32_t sizeof_headers;
    uint32_t sizeof_image;
    uint32_t sizeof_image_header;     // offset to certificate
    uint32_t timedate;
    uint32_t certificate_addr;
    uint32_t sections;
    uint32_t section_headers_addr;
    // ... rest of header not needed
} XbeHeader;

typedef struct {
    uint32_t size;
    uint32_t timedate;
    uint32_t titleid;
    uint16_t title_name[40];          // unicode title name
    uint32_t alt_title_id[16];
    uint32_t allowed_media;
    uint32_t game_region;
    uint32_t game_ratings;
    uint32_t disk_number;
    uint32_t version;
    uint8_t  lan_key[16];
    uint8_t  sig_key[16];
    uint8_t  title_alt_sig_key[16][16];
} XbeCertificate;

typedef struct {
    uint32_t flags;
    uint32_t virtual_addr;
    uint32_t virtual_size;
    uint32_t raw_addr;
    uint32_t sizeof_raw;
    uint32_t section_name_addr;
    uint32_t section_reference_count;
    uint32_t head_shared_ref_count_addr;  // pointer on Xbox, uint32 in file
    uint32_t tail_shared_ref_count_addr;
    uint8_t  section_digest[20];
} XbeSectionHeader;

typedef struct {
    uint32_t dwXPRMagic;
    uint32_t dwTotalSize;
    uint32_t dwHeaderSize;
    uint32_t dwTextureCommon;
    uint32_t dwTextureData;
    uint32_t dwTextureLock;
    uint8_t  btTextureMisc1;
    uint8_t  btTextureFormat;
    uint8_t  btTextureLevelWidth;  // level:4, width:4
    uint8_t  btTextureHeightMisc;  // height:4, misc2:4
    uint32_t dwTextureSize;
    uint32_t dwEndOfHeader;
} XprFileHeader;

#pragma pack(pop) // before_xiso

// ---------------------------------------------------------------------------
// XISO filesystem constants
// ---------------------------------------------------------------------------

#define XISO_SECTOR_SIZE      2048
#define XISO_VOLUME_SECTOR    32        // volume descriptor at sector 32 (byte offset 0x10000)

// XISO volume descriptor is at sector 32 (offset 0x10000)
// The root directory table location is at offset 20 in the volume descriptor

typedef struct {
    uint32_t sector;    // starting sector of file/directory
    uint32_t size;      // size in bytes
    uint8_t  attrib;
    uint8_t  nameLen;
    // followed by nameLen bytes of filename
} XisoDirEntry;

// ---------------------------------------------------------------------------
// DXT1 decoder (4x4 block → RGBA)
// ---------------------------------------------------------------------------

static void DecodeDXT1Block(const uint8_t* block, uint8_t* outRGBA, int stride)
{
    uint16_t c0 = block[0] | (block[1] << 8);
    uint16_t c1 = block[2] | (block[3] << 8);
    uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

    // Decode 565 colors
    uint8_t colors[4][4]; // [index][R,G,B,A]
    colors[0][0] = ((c0 >> 11) & 0x1F) * 255 / 31;
    colors[0][1] = ((c0 >> 5)  & 0x3F) * 255 / 63;
    colors[0][2] = ((c0)       & 0x1F) * 255 / 31;
    colors[0][3] = 255;

    colors[1][0] = ((c1 >> 11) & 0x1F) * 255 / 31;
    colors[1][1] = ((c1 >> 5)  & 0x3F) * 255 / 63;
    colors[1][2] = ((c1)       & 0x1F) * 255 / 31;
    colors[1][3] = 255;

    if (c0 > c1) {
        colors[2][0] = (2 * colors[0][0] + colors[1][0]) / 3;
        colors[2][1] = (2 * colors[0][1] + colors[1][1]) / 3;
        colors[2][2] = (2 * colors[0][2] + colors[1][2]) / 3;
        colors[2][3] = 255;
        colors[3][0] = (colors[0][0] + 2 * colors[1][0]) / 3;
        colors[3][1] = (colors[0][1] + 2 * colors[1][1]) / 3;
        colors[3][2] = (colors[0][2] + 2 * colors[1][2]) / 3;
        colors[3][3] = 255;
    } else {
        colors[2][0] = (colors[0][0] + colors[1][0]) / 2;
        colors[2][1] = (colors[0][1] + colors[1][1]) / 2;
        colors[2][2] = (colors[0][2] + colors[1][2]) / 2;
        colors[2][3] = 255;
        colors[3][0] = 0;
        colors[3][1] = 0;
        colors[3][2] = 0;
        colors[3][3] = 0; // transparent
    }

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = (bits >> (2 * (y * 4 + x))) & 0x3;
            uint8_t* px = outRGBA + y * stride + x * 4;
            px[0] = colors[idx][0];
            px[1] = colors[idx][1];
            px[2] = colors[idx][2];
            px[3] = colors[idx][3];
        }
    }
}

static uint8_t* DecodeDXT1(const uint8_t* dxtData, int width, int height)
{
    uint8_t* rgba = (uint8_t*)calloc(width * height, 4);
    if (!rgba) return NULL;

    int blocksX = width / 4;
    int blocksY = height / 4;
    int stride = width * 4;

    for (int by = 0; by < blocksY; by++) {
        for (int bx = 0; bx < blocksX; bx++) {
            const uint8_t* block = dxtData + (by * blocksX + bx) * 8;
            uint8_t* dest = rgba + (by * 4) * stride + (bx * 4) * 4;
            DecodeDXT1Block(block, dest, stride);
        }
    }

    return rgba;
}

// ---------------------------------------------------------------------------
// XISO directory reader
// ---------------------------------------------------------------------------

// Read bytes from ISO at a given byte offset
static bool IsoRead(FILE* fp, uint64_t offset, void* buf, size_t size)
{
    if (fseeko(fp, (off_t)offset, SEEK_SET) != 0) return false;
    return fread(buf, 1, size, fp) == size;
}

// Find "default.xbe" in an XISO directory table
// Returns the file's start sector and size, or false if not found
static bool FindDefaultXbe(FILE* fp, uint64_t dirOffset, uint32_t dirSize,
                           uint64_t* outFileOffset, uint32_t* outFileSize)
{
    uint8_t* dirBuf = (uint8_t*)malloc(dirSize);
    if (!dirBuf) return false;

    if (!IsoRead(fp, dirOffset, dirBuf, dirSize)) {
        free(dirBuf);
        return false;
    }

    uint32_t pos = 0;
    while (pos + 14 <= dirSize) {
        // Directory entry layout:
        // [0..1]  uint16 left subtree offset (in 4-byte units, 0xFFFF = none)
        // [2..3]  uint16 right subtree offset
        // [4..7]  uint32 sector
        // [8..11] uint32 size
        // [12]    uint8  attributes
        // [13]    uint8  name length
        // [14..]  name bytes

        uint16_t leftOfs  = dirBuf[pos]     | (dirBuf[pos + 1] << 8);
        uint16_t rightOfs = dirBuf[pos + 2] | (dirBuf[pos + 3] << 8);
        uint32_t sector   = dirBuf[pos + 4] | (dirBuf[pos + 5] << 8) |
                           (dirBuf[pos + 6] << 16) | (dirBuf[pos + 7] << 24);
        uint32_t size     = dirBuf[pos + 8] | (dirBuf[pos + 9] << 8) |
                           (dirBuf[pos + 10] << 16) | (dirBuf[pos + 11] << 24);
        uint8_t  attrib   = dirBuf[pos + 12];
        uint8_t  nameLen  = dirBuf[pos + 13];
        (void)attrib;

        if (nameLen == 0 || pos + 14 + nameLen > dirSize) break;

        char name[256];
        memcpy(name, dirBuf + pos + 14, nameLen);
        name[nameLen] = '\0';

        // Case-insensitive compare
        if (nameLen == 11) {
            char lower[12];
            for (int i = 0; i < 11; i++) lower[i] = (char)tolower((unsigned char)name[i]);
            lower[11] = '\0';
            if (strcmp(lower, "default.xbe") == 0) {
                *outFileOffset = (uint64_t)sector * XISO_SECTOR_SIZE;
                *outFileSize = size;
                free(dirBuf);
                return true;
            }
        }

        // Binary tree traversal; try left then right subtree
        // But for simplicity, just scan linearly since XISO dirs are usually small
        // Advance to next entry (entries are variable-length, 4-byte aligned)
        uint32_t entrySize = 14 + nameLen;
        entrySize = (entrySize + 3) & ~3; // align to 4 bytes
        pos += entrySize;

        // If we hit padding (all zeros), skip
        while (pos + 14 <= dirSize && dirBuf[pos] == 0 && dirBuf[pos + 1] == 0 &&
               dirBuf[pos + 2] == 0 && dirBuf[pos + 3] == 0 && dirBuf[pos + 4] == 0) {
            pos += 4;
        }
    }

    free(dirBuf);
    return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

XisoTitleInfo XisoGetTitleInfo(const char* isoPath)
{
    XisoTitleInfo info;
    memset(&info, 0, sizeof(info));

    FILE* fp = fopen(isoPath, "rb");
    if (!fp) return info;

    // Volume descriptor at sector 32 (byte offset 0x10000)
    uint64_t volDescOffset = (uint64_t)XISO_VOLUME_SECTOR * XISO_SECTOR_SIZE;
    uint8_t volDesc[2048];
    uint32_t rootDirSector = 0, rootDirSize = 0;
    bool foundRoot = false;

    if (IsoRead(fp, volDescOffset, volDesc, sizeof(volDesc)) &&
        memcmp(volDesc, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
        rootDirSector = volDesc[20] | (volDesc[21] << 8) |
                       (volDesc[22] << 16) | (volDesc[23] << 24);
        rootDirSize   = volDesc[24] | (volDesc[25] << 8) |
                       (volDesc[26] << 16) | (volDesc[27] << 24);
        foundRoot = true;
    }

    if (!foundRoot) {
        fclose(fp);
        return info;
    }

    // Find default.xbe in root directory
    uint64_t xbeOffset = 0;
    uint32_t xbeSize = 0;
    uint64_t rootDirOffset = (uint64_t)rootDirSector * XISO_SECTOR_SIZE;

    if (!FindDefaultXbe(fp, rootDirOffset, rootDirSize, &xbeOffset, &xbeSize)) {
        fclose(fp);
        return info;
    }

    // Read XBE header (first 256KB should be plenty)
    uint32_t headerReadSize = 256 * 1024;
    if (headerReadSize > xbeSize) headerReadSize = xbeSize;

    uint8_t* xbeData = (uint8_t*)malloc(headerReadSize);
    if (!xbeData) { fclose(fp); return info; }

    if (!IsoRead(fp, xbeOffset, xbeData, headerReadSize)) {
        free(xbeData);
        fclose(fp);
        return info;
    }

    // Validate XBE magic
    XbeHeader* hdr = (XbeHeader*)xbeData;
    if (hdr->magic != XBE_HEADER_MAGIC) {
        free(xbeData);
        fclose(fp);
        return info;
    }

    // Read certificate (at offset sizeof_image_header from start of XBE data)
    uint32_t certOffset = hdr->sizeof_image_header;
    if (certOffset + sizeof(XbeCertificate) > headerReadSize) {
        free(xbeData);
        fclose(fp);
        return info;
    }

    XbeCertificate* cert = (XbeCertificate*)(xbeData + certOffset);
    info.titleId = cert->titleid;

    // Convert unicode title name to ASCII
    for (int i = 0; i < 40 && cert->title_name[i]; i++) {
        uint16_t wc = cert->title_name[i];
        info.titleName[i] = (wc < 128) ? (char)wc : '?';
    }
    info.titleName[127] = '\0';

    // Trim trailing spaces
    int len = (int)strlen(info.titleName);
    while (len > 0 && info.titleName[len - 1] == ' ') info.titleName[--len] = '\0';

    // Find $$XTIMAGE section
    uint32_t numSections = hdr->sections;
    uint32_t sectionHdrOffset = hdr->section_headers_addr - hdr->base;

    if (sectionHdrOffset + numSections * sizeof(XbeSectionHeader) <= headerReadSize) {
        XbeSectionHeader* sections = (XbeSectionHeader*)(xbeData + sectionHdrOffset);

        for (uint32_t i = 0; i < numSections; i++) {
            // Check inserted_file flag (bit 3 of flags)
            if (!(sections[i].flags & 0x08)) continue;

            // Get section name
            uint32_t nameOffset = sections[i].section_name_addr - hdr->base;
            if (nameOffset >= headerReadSize) continue;

            const char* sectionName = (const char*)(xbeData + nameOffset);
            if (strcmp(sectionName, "$$XTIMAGE") != 0) continue;

            // Read the title image from the ISO
            uint32_t imgRawAddr = sections[i].raw_addr;
            uint32_t imgRawSize = sections[i].sizeof_raw;

            // Image is at xbeOffset + imgRawAddr in the ISO
            uint8_t* imgData = (uint8_t*)malloc(imgRawSize);
            if (!imgData) break;

            if (!IsoRead(fp, xbeOffset + imgRawAddr, imgData, imgRawSize)) {
                free(imgData);
                break;
            }

            // Parse XPR header
            if (imgRawSize >= sizeof(XprFileHeader)) {
                XprFileHeader* xpr = (XprFileHeader*)imgData;

                if (xpr->dwXPRMagic == XPR_HEADER_MAGIC) {
                    // DXT1 data starts after the XPR header
                    uint32_t dxtOffset = xpr->dwHeaderSize;
                    uint32_t dxtSize = imgRawSize - dxtOffset;

                    // Title images are 128x128 DXT1 = 8192 bytes
                    int width = 128, height = 128;
                    uint32_t expectedDxtSize = (width / 4) * (height / 4) * 8;

                    if (dxtSize >= expectedDxtSize) {
                        // Try direct decode first (most title images are linear)
                        info.titleImageRGBA = DecodeDXT1(imgData + dxtOffset, width, height);

                        if (info.titleImageRGBA) {
                            info.titleImageWidth = width;
                            info.titleImageHeight = height;
                        }
                    }
                }
            }

            free(imgData);
            break;
        }
    }

    free(xbeData);
    fclose(fp);

    info.valid = true;
    return info;
}

bool XisoSaveTitleImage(const XisoTitleInfo* info, const char* outputPath)
{
    if (!info || !info->titleImageRGBA || info->titleImageWidth == 0) return false;

    // Check extension for format
    const char* ext = strrchr(outputPath, '.');
    if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0)) {
        return stbi_write_png(outputPath, info->titleImageWidth, info->titleImageHeight,
                             4, info->titleImageRGBA, info->titleImageWidth * 4) != 0;
    }

    // Default to JPEG
    return stbi_write_jpg(outputPath, info->titleImageWidth, info->titleImageHeight,
                         4, info->titleImageRGBA, 90) != 0;
}

void XisoFreeTitleInfo(XisoTitleInfo* info)
{
    if (info && info->titleImageRGBA) {
        free(info->titleImageRGBA);
        info->titleImageRGBA = NULL;
    }
}
