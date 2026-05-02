// xboxinternals.h: Xbox kernel / network type declarations the toolbox
// relies on (Nt* prototypes, NTSTATUS, OBJECT_ATTRIBUTES, LAUNCH_DATA_*,
// XINPUT_GAMEPAD constants, Winsock error codes). Forked from PrometheOS;
// see theseus/toolbox/LICENSE for the lineage.

#pragma once

#include <xtl.h>

#define WSAWOULDBLOCK 10035

// POSIX names -> MSVC underscored names
#define strdup _strdup
#define stricmp _stricmp
#define strnicmp _strnicmp

// Fixed-width integer types (not provided by MSVC 7.1, but clang has them)
#ifdef __clang__
#include <stdint.h>
#else
typedef signed char int8_t;
typedef short int16_t;
typedef long int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;
#endif
typedef void (*CallbackFunction)(void*);

// SMC / I2C / EEPROM constants (homebrew, not in XDK)

#define I2C_HDMI_ADDRESS1 0x88
#define I2C_HDMI_ADDRESS2 0x86
#define I2C_HDMI_VERSION1 0x57
#define I2C_HDMI_VERSION2 0x58
#define I2C_HDMI_VERSION3 0x59

#define XC_VIDEO_FLAGS 0x8
#define XC_AUDIO_FLAGS 0x9
#define XC_DVD_REGION 0x12
#define XC_FACTORY_SERIAL_NUMBER 0x100
#define XC_FACTORY_ETHERNET_ADDR 0x101
#define XC_FACTORY_AV_REGION 0x103
#define XC_FACTORY_GAME_REGION 0x104

#define	SMBDEV_PIC16L 0x20
#define	SMBDEV_VIDEO_ENCODER_CONNEXANT 0x8a,
#define	SMBDEV_VIDEO_ENCODER_FOCUS 0xd4,
#define	SMBDEV_VIDEO_ENCODER_XCALIBUR 0xe0,
#define	SMBDEV_TEMP_MONITOR 0x98,
#define	SMBDEV_EEPROM 0xa8

#define PIC16L_CMD_POWER 0x02
#define PIC16L_CMD_AV_PACK 0x04
#define PIC16L_CMD_LED_MODE 0x07
#define PIC16L_CMD_LED_REGISTER 0x08
#define PIC16L_CMD_EJECT 0x0c
#define PIC16L_CMD_INTERRUPT_REASON 0x11
#define PIC16L_CMD_RESET_ON_EJECT 0x19
#define	PIC16L_CMD_SCRATCH_REGISTER 0x1b

#define POWER_SUBCMD_RESET 0x01
#define POWER_SUBCMD_CYCLE 0x40
#define POWER_SUBCMD_POWER_OFF 0x80

#define	SCRATCH_REGISTER_BITVALUE_EJECT_AFTER_BOOT 0x01
#define	SCRATCH_REGISTER_BITVALUE_DISPLAY_ERROR 0x02
#define	SCRATCH_REGISTER_BITVALUE_NO_ANIMATION 0x04
#define	SCRATCH_REGISTER_BITVALUE_RUN_DASHBOARD 0x08

#define RETURN_FIRMWARE_HALT 0x00
#define	RETURN_FIRMWARE_REBOOT 0x01
#define RETURN_FIRMWARE_QUICK_REBOOT 0x02
#define RETURN_FIRMWARE_HARD 0x03
#define RETURN_FIRMWARE_FATAL 0x04
#define RETURN_FIRMWARE_ALL 0x05

#define SMC_COMMAND_TRAY_STATE 0x03
#define SMC_TRAY_STATE_ACTIVITY 0x01
#define SMC_TRAY_STATE_CLOSED 0x00
#define SMC_TRAY_STATE_OPEN 0x10
#define SMC_TRAY_STATE_UNLOADING 0x20
#define SMC_TRAY_STATE_OPENING 0x30
#define SMC_TRAY_STATE_NO_MEDIA 0x40
#define SMC_TRAY_STATE_CLOSING 0x50
#define SMC_TRAY_STATE_MEDIA_DETECT 0x60
#define SMC_TRAY_STATE_RESET 0x70

#define PIC_ADDRESS 0x20
#define FAN_MODE 0x05
#define FAN_REGISTER 0x06
#define FAN_READBACK 0x10
#define MB_TEMP 0x0A
#define CPU_TEMP 0x09

#define I2C_IO_BASE 0xC000

