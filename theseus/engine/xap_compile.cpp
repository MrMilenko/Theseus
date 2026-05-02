// xap_compile.cpp: XAP script compilation pipeline.
//
// Tokenizer, scene graph parser, and bytecode compiler combined into a
// single translation unit. Pipeline shape:
//   XAP text -> tokenizer -> scene parser -> bytecode compiler -> CFunction
// Decompiled from the 5960 retail XBE; see docs/decomp/VM.md for the
// binary-analysis notes on opcode set, operator table, and dispatch.
#include "std.h"
#include "theseus.h"
#include "xap_compile.h"
#include "node.h"
#include "runner.h"


// =========================================================================
// Global parse state
// =========================================================================

int g_nLine;
const TCHAR* g_szFileName;
static TCHAR g_szFileNameBuf[MAX_PATH];
bool g_bParseError;

CNameSpace* g_classes;

static const TCHAR szParseError[] = _T("");

#define IsAlNum(ch) (((ch) >= 'a' && (ch) <= 'z') || ((ch) >= 'A' && (ch) <= 'Z') || ((ch) >= '0' && (ch) <= '9') || (ch) == '.' || (ch) == '-' || (ch) == '_')

// =========================================================================
// Section 1: Tokenizer -- character-level scanning
// =========================================================================

void StartParse(const TCHAR* pch, const TCHAR* szFileName, int nLine)
{
	g_szFileName = szFileName;
	g_nLine = nLine;
	g_bParseError = false;
}

void EndParse()
{
	g_szFileName = NULL;
	g_nLine = 0;
}

void SyntaxError(const TCHAR* szMsg, ...)
{
	va_list args;
	va_start(args, szMsg);

	TCHAR szBuffer[512];
	_vsntprintf(szBuffer, countof(szBuffer), szMsg, args);

	TCHAR szMessage[1024];
	_stprintf(szMessage, _T("Syntax Error\n\nFile: %s\nLine: %d\n\n%s"), g_szFileName, g_nLine, szBuffer);

	Trace(_T("\007%s\n"), szMessage);

	va_end(args);
	g_bParseError = true;
}

// Skip whitespace and comments (supports #, //, /* */ styles)
const TCHAR* SkipWhite(const TCHAR* pch, COMMENT_TYPE ct)
{
	for (;;)
	{
		while (*pch != '\0' && *pch <= ' ')
		{
			if (*pch == '\n')
				g_nLine++;
			pch++;
		}

		if (((ct & pound) && *pch == '#') || ((ct & slashslash) && *pch == '/' && *(pch + 1) == '/'))
		{
			// Handle #line and #file directives
			if (*pch == '#')
			{
				pch++;
				while (*pch == ' ' || *pch == '\t')
					pch++;

				if (_tcsncmp(pch, _T("line"), 4) == 0)
				{
					pch += 4;
					while (*pch == ' ' || *pch == '\t')
						pch++;
					g_nLine = _ttoi(pch) - 1;
				}
				else if (_tcsncmp(pch, _T("file"), 4) == 0)
				{
					pch += 4;
					while (*pch == ' ' || *pch == '\t')
						pch++;

					TCHAR* pDest = g_szFileNameBuf;
					if (*pch == '"')
						pch++;
					while (*pch != '\0' && *pch != '\r' && *pch != '\n' && *pch != '"')
						*pDest++ = *pch++;
					*pDest = 0;
					g_szFileName = g_szFileNameBuf;
				}
			}

			// Skip to end of line
			while (*pch != '\0' && *pch != '\r' && *pch != '\n')
				pch++;
		}
		else if ((ct & slashstar) && *pch == '/' && *(pch + 1) == '*')
		{
			pch += 2;
			while (*pch != '\0' && !(*pch == '*' && *(pch + 1) == '/'))
			{
				if (*pch == '\n')
					g_nLine++;
				pch++;
			}
			if (*pch != '\0')
				pch += 2;
		}
		else
		{
			return pch;
		}
	}
}

// Extract the next token -- handles quoted strings, identifiers, numbers, operators
const TCHAR* Token(const TCHAR* pch, const TCHAR*& pchToken, int& cchToken, bool bAllowPaths, COMMENT_TYPE ct)
{
	pch = SkipWhite(pch, ct);

	BOOL bQuoted = FALSE;
	TCHAR chQuote = 0;
	if (*pch == '"' || *pch == '\'')
	{
		bQuoted = TRUE;
		chQuote = *pch;
		pch++;
	}

	pchToken = pch;

	if (bQuoted)
	{
		while (*pch != '\0' && *pch != chQuote)
		{
			if (*pch == '\\' && *(pch + 1) != '\0')
				pch++;
			pch++;
		}
	}
	else if (bAllowPaths)
	{
		while (*pch != '\0' && *pch > ' ')
			pch++;
	}
	else if ((*pch >= 'a' && *pch <= 'z' || *pch >= 'A' && *pch <= 'Z' || *pch == '_'))
	{
		while (*pch >= 'a' && *pch <= 'z' || *pch >= 'A' && *pch <= 'Z' || *pch >= '0' && *pch <= '9' || *pch == '_')
			pch++;
	}
	else
	{
		BOOL bAlNum = IsAlNum(*pch);
		if (bAlNum)
		{
			while (IsAlNum(*pch))
				pch++;
		}
		else
		{
			while (*pch > ' ' && !IsAlNum(*pch))
				pch++;
		}
	}

	cchToken = (int)(pch - pchToken);

	if (bQuoted && *pch == chQuote)
		pch++;

	return pch;
}

