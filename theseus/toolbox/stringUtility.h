// stringUtility.h: string-manipulation helpers used across the toolbox
// (split, replace, hex / decimal formatters, case-insensitive comparisons,
// etc.). Operates on raw char* and std::string.

#pragma once

#include "xboxinternals.h"
#include "pointerVector.h"

#include <string>

#define StringNotFouund 0xffffffff

class stringUtility
{
public:
	static char* formatString(const char* format, ...);
	static char* lowerCase(const char* value);
	static char* upperCase(const char* value);
	static bool startsWith(const char* value, const char* startsWith, bool caseInsensitive);
	static bool endsWith(const char* value, const char* endsWith, bool caseInsensitive);
	static bool equals(const char* value1, const char* value2, bool caseInsensitive);
	static char* stringUtility::insertAtIndex(const char* value, char toInsert, int index);
	static char* removeAtIndex(const char* value, int index);
	static char* replace(const char* value, const char* search, const char* with);
	static char* leftTrim(const char* value, const char trimChar);
	static char* rightTrim(const char* value, const char trimChar);
	static char* trim(const char* value, const char trimChar);
	static void fTrim(char *s);
	static pointerVector<char*>* split(const char* value, const char* delimiter, bool trimValues);
	static char* splitAfter(const char* value, const char* search);
	static char* substr(const char* value, uint32_t offset, int32_t length);
	static int32_t find(const char* value, uint32_t length, uint32_t startPos, const char* search, bool caseInsensitive);
	static void copyString(char* dest, char *source, uint32_t maxLength);
	static int toInt(const char* value);
	static int hexCharToInt(char c);
	static char* formatSize(uint32_t size);
	static char* formatIp(uint32_t ip);
	static char* padString(const char* message, uint32_t len);
	static char* formatLcdString(const char* message, uint32_t maxlen);
};