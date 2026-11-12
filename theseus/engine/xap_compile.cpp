// xap_compile.cpp: the XAP compiler.
//
// One file holds the whole path from script text to bytecode: a character
// scanner (SkipWhite, Token), typed value readers for node properties, the
// statement and expression compiler (CFunctionCompiler) and the scene compiler
// (CClassCompiler) that turns node declarations into construction ops.
// Every body here follows the 5960 retail XBE; addresses and the decompile
// notes are in docs/decomp/Compiler.md. Opcode numbers live in runner.h.
#include "std.h"
#include "theseus.h"
#include "xap_compile.h"
#include "node.h"
#include "runner.h"


// Parse state shared by every compiler in flight. 5960 keeps the same four
// globals: file name @0x17ae90, line @0x17ae94, error flag @0x17ac80 and the
// #file name buffer @0x17ac88.
int g_nLine;
const TCHAR* g_szFileName;
static TCHAR g_szFileNameBuf[MAX_PATH];
bool g_bParseError;

// The class namespace, 5960's list head @0x17ae98.
CNameSpace* g_classes;

// Every hard failure hands back an empty string so callers stop cleanly.
// 5960 points at a shared L"" (0x201c4 / 0x21b98) the same way.
static const TCHAR szGiveUp[] = _T("");

static inline bool IsLetter(TCHAR ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static inline bool IsDigit(TCHAR ch)
{
	return ch >= '0' && ch <= '9';
}

// Characters that make up a number or bare word: letters, digits, '.' and '-'.
// 5960 also lets '_' through here; Theseus never has, so a '_' ends the run.
static inline bool IsWordChar(TCHAR ch)
{
	return IsLetter(ch) || IsDigit(ch) || ch == '.' || ch == '-';
}

static inline bool IsIdentChar(TCHAR ch)
{
	return IsLetter(ch) || IsDigit(ch) || ch == '_';
}

static inline bool IsKeyword(const TCHAR* pchTok, int cchTok, const TCHAR* szKeyword, int cchKeyword)
{
	return cchTok == cchKeyword && _tcsncmp(pchTok, szKeyword, cchKeyword) == 0;
}


// StartParse / EndParse
// 5960 has no separate functions for these; CClass::ParseFile @0x33d00 sets
// file = name, line = 1, error = 0 before compiling and clears file and line
// after. Theseus splits that into a pair so the line can start elsewhere.
void StartParse(const TCHAR* pch, const TCHAR* szFileName, int nLine)
{
	g_bParseError = false;
	g_nLine = nLine;
	g_szFileName = szFileName;
}

void EndParse()
{
	g_nLine = 0;
	g_szFileName = NULL;
}


// SyntaxError (free function)
// Reconstructed from 5960 retail XBE @ 0x00032cf0. Ghidra decompile:
//     vswprintf(msg, 0x200, fmt, args); msg[511] = 0;
//     swprintf(out, <"Syntax Error ... File: %s Line: %d ... %s">, file, line, msg);
//     g_bParseError = 1;
// Retail builds the text and drops it; Theseus sends it to Trace so it shows up.
void SyntaxError(const TCHAR* szMsg, ...)
{
	TCHAR szDetail[512];
	va_list args;
	va_start(args, szMsg);
	_vsntprintf(szDetail, countof(szDetail), szMsg, args);
	va_end(args);

	TCHAR szReport[1024];
	_stprintf(szReport, _T("Syntax Error\n\nFile: %s\nLine: %d\n\n%s"), g_szFileName, g_nLine, szDetail);
	Trace(_T("\007%s\n"), szReport);

	g_bParseError = true;
}


// SkipWhite
// Reconstructed from 5960 retail XBE @ 0x00032d70. Behavior: loop eating
// control characters and spaces (counting '\n' into the line), then, if the
// comment mask allows it, one comment: '#' or '//' runs to end of line, '/*'
// runs to the matching '*/' counting lines. A '#' line can be a directive:
// "#line N" sets the line so the next one reads N, "#file name" copies the
// name (optionally quoted) into the file buffer and makes it current.
// Returns at the first character that is neither blank nor a comment.
const TCHAR* SkipWhite(const TCHAR* pch, COMMENT_TYPE ct)
{
	for (;;)
	{
		for (; *pch != '\0' && *pch <= ' '; pch++)
		{
			if (*pch == '\n')
				g_nLine++;
		}

		bool bPound = (ct & pound) && pch[0] == '#';
		bool bSlashSlash = (ct & slashslash) && pch[0] == '/' && pch[1] == '/';
		bool bSlashStar = (ct & slashstar) && pch[0] == '/' && pch[1] == '*';

		if (bPound || bSlashSlash)
		{
			if (bPound)
			{
				do
					pch++;
				while (*pch == ' ' || *pch == '\t');

				if (_tcsncmp(pch, _T("line"), 4) == 0)
				{
					for (pch += 4; *pch == ' ' || *pch == '\t'; pch++)
						;
					g_nLine = _ttoi(pch) - 1;
				}
				else if (_tcsncmp(pch, _T("file"), 4) == 0)
				{
					for (pch += 4; *pch == ' ' || *pch == '\t'; pch++)
						;
					if (*pch == '"')
						pch++;

					TCHAR* pchName = g_szFileNameBuf;
					for (; *pch != '\0' && *pch != '\r' && *pch != '\n' && *pch != '"'; pch++)
						*pchName++ = *pch;
					*pchName = '\0';
					g_szFileName = g_szFileNameBuf;
				}
			}

			while (*pch != '\0' && *pch != '\r' && *pch != '\n')
				pch++;
			continue;
		}

		if (bSlashStar)
		{
			for (pch += 2; *pch != '\0'; pch++)
			{
				if (pch[0] == '*' && pch[1] == '/')
				{
					pch += 2;
					break;
				}
				if (*pch == '\n')
					g_nLine++;
			}
			continue;
		}

		return pch;
	}
}


// Token
// Reconstructed from 5960 retail XBE @ 0x00032f70. Behavior: skip white, then
// one of four shapes. A '"' or '\'' opens a quoted token: the token is the
// text inside, '\\' protects the next character, and the closing quote is
// consumed. With bAllowPaths the token is everything up to the next blank.
// A letter or '_' starts an identifier (letters, digits, '_'). Otherwise a
// word character starts a number or bare word, and anything else starts an
// operator run that lasts until a blank or a word character.
const TCHAR* Token(const TCHAR* pch, const TCHAR*& pchTok, int& cchTok, bool bAllowPaths, COMMENT_TYPE ct)
{
	pch = SkipWhite(pch, ct);

	TCHAR chQuote = 0;
	if (*pch == '"' || *pch == '\'')
		chQuote = *pch++;

	pchTok = pch;

	if (chQuote != 0)
	{
		for (; *pch != '\0' && *pch != chQuote; pch++)
		{
			if (pch[0] == '\\' && pch[1] != '\0')
				pch++;
		}
	}
	else if (bAllowPaths)
	{
		while (*pch > ' ')
			pch++;
	}
	else if (IsLetter(*pch) || *pch == '_')
	{
		while (IsIdentChar(*pch))
			pch++;
	}
	else if (IsWordChar(*pch))
	{
		while (IsWordChar(*pch))
			pch++;
	}
	else
	{
		while (*pch > ' ' && !IsWordChar(*pch))
			pch++;
	}

	cchTok = (int)(pch - pchTok);

	if (chQuote != 0 && *pch == chQuote)
		pch++;

	return pch;
}


// Reads exactly cDigits hex digits for \x and \u. Returns false on a bad one.
static bool ReadHexValue(const TCHAR*& pch, int& cch, int cDigits, int& nOut)
{
	int nValue = 0;
	for (int i = 0; i < cDigits; i++)
	{
		TCHAR ch = *pch++;
		cch--;

		int nDigit;
		if (ch >= '0' && ch <= '9')
			nDigit = ch - '0';
		else if (ch >= 'A' && ch <= 'F')
			nDigit = ch - 'A' + 10;
		else if (ch >= 'a' && ch <= 'f')
			nDigit = ch - 'a' + 10;
		else
			return false;

		nValue = nValue * 16 + nDigit;
	}
	nOut = nValue;
	return true;
}

static bool ReadHexEscape(const TCHAR*& pch, int& cch, int cDigits, TCHAR& chOut)
{
	int nValue;
	if (!ReadHexValue(pch, cch, cDigits, nValue))
		return false;
	chOut = (TCHAR)nValue;
	return true;
}

// ExpandCString
// Reconstructed from 5960 retail XBE @ 0x00033690. Behavior: copy the token
// into the buffer turning escapes into characters. \b \f \n \r \t map to their
// control codes, \xHH takes two hex digits, \uHHHH four, \0 not followed by an
// octal digit is a NUL, other \ooo octal runs build one character, and any
// other escaped character stands for itself. Fails with -1 and a syntax error
// when the output fills, the string ends on a backslash, or a hex escape is
// short or bad. Returns the number of characters written.
// \u is one UTF-16 unit in wide builds (5960 is wide) and UTF-8 bytes in narrow ones.
int ExpandCString(TCHAR* szOut, int cchOut, const TCHAR* pchTok, int cchTok)
{
	TCHAR* pchOut = szOut;
	TCHAR* pchEnd = szOut + cchOut;

	while (cchTok > 0)
	{
		if (pchOut >= pchEnd)
		{
			SyntaxError(_T("string constant is too long"));
			return -1;
		}

		TCHAR ch = *pchTok++;
		cchTok--;

		if (ch != '\\')
		{
			*pchOut++ = ch;
			continue;
		}

		if (cchTok == 0)
		{
			SyntaxError(_T("backslash at end of string"));
			return -1;
		}

		ch = *pchTok++;
		cchTok--;

		switch (ch)
		{
		case 'b':
			ch = '\b';
			break;

		case 'f':
			ch = '\f';
			break;

		case 'n':
			ch = '\n';
			break;

		case 'r':
			ch = '\r';
			break;

		case 't':
			ch = '\t';
			break;

		case 'x':
			if (cchTok < 2)
			{
				SyntaxError(_T("Invalid hex character escape sequence"));
				return -1;
			}
			if (!ReadHexEscape(pchTok, cchTok, 2, ch))
			{
				SyntaxError(_T("Invalid hex character"));
				return -1;
			}
			break;

#ifdef _UNICODE
		case 'u':
			if (cchTok < 4)
			{
				SyntaxError(_T("Invalid Unicode character escape sequence"));
				return -1;
			}
			if (!ReadHexEscape(pchTok, cchTok, 4, ch))
			{
				SyntaxError(_T("Invalid Unicode character"));
				return -1;
			}
			break;
#else
		// Theseus: narrow builds carry UTF-8, so \u becomes its UTF-8 bytes.
		case 'u':
			{
				int cp;
				if (cchTok < 4 || !ReadHexValue(pchTok, cchTok, 4, cp))
				{
					SyntaxError(_T("Invalid Unicode character escape sequence"));
					return -1;
				}
				char utf8[3];
				int nBytes;
				if (cp < 0x80)
				{
					utf8[0] = (char)cp;
					nBytes = 1;
				}
				else if (cp < 0x800)
				{
					utf8[0] = (char)(0xC0 | (cp >> 6));
					utf8[1] = (char)(0x80 | (cp & 0x3F));
					nBytes = 2;
				}
				else
				{
					utf8[0] = (char)(0xE0 | (cp >> 12));
					utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
					utf8[2] = (char)(0x80 | (cp & 0x3F));
					nBytes = 3;
				}
				if (pchOut + nBytes > pchEnd)
				{
					SyntaxError(_T("string constant is too long"));
					return -1;
				}
				for (int i = 0; i < nBytes; i++)
					*pchOut++ = utf8[i];
			}
			continue;
#endif

		default:
			if (ch < '0' || ch > '7')
				break;

			if (ch == '0' && cchTok > 0 && (*pchTok < '0' || *pchTok > '7'))
			{
				ch = '\0';
				break;
			}

			{
				int nValue = ch - '0';
				for (; cchTok != 0 && *pchTok >= '0' && *pchTok <= '7'; pchTok++, cchTok--)
					nValue = nValue * 8 + (*pchTok - '0');
				ch = (TCHAR)nValue;
			}
			break;
		}

		*pchOut++ = ch;
	}

	return (int)(pchOut - szOut);
}


// ParseBoolean
// Reconstructed from 5960 retail XBE @ 0x00033150. Ghidra decompile:
//     pch = Token(pch, &tok, &cch);
//     if (!wcsnicmp(tok, L"true", cch)) b = 1; else if (!wcsnicmp(tok, L"false", cch)) b = 0;
//     else SyntaxError(L"Invalid boolean value");
// Case-insensitive prefix compare over the token length, same as the binary.
const TCHAR* ParseBoolean(const TCHAR* pch, bool& b)
{
	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);

	if (_tcsnicmp(pchTok, _T("true"), cchTok) == 0)
		b = true;
	else if (_tcsnicmp(pchTok, _T("false"), cchTok) == 0)
		b = false;
	else
		SyntaxError(_T("Invalid boolean value"));

	return pch;
}

