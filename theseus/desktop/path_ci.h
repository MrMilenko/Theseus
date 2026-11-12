// Case insensitive path resolution. Windows and macOS don't care, Linux
// does, and skins/XAPs written on one land on the other.

#pragma once

#include "host.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// Rewrites resolvedPath to the real on disk spelling. A trailing wildcard is
// peeled off first so FindFirstFile style callers keep working.
inline bool Plat_ResolveCaseInsensitive(char* resolvedPath, size_t maxLen) {
    struct stat st;
    if (stat(resolvedPath, &st) == 0)
        return true;

    char tempPath[1024];
    strncpy(tempPath, resolvedPath, sizeof(tempPath) - 1);
    tempPath[sizeof(tempPath) - 1] = '\0';

    char savedWildcard[64] = "";
    char* lastSlash = strrchr(tempPath, '/');
    if (lastSlash) {
        const char* tail = lastSlash + 1;
        if (strchr(tail, '*') || strchr(tail, '?')) {
            strncpy(savedWildcard, tail, sizeof(savedWildcard) - 1);
            savedWildcard[sizeof(savedWildcard) - 1] = '\0';
            *lastSlash = '\0';
        }
    }

    // Keep the anchor, or an absolute path comes back relative.
    char rebuilt[1024];
    rebuilt[0] = '\0';
    char* walk = tempPath;
    // A drive letter only turns up on Windows.
    if (tempPath[0] && tempPath[1] == ':') {
        snprintf(rebuilt, sizeof(rebuilt), "%c:", tempPath[0]);
        walk = tempPath + 2;
        if (*walk == '/' || *walk == '\\') {
            strncat(rebuilt, "/", sizeof(rebuilt) - strlen(rebuilt) - 1);
            walk++;
        }
    }
    if (*walk == '/') {
        strncat(rebuilt, "/", sizeof(rebuilt) - strlen(rebuilt) - 1);
        walk++;
    }
    char* components[64];
    int nComponents = 0;
    for (char* p = walk; *p && nComponents < 64; ) {
        while (*p == '/') p++;
        if (!*p) break;
        components[nComponents++] = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';
    }

    for (int i = 0; i < nComponents; i++) {
        const char* wanted = components[i];
        const bool needSlash = rebuilt[0] && rebuilt[strlen(rebuilt) - 1] != '/';

        char candidate[1024];
        if (rebuilt[0])
            snprintf(candidate, sizeof(candidate), "%s%s%s", rebuilt, needSlash ? "/" : "", wanted);
        else
            snprintf(candidate, sizeof(candidate), "%s", wanted);

        if (stat(candidate, &st) == 0) {
            strncpy(rebuilt, candidate, sizeof(rebuilt) - 1);
            rebuilt[sizeof(rebuilt) - 1] = '\0';
            continue;
        }

        const char* dirToScan = rebuilt[0] ? rebuilt : ".";
        bool found = false;
        HostDir* dir = Host_OpenDir(dirToScan);
        if (!dir)
            return false;
        char name[260];
        while (Host_ReadDir(dir, name, sizeof(name), NULL)) {
            if (strcasecmp(name, wanted) == 0) {
                snprintf(candidate, sizeof(candidate), "%s%s%s",
                         rebuilt, needSlash ? "/" : "", name);
                strncpy(rebuilt, candidate, sizeof(rebuilt) - 1);
                rebuilt[sizeof(rebuilt) - 1] = '\0';
                found = true;
                break;
            }
        }
        Host_CloseDir(dir);
        if (!found)
            return false;
    }

    if (savedWildcard[0]) {
        const bool needSlash = rebuilt[0] && rebuilt[strlen(rebuilt) - 1] != '/';
        strncat(rebuilt, needSlash ? "/" : "", sizeof(rebuilt) - strlen(rebuilt) - 1);
        strncat(rebuilt, savedWildcard, sizeof(rebuilt) - strlen(rebuilt) - 1);
    }

    if (strlen(rebuilt) >= maxLen)
        return false;
    strcpy(resolvedPath, rebuilt);
    return true;
}
