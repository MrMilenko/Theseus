// disc_manager.h: DiscManager, the DVD-extract-to-XISO pipeline used
// by the panic / recovery ISO loader. Title detection, folder copy,
// XISO build with progress reporting. Theseus-original.

#pragma once

// Type definition for XDK
typedef unsigned __int64 uint64_t;

bool CopyFolderRecursive(const char* sourcePath, const char* destPath, float* progressOut = NULL);
bool BuildXisoFromFolder(const char* sourceFolder, const char* isoPath, float* progressOut = NULL);

class DiscManager {
public:
    static bool IsDiscPresent();
    static bool ReadDiscTitle(WCHAR* outBuf, size_t maxLen);
    static bool EstimateDiscSize(uint64_t* outSizeBytes);
    static bool StartExtraction(const char* extractPath);
    static bool IsExtractionComplete();
    static float GetExtractionProgress();
	static const CHAR *GetFinalExtractPath();
    static bool BuildXISO(const char* extractPath, const char* isoPath);
    static bool IsBuildComplete();
    static float GetBuildProgress();
    static const WCHAR* GetTitleName();
    static const CHAR* GetIsoTargetPath();
};
