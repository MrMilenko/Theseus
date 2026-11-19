// runner.h: the script VM. CRunner executes compiled bytecode on an object
// stack; the C*Object classes are the values it pushes. Bodies are in
// xap_vm.cpp, string_util.cpp and math_node.cpp; docs/decomp/VM.md has the
// binary write-up.
//
// Layouts checked against the 5960 retail XBE:
//   CRunner: m_wakeup 0x00 (double), m_opPos 0x08, m_code 0x0c, m_sp 0x10,
//     m_stack 0x14, m_spFrame 0x1a4, m_spBase 0x1a8, m_self 0x1ac,
//     m_thisRef 0x1b0, m_bFailed 0x1b4, m_nLine 0x1b8, m_nextContext 0x1bc.
//     5960's stack holds 100 objects; Theseus gives it 256.
//   RUNCONTEXT 0x1c: next, ops, nop, spFrame, self, thisRef, sp (PushContext 0x37690).
//   CNumObject 0x18: m_value 0x14.
//   CVarObject: m_length 0x14, m_capacity 0x18, m_text 0x1c.
//   CStrObject: 5960 has length 0x14, capacity 0x18, text 0x1c; Theseus puts
//     m_text first. Nothing reads these by offset.
//   CMathClass: the eight constants follow the CNodeClass part at 0x2c.

class CInstance;
class CFunction;

CClass* LookupClass(const TCHAR* name, int nameLen);

// Bytecode. 0..49 are statement/expression ops, 100..108 build the node tree.
enum
{
	opNull,
	opNew,
	opVar,
	opJump,
	opCond,
	opCall,
	opRet,
	opStr,
	opNum,
	opAdd,
	opSub,
	opMul,
	opDiv,
	opDot,
	opAssign,
	opLocal,
	opEQ,
	opNE,
	opLT,
	opGT,
	opLE,
	opGE,
	opMod,
	opSHL,
	opSHR,
	opSHRU,
	opAnd,
	opOr,
	opXor,
	opLAnd,
	opLOr,
	opQuest,
	opAddAssign,
	opSubAssign,
	opMulAssign,
	opDivAssign,
	opModAssign,
	opAndAssign,
	opOrAssign,
	opXorAssign,
	opSHLAssign,
	opSHRAssign,
	opArray,
	opThis,
	opFrame,
	opStatement,
	opNeg,
	opDrop,
	opEndFrame,
	opSleep,

	opNewNode = 100,
	opNewNodeProp,
	opDefNode,
	opUseNode,
	opEndNode,
	opInitProp,
	opInitArray,
	opEndArray,
	opFunction
};

// A number on the stack.
class CNumObject : public CObject
{
public:
	CNumObject();
	CNumObject(float n);
	CNumObject(const TCHAR* text);

	CNumObject* ToNum() { AddRef(); return this; }
	CStrObject* ToStr();

	float m_value;
};

// A string on the stack, plus the script String methods (FND table in
// string_util.cpp). Append grows the buffer.
class CStrObject : public CObject
{
public:
	CStrObject();
	CStrObject(const TCHAR* text);
	CStrObject(const TCHAR* src, int len);
	~CStrObject();

	inline const TCHAR* GetSz() { return m_text != NULL ? m_text : _T(""); }
	inline int GetLength() { return m_length; }

	CNumObject* ToNum();
	CStrObject* ToStr();

	TCHAR* SetLength(int len);
	void Append(const TCHAR* more);

	int length();
	CStrObject* charAt(int i);
	int charCodeAt(int i);
	CStrObject* concat(const TCHAR* text);
	CObject* indexOf(CObject** args, int argc);
	CObject* lastIndexOf(CObject** args, int argc);
	CStrObject* slice(int from, int to);
	CObject* substr(CObject** args, int argc);
	CObject* substring(CObject** args, int argc);
	CStrObject* toLowerCase();
	CStrObject* toUpperCase();

	TCHAR* m_text;

protected:
	int m_length;
	int m_capacity;

	DECLARE_NODE_FUNCTIONS()

private:
	// Owns its buffer; no copies.
	CStrObject(const CStrObject&);
	CStrObject& operator=(const CStrObject&);
};

// A name the compiler couldn't bind. Deref looks it up when it runs.
class CVarObject : public CObject
{
public:
	CVarObject(const TCHAR* src, int len);
	~CVarObject();

	CObject* Deref();
	void Assign(CObject* obj);

	int m_length;
	int m_capacity;
	TCHAR* m_text;

private:
	CVarObject(const CVarObject&);
	CVarObject& operator=(const CVarObject&);
};

// An entry in an object's member name space; m_memberIndex is the slot in
// the owning class's variables.
class CMember : public CObject
{
public:
	CMember();
	~CMember();

	CObject* Deref();
	void Assign(CObject* obj);

	int m_memberIndex;
};

// "instance.var": reads and writes go to that instance's variable slot.
class CMemberVarObject : public CObject
{
public:
	CMemberVarObject(CInstance* inst, int slot);
	~CMemberVarObject();

