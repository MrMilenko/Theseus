// minimp3_decode.cpp: isolated compilation unit for minimp3.
//
// Built separately from the rest of the tree so the Xbox XDK headers
// don't collide with minimp3's own type definitions (stdint.h, intrin.h).
// minimp3 is public domain (CC0 1.0): https://github.com/lieff/minimp3

// Provide the fixed-width types minimp3 needs without pulling in
// the system stdint.h (which drags in SSE intrinsics on clang).
typedef signed char    int8_t;
typedef short          int16_t;
typedef int            int32_t;
typedef long long      int64_t;
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;

// CRT headers provide memset/memcpy/memmove and size_t
#include <string.h>
#include <stdlib.h>

// Disable SIMD (Xbox P3 has SSE1 but the intrinsics headers conflict)
// Disable stdint.h include (we provided types above)
#define MINIMP3_NO_SIMD
#define MINIMP3_NO_STDINT
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

// Stdcall-compatible wrappers for the minimp3 API.
// minimp3 functions are cdecl (extern "C"), but Theseus compiles with
// stdcall as the default calling convention. These wrappers bridge
// the calling convention so the linker and runtime agree.
void __stdcall mp3dec_init_s(mp3dec_t *dec)
{
    mp3dec_init(dec);
}

int __stdcall mp3dec_decode_frame_s(mp3dec_t *dec, const uint8_t *mp3,
    int mp3_bytes, mp3d_sample_t *pcm, mp3dec_frame_info_t *info)
{
    return mp3dec_decode_frame(dec, mp3, mp3_bytes, pcm, info);
}
