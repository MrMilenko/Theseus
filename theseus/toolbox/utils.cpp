// utils.cpp: misc helper implementations.

#include "utils.h"
#include "xboxinternals.h"

#include <xtl.h>
#include <stdio.h>
#include <string>
#include <cmath>

// Creates a "hexdump -Cv" style string - caller free()'s returned string
char* utils::hexDump(void* data, uint32_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';

	uint32_t rows = (uint32_t)ceil((float)(size / 16));
	uint32_t bufSize = (rows + 1) * 79 + 2;

	char* buf = (char*)malloc(bufSize);
	char* retBuf = buf;

	_snprintf(buf, 10, "00000000  ");
	buf += 10;

	for (i = 0; i < size; ++i) {
		_snprintf(buf, 3, "%02x ", ((unsigned char*)data)[i]);
		buf += 3;
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			_snprintf(buf++, 1, " ");
			if ((i+1) % 16 == 0) {
				_snprintf(buf, 29, "|%s|\n%08lx  ", ascii, (i+1));
				buf += 29;
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					_snprintf(buf++, 1, " ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					_snprintf(buf, 3, "   ");
					buf += 3;
				}
				_snprintf(buf, 19, "|%s|\n", ascii);
				buf += 19;
			}
		}
	}

	_snprintf(buf++, 1, "\n");
	*buf = 0;

	return retBuf;
}

void utils::hexDumpDebug(void* data, uint32_t size) {
#ifdef _DEBUG
	char* buf = hexDump(data, size);
	debugPrint(buf);
	free(buf);
#endif
}

void utils::uSleep(uint64_t timeout) {
	timeout *= -10; // 100ns units
	KeDelayExecutionThread(1, 0, (PLARGE_INTEGER)(&timeout));
}

void utils::byteSwap16(uint8_t* buf, uint32_t size) {
	uint8_t tmp;

	for(uint32_t i = 0; i < size; i += 2) {
		tmp = buf[i];
		buf[i] = buf[i+1];
		buf[i+1] = tmp;
	}
}

void utils::debugPrint(const char* format, ...)
{
#ifdef _DEBUG
	va_list args;
    va_start(args, format);

	uint32_t length = _vsnprintf(NULL, 0, format, args);

	char* message = (char*)malloc(length + 1);
	_vsnprintf(message, length, format, args);
	message[length] = 0;

    va_end(args);

	OutputDebugStringA(message);
	free(message);
#endif
}

void* utils::mallocWithTerminator(uint32_t size)
{
	char* result = (char*)malloc(size + 1);
	result[size] = 0;
	return (void*)result;
}

void* utils::mallocCopyWithTerminator(void* source, uint32_t size, uint32_t copySize)
{
	void* result = mallocWithTerminator(size);
	memcpy(result, source, copySize);
	return result;
}

void utils::reboot()
{
	HalWriteSMBusValue(SMBDEV_PIC16L, PIC16L_CMD_POWER, 0, POWER_SUBCMD_CYCLE);
}

void utils::shutdown()
{
	HalWriteSMBusValue(SMBDEV_PIC16L, PIC16L_CMD_POWER, 0, POWER_SUBCMD_POWER_OFF);
}

uint32_t utils::getFreePhysicalMemory()
{
	uint32_t result;
	MEMORYSTATUS status;
	memset(&status, 0, sizeof(status));
	GlobalMemoryStatus(&status);
	result = (uint32_t)status.dwAvailPhys;
	return result;
}

uint32_t utils::getTotalPhysicalMemory()
{
	uint32_t result;
	MEMORYSTATUS status;
	memset(&status, 0, sizeof(status));
	GlobalMemoryStatus(&status);
	result = (uint32_t)status.dwTotalPhys;
	return result;
}

uint8_t utils::ioInputByte(uint16_t port) 
{
	unsigned char value;
	__asm {
		mov dx, port
		in al, dx
		mov value, al
	}
	return value;
}

uint16_t utils::ioInputShort(uint16_t port) 
{
	unsigned short value;
	__asm {
		mov dx, port
		in ax, dx
		mov value, ax
	}
	return value;
}

uint32_t utils::ioInputInt(uint16_t port) 
{
	unsigned int value;
	__asm {
		mov dx, port
		in eax, dx
		mov value, eax
	}
	return value;
}

void utils::ioOutputByte(uint16_t port, uint8_t value) 
{
	__asm {
		mov dx, port
		mov al, value
		out dx, al
	}
}

void utils::ioOutputShort(uint16_t port, uint16_t value) 
{
	__asm {
		mov  dx, port
		mov  ax, value
		out  dx, ax
	}
}

void utils::ioOutputInt(uint16_t port, uint32_t value) 
{
	__asm {
		mov  dx, port
        mov  eax, value
        out  dx, eax
	}
}

USHORT WINAPI utils::FindFirstSetRightMember(ULONG Set) {
	__asm {
		bsf     eax, Set
	}
}

void utils::setLedStates(uint32_t ledStates)
{
	HalWriteSMBusByte(SMC_SLAVE_ADDRESS, SMC_COMMAND_LED_STATES, ledStates);
    HalWriteSMBusByte(SMC_SLAVE_ADDRESS, SMC_COMMAND_LED_OVERRIDE, SMC_LED_OVERRIDE_USE_REQUESTED_LED_STATES);
}
void utils::ReturnToDashboard() {
    HalReturnToFirmware((FIRMWARE_REENTRY)RETURN_FIRMWARE_QUICK_REBOOT);
}
uint32_t utils::roundUpToNextPowerOf2(uint32_t value) 
{
	value--;
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	value++;
	return value;
}

void utils::sleepMicroSecs(LARGE_INTEGER clockFreq, uint16_t microSecs)
{
	LARGE_INTEGER performanceCount;
	LARGE_INTEGER currentCount;
	QueryPerformanceCounter(&performanceCount);
	LONGLONG waitTime = microSecs * clockFreq.QuadPart / 1000000;
	do
	{
		QueryPerformanceCounter(&currentCount);
		Sleep(0);
	}
	while ( currentCount.QuadPart - performanceCount.QuadPart < waitTime);
}

void utils::swapString(char** oldValue, const char* newValue)
{
	free(*oldValue);
	*oldValue = strdup(newValue);
}

void utils::freeString(char** value)
{
	free(*value);
	*value = NULL;
}