	CObject* Deref();
	void Assign(CObject* obj);

	CInstance* m_owner;
	int m_memberIndex;
};

// "instance.method" without the call: the method bound to its owner.
class CMemberFunctionObject : public CObject
{
public:
	CMemberFunctionObject(CObject* owner, CFunction* func);
	~CMemberFunctionObject();

	CObject* Deref();

	CObject* m_owner;
	CFunction* m_function;
};

// An FND function bound to the object it was looked up on.
class CFunctionObject : public CObject
{
public:
	CFunctionObject();
	~CFunctionObject();

	CObject* Call(CObject** args, int argc);

	int m_functionIndex;
	CObject* m_owner;
};

// A node array property seen from script (length, indexing, push...).
class CNodeArrayObject : public CObject
{
public:
	CNodeArrayObject(CNodeArray* array);

	CObject* Call(int index, CObject** args, int argc);
	CObject* Dot(CObject* obj);

	CNodeArray* m_nodeArray;
};

// The caller's state, saved while a called function runs.
struct RUNCONTEXT
{
	RUNCONTEXT* m_nextContext;
	BYTE* m_code;
	UINT m_opPos;
	UINT m_spFrame;
	CObject* m_self;
	CObject* m_thisRef;
	UINT m_sp;
};

class CRunner
{
public:
	CRunner(CObject* self);
	~CRunner();

	bool SetFunc(const TCHAR* funcName);
	void SetFunc(CFunction* func);
	void ResetFunc();
	bool HasFunc() const { return m_code != NULL; }

	void Push(float value);
	void Push(int value);
	void Push(const TCHAR* value, int len = -1);
	void Push(CObject* obj);
	CObject* Pop();

	CObject* Run();
	bool Step(CObject** ret = NULL);
	void Error(const TCHAR* fmt, ...);

	BYTE* LookupFunction(const TCHAR* name, int nameLen, CObject*& owner);
	CObject* LookupVariable(const TCHAR* name, int nameLen);

	// A sleep that has run out clears itself here.
	bool IsSleeping()
	{
		if (m_wakeup)
		{
			if (m_wakeup > TheseusGetNow())
				return true;
			m_wakeup = 0.0f;
		}
		return false;
	}

	void DumpStack();

protected:
	bool BinaryOperator(BYTE op);
	void PushContext(UINT argc);
	void PopContext();
	BOOL ExecuteBuiltIn(const TCHAR* name, int nameLen, int argc, CObject** args, CObject*& ret);

	// Operands sit unaligned in the op stream.
	inline int FetchInt()
	{
		int n;
		memcpy(&n, m_code + m_opPos, sizeof (n));
		m_opPos += sizeof (n);
		return n;
	}

	inline UINT FetchUInt()
	{
		UINT n;
		memcpy(&n, m_code + m_opPos, sizeof (n));
		m_opPos += sizeof (n);
		return n;
	}

	inline float FetchFloat()
	{
		float n;
		memcpy(&n, m_code + m_opPos, sizeof (n));
		m_opPos += sizeof (n);
		return n;
	}

	// Length first, then that many characters in place.
	inline const TCHAR* FetchString(int& cch)
	{
		cch = FetchInt();
		const TCHAR* pch = (const TCHAR*)(m_code + m_opPos);
		m_opPos += cch * sizeof (TCHAR);
		return pch;
	}

	XTIME m_wakeup;			// sleep deadline, 0 when awake
	UINT m_opPos;				// offset into m_code
	BYTE* m_code;			// bytecode being run
	UINT m_sp;				// next free stack slot
	CObject* m_stack [256];
	UINT m_spFrame;			// first local of the current call
	UINT m_spBase;
	CObject* m_self;		// owner of the running function
	CObject* m_thisRef;		// what "this" means in script
	bool m_bFailed;
	int m_nLine;
	RUNCONTEXT* m_nextContext;
};

extern CRunner* g_pActiveRunner;
extern CObject* g_pThis;

extern bool ExecuteScript(CObject* obj, const TCHAR* funcName);

#undef min
#undef max

// The global Math object: a class whose functions and constants scripts
// reach as Math.name.
class CMathClass : public CNodeClass
{
public:
	CMathClass();

	void AddRef();
	void Release();

	float abs(float x);
	float acos(float x);
	float asin(float x);
	float atan(float x);
	float atan2(float y, float x);
	float ceil(float x);
	float cos(float x);
	float exp(float x);
	float floor(float x);
	float log(float x);
	float max(float a, float b);
	float min(float a, float b);
	float pow(float x, float y);
	float random();
	float round(float x);
	float sin(float x);
	float sqrt(float x);
	float tan(float x);
	float projectMEnabled();	// Theseus: 1 when the desktop milkdrop visualizer is on
	CStrObject* itoa(int x);

	float m_E;
	float m_LN2;
	float m_LN10;
	float m_LOG2E;
	float m_LOG10E;
	float m_PI;
	float m_SQRT1_2;
	float m_SQRT2;

	DECLARE_NODE_FUNCTIONS()
};
