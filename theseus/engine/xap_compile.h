// xap_compile.h: turns XAP text into bytecode. The tokenizer and value
// parsers are free functions; CClassCompiler handles a file's node tree,
// CFunctionCompiler a function body. Bodies in xap_compile.cpp, binary
// write-up in docs/decomp/Compiler.md.
//
// CCompiler layout matches 5960: vtbl, m_opPos 0x04, m_code 0x08,
// m_opsSize 0x0c, m_bFailed 0x10.

#pragma once

class CNodeClass;
class CClass;
class CFunction;

// Which comment styles SkipWhite steps over. Bit flags.
enum COMMENT_TYPE
{
	none = 0,
	pound = 1,
	slashslash = 2,
	slashstar = 4,
	any = 0xffffffff
};

void StartParse(const TCHAR* src, const TCHAR* fileName, int line = 1);
void EndParse();
extern void SyntaxError(const TCHAR* fmt, ...);

const TCHAR* SkipWhite(const TCHAR* src, COMMENT_TYPE comments = any);
const TCHAR* Token(const TCHAR* src, const TCHAR*& tok, int& tokLen, bool paths = false, COMMENT_TYPE comments = any);

const TCHAR* ParseBoolean(const TCHAR* src, bool& b);
const TCHAR* ParseInteger(const TCHAR* src, int& i);
const TCHAR* ParseNumber(const TCHAR* src, float& n);
const TCHAR* ParseString(const TCHAR* src, TCHAR*& out);
const TCHAR* ParseVec3(const TCHAR* src, float v[3]);
const TCHAR* ParseVec4(const TCHAR* src, float v[4]);
const TCHAR* ParseIntArray(const TCHAR* src, BYTE*& data, int& size);
const TCHAR* ParseVecArray(const TCHAR* src, BYTE*& data, int& size, int dims);
int ExpandCString(TCHAR* out, int outMax, const TCHAR* tok, int tokLen);

// Local variable name in a function frame.
struct ST
{
	short cchName;
	const TCHAR* pchName;
};

// Operator table row: up to three characters, precedence, and the op it emits.
struct DOPER
{
	TCHAR m_ch1;
	TCHAR m_ch2;
	TCHAR m_ch3;
	TCHAR m_pri;
	BYTE m_op;
};

// A "break" jump waiting for its loop's end address.
struct BREAK
{
	BREAK* m_next;
	int m_opPos;
};

// Growable op buffer shared by both compilers.
class CCompiler
{
public:
	CCompiler();
	virtual ~CCompiler();

	// 5960 grows when the write would reach the end, then copies (0x34440).
	inline void Write(const void* data, int size)
	{
		if (m_opPos + size >= m_opsSize)
			GrowTo(m_opPos + size);
		memcpy(m_code + m_opPos, data, size);
		m_opPos += size;
	}

	inline void Write(BYTE b) { Write(&b, 1); }
	inline void WriteInteger(int n) { Write(&n, sizeof (n)); }
	inline void WriteNumber(float n) { Write(&n, sizeof (n)); }

	// Length, then the characters.
	inline void WriteString(const TCHAR* src, int len)
	{
		WriteInteger(len);
		Write(src, len * sizeof (TCHAR));
	}

	inline UINT GetAddress() { return m_opPos; }

	// Patches a jump or frame size written earlier. 5960 stores straight in.
	inline void Fixup(UINT at, UINT value)
	{
		if (CHECK(at + sizeof (UINT) <= m_opPos))
			memcpy(m_code + at, &value, sizeof (value));
	}

	void GrowTo(int size);
	CFunction* CreateFunction();

	void SyntaxError(const TCHAR* fmt, ...);
	inline void SetError() { m_bFailed = true; }
	inline bool HadError() const { return m_bFailed; }

protected:
	UINT m_opPos;
	BYTE* m_code;
	UINT m_opsSize;
	bool m_bFailed;
};

// Compiles one function or behavior body.
class CFunctionCompiler : public CCompiler
{
public:
	CFunctionCompiler();

	const TCHAR* ParseBlock(const TCHAR* src);
	const TCHAR* ParseStatement(const TCHAR* src);
	const TCHAR* ParseLocalVar(const TCHAR* src);
	const TCHAR* ParseIF(const TCHAR* src);
	const TCHAR* ParseWHILE(const TCHAR* src);
	const TCHAR* ParseFOR(const TCHAR* src);
	const TCHAR* ParseDO(const TCHAR* src);
	const TCHAR* ParseBREAK(const TCHAR* src);
	const TCHAR* ParseCONTINUE(const TCHAR* src);
	const TCHAR* ParseRETURN(const TCHAR* src);
	const TCHAR* ParseSLEEP(const TCHAR* src);

	const TCHAR* ParseExpression(const TCHAR* src);
	const TCHAR* ParseExp(const TCHAR* src, int prio);
	const TCHAR* ParseOperator(const TCHAR* src, const DOPER*& oper);
	const TCHAR* ParseTerm(const TCHAR* src);
	const TCHAR* ParseArray(const TCHAR* src);
	const TCHAR* ParseCall(const TCHAR* src);

	void FixupBreaks();

	ST m_rgstLocal[100];
	int m_nFrameSize;
	BREAK* m_breaks;
	int m_breakDepth;
	int m_loopStartOp;
	bool m_bBehavior;
};

// Compiles a whole .xap: nodes, props, member vars, classes, functions.
class CClassCompiler : public CCompiler
{
public:
	CClassCompiler(CClass* cls);

	const TCHAR* Compile(const TCHAR* src);

protected:
	const TCHAR* ParseNode(const TCHAR* src);
	const TCHAR* ParseProps(const TCHAR* src, CNodeClass* cls);
	const TCHAR* ParseChildren(const TCHAR* src);
	const TCHAR* ParseMemberVar(const TCHAR* src);
	const TCHAR* ParseClass(const TCHAR* src);
	const TCHAR* ParseFunction(const TCHAR* src, CFunction*& func, bool behavior = false);

	CClass* m_targetClass;
};