// Expand C-style escape sequences in a string literal
int ExpandCString(TCHAR* szString, int cchMaxString, const TCHAR* pchToken, int cchToken)
{
	TCHAR* pchOut = szString;
	while (cchToken > 0)
	{
		if (pchOut >= szString + cchMaxString)
		{
			SyntaxError(_T("string constant is too long"));
			return -1;
		}

		if (*pchToken == '\\')
		{
			pchToken++;
			cchToken--;
			if (cchToken == 0)
			{
				SyntaxError(_T("backslash at end of string"));
				return -1;
			}

			TCHAR ch = *pchToken++;
			cchToken--;
			switch (ch)
			{
			case 'b': *pchOut++ = '\b'; break;
			case 'f': *pchOut++ = '\f'; break;
			case 'n': *pchOut++ = '\n'; break;
			case 'r': *pchOut++ = '\r'; break;
			case 't': *pchOut++ = '\t'; break;

			case 'x':
				{
					if (cchToken < 2)
					{
						SyntaxError(_T("Invalid hex character escape sequence"));
						return -1;
					}
					ch = 0;
					int i;
					for (i = 0; i < 2; i++)
					{
						TCHAR chHex = *pchToken++;
						cchToken--;
						if (chHex >= '0' && chHex <= '9')      ch = 16 * ch + chHex - '0';
						else if (chHex >= 'A' && chHex <= 'F')  ch = 16 * ch + chHex - 'A' + 10;
						else if (chHex >= 'a' && chHex <= 'f')  ch = 16 * ch + chHex - 'a' + 10;
						else { SyntaxError(_T("Invalid hex character")); return -1; }
					}
					*pchOut++ = ch;
				}
				break;

#ifdef _UNICODE
			case 'u':
				{
					if (cchToken < 4)
					{
						SyntaxError(_T("Invalid Unicode character escape sequence"));
						return -1;
					}
					ch = 0;
					int i;
					for (i = 0; i < 4; i++)
					{
						TCHAR chHex = *pchToken++;
						cchToken--;
						if (chHex >= '0' && chHex <= '9')      ch = 16 * ch + chHex - '0';
						else if (chHex >= 'A' && chHex <= 'F')  ch = 16 * ch + chHex - 'A' + 10;
						else if (chHex >= 'a' && chHex <= 'f')  ch = 16 * ch + chHex - 'a' + 10;
						else { SyntaxError(_T("Invalid Unicode character")); return -1; }
					}
					*pchOut++ = ch;
				}
				break;
#endif

			default:
				if (ch >= '0' && ch <= '7')
				{
					if (ch == '0' && cchToken > 0 && (*pchToken < '0' || *pchToken > '7'))
					{
						*pchOut++ = '\0';
					}
					else
					{
						ch -= '0';
						while (cchToken != 0 && *pchToken >= '0' && *pchToken <= '7')
						{
							ch = ch * 8 + *pchToken - '0';
							pchToken++;
							cchToken--;
						}
						*pchOut++ = ch;
					}
				}
				else
				{
					*pchOut++ = ch;
				}
				break;
			}
		}
		else
		{
			*pchOut++ = *pchToken++;
			cchToken--;
		}
	}

	return (int)(pchOut - szString);
}

// =========================================================================
// Value parsers -- extract typed values from token stream
// =========================================================================

const TCHAR* ParseBoolean(const TCHAR* pch, bool& b)
{
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	if (_tcsnicmp(pchToken, _T("true"), cchToken) == 0)
		b = true;
	else if (_tcsnicmp(pchToken, _T("false"), cchToken) == 0)
		b = false;
	else
		SyntaxError(_T("Invalid boolean value"));

	return pch;
}

const TCHAR* ParseInteger(const TCHAR* pch, int& i)
{
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	TCHAR szBuf[64];
	ASSERT(cchToken < countof(szBuf));
	_tcsncpy(szBuf, pchToken, cchToken);
	szBuf[cchToken] = 0;
	i = _ttoi(szBuf);

	return pch;
}

const TCHAR* ParseNumber(const TCHAR* pch, float& n)
{
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	TCHAR szBuf[64];
	ASSERT(cchToken < countof(szBuf));
	_tcsncpy(szBuf, pchToken, cchToken);
	szBuf[cchToken] = 0;
	n = (float)_tcstod(szBuf, NULL);

	return pch;
}

const TCHAR* ParseString(const TCHAR* pch, TCHAR*& sz)
{
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	sz = new TCHAR[cchToken + 1];
	CopyChars(sz, pchToken, cchToken);
	sz[cchToken] = '\0';

	return pch;
}

const TCHAR* ParseVec3(const TCHAR* pch, float v[3])
{
	pch = ParseNumber(pch, v[0]);
	pch = ParseNumber(pch, v[1]);
	pch = ParseNumber(pch, v[2]);
	return pch;
}

const TCHAR* ParseVec4(const TCHAR* pch, float v[4])
{
	pch = ParseNumber(pch, v[0]);
	pch = ParseNumber(pch, v[1]);
	pch = ParseNumber(pch, v[2]);
	pch = ParseNumber(pch, v[3]);
	return pch;
}

const TCHAR* ParseIntArray(const TCHAR* pch, BYTE*& pbArray, int& cbArray)
{
	pch = SkipWhite(pch);
	if (*pch != '[')
	{
		SyntaxError(_T("Expected '[' to start array"));
		return _T("");
	}
	pch++;

	const TCHAR* pchStart = pch;
	int nLineStart = g_nLine;
	int nValues = 0;

	// First pass: count values
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		int n;
		pch = ParseInteger(pch, n);
		nValues++;
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		if (*pch == ',') pch++;
	}

	// Second pass: read values
	cbArray = nValues * sizeof(int);
	pbArray = new BYTE[cbArray];
	BYTE* pb = pbArray;

	pch = pchStart;
	g_nLine = nLineStart;
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		int n;
		pch = ParseInteger(pch, n);
		*(int*)pb = n;
		pb += sizeof(int);
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		if (*pch == ',') pch++;
	}

	return pch + 1;
}