#define SMC_SLAVE_ADDRESS 0x20
#define SMC_COMMAND_LED_OVERRIDE 0x07
#define SMC_COMMAND_LED_STATES 0x08
#define SMC_LED_OVERRIDE_USE_REQUESTED_LED_STATES 0x01
#define SMC_LED_STATES_GREEN_STATE0 0x01
#define SMC_LED_STATES_GREEN_STATE1 0x02
#define SMC_LED_STATES_GREEN_STATE2 0x04
#define SMC_LED_STATES_GREEN_STATE3 0x08
#define SMC_LED_STATES_RED_STATE0 0x10
#define SMC_LED_STATES_RED_STATE1 0x20
#define SMC_LED_STATES_RED_STATE2 0x40
#define SMC_LED_STATES_RED_STATE3 0x80

#define EEPROM_VIDEO_FLAGS_WIDESCREEN   0x00010000
#define EEPROM_VIDEO_FLAGS_HDTV_720p    0x00020000
#define EEPROM_VIDEO_FLAGS_HDTV_1080i   0x00040000
#define EEPROM_VIDEO_FLAGS_HDTV_480p    0x00080000
#define EEPROM_VIDEO_FLAGS_LETTERBOX    0x00100000
#define EEPROM_VIDEO_FLAGS_60Hz         0x00400000
#define EEPROM_VIDEO_FLAGS_MASK         0x005F0000

#define EEPROM_AUDIO_FLAGS_STEREO       0x00000000
#define EEPROM_AUDIO_FLAGS_MONO         0x00000001
#define EEPROM_AUDIO_FLAGS_SURROUND     0x00000002
#define EEPROM_AUDIO_FLAGS_ENABLE_AC3   0x00010000
#define EEPROM_AUDIO_FLAGS_ENABLE_DTS   0x00020000
#define EEPROM_AUDIO_FLAGS_MASK         0x00030003

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_DEVICE_NOT_CONNECTED
#define STATUS_DEVICE_NOT_CONNECTED ((NTSTATUS)0xC000009DL)
#endif
#ifndef STATUS_IO_TIMEOUT
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5L)
#endif
#ifndef STATUS_DEVICE_DOES_NOT_EXIST
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xC00000C0L)
#endif
#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225L)
#endif
#ifndef STATUS_INVALID_DEVICE_REQUEST
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xC0000010L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0x80000000L)
#endif

// -------------------------------------------------------------------
// Types and kernel APIs provided by ntos.h (Xbox private kernel headers).
// When building with the Theseus PCH (Std.h), ntos.h is already included.
// When building standalone (Toolbox), these are needed.
//
// Guard hierarchy:
//   _NTOS_ or _THESEUS_STD_H = full kernel headers present, skip everything
//   _NTDEF_                   = ntdef.h present (clang xdk_compat.h), skip
//                                types it provides but keep the rest
//   neither                   = standalone homebrew build, provide everything
// -------------------------------------------------------------------

// NTSTATUS: ntdef.h already provides this
#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif

#if !defined(_THESEUS_STD_H) && !defined(_NTOS_)

// --- Types also defined by ntdef.h, only needed for standalone builds ---
#ifndef _NTDEF_

typedef struct STRING {
	uint16_t Length;
	uint16_t MaximumLength;
	char* Buffer;
} STRING;

typedef struct OBJECT_ATTRIBUTES
{
    HANDLE RootDirectory;
    STRING*	ObjectName;
    ULONG Attributes;
} OBJECT_ATTRIBUTES;

#define InitializeObjectAttributes( p, n, a, r ) { \
    (p)->RootDirectory = r;                             \
    (p)->Attributes = a;                                \
    (p)->ObjectName = n;                                \
    }

#endif // !_NTDEF_

// --- Types NOT in ntdef.h, needed unless full ntos.h is present ---

typedef struct LAUNCH_DATA_HEADER
{
	DWORD   dwLaunchDataType;
	DWORD   dwTitleId;
	char    szLaunchPath[520];
	DWORD   dwFlags;
}
LAUNCH_DATA_HEADER;

typedef struct LAUNCH_DATA_PAGE
{
	LAUNCH_DATA_HEADER  Header;
	UCHAR               Pad[492];
	UCHAR               LaunchData[3072];
}
LAUNCH_DATA_PAGE;

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif
#define ROUND_TO_PAGES(Size)  (((ULONG_PTR)(Size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

typedef struct DRIVER_OBJECT
{
	const int16_t Type;
	const int16_t Size;
	struct _DEVICE_OBJECT *DeviceObject;
}
DRIVER_OBJECT;

typedef struct DEVICE_OBJECT
{
	const int16_t Type;
	const uint16_t Size;
	int32_t ReferenceCount;
	DRIVER_OBJECT* DriverObject;
}
DEVICE_OBJECT;

typedef struct IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef VOID (WINAPI *PIO_APC_ROUTINE) (PVOID ApcContext, IO_STATUS_BLOCK* IoStatusBlock, ULONG Reserved);

