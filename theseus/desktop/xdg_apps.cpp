// xdg_apps.cpp: .desktop entries, read the way the freedesktop Desktop
// Entry spec lays them out.

#include "xdg_apps.h"
#include "virtual_games.h"
#include "icon_compress.h"
#include "exe_icon.h"

#include <algorithm>
#include <ctype.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <time.h>

#include "host.h"
#include "platform_env.h"

namespace {

bool Exists(const std::string& p) {
	HostFileInfo info;
	return Host_Stat(p.c_str(), &info);
}

bool HasExt(const std::string& s, const char* ext) {
	size_t e = strlen(ext);
	return s.size() > e && strcasecmp(s.c_str() + s.size() - e, ext) == 0;
}

std::string Env(const char* name, const std::string& fallback) {
	const char* v = getenv(name);
	return v && *v ? std::string(v) : fallback;
}

void AddUnique(std::vector<std::string>& v, const std::string& s) {
	if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

// The data folders: XDG_DATA_HOME, then XDG_DATA_DIRS, then the Flatpak
// exports, which a session started outside Flatpak's profile script misses.
void DataDirs(std::vector<std::string>& out) {
	std::string home = PlatformHomeDir();
	AddUnique(out, Env("XDG_DATA_HOME", home.empty() ? "" : home + "/.local/share"));
	std::string dirs = Env("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
	size_t at = 0;
	while (at <= dirs.size()) {
		size_t end = dirs.find(':', at);
		if (end == std::string::npos) end = dirs.size();
		std::string d = dirs.substr(at, end - at);
		while (d.size() > 1 && d[d.size() - 1] == '/') d.erase(d.size() - 1);
		AddUnique(out, d);
		at = end + 1;
	}
	if (!home.empty()) AddUnique(out, home + "/.local/share/flatpak/exports/share");
	AddUnique(out, "/var/lib/flatpak/exports/share");
}

// A value with the spec's escapes undone: \s \n \t \r \\.
std::string Unescape(const std::string& v) {
	std::string o;
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] != '\\' || i + 1 >= v.size()) { o += v[i]; continue; }
		char c = v[++i];
		o += c == 's' ? ' ' : c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
	}
	return o;
}

// Exec split into arguments. Inside quotes a backslash keeps the next ", `, $ or \.
std::vector<std::string> SplitExec(const std::string& exec) {
	std::vector<std::string> args;
	std::string cur;
	bool in = false, quoted = false;
	for (size_t i = 0; i < exec.size(); i++) {
		char c = exec[i];
		if (quoted) {
			if (c == '\\' && i + 1 < exec.size()) cur += exec[++i];
			else if (c == '"') quoted = false;
			else cur += c;
		} else if (c == '"') {
			quoted = in = true;
		} else if (c == ' ' || c == '\t') {
			if (in) { args.push_back(cur); cur.clear(); in = false; }
		} else {
			cur += c;
			in = true;
		}
	}
	if (in) args.push_back(cur);
	return args;
}

// Double quotes, like every other Launch line, with what sh still reads in them escaped.
std::string ShellQuote(const std::string& a) {
	bool plain = !a.empty();
	for (size_t i = 0; i < a.size() && plain; i++) {
		char c = a[i];
		plain = isalnum((unsigned char)c) || strchr("_./:=+,@%-", c);
	}
	if (plain) return a;
	std::string o = "\"";
	for (size_t i = 0; i < a.size(); i++) {
		if (strchr("\"\\$`", a[i])) o += '\\';
		o += a[i];
	}
	return o + "\"";
}