const TCHAR* ParseVecArray(const TCHAR* pch, BYTE*& pbArray, int& cbArray, int nVecSize)
{
	ASSERT(nVecSize > 0 && nVecSize <= 4);

	pch = SkipWhite(pch);
	if (*pch != '[')
	{
		SyntaxError(_T("Expected '[' to start array"));
		return _T("");
	}
	pch++;

	const TCHAR* pchStart = pch;
	int nLineStart = g_nLine;
	int nValues = 0;

	// First pass: count
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		int i;
		for (i = 0; i < nVecSize; i++)
		{
			float n;
			pch = ParseNumber(pch, n);
			nValues++;
		}
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		if (*pch == ',') pch++;
	}

	// Second pass: read
	cbArray = nValues * sizeof(float);
	pbArray = new BYTE[cbArray];
	BYTE* pb = pbArray;

	pch = pchStart;
	g_nLine = nLineStart;
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		int i;
		for (i = 0; i < nVecSize; i++)
		{
			float n;
			pch = ParseNumber(pch, n);
			*(float*)pb = n;
			pb += sizeof(float);
		}
		pch = SkipWhite(pch);
		if (*pch == ']') break;
		if (*pch == ',') pch++;
	}

	return pch + 1;
}

// =========================================================================
// CParser -- tokenizer wrapper with file context
// =========================================================================

CParser::CParser(const TCHAR* szFilePath, const TCHAR* pchFile, int cchFile)
{
	m_pchFile = pchFile;
	m_cchFile = (cchFile == -1) ? _tcslen(pchFile) : cchFile;
	m_filePath = szFilePath;
	m_pch = m_pchFile;
	m_nLine = 1;
}

CParser::~CParser()
{
}

void CParser::SkipWhite()
{
	m_pch = ::SkipWhite(m_pch);
}

bool CParser::Token(const TCHAR*& pchToken, int& cchToken, bool bAllowPaths)
{
	m_pch = ::Token(m_pch, pchToken, cchToken, bAllowPaths);
	return cchToken != 0;
}

void CParser::SyntaxError(const TCHAR* szMsg, ...)
{
	va_list args;
	va_start(args, szMsg);

	TCHAR szBuffer[512];
	_vsntprintf(szBuffer, countof(szBuffer), szMsg, args);

	TCHAR szMessage[1024];
	_stprintf(szMessage, _T("Syntax Error\n\nFile: %s\nLine: %d\n\n%s"), m_filePath, m_nLine, szBuffer);

	Trace(_T("\007%s\n"), szMessage);

	va_end(args);
}

// =========================================================================
// Section 2: Bytecode emitter -- CCompiler base class
// =========================================================================

CCompiler::CCompiler()
{
	m_nop = 0;
	m_opsSize = 0;
	m_ops = NULL;
	m_bError = false;
}

CCompiler::~CCompiler()
{
	delete[] m_ops;
}

void CCompiler::GrowTo(int nNewSize)
{
	int cb = ((nNewSize + 4095) & ~0xfff);
	BYTE* ops = new BYTE[cb];
	CopyMemory(ops, m_ops, m_nop);
	delete[] m_ops;
	m_ops = ops;
	m_opsSize = cb;
}

void CCompiler::SyntaxError(const TCHAR* szMsg, ...)
{
	va_list args;
	va_start(args, szMsg);

	TCHAR szBuffer[512];
	_vsntprintf(szBuffer, countof(szBuffer), szMsg, args);

	TCHAR szMessage[1024];
	_stprintf(szMessage, _T("Syntax Error\n\nFile: %s\nLine: %d\n\n%s"), g_szFileName, g_nLine, szBuffer);

	Trace(_T("\007%s\n"), szMessage);

	va_end(args);
	m_bError = true;
	g_bParseError = true;
}

CFunction* CCompiler::CreateFunction()
{
#pragma push_macro("new")
#undef new
	CFunction* pFunction = new(m_nop) CFunction;
#pragma pop_macro("new")
	pFunction->m_codeSize = m_nop;
	CopyMemory(pFunction->m_rgop, m_ops, m_nop);
	return pFunction;
}

// =========================================================================
// Section 3: Expression and statement compiler -- CFunctionCompiler
// Compiles JS-like expressions and statements into bytecode opcodes
// =========================================================================

CFunctionCompiler::CFunctionCompiler()
{
	m_nFrameSize = 0;
	m_breaks = NULL;
	m_nBreakables = 0;
	m_nopTopOfLoop = 0;
	m_bBehavior = false;
}

// Operator precedence table -- maps character sequences to opcodes
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

#define LAST_PRI 14

const TCHAR* CFunctionCompiler::ParseOperator(const TCHAR* pch, const DOPER*& pdoper)
{
	pdoper = NULL;
	pch = SkipWhite(pch);
	if (*pch == '\0')
		return pch;

	for (int i = 0; i < sizeof(rgdoper) / sizeof(DOPER); i++)
	{
		if (rgdoper[i].m_ch1 == *pch && (rgdoper[i].m_ch2 == 0 || rgdoper[i].m_ch2 == *(pch + 1)))
		{
			pch++;
			if (rgdoper[i].m_ch2 != 0)
				pch++;
			pdoper = &rgdoper[i];
			break;
		}
	}

	return pch;
}

