// debug.cpp: desktop debug helpers. Implements TRACE / ASSERT and
// the LogComError / LogError pair declared in shared/theseus.h.
// Counterpart to engine/debug.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"

// Trace is always available (TRACE macro compiles it out in release via dead-code elimination)
extern "C" void Trace(const char* szMsg, ...)
{
	va_list args;
	va_start(args, szMsg);

	char szBuffer [512];
	vsnprintf(szBuffer, sizeof(szBuffer), szMsg, args);

	// Skip leading \001 control character (Xbox debug severity marker)
	const char* szOut = szBuffer;
	if (szOut[0] == '\001') szOut++;

	fprintf(stderr, "%s", szOut);

	va_end(args);
}

void TheseusGetErrorString(HRESULT hr, char* szErrorBuf, int cchErrorBuf)
{
	snprintf(szErrorBuf, cchErrorBuf, "HRESULT 0x%08x (facility %d, code %d)",
		(unsigned)hr, HRESULT_FACILITY(hr), HRESULT_CODE(hr));
}

const char* TheseusGetErrorString(HRESULT hr)
{
	static char szBuf [100];
	TheseusGetErrorString(hr, szBuf, countof(szBuf));
	return szBuf;
}

void LogComError(HRESULT hr, const char* szFunc/*= NULL*/)
{
	char szError [100];
	TheseusGetErrorString(hr, szError, countof(szError));
	Trace("\001Error in function: %s\n\001%s\n", szFunc == NULL ? "unknown" : szFunc, szError);
}

void LogError(const char* szFunc)
{
	Trace("\001Error in function: %s\n", szFunc);
}

extern "C" bool AssertFailed(const char* szFile, int nLine, HRESULT hr)
{
	if (hr == 0)
	{
		fprintf(stderr, "[ASSERT] %s:%d (last error 0x%x)\n", szFile, nLine, (unsigned)GetLastError());
	}
	else
	{
		char szComError [100];
		TheseusGetErrorString(hr, szComError, countof(szComError));
		fprintf(stderr, "[ASSERT] %s:%d: %s\n", szFile, nLine, szComError);
	}
	return false;
}

void Debug_Init()
{
}

void Debug_Exit()
{
}

void DumpHex(const uint8_t* pbData, int cbData, int cbMax/*=0*/)
{
	char szBuf [256];
	char szBuf2 [32];

	bool bTruncated = false;
	if (cbMax != 0 && cbData > cbMax)
	{
		bTruncated = true;
		cbData = cbMax;
	}

	char* pch = szBuf;
	char* pch2 = szBuf2;
	for (int i = 0; i < cbData; i += 1)
	{
		uint8_t b = *pbData++;

		pch += sprintf(pch, "%02x ", b);

		if (b >= ' ' && b < 128)
			*pch2++ = b;
		else
			*pch2++ = '.';

		if ((i & 15) == 15 || i == cbData - 1)
		{
			*pch2 = '\0';
			Trace("%-48s %s\n", szBuf, szBuf2);
			pch = szBuf;
			pch2 = szBuf2;
		}
	}

	if (bTruncated)
		Trace("...\n");
}

void Debug_Frame()
{
}

void DumpRegisteredClasses()
{
	TRACE("Registered Classes:\n");
	for (CNodeClass* pClass = CNodeClass::c_pFirstClass; pClass != NULL; pClass = pClass->m_nextClass)
	{
		TRACE("\t%s\n", pClass->m_className);
	}
}
