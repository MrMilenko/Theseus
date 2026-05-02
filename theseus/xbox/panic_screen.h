// panic_screen.h: TheseusPanic public entry point. Triggered when
// init fails; renders a D3D8-direct recovery UI and never returns.
// Companion to xbox/panic_screen.cpp.

#pragma once

// std.h must already be included before this header (every .cpp in
// xbox/ does it first; matching the rest of the tree).

// Drop the dashboard into recovery mode. Never returns.
//
// The normal init / main-loop paths are abandoned. The panic screen draws a
// D3D8-direct UI that does not touch the scene graph, the XAP VM, or any
// asset that lives inside a XIP archive (only the bitmap font in Q:\Fonts).
// The toolbox FTP server stays up so missing/broken XIPs can be pushed back
// over the network without reflashing.
//
// `reason` is shown to the user verbatim and dumped to Q:\Logs\panic-*.txt.
// `pEx` is optional; when set its EXCEPTION_RECORD + register CONTEXT are
// also written to the log so post-mortem stack walks have something to work
// with.
void TheseusPanic(const TCHAR* reason, LPEXCEPTION_POINTERS pEx = NULL);
