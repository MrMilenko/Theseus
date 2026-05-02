// d3d_compat.cpp -- Symbols needed for linking with clang + lld-link
//
// The D3D "fast-path" functions (GetBackBuffer2, Lock2, etc.) ARE in
// d3d8d.lib -- they're real implementations, not LTCG-generated.
// We do NOT stub them; the linker pulls them from the lib directly.
//
// We only need to provide symbols that the lib references but doesn't define.

#include "std.h"

// D3D__pDevice is defined in d3d8d.lib (globals.obj).
// Do NOT redefine it here -- duplicate causes /force:multiple conflict.

extern "C" {

// Xbox crypto function -- not in any lib we link against
VOID XCCalcDigest(PUCHAR MsgData, ULONG MsgDataLen, PUCHAR Digest)
{
    ZeroMemory(Digest, 20);
}

} // extern "C"

// driveManager stubs removed -- real toolbox is now in the build.
