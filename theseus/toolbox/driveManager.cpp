// driveManager.cpp: drive enumeration and per-volume bookkeeping.

#include "driveManager.h"
#include "pointerVector.h"
#include "fileSystem.h"
#include "stringUtility.h"
#include "Drive.h"

namespace
{
	bool mInitialized;
	bool mAllMounted;
	pointerVector<drive*>* m_drives = NULL;
}

bool driveManager::getTotalNumberOfBytes(const char* mountPoint, uint64_t& totalSize)
{
	driveManager::mountDrive(mountPoint);

	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		if (strcmp(currentDrive->getMountPoint(), mountPoint) == 0) 
		{
			totalSize = (uint64_t)currentDrive->getTotalNumberOfBytes();
			return true;
		}
	}
	return false;
}

bool driveManager::getTotalFreeNumberOfBytes(const char* mountPoint, uint64_t& totalFree)
{
	driveManager::mountDrive(mountPoint);

	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* currentMountPoint = currentDrive->getMountPoint();
		if (strcmp(currentMountPoint, mountPoint) == 0) 
		{
			totalFree = (uint64_t)currentDrive->getTotalFreeNumberOfBytes();
			free(currentMountPoint);
			return true;
		}
		free(currentMountPoint);
	}
	return false;
}

pointerVector<char*>* driveManager::getMountedDrives()
{
	driveManager::mountAllDrives();

	pointerVector<char*>* drives = new pointerVector<char*>(false);
	for (uint32_t i = 0; i < m_drives->count(); i++)
	{
		drive* currentDrive = m_drives->get(i);
		if (currentDrive->isMounted()) 
		{
			drives->add(currentDrive->getMountPoint());
		}
	}
	return drives;
}

char* driveManager::mapFtpPath(const char* path)
{
	char* tempPath = stringUtility::leftTrim(path, '/');
	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* mountPointAlias = currentDrive->getMountPointAlias();
		if (stringUtility::startsWith(tempPath, mountPointAlias, true)) 
		{
			currentDrive->mount();
			char* localFolder = stringUtility::substr(tempPath, strlen(mountPointAlias), -1);
			char* mountPoint = currentDrive->getMountPoint();
			char* localPath = stringUtility::formatString("%s:%s", mountPoint, localFolder);
			char* result = stringUtility::replace(localPath, "/", "\\");
			free(localFolder);
			free(mountPoint);
			free(localPath);
			free(tempPath);
			free(mountPointAlias);
			return result;
		}
		free(mountPointAlias);
	}
	free(tempPath);
	return strdup(path);
}

bool driveManager::ftpPathMounted(const char* path)
{
	char* tempPath = stringUtility::leftTrim(path, '/');
	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* mountPointAlias = currentDrive->getMountPointAlias();
		if (stringUtility::startsWith(tempPath, mountPointAlias, true))
		{
			currentDrive->mount();
			free(tempPath);
			free(mountPointAlias);
			return currentDrive->isMounted() == true;
		}
		free(mountPointAlias);
	}
	free(tempPath);
	return false;
}

char* driveManager::convertMountPointToSystemPath(const char* path)
{
	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* temp = currentDrive->getMountPoint();
		if (stringUtility::startsWith(path, temp, true))
		{
			char* tempSystemPath = currentDrive->getSystemPath();
			char* localFolder = stringUtility::substr(path, strlen(temp) + 1, -1);
			char* localPath = stringUtility::formatString("%s%s", tempSystemPath, localFolder);
			free(localFolder);
			free(tempSystemPath);
			free(temp);
			return localPath;
		}
		free(temp);
	}
	return strdup(path);
}

char* driveManager::convertMountPointToMountPointAlias(const char* path)
{
	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* temp = currentDrive->getMountPoint();
		if (stringUtility::startsWith(path, temp, true))
		{
			char* tempMountPointAlias = currentDrive->getMountPointAlias();
			char* localFolder = stringUtility::substr(path, strlen(temp), -1);
			char* localPath = stringUtility::formatString("%s%s", tempMountPointAlias, localFolder);
			free(localFolder);
			free(tempMountPointAlias);
			free(temp);
			return localPath;
		}
		free(temp);
	}
	return strdup(path);
}

char* driveManager::convertMountPointAliasToMountPoint(const char* path)
{
	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* temp = currentDrive->getMountPointAlias();
		if (stringUtility::startsWith(path, temp, true))
		{
			char* tempMountPoint = currentDrive->getMountPoint();
			char* localFolder = stringUtility::substr(path, strlen(temp), -1);
			char* localPath = stringUtility::formatString("%s%s", tempMountPoint, localFolder);
			free(localFolder);
			free(tempMountPoint);
			free(temp);
			return localPath;
		}
		free(temp);
	}
	return strdup(path);
}