void ScanDir(const std::string& dir, int depth, std::vector<std::string>& seenIds,
             const std::string& idPrefix, std::vector<XdgApp>& out) {
	HostDir* d = Host_OpenDir(dir.c_str());
	if (!d) return;
	char name[512];
	bool isDir = false;
	std::vector<std::pair<std::string, bool> > entries;
	while (entries.size() < 20000 && Host_ReadDirType(d, name, sizeof(name), &isDir))
		if (name[0] != '.') entries.push_back(std::make_pair(std::string(name), isDir));
	Host_CloseDir(d);
	for (size_t i = 0; i < entries.size(); i++) {
		std::string full = dir + "/" + entries[i].first;
		if (entries[i].second) {
			if (depth < 3) ScanDir(full, depth + 1, seenIds, idPrefix + entries[i].first + "-", out);
			continue;
		}
		if (!HasExt(entries[i].first, ".desktop")) continue;
		// The same id further down the list is hidden by the first one.
		std::string id = idPrefix + entries[i].first;
		if (std::find(seenIds.begin(), seenIds.end(), id) != seenIds.end()) continue;
		seenIds.push_back(id);
		XdgApp app;
		if (XdgApps_Parse(full.c_str(), &app)) out.push_back(app);
	}
}

} // namespace

void XdgApps_Dirs(std::vector<std::string>& out) {
	out.clear();
	std::vector<std::string> data;
	DataDirs(data);
	for (size_t i = 0; i < data.size(); i++) AddUnique(out, data[i] + "/applications");
	AddUnique(out, "/var/lib/snapd/desktop/applications");
	// Shortcuts people keep on the Desktop.
	std::string home = PlatformHomeDir();
	if (!home.empty()) AddUnique(out, Env("XDG_DESKTOP_DIR", home + "/Desktop"));
}

bool XdgApps_Parse(const char* file, XdgApp* out) {
	FILE* f = fopen(file, "r");
	if (!f) return false;
	*out = XdgApp();
	out->path = file;
	std::string type, categories;
	bool inEntry = false, hidden = false, terminal = false;
	char line[4096];
	while (fgets(line, sizeof(line), f)) {
		std::string l = line;
		while (!l.empty() && (l[l.size() - 1] == '\n' || l[l.size() - 1] == '\r')) l.erase(l.size() - 1);
		if (l.empty() || l[0] == '#') continue;
		if (l[0] == '[') { inEntry = l == "[Desktop Entry]"; continue; }
		if (!inEntry) continue;
		size_t eq = l.find('=');
		if (eq == std::string::npos) continue;
		std::string key = l.substr(0, eq), val = l.substr(eq + 1);
		while (!key.empty() && key[key.size() - 1] == ' ') key.erase(key.size() - 1);
		while (!val.empty() && val[0] == ' ') val.erase(0, 1);
		val = Unescape(val);
		// Keys are case sensitive in the spec; hand written files aren't always.
		const char* k = key.c_str();
		if      (!strcasecmp(k, "Type"))       type = val;
		else if (!strcasecmp(k, "Name"))       out->name = val;
		else if (!strcasecmp(k, "Exec"))       out->exec = val;
		else if (!strcasecmp(k, "Icon"))       out->icon = val;
		else if (!strcasecmp(k, "Categories")) categories = val;
		else if (!strcasecmp(k, "Path"))       out->workdir = val;
		else if (!strcasecmp(k, "Hidden"))     hidden = !strcasecmp(val.c_str(), "true");
		else if (!strcasecmp(k, "NoDisplay"))  hidden = hidden || !strcasecmp(val.c_str(), "true");
		else if (!strcasecmp(k, "Terminal"))   terminal = !strcasecmp(val.c_str(), "true");
	}
	fclose(f);
	std::string cats = ";" + categories + ";";
	out->game = cats.find(";Game;") != std::string::npos;
	if (!type.empty() && type != "Application") return false;
	return !hidden && !terminal && !out->name.empty() && !out->exec.empty();
}

