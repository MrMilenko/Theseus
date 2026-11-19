// exe_icon.h: the picture inside an .ico file, or the icon a Windows .exe
// or .dll carries. Plain file reading, so it works the same on every OS.

#pragma once

#include <stddef.h>

// .ico, .exe or .dll, by extension.
bool ExeIcon_IsIconSource(const char* path);

// The biggest picture in it as RGBA (w * h * 4 bytes, free() it). NULL when
// there's no icon or it won't decode.
unsigned char* ExeIcon_Load(const char* path, int* w, int* h);

// Same, for an .ico already in memory.
unsigned char* ExeIcon_FromIco(const unsigned char* data, size_t size, int* w, int* h);

// The biggest icon into Configs/icons as dst (a .jpg). False when there's none.
bool ExeIcon_Save(const char* path, const char* dst);