extern "C"
{
	extern STRING* XeImageFileName;
	extern STRING* HalDiskModelNumber;
	extern STRING* HalDiskSerialNumber;

	LONG WINAPI IoCreateSymbolicLink(STRING*, STRING*);
	LONG WINAPI IoDeleteSymbolicLink(STRING*);
	LONG WINAPI IoDismountVolumeByName(STRING*);
	LONG WINAPI IoDismountVolume(DEVICE_OBJECT*);
	VOID WINAPI HalReturnToFirmware(unsigned int value);
	LONG WINAPI ExQueryNonVolatileSetting(ULONG ValueIndex, ULONG* Type, void* Value, ULONG ValueLength, ULONG* ResultLength);
	LONG WINAPI ExSaveNonVolatileSetting(ULONG ValueIndex, ULONG Type, void* Value, ULONG ValueLength);
	NTSTATUS WINAPI HalWriteSMBusValue(UCHAR devddress, UCHAR offset, UCHAR writedw, DWORD data);
	NTSTATUS WINAPI HalReadSMBusValue(UCHAR devddress, UCHAR offset, UCHAR readdw, DWORD* pdata);
	NTSTATUS WINAPI HalReadSMCTrayState(ULONG* TrayState, ULONG* EjectCount);

	VOID WINAPI KeQuerySystemTime(LPFILETIME CurrentTime);
}

#define FAT_VOLUME_NAME_LENGTH          32
#define FAT_ONLINE_DATA_LENGTH          2048
typedef struct _FAT_VOLUME_METADATA {
    ULONG Signature;
    ULONG SerialNumber;
    ULONG SectorsPerCluster;
    ULONG RootDirFirstCluster;
    WCHAR VolumeName[FAT_VOLUME_NAME_LENGTH];
    UCHAR OnlineData[FAT_ONLINE_DATA_LENGTH];
} FAT_VOLUME_METADATA, *PFAT_VOLUME_METADATA;

typedef enum _FSINFOCLASS {
    FileFsVolumeInformation = 1,
    FileFsLabelInformation,
    FileFsSizeInformation,
    FileFsDeviceInformation,
    FileFsAttributeInformation,
    FileFsControlInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsMaximumInformation
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

typedef struct _FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER AvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_SIZE_INFORMATION, *PFILE_FS_SIZE_INFORMATION;

extern "C"
{
	NTSTATUS WINAPI NtQueryVolumeInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FsInformation, ULONG Length, FS_INFORMATION_CLASS FsInformationClass);
	NTSTATUS WINAPI NtSetSystemTime(LPFILETIME SystemTime, LPFILETIME PreviousTime);
	NTSTATUS WINAPI NtOpenFile(HANDLE* FileHandle, ACCESS_MASK DesiredAccess, OBJECT_ATTRIBUTES* ObjectAttributes, IO_STATUS_BLOCK* IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions);
	NTSTATUS WINAPI NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, VOID* ApcContext, IO_STATUS_BLOCK* IoStatusBlock, VOID* Buffer, ULONG Length, LARGE_INTEGER* ByteOffset);
	NTSTATUS WINAPI NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, VOID* ApcContext, IO_STATUS_BLOCK* IoStatusBlock, VOID* Buffer, ULONG Length, LARGE_INTEGER* ByteOffset);
	NTSTATUS WINAPI NtClose(HANDLE Handle);
	NTSTATUS WINAPI NtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, IO_STATUS_BLOCK* IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);
	NTSTATUS WINAPI RtlInitAnsiString(STRING* DestinationString, LPCSTR SourceString);

	NTSTATUS WINAPI KeDelayExecutionThread(CHAR WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
	VOID WINAPI KeStallExecutionProcessor(ULONG usec);
	ULONG WINAPI HalGetInterruptVector(ULONG BusInterruptLevel, UCHAR* Irql);
	UCHAR _fastcall KfRaiseIrql(UCHAR NewIrql);
	VOID _fastcall KfLowerIrql(UCHAR NewIrql);

	NTSTATUS WINAPI MU_CreateDeviceObject(uint32_t port, uint32_t slot, STRING* deviceName);
	VOID WINAPI MU_CloseDeviceObject(uint32_t port, uint32_t slot);
	DEVICE_OBJECT* WINAPI MU_GetExistingDeviceObject(uint32_t port, uint32_t slot);
	BOOL WINAPI XapiFormatFATVolumeEx(STRING* VolumePath, ULONG BytesPerCluster);

	extern LAUNCH_DATA_PAGE* LaunchDataPage;

	VOID WINAPI MmPersistContiguousMemory(PVOID BaseAddress, ULONG NumberOfBytes, BOOLEAN Persist);
	PVOID WINAPI MmAllocateContiguousMemory(ULONG NumberOfBytes);

	UCHAR NTSYSAPI XboxHDKey[0x10];
	VOID WINAPI XcHMAC(PBYTE pbKey, DWORD dwKeyLength, PBYTE pbInput, DWORD dwInputLength, PBYTE pbInput2, DWORD dwInputLength2, PBYTE pbDigest);
}

