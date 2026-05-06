// path_template.cpp: $VAR / ${VAR} expansion for launch templates.
// Substitutes a small set of well-known variables read from the
// desktop.ini globals defined in sdl_main.cpp.
//
// Adding a new variable:
//   1. Add an entry to s_vars[] below mapping the name to the
//      address of the existing extern.
//   2. Make sure the extern is declared at the top of this file.
//
// Once the launcher-module registry lands (launchers/registry.cpp),
// each module will call a Register-style API instead of editing
// this table; for now we hardcode the existing dispatcher knobs.

#include "path_template.h"

#include <cstdio>
#include <cstring>
#include <cctype>

// Globals defined in sdl_main.cpp. Each is a NUL-terminated path; an
// empty value is fine and produces an empty substitution.
extern char s_xemuPath[512];
extern char s_steamPath[512];
extern char g_qcowPath[512];
extern char g_romsDir[512];

namespace {
struct VarEntry {
	const char* name;
	const char* (*resolve)();
};

static const char* ResolveXemuPath()  { return s_xemuPath; }
static const char* ResolveSteamPath() { return s_steamPath; }
static const char* ResolveQcowPath()  { return g_qcowPath; }
static const char* ResolveRomsDir()   { return g_romsDir; }

static const VarEntry s_vars[] = {
	{ "XEMU_PATH",  ResolveXemuPath  },
	{ "STEAM_PATH", ResolveSteamPath },
	{ "QCOW_PATH",  ResolveQcowPath  },
	{ "ROMS_DIR",   ResolveRomsDir   },
};

static bool IsVarChar(char c) {
	return isalnum((unsigned char)c) || c == '_';
}

static const char* LookupVar(const char* name, size_t nameLen) {
	for (size_t i = 0; i < sizeof(s_vars) / sizeof(s_vars[0]); i++) {
		if (strlen(s_vars[i].name) == nameLen &&
		    strncmp(s_vars[i].name, name, nameLen) == 0) {
			const char* val = s_vars[i].resolve();
			return val ? val : "";
		}
	}
	return 0; // unknown -- caller passes through original token
}

static bool AppendStr(char* out, size_t outSize, size_t* pos, const char* s, size_t n) {
	if (*pos + n >= outSize) return false;
	memcpy(out + *pos, s, n);
	*pos += n;
	out[*pos] = '\0';
	return true;
}
} // namespace

int PathTemplate_Expand(const char* in, char* out, size_t outSize) {
	if (!in || !out || outSize == 0) return -1;
	size_t pos = 0;
	out[0] = '\0';

	const char* p = in;
	while (*p) {
		if (*p == '$') {
			const char* nameStart = 0;
			size_t      nameLen   = 0;
			const char* tokenEnd  = 0;
			bool        braced    = false;

			if (p[1] == '{') {
				const char* end = strchr(p + 2, '}');
				if (end) {
					nameStart = p + 2;
					nameLen   = (size_t)(end - nameStart);
					tokenEnd  = end + 1;
					braced    = true;
				}
			} else if (IsVarChar(p[1]) && !isdigit((unsigned char)p[1])) {
				const char* q = p + 1;
				while (IsVarChar(*q)) q++;
				nameStart = p + 1;
				nameLen   = (size_t)(q - nameStart);
				tokenEnd  = q;
			}

			if (nameStart && nameLen > 0) {
				const char* val = LookupVar(nameStart, nameLen);
				if (val) {
					if (!AppendStr(out, outSize, &pos, val, strlen(val)))
						return (int)pos;
					p = tokenEnd;
					continue;
				}
				// Unknown var -- pass token through verbatim so users
				// can spot typos in the launch log.
				size_t tokenLen = (size_t)(tokenEnd - p);
				if (!AppendStr(out, outSize, &pos, p, tokenLen))
					return (int)pos;
				p = tokenEnd;
				(void)braced;
				continue;
			}
			// Lone $ or $<digit> -- copy as literal.
		}
		if (pos + 1 >= outSize) return (int)pos;
		out[pos++] = *p++;
		out[pos] = '\0';
	}
	return (int)pos;
}
