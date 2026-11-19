// store_import.cpp: store games into games.ini, with an icon from the disk
// when the game has one and the store's cover art queued when it doesn't.

#include "launchers/stores.h"
#include "virtual_games.h"
#include "udata_synth.h"
#include "icon_compress.h"
#include "exe_icon.h"
#include "game_art.h"
#include "title_sanitize.h"
#include "host.h"

#include <cstdio>
#include <cstring>

// The same game gets the same id every time it comes in.
static void TitleIdFor(const std::string& launch, char* out, size_t size)
{
	unsigned h = 2166136261u;
	for (size_t i = 0; i < launch.size(); i++) { h ^= (unsigned char)launch[i]; h *= 16777619u; }
	for (;;) {
		snprintf(out, size, "%08x", h);
		bool used = false;
		for (int i = 0; i < g_vgames.count && !used; i++)
			used = g_vgames.games[i].valid && strcmp(g_vgames.games[i].titleID, out) == 0;
		if (!used) return;
		h++;
	}
}

static bool PlaceIcon(const std::string& src, const char* titleID)
{
	HostFileInfo info;
	if (src.empty() || !Host_Stat(src.c_str(), &info) || (info.attrs & HOST_ATTR_DIR)) return false;
	char dst[1024];
	Host_MakeDir(VGAMES_ICONS);
	snprintf(dst, sizeof(dst), "%s/%s.jpg", VGAMES_ICONS, titleID);
	if (ExeIcon_IsIconSource(src.c_str())) return ExeIcon_Save(src.c_str(), dst);
	return Icons_FitFile(src.c_str(), dst, 256, 90);
}

void StoreGames_Add(const std::vector<StoreGame>& games, int* added, int* had)
{
	*added = *had = 0;
	VGames_Load();
	for (size_t i = 0; i < games.size(); i++) {
		const StoreGame& sg = games[i];
		char clean[128];
		Title_SanitizeName(sg.name.c_str(), clean, sizeof(clean));
		if (!clean[0] || sg.launch.empty() || sg.launch.size() >= sizeof(VirtualGame::launch)) continue;
		bool there = VGames_FindByName(clean) >= 0;
		for (int k = 0; k < g_vgames.count && !there; k++)
			there = g_vgames.games[k].valid && sg.launch == g_vgames.games[k].launch;
		if (there) { (*had)++; continue; }

		char id[16];
		TitleIdFor(sg.launch, id, sizeof(id));
		int row = VGames_Add(clean, id, sg.launch.c_str(), "E",
		                     sg.category.empty() ? "Games" : sg.category.c_str());
		if (row < 0) break;
		if (!sg.workdir.empty() && sg.workdir.size() < sizeof(g_vgames.games[row].workdir))
			snprintf(g_vgames.games[row].workdir, sizeof(g_vgames.games[row].workdir), "%s", sg.workdir.c_str());
		if (!PlaceIcon(sg.icon, id) && !sg.artUrl.empty()) GameArt_HintArt(id, sg.artUrl.c_str());
		(*added)++;
	}
	if (*added) {
		VGames_Save();
		UDataSynth_RebuildAll();
	}
}
