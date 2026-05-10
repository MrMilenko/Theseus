// registry.cpp: static launcher registry. Each provider module
// (shell, url, xemu, ...) calls Launcher_Register() once at startup
// from its own RegisterFoo() function; Launchers_RegisterAll()
// invokes those.

#include "launcher.h"

#include <cstring>

namespace {
enum { kMaxLaunchers = 16 };
const Launcher* s_launchers[kMaxLaunchers] = {};
int s_launcherCount = 0;
} // namespace

void Launcher_Register(const Launcher* l) {
	if (!l || !l->id || s_launcherCount >= kMaxLaunchers) return;
	// Skip duplicates (re-registration is idempotent).
	for (int i = 0; i < s_launcherCount; i++) {
		if (s_launchers[i] == l ||
		    (s_launchers[i]->id && strcmp(s_launchers[i]->id, l->id) == 0)) {
			return;
		}
	}
	s_launchers[s_launcherCount++] = l;
}

const Launcher* Launcher_FindByID(const char* id) {
	if (!id) return 0;
	for (int i = 0; i < s_launcherCount; i++) {
		if (s_launchers[i]->id && strcmp(s_launchers[i]->id, id) == 0)
			return s_launchers[i];
	}
	return 0;
}

const Launcher* Launcher_FindForSpec(const char* spec) {
	if (!spec || !*spec) return 0;
	// Walk by priority -- lower runs first. The shell catch-all has
	// the highest priority value so it claims last; URL/xemu/etc.
	// modules sit at lower priorities and short-circuit earlier.
	const Launcher* best = 0;
	int bestPrio = 0;
	for (int i = 0; i < s_launcherCount; i++) {
		const Launcher* l = s_launchers[i];
		if (!l->Claims || !l->Claims(spec)) continue;
		if (!best || l->priority < bestPrio) {
			best = l;
			bestPrio = l->priority;
		}
	}
	return best;
}

void Launcher_Build(const char* spec, const char* typeHint,
                    char* outCmd, size_t outSize) {
	if (!outCmd || outSize == 0) return;
	const Launcher* l = 0;
	if (typeHint && typeHint[0]) l = Launcher_FindByID(typeHint);
	if (!l) l = Launcher_FindForSpec(spec);
	if (l && l->Build && l->Build(spec, outCmd, outSize)) return;
	// Identity fallback: spec is already a shell-ready command.
	if (!spec) { outCmd[0] = '\0'; return; }
	size_t n = strlen(spec);
	if (n >= outSize) n = outSize - 1;
	memcpy(outCmd, spec, n);
	outCmd[n] = '\0';
}

// Forward decls -- each module's RegisterFoo lives in its own .cpp.
extern void Launcher_RegisterShell();
extern void Launcher_RegisterUrl();
extern void Launcher_RegisterXemu();
extern void Launcher_RegisterSteam();
extern void Launcher_RegisterRetroArch();

void Launchers_RegisterAll() {
	// Order matters only for the implicit registration ordering, not
	// for Claims (priority sorting handles that).
	Launcher_RegisterUrl();
	Launcher_RegisterXemu();
	Launcher_RegisterSteam();
	Launcher_RegisterRetroArch();
	Launcher_RegisterShell();
}
