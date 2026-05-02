// xiso.c: Xbox ISO parser implementation.
//
// Reads an XISO via the Win32 file API (so 64-bit offsets work on Xbox
// where the CRT only ships 32-bit fseek), walks the directory table to
// find default.xbe, parses the XBE header and certificate to pull out
// title id and title name, and decodes the 128x128 DXT1 image embedded
// in the $$XTIMAGE section.
//
// XBE struct layouts here are deliberately minimal (only the fields the
// parser actually touches) and intentionally not pulled from
// theseus/shared/xbe.h. That header is C++ with nested structs and
// bitfields and pulling it into a C lib would entangle this migration
// with a much larger refactor.

#include <xtl.h>

#include "xiso.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// =============================================================================
// XBE structures (only what the parser needs to touch)
// =============================================================================

#define XBE_HEADER_MAGIC  0x48454258u  // 'XBEH'
#define XPR_HEADER_MAGIC  0x30525058u  // 'XPR0'

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint8_t  digsig[256];
    uint32_t base;
    uint32_t sizeof_headers;
    uint32_t sizeof_image;
    uint32_t sizeof_image_header;     // offset of certificate from start of XBE
    uint32_t timedate;
    uint32_t certificate_addr;
    uint32_t sections;
    uint32_t section_headers_addr;
    // remaining header fields are not consumed by this parser
} XbeHeaderRaw;

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
} XbeCertificateRaw;

typedef struct {
    uint32_t flags;
    uint32_t virtual_addr;
    uint32_t virtual_size;
    uint32_t raw_addr;
    uint32_t sizeof_raw;
    uint32_t section_name_addr;
    uint32_t section_reference_count;
    uint32_t head_shared_ref_count_addr;  // pointer on Xbox, plain dword in file
    uint32_t tail_shared_ref_count_addr;
    uint8_t  section_digest[20];
} XbeSectionHeaderRaw;

typedef struct {
    uint32_t dwXPRMagic;
    uint32_t dwTotalSize;
    uint32_t dwHeaderSize;
    uint32_t dwTextureCommon;
    uint32_t dwTextureData;
    uint32_t dwTextureLock;
    uint8_t  btTextureMisc1;
    uint8_t  btTextureFormat;
    uint8_t  btTextureLevelWidth;     // level:4, width:4
    uint8_t  btTextureHeightMisc;     // height:4, misc2:4
    uint32_t dwTextureSize;
    uint32_t dwEndOfHeader;
} XprFileHeaderRaw;

#pragma pack(pop)

// XBE section flag bit indicating an inserted (data-only) section
// like $$XTIMAGE / $$XSIMAGE.
#define XBE_SECTION_FLAG_INSERTED  0x00000008u

// XISO filesystem layout: the volume descriptor lives at sector 32
// (byte offset 0x10000) and contains the root directory's starting
// sector and length.
#define XISO_SECTOR_SIZE    2048u
#define XISO_VOLUME_SECTOR  32u

// Title image is fixed-size 128x128 DXT1 in every XBE.
#define TITLE_IMAGE_WIDTH   128
#define TITLE_IMAGE_HEIGHT  128
#define DXT1_BLOCK_BYTES    8

// =============================================================================
// 64-bit Win32 read-at-offset
// =============================================================================

// Read `size` bytes from `h` starting at `offset`. Uses SetFilePointer's
// high-dword form so the offset is a real 64-bit position; XISOs
// routinely live above the 2 GB mark.
static BOOL XisoReadAt(HANDLE h, ULONGLONG offset, void* buf, DWORD size)
{
    LONG lowPart  = (LONG)(offset & 0xFFFFFFFFu);
    LONG highPart = (LONG)(offset >> 32);
    DWORD result = SetFilePointer(h, lowPart, &highPart, FILE_BEGIN);
    if (result == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        return FALSE;
    }
    DWORD bytesRead = 0;
    if (!ReadFile(h, buf, size, &bytesRead, NULL)) {
        return FALSE;
    }
    return bytesRead == size;
}

// =============================================================================
// DXT1 -> RGBA decode (one 4x4 block at a time, then a whole-image walk)
// =============================================================================

