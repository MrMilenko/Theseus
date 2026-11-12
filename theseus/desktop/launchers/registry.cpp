// registry.cpp: static launcher registry. Each provider module calls
// Launcher_Register() once at startup from its own RegisterFoo();
// Launchers_RegisterAll() invokes those. A launcher claims a spec by
// scheme/shape and rewrites it into a spawnable command; anything no
// module claims falls through to an identity passthrough.

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

const Launcher* Launcher_FindForSpec(const char* spec) {
	if (!spec || !*spec) return 0;
	// Walk by priority, lower runs first, so a scheme-specific module
	// claims ahead of any broader one.
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

void Launcher_Build(const char* spec, char* outCmd, size_t outSize) {
	if (!outCmd || outSize == 0) return;
	const Launcher* l = Launcher_FindForSpec(spec);
	if (l && l->Build && l->Build(spec, outCmd, outSize)) return;
	// Identity fallback: spec is already a spawnable command.
	if (!spec) { outCmd[0] = '\0'; return; }
	size_t n = strlen(spec);
	if (n >= outSize) n = outSize - 1;
	memcpy(outCmd, spec, n);
	outCmd[n] = '\0';
}

// Each module's RegisterFoo lives in its own .cpp.
extern void Launcher_RegisterRetroArch();
extern void Launcher_RegisterXemu();

void Launchers_RegisterAll() {
	Launcher_RegisterRetroArch();
	Launcher_RegisterXemu();
}