// Parse a primary expression (literal, variable, 'new', parenthesized expr)
const TCHAR* CFunctionCompiler::ParseTerm(const TCHAR* pch)
{
	bool bNeg = false;
	bool bInvert = false;

	// Handle unary +, -, ~
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == '-')       bNeg = !bNeg;
		else if (*pch == '+')  ;
		else if (*pch == '~')  bInvert = !bInvert;
		else break;
		pch++;
	}

	if (bInvert)
		SyntaxError(_T("Sorry, the ~ operator has not been implemented yet!"));

	if (*pch == '(')
	{
		// Parenthesized sub-expression
		pch++;
		pch = ParseExpression(pch);
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
		bool bQuoted = *pch == '"' || *pch == '\'';
		const TCHAR* pchToken;
		int cchToken;
		pch = Token(pch, pchToken, cchToken);

		if (cchToken == 0 && !bQuoted)
			return pch;

		if (bQuoted)
		{
			// String literal
			Write(opStr);
			TCHAR rgchString[1024];
			int nLen = ExpandCString(rgchString, countof(rgchString), pchToken, cchToken);
			if (nLen < 0)
				return _T("");
			WriteString(rgchString, nLen);
		}
		else if (*pchToken == '.' || *pchToken == '-' || *pchToken >= '0' && *pchToken <= '9')
		{
			// Numeric literal
			TCHAR szBuf[256]; // copy the token because _tcstod stupidly copies the whole string
			ASSERT(cchToken < countof(szBuf));
			CopyChars(szBuf, pchToken, cchToken);
			szBuf[cchToken] = 0;
			float n = (float)_tcstod(szBuf, NULL);
			if (bNeg) { bNeg = false; n = -n; }
			Write(opNum);
			WriteNumber(n);
		}
		else if (cchToken == 4 && _tcsncmp(pchToken, _T("true"), 4) == 0)
		{
			Write(opNum);
			WriteNumber(1.0f);
		}
		else if (cchToken == 5 && _tcsncmp(pchToken, _T("false"), 5) == 0)
		{
			Write(opNum);
			WriteNumber(0.0f);
		}
		else if (cchToken == 4 && _tcsncmp(pchToken, _T("this"), 4) == 0)
		{
			Write(opThis);
		}
		else if (cchToken == 4 && _tcsncmp(pchToken, _T("null"), 4) == 0)
		{
			Write(opNull);
		}
		else if (cchToken == 3 && _tcsncmp(pchToken, _T("new"), 3) == 0)
		{
			// Object construction: new ClassName(args...)
			pch = Token(pch, pchToken, cchToken);

			int nParam = 0;
			pch = SkipWhite(pch);
			if (*pch == '(')
			{
				pch++;
				if (*pch == ')')
				{
					pch++;
				}
				else
				{
					for (;;)
					{
						pch = ParseExpression(pch);
						nParam++;
						pch = SkipWhite(pch);
						if (*pch == ')') { pch++; break; }
						if (*pch != ',') { SyntaxError(_T("Missing ','")); return pch; }
						pch++;
					}
				}
			}

			Write(opNew);
			WriteInteger(nParam);
			WriteString(pchToken, cchToken);
		}
		else
		{
			// Symbol -- check local variables first, then late-bind
			int i;
			for (i = 0; i < m_nFrameSize; i++)
			{
				if (cchToken == m_rgstLocal[i].cchName && _tcsncmp(pchToken, m_rgstLocal[i].pchName, cchToken) == 0)
				{
					Write(opLocal);
					WriteInteger(i);
					break;
				}
			}

			if (i == m_nFrameSize)
			{
				// Late-bound variable reference
				Write(opVar);
				WriteString(pchToken, cchToken);
			}
		}
	}

	if (bNeg)
		Write(opNeg);

	return pch;
}

const TCHAR* CFunctionCompiler::ParseArray(const TCHAR* pch)
{
	ASSERT(*pch == '[');
	pch++;

	pch = ParseExpression(pch);
	pch = SkipWhite(pch);
	if (*pch != ']')
	{
		SyntaxError(_T("Missing ']'"));
		return pch;
	}
	pch++;
	Write(opArray);
	return pch;
}

const TCHAR* CFunctionCompiler::ParseCall(const TCHAR* pch)
{
	// we have evaluated the reference to the function already and are looking at the '('

	int nArgs = 0;

	ASSERT(*pch == '(');
	pch++;

	pch = SkipWhite(pch);

	if (*pch == ')')
	{
		pch++;
	}
	else
	{
		for (;;)
		{
			pch = ParseExpression(pch);
			nArgs++;
			pch = SkipWhite(pch);
			if (*pch == ')') { pch++; break; }
			if (*pch != ',') { SyntaxError(_T("Missing ,")); return pch; }
			pch++;
		}
	}

	Write(opCall);
	Write((BYTE)nArgs);
	return pch;
}

// Recursive descent expression parser with operator precedence
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

		const TCHAR* pchOper = pch;
		const DOPER* pdoper;
		pch = ParseOperator(pch, pdoper);
		if (pdoper == NULL || pdoper->m_pri != nPrio)
			return pchOper;

		pch = ParseExp(pch, nPrio - 1);
		Write(pdoper->m_op);
	}
}

const TCHAR* CFunctionCompiler::ParseExpression(const TCHAR* pch)
{
	return ParseExp(pch, LAST_PRI);
}

// =========================================================================
// Statement parsers -- if, while, for, do, break, continue, return, sleep
// =========================================================================

const TCHAR* CFunctionCompiler::ParseIF(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != '(') { SyntaxError(_T("expected '('")); return pch; }
	pch++;

	pch = ParseExpression(pch);

	pch = SkipWhite(pch);
	if (*pch != ')') { SyntaxError(_T("expected ')'")); return pch; }
	pch++;

	Write(opCond);
	UINT nop = GetAddress();
	WriteInteger(0);

	pch = ParseStatement(pch);

	// Check for else
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	if (cchToken == 4 && _tcsncmp(pchToken, _T("else"), cchToken) == 0)
	{
		Write(opJump);
		UINT nopElse = GetAddress();
		WriteInteger(0);
		Fixup(nop, GetAddress());
		pch = ParseStatement(pch);
		Fixup(nopElse, GetAddress());
	}
	else
	{
		pch = pchToken; // back up
		Fixup(nop, GetAddress());
	}

	return pch;
}

