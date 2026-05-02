// utils.h: miscellaneous helpers (formatted output, byte-size humanizers,
// random IDs, IP address parsing) used across the toolbox.

#pragma once

#include "xboxinternals.h"
#include <string>

class utils
{
public:

	typedef struct dataContainer 
	{

		char* data;
		uint32_t size;

		dataContainer() : data(NULL), size(0) {}

		dataContainer(uint32_t size)
		{
			this->data = (char*)utils::mallocWithTerminator(size);
			this->size = size; 
		}

		dataContainer(char* data, uint32_t size, uint32_t copySize)
		{
			this->data = (char*)utils::mallocCopyWithTerminator(data, size, copySize);
			this->size = size; 
		}

		~dataContainer()
		{
			free(this->data);
		}

	} dataContainer;

	typedef struct intContainer 
	{
		int32_t value;

		intContainer(int32_t value)
		{
			this->value = value;
		}

	} intContainer;

	static void hexDumpDebug(void* data, uint32_t size);
	static char* hexDump(void* data, uint32_t size);
	static void uSleep(uint64_t timeout);
	static void byteSwap16(uint8_t* buf, uint32_t size);
	static void debugPrint(const char* format, ...);
	static void* mallocWithTerminator(uint32_t size);
	static void* mallocCopyWithTerminator(void* source, uint32_t size, uint32_t copySize);
	static void reboot();
	static void ReturnToDashboard();
	static void shutdown();
	static uint32_t getFreePhysicalMemory();
	static uint32_t getTotalPhysicalMemory();

	static uint8_t ioInputByte(uint16_t port);
	static uint16_t ioInputShort(uint16_t port);
	static uint32_t ioInputInt(uint16_t port);
	static void ioOutputByte(uint16_t port, uint8_t value);
	static void ioOutputShort(uint16_t port, uint16_t value);
	static void ioOutputInt(uint16_t port, uint32_t value);
	static void setLedStates(uint32_t ledStates);
	static uint32_t roundUpToNextPowerOf2(uint32_t value);
	static void sleepMicroSecs(LARGE_INTEGER clockFreq, uint16_t microSecs);
	static void swapString(char** oldValue, const char* newValue);
	static void freeString(char** value);
	static USHORT WINAPI FindFirstSetRightMember(ULONG Set);
};
