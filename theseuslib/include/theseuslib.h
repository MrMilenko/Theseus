// theseuslib.h: shared C library for the Xbox-side dashboard build.
//
// Provides the small set of private-API helper functions the dashboard
// requires at link time but which the public Xbox SDK does not expose.
// The surface is intentionally tiny: 11 functions covering EEPROM writes,
// auto-power-down, cache-partition cleanup, MU naming, recursive directory
// cleanup, and timezone / local-time helpers. Function signatures and
// behavior were derived from binary analysis of the dashboard's import
// table. All functions use the standard Xbox WINAPI (stdcall) convention.

#pragma once

#include <xtl.h>

#ifdef __cplusplus
extern "C" {
#endif

// EEPROM persistent setting writer.
// Wraps ExSaveNonVolatileSetting and translates the NTSTATUS to a Win32
// DOS error code. Counterpart to the public XQueryValue.
DWORD WINAPI XSetValue(ULONG ulValueIndex, ULONG ulType, PVOID pValue, ULONG cbValueLength);

// Auto power-down toggle. The setting is persisted in the XC_MISC_FLAGS
// EEPROM field. When enabled, a six-hour kernel timer fires a hard reboot.
DWORD WINAPI XAutoPowerDownSet(BOOL fAutoPowerDown);
DWORD WINAPI XAutoPowerDownGet(BOOL *pfAutoPowerDown);

// Cache-partition cleanup. Clears the cache database entry for the given
// title id, freeing its slot for reuse. Silently no-ops if the title has
// no cache entry.
VOID WINAPI XapiDeleteCachePartition(DWORD dwTitleId);

// Writes a UTF-16 volume name to the FATX volume metadata of a mounted MU.
// chDrive is the DOS drive letter ('F'..'M' typically). Fails if the MU
// is not currently mounted.
DWORD WINAPI XMUWriteNameToDriveLetter(CHAR chDrive, LPCWSTR lpName);

// Recursive empty-subdirectory cleanup. Walks the immediate children of
// pszDrivePath and removes any that are empty (or contain only files when
// fNukeFiles is TRUE), preserving the optional pszPreserveDir entry.
DWORD WINAPI XapiNukeEmptySubdirs(LPCSTR pszDrivePath, LPCSTR pszPreserveDir, BOOLEAN fNukeFiles);

// Cleans empty title directories from a root-mounted MU drive, preserving
// the optional pszPreserveDir entry. Convenience wrapper for the dashboard's
// MU cleanup flow.
DWORD WINAPI XCleanMUFromRoot(CHAR chDrive, LPCSTR pszPreserveDir);

// Timezone helpers. Used by the dashboard's locale and clock UI to push
// timezone changes back into EEPROM and to set the system clock.
//
// SystemTimeToXboxTimeZoneDate and WstrToXboxTimeZoneName are internal
// helpers used by XapipSetTimeZoneInformation. WstrToXboxTimeZoneName is
// declared as __inline by xconfig.h; we don't redeclare it here.
DWORD WINAPI XapipSetTimeZoneInformation(PTIME_ZONE_INFORMATION TimeZoneInformation);
BOOL WINAPI XapiSetLocalTime(const SYSTEMTIME *lpLocalTime);

// Library build identifier. The dashboard pulls this in for version checks
// in some of the lib code paths.
extern const ULONG XapiBuildNumberP;

#ifdef __cplusplus
}
#endif
