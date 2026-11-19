// gog.cpp: GOG installs. GOG Galaxy and the offline installers register each
// game under Software\GOG.com\Games\<id> on Windows; those launch the game's
// own exe so play time counts. Heroic lists its GOG installs in
// gog_store/installed.json and launches them through heroic://.

#include "stores.h"

#include <cstdlib>
#include <cstring>

#include "host.h"
#include "../json_util.h"

// 32 bit installers land under WOW6432Node, a few write the plain key, and a
// per-user install goes under HKEY_CURRENT_USER.
static const struct { int hive; const char* key; } kGogKeys[] = {
	{ HOST_REG_MACHINE, "Software\\WOW6432Node\\GOG.com\\Games" },
	{ HOST_REG_MACHINE, "Software\\GOG.com\\Games" },
	{ HOST_REG_USER,    "Software\\GOG.com\\Games" },
};
static const int kGogKeyCount = (int)(sizeof(kGogKeys) / sizeof(kGogKeys[0]));

static std::string RegValue(int hive, const std::string& key, const char* name)
{
	char buf[1024];
	return Host_RegistryValue(hive, key.c_str(), name, buf, sizeof(buf)) ? buf : "";
}

static void HeroicFiles(std::vector<std::string>& dirs)
{
	std::vector<std::string> heroic;
	Store_HeroicDirs(heroic);
	dirs.clear();
	for (size_t i = 0; i < heroic.size(); i++)
		if (Store_IsFile(heroic[i] + "/gog_store/installed.json")) dirs.push_back(heroic[i]);
}

bool Gog_Found()
{
	char ids[1][256];
	for (int k = 0; k < kGogKeyCount; k++)
		if (Host_RegistrySubkeys(kGogKeys[k].hive, kGogKeys[k].key, ids, 1) > 0) return true;
	std::vector<std::string> heroic;
	HeroicFiles(heroic);
	return !heroic.empty();
}

// goggame-<id>.ico sits next to most GOG games and looks better than the exe's.
static std::string GameIcon(const std::string& dir, const std::string& id, const std::string& exe)
{
	std::string ico = Store_Join(dir, "goggame-" + id + ".ico");
	return Store_IsFile(ico) ? ico : exe;
}

static void ScanRegistry(std::vector<StoreGame>& out)
{
	std::vector<std::string> seen;
	static char ids[512][256];
	for (int k = 0; k < kGogKeyCount; k++) {
		int n = Host_RegistrySubkeys(kGogKeys[k].hive, kGogKeys[k].key, ids, 512);
		for (int i = 0; i < n; i++) {
			std::string id = ids[i];
			bool dup = false;
			for (size_t s = 0; s < seen.size(); s++) dup |= seen[s] == id;
			if (dup) continue;
			seen.push_back(id);

			std::string key = std::string(kGogKeys[k].key) + "\\" + id;
			int hive = kGogKeys[k].hive;
			if (!RegValue(hive, key, "dependsOn").empty()) continue;   // DLC
			std::string exe = RegValue(hive, key, "exe");
			std::string name = RegValue(hive, key, "gameName");
			std::string cmd = RegValue(hive, key, "launchCommand");
			if (name.empty() || (exe.empty() && cmd.empty())) continue;

			StoreGame g;
			g.name = name;
			g.launch = exe.empty() ? cmd : "\"" + exe + "\"";
			std::string param = RegValue(hive, key, "launchParam");
			if (!exe.empty() && !param.empty()) g.launch += " " + param;
			std::string path = RegValue(hive, key, "path");
			g.workdir = RegValue(hive, key, "workingDir");
			if (g.workdir.empty()) g.workdir = path;
			g.icon = GameIcon(path, id, exe);
			out.push_back(g);
		}
	}
}

// The name GOG wrote into the install folder, for a game Heroic's cache doesn't list.
static std::string InfoName(const std::string& dir, const std::string& id)
{
	std::string text;
	if (!Store_ReadFile(Store_Join(dir, "goggame-" + id + ".info"), text)) return "";
	return Store_JsonString(text, "name");
}

static void ScanHeroic(const std::string& heroicDir, std::vector<StoreGame>& out)
{
	std::string text;
	if (!Store_ReadFile(heroicDir + "/gog_store/installed.json", text)) return;
	size_t arr = Json_FindArray(text, "installed");
	if (arr == std::string::npos) return;
	std::map<std::string, HeroicInfo> lib;
	Store_HeroicLibrary(heroicDir, "gog_library.json", lib);
	std::vector<std::string> games = Json_SplitArray(text, arr);
	for (size_t i = 0; i < games.size(); i++) {
		const std::string& e = games[i];
		std::string id = Store_JsonString(e, "appName");
		if (id.empty() || Store_JsonBool(e, "is_dlc")) continue;
		std::string path = Store_JsonString(e, "install_path");
		StoreGame g;
		std::map<std::string, HeroicInfo>::iterator it = lib.find(id);
		if (it != lib.end()) { g.name = it->second.title; g.artUrl = it->second.art; }
		if (g.name.empty()) g.name = InfoName(path, id);
		if (g.name.empty() && !path.empty()) {
			size_t slash = path.find_last_of("/\\", path.size() - 2);
			g.name = slash == std::string::npos ? path : path.substr(slash + 1);
			while (!g.name.empty() && (g.name[g.name.size() - 1] == '/' || g.name[g.name.size() - 1] == '\\'))
				g.name.erase(g.name.size() - 1);
		}
		if (g.name.empty()) continue;
		g.launch = "heroic://launch/gog/" + id;
		g.icon = GameIcon(path, id, "");
		out.push_back(g);
	}
}

void Gog_Scan(std::vector<StoreGame>& out)
{
	out.clear();
	ScanRegistry(out);
	std::vector<std::string> heroic;
	HeroicFiles(heroic);
	for (size_t i = 0; i < heroic.size(); i++) ScanHeroic(heroic[i], out);
}
