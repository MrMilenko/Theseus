// settings_backup.h: copies of your settings and library in Backups/ under
// the user data folder, and putting one back. Plain folders, no archive.
// Everything it reads or writes sits under the root it's handed.
#pragma once

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <string>
#include <vector>
#include "host.h"

// What a backup holds, relative to the user data folder.
static const char* const kBackupFiles[] = {
    "Configs/games.ini", "Configs/games_live.ini", "Configs/desktop.ini", "Configs/Icons.ini",
    "Configs/playlists.ini",
};
static const char* const kBackupFolders[] = { "Configs/icons" };

#define BACKUP_DIR "Backups"

static bool Backup_Exists(const std::string& path, bool* isDir = NULL)
{
    HostFileInfo info;
    if (!Host_Stat(path.c_str(), &info)) return false;
    if (isDir) *isDir = (info.attrs & HOST_ATTR_DIR) != 0;
    return true;
}

// Makes every folder down to path. Only ever called below the root.
static bool Backup_MakeDirs(const std::string& root, const std::string& rel)
{
    std::string at = root;
    size_t from = 0;
    while (from <= rel.size()) {
        size_t slash = rel.find('/', from);
        std::string part = rel.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        if (!part.empty()) {
            at += "/" + part;
            if (!Host_MakeDir(at.c_str())) return false;
        }
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return true;
}

// A name from the list, never a path that climbs out.
static bool Backup_NameOk(const std::string& name)
{
    return !name.empty() && name[0] != '.' && name.find_first_of("/\\:") == std::string::npos &&
           name.find("..") == std::string::npos;
}

// Swaps one file in the way SafeSave does: a copy next to it, flushed, then
// one rename. A settings file it replaces stays as .old.
static bool Backup_Replace(const std::string& from, const std::string& to, bool keepOld)
{
    std::string tmp = to + ".new";
    if (!Host_CopyFile(from.c_str(), tmp.c_str(), false) || !Host_FlushToDisk(tmp.c_str())) {
        Host_RemoveFile(tmp.c_str());
        return false;
    }
    if (keepOld && Backup_Exists(to)) Host_CopyFile(to.c_str(), (to + ".old").c_str(), false);
    if (!Host_RenameReplace(tmp.c_str(), to.c_str())) { Host_RemoveFile(tmp.c_str()); return false; }
    return true;
}

static bool Backup_EndsWith(const std::string& s, const char* end)
{
    size_t n = strlen(end);
    return s.size() >= n && s.compare(s.size() - n, n, end) == 0;
}

// Every file in a folder and the ones below it, relative to it. A swap's
// leftovers aren't worth keeping.
static void Backup_ListFiles(const std::string& dir, const std::string& rel, std::vector<std::string>& out)
{
    HostDir* d = Host_OpenDir(dir.c_str());
    if (!d) return;
    char name[512];
    bool isDir = false;
    std::vector<std::string> subs;
    while (Host_ReadDirType(d, name, sizeof(name), &isDir)) {
        std::string n = name;
        if (n == "." || n == ".." || Backup_EndsWith(n, ".old") || Backup_EndsWith(n, ".new")) continue;
        if (isDir) subs.push_back(n);
        else out.push_back(rel.empty() ? n : rel + "/" + n);
    }
    Host_CloseDir(d);
    for (size_t i = 0; i < subs.size(); i++)
        Backup_ListFiles(dir + "/" + subs[i], rel.empty() ? subs[i] : rel + "/" + subs[i], out);
}

// Copies what there is into Backups/<date time><suffix>. Gives back the
// folder's name, or empty when nothing could be written.
static std::string Backup_Make(const std::string& root, const char* suffix, int* outFiles)
{
    if (outFiles) *outFiles = 0;
    char stamp[64];
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H.%M.%S", localtime(&now));
    std::string name = std::string(stamp) + (suffix ? suffix : "");
    // Two in the same second get a number.
    for (int n = 2; Backup_Exists(root + "/" BACKUP_DIR "/" + name); n++) {
        char num[16];
        snprintf(num, sizeof(num), " %d", n);
        name = std::string(stamp) + (suffix ? suffix : "") + num;
    }
    std::string rel = std::string(BACKUP_DIR "/") + name;
    if (!Backup_MakeDirs(root, rel + "/Configs")) return "";
    std::string dest = root + "/" + rel;
    int files = 0;
    bool ok = true;
    for (size_t i = 0; i < sizeof(kBackupFiles) / sizeof(kBackupFiles[0]); i++) {
        std::string src = root + "/" + kBackupFiles[i];
        if (!Backup_Exists(src)) continue;
        if (Host_CopyFile(src.c_str(), (dest + "/" + kBackupFiles[i]).c_str(), false)) files++;
        else ok = false;
    }
    for (size_t i = 0; i < sizeof(kBackupFolders) / sizeof(kBackupFolders[0]); i++) {
        std::string src = root + "/" + kBackupFolders[i];
        std::vector<std::string> list;
        Backup_ListFiles(src, "", list);
        for (size_t k = 0; k < list.size(); k++) {
            std::string to = std::string(kBackupFolders[i]) + "/" + list[k];
            size_t cut = to.find_last_of('/');
            if (!Backup_MakeDirs(dest, to.substr(0, cut)) ||
                !Host_CopyFile((src + "/" + list[k]).c_str(), (dest + "/" + to).c_str(), false)) { ok = false; continue; }
            files++;
        }
    }
    if (outFiles) *outFiles = files;
    return ok ? name : "";
}

// Backup names, newest first. The names sort by date already.
static void Backup_List(const std::string& root, std::vector<std::string>& out)
{
    out.clear();
    HostDir* d = Host_OpenDir((root + "/" BACKUP_DIR).c_str());
    if (!d) return;
    char name[512];
    bool isDir = false;
    while (Host_ReadDirType(d, name, sizeof(name), &isDir))
        if (isDir && Backup_NameOk(name)) out.push_back(name);
    Host_CloseDir(d);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return a > b; });
}

// Puts a backup back. Whatever's there now is backed up first, so a restore
// can always be undone. A settings file the backup didn't have is removed,
// icons only ever get added or replaced.
static bool Backup_Restore(const std::string& root, const std::string& name, std::string* outSaved)
{
    if (!Backup_NameOk(name)) return false;
    std::string src = root + "/" BACKUP_DIR "/" + name;
    bool isDir = false;
    if (!Backup_Exists(src + "/Configs", &isDir) || !isDir) return false;
    std::string saved = Backup_Make(root, " before restore", NULL);
    if (saved.empty()) return false;
    if (outSaved) *outSaved = saved;
    bool ok = true;
    for (size_t i = 0; i < sizeof(kBackupFiles) / sizeof(kBackupFiles[0]); i++) {
        std::string from = src + "/" + kBackupFiles[i], to = root + "/" + kBackupFiles[i];
        if (Backup_Exists(from)) ok = Backup_Replace(from, to, true) && ok;
        else if (Backup_Exists(to)) ok = Host_RemoveFile(to.c_str()) && ok;
    }
    for (size_t i = 0; i < sizeof(kBackupFolders) / sizeof(kBackupFolders[0]); i++) {
        std::vector<std::string> list;
        Backup_ListFiles(src + "/" + kBackupFolders[i], "", list);
        for (size_t k = 0; k < list.size(); k++) {
            std::string rel = std::string(kBackupFolders[i]) + "/" + list[k];
            if (!Backup_MakeDirs(root, rel.substr(0, rel.find_last_of('/')))) { ok = false; continue; }
            ok = Backup_Replace(src + "/" + rel, root + "/" + rel, false) && ok;
        }
    }
    return ok;
}