// Copies a numeric token into a terminated buffer. 5960 copies blind into a
// 64 character stack buffer; Theseus clamps long tokens and flags them.
static void CopyNumberToken(TCHAR* szBuf, int cchBuf, const TCHAR* pchTok, int cchTok)
{
	if (!CHECK(cchTok < cchBuf))
	{
		SyntaxError(_T("Number is too long"));
		cchTok = cchBuf - 1;
	}
	_tcsncpy(szBuf, pchTok, cchTok);
	szBuf[cchTok] = '\0';
}

// ParseInteger / ParseNumber
// Reconstructed from 5960 retail XBE @ 0x000331d0 / 0x00033250. Ghidra decompile:
//     pch = Token(pch, &tok, &cch); wcsncpy(buf, tok, cch); buf[cch] = 0;   // buf[64]
//     *pi = _wtol(buf);        or        *pn = (float)wcstod(buf, NULL);
// The CHECK on the length is Theseus's bounds fix.
const TCHAR* ParseInteger(const TCHAR* pch, int& i)
{
	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);

	TCHAR szNumber[64];
	CopyNumberToken(szNumber, countof(szNumber), pchTok, cchTok);
	i = _ttoi(szNumber);
	return pch;
}

const TCHAR* ParseNumber(const TCHAR* pch, float& n)
{
	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);

	TCHAR szNumber[64];
	CopyNumberToken(szNumber, countof(szNumber), pchTok, cchTok);
	n = (float)_tcstod(szNumber, NULL);
	return pch;
}

// ParseString
// Reconstructed from 5960 retail XBE @ 0x000332d0. Ghidra decompile:
//     pch = Token(pch, &tok, &cch); sz = new WCHAR[cch + 1]; memcpy(sz, tok, cch * 2); sz[cch] = 0;
// No escape expansion: property strings are taken as written.
const TCHAR* ParseString(const TCHAR* pch, TCHAR*& sz)
{
	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);

	sz = new TCHAR[cchTok + 1];
	CopyChars(sz, pchTok, cchTok);
	sz[cchTok] = '\0';
	return pch;
}

// ParseVec3 / ParseVec4
// Reconstructed from 5960 retail XBE @ 0x00033340 / 0x00033370. Ghidra decompile:
//     ParseNumber(v + 0); ParseNumber(v + 1); ParseNumber(v + 2); [ParseNumber(v + 3);]
const TCHAR* ParseVec3(const TCHAR* pch, float v[3])
{
	for (int i = 0; i < 3; i++)
		pch = ParseNumber(pch, v[i]);
	return pch;
}

const TCHAR* ParseVec4(const TCHAR* pch, float v[4])
{
	for (int i = 0; i < 4; i++)
		pch = ParseNumber(pch, v[i]);
	return pch;
}

// The array readers stop at the end of the text instead of spinning on it
// forever. Theseus fix; 5960 has no such check.
static bool ArrayRanOut(const TCHAR* pch)
{
	if (*pch != '\0')
		return false;
	SyntaxError(_T("Expected ']' to end array"));
	return true;
}

