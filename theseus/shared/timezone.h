// timezone.h: TZINFO + TZDATE descriptors used by the locale UI to
// drive Windows-style timezone bias / DST rule selection. Table is
// defined in shared/timezone.cpp.

#pragma once

struct TZDATE
{
	BYTE month;
	BYTE day;
	BYTE dayofweek;
	BYTE hour;
};

struct TZINFO
{
	const TCHAR* dispname;
	SHORT dltflag;
	SHORT bias;
	SHORT stdbias;
	SHORT dltbias;
	const WCHAR* stdname;
	struct TZDATE stddate;
	const WCHAR* dltname;
	struct TZDATE dltdate;
};

extern const struct TZINFO g_timezoneinfo[];
extern const int g_timezoneCount;

#define TIMEZONECOUNT g_timezoneCount

#define NA_DEFAULT_TIMEZONE     11 // Eastern
#define JAPAN_DEFAULT_TIMEZONE  60 // Tokyo
#define ROW_DEFAULT_TIMEZONE    25 // London