#endif // !_THESEUS_STD_H && !_NTOS_

// -------------------------------------------------------------------
// Undocumented network config structures (not in any XDK headers)
// -------------------------------------------------------------------

typedef struct {
    BYTE        abSeed[20];
    IN_ADDR     ina;
    IN_ADDR     inaMask;
    IN_ADDR     inaGateway;
    IN_ADDR     inaDnsPrimary;
    IN_ADDR     inaDnsSecondary;
    char        achDhcpHostName[64];
    char        achPppUserName[64];
    char        achPppPassword[64];
    char        achPppServer[64];
    BYTE        abReserved[192];
    DWORD       dwSigEnd;
} XXNetConfigParams;

typedef struct
{
	DWORD	Data_00;
	DWORD	Data_04;
	DWORD	Data_08;
	DWORD	Data_0c;
	DWORD	Data_10;

	DWORD	V1_IP;
	DWORD	V1_Subnetmask;
	DWORD	V1_Defaultgateway;
	DWORD	V1_DNS1;
	DWORD	V1_DNS2;

	DWORD	Data_28;
	DWORD	Data_2c;
	DWORD	Data_30;
	DWORD	Data_34;
	DWORD	Data_38;

	DWORD	V2_Tag;

	DWORD	Flag;
	DWORD	Data_44;

	DWORD	V2_IP;
	DWORD	V2_Subnetmask;
	DWORD	V2_Defaultgateway;
	DWORD	V2_DNS1;
	DWORD	V2_DNS2;

	DWORD   Data_xx[0x200-0x5c];

} XNetConfigParams;

#define XDK_NETWORK_CONFIG_MANUAL_IP 0x00000004
#define XDK_NETWORK_CONFIG_MANUAL_DNS 0x00000008

typedef struct {
    DWORD       dwFlags;
    IN_ADDR     ina;
    IN_ADDR     inaMask;
    IN_ADDR     inaGateway;
    IN_ADDR     inaDnsPrimary;
    IN_ADDR     inaDnsSecondary;
    IN_ADDR     inaDhcpServer;
    char        achPppServer[64][4];
} XNetConfigStatus;

#define XNET_CONFIG_NORMAL 0
#define XNET_CONFIG_PARAMS_SIGEND 'XBCP'

#define XNET_STATUS_PENDING             0x0001
#define XNET_STATUS_PPPOE_DISCOVERED    0x0002
#define XNET_STATUS_PPPOE_CONFIGURED    0x0004
#define XNET_STATUS_PPPOE_REJECTED      0x0008
#define XNET_STATUS_PPPOE_NORESPONSE    0x0010
#define XNET_STATUS_DHCP_CONFIGURED     0x0020
#define XNET_STATUS_DHCP_REJECTED       0x0040
#define XNET_STATUS_DHCP_GATEWAY        0x0080
#define XNET_STATUS_DHCP_DNS            0x0100
#define XNET_STATUS_DHCP_NORESPONSE     0x0200
#define XNET_STATUS_DNS_CONFIGURED      0x0400
#define XNET_STATUS_DNS_FAILED          0x0800
#define XNET_STATUS_DNS_NORESPONSE      0x1000
#define XNET_STATUS_PING_SUCCESSFUL     0x2000
#define XNET_STATUS_PING_NORESPONSE     0x4000

#define SMC_COMMAND_OVERRIDE_RESET_ON_TRAY_OPEN 0x19
#define SMC_RESET_ON_TRAY_OPEN_NONSECURE_MODE 0x01

// Undocumented network kernel APIs
extern "C"
{
	NTSTATUS WINAPI XNetLoadConfigParams(XNetConfigParams* params);
	NTSTATUS WINAPI XNetSaveConfigParams(const XNetConfigParams* params);
	NTSTATUS WINAPI XNetConfig(const XNetConfigParams* params, DWORD data);
	NTSTATUS WINAPI XNetGetConfigStatus(XNetConfigStatus* status);
}

#define HalReadSMBusByte(SlaveAddress, CommandCode, DataValue) HalReadSMBusValue(SlaveAddress, CommandCode, FALSE, DataValue)
#define HalReadSMBusWord(SlaveAddress, CommandCode, DataValue) HalReadSMBusValue(SlaveAddress, CommandCode, TRUE, DataValue)
#define HalWriteSMBusByte(SlaveAddress, CommandCode, DataValue) HalWriteSMBusValue(SlaveAddress, CommandCode, FALSE, DataValue)
#define HalWriteSMBusWord(SlaveAddress, CommandCode, DataValue) HalWriteSMBusValue(SlaveAddress, CommandCode, TRUE, DataValue)