// ParseIntArray / ParseVecArray
// Reconstructed from 5960 retail XBE @ 0x000333a0 / 0x00033490. Behavior:
// demand '[', then walk the list twice. The first walk only counts values (a
// group of nVecSize numbers per entry, entries split by an optional ','),
// stopping at ']'. Then it allocates count * 4 bytes, rewinds the cursor and
// the line counter to just after '[', and walks again storing each value.
// Returns past the ']'. A missing '[' is a syntax error and returns "".
// 5960 inlines the number read in ParseVecArray; Theseus calls ParseNumber so
// the long token CHECK covers arrays too.
const TCHAR* ParseIntArray(const TCHAR* pch, BYTE*& pbItems, int& cbArray)
{
	pch = SkipWhite(pch);
	if (*pch != '[')
	{
		SyntaxError(_T("Expected '[' to start array"));
		return szGiveUp;
	}

	const TCHAR* pchFirst = pch + 1;
	const int nLineFirst = g_nLine;

	int cValues = 0;
	for (pch = SkipWhite(pchFirst); *pch != ']' && !ArrayRanOut(pch); pch = SkipWhite(pch))
	{
		int nIgnored;
		pch = ParseInteger(pch, nIgnored);
		cValues++;

		pch = SkipWhite(pch);
		if (*pch == ']')
			break;
		if (*pch == ',')
			pch++;
	}

	if (*pch == '\0')
		return pch;

	cbArray = cValues * sizeof(int);
	pbItems = new BYTE[cbArray];
	int* pnOut = (int*)pbItems;

	g_nLine = nLineFirst;
	for (pch = SkipWhite(pchFirst); *pch != ']' && !ArrayRanOut(pch); pch = SkipWhite(pch))
	{
		pch = ParseInteger(pch, *pnOut++);

		pch = SkipWhite(pch);
		if (*pch == ']')
			break;
		if (*pch == ',')
			pch++;
	}

	return pch + 1;
}

const TCHAR* ParseVecArray(const TCHAR* pch, BYTE*& pbItems, int& cbArray, int nVecSize)
{
	pch = SkipWhite(pch);
	if (*pch != '[')
	{
		SyntaxError(_T("Expected '[' to start array"));
		return szGiveUp;
	}

	const TCHAR* pchFirst = pch + 1;
	const int nLineFirst = g_nLine;

	int cValues = 0;
	for (pch = SkipWhite(pchFirst); *pch != ']' && !ArrayRanOut(pch); pch = SkipWhite(pch))
	{
		for (int i = 0; i < nVecSize; i++)
		{
			float nIgnored;
			pch = ParseNumber(pch, nIgnored);
		}
		cValues += nVecSize;

		pch = SkipWhite(pch);
		if (*pch == ']')
			break;
		if (*pch == ',')
			pch++;
	}

	if (*pch == '\0')
		return pch;

	cbArray = cValues * sizeof(float);
	pbItems = new BYTE[cbArray];
	float* pnOut = (float*)pbItems;

	g_nLine = nLineFirst;
	for (pch = SkipWhite(pchFirst); *pch != ']' && !ArrayRanOut(pch); pch = SkipWhite(pch))
	{
		for (int i = 0; i < nVecSize; i++)
			pch = ParseNumber(pch, *pnOut++);

		pch = SkipWhite(pch);
		if (*pch == ']')
			break;
		if (*pch == ',')
			pch++;
	}

	return pch + 1;
}


// CCompiler::CCompiler / ~CCompiler
// 5960 inlines the constructor (see the CFunctionCompiler set up in
// ParseFunction @0x36c30: ops count, buffer, size and error all zeroed).
// The destructor @0x000346f0 frees the op buffer.
CCompiler::CCompiler()
{
	m_ops = NULL;
	m_nop = 0;
	m_opsSize = 0;
	m_bError = false;
}

CCompiler::~CCompiler()
{
	delete[] m_ops;
}

// CCompiler::GrowTo
// Reconstructed from 5960 retail XBE @ 0x00034590. Ghidra decompile:
//     cb = (n + 0xfff) & ~0xfff; p = new BYTE[cb]; memcpy(p, m_ops, m_nop);
//     delete m_ops; m_opsSize = cb; m_ops = p;
// The buffer grows in whole 4 KB pages.
void CCompiler::GrowTo(int nNewSize)
{
	const int cbPage = 4096;
	int cbNew = (nNewSize + cbPage - 1) / cbPage * cbPage;

	BYTE* pbNew = new BYTE[cbNew];
	CopyMemory(pbNew, m_ops, m_nop);
	delete[] m_ops;

	m_ops = pbNew;
	m_opsSize = cbNew;
}

// CCompiler::SyntaxError
// Reconstructed from 5960 retail XBE @ 0x000345e0. Same report as the free
// SyntaxError, and it also marks this compiler: m_bError = 1; g_bParseError = 1.
// Theseus sends the text to Trace.
void CCompiler::SyntaxError(const TCHAR* szMsg, ...)
{
	TCHAR szDetail[512];
	va_list args;
	va_start(args, szMsg);
	_vsntprintf(szDetail, countof(szDetail), szMsg, args);
	va_end(args);

	TCHAR szReport[1024];
	_stprintf(szReport, _T("Syntax Error\n\nFile: %s\nLine: %d\n\n%s"), g_szFileName, g_nLine, szDetail);
	Trace(_T("\007%s\n"), szReport);

	m_bError = true;
	g_bParseError = true;
}

// CCompiler::CreateFunction
// Reconstructed from 5960 retail XBE @ 0x00034670. Ghidra decompile:
//     f = new(m_nop) CFunction; f->m_codeSize = m_nop; memcpy(f->m_rgop, m_ops, m_nop);
// One allocation holds the function header and its ops.
CFunction* CCompiler::CreateFunction()
{
#pragma push_macro("new")
#undef new
	CFunction* pFn = new(m_nop) CFunction;
#pragma pop_macro("new")

	pFn->m_codeSize = m_nop;
	CopyMemory(pFn->m_rgop, m_ops, m_nop);
	return pFn;
}


// CFunctionCompiler::CFunctionCompiler
// Inlined in 5960 (ParseFunction @0x36c30): no locals, no pending breaks, not
// inside a loop, loop top at 0, behavior flag from the caller.
CFunctionCompiler::CFunctionCompiler()
{
	m_nFrameSize = 0;
	m_breaks = NULL;
	m_nBreakables = 0;
	m_nopTopOfLoop = 0;
	m_bBehavior = false;
}

// Binary operator table, 22 entries of { ch1, ch2, ch3, priority, opcode }.
// Read straight out of the 5960 XBE @ 0x000200e8. Scanned in order and the
// first match wins, so the order is part of the language.
static const DOPER rgdoper[] =
{
	{ '.',	0,		0,		1, opDot },
	{ '*',	0,		0,		2, opMul },
	{ '/',	0,		0,		2, opDiv },
	{ '+',	0,		0,		3, opAdd },
	{ '-',	0,		0,		3, opSub },
	{ '<',	'<',	0,		4, opSHL },
	{ '>',	'>',	0,		4, opSHR },
	{ '<',	'=',	0,		5, opLE },
	{ '>',	'=',	0,		5, opGE },
	{ '<',	0,		0,		5, opLT },
	{ '>',	0,		0,		5, opGT },
	{ '=',	'=',	0,		6, opEQ },
	{ '!',	'=',	0,		6, opNE },
	{ '&',	0,		0,		7, opAnd },
	{ '^',	0,		0,		8, opXor },
	{ '|',	0,		0,		9, opOr },
	{ '=',	0,		0,		13, opAssign },
	{ '+',	'=',	0,		13, opAddAssign },
	{ '-',	'=',	0,		13, opSubAssign },
	{ '*',	'=',	0,		13, opMulAssign },
	{ '/',	'=',	0,		13, opDivAssign },
	{ '%',	'=',	0,		13, opModAssign },
};

// ParseExpression always starts at priority 14 (0xe in every 5960 call).
static const int nLowestPriority = 14;