const TCHAR* CFunctionCompiler::ParseWHILE(const TCHAR* pch)
{
	BREAK* pOldBreaks = m_breaks;
	m_breaks = NULL;
	m_nBreakables++;

	pch = SkipWhite(pch);
	if (*pch != '(') { SyntaxError(_T("expected '('")); return _T(""); }
	pch++;

	int nopOldTop = m_nopTopOfLoop;
	m_nopTopOfLoop = GetAddress();
	pch = ParseExpression(pch);

	pch = SkipWhite(pch);
	if (*pch != ')') { SyntaxError(_T("expected ')'")); return _T(""); }
	pch++;

	Write(opCond);
	UINT nopFixup = GetAddress();
	WriteInteger(0);

	pch = ParseStatement(pch);

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	Fixup(nopFixup, GetAddress());
	FixupBreaks();

	m_breaks = pOldBreaks;
	m_nBreakables--;
	m_nopTopOfLoop = nopOldTop;

	return pch;
}

const TCHAR* CFunctionCompiler::ParseDO(const TCHAR* pch)
{
	BREAK* pOldBreaks = m_breaks;
	m_breaks = NULL;
	m_nBreakables++;

	pch++;

	int nopOldTop = m_nopTopOfLoop;
	m_nopTopOfLoop = GetAddress();

	pch = ParseStatement(pch);

	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);
	if (cchToken != 5 || _tcsncmp(pchToken, _T("while"), 5) != 0)
	{
		SyntaxError(_T("expected 'while'"));
		return _T("");
	}

	pch = SkipWhite(pch);
	if (*pch != '(') { SyntaxError(_T("expected '('")); return _T(""); }
	pch++;

	pch = ParseExpression(pch);

	pch = SkipWhite(pch);
	if (*pch != ')') { SyntaxError(_T("expected ')'")); return _T(""); }
	pch++;

	pch = SkipWhite(pch);
	if (*pch != ';') { SyntaxError(_T("expected ';'")); return _T(""); }
	pch++;

	Write(opCond);
	UINT nopFixup = GetAddress();
	WriteInteger(0);

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	Fixup(nopFixup, GetAddress());
	FixupBreaks();

	m_breaks = pOldBreaks;
	m_nBreakables--;
	m_nopTopOfLoop = nopOldTop;

	return pch;
}

const TCHAR* CFunctionCompiler::ParseFOR(const TCHAR* pch)
{
	BREAK* pOldBreaks = m_breaks;
	m_breaks = NULL;
	m_nBreakables++;

	pch = SkipWhite(pch);
	if (*pch != '(') { SyntaxError(_T("expected '('")); return _T(""); }
	pch++;

	// Initializer
	const TCHAR* pchToken;
	int cchToken;
	Token(pch, pchToken, cchToken);
	if (cchToken == 3 && _tcsncmp(pchToken, _T("var"), cchToken) == 0)
	{
		pch += 3;
		pch = ParseLocalVar(pch);
	}
	else
	{
		pch = ParseExpression(pch);
		Write(opDrop);
		pch = SkipWhite(pch);
		if (*pch != ';') { SyntaxError(_T("expected ';'")); return _T(""); }
		pch++;
	}

	// Condition
	int nopOldTop = m_nopTopOfLoop;
	m_nopTopOfLoop = GetAddress();
	pch = ParseExpression(pch);

	pch = SkipWhite(pch);
	if (*pch != ';') { SyntaxError(_T("expected ';'")); return _T(""); }
	pch++;

	// Save third expression position, parse it, then rewind
	int nopExp3 = GetAddress();
	const TCHAR* pchExp3 = pch;
	pch = ParseExpression(pch);
	Write(opDrop);
	m_nop = nopExp3;

	pch = SkipWhite(pch);
	if (*pch != ')') { SyntaxError(_T("expected ')'")); return _T(""); }
	pch++;

	Write(opCond);
	UINT nopFixup = GetAddress();
	WriteInteger(0);

	pch = ParseStatement(pch);

	// Generate the third expression code
	ParseExpression(pchExp3);
	Write(opDrop);

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	Fixup(nopFixup, GetAddress());
	FixupBreaks();

	m_breaks = pOldBreaks;
	m_nBreakables--;
	m_nopTopOfLoop = nopOldTop;

	return pch;
}

void CFunctionCompiler::FixupBreaks()
{
	for (BREAK* pBreak = m_breaks; pBreak != NULL; )
	{
		BREAK* pNext = pBreak->m_next;
		Fixup(pBreak->m_nop, GetAddress());
		delete pBreak;
		pBreak = pNext;
	}
}

const TCHAR* CFunctionCompiler::ParseBREAK(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != ';') { SyntaxError(_T("Missing ';'")); return _T(""); }
	pch++;

	if (m_nBreakables == 0) { SyntaxError(_T("unexpected 'break'")); return _T(""); }

	Write(opJump);
	BREAK* pBreak = new BREAK;
	pBreak->m_next = m_breaks;
	m_breaks = pBreak;
	pBreak->m_nop = GetAddress();
	WriteInteger(0);

	return pch;
}

const TCHAR* CFunctionCompiler::ParseCONTINUE(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch != ';') { SyntaxError(_T("Missing ';'")); return _T(""); }
	pch++;

	if (m_nBreakables == 0) { SyntaxError(_T("unexpected 'continue'")); return _T(""); }

	Write(opJump);
	WriteInteger(m_nopTopOfLoop);

	return pch;
}

