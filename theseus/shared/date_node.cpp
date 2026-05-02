// date_node.cpp: CDateObject, the JavaScript-style Date object exposed to
// XAP scripts. Decompiled from the 5960 retail XBE; see
// docs/decomp/Date.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "date_node.h"
#include "locale_node.h"
#ifdef _XBOX
#include "xconfig.h"
#endif

extern CObject** g_rgParam;
extern int g_nParam;
extern CRunner* g_pRunner;
extern CObject* Dereference(CObject* pObject);

// =========================================================================
// Node registration. Names must match what XAP scripts call.
// =========================================================================

IMPLEMENT_NODE("Date", CDateObject, CObject)

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

// =========================================================================
// Internal helpers
// =========================================================================

// Convert stored FILETIME to local SYSTEMTIME
static void ToLocalSystemTime(const FILETIME& ft, SYSTEMTIME* pST)
{
	FILETIME local;
	FileTimeToLocalFileTime(&ft, &local);
	FileTimeToSystemTime(&local, pST);
}

// Convert stored FILETIME to UTC SYSTEMTIME
static void ToUTCSystemTime(const FILETIME& ft, SYSTEMTIME* pST)
{
	FileTimeToSystemTime(&ft, pST);
}

// Pull a numeric param from the script VM
static int GetNumParam(int idx)
{
	CNumObject* pNum = g_rgParam[idx]->ToNum();
	int val = (int)pNum->m_value;
	pNum->Release();
	return val;
}

// =========================================================================
// Calendar utilities (exposed to XAP scripts)
// =========================================================================

int CDateObject::isLeapYear(int nYear)
{
	// Divisible by 4, except centuries unless also divisible by 400
	return (nYear % 4 == 0 && (nYear % 100 != 0 || nYear % 400 == 0)) ? 1 : 0;
}

int CDateObject::getDaysInMonth(int nMonth, int nYear)
{
	static const short days[] = { 31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (nMonth == 2)
		return isLeapYear(nYear) ? 29 : 28;

	return days[nMonth - 1];
}

// =========================================================================
// Constructor
// =========================================================================

CDateObject::CDateObject()
{
	SYSTEMTIME st;

	if (g_nParam == 0)
	{
		// No args: current system time
		GetSystemTime(&st);
	}
	else if (g_nParam >= 3 && g_nParam <= 7)
	{
		// 3-7 args: year, month(0-based), day, [hour], [min], [sec], [ms]
		ZeroMemory(&st, sizeof(st));

		for (int i = 0; i < g_nParam; i++)
			g_rgParam[i] = Dereference(g_rgParam[i]);

		st.wYear = (WORD)GetNumParam(0);
		if (st.wYear < 100)
			st.wYear += 1900;

		st.wMonth = (WORD)(GetNumParam(1) + 1); // JS months are 0-based
		st.wDay = (WORD)GetNumParam(2);

		if (g_nParam > 3) st.wHour = (WORD)GetNumParam(3);
		if (g_nParam > 4) st.wMinute = (WORD)GetNumParam(4);
		if (g_nParam > 5) st.wSecond = (WORD)GetNumParam(5);
		if (g_nParam > 6) st.wMilliseconds = (WORD)GetNumParam(6);
	}
	else
	{
		g_pRunner->Error(_T("Wrong number of parameters to Date constructor\n"));
		return;
	}

	VERIFY(SystemTimeToFileTime(&st, &m_time));
}

// =========================================================================
// Local time accessors
// =========================================================================

int CDateObject::getDate()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wDay;
}

int CDateObject::getDay()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wDayOfWeek;
}

int CDateObject::getFullYear()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wYear;
}

int CDateObject::getHours()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wHour;
}

int CDateObject::getMilliseconds()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wMilliseconds;
}

int CDateObject::getMinutes()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wMinute;
}

int CDateObject::getMonth()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wMonth - 1; // JS months are 0-based
}

int CDateObject::getSeconds()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);
	return st.wSecond;
}

