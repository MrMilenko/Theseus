// Named playlists stored in Configs/playlists.ini.

#include "app_paths.h"
#include "playlist.h"
#include "xboxfs_drive.h"
#include "safe_save.h"

#include <cstdio>
#include <cstring>

static std::vector<Playlist> s_playlists;
static bool                  s_dirty = false;
#define kPath AppPath("Configs/playlists.ini")

bool Playlist_IsDirty() { return s_dirty; }

static void TrimEol(char* line) {
    char* nl = strchr(line, '\n'); if (nl) *nl = 0;
    char* cr = strchr(line, '\r'); if (cr) *cr = 0;
}

void Playlist_LoadAll() {
    s_playlists.clear();
    FILE* fp = fopen(kPath, "r");
    if (!fp) return;
    char line[2048];
    Playlist* cur = nullptr;
    PlaylistItem pending;
    bool havePending = false;
    while (fgets(line, sizeof(line), fp)) {
        TrimEol(line);
        if (line[0] == 0) continue;
        if (line[0] == '[') {
            const char* s = strstr(line, "[Playlist:");
            if (s) {
                // The last item belongs to the playlist that's ending, and has
                // to land before push_back can move the vector under cur.
                if (cur && havePending) { cur->items.push_back(pending); pending = {}; havePending = false; }
                Playlist p;
                const char* nameStart = s + 10;
                const char* nameEnd   = strrchr(nameStart, ']');
                if (nameEnd) p.name.assign(nameStart, nameEnd - nameStart);
                s_playlists.push_back(p);
                cur = &s_playlists.back();
            }
            continue;
        }
        if (!cur) continue;
        if (strncmp(line, "Item=", 5) == 0) {
            if (havePending) { cur->items.push_back(pending); pending = {}; }
            pending.path = line + 5;
            havePending = true;
        }
        else if (strncmp(line, "Title=", 6) == 0) {
            pending.title = line + 6;
        }
    }
    if (cur && havePending) cur->items.push_back(pending);
    fclose(fp);
}

void Playlist_SaveAll() {
    Plat_MkdirP(AppPath_Tree("Configs"));
    SafeSave save(kPath);
    FILE* fp = save.f;
    if (!fp) return;
    for (size_t i = 0; i < s_playlists.size(); i++) {
        const Playlist& p = s_playlists[i];
        fprintf(fp, "[Playlist:%s]\n", p.name.c_str());
        for (size_t j = 0; j < p.items.size(); j++) {
            fprintf(fp, "Item=%s\n",  p.items[j].path.c_str());
            fprintf(fp, "Title=%s\n", p.items[j].title.c_str());
        }
        fprintf(fp, "\n");
    }
    save.Commit();
    s_dirty = false;
}

int Playlist_Count() { return (int)s_playlists.size(); }

const Playlist* Playlist_Get(int idx) {
    if (idx < 0 || idx >= (int)s_playlists.size()) return nullptr;
    return &s_playlists[idx];
}

Playlist* Playlist_Find(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < s_playlists.size(); i++)
        if (s_playlists[i].name == name) return &s_playlists[i];
    return nullptr;
}

bool Playlist_Create(const char* name) {
    if (!name || !*name) return false;
    if (Playlist_Find(name)) return false;
    Playlist p;
    p.name = name;
    s_playlists.push_back(p);
    s_dirty = true;
    return true;
}

bool Playlist_Delete(const char* name) {
    if (!name) return false;
    for (auto it = s_playlists.begin(); it != s_playlists.end(); ++it) {
        if (it->name == name) {
            s_playlists.erase(it);
            s_dirty = true;
            return true;
        }
    }
    return false;
}

bool Playlist_Rename(const char* oldName, const char* newName) {
    if (!oldName || !newName || !*newName) return false;
    if (Playlist_Find(newName)) return false;
    Playlist* p = Playlist_Find(oldName);
    if (!p) return false;
    p->name = newName;
    s_dirty = true;
    return true;
}

bool Playlist_AddItem(const char* name, const char* path, const char* title) {
    if (!path || !*path) return false;
    Playlist* p = Playlist_Find(name);
    if (!p) return false;
    PlaylistItem it;
    it.path  = path;
    it.title = title ? title : "";
    p->items.push_back(it);
    s_dirty = true;
    return true;
}

bool Playlist_RemoveItem(const char* name, int idx) {
    Playlist* p = Playlist_Find(name);
    if (!p) return false;
    if (idx < 0 || idx >= (int)p->items.size()) return false;
    p->items.erase(p->items.begin() + idx);
    s_dirty = true;
    return true;
}

bool Playlist_MoveItem(const char* name, int from, int to) {
    Playlist* p = Playlist_Find(name);
    if (!p) return false;
    int n = (int)p->items.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    PlaylistItem tmp = p->items[from];
    p->items.erase(p->items.begin() + from);
    p->items.insert(p->items.begin() + to, tmp);
    s_dirty = true;
    return true;
}
