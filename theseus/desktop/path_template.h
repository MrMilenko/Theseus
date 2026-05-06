// path_template.h: $VAR expansion for launch templates and stored
// game/app entries. Substitutes a small set of well-known variables
// (xemu path, steam install root, ROMs directory, qcow2 image) so
// launch templates stored in games.ini stay portable when users
// move their tooling around.
//
// Future launcher modules (RetroArch, Dolphin, RPCS3, ...) will
// register their own variables here. For now we hand-roll the set
// that the existing dispatchers care about.
//
// Cross-platform: matches both `$XEMU_PATH` and `${XEMU_PATH}` so
// users can disambiguate when a variable is followed by alphanumerics.

#pragma once

#include <stddef.h>

// Expand `$VAR` and `${VAR}` tokens in `in` and write the result into
// `out` (capped at `outSize`). Unknown tokens are passed through
// unchanged so users can spot typos in the launch log instead of
// silently getting an empty string. Returns the number of bytes
// written (excluding NUL), or -1 on argument error.
int PathTemplate_Expand(const char* in, char* out, size_t outSize);
