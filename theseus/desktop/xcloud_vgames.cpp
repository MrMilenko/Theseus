// xcloud_vgames.cpp: bridges the xCloud client into the Virtual Games DB.
// If you're signed in, the account's cloud games and consoles are injected as
// live VGDB entries (launch xcloud://<titleId> or xhome://<serverId>), tagged
// so they never persist to games.ini. They show up in the games grid like any
// other title and launch through the normal path.
//
// The fetch is async (the client runs its own workers), but the g_vgames
// mutation happens on the main thread via the tick, so the DB stays single-
// threaded. Re-runnable: prior streaming entries are cleared before a refresh.

#include "xcloud_client.h"
#include "http_util.h"
#include "virtual_games.h"
#include "xcloud_vgames.h"

#include <string>
#include <vector>

// Boot flow: kick a session, wait for it, fetch games + consoles, wait for
// those, then inject. All transitions run on the main thread from the tick.
enum { XV_IDLE, XV_SESSION, XV_FETCH, XV_DONE };
static int s_state = XV_IDLE;

static void ClearStreamingEntries()
{
    for (int i = 0; i < g_vgames.count; i++) {
        if (g_vgames.games[i].valid && VGames_IsStreaming(g_vgames.games[i]))
            g_vgames.games[i].valid = false;
    }
    VGames_Compact();
}

static void Inject()
{
    VGames_Load();
    ClearStreamingEntries();

    std::vector<XcloudGame> games = Xcloud_GetGames();
    int nGames = 0;
    for (size_t i = 0; i < games.size(); i++) {
        const XcloudGame& g = games[i];
        // The id doubles as the icon filename, so it has to be a safe one.
        if (!g.playable || !Http_IsSafeFileId(g.titleId)) continue;
        std::string launch = "xcloud://" + g.titleId;
        if (VGames_Add(g.name.c_str(), g.titleId.c_str(), launch.c_str(), "E", "Cloud Gaming") < 0)
            break;   // DB full
        Xcloud_QueueArtDownload(g.titleId, g.artUrl);
        nGames++;
    }

    std::vector<XcloudConsole> consoles = Xcloud_GetConsoles();
    int nConsoles = 0;
    for (size_t i = 0; i < consoles.size(); i++) {
        const XcloudConsole& c = consoles[i];
        if (!Http_IsSafeFileId(c.serverId)) continue;
        std::string launch = "xhome://" + c.serverId;
        if (VGames_Add(c.name.c_str(), c.serverId.c_str(), launch.c_str(), "E", "Your Xbox") < 0)
            break;
        nConsoles++;
    }

    g_vgames.generation++;
    fprintf(stderr, "[xCloud] injected %d cloud games, %d consoles into the games grid\n",
            nGames, nConsoles);
}

void XcloudVGames_Sync()
{
    if (!Xcloud_HasToken()) return;
    if (s_state != XV_IDLE && s_state != XV_DONE) return;   // already in flight
    Xcloud_StartSession();
    s_state = XV_SESSION;
}

void XcloudVGames_Tick()
{
    // Notice a sign-in that happened mid-session (or a saved token at boot)
    // and kick a sync; notice a sign-out to clear. Edge-triggered so it fires
    // once per transition, and guarded on state so it never double-kicks.
    static bool s_hadToken = false;
    bool now = Xcloud_HasToken();
    if (now && !s_hadToken && (s_state == XV_IDLE || s_state == XV_DONE)) {
        fprintf(stderr, "[xCloud] token present, starting session for the games grid\n");
        Xcloud_StartSession();
        s_state = XV_SESSION;
    } else if (!now && s_hadToken) {
        XcloudVGames_Clear();
    }
    s_hadToken = now;

    switch (s_state) {
    case XV_SESSION:
        if (Xcloud_SessionReady()) {
            fprintf(stderr, "[xCloud] session ready, fetching games + consoles\n");
            Xcloud_FetchGames();
            Xcloud_FetchConsoles();
            s_state = XV_FETCH;
        }
        break;
    case XV_FETCH:
        if (Xcloud_GamesReady() && Xcloud_ConsolesReady()) {
            Inject();
            s_state = XV_DONE;
        }
        break;
    default:
        break;
    }
}

void XcloudVGames_Clear()
{
    ClearStreamingEntries();
    s_state = XV_IDLE;
}
