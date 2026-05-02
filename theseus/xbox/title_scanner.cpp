// title_scanner.cpp: CTitleScanner XAP node. Native title browser
// that walks installed-XBE directories on every drive, parses XBE
// title metadata, and feeds the results into the dashboard's title
// menu. Replaces the slower XAP-side title-walk in UIX-Lite.
// Theseus-original.

#include "title_scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

IMPLEMENT_NODE("TitleScanner", CTitleScanner, CNode)

#define _FND_CLASS CTitleScanner
START_NODE_FUN(CTitleScanner, CNode)
    NODE_FUN_VV(ClearMenus)
    NODE_FUN_VSS(AddMenu)
    NODE_FUN_VV(Rebuild)
END_NODE_FUN()
#undef _FND_CLASS

namespace {

// Scratch limits sized for plausible Xbox libraries. A buffered 256 titles
// per menu × 8 menus is generous; if anyone ever overflows this we'll know
// from the panic log when SetValue truncates.
const int kMaxTitlesPerMenu = 512;
const int kMaxMenus         = 16;
const int kEntriesPerKey    = 25;     // UIX-Lite's overflow-trigger threshold
const int kFolderNameMax    = 64;
const int kLaunchFileMax    = 16;
const int kRelativePathsMax = 16;
const int kPartitionsMax    = 16;

const TCHAR kCacheFilePath[] = _T("C:\\UIX Configs\\cache.ini");
const TCHAR kCacheSectionT[] = _T("Cache");
const char  kCacheSection[]  = "Cache";

const TCHAR kIconsFilePath[] = _T("C:\\UIX Configs\\Icons.ini");
const TCHAR kIconsSectionT[] = _T("default");

// Partition table in driveIdx order matching the script's DriveLocations
// array. Win32 file APIs need DOS-letter paths, not raw device paths, so
// every probe / FindFirstFile uses dosPath. The bit / devPath columns are
// kept for diagnostics + future use.
struct PartitionDef {
    DWORD bit;            // 0 = always present
    const char* devPath;  // raw NT device path (diagnostic)
    const char* dosPath;  // DOS letter root, no trailing backslash
};

const PartitionDef kPartitions[] = {
    { 0,  "\\Device\\Harddisk0\\Partition2", "C:" },  // 0
    { 0,  "\\Device\\Harddisk0\\Partition1", "E:" },  // 1
    { 1,  "\\Device\\Harddisk0\\Partition6", "F:" },  // bit 0
    { 2,  "\\Device\\Harddisk0\\Partition7", "G:" },  // bit 1
    { 4,  "\\Device\\Harddisk1\\Partition6", "H:" },  // bit 2
    { 8,  "\\Device\\Harddisk1\\Partition7", "I:" },  // bit 3
    { 16, "\\Device\\Harddisk1\\Partition1", "R:" },  // bit 4
};

struct TitleEntry {
    char folderName[kFolderNameMax];
    int  driveIdx;
    int  pathIdx;
    char launchFile[kLaunchFileMax];   // "default.xbe" / "default.iso" / "default.cci"
    DWORD titleId;                     // 0 if not extracted
    char xbePath[MAX_PATH];            // full path to default.xbe (only valid when titleId != 0)
};

struct MenuSpec {
    char name[64];
    char pathSpec[256]; // ;-separated relative paths
};

bool FileExistsA(const char* path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != 0xFFFFFFFF && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExistsA(const char* path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Probe each partition via DOS-letter root (Win32 APIs reject device paths).
// Returns the per-partition driveIdx + DOS path. Also yields the bitmask
// the .xap stores in the "ExtendedPartitions" save slot for parity.
int ProbePartitions(int outIdx[kPartitionsMax],
                    const char* outDosPath[kPartitionsMax],
                    DWORD* outBitmask)
{
    DWORD mask = 0;
    int n = 0;
    for (size_t i = 0; i < sizeof(kPartitions) / sizeof(kPartitions[0]); i++) {
        const PartitionDef& p = kPartitions[i];
        char rootProbe[8];
        _snprintf(rootProbe, sizeof(rootProbe), "%s\\", p.dosPath);
        if (p.bit == 0 || DirExistsA(rootProbe)) {
            outIdx[n] = (int)i;
            outDosPath[n] = p.dosPath;
            n++;
            if (p.bit != 0)
                mask |= p.bit;
        }
    }
    if (outBitmask) *outBitmask = mask;
    return n;
}

// Split a ;-separated relative-path list. Returns count.
int SplitPathSpec(const char* spec, char outPaths[][MAX_PATH])
{
    if (!spec || !*spec) return 0;
    int n = 0;
    const char* p = spec;
    while (*p && n < kRelativePathsMax) {
        const char* sep = strchr(p, ';');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len >= MAX_PATH) len = MAX_PATH - 1;
        memcpy(outPaths[n], p, len);
        outPaths[n][len] = '\0';
        // Trim trailing backslash for predictable joining.
        while (len > 0 && (outPaths[n][len-1] == '\\' || outPaths[n][len-1] == '/')) {
            outPaths[n][--len] = '\0';
        }
        n++;
        if (!sep) break;
        p = sep + 1;
    }
    return n;
}

int __cdecl CompareEntries(const void* a, const void* b)
{
    const TitleEntry* ea = (const TitleEntry*)a;
    const TitleEntry* eb = (const TitleEntry*)b;
    return _stricmp(ea->folderName, eb->folderName);
}

// Look at a candidate `<dir>\<sub>\` for default.xbe / default.iso / default.cci.
// Returns the launch filename (static string) on hit, NULL on miss.
const char* DetectLaunchFile(const char* dir, const char* sub)
{
    char path[MAX_PATH];

    _snprintf(path, sizeof(path), "%s\\%s\\default.xbe", dir, sub);
    if (FileExistsA(path)) return "default.xbe";

    _snprintf(path, sizeof(path), "%s\\%s\\default.iso", dir, sub);
    if (FileExistsA(path)) return "default.iso";

    _snprintf(path, sizeof(path), "%s\\%s\\default.cci", dir, sub);
    if (FileExistsA(path)) return "default.cci";

    return NULL;
}

// Walk one (partition, relativePath) combo. Each subdirectory containing a
// known launchable becomes one TitleEntry.
void ScanDir(const char* dir, int driveIdx, int pathIdx,
             TitleEntry* out, int* outCount)
{
    char wild[MAX_PATH];
    _snprintf(wild, sizeof(wild), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(wild, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (fd.cFileName[0] == '.')
            continue;

        const char* launchFile = DetectLaunchFile(dir, fd.cFileName);
        if (!launchFile)
            continue;

        if (*outCount >= kMaxTitlesPerMenu) {
            OutputDebugStringA("[TitleScanner] menu full; skipping further titles\n");
            break;
        }

        TitleEntry* e = &out[(*outCount)++];
        strncpy(e->folderName, fd.cFileName, kFolderNameMax - 1);
        e->folderName[kFolderNameMax - 1] = '\0';
        e->driveIdx = driveIdx;
        e->pathIdx  = pathIdx;
        strncpy(e->launchFile, launchFile, kLaunchFileMax - 1);
        e->launchFile[kLaunchFileMax - 1] = '\0';
        e->titleId = 0;
        e->xbePath[0] = '\0';

        // Best-effort title-ID extraction for default.xbe; ISO/CCI extraction
        // requires reading the embedded XBE header at the right offset and
        // is deferred until v2 of the scanner.
        if (strcmp(launchFile, "default.xbe") == 0) {
            _snprintf(e->xbePath, sizeof(e->xbePath), "%s\\%s\\%s",
                     dir, fd.cFileName, launchFile);
            CXBExecutable xbe;
            // ReadFile returns 1 on success, 0 on failure.
            if (xbe.ReadFile(e->xbePath, false, false) != 0)
                e->titleId = (DWORD)xbe.TitleId();
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

// Append `entry` to `value`, prefixing with `|` if `value` is non-empty.
// Returns false if the buffer would overflow.
bool AppendEntry(char* value, size_t valueCap, const TitleEntry& e)
{
    char rec[kFolderNameMax + kLaunchFileMax + 32];
    _snprintf(rec, sizeof(rec), "%s%s;%d;%d;%s",
             value[0] ? "|" : "",
             e.folderName, e.driveIdx, e.pathIdx, e.launchFile);
    size_t curLen = strlen(value);
    size_t recLen = strlen(rec);
    if (curLen + recLen + 1 > valueCap)
        return false;
    memcpy(value + curLen, rec, recLen + 1);
    return true;
}

// Wipe any prior <menuName>-N overflow keys so a shrunken list doesn't leave
// stale tails behind.
void ClearOverflow(CSettingsFile& cache, const char* menuName)
{
    TCHAR existing[2];
    char overflowKey[128];
    for (int i = 1; i < 64; i++) {
        _snprintf(overflowKey, sizeof(overflowKey), "%s-%d", menuName, i);
#ifdef UNICODE
        TCHAR wKey[128];
        MultiByteToWideChar(CP_ACP, 0, overflowKey, -1, wKey, 128);
        TCHAR wSection[16] = L"Cache";
        if (!cache.GetValue(wSection, wKey, existing, 2))
            break;
        cache.SetValue(wSection, wKey, _T(""));
#else
        if (!cache.GetValue(kCacheSection, overflowKey, existing, 2))
            break;
        cache.SetValue(kCacheSection, overflowKey, "");
#endif
    }
}

void WriteMenuToCache(CSettingsFile& cache, const char* menuName,
                      TitleEntry* titles, int n)
{
    qsort(titles, n, sizeof(TitleEntry), CompareEntries);

    ClearOverflow(cache, menuName);

    int idx = 0;
    int chunkNum = 0;
    while (idx < n) {
        char value[8192];
        value[0] = '\0';

        int chunkEnd = idx + kEntriesPerKey;
        if (chunkEnd > n) chunkEnd = n;

        for (int j = idx; j < chunkEnd; j++) {
            if (!AppendEntry(value, sizeof(value), titles[j])) {
                OutputDebugStringA("[TitleScanner] cache value overflowed; truncating\n");
                break;
            }
        }

        char keyBuf[128];
        if (chunkNum == 0)
            strncpy(keyBuf, menuName, sizeof(keyBuf) - 1);
        else
            _snprintf(keyBuf, sizeof(keyBuf), "%s-%d", menuName, chunkNum);
        keyBuf[sizeof(keyBuf) - 1] = '\0';

#ifdef UNICODE
        TCHAR wKey[128], wValue[8192], wSection[16] = L"Cache";
        MultiByteToWideChar(CP_ACP, 0, keyBuf, -1, wKey, 128);
        MultiByteToWideChar(CP_ACP, 0, value, -1, wValue, 8192);
        cache.SetValue(wSection, wKey, wValue);
#else
        cache.SetValue(kCacheSection, keyBuf, value);
#endif

        chunkNum++;
        idx = chunkEnd;
    }
}

// Make sure E:\UDATA\<titleId>\TitleImage.xbx exists. The Xbox dashboard's
// title-image cache lives under UDATA per-titleId; if it's missing the
// dashboard falls back to a generic icon. We extract the $$XTIMAGE section
// straight out of the title's default.xbe (CXBExecutable's m_pImage holds
// the raw section bytes after ReadFile) and write it into UDATA verbatim.
// Doesn't overwrite an existing TitleImage.xbx; that's where users put
// their custom artwork.
bool EnsureUDATATitleImage(DWORD titleId, const char* xbePath)
{
    if (titleId == 0 || !xbePath)
        return false;

    char udataDir[MAX_PATH];
    char udataPath[MAX_PATH];
    _snprintf(udataDir,  sizeof(udataDir),  "E:\\UDATA\\%08lX", (unsigned long)titleId);
    _snprintf(udataPath, sizeof(udataPath), "%s\\TitleImage.xbx", udataDir);

    if (FileExistsA(udataPath))
        return false;  // already there, leave it

    CXBExecutable xbe;
    if (xbe.ReadFile(xbePath, /*bGetTitleImage*/ true, /*bGetAlternativeImage*/ false) == 0)
        return false;
    if (xbe.m_pImage == NULL || xbe.m_iTitleImageActualSize <= 0)
        return false;  // XBE didn't carry an embedded title image

    CreateDirectoryA(udataDir, NULL);

    FILE* f = fopen(udataPath, "wb");
    if (!f) return false;
    fwrite(xbe.m_pImage, 1, (size_t)xbe.m_iTitleImageActualSize, f);
    fclose(f);
    return true;
}

// Rewrite Icons.ini entries + ensure UDATA TitleImage.xbx exists for every
// recognised title in this batch. Always clobbers the existing icons key so
// a renamed/replaced game gets the right titleId. Returns counts for log.
struct IconsResult { int mapped; int udataCreated; };

IconsResult WriteIconsForTitles(CSettingsFile& icons, TitleEntry* titles, int n)
{
    IconsResult r = { 0, 0 };
    for (int i = 0; i < n; i++) {
        if (titles[i].titleId == 0) continue;

#ifdef UNICODE
        TCHAR wKey[kFolderNameMax];
        TCHAR wHex[16];
        MultiByteToWideChar(CP_ACP, 0, titles[i].folderName, -1, wKey, kFolderNameMax);
        _stprintf(wHex, _T("%08lx"), (unsigned long)titles[i].titleId);
        icons.SetValue(kIconsSectionT, wKey, wHex);
#else
        char hex[16];
        _snprintf(hex, sizeof(hex), "%08lx", (unsigned long)titles[i].titleId);
        icons.SetValue("default", titles[i].folderName, hex);
#endif
        r.mapped++;

        if (titles[i].xbePath[0] &&
            EnsureUDATATitleImage(titles[i].titleId, titles[i].xbePath))
            r.udataCreated++;
    }
    return r;
}

// Module-level state. Single instance because XAP-side `theTitleScanner` is
// a singleton; nothing's threaded.
MenuSpec     g_menus[kMaxMenus];
int          g_menuCount = 0;

}  // namespace

CTitleScanner::CTitleScanner() {}
CTitleScanner::~CTitleScanner() {}

void CTitleScanner::ClearMenus()
{
    g_menuCount = 0;
}

void CTitleScanner::AddMenu(const TCHAR* szName, const TCHAR* szPathSpec)
{
    if (!szName || !szPathSpec) return;
    if (g_menuCount >= kMaxMenus) {
        OutputDebugStringA("[TitleScanner] AddMenu: too many menus\n");
        return;
    }

    MenuSpec& m = g_menus[g_menuCount++];
#ifdef UNICODE
    WideCharToMultiByte(CP_ACP, 0, szName,     -1, m.name,     sizeof(m.name),     NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, szPathSpec, -1, m.pathSpec, sizeof(m.pathSpec), NULL, NULL);
#else
    strncpy(m.name,     szName,     sizeof(m.name) - 1);
    strncpy(m.pathSpec, szPathSpec, sizeof(m.pathSpec) - 1);
    m.name[sizeof(m.name) - 1] = '\0';
    m.pathSpec[sizeof(m.pathSpec) - 1] = '\0';
#endif
}

void CTitleScanner::Rebuild()
{
    DWORD startTick = GetTickCount();
    OutputDebugStringA("[TitleScanner] Rebuild starting\n");

    int    partIdx[kPartitionsMax];
    const char* partDos[kPartitionsMax];
    DWORD  partMask = 0;
    int    nParts = ProbePartitions(partIdx, partDos, &partMask);

    char buf[64];
    _snprintf(buf, sizeof(buf), "[TitleScanner] %d partitions present, mask=0x%lx\n",
             nParts, (unsigned long)partMask);
    OutputDebugStringA(buf);

    CSettingsFile cache;
    if (!cache.Open(kCacheFilePath)) {
        OutputDebugStringA("[TitleScanner] open cache.ini failed; writing new file\n");
        // CSettingsFile::Open returns false when the file doesn't exist yet
        // but it still allows SetValue to populate an empty in-memory file
        // that Save() will create on disk; continue.
    }

    CSettingsFile icons;
    bool iconsOk = icons.Open(kIconsFilePath);
    if (!iconsOk)
        OutputDebugStringA("[TitleScanner] open Icons.ini failed; will create new\n");

    int totalTitles = 0;
    int totalIcons  = 0;
    int totalUdata  = 0;

    for (int m = 0; m < g_menuCount; m++) {
        MenuSpec& menu = g_menus[m];

        char relPaths[kRelativePathsMax][MAX_PATH];
        int nRelPaths = SplitPathSpec(menu.pathSpec, relPaths);

        TitleEntry titles[kMaxTitlesPerMenu];
        int nTitles = 0;

        for (int p = 0; p < nParts; p++) {
            for (int rp = 0; rp < nRelPaths; rp++) {
                char dir[MAX_PATH];
                _snprintf(dir, sizeof(dir), "%s\\%s", partDos[p], relPaths[rp]);
                ScanDir(dir, partIdx[p], rp, titles, &nTitles);
            }
        }

        char log[128];
        _snprintf(log, sizeof(log),
                 "[TitleScanner] menu '%s': %d titles\n", menu.name, nTitles);
        OutputDebugStringA(log);

        WriteMenuToCache(cache, menu.name, titles, nTitles);
        IconsResult ir = WriteIconsForTitles(icons, titles, nTitles);
        totalIcons += ir.mapped;
        totalUdata += ir.udataCreated;
        totalTitles += nTitles;
    }

    if (!cache.Save())
        OutputDebugStringA("[TitleScanner] save cache.ini FAILED\n");
    cache.Close();

    if (totalIcons > 0) {
        if (!icons.Save())
            OutputDebugStringA("[TitleScanner] save Icons.ini FAILED\n");
    }
    icons.Close();

    DWORD ms = GetTickCount() - startTick;
    _snprintf(buf, sizeof(buf),
              "[TitleScanner] done: %d titles, %d icons mapped, %d UDATA images created, in %lu ms\n",
              totalTitles, totalIcons, totalUdata, (unsigned long)ms);
    OutputDebugStringA(buf);
}
