// driveManager.h: enumerates available Xbox volumes (C, E, F, G, X, Y, Z,
// utility partitions) and presents them as a vector of drive objects to
// the rest of the toolbox.

#pragma once

#include "Drive.h"
#include "pointerVector.h"
#include "xboxinternals.h"

#include <string>

class driveManager
{
public:
	static bool getTotalNumberOfBytes(const char* mountPoint, uint64_t& totalSize);
	static bool getTotalFreeNumberOfBytes(const char* mountPoint, uint64_t& totalFree);
	static pointerVector<char*>* getMountedDrives();
	static char* getDrivePartition(const char* drvLtr);
	static bool ftpPathMounted(const char* path);
	static char* mapFtpPath(const char* path);
	static char* convertMountPointToSystemPath(const char* path);
	static char* convertMountPointToMountPointAlias(const char* path);
	static char* convertMountPointAliasToMountPoint(const char* path);
	static void init();
	static bool mountDrive(const char* driveLetter);
	static void mountAllDrives();
	static void unmountAllDrives();
	static bool isAllMounted();
};