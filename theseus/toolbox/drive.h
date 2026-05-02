// drive.h: drive class wrapping a single mounted Xbox volume (drive
// letter, partition path, FATX label, mount / dismount, free / total bytes).

#pragma once

#include "xboxinternals.h"
#include <string>
#include <xtl.h>
#include <winsockx.h>

enum driveType
{
	driveTypeHardDrive,
	driveTypeCdRom,
	driveTypeMemoryUnit,
};

class drive
{
public:
	drive(const char* mountPoint, const char* mountPointAlias, const char* systemPath, driveType drive);
	bool mount();  		
	bool unmount();
	bool isMounted();
	char* getMountPoint();
	char* getMountPointAlias();
	char* getSystemPath();
	uint64_t getTotalNumberOfBytes();
	uint64_t getTotalFreeNumberOfBytes();
protected:
	char* mMountPoint;
	char* mMountPointAlias;
	char* mSystemPath;
	bool mMounted;
	driveType mDriveType;
};