// CFunctionCompiler::ParseOperator
// Reconstructed from 5960 retail XBE @ 0x00034bb0. Ghidra decompile:
//     *ppdoper = 0; pch = SkipWhite(pch); if (*pch == 0) return pch;
//     for (i = 0; i < 22; i++) if (t[i].ch1 == pch[0] && (!t[i].ch2 || t[i].ch2 == pch[1])) {
//         *ppdoper = &t[i]; return pch + (t[i].ch2 ? 2 : 1); }
//     return pch;
const TCHAR* CFunctionCompiler::ParseOperator(const TCHAR* pch, const DOPER*& pOp)
{
	pOp = NULL;

	pch = SkipWhite(pch);
	if (*pch == '\0')
		return pch;

	for (const DOPER* p = rgdoper; p < rgdoper + countof(rgdoper); p++)
	{
		if (p->m_ch1 != pch[0])
			continue;
		if (p->m_ch2 != 0 && p->m_ch2 != pch[1])
			continue;

		pOp = p;
		return pch + (p->m_ch2 != 0 ? 2 : 1);
	}

	return pch;
}

// CFunctionCompiler::ParseTerm
// Reconstructed from 5960 retail XBE @ 0x00034710. Behavior: fold any run of
// unary '-', '+' and '~' ('~' only raises "not implemented"). Then either a
// parenthesized expression, or one token: a quoted string becomes opStr with
// its escapes expanded; a token starting with '.', '-' or a digit becomes
// opNum (a pending '-' is folded into the constant); true/false become opNum
// 1/0; this and null become opThis/opNull; "new Name(args)" becomes opNew
// with the arg count and class name; a declared local becomes opLocal with
// its slot; anything else is a late-bound opVar by name. A '-' that wasn't
// folded emits opNeg at the end. See docs/decomp/Compiler.md.
const TCHAR* CFunctionCompiler::ParseTerm(const TCHAR* pch)
{
	bool bNegate = false;
	bool bComplement = false;

	for (;; pch++)
	{
		pch = SkipWhite(pch);
		if (*pch == '-')
			bNegate = !bNegate;
		else if (*pch == '~')
			bComplement = !bComplement;
		else if (*pch != '+')
			break;
	}

	if (bComplement)
		SyntaxError(_T("Sorry, the ~ operator has not been implemented yet!"));

	if (*pch == '(')
	{
		pch = ParseExpression(pch + 1);
		pch = SkipWhite(pch);
		if (*pch != ')')
		{
			SyntaxError(_T("Missing ')'"));
			return pch;
		}
		pch++;
	}
	else
	{
		const bool bString = (*pch == '"' || *pch == '\'');

		const TCHAR* pchTok;
		int cchTok;
		pch = Token(pch, pchTok, cchTok);

		if (bString)
		{
			Write(opStr);
			TCHAR szText[1024];
			int cchText = ExpandCString(szText, countof(szText), pchTok, cchTok);
			if (cchText < 0)
				return szGiveUp;
			WriteString(szText, cchText);
		}
		else if (cchTok == 0)
		{
			return pch;
		}
		else if (*pchTok == '.' || *pchTok == '-' || IsDigit(*pchTok))
		{
			// Theseus clamps over-long numbers; 5960 copies them blind.
			TCHAR szNumber[256];
			if (!CHECK(cchTok < countof(szNumber)))
			{
				SyntaxError(_T("Number is too long"));
				cchTok = countof(szNumber) - 1;
			}
			CopyChars(szNumber, pchTok, cchTok);
			szNumber[cchTok] = '\0';

			float nValue = (float)_tcstod(szNumber, NULL);
			if (bNegate)
			{
				nValue = -nValue;
				bNegate = false;
			}
			Write(opNum);
			WriteNumber(nValue);
		}
		else if (IsKeyword(pchTok, cchTok, _T("true"), 4))
		{
			Write(opNum);
			WriteNumber(1.0f);
		}
		else if (IsKeyword(pchTok, cchTok, _T("false"), 5))
		{
			Write(opNum);
			WriteNumber(0.0f);
		}
		else if (IsKeyword(pchTok, cchTok, _T("this"), 4))
		{
			Write(opThis);
		}
		else if (IsKeyword(pchTok, cchTok, _T("null"), 4))
		{
			Write(opNull);
		}
		else if (IsKeyword(pchTok, cchTok, _T("new"), 3))
		{
			const TCHAR* pchClass;
			int cchClass;
			pch = Token(pch, pchClass, cchClass);

			int cArgs = 0;
			pch = SkipWhite(pch);
			if (*pch == '(')
			{
				pch++;
				if (*pch != ')')
				{
					for (;;)
					{
						pch = ParseExpression(pch);
						cArgs++;
						pch = SkipWhite(pch);
						if (*pch == ')')
							break;
						if (*pch != ',')
						{
							SyntaxError(_T("Missing ','"));
							return pch;
						}
						pch++;
					}
				}
				pch++;
			}

			Write(opNew);
			WriteInteger(cArgs);
			WriteString(pchClass, cchClass);
		}
		else
		{
			int iLocal = 0;
			while (iLocal < m_nFrameSize &&
			       !(m_rgstLocal[iLocal].cchName == cchTok && _tcsncmp(pchTok, m_rgstLocal[iLocal].pchName, cchTok) == 0))
			{
				iLocal++;
			}

			if (iLocal < m_nFrameSize)
			{
				Write(opLocal);
				WriteInteger(iLocal);
			}
			else
			{
				Write(opVar);
				WriteString(pchTok, cchTok);
			}
		}
	}

	if (bNegate)
		Write(opNeg);

	return pch;
}

// CFunctionCompiler::ParseArray
// Reconstructed from 5960 retail XBE @ 0x00034c20. Ghidra decompile:
//     pch = ParseExpression(pch + 1); pch = SkipWhite(pch);
//     if (*pch != ']') { SyntaxError(L"Missing ']'"); return pch; }
//     Write(opArray); return pch + 1;
const TCHAR* CFunctionCompiler::ParseArray(const TCHAR* pch)
{
	pch = SkipWhite(ParseExpression(pch + 1));
	if (*pch != ']')
	{
		SyntaxError(_T("Missing ']'"));
		return pch;
	}

	Write(opArray);
	return pch + 1;
}

// CFunctionCompiler::ParseCall
// Reconstructed from 5960 retail XBE @ 0x00034c90. Ghidra decompile:
//     n = 0; pch = SkipWhite(pch + 1);
//     if (*pch != ')') { ParseExpression; n = 1; while (*(pch = SkipWhite) != ')') {
//         if (*pch != ',') { SyntaxError(L"Missing ,"); return pch; } ParseExpression(pch + 1); n++; } }
//     Write(opCall); Write((BYTE)n); return pch + 1;
// The callee reference is already on the stack; the count is a single byte.
const TCHAR* CFunctionCompiler::ParseCall(const TCHAR* pch)
{
	int cArgs = 0;
	pch = SkipWhite(pch + 1);
	if (*pch != ')')
	{
		for (;;)
		{
			pch = SkipWhite(ParseExpression(pch));
			cArgs++;
			if (*pch == ')')
				break;
			if (*pch != ',')
			{
				SyntaxError(_T("Missing ,"));
				return pch;
			}
			pch++;
		}
	}

	Write(opCall);
	Write((BYTE)cArgs);
	return pch + 1;
}

// CFunctionCompiler::ParseExp
// Reconstructed from 5960 retail XBE @ 0x00034d80 (the binary's
// ParseExpression(pch, prio)). Ghidra decompile:
//     if (prio == 0) return ParseTerm(pch);
//     pch = ParseExp(pch, prio - 1);
//     for (;;) { pch = SkipWhite(pch);
//         if (prio == 1) { if (*pch == '(') pch = ParseCall(pch); else if (*pch == '[') pch = ParseArray(pch); }
//         p = ParseOperator(pch, &op); if (!op || op->pri != prio) return pch;
//         pch = ParseExp(p, prio - 1); Write(op->op); }
// Left-associative precedence climbing; calls and indexing bind at level 1.
const TCHAR* CFunctionCompiler::ParseExp(const TCHAR* pch, int nPrio)
{
	if (nPrio == 0)
		return ParseTerm(pch);

	pch = ParseExp(pch, nPrio - 1);

	for (;;)
	{
		pch = SkipWhite(pch);

		if (nPrio == 1)
		{
			if (*pch == '(')
				pch = ParseCall(pch);
			else if (*pch == '[')
				pch = ParseArray(pch);
		}

		const DOPER* pOp;
		const TCHAR* pchRight = ParseOperator(pch, pOp);
		if (pOp == NULL || pOp->m_pri != nPrio)
			return pch;

		pch = ParseExp(pchRight, nPrio - 1);
		Write(pOp->m_op);
	}
}

