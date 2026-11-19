// lutris.cpp: Lutris on Linux. Its library is the SQLite file pga.db; each
// installed game launches with lutris:rungameid/<id>, through Flatpak when
// that's the Lutris that's here. Covers, banners and icons come from
// Lutris' own folders.

#include "stores.h"

#include <cstdlib>

#include "../sqlite_read.h"

struct LutrisHome {
	std::string db, data, cache, icons, run;
};

static void Homes(std::vector<LutrisHome>& out)
{
	out.clear();
	std::string home = Store_Home();
	if (home.empty()) return;
	const char* xdgData = getenv("XDG_DATA_HOME");
	const char* xdgCache = getenv("XDG_CACHE_HOME");
	std::string data = xdgData && *xdgData ? xdgData : home + "/.local/share";
	std::string cache = xdgCache && *xdgCache ? xdgCache : home + "/.cache";
	LutrisHome native = { data + "/lutris/pga.db", data + "/lutris", cache + "/lutris",
	                      data + "/icons/hicolor/128x128/apps", "lutris" };
	std::string fp = home + "/.var/app/net.lutris.Lutris";
	LutrisHome flatpak = { fp + "/data/lutris/pga.db", fp + "/data/lutris", fp + "/cache/lutris",
	                       fp + "/data/icons/hicolor/128x128/apps", "flatpak run net.lutris.Lutris" };
	if (Store_IsFile(native.db)) out.push_back(native);
	if (Store_IsFile(flatpak.db)) out.push_back(flatpak);
}

bool Lutris_Found()
{
	std::vector<LutrisHome> homes;
	Homes(homes);
	return !homes.empty();
}

// Cover first, it's the shape the other stores' art is; then the banner, then the icon.
static std::string Art(const LutrisHome& h, const std::string& slug)
{
	if (slug.empty()) return "";
	const std::string tries[] = {
		h.cache + "/coverart/" + slug + ".jpg", h.data + "/coverart/" + slug + ".jpg",
		h.cache + "/banners/" + slug + ".jpg", h.data + "/banners/" + slug + ".jpg",
		h.icons + "/lutris_" + slug + ".png",
	};
	for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]); i++)
		if (Store_IsFile(tries[i])) return tries[i];
	return "";
}

static void ScanHome(const LutrisHome& h, std::vector<StoreGame>& out)
{
	std::vector<SqliteRow> games;
	if (!Sqlite_ReadTable(h.db.c_str(), "games", games)) return;

	// Games you hid in Lutris sit in its ".hidden" category.
	std::vector<std::string> hidden;
	std::vector<SqliteRow> cats, links;
	if (Sqlite_ReadTable(h.db.c_str(), "categories", cats) &&
	    Sqlite_ReadTable(h.db.c_str(), "games_categories", links)) {
		std::string hiddenId;
		for (size_t i = 0; i < cats.size(); i++)
			if (cats[i]["name"] == ".hidden") hiddenId = cats[i]["id"];
		for (size_t i = 0; i < links.size() && !hiddenId.empty(); i++)
			if (links[i]["category_id"] == hiddenId) hidden.push_back(links[i]["game_id"]);
	}

	for (size_t i = 0; i < games.size(); i++) {
		SqliteRow& r = games[i];
		if (r["installed"] != "1" || r["hidden"] == "1" || r["id"].empty() || r["name"].empty()) continue;
		bool hide = false;
		for (size_t k = 0; k < hidden.size(); k++) hide |= hidden[k] == r["id"];
		if (hide) continue;
		StoreGame g;
		g.name = r["name"];
		g.launch = h.run + " lutris:rungameid/" + r["id"];
		g.icon = Art(h, r["slug"]);
		out.push_back(g);
	}
}

void Lutris_Scan(std::vector<StoreGame>& out)
{
	out.clear();
	std::vector<LutrisHome> homes;
	Homes(homes);
	for (size_t i = 0; i < homes.size(); i++) ScanHome(homes[i], out);
}
