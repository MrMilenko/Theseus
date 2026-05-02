// theseuslib.c: implementations for the helpers declared in theseuslib.h.
//
// Function signatures and behavior were derived from binary analysis of
// the dashboard's import table; implementations are written from first
// principles against the publicly documented Xbox kernel API surface
// (xboxkrnl exports). Type definitions for EEPROM settings, timezone
// records, and the cache partition database come from <xtl.h> and related
// public XDK headers.

// xtl.h is the umbrella public XDK header. It brings in the standard
// Xbox types (DWORD, HANDLE, NTSTATUS, etc.) and the kernel-level APIs
// the implementations below call (Nt*, Ke*, Ex*, Hal*, Rtl*).
#include <xtl.h>

// xboxp.h provides the private XAPI helpers we forward to: XQueryValue
// for EEPROM reads, HalReturnToFirmware/HalQuickRebootRoutine for the
// auto-power-down shutdown.
#include <xboxp.h>

// xconfig.h provides the EEPROM register layout (XBOX_USER_SETTINGS),
// the EEPROM index constants (XC_MISC_FLAGS, XC_MAX_OS, ...) and the
// kernel non-volatile setting accessor (ExSaveNonVolatileSetting).
#include <xconfig.h>

// FILE_DISPOSITION_INFORMATION has a "DeleteFile" field which collides
// with the Win32 DeleteFile macro under UNICODE builds. Drop the macro
// so we can name the field directly.
#ifdef DeleteFile
#undef DeleteFile
#endif

#include "theseuslib.h"

// =============================================================================
// EEPROM-backed configuration constants used by the EEPROM helpers below.
// These are publicly documented Xbox EEPROM register indices and flag bits.
// =============================================================================

#ifndef XC_MISC_FLAGS
#define XC_MISC_FLAGS                  0x0107
#endif

#ifndef XC_MISC_FLAG_AUTOPOWERDOWN
#define XC_MISC_FLAG_AUTOPOWERDOWN     0x00000001
#endif

#ifndef XC_MAX_OS
#define XC_MAX_OS                      0x000010BB
#endif

// Six hours expressed as a negative count of 100-nanosecond units, which
// is how the kernel timer API consumes relative due times.
#define APD_TIMEOUT_100NS              (-216000000000LL)

// =============================================================================
// XSetValue
// =============================================================================
//
// Persists an EEPROM-backed configuration value. Mirror of the public
// XQueryValue API. The kernel handles the actual signed-area write through
// ExSaveNonVolatileSetting; we just translate the NTSTATUS into the Win32
// DOS error namespace the dashboard expects.

DWORD WINAPI XSetValue(
	ULONG ulValueIndex,
	ULONG ulType,
	PVOID pValue,
	ULONG cbValueLength)
{
	NTSTATUS status = ExSaveNonVolatileSetting(ulValueIndex, ulType, pValue, cbValueLength);
	return RtlNtStatusToDosError(status);
}

// =============================================================================
// XAutoPowerDownSet / XAutoPowerDownGet
// =============================================================================
//
// Six-hour idle auto-power-down. The user-visible setting persists in the
// XC_MISC_FLAGS EEPROM byte. When enabled, a kernel timer is armed for six
// hours; when it expires the DPC fires HalReturnToFirmware to perform a
// hard shutdown.
//
// The dashboard never calls a "reset on activity" entrypoint, so the timer
// is single-shot from the moment XAutoPowerDownSet(TRUE) is called.

typedef struct _APD_STATE {
	KDPC   dpc;
	KTIMER timer;
	BOOL   enabled;
	BOOL   timerInitialized;
	BOOL   lastWriteFailed;
} APD_STATE;

static APD_STATE g_apd = { 0 };

static VOID NTAPI ApdDpcRoutine(
	IN PKDPC Dpc,
	IN PVOID DeferredContext,
	IN PVOID SystemArgument1,
	IN PVOID SystemArgument2)
{
	(void)Dpc;
	(void)DeferredContext;
	(void)SystemArgument1;
	(void)SystemArgument2;
	HalReturnToFirmware(HalQuickRebootRoutine);
}