char* driveManager::getDrivePartition(const char* drvLtr) {
	for (size_t i = 0; i < m_drives->count(); i++) 
	{
		drive* currentDrive = m_drives->get(i);
		char* mnt = currentDrive->getMountPoint();
		if(stricmp(drvLtr, mnt) == 0) {
			free(mnt);
			return currentDrive->getSystemPath();
		}
		free(mnt);
	}
	return strdup("");
}

void driveManager::init()
{
	if (mInitialized == false)
	{
		m_drives = new pointerVector<drive*>(true);

		m_drives->add(new drive("DVD-ROM", "DVD-ROM", "\\Device\\Cdrom0", driveTypeCdRom));

		m_drives->add(new drive("HDD0-C", "HDD0-C", "\\Device\\Harddisk0\\Partition2", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-E", "HDD0-E", "\\Device\\Harddisk0\\Partition1", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-F", "HDD0-F", "\\Device\\Harddisk0\\Partition6", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-G", "HDD0-G", "\\Device\\Harddisk0\\Partition7", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-H", "HDD0-H", "\\Device\\Harddisk0\\Partition8", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-I", "HDD0-I", "\\Device\\Harddisk0\\Partition9", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-J", "HDD0-J", "\\Device\\Harddisk0\\Partition10", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-K", "HDD0-K", "\\Device\\Harddisk0\\Partition11", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-L", "HDD0-L", "\\Device\\Harddisk0\\Partition12", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-M", "HDD0-M", "\\Device\\Harddisk0\\Partition13", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-N", "HDD0-N", "\\Device\\Harddisk0\\Partition14", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-X", "HDD0-X", "\\Device\\Harddisk0\\Partition3", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-Y", "HDD0-Y", "\\Device\\Harddisk0\\Partition4", driveTypeHardDrive));
		m_drives->add(new drive("HDD0-Z", "HDD0-Z", "\\Device\\Harddisk0\\Partition5", driveTypeHardDrive));

		m_drives->add(new drive("HDD1-C", "HDD1-C", "\\Device\\Harddisk1\\Partition2", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-E", "HDD1-E", "\\Device\\Harddisk1\\Partition1", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-F", "HDD1-F", "\\Device\\Harddisk1\\Partition6", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-G", "HDD1-G", "\\Device\\Harddisk1\\Partition7", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-H", "HDD1-H", "\\Device\\Harddisk1\\Partition8", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-I", "HDD1-I", "\\Device\\Harddisk1\\Partition9", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-J", "HDD1-J", "\\Device\\Harddisk1\\Partition10", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-K", "HDD1-K", "\\Device\\Harddisk1\\Partition11", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-L", "HDD1-L", "\\Device\\Harddisk1\\Partition12", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-M", "HDD1-M", "\\Device\\Harddisk1\\Partition13", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-N", "HDD1-N", "\\Device\\Harddisk1\\Partition14", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-X", "HDD1-X", "\\Device\\Harddisk1\\Partition3", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-Y", "HDD1-Y", "\\Device\\Harddisk1\\Partition4", driveTypeHardDrive));
		m_drives->add(new drive("HDD1-Z", "HDD1-Z", "\\Device\\Harddisk1\\Partition5", driveTypeHardDrive));

		// MU handling is done by Theseus (Memory.cpp), not the Toolbox.
		// Letters H-N are used for Cerbios extended partitions 8-14.

		//https://github.com/brentdc-nz/xbmc4xbox/blob/13cf4fbab8d70b154941a6b91e101bd05cc5b111/xbmc/utils/MemoryUnitManager.cpp#L77

		mInitialized = true;
	}
}

bool driveManager::mountDrive(const char* driveLetter)
{
	init();

	for (size_t i = 0; i < m_drives->count(); i++)
	{
		drive* currentDrive = m_drives->get(i);
		char* mountPoint = currentDrive->getMountPoint();

		if (stringUtility::equals(mountPoint, driveLetter, true) == true)
		{
			free(mountPoint);
			return currentDrive->mount();
		}
		free(mountPoint);
	}
	return false;
}

void driveManager::mountAllDrives()
{
	init();

	for (size_t i = 0; i < m_drives->count(); i++)
	{
		drive* currentDrive = m_drives->get(i);
		currentDrive->mount();
	}
	mAllMounted = true;
}

void driveManager::unmountAllDrives()
{
	for (size_t i = 0; i < m_drives->count(); i++)
	{
		drive* currentDrive = m_drives->get(i);
		currentDrive->unmount();
	}
	mAllMounted = false;
}

bool driveManager::isAllMounted()
{
	return mAllMounted;
}