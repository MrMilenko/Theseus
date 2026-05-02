// discmanager.h: desktop disc manager API. Mostly a stub layer on
// the desktop side; the real disc work lives in xbox/disc_management.cpp.

#pragma once

#ifndef _MSC_VER
#include <cstdint>
#else
typedef unsigned __int64 uint64_t;
#endif

class DiscManager {
public:
    static bool IsDiscPresent();
    static bool ReadDiscTitle(WCHAR* outBuf, size_t maxLen);
    static bool EstimateDiscSize(uint64_t* outSizeBytes);
    static bool StartExtraction(const char* extractPath);
    static bool IsExtractionComplete();
    static float GetExtractionProgress();
	static const char *GetFinalExtractPath();
    static bool BuildXISO(const char* extractPath, const char* isoPath);
    static bool IsBuildComplete();
    static float GetBuildProgress();
    static const WCHAR* GetTitleName();
    static const char* GetIsoTargetPath();
};
