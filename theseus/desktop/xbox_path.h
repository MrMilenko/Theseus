// xbox_path.h: turns an Xbox path into a host path. The one translator on
// desktop; everything that takes an Xbox path comes through here.
//
// C: lands in Configs, Q: in Data, E: and X: in Library, all under the user
// data dir. F:, G:, R: and friends have no analog and come back empty, so
// XAP scripts that iterate drives just see nothing.
//
// Leaf header on purpose: platform_shim.h includes it before any of its own
// stubs, so it can't lean on the shim.

#pragma once

#include "platform.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "xboxfs_drive.h"  // XboxFS_DriveToPrefix, AppPath
#include "path_ci.h"       // Plat_ResolveCaseInsensitive

extern int VGames_MatchFolder(const char*);
extern const char* VGames_GetIconPath(int);

inline bool XboxPath_NoCaseN(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
        if (!a[i]) return true;
    }
    return true;
}

inline bool XboxPath_NoCase(const char* a, const char* b) {
    size_t na = strlen(a);
    return na == strlen(b) && XboxPath_NoCaseN(a, b, na);
}

inline void XboxPath_Slashes(char* s) {
    for (; *s; s++) if (*s == '\\') *s = '/';
}

// Shared body. Writes never follow the virtual-game icon redirect: a
// DeleteFile on a missing folder icon would otherwise land on the real
// backing asset.
inline bool XboxPath_TranslateImpl(const char* xboxPath, char* out, size_t cch,
                                   bool followIconRedirect) {
    if (!out || !cch) return false;
    out[0] = '\0';
    if (!xboxPath || !*xboxPath) return false;

    // Device-path form: "\Device\Harddisk0\PartitionN\..." Used by the XAP
    // `launch()` builtin to address Xbox HDD partitions. Map the partition
    // back to a drive letter. Only Partition1 (E) and Partition2 (C) exist
    // on desktop; 6/7/8/9 = F/G/R/S are dead letters.
    static const char kDevPrefix[] = "\\Device\\Harddisk0\\Partition";
    const size_t kDevPrefixLen = sizeof(kDevPrefix) - 1;
    if (strncmp(xboxPath, kDevPrefix, kDevPrefixLen) == 0) {
        const char* p = xboxPath + kDevPrefixLen;
        int partNum = 0;
        while (*p >= '0' && *p <= '9') { partNum = partNum * 10 + (*p - '0'); p++; }
        if (*p == '\\' || *p == '/') p++;
        char drive = 0;
        switch (partNum) {
            case 1: drive = 'E'; break;
            case 2: drive = 'C'; break;
            case 6: drive = 'F'; break;
            case 7: drive = 'G'; break;
            case 8: drive = 'R'; break;
            case 9: drive = 'S'; break;
        }
        const char* prefix = drive ? XboxFS_DriveToPrefix(drive) : 0;
        if (!prefix) return false;
        snprintf(out, cch, "%s/%s", prefix, p);
        XboxPath_Slashes(out);
        Plat_ResolveCaseInsensitive(out, cch);
        return true;
    }

    // Drive letter pattern: "X:\" or "X:/" or "WORD:\".
    const char* colon = strchr(xboxPath, ':');
    if (colon && (colon[1] == '\\' || colon[1] == '/')) {
        int driveLen = (int)(colon - xboxPath);
        if (driveLen > 0 && driveLen < 32) {
            char drive[32];
            memcpy(drive, xboxPath, driveLen);
            drive[driveLen] = '\0';

            const char* rest = colon + 2;
            const char* prefix = NULL;

            if (driveLen == 1) {
                if (drive[0] == 'C' || drive[0] == 'c') {
                    // Strip "UIX Configs/" so the on-disk Configs/ is flat,
                    // without the UIX-Lite legacy folder name.
                    static const char kUix[] = "UIX Configs";
                    size_t uixLen = sizeof(kUix) - 1;
                    if (strncmp(rest, kUix, uixLen) == 0 &&
                        (rest[uixLen] == '\\' || rest[uixLen] == '/')) {
                        rest += uixLen + 1;
                    }
                    prefix = AppPath_Tree("Configs");
                } else {
                    prefix = XboxFS_DriveToPrefix(drive[0]);
                }
            } else if (XboxPath_NoCase(drive, "MUSIC")) {
                // Legacy MUSIC: virtual drive, pointed at the bundled
                // Library/Music fallback. desktop.ini's [Library] MusicRoot
                // is consumed separately by CMediaCollection / DashMusic_Scan.
                prefix = AppPath_Tree("Library/Music");
            }

            // F:, G:, R: and so on. Come back empty so stat/opendir/fopen/
            // mkdir all fail predictably. Passing it through would have
            // mkdir create a literal "F:" directory.
            if (!prefix) return false;

            snprintf(out, cch, "%s/%s", prefix, rest);
            XboxPath_Slashes(out);

            // Engine settings lived in Q:\System\config.ini on Xbox. Desktop
            // folds them into Configs/desktop.ini, so both names land on the
            // one file.
            char sysCfg[512];
            snprintf(sysCfg, sizeof(sysCfg), "%s/System/config.ini", AppPath_Tree("Data"));
            if (XboxPath_NoCase(out, sysCfg))
                snprintf(out, cch, "%s", AppPath("Configs/desktop.ini"));

            // FATX doesn't care about case, Linux does.
            Plat_ResolveCaseInsensitive(out, cch);

            // A missing folder icon falls through to the virtual games DB.
            struct stat st;
            if (followIconRedirect && stat(out, &st) != 0) {
                const char* iconTail = strstr(out, "/icon.jpg");
                if (!iconTail) iconTail = strstr(out, "/icon.png");
                if (iconTail) {
                    char folder[512];
                    snprintf(folder, sizeof(folder), "%.*s", (int)(iconTail - out), out);
                    int vgIdx = VGames_MatchFolder(folder);
                    if (vgIdx >= 0) {
                        const char* realIcon = VGames_GetIconPath(vgIdx);
                        if (realIcon) snprintf(out, cch, "%s", realIcon);
                    }
                }
            }
            return true;
        }
    }

    // No drive letter: hand it back with forward slashes.
    snprintf(out, cch, "%s", xboxPath);
    XboxPath_Slashes(out);
    return true;
}

// Returns false, with out emptied, when the path has no host analog.
inline bool XboxPath_Translate(const char* xboxPath, char* out, size_t cch) {
    return XboxPath_TranslateImpl(xboxPath, out, cch, true);
}

// For anything that creates, deletes, renames or writes.
inline bool XboxPath_TranslateForWrite(const char* xboxPath, char* out, size_t cch) {
    return XboxPath_TranslateImpl(xboxPath, out, cch, false);
}
