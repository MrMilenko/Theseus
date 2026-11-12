// debug.cpp: the desktop's logging backend. Trace prints to stderr; the
// HRESULT helpers format an error for the log. Counterpart to
// engine/debug.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"

// Prints like printf to stderr. A leading \001 (the Xbox's severity mark)
// is dropped; messages past 511 characters are cut.
extern "C" void Trace(const char* szMsg, ...)
{
	char text[512];
	va_list args;
	va_start(args, szMsg);
	vsnprintf(text, sizeof(text), szMsg, args);
	va_end(args);

	fputs(text[0] == '\001' ? text + 1 : text, stderr);
}

void TheseusGetErrorString(HRESULT hr, char* szErrorBuf, int cchErrorBuf)
{
	snprintf(szErrorBuf, cchErrorBuf, "HRESULT 0x%08x (facility %d, code %d)",
		(unsigned)hr, HRESULT_FACILITY(hr), HRESULT_CODE(hr));
}

const char* TheseusGetErrorString(HRESULT hr)
{
	static char text[100];
	TheseusGetErrorString(hr, text, sizeof(text));
	return text;
}

void LogComError(HRESULT hr, const char* szFunc/*= NULL*/)
{
	Trace("\001Error in function: %s\n\001%s\n", szFunc != NULL ? szFunc : "unknown", TheseusGetErrorString(hr));
}

void LogError(const char* szFunc)
{
	Trace("\001Error in function: %s\n", szFunc);
}

void Debug_Init()
{
}

void Debug_Exit()
{
}

void Debug_Frame()
{
}