static VOID ApdEnsureTimerInitialized(VOID)
{
	if (g_apd.timerInitialized)
		return;
	KeInitializeDpc(&g_apd.dpc, ApdDpcRoutine, NULL);
	KeInitializeTimer(&g_apd.timer);
	g_apd.timerInitialized = TRUE;
}

static VOID ApdArmTimer(VOID)
{
	LARGE_INTEGER due;
	due.QuadPart = APD_TIMEOUT_100NS;
	ApdEnsureTimerInitialized();
	KeSetTimer(&g_apd.timer, due, &g_apd.dpc);
}

static VOID ApdCancelTimer(VOID)
{
	if (!g_apd.timerInitialized)
		return;
	KeCancelTimer(&g_apd.timer);
}

DWORD WINAPI XAutoPowerDownSet(BOOL fAutoPowerDown)
{
	DWORD miscFlags = 0;
	ULONG queryType = 0;
	ULONG queryLen = 0;
	DWORD err;

	g_apd.enabled = fAutoPowerDown;

	// Read the existing XC_MISC_FLAGS byte so we can flip just the auto
	// power-down bit and leave any other miscellaneous flags alone. If the
	// read fails we still attempt the write below to keep the state machine
	// converging on the user's intent.
	err = XQueryValue(XC_MISC_FLAGS, &queryType, &miscFlags, sizeof(miscFlags), &queryLen);

	if (fAutoPowerDown) {
		miscFlags |= XC_MISC_FLAG_AUTOPOWERDOWN;
		ApdArmTimer();
	} else {
		miscFlags &= ~XC_MISC_FLAG_AUTOPOWERDOWN;
		ApdCancelTimer();
	}

	if (err == ERROR_SUCCESS) {
		err = XSetValue(XC_MISC_FLAGS, REG_DWORD, &miscFlags, sizeof(miscFlags));
	}

	g_apd.lastWriteFailed = (err != ERROR_SUCCESS);
	return err;
}

DWORD WINAPI XAutoPowerDownGet(BOOL *pfAutoPowerDown)
{
	DWORD err = ERROR_SUCCESS;

	if (pfAutoPowerDown == NULL)
		return ERROR_INVALID_PARAMETER;

	*pfAutoPowerDown = g_apd.enabled;

	// If the previous write failed, retry now so we eventually converge.
	if (g_apd.lastWriteFailed)
		err = XAutoPowerDownSet(g_apd.enabled);

	return err;
}

// =============================================================================
// XapiDeleteCachePartition
// =============================================================================
//
// Removes a title's reservation from the on-disk cache partition database.
// The cache database lives in sector 4 of partition 0 ("Harddisk0\\partition0")
// and is a fixed 512-byte structure: a 4-byte begin signature, a 4-byte
// version field, 496 bytes of cache entries, and a 4-byte end signature.
// The HAL global HalDiskCachePartitionCount tells us how many entries are
// actually populated.
//
// We open the volume directly, read sector 4, find the matching title id
// (if any), zero its slot, and write the sector back. The function is
// declared VOID; failures are silently ignored, matching the dashboard's
// expectations of "best effort cleanup".

#define CACHEDB_SECTOR_INDEX           4
#define CACHEDB_SECTOR_SIZE            512
#define CACHEDB_BEGIN_SIGNATURE        0x97315286UL
#define CACHEDB_END_SIGNATURE          0xAA550000UL
#define CACHEDB_VERSION                2
#define CACHEDB_ENTRY_DATA_SIZE        496

typedef struct _CACHEDB_ENTRY {
	DWORD dwTitleId;
	DWORD fUsed;
} CACHEDB_ENTRY;

#define CACHEDB_MAX_ENTRIES            (CACHEDB_ENTRY_DATA_SIZE / sizeof(CACHEDB_ENTRY))