const TCHAR* CFunctionCompiler::ParseRETURN(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch == ';')
		Write(opNull);
	else
	{
		pch = ParseExpression(pch);
		pch = SkipWhite(pch);
		if (*pch != ';') { SyntaxError(_T("Missing ';'")); return _T(""); }
	}
	pch++;
	Write(opRet);
	return pch;
}

const TCHAR* CFunctionCompiler::ParseSLEEP(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch == ';')
		Write(opNull);
	else
	{
		pch = ParseExpression(pch);
		pch = SkipWhite(pch);
		if (*pch != ';') { SyntaxError(_T("Missing ';'")); return _T(""); }
	}
	pch++;
	Write(opSleep);
	return pch;
}

const TCHAR* CFunctionCompiler::ParseBlock(const TCHAR* pch)
{
	bool bBrace = (*pch == '{');
	if (bBrace)
		pch++;

	int nFrameSize = m_nFrameSize;

	Write(opFrame);
	UINT nopFrameFixup = GetAddress();
	WriteInteger(0);

	while (*pch != '\0' && (!bBrace || *pch != '}'))
		pch = ParseStatement(pch);

	if (bBrace)
	{
		if (*pch != '}') { SyntaxError(_T("expected '}'")); return pch; }
		pch++;
	}

	Fixup(nopFrameFixup, m_nFrameSize - nFrameSize);
	Write(opEndFrame);
	WriteInteger(m_nFrameSize - nFrameSize);

	m_nFrameSize = nFrameSize;
	return pch;
}

const TCHAR* CFunctionCompiler::ParseStatement(const TCHAR* pch)
{
	pch = SkipWhite(pch);
	if (*pch == '}') return pch;
	if (*pch == '{') return ParseBlock(pch);

	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	Write(opStatement);
	WriteInteger(g_nLine);

	if (cchToken == 3 && _tcsncmp(pchToken, _T("var"), cchToken) == 0)
		return ParseLocalVar(pch);
	if (cchToken == 2 && _tcsncmp(pchToken, _T("if"), cchToken) == 0)
		return ParseIF(pch);
	if (cchToken == 5 && _tcsncmp(pchToken, _T("while"), cchToken) == 0)
		return ParseWHILE(pch);
	if (cchToken == 3 && _tcsncmp(pchToken, _T("for"), cchToken) == 0)
		return ParseFOR(pch);
	if (cchToken == 2 && _tcsncmp(pchToken, _T("do"), cchToken) == 0)
		return ParseDO(pch);
	if (cchToken == 5 && _tcsncmp(pchToken, _T("break"), cchToken) == 0)
		return ParseBREAK(pch);
	if (cchToken == 8 && _tcsncmp(pchToken, _T("continue"), cchToken) == 0)
		return ParseCONTINUE(pch);
	if (cchToken == 6 && _tcsncmp(pchToken, _T("return"), cchToken) == 0)
		return ParseRETURN(pch);
	if (m_bBehavior && cchToken == 5 && _tcsncmp(pchToken, _T("sleep"), cchToken) == 0)
		return ParseSLEEP(pch);

	// Expression statement
	pch = ParseExpression(pchToken);
	Write(opDrop);

	if (*pch != 0)
	{
		if (*pch != ';') { SyntaxError(_T("expected ';'")); return _T(""); }
		pch++;
	}

	return pch;
}

const TCHAR* CFunctionCompiler::ParseLocalVar(const TCHAR* pch)
{
	for (;;)
	{
		const TCHAR* pchToken;
		int cchToken;
		pch = Token(pch, pchToken, cchToken);

		m_rgstLocal[m_nFrameSize].pchName = pchToken;
		m_rgstLocal[m_nFrameSize].cchName = (short)cchToken;
		m_nFrameSize++;

		pch = SkipWhite(pch);
		if (*pch == '=')
		{
			pch++;
			Write(opLocal);
			WriteInteger(m_nFrameSize - 1);
			pch = ParseExpression(pch);
			Write(opAssign);
			Write(opDrop);
			pch = SkipWhite(pch);
		}

		if (*pch != ',')
			break;
		pch++;
	}

	if (*pch != ';')
		SyntaxError(_T("Expected a ';'"));
	else
		pch++;

	return pch;
}

// =========================================================================
// Section 4: XAP scene graph parser -- CClassCompiler
// Parses VRML97-like node declarations, properties, and inline scripts
// =========================================================================

CClassCompiler::CClassCompiler(CClass* pClass)
{
	ASSERT(pClass != NULL);
	m_targetClass = pClass;
}

const TCHAR* CClassCompiler::Compile(const TCHAR* pch)
{
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == '\0' || *pch == '}')
			break;
		pch = ParseNode(pch);
	}

	Write(0);
	return pch;
}

