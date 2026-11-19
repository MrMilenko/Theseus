// stores.cpp: the file and JSON bits every store reader shares, and where
// Heroic keeps its config.

#include "stores.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host.h"
#include "../json_util.h"

std::string Store_Home()
{
	const char* h = getenv("HOME");
	if (!h || !*h) h = getenv("USERPROFILE");
	return h ? h : "";
}

std::string Store_Join(const std::string& dir, const std::string& name)
{
	if (dir.empty()) return name;
	char last = dir[dir.size() - 1];
	if (last == '/' || last == '\\') return dir + name;
	// Keep a Windows path looking like one.
	return dir + (dir.find('\\') != std::string::npos ? "\\" : "/") + name;
}

bool Store_IsDir(const std::string& path)
{
	HostFileInfo info;
	return !path.empty() && Host_Stat(path.c_str(), &info) && (info.attrs & HOST_ATTR_DIR);
}

bool Store_IsFile(const std::string& path)
{
	HostFileInfo info;
	return !path.empty() && Host_Stat(path.c_str(), &info) && !(info.attrs & HOST_ATTR_DIR);
}

bool Store_ReadFile(const std::string& path, std::string& out)
{
	out.clear();
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	char buf[65536];
	size_t n;
	// A store cache past 64 MB isn't one we want to walk.
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < (64u << 20))
		out.append(buf, n);
	fclose(f);
	return true;
}

void Store_ListDir(const std::string& dir, std::vector<std::string>& names)
{
	names.clear();
	HostDir* d = Host_OpenDir(dir.c_str());
	if (!d) return;
	char name[512];
	bool isDir = false;
	while (Host_ReadDirType(d, name, sizeof(name), &isDir))
		if (name[0] != '.') names.push_back(name);
	Host_CloseDir(d);
}

size_t Store_JsonKey(const std::string& obj, const char* key)
{
	size_t klen = strlen(key);
	int depth = 0;
	for (size_t i = 0; i < obj.size(); i++) {
		char c = obj[i];
		if (c == '{' || c == '[') { depth++; continue; }
		if (c == '}' || c == ']') { depth--; continue; }
		if (c != '"') continue;
		size_t start = i;
		for (i++; i < obj.size() && obj[i] != '"'; i++)
			if (obj[i] == '\\') i++;
		if (depth != 1) continue;
		size_t j = i + 1;
		while (j < obj.size() && (obj[j] == ' ' || obj[j] == '\t' || obj[j] == '\r' || obj[j] == '\n')) j++;
		if (j >= obj.size() || obj[j] != ':') continue;
		if (i - start - 1 == klen && obj.compare(start + 1, klen, key) == 0) return start;
	}
	return std::string::npos;
}

std::string Store_JsonString(const std::string& obj, const char* key)
{
	size_t at = Store_JsonKey(obj, key);
	return at == std::string::npos ? "" : Json_GetString(obj, key, at);
}

bool Store_JsonBool(const std::string& obj, const char* key)
{
	size_t at = Store_JsonKey(obj, key);
	return at != std::string::npos && Json_GetBool(obj, key, at);
}

void Store_HeroicDirs(std::vector<std::string>& out)
{
	out.clear();
	std::vector<std::string> tries;
	std::string home = Store_Home();
	const char* xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) tries.push_back(std::string(xdg) + "/heroic");
	if (!home.empty()) {
		tries.push_back(home + "/.config/heroic");
		tries.push_back(home + "/.var/app/com.heroicgameslauncher.hgl/config/heroic");
		tries.push_back(home + "/Library/Application Support/heroic");
	}
	const char* appData = getenv("APPDATA");
	if (appData && *appData) tries.push_back(std::string(appData) + "\\heroic");
	for (size_t i = 0; i < tries.size(); i++) {
		bool dup = false;
		for (size_t k = 0; k < out.size(); k++) dup |= out[k] == tries[i];
		if (!dup && Store_IsDir(tries[i])) out.push_back(tries[i]);
	}
}

void Store_HeroicLibrary(const std::string& heroicDir, const char* file,
                         std::map<std::string, HeroicInfo>& out)
{
	std::string text;
	if (!Store_ReadFile(heroicDir + "/store_cache/" + file, text)) return;
	// Epic's list sits under "library", GOG's under "games".
	size_t arr = Json_FindArray(text, "library");
	if (arr == std::string::npos) arr = Json_FindArray(text, "games");
	if (arr == std::string::npos) return;
	std::vector<std::string> games = Json_SplitArray(text, arr);
	for (size_t i = 0; i < games.size(); i++) {
		std::string id = Store_JsonString(games[i], "app_name");
		if (id.empty()) continue;
		HeroicInfo info;
		info.title = Store_JsonString(games[i], "title");
		info.art = Store_JsonString(games[i], "art_square");
		if (info.art.empty()) info.art = Store_JsonString(games[i], "art_cover");
		if (info.art.compare(0, 8, "https://") != 0) info.art.clear();
		out[id] = info;
	}
}
