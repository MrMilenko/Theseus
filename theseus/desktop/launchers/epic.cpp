// epic.cpp: Epic Games Store installs. The launcher writes a JSON .item per
// install into its Manifests folder (ProgramData on Windows, Application
// Support on a Mac); a game launches through the launcher's own URL so
// sign-in and cloud saves still happen. Heroic keeps its Epic installs in
// legendary's installed.json and launches them through heroic://.

#include "stores.h"

#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "host.h"
#include "../json_util.h"

static void ManifestDirs(std::vector<std::string>& out)
{
	out.clear();
	if (strcmp(Host_Name(), "Windows") == 0) {
		const char* pd = getenv("ProgramData");
		out.push_back(std::string(pd && *pd ? pd : "C:\\ProgramData") +
		              "\\Epic\\EpicGamesLauncher\\Data\\Manifests");
	}
	std::string home = Store_Home();
	if (!home.empty())
		out.push_back(home + "/Library/Application Support/Epic/EpicGamesLauncher/Data/Manifests");
	for (size_t i = out.size(); i-- > 0;)
		if (!Store_IsDir(out[i])) out.erase(out.begin() + i);
}

static void LegendaryFiles(std::vector<std::string>& out)
{
	out.clear();
	std::vector<std::string> heroic;
	Store_HeroicDirs(heroic);
	for (size_t i = 0; i < heroic.size(); i++) {
		std::string f = heroic[i] + "/legendaryConfig/legendary/installed.json";
		if (Store_IsFile(f)) out.push_back(f);
	}
}

bool Epic_Found()
{
	std::vector<std::string> a, b;
	ManifestDirs(a);
	LegendaryFiles(b);
	return !a.empty() || !b.empty();
}

// AppCategories lists "games" for a game; add-ons and engines leave it out.
static bool IsGame(const std::string& item)
{
	size_t at = Store_JsonKey(item, "AppCategories");
	if (at == std::string::npos) return true;
	size_t open = item.find('[', at), close = item.find(']', at);
	if (open == std::string::npos || close == std::string::npos) return true;
	return item.substr(open, close - open).find("\"games\"") != std::string::npos;
}

static void ScanManifests(const std::string& dir, std::vector<StoreGame>& out)
{
	std::vector<std::string> names;
	Store_ListDir(dir, names);
	for (size_t i = 0; i < names.size(); i++) {
		const std::string& n = names[i];
		if (n.size() < 5 || strcasecmp(n.c_str() + n.size() - 5, ".item") != 0) continue;
		std::string item;
		if (!Store_ReadFile(Store_Join(dir, n), item)) continue;
		std::string app = Store_JsonString(item, "AppName");
		std::string ns = Store_JsonString(item, "CatalogNamespace");
		std::string id = Store_JsonString(item, "CatalogItemId");
		std::string mainApp = Store_JsonString(item, "MainGameAppName");
		if (app.empty() || ns.empty() || id.empty()) continue;
		if (Store_JsonBool(item, "bIsIncompleteInstall")) continue;
		if (!mainApp.empty() && mainApp != app) continue;   // DLC
		if (!IsGame(item)) continue;

		StoreGame g;
		g.name = Store_JsonString(item, "DisplayName");
		if (g.name.empty()) g.name = app;
		g.launch = "com.epicgames.launcher://apps/" + ns + "%3A" + id + "%3A" + app +
		           "?action=launch&silent=true";
		std::string loc = Store_JsonString(item, "InstallLocation");
		std::string exe = Store_JsonString(item, "LaunchExecutable");
		if (!loc.empty() && !exe.empty()) g.icon = Store_Join(loc, exe);
		out.push_back(g);
	}
}

static void ScanLegendary(const std::string& file, const std::string& heroicDir,
                          std::vector<StoreGame>& out)
{
	std::string text;
	if (!Store_ReadFile(file, text)) return;
	size_t obj = text.find('{');
	if (obj == std::string::npos) return;
	std::map<std::string, HeroicInfo> lib;
	Store_HeroicLibrary(heroicDir, "legendary_library.json", lib);
	std::vector<std::string> games = Json_SplitObjectValues(text, obj);
	for (size_t i = 0; i < games.size(); i++) {
		const std::string& e = games[i];
		std::string app = Store_JsonString(e, "app_name");
		if (app.empty() || Store_JsonBool(e, "is_dlc")) continue;
		StoreGame g;
		g.name = Store_JsonString(e, "title");
		std::map<std::string, HeroicInfo>::iterator it = lib.find(app);
		if (it != lib.end()) {
			if (g.name.empty()) g.name = it->second.title;
			g.artUrl = it->second.art;
		}
		if (g.name.empty()) g.name = app;
		g.launch = "heroic://launch/legendary/" + app;
		std::string path = Store_JsonString(e, "install_path");
		std::string exe = Store_JsonString(e, "executable");
		if (!path.empty() && !exe.empty()) g.icon = Store_Join(path, exe);
		out.push_back(g);
	}
}

void Epic_Scan(std::vector<StoreGame>& out)
{
	out.clear();
	std::vector<std::string> dirs;
	ManifestDirs(dirs);
	for (size_t i = 0; i < dirs.size(); i++) ScanManifests(dirs[i], out);
	std::vector<std::string> heroic;
	Store_HeroicDirs(heroic);
	for (size_t i = 0; i < heroic.size(); i++) {
		std::string f = heroic[i] + "/legendaryConfig/legendary/installed.json";
		if (Store_IsFile(f)) ScanLegendary(f, heroic[i], out);
	}
}
