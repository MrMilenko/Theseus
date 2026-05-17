// skin_assets.h: canonical allowlist of files a skin folder is
// allowed to override, plus equivalence groups for files that
// changed names across skin-engine generations.
//
// Two purposes:
//
//   1. IsSkinnableAsset gates skin-folder probing. Without it, every
//      texture / mesh reference would fopen the skin dir even though
//      most of them aren't skinnable. The allowlist is the union of
//      filenames actually present across Stock, the commemorative
//      pack (Blue/Green/Red/Purple/Orange/Yellow/Black/White), and
//      the community Cubewall/Wavewall/Fractal/Carbon skins.
//
//   2. SkinCandidatesFor returns the ordered list of filenames to
//      probe in the skin folder for a given request. Two assets can
//      have multiple names because the skin engine was renamed
//      across versions; UI.X-era skins ship the new names while
//      retail XIPs request the old, and vice versa. We probe every
//      member of the equivalence group so a skin can ship just one
//      and satisfy both eras.
//
//      Known groups:
//        cellwall.xm        <->  Inner_cell-FACES.xm
//        cellwall.xbx       <->  shell.xbx
//        GameHilite_01.xbx  <->  menu_hilite.xbx / menu_hilight.xbx
//
// Excluded from the allowlist:
//   - <SkinName>.xbx: handled by the skin INI loader, not here.
//   - DefaultIcon/: folder fallback, not a texture override.
//   - .temp files: authoring artifacts.

#pragma once

#include <cstring>

// Fill `out` with the ordered list of skin-folder filenames to probe
// for `name`. Always starts with `name` itself (basename) so an
// exact-match skin file wins over an alias. Returns the count.
//
// `out` must hold at least `max` slots; 4 is enough for every group
// currently defined.
static inline int SkinCandidatesFor(const char *name, const char **out, int max)
{
	if (!name || !*name || max < 1)
		return 0;

	const char *base = name;
	for (const char *p = name; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}

	// Equivalence groups. Order within a row doesn't matter; we
	// always lead with the requested name and fill with the rest.
	static const char *const kGroups[][4] = {
		{ "cellwall.xm",       "Inner_cell-FACES.xm", NULL,                NULL },
		{ "cellwall.xbx",      "shell.xbx",           NULL,                NULL },
		{ "GameHilite_01.xbx", "menu_hilite.xbx",     "menu_hilight.xbx",  NULL },
	};

	int n = 0;
	out[n++] = base;

	for (size_t i = 0; i < sizeof(kGroups) / sizeof(kGroups[0]); i++) {
		bool inGroup = false;
		for (int j = 0; j < 4 && kGroups[i][j]; j++) {
			if (_stricmp(base, kGroups[i][j]) == 0) {
				inGroup = true;
				break;
			}
		}
		if (!inGroup) continue;

		for (int j = 0; j < 4 && kGroups[i][j] && n < max; j++) {
			if (_stricmp(base, kGroups[i][j]) != 0)
				out[n++] = kGroups[i][j];
		}
		break; // a name belongs to at most one group
	}

	return n;
}

// Returns true if `path` (full path or filename) names a file the
// active skin is allowed to override. Case-insensitive on basename.
// Members of an equivalence group all hit through their canonical
// entry. only one name per group needs to be in the list below.
static inline bool IsSkinnableAsset(const char *path)
{
	if (!path || !*path) return false;

	const char *base = path;
	for (const char *p = path; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}

	static const char *const kSkinnable[] = {
		// Textures (.xbx after extension forcing in LoadTexture).
		"cellwall.xbx",      // shell.xbx aliases here
		"dvd_button.xbx",
		"DVD_paneltex.xbx",
		"dvdaudio.xbx",
		"dvdempty.xbx",
		"dvdstop.xbx",
		"dvdstopw.xbx",
		"dvdtitle.xbx",
		"dvdunknown.xbx",
		"dvdvideo.xbx",
		"GameHilite_01.xbx", // menu_hilite / menu_hilight alias here
		"menu_hilite.xbx",
		"menu_hilight.xbx",
		"outline.xbx",
		"screenshot.xbx",
		"shell.xbx",
		"status_gauge.xbx",
		"xbox4.xbx",
		"xboxlogo.xbx",
		"xboxlogo64.xbx",
		"xboxlogo128.xbx",
		"xboxlogow.xbx",
		// Meshes.
		"cellwall.xm",       // Inner_cell-FACES.xm aliases here
		"Inner_cell-FACES.xm",
		0
	};

	for (int i = 0; kSkinnable[i]; i++) {
		if (_stricmp(base, kSkinnable[i]) == 0) return true;
	}
	return false;
}
