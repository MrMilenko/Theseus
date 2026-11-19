// itch.cpp: the itch.io app. Its installs live in butler.db (SQLite): a cave
// per install, with the verdict butler wrote after looking at the files,
// which names the programs it found. We launch the one that fits this OS
// directly, so play time counts, and use the game's cover from itch.io.

#include "stores.h"

#include <cstdlib>
#include <cstring>

#include "host.h"
#include "../json_util.h"
#include "../sqlite_read.h"

static void Dbs(std::vector<std::string>& out)
{
	out.clear();
	std::vector<std::string> tries;
	std::string home = Store_Home();
	const char* xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) tries.push_back(std::string(xdg) + "/itch/db/butler.db");
	if (!home.empty()) {
		tries.push_back(home + "/.config/itch/db/butler.db");
		tries.push_back(home + "/.var/app/io.itch.itch/config/itch/db/butler.db");
		tries.push_back(home + "/Library/Application Support/itch/db/butler.db");
	}
	const char* appData = getenv("APPDATA");
	if (appData && *appData) tries.push_back(std::string(appData) + "\\itch\\db\\butler.db");
	for (size_t i = 0; i < tries.size(); i++) {
		bool dup = false;
		for (size_t k = 0; k < out.size(); k++) dup |= out[k] == tries[i];
		if (!dup && Store_IsFile(tries[i])) out.push_back(tries[i]);
	}
}

bool Itch_Found()
{
	std::vector<std::string> dbs;
	Dbs(dbs);
	return !dbs.empty();
}

// Which of butler's flavors run here, best first.
static int FlavorRank(const std::string& flavor)
{
	static const char* kWin[] = { "windows", "windows-script", NULL };
	static const char* kMac[] = { "app-macos", "macos", "script", NULL };
	static const char* kLinux[] = { "linux", "script", NULL };
	const char* os = Host_Name();
	const char** list = strcmp(os, "Windows") == 0 ? kWin : strcmp(os, "macOS") == 0 ? kMac : kLinux;
	for (int i = 0; list[i]; i++)
		if (flavor == list[i]) return i;
	return -1;
}

static void ScanDb(const std::string& db, std::vector<StoreGame>& out)
{
	std::vector<SqliteRow> caves, games;
	if (!Sqlite_ReadTable(db.c_str(), "caves", caves)) return;
	Sqlite_ReadTable(db.c_str(), "games", games);
	std::map<std::string, SqliteRow*> byId;
	for (size_t i = 0; i < games.size(); i++) byId[games[i]["id"]] = &games[i];

	for (size_t i = 0; i < caves.size(); i++) {
		const std::string& verdict = caves[i]["verdict"];
		std::string base = Store_JsonString(verdict, "basePath");
		size_t arr = Json_FindArray(verdict, "candidates");
		if (base.empty() || arr == std::string::npos) continue;
		std::vector<std::string> cands = Json_SplitArray(verdict, arr);
		std::string path, flavor;
		int best = -1;
		for (size_t c = 0; c < cands.size(); c++) {
			std::string f = Store_JsonString(cands[c], "flavor");
			int rank = FlavorRank(f);
			if (rank < 0 || (best >= 0 && rank >= best)) continue;
			std::string p = Store_JsonString(cands[c], "path");
			if (p.empty()) continue;
			best = rank;
			path = p;
			flavor = f;
		}
		if (path.empty()) continue;

		std::map<std::string, SqliteRow*>::iterator it = byId.find(caves[i]["game_id"]);
		SqliteRow* game = it == byId.end() ? NULL : it->second;
		StoreGame g;
		g.name = game ? (*game)["title"] : "";
		if (g.name.empty()) g.name = caves[i]["install_folder_name"];
		if (g.name.empty()) continue;
		std::string full = Store_Join(base, path);
		g.launch = "\"" + full + "\"";
		if (flavor == "app-macos") g.launch = "open -W " + g.launch;
		g.workdir = base;
		if (game) {
			g.artUrl = (*game)["still_cover_url"];
			if (g.artUrl.empty()) g.artUrl = (*game)["cover_url"];
			if ((*game)["classification"] == "tool") g.category = "Applications";
		}
		// The exe's own icon only when itch.io has no cover.
		if (g.artUrl.empty()) g.icon = full;
		out.push_back(g);
	}
}

void Itch_Scan(std::vector<StoreGame>& out)
{
	out.clear();
	std::vector<std::string> dbs;
	Dbs(dbs);
	for (size_t i = 0; i < dbs.size(); i++) ScanDb(dbs[i], out);
}