// =========================================================================
// UTC accessors
// =========================================================================

int CDateObject::getUTCDate()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wDay;
}

int CDateObject::getUTCDay()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wDayOfWeek;
}

int CDateObject::getUTCFullYear()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wYear;
}

int CDateObject::getUTCHours()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wHour;
}

int CDateObject::getUTCMilliseconds()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wMilliseconds;
}

int CDateObject::getUTCMinutes()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wMinute;
}

int CDateObject::getUTCMonth()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wMonth - 1;
}

int CDateObject::getUTCSeconds()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);
	return st.wSecond;
}

// =========================================================================
// Deprecated JS accessor
// =========================================================================

int CDateObject::getYear()
{
	int nYear = getFullYear();
	if (nYear >= 1900 && nYear < 2000)
		return nYear - 1900;
	return nYear;
}

// =========================================================================
// String formatting
// =========================================================================

static const TCHAR* s_rgszMonth3[] = {
	_T("Jan"), _T("Feb"), _T("Mar"), _T("Apr"), _T("May"), _T("Jun"),
	_T("Jul"), _T("Aug"), _T("Sep"), _T("Oct"), _T("Nov"), _T("Dec")
};

CStrObject* CDateObject::toGMTString()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);

	TCHAR szBuf[32];
	_stprintf(szBuf, _T("%02d %s %d %02d:%02d:%02d GMT"),
		st.wDay, s_rgszMonth3[st.wMonth - 1], st.wYear,
		st.wHour, st.wMinute, st.wSecond);
	return new CStrObject(szBuf);
}

CStrObject* CDateObject::toLocaleString()
{
	SYSTEMTIME st;
	ToLocalSystemTime(m_time, &st);

	TCHAR szBuf[32];
	FormatTime(szBuf, countof(szBuf), &st);
	return new CStrObject(szBuf);
}

CStrObject* CDateObject::toUTCString()
{
	SYSTEMTIME st;
	ToUTCSystemTime(m_time, &st);

	TCHAR szBuf[32];
	_stprintf(szBuf, _T("%02d %s %d %02d:%02d:%02d UTC"),
		st.wDay, s_rgszMonth3[st.wMonth - 1], st.wYear,
		st.wHour, st.wMinute, st.wSecond);
	return new CStrObject(szBuf);
}

// =========================================================================
// System clock (Xbox kernel)
// =========================================================================

void CDateObject::SetSystemClock()
{
#ifdef _XBOX
	SYSTEMTIME st;
	FileTimeToSystemTime(&m_time, &st);

	VERIFY(XapiSetLocalTime(&st));

	// DST boundary correction:
	// If the old time was in daylight saving and the new time is in standard
	// (or vice versa), the result is off by one hour. Detect by checking if
	// the time we just set differs from what we wanted by more than a minute,
	// and re-set if so.
	SYSTEMTIME verify;
	FILETIME verifyFT;
	GetLocalTime(&verify);
	VERIFY(SystemTimeToFileTime(&verify, &verifyFT));

	LONGLONG offset = *((LONGLONG*)&verifyFT) - *((LONGLONG*)&m_time);
	if (offset < 0) offset = -offset;

	if (offset >= 600000000) // 60 seconds in 100ns ticks
		VERIFY(XapiSetLocalTime(&st));

	// If this is the first time the clock has been set, record it in the
	// refurb sector so the kernel knows the console has been initialized.
	XBOX_REFURB_INFO refurb;
	NTSTATUS status = ExReadWriteRefurbInfo(&refurb, sizeof(refurb), FALSE);
	if (NT_SUCCESS(status) && refurb.FirstSetTime.QuadPart == 0)
	{
		refurb.FirstSetTime.LowPart = m_time.dwLowDateTime;
		refurb.FirstSetTime.HighPart = m_time.dwHighDateTime;
		ExReadWriteRefurbInfo(&refurb, sizeof(refurb), TRUE);
	}
#endif
}
