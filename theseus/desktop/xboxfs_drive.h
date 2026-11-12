// Xbox drive letter to a real absolute directory under the user data dir:
//   C: Configs   Q: Data   E: Library   X: Library
// X is the desktop's own virtual drive and never collides with a real Windows
// path, so new libraries use it. E stays mapped for libraries written before.
// F: G: R: and the rest have no analog, so this returns NULL and the caller's
// stat/opendir fails on its own. Also home to the mkdir helpers.

#pragma once

#include "app_paths.h"

#include "host.h"

#include <string.h>

static inline const char* XboxFS_DriveToPrefix(char drive) {
    if (drive == 'C' || drive == 'c') return AppPath_Tree("Configs");
    if (drive == 'Q' || drive == 'q') return AppPath_Tree("Data");
    if (drive == 'E' || drive == 'e') return AppPath_Tree("Library");
    if (drive == 'X' || drive == 'x') return AppPath_Tree("Library");
    return 0;
}

// Make one directory. True if it exists after.
static inline bool Plat_Mkdir(const char* path) {
    if (!path || !*path) return false;
    return Host_MakeDir(path);
}

// Make a directory and any missing parents. Accepts either slash style.
static inline bool Plat_MkdirP(const char* path) {
    if (!path || !*path) return false;
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char* p = tmp; *p; ++p) if (*p == '\\') *p = '/';
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; Plat_Mkdir(tmp); *p = '/'; }
    }
    return Plat_Mkdir(tmp);
}
