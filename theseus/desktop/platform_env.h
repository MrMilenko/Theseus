// Home and user lookup that works the same on every desktop OS. Standalone
// on purpose (only libc), so host files can pull it without dragging the
// platform shim in. Both return a stable buffer,
// never NULL.

#pragma once

#include "platform.h"
#include <cstdlib>
#include <cstring>

inline const char* PlatformHomeDir() {
    static char s_home[512] = {0};
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        const char* h = getenv("HOME");
#ifdef THESEUS_HOST_WIN
        if (!h || !*h) h = getenv("USERPROFILE");
#endif
        if (h && *h) {
            size_t n = strlen(h);
            if (n >= sizeof(s_home)) n = sizeof(s_home) - 1;
            memcpy(s_home, h, n);
            s_home[n] = '\0';
        }
    }
    return s_home;
}

inline const char* PlatformUserName() {
    static char s_user[128] = {0};
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        const char* u;
#ifdef THESEUS_HOST_WIN
        u = getenv("USERNAME");
#else
        u = getenv("USER");
        if (!u || !*u) u = getenv("LOGNAME");
#endif
        if (u && *u) {
            size_t n = strlen(u);
            if (n >= sizeof(s_user)) n = sizeof(s_user) - 1;
            memcpy(s_user, u, n);
            s_user[n] = '\0';
        } else {
            strcpy(s_user, "user");
        }
    }
    return s_user;
}
