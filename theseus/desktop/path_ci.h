// Case insensitive path resolution. Windows and macOS don't care, Linux
// does, and skins/XAPs written on one land on the other.

#pragma once

#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef THESEUS_HOST_WIN
#include <io.h>
#else
#include <dirent.h>
#include <strings.h>
#endif

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
#ifdef THESEUS_HOST_WIN
    if (tempPath[0] && tempPath[1] == ':') {
        snprintf(rebuilt, sizeof(rebuilt), "%c:", tempPath[0]);
        walk = tempPath + 2;
        if (*walk == '/' || *walk == '\\') {
            strncat(rebuilt, "/", sizeof(rebuilt) - strlen(rebuilt) - 1);
            walk++;
        }
    }
#endif
    if (*walk == '/') {
        strncat(rebuilt, "/", sizeof(rebuilt) - strlen(rebuilt) - 1);
        walk++;
    }
    char* components[64];
    int nComponents = 0;
    char* save = NULL;
#ifdef THESEUS_HOST_WIN
    char* tok = strtok_s(walk, "/", &save);
#else
    char* tok = strtok_r(walk, "/", &save);
#endif
    while (tok && nComponents < 64) {
        components[nComponents++] = tok;
#ifdef THESEUS_HOST_WIN
        tok = strtok_s(NULL, "/", &save);
#else
        tok = strtok_r(NULL, "/", &save);
#endif
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
#ifdef THESEUS_HOST_WIN
        char searchBuf[1024];
        snprintf(searchBuf, sizeof(searchBuf), "%s%s*", dirToScan, needSlash ? "/" : "");
        struct _finddata_t fd;
        intptr_t hFind = _findfirst(searchBuf, &fd);
        if (hFind != -1) {
            do {
                if (_stricmp(fd.name, wanted) == 0) {
                    snprintf(candidate, sizeof(candidate), "%s%s%s",
                             rebuilt, needSlash ? "/" : "", fd.name);
                    strncpy(rebuilt, candidate, sizeof(rebuilt) - 1);
                    rebuilt[sizeof(rebuilt) - 1] = '\0';
                    found = true;
                    break;
                }
            } while (_findnext(hFind, &fd) == 0);
            _findclose(hFind);
        }
#else
        DIR* dir = opendir(dirToScan);
        if (!dir)
            return false;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcasecmp(entry->d_name, wanted) == 0) {
                snprintf(candidate, sizeof(candidate), "%s%s%s",
                         rebuilt, needSlash ? "/" : "", entry->d_name);
                strncpy(rebuilt, candidate, sizeof(rebuilt) - 1);
                rebuilt[sizeof(rebuilt) - 1] = '\0';
                found = true;
                break;
            }
        }
        closedir(dir);
#endif
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