const TCHAR* CClassCompiler::ParseNode(const TCHAR* pch)
{
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

	if (cchToken == 3 && _tcsncmp(pchToken, _T("DEF"), cchToken) == 0)
	{
		pch = Token(pch, pchToken, cchToken, true);

		CMember* pMember = new CMember;
		pMember->m_memberIndex = m_targetClass->m_nVarCount++;
		m_targetClass->AddMember(pchToken, cchToken, pMember);

		Write(opDefNode);
		WriteInteger(pMember->m_memberIndex);

		pch = ParseNode(pch);
	}
	else if (cchToken == 3 && _tcsncmp(pchToken, _T("USE"), cchToken) == 0)
	{
		pch = Token(pch, pchToken, cchToken, true);

		CMember* pMember = (CMember*)m_targetClass->GetMember(pchToken, cchToken);
		if (pMember == NULL || pMember->m_obj != objMember)
		{
			TCHAR szBuf[256];
			if (cchToken > 255) cchToken = 255;
			CopyChars(szBuf, pchToken, cchToken);
			szBuf[cchToken] = 0;
			SyntaxError(_T("Undefined USE '%s'"), szBuf);
			return szParseError;
		}

		Write(opUseNode);
		WriteInteger(pMember->m_memberIndex);
	}
	else if (cchToken == 8 && _tcsncmp(pchToken, _T("behavior"), cchToken) == 0)
	{
		CFunction* pFunction;
		pch = ParseFunction(pch, pFunction, true);
		if (pFunction != NULL)
			m_targetClass->SetMember(pchToken, cchToken, pFunction);
	}
	else if (cchToken == 8 && _tcsncmp(pchToken, _T("function"), cchToken) == 0)
	{
		pch = Token(pch, pchToken, cchToken);

		if (cchToken == 8 && _tcsncmp(_T("behavior"), pchToken, cchToken) == 0)
		{
			SyntaxError(_T("invalid function name"));
			return szParseError;
		}

		CFunction* pFunction;
		pch = ParseFunction(pch, pFunction);
		if (pFunction != NULL)
		{
			m_targetClass->SetMember(pchToken, cchToken, pFunction);
		}
	}
	else if (cchToken == 3 && _tcsncmp(pchToken, _T("var"), cchToken) == 0)
	{
		pch = ParseMemberVar(pch);
	}
	else if (cchToken == 5 && _tcsncmp(pchToken, _T("class"), cchToken) == 0)
	{
		pch = ParseClass(pch);
	}
	else if (cchToken == 6 && _tcsncmp(pchToken, _T("import"), cchToken) == 0)
	{
		pch = Token(pch, pchToken, cchToken);
		pch = SkipWhite(pch);
		if (*pch != ',') { SyntaxError(_T("Expected ','")); return szParseError; }
		pch++;

		const TCHAR* pchURL;
		int cchURL;
		pch = Token(pch, pchURL, cchURL);

		TCHAR szURL[MAX_PATH];
		CopyChars(szURL, pchURL, cchURL);
		szURL[cchURL] = '\0';

		CClass* pClass = new CClass;

		TRACE(_T("Importing "), szURL);

		if (!pClass->Load(szURL))
		{
			delete pClass;
			return szParseError;
		}

		g_classes->Define(pchToken, cchToken, (CNode*)pClass);
	}
	else
	{
		// Node instantiation
		CNodeClass* pNodeClass = LookupClass(pchToken, cchToken);
		if (pNodeClass == NULL)
			pNodeClass = CNodeClass::FindByName(pchToken, cchToken);

		if (pNodeClass == NULL)
		{
			TCHAR chSav = pchToken[cchToken];
			((TCHAR*)pchToken)[cchToken] = '\0';
			SyntaxError(_T("Unknown class: %s"), pchToken);
			((TCHAR*)pchToken)[cchToken] = chSav;
			return szParseError;
		}

		Write(opNewNode);
		WriteString(pchToken, cchToken);

		pch = SkipWhite(pch);

		if (*pch == '{')
		{
			pch++;
			pch = ParseProps(pch, pNodeClass);
			if (*pch != '}')
			{
				SyntaxError(_T("Expected a '}'"));
				return szParseError;
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

const TCHAR* CClassCompiler::ParseChildren(const TCHAR* pch)
{
	pch = SkipWhite(pch);

	if (*pch == '[')
	{
		pch++;
		pch = SkipWhite(pch);

		for (;;)
		{
			pch = SkipWhite(pch);
			if (*pch == '\0' || *pch == '}' || *pch == ']')
				break;
			pch = ParseNode(pch);
		}

		if (*pch == ']')
			pch++;
		else
			SyntaxError(_T("Expected a ']'"));
	}
	else
	{
		pch = ParseNode(pch);
	}

	return pch;
}

const TCHAR* CClassCompiler::ParseProps(const TCHAR* pch, CNodeClass* pNodeClass)
{
	for (;;)
	{
		pch = SkipWhite(pch);
		if (*pch == '\0' || *pch == '}')
			break;

		const TCHAR* pchToken;
		int cchToken;
		pch = Token(pch, pchToken, cchToken);

		// Inline behavior
		if (cchToken == 8 && _tcsncmp(_T("behavior"), pchToken, cchToken) == 0)
		{
			CFunction* pFunction;
			pch = ParseFunction(pch, pFunction, true);
			if (pFunction != NULL)
			{
				int nFunction = m_targetClass->m_instanceFunctions.GetLength();
				m_targetClass->m_instanceFunctions.AddNode((CNode*)pFunction);
				Write(opFunction);
				WriteString(pchToken, cchToken);
				WriteInteger(nFunction);
			}
			continue;
		}

		// Inline function
		if (cchToken == 8 && _tcsncmp(_T("function"), pchToken, cchToken) == 0)
		{
			pch = Token(pch, pchToken, cchToken);
			if (cchToken == 8 && _tcsncmp(_T("behavior"), pchToken, cchToken) == 0)
			{
				SyntaxError(_T("invalid function name"));
				return szParseError;
			}

			CFunction* pFunction;
			pch = ParseFunction(pch, pFunction);
			if (pFunction != NULL)
			{
				int nFunction = m_targetClass->m_instanceFunctions.GetLength();
				m_targetClass->m_instanceFunctions.AddNode((CNode*)pFunction);
				Write(opFunction);
				WriteString(pchToken, cchToken);
				WriteInteger(nFunction);
			}
			continue;
		}

		// Property assignment
		const PRD* pprd = pNodeClass->FindProp(pchToken, cchToken);

		if (pprd == NULL)
		{
			TCHAR chSav = pchToken[cchToken];
			((TCHAR*)pchToken)[cchToken] = '\0';
			SyntaxError(_T("Unknown property: %s"), pchToken);
			((TCHAR*)pchToken)[cchToken] = chSav;
			return szParseError;
		}

		if (pprd->nType == pt_children || pprd->nType == pt_nodearray)
		{
			Write(opInitArray);
			WriteInteger(PTR2INT(pprd->pbOffset));
			pch = ParseChildren(pch);
			Write(opEndArray);
		}
		else if (pprd->nType == pt_node)
		{
			Write(opNewNodeProp);
			Write(pprd, sizeof(PRD));
			pch = ParseNode(pch);
		}
		else
		{
			union { bool b; int i; float n; float v[4]; TCHAR* s; } value;
			BYTE* pbArray = NULL;
			int cbValue = 0;

			switch (pprd->nType)
			{
			case pt_boolean:    pch = ParseBoolean(pch, value.b); cbValue = sizeof(bool); break;
			case pt_integer:    pch = ParseInteger(pch, value.i); cbValue = sizeof(int); break;
			case pt_number:     pch = ParseNumber(pch, value.n); cbValue = sizeof(float); break;
			case pt_string:     pch = ParseString(pch, value.s); cbValue = _tcslen(value.s); break;
			case pt_vec3:
			case pt_color:      pch = ParseVec3(pch, value.v); cbValue = sizeof(D3DXVECTOR3); break;
			case pt_vec4:
			case pt_quaternion: pch = ParseVec4(pch, value.v); cbValue = sizeof(D3DXVECTOR4); break;
			case pt_intarray:   pch = ParseIntArray(pch, pbArray, cbValue); break;
			case pt_numarray:   pch = ParseVecArray(pch, pbArray, cbValue, 1); break;
			case pt_vec2array:  pch = ParseVecArray(pch, pbArray, cbValue, 2); break;
			case pt_vec3array:  pch = ParseVecArray(pch, pbArray, cbValue, 3); break;
			case pt_vec4array:  pch = ParseVecArray(pch, pbArray, cbValue, 4); break;
			}

			Write(opInitProp);
			Write(pprd, sizeof(PRD));

			if (pprd->nType == pt_string)
			{
				WriteString(value.s, cbValue);
				delete[] value.s;
			}
			else
			{
				WriteInteger(cbValue);
				if (pbArray != NULL)
				{
					Write(pbArray, cbValue);
					delete[] pbArray;
				}
				else
				{
					Write(&value, cbValue);
				}
			}
		}
	}

	return pch;
}

const TCHAR* CClassCompiler::ParseMemberVar(const TCHAR* pch)
{
	for (;;)
	{
		const TCHAR* pchToken;
		int cchToken;
		pch = Token(pch, pchToken, cchToken);

		CMember* pMember = new CMember;
		pMember->m_memberIndex = m_targetClass->m_nVarCount++;
		m_targetClass->AddMember(pchToken, cchToken, pMember);

		pch = SkipWhite(pch);

		if (*pch == '=')
		{
			pch++;
			SyntaxError(_T("Sorry, class member initializers have not been implemented yet!"));
			return szParseError;
		}

		if (*pch != ',')
			break;
		pch++;
	}

	if (*pch != ';')
		SyntaxError(_T("Expected a ';'"));
	else
		pch++;

	return pch;
}

const TCHAR* CClassCompiler::ParseFunction(const TCHAR* pch, CFunction*& pFunction, bool bBehavior)
{
	CFunctionCompiler compiler;
	compiler.m_bBehavior = bBehavior;

	pFunction = NULL;
	pch = SkipWhite(pch);

	if (!bBehavior)
	{
		// Parse parameter list
		if (*pch != '(')
		{
			return pch;
		}
		pch++;

		for (;;)
		{
			pch = SkipWhite(pch);
			if (*pch == '\0' || *pch == ')')
				break;

			const TCHAR* pchToken;
			int cchToken;
			pch = Token(pch, pchToken, cchToken);

			compiler.m_rgstLocal[compiler.m_nFrameSize].pchName = pchToken;
			compiler.m_rgstLocal[compiler.m_nFrameSize].cchName = (short)cchToken;
			compiler.m_nFrameSize++;

			pch = SkipWhite(pch);
			if (*pch != ',')
				break;
			pch++;
		}

		if (*pch != ')')
		{
			SyntaxError(_T("expected ')'\n"));
			return szParseError;
		}
		pch++;
	}

	// Parse function body
	{
		pch = SkipWhite(pch);
		if (*pch != '{')
		{
			return pch;
		}
		pch = compiler.ParseBlock(pch);
	}

	compiler.Write(opNull);
	compiler.Write(opRet);

	pFunction = compiler.CreateFunction();
	return pch;
}

const TCHAR* CClassCompiler::ParseClass(const TCHAR* pch)
{
	const TCHAR* pchToken;
	int cchToken;
	pch = Token(pch, pchToken, cchToken);

#ifdef _DEBUG
	{
		TCHAR chSav = pchToken[cchToken];
		((TCHAR*)pchToken)[cchToken] = '\0';
		TRACE(_T("class %s\n"), pchToken);
		((TCHAR*)pchToken)[cchToken] = chSav;
	}
#endif

	// TODO: base class? interfaces?

	pch = SkipWhite(pch);
	if (*pch != '{')
	{
		SyntaxError(_T("Expected '{'\n"));
		return szParseError;
	}
	pch++;

	CClass* pClass = new CClass;
	pch = pClass->ParseClassBody(pch);

	if (*pch != '}')
	{
		SyntaxError(_T("Expected '}'"));
		return szParseError;
	}
	pch++;

	g_classes->Define(pchToken, cchToken, (CNode*)pClass);
	return pch;
}

// =========================================================================
// Class namespace init/cleanup
// =========================================================================

void Class_Init()
{
	g_classes = new CNameSpace;
}

void Class_Exit()
{
	delete g_classes;
}
