// safe_save.h: settings writes that can't leave a half-written file behind.
#pragma once
#include <stdio.h>
#include <string>
#include "host.h"

// Write through f, then Commit. The file is built as <path>.new and swapped in
// with one rename, and the version it replaces is kept as <path>.old. Never
// committing (an early return) throws the new file away and leaves the old.
struct SafeSave {
    std::string path, tmp;
    FILE* f = nullptr;

    explicit SafeSave(const char* p, const char* mode = "w") : path(p), tmp(path + ".new") {
        f = fopen(tmp.c_str(), mode);
    }
    ~SafeSave() {
        if (f) { fclose(f); remove(tmp.c_str()); }
    }
    bool Commit() {
        if (!f) return false;
        bool ok = !ferror(f);
        ok = (fclose(f) == 0) && ok;
        f = nullptr;
        // On disk before the rename, or a power cut can leave an empty file.
        ok = ok && Host_FlushToDisk(tmp.c_str());
        if (!ok) { remove(tmp.c_str()); return false; }
        HostFileInfo info;
        if (Host_Stat(path.c_str(), &info))
            Host_CopyFile(path.c_str(), (path + ".old").c_str(), false);
        if (!Host_RenameReplace(tmp.c_str(), path.c_str())) { remove(tmp.c_str()); return false; }
        return true;
    }
};
