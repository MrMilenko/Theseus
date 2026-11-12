// playlist_maker.cpp: F6 panel. Build playlists from the media DB.
// Stored via playlist.h; consumed at runtime by media_ui playback queue
// and (later) the Playlists XAP scene.

#include "std.h"
#include "imgui.h"
#include "playlist.h"
#include "media_db.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

extern "C" int  MediaDB_IsScanning();
extern "C" void MediaDB_ScanAndCache();

bool g_playlistMakerOpen = false;

void TogglePlaylistMaker() { g_playlistMakerOpen = !g_playlistMakerOpen; }

static int  s_selectedPlaylist = -1;
static char s_newName[64] = "";
static char s_renameBuf[64] = "";
static bool s_renameRequested = false;

static void DrawPlaylistsPane() {
    ImGui::TextDisabled("Playlists");
    ImGui::Separator();
    int n = Playlist_Count();
    for (int i = 0; i < n; i++) {
        const Playlist* p = Playlist_Get(i);
        if (!p) continue;
        char label[96];
        snprintf(label, sizeof(label), "%s  (%d)##pl%d",
            p->name.c_str(), (int)p->items.size(), i);
        if (ImGui::Selectable(label, s_selectedPlaylist == i))
            s_selectedPlaylist = i;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputTextWithHint("##newpl", "New playlist name", s_newName, sizeof(s_newName));
    ImGui::SameLine();
    if (ImGui::Button("Create") && s_newName[0]) {
        if (Playlist_Create(s_newName)) {
            s_selectedPlaylist = Playlist_Count() - 1;
            s_newName[0] = 0;
        }
    }

    if (s_selectedPlaylist >= 0 && s_selectedPlaylist < Playlist_Count()) {
        const Playlist* sel = Playlist_Get(s_selectedPlaylist);
        if (sel) {
            ImGui::Spacing();
            if (ImGui::Button("Rename")) {
                strncpy(s_renameBuf, sel->name.c_str(), sizeof(s_renameBuf) - 1);
                s_renameBuf[sizeof(s_renameBuf) - 1] = 0;
                s_renameRequested = true;
                ImGui::OpenPopup("Rename Playlist");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete Playlist?");
        }
    }

    if (ImGui::BeginPopupModal("Rename Playlist", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("##rn", s_renameBuf, sizeof(s_renameBuf));
        if (ImGui::Button("OK") && s_renameBuf[0]) {
            const Playlist* sel = Playlist_Get(s_selectedPlaylist);
            if (sel) Playlist_Rename(sel->name.c_str(), s_renameBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete Playlist?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Playlist* sel = Playlist_Get(s_selectedPlaylist);
        ImGui::Text("Delete \"%s\"?", sel ? sel->name.c_str() : "");
        if (ImGui::Button("Yes")) {
            if (sel) Playlist_Delete(sel->name.c_str());
            s_selectedPlaylist = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void DrawItemsPane() {
    if (s_selectedPlaylist < 0 || s_selectedPlaylist >= Playlist_Count()) {
        ImGui::TextDisabled("Select a playlist on the left.");
        return;
    }
    const Playlist* sel = Playlist_Get(s_selectedPlaylist);
    if (!sel) return;
    std::string name = sel->name; // copy because list ops may invalidate the pointer

    ImGui::Text("%s", name.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("(%d items)", (int)sel->items.size());
    ImGui::Separator();

    // Items
    int moveFrom = -1, moveTo = -1, removeIdx = -1;
    for (int i = 0; i < (int)sel->items.size(); i++) {
        const PlaylistItem& it = sel->items[i];
        ImGui::PushID(i);
        ImGui::Text("%2d.", i + 1);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", it.title.empty() ? it.path.c_str() : it.title.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);
        if (ImGui::SmallButton("Up") && i > 0) { moveFrom = i; moveTo = i - 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dn") && i + 1 < (int)sel->items.size()) { moveFrom = i; moveTo = i + 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) removeIdx = i;
        ImGui::PopID();
    }
    if (moveFrom >= 0) Playlist_MoveItem(name.c_str(), moveFrom, moveTo);
    if (removeIdx >= 0) Playlist_RemoveItem(name.c_str(), removeIdx);

    // Snapshot of paths already in playlist; the bottom list filters these
    // out so each item only appears in one place at a time.
    std::set<std::string> inPlaylist;
    for (size_t i = 0; i < sel->items.size(); i++) inPlaylist.insert(sel->items[i].path);

    ImGui::Spacing();
    ImGui::Separator();

    // Counts self-lock. The listing below holds the DB lock, since a scan
    // swaps the vectors out from under the C string getters.
    int movieCount = MediaDB_GetMovieCount();
    int showCount  = MediaDB_GetShowCount();

    ImGui::TextDisabled("Add items");
    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);
    if (MediaDB_IsScanning()) {
        ImGui::TextDisabled("Scanning...");
    } else if (ImGui::SmallButton("Refresh Library")) {
        MediaDB_ScanAndCache();
    }
    ImGui::Separator();

    if (movieCount == 0 && showCount == 0) {
        ImGui::TextWrapped("No media loaded. Set Movies / TV roots in Settings > Media Library, then hit Refresh Library above.");
        return;
    }

    MediaDB_Lock();
    int movieAvail = 0;
    for (int i = 0; i < movieCount; i++)
        if (!inPlaylist.count(MediaDB_GetMoviePathC(i))) movieAvail++;

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "Movies (%d)", movieAvail);
    if (ImGui::CollapsingHeader(hdr)) {
        for (int i = 0; i < movieCount; i++) {
            const char* path = MediaDB_GetMoviePathC(i);
            if (inPlaylist.count(path)) continue;
            const char* title = MediaDB_GetMovieTitleC(i);
            int year = MediaDB_GetMovieYearC(i);
            char label[256];
            if (year > 0) snprintf(label, sizeof(label), "+ %s (%d)##m%d", title, year, i);
            else          snprintf(label, sizeof(label), "+ %s##m%d", title, i);
            if (ImGui::SmallButton(label))
                Playlist_AddItem(name.c_str(), path, title);
        }
    }
    snprintf(hdr, sizeof(hdr), "TV Shows (%d)", showCount);
    if (ImGui::CollapsingHeader(hdr)) {
        for (int s = 0; s < showCount; s++) {
            const char* showTitle = MediaDB_GetShowTitleC(s);
            ImGui::PushID(s);
            if (ImGui::TreeNode(showTitle)) {
                int seasonCount = MediaDB_GetSeasonCountC(s);
                for (int se = 0; se < seasonCount; se++) {
                    const char* seasonName = MediaDB_GetSeasonNameC(s, se);
                    int epCount = MediaDB_GetEpisodeCountC(s, se);

                    // Available = episodes not already in this playlist.
                    int avail = 0;
                    for (int e = 0; e < epCount; e++)
                        if (!inPlaylist.count(MediaDB_GetEpisodePathC(s, se, e))) avail++;

                    ImGui::PushID(se);
                    bool seasonOpen = ImGui::TreeNode(seasonName);

                    // Add Season button sits next to the disclosure widget so
                    // the user can bulk-add without expanding the season.
                    if (avail > 0) {
                        ImGui::SameLine();
                        char addBtn[64];
                        snprintf(addBtn, sizeof(addBtn), "+ Season (%d)", avail);
                        if (ImGui::SmallButton(addBtn)) {
                            for (int e = 0; e < epCount; e++) {
                                const char* epPath = MediaDB_GetEpisodePathC(s, se, e);
                                if (inPlaylist.count(epPath)) continue;
                                char displayed[256];
                                snprintf(displayed, sizeof(displayed), "%s S%02dE%02d %s",
                                    showTitle,
                                    MediaDB_GetEpisodeSeasonNumC(s, se, e),
                                    MediaDB_GetEpisodeNumberC(s, se, e),
                                    MediaDB_GetEpisodeTitleC(s, se, e));
                                Playlist_AddItem(name.c_str(), epPath, displayed);
                            }
                        }
                    }

                    if (seasonOpen) {
                        for (int e = 0; e < epCount; e++) {
                            const char* epPath = MediaDB_GetEpisodePathC(s, se, e);
                            if (inPlaylist.count(epPath)) continue;
                            const char* epTitle = MediaDB_GetEpisodeTitleC(s, se, e);
                            int sn = MediaDB_GetEpisodeSeasonNumC(s, se, e);
                            int en = MediaDB_GetEpisodeNumberC(s, se, e);
                            char label[256];
                            snprintf(label, sizeof(label), "+ S%02dE%02d %s##e%d", sn, en, epTitle, e);
                            if (ImGui::SmallButton(label)) {
                                char displayed[256];
                                snprintf(displayed, sizeof(displayed), "%s S%02dE%02d %s",
                                    showTitle, sn, en, epTitle);
                                Playlist_AddItem(name.c_str(), epPath, displayed);
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    MediaDB_Unlock();
}

void RenderPlaylistMaker() {
    if (!g_playlistMakerOpen) return;

    ImVec2 vp = ImGui::GetMainViewport()->Size;
    float maxW = vp.x - 40.0f, maxH = vp.y - 80.0f;
    if (maxW < 720) maxW = 720;
    if (maxH < 480) maxH = 480;
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, maxH < 700 ? maxH : 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(720, 480), ImVec2(maxW, maxH));

    ImGui::Begin("UIX Playlist Maker", &g_playlistMakerOpen);

    bool dirty = Playlist_IsDirty();
    if (ImGui::Button(dirty ? "Save *" : "Save") && dirty) {
        Playlist_SaveAll();
    }
    ImGui::SameLine();
    if (dirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Unsaved changes");
    } else {
        ImGui::TextDisabled("All changes saved");
    }
    ImGui::Separator();

    if (ImGui::BeginTable("##plmgrid", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("##left",  ImGuiTableColumnFlags_WidthFixed, 240.0f);
        ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawPlaylistsPane();
        ImGui::TableSetColumnIndex(1);
        DrawItemsPane();
        ImGui::EndTable();
    }
    ImGui::End();
}