typedef struct _CACHEDB_SECTOR {
	DWORD beginSignature;
	DWORD version;
	BYTE  data[CACHEDB_ENTRY_DATA_SIZE];
	DWORD endSignature;
} CACHEDB_SECTOR;

static const CHAR g_partition0Path[] = "\\Device\\Harddisk0\\partition0";

VOID WINAPI XapiDeleteCachePartition(DWORD dwTitleId)
{
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK   iosb;
	ANSI_STRING       devName;
	HANDLE            hVolume = NULL;
	NTSTATUS          status;
	LARGE_INTEGER     byteOffset;
	BYTE              sectorBytes[CACHEDB_SECTOR_SIZE];
	CACHEDB_SECTOR   *sector;
	CACHEDB_ENTRY    *entries;
	ULONG             entryCount;
	ULONG             i;

	RtlInitAnsiString(&devName, g_partition0Path);
	InitializeObjectAttributes(&oa, &devName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = NtOpenFile(
		&hVolume,
		SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE,
		&oa,
		&iosb,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_SYNCHRONOUS_IO_ALERT);

	if (!NT_SUCCESS(status))
		return;

	byteOffset.QuadPart = (LONGLONG)CACHEDB_SECTOR_INDEX * CACHEDB_SECTOR_SIZE;

	status = NtReadFile(
		hVolume, NULL, NULL, NULL, &iosb,
		sectorBytes, CACHEDB_SECTOR_SIZE, &byteOffset);

	if (!NT_SUCCESS(status)) {
		NtClose(hVolume);
		return;
	}

	sector = (CACHEDB_SECTOR *)sectorBytes;
	if (sector->beginSignature != CACHEDB_BEGIN_SIGNATURE ||
	    sector->endSignature   != CACHEDB_END_SIGNATURE   ||
	    sector->version        != CACHEDB_VERSION) {
		NtClose(hVolume);
		return;
	}

	entryCount = *HalDiskCachePartitionCount;
	if (entryCount > CACHEDB_MAX_ENTRIES)
		entryCount = CACHEDB_MAX_ENTRIES;

	entries = (CACHEDB_ENTRY *)sector->data;
	for (i = 0; i < entryCount; i++) {
		if (entries[i].dwTitleId == dwTitleId) {
			entries[i].dwTitleId = 0;
			entries[i].fUsed = FALSE;
			break;
		}
	}

	// Write the sector back. Status is intentionally ignored; we made a
	// best effort and have no recovery path on failure here.
	NtWriteFile(
		hVolume, NULL, NULL, NULL, &iosb,
		sectorBytes, CACHEDB_SECTOR_SIZE, &byteOffset);

	NtClose(hVolume);
}

// =============================================================================
// XMUWriteNameToDriveLetter
// =============================================================================
//
// Writes a UTF-16 volume name into the FATX volume metadata of a mounted
// memory unit. The kernel exposes FSCTL_WRITE_VOLUME_METADATA via
// NtFsControlFile; we just have to format an FSCTL_VOLUME_METADATA request
// pointing at the FAT_VOLUME_METADATA's VolumeName field at the right
// offset.

#define FATX_VOLUME_NAME_LENGTH        20

typedef struct _FATX_VOLUME_METADATA_NAME {
	WCHAR VolumeName[FATX_VOLUME_NAME_LENGTH];
} FATX_VOLUME_METADATA_NAME;

typedef struct _FSCTL_VOLUME_METADATA_REQ {
	ULONG ByteOffset;
	ULONG TransferLength;
	PVOID TransferBuffer;
} FSCTL_VOLUME_METADATA_REQ;

DWORD WINAPI XMUWriteNameToDriveLetter(CHAR chDrive, LPCWSTR lpName)
{
	CHAR              dosPath[8];
	ANSI_STRING       devName;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK   iosb;
	HANDLE            hVolume = NULL;
	NTSTATUS          status;
	WCHAR             volumeName[FATX_VOLUME_NAME_LENGTH];
	FSCTL_VOLUME_METADATA_REQ req;
	int               i;

	if (lpName == NULL)
		return ERROR_INVALID_PARAMETER;

	// Build the DOS device path "\??\X:" for the requested drive letter.
	dosPath[0] = '\\';
	dosPath[1] = '?';
	dosPath[2] = '?';
	dosPath[3] = '\\';
	dosPath[4] = chDrive;
	dosPath[5] = ':';
	dosPath[6] = 0;

	RtlInitAnsiString(&devName, dosPath);
	InitializeObjectAttributes(&oa, &devName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = NtOpenFile(
		&hVolume,
		SYNCHRONIZE | GENERIC_WRITE,
		&oa,
		&iosb,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		FILE_SYNCHRONOUS_IO_ALERT);

	if (!NT_SUCCESS(status))
		return RtlNtStatusToDosError(status);

	// Copy the caller's name into a fixed-size buffer with null padding.
	for (i = 0; i < FATX_VOLUME_NAME_LENGTH - 1; i++) {
		volumeName[i] = lpName[i];
		if (lpName[i] == 0)
			break;
	}
	for (; i < FATX_VOLUME_NAME_LENGTH; i++)
		volumeName[i] = 0;

	req.ByteOffset     = 0; // VolumeName lives at offset 0 of FATX_VOLUME_METADATA_NAME
	req.TransferLength = sizeof(volumeName);
	req.TransferBuffer = volumeName;

	status = NtFsControlFile(
		hVolume, NULL, NULL, NULL,
		&iosb,
		FSCTL_WRITE_VOLUME_METADATA,
		&req, sizeof(req),
		NULL, 0);

	NtClose(hVolume);
	return RtlNtStatusToDosError(status);
}

// =============================================================================
// XapiNukeEmptySubdirs / XCleanMUFromRoot
// =============================================================================
//
// Recursive empty-directory cleanup used by the dashboard's MU and TDATA
// management flows.
//
// XapiNukeEmptySubdirs walks the immediate children of pszDrivePath. For
// each subdirectory it opens with FILE_LIST_DIRECTORY|DELETE access and
// peeks at the contents. A subdirectory is considered "empty" and gets
// deleted when:
//   - it has no entries at all, or
//   - fNukeFiles is TRUE and it has no SUB-directories (only files).
// The pszPreserveDir entry, if non-NULL, is always skipped.
//
// XCleanMUFromRoot is a thin wrapper that builds the "\??\X:\" path for
// a root-mounted MU drive and delegates to XapiNukeEmptySubdirs.

typedef struct _DIR_QUERY_BUF {
	FILE_DIRECTORY_INFORMATION info;
	CHAR                       nameTail[260];
} DIR_QUERY_BUF;

static NTSTATUS NukeDirectoryByHandle(HANDLE hDir);

static NTSTATUS NukeDirectoryByHandle(HANDLE hDir)
{
	IO_STATUS_BLOCK              iosb;
	DIR_QUERY_BUF                buf;
	FILE_DISPOSITION_INFORMATION dispose;
	NTSTATUS                     status;
	BOOLEAN                      first = TRUE;

	for (;;) {
		status = NtQueryDirectoryFile(
			hDir, NULL, NULL, NULL,
			&iosb,
			&buf, sizeof(buf) - sizeof(CHAR),
			FileDirectoryInformation,
			NULL,
			first);
		first = FALSE;

		if (!NT_SUCCESS(status))
			break;

		// Null-terminate the name in the spare byte we left at the end.
		{
			ULONG nameLen = buf.info.FileNameLength;
			((CHAR *)buf.info.FileName)[nameLen] = 0;
		}

		// Recurse into directories, delete files directly.
		{
			OBJECT_ATTRIBUTES childOa;
			ANSI_STRING       childName;
			HANDLE            hChild = NULL;
			NTSTATUS          childStatus;
			BOOLEAN           isDir = (buf.info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
			ULONG             childAccess = FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE;
			ULONG             childOptions = FILE_OPEN_FOR_BACKUP_INTENT;

			if (isDir) {
				childAccess  |= FILE_LIST_DIRECTORY;
				childOptions |= FILE_DIRECTORY_FILE;
			} else {
				childOptions |= FILE_NON_DIRECTORY_FILE;
			}

			RtlInitAnsiString(&childName, (PCSZ)buf.info.FileName);
			InitializeObjectAttributes(&childOa, &childName, OBJ_CASE_INSENSITIVE, hDir, NULL);

			childStatus = NtOpenFile(
				&hChild, childAccess, &childOa, &iosb,
				FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
				childOptions);

			if (!NT_SUCCESS(childStatus))
				continue;

			if (isDir) {
				NukeDirectoryByHandle(hChild);
				NtClose(hChild);
				continue;
			}

			// Clear the read-only bit if it's set, then mark for delete.
			if (buf.info.FileAttributes & FILE_ATTRIBUTE_READONLY) {
				FILE_BASIC_INFORMATION basic;
				RtlZeroMemory(&basic, sizeof(basic));
				basic.FileAttributes =
					(buf.info.FileAttributes & FILE_ATTRIBUTE_VALID_SET_FLAGS) &
					~FILE_ATTRIBUTE_READONLY;
				basic.FileAttributes |= FILE_ATTRIBUTE_NORMAL;
				NtSetInformationFile(hChild, &iosb, &basic, sizeof(basic), FileBasicInformation);
			}

			dispose.DeleteFile = TRUE;
			NtSetInformationFile(hChild, &iosb, &dispose, sizeof(dispose), FileDispositionInformation);
			NtClose(hChild);
		}
	}

	// We exited the loop because we ran out of entries. Mark the directory
	// itself for deletion so the caller's NtClose finalizes the removal.
	if (status == STATUS_NO_MORE_FILES || status == STATUS_NO_SUCH_FILE) {
		FILE_BASIC_INFORMATION basic;
		RtlZeroMemory(&basic, sizeof(basic));
		basic.FileAttributes =
			(buf.info.FileAttributes & FILE_ATTRIBUTE_VALID_SET_FLAGS) &
			~FILE_ATTRIBUTE_READONLY;
		basic.FileAttributes |= FILE_ATTRIBUTE_NORMAL;
		NtSetInformationFile(hDir, &iosb, &basic, sizeof(basic), FileBasicInformation);

		dispose.DeleteFile = TRUE;
		status = NtSetInformationFile(hDir, &iosb, &dispose, sizeof(dispose), FileDispositionInformation);
	}

	return status;
}

DWORD WINAPI XapiNukeEmptySubdirs(LPCSTR pszDrivePath, LPCSTR pszPreserveDir, BOOLEAN fNukeFiles)
{
	OBJECT_ATTRIBUTES rootOa;
	ANSI_STRING       rootName;
	HANDLE            hRoot = NULL;
	IO_STATUS_BLOCK   iosb;
	DIR_QUERY_BUF     buf;
	NTSTATUS          status;
	BOOLEAN           rootOpened = FALSE;
	BOOLEAN           first;

	if (pszDrivePath == NULL)
		return ERROR_INVALID_PARAMETER;

	RtlInitAnsiString(&rootName, pszDrivePath);
	InitializeObjectAttributes(&rootOa, &rootName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = NtOpenFile(
		&hRoot,
		FILE_LIST_DIRECTORY | SYNCHRONIZE,
		&rootOa, &iosb,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);

	if (!NT_SUCCESS(status))
		return RtlNtStatusToDosError(status);

	rootOpened = TRUE;
	first = TRUE;

	for (;;) {
		status = NtQueryDirectoryFile(
			hRoot, NULL, NULL, NULL,
			&iosb,
			&buf, sizeof(buf) - sizeof(CHAR),
			FileDirectoryInformation,
			NULL, first);
		first = FALSE;

		if (!NT_SUCCESS(status))
			break;

		if (!(buf.info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		// Null-terminate the entry name in place.
		((CHAR *)buf.info.FileName)[buf.info.FileNameLength] = 0;

		// Skip the preserve target if the caller asked us to.
		if (pszPreserveDir != NULL) {
			ULONG j;
			BOOLEAN match = TRUE;
			for (j = 0; ; j++) {
				CHAR a = ((CHAR *)buf.info.FileName)[j];
				CHAR b = pszPreserveDir[j];
				if (a != b) { match = FALSE; break; }
				if (a == 0) break;
			}
			if (match)
				continue;
		}

		// Open the candidate subdirectory and inspect its contents.
		{
			OBJECT_ATTRIBUTES subOa;
			ANSI_STRING       subName;
			HANDLE            hSub = NULL;
			NTSTATUS          subStatus;
			DIR_QUERY_BUF     subBuf;
			BOOLEAN           subFirst = TRUE;
			BOOLEAN           keepIt = FALSE;

			RtlInitAnsiString(&subName, (PCSZ)buf.info.FileName);
			InitializeObjectAttributes(&subOa, &subName, OBJ_CASE_INSENSITIVE, hRoot, NULL);

			subStatus = NtOpenFile(
				&hSub,
				FILE_LIST_DIRECTORY | DELETE | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
				&subOa, &iosb,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);

			if (!NT_SUCCESS(subStatus))
				continue;

			while (NT_SUCCESS(subStatus)) {
				subStatus = NtQueryDirectoryFile(
					hSub, NULL, NULL, NULL,
					&iosb,
					&subBuf, sizeof(subBuf) - sizeof(CHAR),
					FileDirectoryInformation,
					NULL, subFirst);
				subFirst = FALSE;

				if (!NT_SUCCESS(subStatus))
					break;

				// If we found anything that disqualifies the parent from
				// being treated as "empty", remember it and stop scanning.
				if (!fNukeFiles || (subBuf.info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					keepIt = TRUE;
					break;
				}
			}

			if (!keepIt &&
			    (subStatus == STATUS_NO_MORE_FILES || subStatus == STATUS_NO_SUCH_FILE)) {
				NukeDirectoryByHandle(hSub);
			}

			NtClose(hSub);
		}
	}

	if (rootOpened) {
		if (status == STATUS_NO_MORE_FILES || status == STATUS_NO_SUCH_FILE)
			status = STATUS_SUCCESS;
		NtClose(hRoot);
	}

	return RtlNtStatusToDosError(status);
}

DWORD WINAPI XCleanMUFromRoot(CHAR chDrive, LPCSTR pszPreserveDir)
{
	CHAR path[8];

	// Normalize to upper case.
	chDrive = (CHAR)(chDrive & ~0x20);

	path[0] = '\\';
	path[1] = '?';
	path[2] = '?';
	path[3] = '\\';
	path[4] = chDrive;
	path[5] = ':';
	path[6] = '\\';
	path[7] = 0;

	return XapiNukeEmptySubdirs(path, pszPreserveDir, TRUE);
}

// =============================================================================
// Timezone helpers
// =============================================================================
//
// The Xbox stores timezone state in the XC_MAX_OS EEPROM region as part of
// a packed XBOX_USER_SETTINGS structure. The dashboard's settings UI calls
// these helpers to push timezone changes back into EEPROM and to set the
// system clock from a local time.
//
// Layout of the timezone fields inside XBOX_USER_SETTINGS:
//   - TimeZoneBias       (LONG, minutes east of UTC, negative for west)
//   - TimeZoneStdName    (4 bytes)
//   - TimeZoneDltName    (4 bytes)
//   - TimeZoneStdDate    (XBOX_TIMEZONE_DATE_PUB)
//   - TimeZoneDltDate    (XBOX_TIMEZONE_DATE_PUB)
//   - TimeZoneStdBias    (LONG)
//   - TimeZoneDltBias    (LONG)
//
// The exact byte offsets depend on the XBOX_USER_SETTINGS layout in
// xconfig.h, which we read into a local buffer and write back wholesale.

// File-local: copy SYSTEMTIME month/day/dayofweek/hour fields into the
// 4-byte XBOX_TIMEZONE_DATE record stored in EEPROM. The wYear field is
// ignored because Xbox timezone records use the "Nth weekday of month"
// recurring date format, not absolute dates.
//
// WstrToXboxTimeZoneName is provided as __inline by xconfig.h, so we
// just call it from XapipSetTimeZoneInformation below without defining
// our own.
static VOID CopySystemTimeToTzDate(const SYSTEMTIME *systime, XBOX_TIMEZONE_DATE *out)
{
	out->Month     = (BYTE)systime->wMonth;
	out->Day       = (BYTE)systime->wDay;
	out->DayOfWeek = (BYTE)systime->wDayOfWeek;
	out->Hour      = (BYTE)systime->wHour;
}

DWORD WINAPI XapipSetTimeZoneInformation(PTIME_ZONE_INFORMATION TimeZoneInformation)
{
	DWORD       err;
	ULONG       queryType = 0;
	ULONG       querySize = 0;
	BYTE        userSettings[256]; // EEPROM_TOTAL_MEMORY_SIZE upper bound
	XBOX_USER_SETTINGS *config;

	if (TimeZoneInformation == NULL)
		return ERROR_INVALID_PARAMETER;

	err = XQueryValue(XC_MAX_OS, &queryType, userSettings, sizeof(userSettings), &querySize);
	if (err != ERROR_SUCCESS)
		return err;

	config = (XBOX_USER_SETTINGS *)userSettings;
	config->TimeZoneBias = TimeZoneInformation->Bias;

	WstrToXboxTimeZoneName(TimeZoneInformation->StandardName, config->TimeZoneStdName);
	WstrToXboxTimeZoneName(TimeZoneInformation->DaylightName, config->TimeZoneDltName);

	CopySystemTimeToTzDate(&TimeZoneInformation->StandardDate, &config->TimeZoneStdDate);
	CopySystemTimeToTzDate(&TimeZoneInformation->DaylightDate, &config->TimeZoneDltDate);

	config->TimeZoneStdBias = TimeZoneInformation->StandardBias;
	config->TimeZoneDltBias = TimeZoneInformation->DaylightBias;

	return XSetValue(XC_MAX_OS, REG_BINARY, userSettings, querySize);
}

// XapipGetTimeZoneBias is provided by the public xapilib(d).lib. We forward-
// declare it here so the local time -> system time conversion below can
// pick up the current bias without re-reading EEPROM.
extern VOID XapipGetTimeZoneBias(LARGE_INTEGER *bias);

BOOL WINAPI XapiSetLocalTime(const SYSTEMTIME *lpLocalTime)
{
	TIME_FIELDS   fields;
	LARGE_INTEGER localTime;
	LARGE_INTEGER systemTime;
	LARGE_INTEGER bias;

	if (lpLocalTime == NULL)
		return FALSE;

	fields.Year         = lpLocalTime->wYear;
	fields.Month        = lpLocalTime->wMonth;
	fields.Day          = lpLocalTime->wDay;
	fields.Hour         = lpLocalTime->wHour;
	fields.Minute       = lpLocalTime->wMinute;
	fields.Second       = lpLocalTime->wSecond;
	fields.Milliseconds = lpLocalTime->wMilliseconds;
	fields.Weekday      = 0;

	if (!RtlTimeFieldsToTime(&fields, &localTime))
		return FALSE;

	XapipGetTimeZoneBias(&bias);
	systemTime.QuadPart = localTime.QuadPart + bias.QuadPart;

	NtSetSystemTime(&systemTime, NULL);
	return TRUE;
}

// =============================================================================
// Library identifier
// =============================================================================

const ULONG XapiBuildNumberP = 5960;