void XdgApps_Scan(const std::vector<std::string>& dirs, std::vector<XdgApp>& out) {
	out.clear();
	std::vector<std::string> seen;
	for (size_t i = 0; i < dirs.size(); i++) ScanDir(dirs[i], 0, seen, "", out);
	std::stable_sort(out.begin(), out.end(), [](const XdgApp& a, const XdgApp& b) {
		if (a.game != b.game) return a.game;
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
}

std::string XdgApps_Command(const XdgApp& app) {
	std::vector<std::string> args = SplitExec(app.exec);
	// Steam's own shortcuts: the URL alone lets the launcher watch the game.
	if (args.size() == 2 && args[0] == "steam" && args[1].compare(0, 8, "steam://") == 0) return args[1];
	std::string cmd;
	for (size_t i = 0; i < args.size(); i++) {
		const std::string& a = args[i];
		// Flatpak wraps the file arguments in @@ markers; there are no files.
		if (a == "@@" || a == "@@u" || a == "@@f") continue;
		std::string o;
		bool dropped = false;
		for (size_t j = 0; j < a.size(); j++) {
			if (a[j] != '%' || j + 1 >= a.size()) { o += a[j]; continue; }
			char c = a[++j];
			if (c == '%')      o += '%';
			else if (c == 'c') o += app.name;
			else if (c == 'k') o += app.path;
			else dropped = true;   // %f %u %i and the old ones: nothing to hand over
		}
		if (dropped && o.empty()) continue;
		if (!cmd.empty()) cmd += ' ';
		cmd += ShellQuote(o);
	}
	return cmd;
}

std::string XdgApps_IconFile(const std::string& icon) {
	if (icon.empty()) return "";
	static const char* const kDrawable[] = { ".png", ".jpg", ".jpeg", ".bmp", ".ico", NULL };
	if (icon[0] == '/') {
		for (int i = 0; kDrawable[i]; i++)
			if (HasExt(icon, kDrawable[i])) return Exists(icon) ? icon : "";
		return "";
	}
	std::string name = HasExt(icon, ".png") ? icon.substr(0, icon.size() - 4) : icon;
	std::vector<std::string> bases;
	std::string home = PlatformHomeDir();
	if (!home.empty()) AddUnique(bases, home + "/.icons");
	std::vector<std::string> data;
	DataDirs(data);
	for (size_t i = 0; i < data.size(); i++) AddUnique(bases, data[i] + "/icons");
	static const char* const kSizes[] = {
		"512x512", "256x256", "192x192", "128x128", "96x96", "72x72", "64x64", "48x48", "32x32", NULL
	};
	// Biggest first, across every folder, so a 256 in /usr/share beats a 48 in yours.
	for (int s = 0; kSizes[s]; s++)
		for (size_t b = 0; b < bases.size(); b++) {
			std::string p = bases[b] + "/hicolor/" + kSizes[s] + "/apps/" + name + ".png";
			if (Exists(p)) return p;
		}
	for (size_t i = 0; i < data.size(); i++) {
		std::string p = data[i] + "/pixmaps/" + name + ".png";
		if (Exists(p)) return p;
	}
	return "";
}

bool XdgApps_Add(const XdgApp& app, const char* category) {
	extern int Title_SanitizeName(const char*, char*, size_t);
	char clean[128];
	Title_SanitizeName(app.name.c_str(), clean, sizeof(clean));
	std::string cmd = XdgApps_Command(app);
	if (!clean[0] || cmd.empty() || cmd.size() >= sizeof(VirtualGame::launch)) return false;
	if (VGames_FindByName(clean) >= 0) return false;
	for (int i = 0; i < g_vgames.count; i++)
		if (g_vgames.games[i].valid && cmd == g_vgames.games[i].launch) return false;
	// Several added in the same second still need their own ids.
	static unsigned s_bump = 0;
	char id[16];
	snprintf(id, sizeof(id), "%08x", (unsigned)time(NULL) + s_bump++);
	int row = VGames_Add(clean, id, cmd.c_str(), "E", category);
	if (row >= 0) snprintf(g_vgames.games[row].workdir, sizeof(g_vgames.games[row].workdir), "%s", app.workdir.c_str());
	std::string icon = XdgApps_IconFile(app.icon);
	if (!icon.empty()) {
		char dst[1024];
		Host_MakeDir(VGAMES_ICONS);
		snprintf(dst, sizeof(dst), "%s/%s.jpg", VGAMES_ICONS, id);
		if (ExeIcon_IsIconSource(icon.c_str())) ExeIcon_Save(icon.c_str(), dst);
		else Icons_FitFile(icon.c_str(), dst, 256, 90);
	}
	return true;
}
