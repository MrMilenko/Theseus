// date_node.cpp: the script Date object. Holds one FILETIME and hands out
// its parts in local time or UTC, a few strings, two calendar helpers, and
// the call that sets the console clock. Rebuilt function by function from
// the 5960 retail XBE; see docs/decomp/Date.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "date_node.h"
#include "locale_node.h"
#ifdef _XBOX
#include "../xbox/xbox_private_types.h"
#endif

extern CObject** g_rgParam;
extern int g_nParam;
extern CRunner* g_pRunner;
extern CObject* Dereference(CObject* pObject);

IMPLEMENT_NODE("Date", CDateObject, CObject)

// FND at 0x0017f698, same order.
#define _FND_CLASS CDateObject
START_NODE_FUN(CDateObject, CObject)
	NODE_FUN_IV(getDate)
	NODE_FUN_IV(getDay)
	NODE_FUN_IV(getFullYear)
	NODE_FUN_IV(getHours)
	NODE_FUN_IV(getMilliseconds)
	NODE_FUN_IV(getMinutes)
	NODE_FUN_IV(getMonth)
	NODE_FUN_IV(getSeconds)
	NODE_FUN_IV(getUTCDate)
	NODE_FUN_IV(getUTCDay)
	NODE_FUN_IV(getUTCFullYear)
	NODE_FUN_IV(getUTCHours)
	NODE_FUN_IV(getUTCMilliseconds)
	NODE_FUN_IV(getUTCMinutes)
	NODE_FUN_IV(getUTCMonth)
	NODE_FUN_IV(getUTCSeconds)
	NODE_FUN_SV(toGMTString)
	NODE_FUN_SV(toLocaleString)
	NODE_FUN_SV(toUTCString)
	NODE_FUN_II(isLeapYear)
	NODE_FUN_III(getDaysInMonth)
	NODE_FUN_VV(SetSystemClock)
END_NODE_FUN()
#undef _FND_CLASS

// Every getter is one of these two conversions and a field.
static SYSTEMTIME LocalParts(const FILETIME& when)
{
	FILETIME local;
	SYSTEMTIME parts;
	FileTimeToLocalFileTime(&when, &local);
	FileTimeToSystemTime(&local, &parts);
	return parts;
}

static SYSTEMTIME UTCParts(const FILETIME& when)
{
	SYSTEMTIME parts;
	FileTimeToSystemTime(&when, &parts);
	return parts;
}


// ===== Calendar ============================================================

// CDateObject::isLeapYear(year)   [script fn 'isLeapYear', FND 0x0017f860, sig 3]
// Reconstructed from 5960 retail XBE @ 0x00030f90. Ghidra decompile:
//     if (year%100==0 && year%400!=0) return false; return (year & 3)==0;
// The binary tests the 100/400 exception first, then year%4 (emitted as the
// signed `& 0x80000003` idiom). The Gregorian leap rule is functionally fixed,
// so every correct version reads the same; this one is the plain `% 4` form.
int CDateObject::isLeapYear(int nYear)
{
	if (nYear % 100 == 0 && nYear % 400 != 0)
		return 0;
	return nYear % 4 == 0;
}

