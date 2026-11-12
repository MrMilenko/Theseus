// udata_synth.cpp: write Library/UDATA/<TitleID>/TitleMeta.xbx for every
// Title Maker entry so the dashboard's Memory section renders a title
// pod per game even when the user doesn't have a qcow2 image mounted.
// Format mirrors what ParseXboxMetaValue (desktop_nodes.cpp) reads:
// UTF-16 LE encoded INI text with a [default] section containing
// `TitleName=<name>`.

#include "app_paths.h"
#include "udata_synth.h"
#include "virtual_games.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <stdint.h>
#include "host.h"
#define UDS_MKDIR(p) Host_MakeDir(p)

namespace {

// Recursive mkdir-p, walking forward slashes. Used so we can build
// Library/UDATA/<TitleID>/ in one shot regardless of which segments
// the host filesystem already has.
static void MkdirP(const char* path) {
	char buf[1024];
	strncpy(buf, path, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	for (char* p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			UDS_MKDIR(buf);
			*p = '/';
		}
	}
	UDS_MKDIR(buf);
}

// Encode a single UTF-16 LE codepoint into outBuf at outPos. Surrogate
// pairs are written as two halves; codepoints outside the BMP get
// dropped (rare for game titles, and the dashboard atlas can't render
// them anyway). Advances *outPos.
static void WriteUtf16LE(unsigned int cp, unsigned char* outBuf, size_t* outPos, size_t outSize) {
	if (cp >= 0x10000) {
		// Surrogate pair.
		if (*outPos + 4 > outSize) return;
		cp -= 0x10000;
		uint16_t hi = 0xD800 | (cp >> 10);
		uint16_t lo = 0xDC00 | (cp & 0x3FF);
		outBuf[(*outPos)++] = (unsigned char)(hi & 0xFF);
		outBuf[(*outPos)++] = (unsigned char)(hi >> 8);
		outBuf[(*outPos)++] = (unsigned char)(lo & 0xFF);
		outBuf[(*outPos)++] = (unsigned char)(lo >> 8);
		return;
	}
	if (*outPos + 2 > outSize) return;
	outBuf[(*outPos)++] = (unsigned char)(cp & 0xFF);
	outBuf[(*outPos)++] = (unsigned char)((cp >> 8) & 0xFF);
}

// Append a UTF-8 string to outBuf as UTF-16 LE.
static void AppendUtf8AsUtf16(const char* s, unsigned char* outBuf, size_t* outPos, size_t outSize) {
	const unsigned char* p = (const unsigned char*)s;
	while (*p) {
		unsigned int cp = 0;
		int consumed = 1;
		if (*p < 0x80) {
			cp = *p;
		} else if ((*p & 0xE0) == 0xC0 && p[1]) {
			cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
			consumed = 2;
		} else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
			cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			consumed = 3;
		} else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
			cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
			     ((p[2] & 0x3F) << 6)  |  (p[3] & 0x3F);
			consumed = 4;
		} else {
			cp = 0xFFFD;
		}
		WriteUtf16LE(cp, outBuf, outPos, outSize);
		p += consumed;
	}
}

// Write Library/UDATA/<titleID>/TitleMeta.xbx. Format mirrors the
// shape ParseXboxMetaValue reads: UTF-16 LE BOM, INI-style with a
// [default] section. We don't bother with per-language sections;
// CSavedGameGrid falls back to [default] when the user's locale
// section is missing (which is always, for our synthesis).
static bool WriteTitleMeta(const char* titleID, const char* name) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", AppPathf("Library/UDATA/%s", titleID));
	MkdirP(dir);

	char path[512];
	snprintf(path, sizeof(path), "%s/TitleMeta.xbx", dir);

	unsigned char buf[2048];
	size_t pos = 0;
	// BOM
	buf[pos++] = 0xFF;
	buf[pos++] = 0xFE;
	AppendUtf8AsUtf16("[default]\r\nTitleName=", buf, &pos, sizeof(buf));
	AppendUtf8AsUtf16(name, buf, &pos, sizeof(buf));
	AppendUtf8AsUtf16("\r\n", buf, &pos, sizeof(buf));

	FILE* fp = fopen(path, "wb");
	if (!fp) {
		fprintf(stderr, "[udata_synth] failed to open %s for write\n", path);
		return false;
	}
	fwrite(buf, 1, pos, fp);
	fclose(fp);
	return true;
}

} // namespace

int UDataSynth_RebuildAll() {
	int touched = 0;
	for (int i = 0; i < g_vgames.count; i++) {
		const VirtualGame& g = g_vgames.games[i];
		if (!g.valid || !g.titleID[0] || !g.name[0]) continue;

		// Skip if a TitleMeta.xbx already exists. User-supplied save
		// metadata wins over our synthesized stub.
		char metaPath[512];
		snprintf(metaPath, sizeof(metaPath), "%s", AppPathf("Library/UDATA/%s/TitleMeta.xbx", g.titleID));
		struct stat st;
		if (stat(metaPath, &st) == 0) continue;

		if (WriteTitleMeta(g.titleID, g.name)) touched++;
	}
	if (touched > 0) {
		fprintf(stderr, "[udata_synth] synthesized %d title meta entr%s\n",
		        touched, touched == 1 ? "y" : "ies");
	}
	return touched;
}