const TCHAR* CFunctionCompiler::ParseExpression(const TCHAR* pch)
{
	return ParseExp(pch, nLowestPriority);
}

// CFunctionCompiler::ParseIF
// Reconstructed from 5960 retail XBE @ 0x00034ea0. Ghidra decompile:
//     '(' expr ')' or SyntaxError(L"expected '('" / L"expected ')'") and return;
//     Write(opCond); fix = m_nop; WriteInteger(0); pch = ParseStatement(pch);
//     pch = Token(pch, &tok, &cch);
//     if (cch == 4 && !wcsncmp(tok, L"else", 4)) { Write(opJump); fix2 = m_nop; WriteInteger(0);
//         patch(fix) = m_nop; pch = ParseStatement(pch); patch(fix2) = m_nop; return pch; }
//     patch(fix) = m_nop; return tok;
// With no else the cursor goes back to the start of the peeked token.
const TCHAR* CFunctionCompiler::ParseIF(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != '(')
	{
		SyntaxError(_T("expected '('"));
		return pch;
	}

	pch = SkipWhite(ParseExpression(pch + 1));
	if (*pch != ')')
	{
		SyntaxError(_T("expected ')'"));
		return pch;
	}

	Write(opCond);
	UINT nopSkipThen = GetAddress();
	WriteInteger(0);

	pch = ParseStatement(pch + 1);

	const TCHAR* pchTok;
	int cchTok;
	const TCHAR* pchAfter = Token(pch, pchTok, cchTok);
	if (!IsKeyword(pchTok, cchTok, _T("else"), 4))
	{
		Fixup(nopSkipThen, GetAddress());
		return pchTok;
	}

	Write(opJump);
	UINT nopSkipElse = GetAddress();
	WriteInteger(0);

	Fixup(nopSkipThen, GetAddress());
	pch = ParseStatement(pchAfter);
	Fixup(nopSkipElse, GetAddress());
	return pch;
}

// CFunctionCompiler::ParseWHILE
// Reconstructed from 5960 retail XBE @ 0x00034fe0. Behavior: save the break
// list and loop top, open a breakable scope, then '(' cond ')' with the loop
// top at the start of the condition. Emits opCond (forward fixup), the body,
// opJump back to the top, patches the exit and every break to here, and
// restores the outer loop state. A missing paren is a syntax error and "".
const TCHAR* CFunctionCompiler::ParseWHILE(const TCHAR* pch)
{
	BREAK* pOuterBreaks = m_breaks;
	m_breaks = NULL;
	m_nBreakables++;

	pch = SkipWhite(pch);
	if (*pch != '(')
	{
		SyntaxError(_T("expected '('"));
		return szGiveUp;
	}

	int nopOuterTop = m_nopTopOfLoop;
	m_nopTopOfLoop = GetAddress();

	pch = SkipWhite(ParseExpression(pch + 1));
	if (*pch != ')')
	{
		SyntaxError(_T("expected ')'"));
		return szGiveUp;
	}

	Write(opCond);
	UINT nopExit = GetAddress();
	WriteInteger(0);

	pch = ParseStatement(pch + 1);

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	Fixup(nopExit, GetAddress());
	FixupBreaks();

	m_breaks = pOuterBreaks;
	m_nBreakables--;
	m_nopTopOfLoop = nopOuterTop;
	return pch;
}

// CFunctionCompiler::ParseDO
// Reconstructed from 5960 retail XBE @ 0x00035170. Behavior: same loop
// bookkeeping as while, with the top at the body. Steps one character past
// the "do" keyword, compiles the body, then demands while '(' cond ')' ';'.
// Emits opCond to the exit, then an unconditional opJump back to the top
// (the condition is checked once per pass that way), patches exit and breaks.
const TCHAR* CFunctionCompiler::ParseDO(const TCHAR* pch)
{
	BREAK* pOuterBreaks = m_breaks;
	m_breaks = NULL;
	m_nBreakables++;

	int nopOuterTop = m_nopTopOfLoop;
	m_nopTopOfLoop = GetAddress();

	pch = ParseStatement(pch + 1);

	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);
	if (!IsKeyword(pchTok, cchTok, _T("while"), 5))
	{
		SyntaxError(_T("expected 'while'"));
		return szGiveUp;
	}

	pch = SkipWhite(pch);
	if (*pch != '(')
	{
		SyntaxError(_T("expected '('"));
		return szGiveUp;
	}

	pch = SkipWhite(ParseExpression(pch + 1));
	if (*pch != ')')
	{
		SyntaxError(_T("expected ')'"));
		return szGiveUp;
	}

	pch = SkipWhite(pch + 1);
	if (*pch != ';')
	{
		SyntaxError(_T("expected ';'"));
		return szGiveUp;
	}

	Write(opCond);
	UINT nopExit = GetAddress();
	WriteInteger(0);

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	Fixup(nopExit, GetAddress());
	FixupBreaks();

	m_breaks = pOuterBreaks;
	m_nBreakables--;
	m_nopTopOfLoop = nopOuterTop;
	return pch + 1;
}

// CFunctionCompiler::ParseFOR
// Reconstructed from 5960 retail XBE @ 0x000352f0. Behavior: '(' then the
// init clause: "var ..." (read from three characters past the paren, handled
// by ParseLocalVar which eats the ';') or an expression + opDrop + ';'. The
// loop top is the condition. The step clause is compiled once and thrown
// away just to find the ')', then compiled for real after the body, followed
// by opDrop and opJump back to the top. Exit and breaks are patched last.
const TCHAR* CFunctionCompiler::ParseFOR(const TCHAR* pch)
{
	BREAK* pOuterBreaks = m_breaks;
	m_breaks = NULL;
	m_nBreakables++;

	pch = SkipWhite(pch);
	if (*pch != '(')
	{
		SyntaxError(_T("expected '('"));
		return szGiveUp;
	}
	pch++;

	const TCHAR* pchTok;
	int cchTok;
	Token(pch, pchTok, cchTok);
	if (IsKeyword(pchTok, cchTok, _T("var"), 3))
	{
		pch = ParseLocalVar(pch + 3);
	}
	else
	{
		pch = ParseExpression(pch);
		Write(opDrop);
		pch = SkipWhite(pch);
		if (*pch != ';')
		{
			SyntaxError(_T("expected ';'"));
			return szGiveUp;
		}
		pch++;
	}

	int nopOuterTop = m_nopTopOfLoop;
	m_nopTopOfLoop = GetAddress();

	pch = SkipWhite(ParseExpression(pch));
	if (*pch != ';')
	{
		SyntaxError(_T("expected ';'"));
		return szGiveUp;
	}

	const TCHAR* pchStep = pch + 1;
	UINT nopStep = GetAddress();
	pch = ParseExpression(pchStep);
	Write(opDrop);
	m_nop = nopStep;

	pch = SkipWhite(pch);
	if (*pch != ')')
	{
		SyntaxError(_T("expected ')'"));
		return szGiveUp;
	}

	Write(opCond);
	UINT nopExit = GetAddress();
	WriteInteger(0);

	pch = ParseStatement(pch + 1);

	ParseExpression(pchStep);
	Write(opDrop);
	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	Fixup(nopExit, GetAddress());
	FixupBreaks();

	m_breaks = pOuterBreaks;
	m_nBreakables--;
	m_nopTopOfLoop = nopOuterTop;
	return pch;
}

// CFunctionCompiler::FixupBreaks
// Reconstructed from 5960 retail XBE @ 0x00035510. Ghidra decompile:
//     for (b = m_breaks; b; b = next) { next = b->next; *(int*)(m_ops + b->nop) = m_nop; delete b; }
void CFunctionCompiler::FixupBreaks()
{
	BREAK* pEntry = m_breaks;
	while (pEntry != NULL)
	{
		BREAK* pNext = pEntry->m_next;
		Fixup(pEntry->m_nop, GetAddress());
		delete pEntry;
		pEntry = pNext;
	}
}