// CDateObject::getDaysInMonth(month, year)  [script fn 'getDaysInMonth', FND 0x0017f878, sig 24]
// Reconstructed from 5960 retail XBE @ 0x00030fd0. Ghidra decompile:
//     if (month != 2) return days[month];      // short table @ 0x001765c6, [0] = 0
//     return 28 + isLeapYear(year);
// Month lengths are calendar facts. Months are 1 based; Theseus answers 0
// outside 1..12 where the binary reads past its table.
int CDateObject::getDaysInMonth(int nMonth, int nYear)
{
	static const short s_monthDays[13] = { 0, 31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (nMonth == 2)
		return 28 + isLeapYear(nYear);
	if (nMonth < 1 || nMonth > 12)
		return 0;
	return s_monthDays[nMonth];
}


// ===== Construction ========================================================

static WORD ParamWord(int index)
{
	CNumObject* num = g_rgParam[index]->ToNum();
	WORD value = (WORD)(int)num->m_value;
	num->Release();
	return value;
}

// CDateObject::CDateObject()
// Reconstructed from 5960 retail XBE @ 0x00031040. No arguments takes the
// current UTC time. Three to seven are year, month (0 based), day, then
// optional hour, minute, second and millisecond; each argument is
// dereferenced first and read as a number, a year below 100 gets 1900 added.
// Any other count is a script error and leaves the time zero.
CDateObject::CDateObject()
{
	m_time.dwLowDateTime = 0;
	m_time.dwHighDateTime = 0;

	SYSTEMTIME parts;
	if (g_nParam == 0)
	{
		GetSystemTime(&parts);
		SystemTimeToFileTime(&parts, &m_time);
		return;
	}
	if (g_nParam < 3 || g_nParam > 7)
	{
		g_pRunner->Error(_T("Wrong number of parameters to Date constructor\n"));
		return;
	}

	for (int i = 0; i < g_nParam; i++)
		g_rgParam[i] = Dereference(g_rgParam[i]);

	memset(&parts, 0, sizeof(parts));
	parts.wYear = ParamWord(0);
	if (parts.wYear < 100)
		parts.wYear += 1900;
	parts.wMonth = ParamWord(1) + 1;
	parts.wDay = ParamWord(2);

	WORD* optional[4] = { &parts.wHour, &parts.wMinute, &parts.wSecond, &parts.wMilliseconds };
	for (int i = 3; i < g_nParam; i++)
		*optional[i - 3] = ParamWord(i);

	SystemTimeToFileTime(&parts, &m_time);
}


// ===== Getters =============================================================

// CDateObject::getDate .. getSeconds   [script fns, FND 0x0017f698..0x0017f740, sig 2]
// Reconstructed from 5960 retail XBE @ 0x00031290..0x000313e0, one each:
//     FileTimeToLocalFileTime(&time, &local); FileTimeToSystemTime(&local, &st);
//     return st.<field>;          // getMonth returns wMonth - 1
int CDateObject::getDate()          { return LocalParts(m_time).wDay; }
int CDateObject::getDay()           { return LocalParts(m_time).wDayOfWeek; }
int CDateObject::getFullYear()      { return LocalParts(m_time).wYear; }
int CDateObject::getHours()         { return LocalParts(m_time).wHour; }
int CDateObject::getMilliseconds()  { return LocalParts(m_time).wMilliseconds; }
int CDateObject::getMinutes()       { return LocalParts(m_time).wMinute; }
int CDateObject::getMonth()         { return LocalParts(m_time).wMonth - 1; }
int CDateObject::getSeconds()       { return LocalParts(m_time).wSecond; }

// CDateObject::getUTCDate .. getUTCSeconds   [script fns, FND 0x0017f758..0x0017f800, sig 2]
// Reconstructed from 5960 retail XBE @ 0x00031410..0x000314f0: the same
// without the local time step.
int CDateObject::getUTCDate()         { return UTCParts(m_time).wDay; }
int CDateObject::getUTCDay()          { return UTCParts(m_time).wDayOfWeek; }
int CDateObject::getUTCFullYear()     { return UTCParts(m_time).wYear; }
int CDateObject::getUTCHours()        { return UTCParts(m_time).wHour; }
int CDateObject::getUTCMilliseconds() { return UTCParts(m_time).wMilliseconds; }
int CDateObject::getUTCMinutes()      { return UTCParts(m_time).wMinute; }
int CDateObject::getUTCMonth()        { return UTCParts(m_time).wMonth - 1; }
int CDateObject::getUTCSeconds()      { return UTCParts(m_time).wSecond; }

// Not in the 5960 function table; Theseus keeps it for C++ callers.
int CDateObject::getYear()
{
	int year = getFullYear();
	if (year >= 1900 && year < 2000)
		return year - 1900;
	return year;
}


// ===== Strings =============================================================

// Month abbreviations, 1 based like the pointer table at 0x0017442c.
static const TCHAR* const s_monthNames[13] =
{
	NULL, _T("Jan"), _T("Feb"), _T("Mar"), _T("Apr"), _T("May"), _T("Jun"),
	_T("Jul"), _T("Aug"), _T("Sep"), _T("Oct"), _T("Nov"), _T("Dec"),
};

static CStrObject* StampString(const FILETIME& when, const TCHAR* zone)
{
	SYSTEMTIME st = UTCParts(when);
	TCHAR buf[32];
	_stprintf(buf, _T("%02d %s %d %02d:%02d:%02d %s"),
		st.wDay, s_monthNames[st.wMonth], st.wYear, st.wHour, st.wMinute, st.wSecond, zone);
	return new CStrObject(buf);
}

// CDateObject::toGMTString()   [script fn 'toGMTString', FND 0x0017f818, sig 8]
// CDateObject::toUTCString()   [script fn 'toUTCString', FND 0x0017f848, sig 8]
// Reconstructed from 5960 retail XBE @ 0x00031510 and @ 0x00031660:
//     FileTimeToSystemTime(&time, &st);
//     swprintf(buf, L"%02d %s %d %02d:%02d:%02d GMT", day, months[month], year, h, m, s);
// The two differ only in the last word (UTC format at 0x00021d24).
CStrObject* CDateObject::toGMTString()
{
	return StampString(m_time, _T("GMT"));
}

CStrObject* CDateObject::toUTCString()
{
	return StampString(m_time, _T("UTC"));
}

// CDateObject::toLocaleString()   [script fn 'toLocaleString', FND 0x0017f830, sig 8]
// Reconstructed from 5960 retail XBE @ 0x000315c0: local time through
// FormatTime into a 32 character buffer.
CStrObject* CDateObject::toLocaleString()
{
	SYSTEMTIME st = LocalParts(m_time);
	TCHAR buf[32];
	FormatTime(buf, countof(buf), &st);
	return new CStrObject(buf);
}


// ===== Clock ===============================================================

// CDateObject::SetSystemClock()   [script fn 'SetSystemClock', FND 0x0017f890, sig 1]
// Reconstructed from 5960 retail XBE @ 0x00031710. Sets the local clock to
// this date, reads it back, and sets it again when the two are a minute or
// more apart (crossing a daylight saving change moves it an hour). Then, if
// the refurb record has no first set time yet, stores this one there.
// Desktop leaves the host clock alone.
void CDateObject::SetSystemClock()
{
#ifdef _XBOX
	SYSTEMTIME wanted;
	FileTimeToSystemTime(&m_time, &wanted);
	XapiSetLocalTime(&wanted);

	SYSTEMTIME now;
	FILETIME nowTime;
	GetLocalTime(&now);
	SystemTimeToFileTime(&now, &nowTime);

	ULARGE_INTEGER a, b;
	a.LowPart = nowTime.dwLowDateTime;
	a.HighPart = nowTime.dwHighDateTime;
	b.LowPart = m_time.dwLowDateTime;
	b.HighPart = m_time.dwHighDateTime;
	ULONGLONG drift = a.QuadPart > b.QuadPart ? a.QuadPart - b.QuadPart : b.QuadPart - a.QuadPart;
	const ULONGLONG ONE_MINUTE = 60ull * 10000000;
	if (drift >= ONE_MINUTE)
		XapiSetLocalTime(&wanted);

	XBOX_REFURB_INFO refurb;
	if (NT_SUCCESS(ExReadWriteRefurbInfo(&refurb, sizeof(refurb), FALSE)) && refurb.FirstSetTime.QuadPart == 0)
	{
		refurb.FirstSetTime.LowPart = m_time.dwLowDateTime;
		refurb.FirstSetTime.HighPart = m_time.dwHighDateTime;
		ExReadWriteRefurbInfo(&refurb, sizeof(refurb), TRUE);
	}
#endif
}