static void DecodeDXT1Block(const uint8_t* block, uint8_t* outRGBA, int stride)
{
    uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
    uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
    uint32_t bits = (uint32_t)block[4] |
                    ((uint32_t)block[5] << 8) |
                    ((uint32_t)block[6] << 16) |
                    ((uint32_t)block[7] << 24);

    // Expand the two RGB565 endpoint colors out to 8-bit RGBA.
    uint8_t colors[4][4];
    colors[0][0] = (uint8_t)(((c0 >> 11) & 0x1F) * 255 / 31);
    colors[0][1] = (uint8_t)(((c0 >> 5)  & 0x3F) * 255 / 63);
    colors[0][2] = (uint8_t)(( c0        & 0x1F) * 255 / 31);
    colors[0][3] = 255;

    colors[1][0] = (uint8_t)(((c1 >> 11) & 0x1F) * 255 / 31);
    colors[1][1] = (uint8_t)(((c1 >> 5)  & 0x3F) * 255 / 63);
    colors[1][2] = (uint8_t)(( c1        & 0x1F) * 255 / 31);
    colors[1][3] = 255;

    // Interpolated midpoints. The c0 > c1 branch produces two opaque
    // 1/3-2/3 mixes; the c0 <= c1 branch produces a single midpoint
    // and a transparent black.
    if (c0 > c1) {
        colors[2][0] = (uint8_t)((2 * colors[0][0] + colors[1][0]) / 3);
        colors[2][1] = (uint8_t)((2 * colors[0][1] + colors[1][1]) / 3);
        colors[2][2] = (uint8_t)((2 * colors[0][2] + colors[1][2]) / 3);
        colors[2][3] = 255;
        colors[3][0] = (uint8_t)((colors[0][0] + 2 * colors[1][0]) / 3);
        colors[3][1] = (uint8_t)((colors[0][1] + 2 * colors[1][1]) / 3);
        colors[3][2] = (uint8_t)((colors[0][2] + 2 * colors[1][2]) / 3);
        colors[3][3] = 255;
    } else {
        colors[2][0] = (uint8_t)((colors[0][0] + colors[1][0]) / 2);
        colors[2][1] = (uint8_t)((colors[0][1] + colors[1][1]) / 2);
        colors[2][2] = (uint8_t)((colors[0][2] + colors[1][2]) / 2);
        colors[2][3] = 255;
        colors[3][0] = 0;
        colors[3][1] = 0;
        colors[3][2] = 0;
        colors[3][3] = 0;
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
    uint8_t* rgba = (uint8_t*)calloc((size_t)(width * height), 4);
    if (!rgba) return NULL;

    int blocksX = width  / 4;
    int blocksY = height / 4;
    int stride  = width  * 4;

    for (int by = 0; by < blocksY; by++) {
        for (int bx = 0; bx < blocksX; bx++) {
            const uint8_t* block = dxtData + (by * blocksX + bx) * DXT1_BLOCK_BYTES;
            uint8_t*       dest  = rgba + (by * 4) * stride + (bx * 4) * 4;
            DecodeDXT1Block(block, dest, stride);
        }
    }
    return rgba;
}

// =============================================================================
// XISO directory walker
// =============================================================================

// Locate "default.xbe" inside an XISO directory table. Returns its byte
// offset and size on success. The directory table is a left/right
// binary tree with 4-byte aligned variable-length entries; rather than
// honoring the tree pointers we just walk linearly because XISO root
// dirs are small enough that the difference does not matter.
static BOOL FindDefaultXbe(HANDLE h,
                           ULONGLONG dirOffset,
                           DWORD     dirSize,
                           ULONGLONG* outFileOffset,
                           DWORD*     outFileSize)
{
    uint8_t* dirBuf = (uint8_t*)malloc(dirSize);
    if (!dirBuf) return FALSE;

    if (!XisoReadAt(h, dirOffset, dirBuf, dirSize)) {
        free(dirBuf);
        return FALSE;
    }

    DWORD pos = 0;
    while (pos + 14 <= dirSize) {
        // Directory entry layout:
        //   [0..1]  uint16  left subtree offset (4-byte units, 0xFFFF = none)
        //   [2..3]  uint16  right subtree offset
        //   [4..7]  uint32  starting sector
        //   [8..11] uint32  byte size
        //   [12]    uint8   attributes
        //   [13]    uint8   name length
        //   [14..]  ASCII filename
        DWORD sector =  (DWORD)dirBuf[pos + 4]        |
                       ((DWORD)dirBuf[pos + 5] << 8)  |
                       ((DWORD)dirBuf[pos + 6] << 16) |
                       ((DWORD)dirBuf[pos + 7] << 24);
        DWORD size   =  (DWORD)dirBuf[pos + 8]        |
                       ((DWORD)dirBuf[pos + 9] << 8)  |
                       ((DWORD)dirBuf[pos + 10] << 16)|
                       ((DWORD)dirBuf[pos + 11] << 24);
        uint8_t nameLen = dirBuf[pos + 13];

        if (nameLen == 0 || pos + 14 + nameLen > dirSize) break;

        if (nameLen == 11) {
            char lower[12];
            const uint8_t* name = dirBuf + pos + 14;
            for (int i = 0; i < 11; i++) {
                lower[i] = (char)tolower(name[i]);
            }
            lower[11] = '\0';
            if (strcmp(lower, "default.xbe") == 0) {
                *outFileOffset = (ULONGLONG)sector * XISO_SECTOR_SIZE;
                *outFileSize   = size;
                free(dirBuf);
                return TRUE;
            }
        }

        // Variable-length entries are aligned to 4 bytes.
        DWORD entrySize = 14u + nameLen;
        entrySize = (entrySize + 3u) & ~3u;
        pos += entrySize;

        // Skip any zero padding between entries.
        while (pos + 14 <= dirSize &&
               dirBuf[pos]     == 0 && dirBuf[pos + 1] == 0 &&
               dirBuf[pos + 2] == 0 && dirBuf[pos + 3] == 0 &&
               dirBuf[pos + 4] == 0)
        {
            pos += 4;
        }
    }

    free(dirBuf);
    return FALSE;
}

// =============================================================================
// XBE -> XisoTitleInfo extraction
// =============================================================================

// Pull title id, title name, and the decoded title image out of the
// XBE that lives at `xbeOffset` inside an open XISO. Writes into
// `info`; leaves titleImageRGBA NULL if the XBE has no $$XTIMAGE.
static void ExtractTitleFromXbe(HANDLE h,
                                ULONGLONG xbeOffset,
                                DWORD xbeSize,
                                XisoTitleInfo* info)
{
    // 256 KB is well over the size of any real XBE header + section
    // table + cert + inserted-file payloads we care about.
    DWORD headerReadSize = 256u * 1024u;
    if (headerReadSize > xbeSize) headerReadSize = xbeSize;

    uint8_t* xbeData = (uint8_t*)malloc(headerReadSize);
    if (!xbeData) return;

    if (!XisoReadAt(h, xbeOffset, xbeData, headerReadSize)) {
        free(xbeData);
        return;
    }

    XbeHeaderRaw* hdr = (XbeHeaderRaw*)xbeData;
    if (hdr->magic != XBE_HEADER_MAGIC) {
        free(xbeData);
        return;
    }

    // The certificate sits sizeof_image_header bytes into the XBE.
    DWORD certOffset = hdr->sizeof_image_header;
    if (certOffset + sizeof(XbeCertificateRaw) > headerReadSize) {
        free(xbeData);
        return;
    }

    XbeCertificateRaw* cert = (XbeCertificateRaw*)(xbeData + certOffset);
    info->titleId = cert->titleid;

    // The certificate's title name is UTF-16. We don't carry around a
    // wide-string runtime here so anything outside ASCII gets a '?'.
    int outIdx = 0;
    for (int i = 0; i < 40 && cert->title_name[i]; i++) {
        uint16_t wc = cert->title_name[i];
        info->titleName[outIdx++] = (wc < 128) ? (char)wc : '?';
    }
    info->titleName[outIdx] = '\0';
    info->titleName[sizeof(info->titleName) - 1] = '\0';

    // Trim trailing spaces from the title name.
    int len = (int)strlen(info->titleName);
    while (len > 0 && info->titleName[len - 1] == ' ') {
        info->titleName[--len] = '\0';
    }

    // Walk the section table looking for $$XTIMAGE.
    DWORD numSections      = hdr->sections;
    DWORD sectionHdrOffset = hdr->section_headers_addr - hdr->base;
    if (sectionHdrOffset + numSections * sizeof(XbeSectionHeaderRaw) > headerReadSize) {
        free(xbeData);
        return;
    }

    XbeSectionHeaderRaw* sections = (XbeSectionHeaderRaw*)(xbeData + sectionHdrOffset);
    for (DWORD i = 0; i < numSections; i++) {
        if (!(sections[i].flags & XBE_SECTION_FLAG_INSERTED)) continue;

        DWORD nameOffset = sections[i].section_name_addr - hdr->base;
        if (nameOffset >= headerReadSize) continue;

        const char* sectionName = (const char*)(xbeData + nameOffset);
        if (strcmp(sectionName, "$$XTIMAGE") != 0) continue;

        DWORD imgRawAddr = sections[i].raw_addr;
        DWORD imgRawSize = sections[i].sizeof_raw;

        uint8_t* imgData = (uint8_t*)malloc(imgRawSize);
        if (!imgData) break;

        if (!XisoReadAt(h, xbeOffset + imgRawAddr, imgData, imgRawSize)) {
            free(imgData);
            break;
        }

        // The inserted file is an XPR0 container with a fixed-size
        // header followed by raw DXT1 texture data.
        if (imgRawSize >= sizeof(XprFileHeaderRaw)) {
            XprFileHeaderRaw* xpr = (XprFileHeaderRaw*)imgData;
            if (xpr->dwXPRMagic == XPR_HEADER_MAGIC) {
                DWORD dxtOffset = xpr->dwHeaderSize;
                if (dxtOffset < imgRawSize) {
                    DWORD dxtAvail = imgRawSize - dxtOffset;
                    DWORD dxtNeed  = (TITLE_IMAGE_WIDTH  / 4) *
                                     (TITLE_IMAGE_HEIGHT / 4) *
                                     DXT1_BLOCK_BYTES;
                    if (dxtAvail >= dxtNeed) {
                        info->titleImageRGBA = DecodeDXT1(imgData + dxtOffset,
                                                          TITLE_IMAGE_WIDTH,
                                                          TITLE_IMAGE_HEIGHT);
                        if (info->titleImageRGBA) {
                            info->titleImageWidth  = TITLE_IMAGE_WIDTH;
                            info->titleImageHeight = TITLE_IMAGE_HEIGHT;
                        }
                    }
                }
            }
        }

        free(imgData);
        break;
    }

    free(xbeData);
}

// =============================================================================
// Public API
// =============================================================================

XisoTitleInfo XisoGetTitleInfo(const char* isoPath)
{
    XisoTitleInfo info;
    memset(&info, 0, sizeof(info));

    HANDLE h = CreateFileA(isoPath,
                           GENERIC_READ,
                           FILE_SHARE_READ,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return info;

    // The XISO volume descriptor is always at sector 32.
    uint8_t volDesc[XISO_SECTOR_SIZE];
    if (!XisoReadAt(h,
                    (ULONGLONG)XISO_VOLUME_SECTOR * XISO_SECTOR_SIZE,
                    volDesc,
                    sizeof(volDesc)) ||
        memcmp(volDesc, "MICROSOFT*XBOX*MEDIA", 20) != 0)
    {
        CloseHandle(h);
        return info;
    }

    // Volume descriptor layout: bytes 20..23 are the root directory's
    // starting sector, bytes 24..27 its byte size.
    DWORD rootDirSector = (DWORD)volDesc[20]        |
                          ((DWORD)volDesc[21] << 8) |
                          ((DWORD)volDesc[22] << 16)|
                          ((DWORD)volDesc[23] << 24);
    DWORD rootDirSize   = (DWORD)volDesc[24]        |
                          ((DWORD)volDesc[25] << 8) |
                          ((DWORD)volDesc[26] << 16)|
                          ((DWORD)volDesc[27] << 24);

    ULONGLONG xbeOffset = 0;
    DWORD     xbeSize   = 0;
    if (!FindDefaultXbe(h,
                        (ULONGLONG)rootDirSector * XISO_SECTOR_SIZE,
                        rootDirSize,
                        &xbeOffset,
                        &xbeSize))
    {
        CloseHandle(h);
        return info;
    }

    ExtractTitleFromXbe(h, xbeOffset, xbeSize, &info);
    CloseHandle(h);

    info.valid = true;
    return info;
}

void XisoFreeTitleInfo(XisoTitleInfo* info)
{
    if (info && info->titleImageRGBA) {
        free(info->titleImageRGBA);
        info->titleImageRGBA = NULL;
    }
}
