// debug.cpp: the Xbox debug build's trace, assert and error-text helpers.
// None of this is in the 5960 retail XBE: retail formats its alerts
// (0x0002c520) and throws them away. Theseus's own, and empty in retail.

#include "std.h"
#include "theseus.h"
#include "node.h"

#ifdef _DEBUG

#ifdef _XBOX
// Untagged trace lines are chatter; flip this to see them.
bool bShowComments = false;
#endif

// A message may start with a 1..7 tag byte (its color on the debug
// console). Tagged lines always print; on Xbox untagged ones are optional.
EXTERN_C void Trace(const TCHAR* szMsg, ...)
{
	if (*szMsg >= 1 && *szMsg <= 7)
		szMsg++;
#ifdef _XBOX
	else if (!bShowComments)
		return;
#endif

	TCHAR szLine[512];
	va_list args;
	va_start(args, szMsg);
	_vsntprintf(szLine, countof(szLine), szMsg, args);
	va_end(args);
	szLine[countof(szLine) - 1] = 0;
	OutputDebugString(szLine);
}

static const TCHAR* FacilityName(int nFacility)
{
	switch (nFacility)
	{
	case FACILITY_NULL:     return _T("null");
	case FACILITY_WIN32:    return _T("Win32");
	case FACILITY_WINDOWS:  return _T("Windows");
	case FACILITY_ITF:      return _T("ITF");
	case 0x876:             return _T("Direct3D");
	case 0x878:             return _T("DirectSound");
	}
	return _T("Unknown Facility");
}

// "<facility>: <D3DX text>", cut to fit.
void TheseusGetErrorString(HRESULT hr, TCHAR* szBuf, int cchBuf)
{
	if (cchBuf <= 0)
		return;
	_sntprintf(szBuf, cchBuf, _T("%s: "), FacilityName(HRESULT_FACILITY(hr)));
	szBuf[cchBuf - 1] = 0;
	int used = (int)_tcslen(szBuf);
	if (used < cchBuf - 1)
		D3DXGetErrorString(hr, szBuf + used, cchBuf - used);
}

const TCHAR* TheseusGetErrorString(HRESULT hr)
{
	static TCHAR szText[100];
	TheseusGetErrorString(hr, szText, countof(szText));
	return szText;
}

void LogComError(HRESULT hr, const char* szFunc)
{
	TCHAR szFuncW[100];
	Unicode(szFuncW, szFunc != NULL ? szFunc : "unknown", countof(szFuncW));
	Trace(_T("\001%s failed: %s\n"), szFuncW, TheseusGetErrorString(hr));
}

void LogError(const char* szFunc)
{
	Trace(_T("\001%hs failed\n"), szFunc);
}

// ASSERTHR lands here. True asks the caller to break into the debugger,
// which only makes sense with one attached.
EXTERN_C bool AssertFailed(const TCHAR* szFile, int nLine, HRESULT hr)
{
	if (hr == 0)
		Trace(_T("\001Assert failed: %s(%d), last error %u\n"), szFile, nLine, GetLastError());
	else
		Trace(_T("\001HRESULT 0x%08x at %s(%d): %s\n"), hr, szFile, nLine, TheseusGetErrorString(hr));
#ifdef _MAC_CROSS_COMPILE
	return false;
#else
	return true;
#endif
}

void Debug_Init()
{
	Trace(_T("\001Theseus debug build\n"));
}

void Debug_Exit()
{
	_CrtDumpMemoryLeaks();
}

// Walk the heap every five seconds and stop on the first sign of damage.
void Debug_Frame()
{
	static XTIME nextCheck;
	if (TheseusGetNow() < nextCheck)
		return;
	nextCheck = TheseusGetNow() + 5.0f;

	int status = _heapchk();
	if (status != _HEAPOK)
	{
		Trace(_T("\001Heap check failed (%d)\n"), status);
		_CrtDbgBreak();
	}
}

#endif