// CFunctionCompiler::ParseBREAK
// Reconstructed from 5960 retail XBE @ 0x00035540. Ghidra decompile:
//     pch = SkipWhite(pch); if (*pch != ';') { SyntaxError(L"Missing ';'"); return L""; }
//     if (!m_nBreakables) { SyntaxError(L"unexpected 'break'"); return L""; }
//     Write(opJump); b = new BREAK; b->next = m_breaks; m_breaks = b; b->nop = m_nop;
//     WriteInteger(0); return pch + 1;
const TCHAR* CFunctionCompiler::ParseBREAK(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != ';')
	{
		SyntaxError(_T("Missing ';'"));
		return szGiveUp;
	}
	if (m_nBreakables == 0)
	{
		SyntaxError(_T("unexpected 'break'"));
		return szGiveUp;
	}

	Write(opJump);

	BREAK* pEntry = new BREAK;
	pEntry->m_nop = GetAddress();
	pEntry->m_next = m_breaks;
	m_breaks = pEntry;

	WriteInteger(0);
	return pch + 1;
}

// CFunctionCompiler::ParseCONTINUE
// Reconstructed from 5960 retail XBE @ 0x00035610. Same checks as break
// (L"unexpected 'continue'"), then: Write(opJump); WriteInteger(m_nopTopOfLoop).
const TCHAR* CFunctionCompiler::ParseCONTINUE(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != ';')
	{
		SyntaxError(_T("Missing ';'"));
		return szGiveUp;
	}
	if (m_nBreakables == 0)
	{
		SyntaxError(_T("unexpected 'continue'"));
		return szGiveUp;
	}

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);
	return pch + 1;
}

// CFunctionCompiler::ParseRETURN / ParseSLEEP
// Reconstructed from 5960 retail XBE @ 0x000356c0 / 0x00035760. Ghidra decompile:
//     pch = SkipWhite(pch);
//     if (*pch == ';') Write(opNull);
//     else { pch = SkipWhite(ParseExpression(pch)); if (*pch != ';') { SyntaxError(L"Missing ';'"); return L""; } }
//     Write(opRet or opSleep); return pch + 1;
// The two are the same shape and differ only in the closing opcode.
static const TCHAR* ParseValueThenOp(CFunctionCompiler* pCompiler, const TCHAR* pch, BYTE op)
{
	pch = SkipWhite(pch);
	if (*pch == ';')
	{
		pCompiler->Write(opNull);
	}
	else
	{
		pch = SkipWhite(pCompiler->ParseExpression(pch));
		if (*pch != ';')
		{
			pCompiler->SyntaxError(_T("Missing ';'"));
			return szGiveUp;
		}
	}

	pCompiler->Write(op);
	return pch + 1;
}

const TCHAR* CFunctionCompiler::ParseRETURN(const TCHAR* pch)
{
	return ParseValueThenOp(this, pch, opRet);
}

const TCHAR* CFunctionCompiler::ParseSLEEP(const TCHAR* pch)
{
	return ParseValueThenOp(this, pch, opSleep);
}

// CFunctionCompiler::ParseBlock
// Reconstructed from 5960 retail XBE @ 0x00035800. Ghidra decompile:
//     brace = (*pch == '{'); if (brace) pch++;
//     base = m_nFrameSize; Write(opFrame); fix = m_nop; WriteInteger(0);
//     while (*pch && !(brace && *pch == '}')) pch = ParseStatement(pch);
//     if (brace) { if (*pch != '}') { SyntaxError(L"expected '}'"); return pch; } pch++; }
//     patch(fix) = m_nFrameSize - base; Write(opEndFrame); WriteInteger(m_nFrameSize - base);
//     m_nFrameSize = base;
// Locals declared inside the block live exactly as long as its frame.
const TCHAR* CFunctionCompiler::ParseBlock(const TCHAR* pch)
{
	const bool bBraced = (*pch == '{');
	if (bBraced)
		pch++;

	const int nFrameBase = m_nFrameSize;

	Write(opFrame);
	UINT nopFrameSize = GetAddress();
	WriteInteger(0);

	while (*pch != '\0' && !(bBraced && *pch == '}'))
		pch = ParseStatement(pch);

	if (bBraced)
	{
		if (*pch != '}')
		{
			SyntaxError(_T("expected '}'"));
			return pch;
		}
		pch++;
	}

	const int cLocals = m_nFrameSize - nFrameBase;
	Fixup(nopFrameSize, cLocals);
	Write(opEndFrame);
	WriteInteger(cLocals);

	m_nFrameSize = nFrameBase;
	return pch;
}

// CFunctionCompiler::ParseStatement
// Reconstructed from 5960 retail XBE @ 0x00035960. Behavior: skip white; a
// '}' is left for the caller and a '{' opens a block. Otherwise read one
// token, emit opStatement with the current line, and dispatch on keyword
// (grouped by length in the binary): var, for, if, do, while, break,
// continue, return, and sleep inside behaviors only. Anything else is an
// expression statement compiled from the start of that token, followed by
// opDrop and a ';' (which may be missing at the very end of the text).
const TCHAR* CFunctionCompiler::ParseStatement(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch == '}')
		return pch;
	if (*pch == '{')
		return ParseBlock(pch);

	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);

	Write(opStatement);
	WriteInteger(g_nLine);

	switch (cchTok)
	{
	case 2:
		if (_tcsncmp(pchTok, _T("if"), 2) == 0)
			return ParseIF(pch);
		if (_tcsncmp(pchTok, _T("do"), 2) == 0)
			return ParseDO(pch);
		break;

	case 3:
		if (_tcsncmp(pchTok, _T("var"), 3) == 0)
			return ParseLocalVar(pch);
		if (_tcsncmp(pchTok, _T("for"), 3) == 0)
			return ParseFOR(pch);
		break;

	case 5:
		if (_tcsncmp(pchTok, _T("while"), 5) == 0)
			return ParseWHILE(pch);
		if (_tcsncmp(pchTok, _T("break"), 5) == 0)
			return ParseBREAK(pch);
		if (m_bBehavior && _tcsncmp(pchTok, _T("sleep"), 5) == 0)
			return ParseSLEEP(pch);
		break;

	case 6:
		if (_tcsncmp(pchTok, _T("return"), 6) == 0)
			return ParseRETURN(pch);
		break;

	case 8:
		if (_tcsncmp(pchTok, _T("continue"), 8) == 0)
			return ParseCONTINUE(pch);
		break;
	}

	pch = ParseExpression(pchTok);
	Write(opDrop);

	if (*pch == '\0')
		return pch;
	if (*pch != ';')
	{
		SyntaxError(_T("expected ';'"));
		return szGiveUp;
	}
	return pch + 1;
}

// CFunctionCompiler::ParseLocalVar
// Reconstructed from 5960 retail XBE @ 0x00035bf0. Behavior: for each name in
// a ','-separated list, record it in the next local slot; with "= expr" emit
// opLocal slot, the expression, opAssign, opDrop. Ends at ';' (consumed) or
// raises "Expected a ';'" and leaves the cursor where it stopped.
const TCHAR* CFunctionCompiler::ParseLocalVar(const TCHAR* pch)
{
	for (;;)
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName);

		// Theseus fix: 5960 runs past the 100 local slots without noticing.
		if (!CHECK(m_nFrameSize < (int)countof(m_rgstLocal)))
		{
			SyntaxError(_T("Too many local variables"));
			return szGiveUp;
		}

		const int iSlot = m_nFrameSize++;
		m_rgstLocal[iSlot].pchName = pchName;
		m_rgstLocal[iSlot].cchName = (short)cchName;

		pch = SkipWhite(pch);
		if (*pch == '=')
		{
			Write(opLocal);
			WriteInteger(iSlot);
			pch = ParseExpression(pch + 1);
			Write(opAssign);
			Write(opDrop);
			pch = SkipWhite(pch);
		}

		if (*pch != ',')
			break;
		pch++;
	}

	if (*pch != ';')
	{
		SyntaxError(_T("Expected a ';'"));
		return pch;
	}
	return pch + 1;
}


// CClassCompiler::CClassCompiler
// Inlined in 5960 (CClass::ParseClassBody @0x33d60): a zeroed compiler
// pointed at the class being built.
CClassCompiler::CClassCompiler(CClass* pNewClass)
{
	m_targetClass = pNewClass;
}

// CClassCompiler::Compile
// Reconstructed from 5960 retail XBE @ 0x00035e50. Ghidra decompile:
//     pch = SkipWhite(pch); while (*pch && *pch != '}') { ParseNode(pch); pch = SkipWhite(...); }
//     Write(0); return pch;
// The trailing 0 is opNull, the end marker of a class body.
const TCHAR* CClassCompiler::Compile(const TCHAR* pch)
{
	for (pch = SkipWhite(pch); *pch != '\0' && *pch != '}'; pch = SkipWhite(pch))
		pch = ParseNode(pch);

	Write((BYTE)0);
	return pch;
}

// Copies a name token into a terminated buffer for error text, cut to fit.
static void CopyNameForMessage(TCHAR* szName, int cchMax, const TCHAR* pchName, int cchName)
{
	if (cchName > cchMax - 1)
		cchName = cchMax - 1;
	CopyChars(szName, pchName, cchName);
	szName[cchName] = '\0';
}

// Reports an unknown name by briefly terminating the token in place, the way
// 5960 does it for "Unknown class" and "Unknown property".
static void ReportUnknown(CCompiler* pCompiler, const TCHAR* szFormat, const TCHAR* pchName, int cchName)
{
	TCHAR* pchEnd = (TCHAR*)pchName + cchName;
	TCHAR chSaved = *pchEnd;
	*pchEnd = '\0';
	pCompiler->SyntaxError(szFormat, pchName);
	*pchEnd = chSaved;
}

// CClassCompiler::ParseNode
// Reconstructed from 5960 retail XBE @ 0x00035ec0. Behavior, by first token:
//   DEF name node   new CMember in the next variable slot, opDefNode slot,
//                   then the node itself.
//   USE name        opUseNode with the slot of an existing member, or
//                   "Undefined USE".
//   behavior {...}  compiled as a behavior and set as a class member.
//   function n(..)  compiled and set as member n ("behavior" is refused).
//   var a, b;       class member variables.
//   class N {...}   nested class, defined in the class namespace.
//   import N, url   loads another class file under name N.
//   otherwise       a node: script class or built-in node class by name,
//                   opNewNode name, optional { props }, opEndNode.
// One trailing ',' after the item is eaten. See docs/decomp/Compiler.md.
// Theseus spells "Expected a '}'" right; 5960 has a typo there.
const TCHAR* CClassCompiler::ParseNode(const TCHAR* pch)
{
	const TCHAR* pchTok;
	int cchTok;
	pch = Token(pch, pchTok, cchTok);

	if (IsKeyword(pchTok, cchTok, _T("DEF"), 3))
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName, true);

		CMember* pVar = new CMember;
		pVar->m_memberIndex = m_targetClass->m_nVarCount++;
		m_targetClass->AddMember(pchName, cchName, pVar);

		Write(opDefNode);
		WriteInteger(pVar->m_memberIndex);

		pch = ParseNode(pch);
	}
	else if (IsKeyword(pchTok, cchTok, _T("USE"), 3))
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName, true);

		CMember* pVar = (CMember*)m_targetClass->GetMember(pchName, cchName);
		if (pVar == NULL || pVar->m_obj != objMember)
		{
			TCHAR szName[256];
			CopyNameForMessage(szName, countof(szName), pchName, cchName);
			SyntaxError(_T("Undefined USE '%s'"), szName);
			return szGiveUp;
		}

		Write(opUseNode);
		WriteInteger(pVar->m_memberIndex);
	}
	else if (IsKeyword(pchTok, cchTok, _T("behavior"), 8))
	{
		CFunction* pBehavior;
		pch = ParseFunction(pch, pBehavior, true);
		if (pBehavior != NULL)
			m_targetClass->SetMember(pchTok, cchTok, pBehavior);
	}
	else if (IsKeyword(pchTok, cchTok, _T("function"), 8))
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName);
		if (IsKeyword(pchName, cchName, _T("behavior"), 8))
		{
			SyntaxError(_T("invalid function name"));
			return szGiveUp;
		}

		CFunction* pFn;
		pch = ParseFunction(pch, pFn);
		if (pFn != NULL)
			m_targetClass->SetMember(pchName, cchName, pFn);
	}
	else if (IsKeyword(pchTok, cchTok, _T("var"), 3))
	{
		pch = ParseMemberVar(pch);
	}
	else if (IsKeyword(pchTok, cchTok, _T("class"), 5))
	{
		pch = ParseClass(pch);
	}
	else if (IsKeyword(pchTok, cchTok, _T("import"), 6))
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName);

		pch = SkipWhite(pch);
		if (*pch != ',')
		{
			SyntaxError(_T("Expected ','"));
			return szGiveUp;
		}

		const TCHAR* pchPath;
		int cchPath;
		pch = Token(pch + 1, pchPath, cchPath);

		// Theseus fix: 5960 copies the url into its buffer without a length check.
		TCHAR szPath[MAX_PATH];
		if (!CHECK(cchPath < MAX_PATH))
		{
			SyntaxError(_T("Import path is too long"));
			return szGiveUp;
		}
		CopyChars(szPath, pchPath, cchPath);
		szPath[cchPath] = '\0';

		CClass* pImported = new CClass;
		if (!pImported->Load(szPath))
		{
			delete pImported;
			return szGiveUp;
		}

		g_classes->Define(pchName, cchName, (CNode*)pImported);
	}
	else
	{
		CNodeClass* pNodeType = LookupClass(pchTok, cchTok);
		if (pNodeType == NULL)
			pNodeType = CNodeClass::FindByName(pchTok, cchTok);
		if (pNodeType == NULL)
		{
			ReportUnknown(this, _T("Unknown class: %s"), pchTok, cchTok);
			return szGiveUp;
		}

		Write(opNewNode);
		WriteString(pchTok, cchTok);

		pch = SkipWhite(pch);
		if (*pch == '{')
		{
			pch = ParseProps(pch + 1, pNodeType);
			if (*pch != '}')
			{
				SyntaxError(_T("Expected a '}'"));
				return szGiveUp;
			}
			pch++;
		}

		Write(opEndNode);
	}

	pch = SkipWhite(pch);
	if (*pch == ',')
		pch++;
	return pch;
}

// CClassCompiler::ParseChildren
// Reconstructed from 5960 retail XBE @ 0x000363c0. Ghidra decompile:
//     pch = SkipWhite(pch); if (*pch != '[') return ParseNode(pch);
//     pch = SkipWhite(pch + 1); while (*pch && *pch != '}' && *pch != ']') { ParseNode; SkipWhite; }
//     if (*pch != ']') { SyntaxError(...); return pch; } return pch + 1;
// A children value is either one node or a bracketed list of them.
const TCHAR* CClassCompiler::ParseChildren(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != '[')
		return ParseNode(pch);

	for (pch = SkipWhite(pch + 1); *pch != '\0' && *pch != '}' && *pch != ']'; pch = SkipWhite(pch))
		pch = ParseNode(pch);

	if (*pch != ']')
	{
		SyntaxError(_T("Expected a ']'"));
		return pch;
	}
	return pch + 1;
}

// CClassCompiler::ParseProps
// Reconstructed from 5960 retail XBE @ 0x00036440. Behavior: until '}' or the
// end of text, read a name. "behavior" and "function name" compile an inline
// function, append it to the class's instance functions and emit opFunction
// with the name and its index. Any other name must be a property of the node
// class or it's "Unknown property". children / node arrays emit opInitArray
// with the property offset, the child list, opEndArray. A node property emits
// opNewNodeProp, the PRD, and the node. Everything else is read by type
// (bool, int, float, string, vec3/color, vec4/quaternion, int and vector
// arrays) and emitted as opInitProp, the PRD, then the value: strings as
// length + characters, the rest as byte count + bytes.
// See docs/decomp/Compiler.md for the type to reader table.
const TCHAR* CClassCompiler::ParseProps(const TCHAR* pch, CNodeClass* pNodeType)
{
	for (pch = SkipWhite(pch); *pch != '\0' && *pch != '}'; pch = SkipWhite(pch))
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName);

		const bool bBehavior = IsKeyword(pchName, cchName, _T("behavior"), 8);
		if (bBehavior || IsKeyword(pchName, cchName, _T("function"), 8))
		{
			if (!bBehavior)
			{
				pch = Token(pch, pchName, cchName);
				if (IsKeyword(pchName, cchName, _T("behavior"), 8))
				{
					SyntaxError(_T("invalid function name"));
					return szGiveUp;
				}
			}

			CFunction* pFn;
			pch = ParseFunction(pch, pFn, bBehavior);
			if (pFn != NULL)
			{
				int iFunction = m_targetClass->m_instanceFunctions.GetLength();
				m_targetClass->m_instanceFunctions.AddNode((CNode*)pFn);
				Write(opFunction);
				WriteString(pchName, cchName);
				WriteInteger(iFunction);
			}
			continue;
		}

		const PRD* pprd = pNodeType->FindProp(pchName, cchName);
		if (pprd == NULL)
		{
			ReportUnknown(this, _T("Unknown property: %s"), pchName, cchName);
			return szGiveUp;
		}

		switch (pprd->nType)
		{
		case pt_children:
		case pt_nodearray:
			Write(opInitArray);
			WriteInteger(PTR2INT(pprd->pbOffset));
			pch = ParseChildren(pch);
			Write(opEndArray);
			continue;

		case pt_node:
			Write(opNewNodeProp);
			Write(pprd, sizeof(PRD));
			pch = ParseNode(pch);
			continue;

		default:
			break;
		}

		union
		{
			bool b;
			int i;
			float n;
			float v[4];
			TCHAR* sz;
		} value;
		BYTE* pbItems = NULL;
		int cbData = 0;

		switch (pprd->nType)
		{
		case pt_integer:
			pch = ParseInteger(pch, value.i);
			cbData = sizeof(int);
			break;

		case pt_boolean:
			pch = ParseBoolean(pch, value.b);
			cbData = sizeof(bool);
			break;

		case pt_number:
			pch = ParseNumber(pch, value.n);
			cbData = sizeof(float);
			break;

		case pt_vec3:
		case pt_color:
			pch = ParseVec3(pch, value.v);
			cbData = 3 * sizeof(float);
			break;

		case pt_vec4:
		case pt_quaternion:
			pch = ParseVec4(pch, value.v);
			cbData = 4 * sizeof(float);
			break;

		case pt_string:
			pch = ParseString(pch, value.sz);
			cbData = _tcslen(value.sz);
			break;

		case pt_intarray:
			pch = ParseIntArray(pch, pbItems, cbData);
			break;

		case pt_numarray:
			pch = ParseVecArray(pch, pbItems, cbData, 1);
			break;

		case pt_vec2array:
			pch = ParseVecArray(pch, pbItems, cbData, 2);
			break;

		case pt_vec3array:
			pch = ParseVecArray(pch, pbItems, cbData, 3);
			break;

		case pt_vec4array:
			pch = ParseVecArray(pch, pbItems, cbData, 4);
			break;

		default:
			break;
		}

		Write(opInitProp);
		Write(pprd, sizeof(PRD));

		if (pprd->nType == pt_string)
		{
			WriteString(value.sz, cbData);
			delete[] value.sz;
		}
		else
		{
			WriteInteger(cbData);
			if (pbItems != NULL)
			{
				Write(pbItems, cbData);
				delete[] pbItems;
			}
			else
			{
				Write(&value, cbData);
			}
		}
	}

	return pch;
}

// CClassCompiler::ParseMemberVar
// Reconstructed from 5960 retail XBE @ 0x00036ad0. Behavior: for each name in
// a ','-separated list, add a CMember in the next variable slot. An '='
// initializer is refused ("not implemented") and stops the parse. Ends at ';'
// or raises "Expected a ';'" leaving the cursor where it stopped.
const TCHAR* CClassCompiler::ParseMemberVar(const TCHAR* pch)
{
	for (;;)
	{
		const TCHAR* pchName;
		int cchName;
		pch = Token(pch, pchName, cchName);

		CMember* pVar = new CMember;
		pVar->m_memberIndex = m_targetClass->m_nVarCount++;
		m_targetClass->AddMember(pchName, cchName, pVar);

		pch = SkipWhite(pch);
		if (*pch == '=')
		{
			SyntaxError(_T("Sorry, class member initializers have not been implemented yet!"));
			return szGiveUp;
		}

		if (*pch != ',')
			break;
		pch++;
	}

	if (*pch != ';')
	{
		SyntaxError(_T("Expected a ';'"));
		return pch;
	}
	return pch + 1;
}

// CClassCompiler::ParseFunction
// Reconstructed from 5960 retail XBE @ 0x00036c30. Behavior: set up a fresh
// CFunctionCompiler (behavior flag from the caller). A function needs a
// parameter list: '(' names split by ',' ')', each name becoming the next
// local; no '(' just returns with no function, a bad list is "expected ')'".
// Behaviors take no list. The body must start with '{' or there's no
// function. The block is compiled, opNull opRet is appended as the implicit
// "return null;" and the ops become the CFunction.
const TCHAR* CClassCompiler::ParseFunction(const TCHAR* pch, CFunction*& pFn, bool bBehavior)
{
	CFunctionCompiler body;
	body.m_bBehavior = bBehavior;

	pFn = NULL;
	pch = SkipWhite(pch);

	if (!bBehavior)
	{
		if (*pch != '(')
			return pch;

		for (pch = SkipWhite(pch + 1); *pch != '\0' && *pch != ')'; pch = SkipWhite(pch + 1))
		{
			const TCHAR* pchName;
			int cchName;
			pch = Token(pch, pchName, cchName);

			// Theseus fix: 5960 runs past the 100 local slots without noticing.
			if (!CHECK(body.m_nFrameSize < (int)countof(body.m_rgstLocal)))
			{
				SyntaxError(_T("Too many parameters"));
				return szGiveUp;
			}

			ST& local = body.m_rgstLocal[body.m_nFrameSize++];
			local.pchName = pchName;
			local.cchName = (short)cchName;

			pch = SkipWhite(pch);
			if (*pch != ',')
				break;
		}

		if (*pch != ')')
		{
			SyntaxError(_T("expected ')'\n"));
			return szGiveUp;
		}
		pch++;
	}

	pch = SkipWhite(pch);
	if (*pch != '{')
		return pch;

	pch = body.ParseBlock(pch);
	body.Write(opNull);
	body.Write(opRet);

	pFn = body.CreateFunction();
	return pch;
}

// CClassCompiler::ParseClass
// Reconstructed from 5960 retail XBE @ 0x00036e50. Ghidra decompile:
//     Token(pch, &name, &cch); pch = SkipWhite(...); if (*pch != '{') { SyntaxError; return L""; }
//     c = new CClass; pch = c->ParseClassBody(pch + 1);
//     if (*pch != '}') { SyntaxError; return L""; } g_classes->Define(name, cch, c); return pch + 1;
const TCHAR* CClassCompiler::ParseClass(const TCHAR* pch)
{
	const TCHAR* pchName;
	int cchName;
	pch = Token(pch, pchName, cchName);

	pch = SkipWhite(pch);
	if (*pch != '{')
	{
		SyntaxError(_T("Expected '{'\n"));
		return szGiveUp;
	}

	CClass* pNewClass = new CClass;
	pch = pNewClass->ParseClassBody(pch + 1);
	if (*pch != '}')
	{
		SyntaxError(_T("Expected '}'"));
		return szGiveUp;
	}

	g_classes->Define(pchName, cchName, (CNode*)pNewClass);
	return pch + 1;
}


// Class_Init / Class_Exit
// Reconstructed from 5960 retail XBE @ 0x00036f00 / 0x00036f30. Ghidra decompile:
//     g_classes = new CNameSpace;        // one word: the list head, zeroed
//     delete g_classes;                  // walks the list releasing each class
void Class_Init()
{
	g_classes = new CNameSpace;
}

void Class_Exit()
{
	delete g_classes;
}
