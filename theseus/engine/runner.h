// runner.h: VM runtime stack machine, value object hierarchy, and built-ins.
//
// The dashboard's XAP VM is a register-stack machine that walks bytecode
// produced by xap_compile.cpp. CRunner holds the per-execution state
// (stack, frame pointer, current bytecode position, sleep clock) and
// drives the dispatch loop in xap_vm.cpp. Every value the VM pushes is a
// CObject subclass: numbers wrap CNumObject, strings wrap CStrObject,
// "this is a member of <instance>" wraps CMemberVarObject, and so on.
// Math built-ins live on a global CMathClass instance the runner finds by
// name when scripts call Math.foo().
//
// None of these classes export properties via NODE_PROP. The only
// node-system surface here is CStrObject's NODE_FUN entries (charAt,
// indexOf, etc.) and CMathClass's NODE_FUN entries (sin, cos, etc.). All
// member fields below are pure VM internals and are renamable.

class CInstance;
class CFunction;

// One slot in CObject::m_members -- a named entry in an instance's
// scripted member table. m_memberIndex is the index back into the
// owning class's variable table.
class CMember : public CObject
{
public:
	CMember();
	~CMember();

	void Assign(CObject* pObject);
	CObject* Deref();

	int m_memberIndex;
};

// Run-time reference to "instance.member" -- the value pushed onto
// the VM stack when bytecode dereferences a class member. Deref reads
// through to the actual variable; Assign writes through.
class CMemberVarObject : public CObject
{
public:
	CMemberVarObject(CInstance* pInstance, int memberIndex);
	~CMemberVarObject();

	CInstance* m_owner;
	int m_memberIndex;

	CObject* Deref();
	void Assign(CObject* pObject);
};


// Run-time reference to a method bound to a particular instance --
// what gets pushed when a script does instance.foo (without calling).
class CMemberFunctionObject : public CObject
{
public:
	CMemberFunctionObject(CObject* pOwner, CFunction* pFunction);
	~CMemberFunctionObject();
	CObject* Deref();
	CObject* m_owner;
	CFunction* m_function;
};


CClass* LookupClass(const TCHAR* pchName, int cchName);

// Identifier-token value: holds the literal name of a variable that
// hasn't been resolved yet. The compiler emits these for unbound
// references; the runner resolves them lazily on Deref.
class CVarObject : public CObject
{
public:
	CVarObject(const TCHAR* pch, int cch);
	~CVarObject();

	int m_length;
	int m_capacity;
	TCHAR* m_text;

	CObject* Deref();
	void Assign(CObject* pObject);

private:
    // Need this to prevent the compiler from using default copy ctor
    CVarObject(const CVarObject&);
    CVarObject& operator=(const CVarObject& rhs);
};

class CNumObject : public CObject
{
public:
	CNumObject();
	CNumObject(float n);
	CNumObject(const TCHAR* szNum);

	CStrObject* ToStr();

	CNumObject* ToNum()
	{
		AddRef();
		return this;
	}

	float m_value;
};


// Script-visible string object. Owns a TCHAR buffer and exposes the
// JavaScript-style string API (charAt, indexOf, slice, etc.) via the
// FND table in string_util.cpp. The buffer grows on Append.
class CStrObject : public CObject
{
public:
	CStrObject();
	CStrObject(const TCHAR* sz);
	CStrObject(const TCHAR* pch, int cch);
	~CStrObject();

	inline const TCHAR* GetSz()
	{
		if (m_text == NULL)
			return _T("");
		return m_text;
	}

	inline int GetLength()
	{
		return m_length;
	}

	CStrObject* ToStr();
	CNumObject* ToNum();

	TCHAR* SetLength(int nLength);
	void Append(const TCHAR* szAppend);


	// JavaScript-style String methods exposed via the FND table.
	int length();
	CStrObject* charAt(int index);
	int charCodeAt(int index);
	CStrObject* concat(const TCHAR* sz);
	CObject* indexOf(CObject** rgparam, int nParam);
	CObject* lastIndexOf(CObject** rgparam, int nParam);
	CStrObject* slice(int start, int end);
	CObject* substr(CObject** rgparam, int nParam);
	CObject* substring(CObject** rgparam, int nParam);
	CStrObject* toLowerCase();
	CStrObject* toUpperCase();

	TCHAR* m_text;

protected:
	int m_length;
	int m_capacity;

	DECLARE_NODE_FUNCTIONS()

private:
    // Need this to prevent the compiler from using default copy ctor
    CStrObject(const CStrObject&);
    CStrObject& operator=(const CStrObject& rhs);
};

// Script-visible wrapper around a CNodeArray. Lets scenes treat a
// node array property as a JavaScript-ish array (length, push, splice).
class CNodeArrayObject : public CObject
{
public:
	CNodeArrayObject(CNodeArray* pNodeArray);

	CObject* Call(int nFunction, CObject** rgparam, int nParam);
	CObject* Dot(CObject* pObject);

	CNodeArray* m_nodeArray;
};

// Reference to a function bound to a particular owning object --
// what bytecode pushes when it pulls "this.foo" off an instance.
class CFunctionObject : public CObject
{
public:
	CFunctionObject();
	~CFunctionObject();

	int m_functionIndex;
	CObject* m_owner;

	CObject* Call(CObject** rgparam, int nParam);
};


#define opNull		0
#define opNew		1
#define opVar		2
#define opJump		3
#define opCond		4
#define opCall		5
#define opRet		6
#define opStr		7
#define opNum		8
#define opAdd		9
#define opSub		10
#define opMul		11
#define opDiv		12
#define opDot		13
#define opAssign	14
#define opLocal		15
#define opEQ		16
#define opNE		17
#define opLT		18
#define opGT		19
#define opLE		20
#define opGE		21
#define opMod		22
#define opSHL		23
#define opSHR		24
#define opSHRU		25
#define opAnd		26
#define opOr		27
#define opXor		28
#define opLAnd		29
#define opLOr		30
#define opQuest		31
#define opAddAssign	32
#define opSubAssign	33
#define opMulAssign	34
#define opDivAssign	35
#define opModAssign	36
#define opAndAssign	37
#define opOrAssign	38
#define opXorAssign	39
#define opSHLAssign	40
#define opSHRAssign	41
#define opArray		42
#define opThis		43
#define opFrame		44
#define opStatement	45
#define opNeg		46
#define opDrop		47
#define opEndFrame	48
#define opSleep		49


#define opNewNode		100
#define opNewNodeProp	101
#define opDefNode		102
#define opUseNode		103
#define opEndNode		104
#define opInitProp		105
#define opInitArray		106
#define opEndArray		107
#define opFunction		108

// Saved CRunner state across a function call. PushContext spills the
// outer frame here when bytecode steps into a callee, PopContext
// restores it on Ret. The field names mirror CRunner exactly so the
// push/pop code can move state with one assignment per slot.
struct RUNCONTEXT
{
	RUNCONTEXT* m_nextContext;

	BYTE* m_ops;
	UINT m_nop;
	UINT m_spFrame;
	CObject* m_self;
	CObject* m_thisRef;
	UINT m_sp;
};

class CRunner
{
public:
	CRunner(CObject* pThis);
	~CRunner();

	bool SetFunc(const TCHAR* szFunc);


	void Push(float nValue);
	void Push(int nValue);
	void Push(const TCHAR* szValue, int nLen = -1);

	void Push(CObject* pObject);
	CObject* Pop();

	bool HasFunc() const { return m_ops != NULL; }
	void SetFunc(CFunction* pFunction);
	void ResetFunc();

	CObject* Run();
	bool Step(CObject** ppRetObj = NULL);
	void Error(const TCHAR* szFmt, ...);

	BYTE* LookupFunction(const TCHAR* pchName, int cchName, CObject*& pOwner);
	CObject* LookupVariable(const TCHAR* pchName, int cchName);

	bool IsSleeping()
	{
		if (m_wakeup != 0.0f)
		{
			if (m_wakeup > TheseusGetNow())
				return true;

			m_wakeup = 0.0f;
		}

		return false;
	}

	void DumpStack();

protected:
	XTIME m_wakeup;          // sleep deadline (0 = not sleeping)
	UINT m_nop;              // bytecode offset within m_ops
	BYTE* m_ops;             // currently executing bytecode buffer
	UINT m_sp;               // stack pointer (top-of-stack index)
	CObject* m_stack [256];  // operand stack
	UINT m_spFrame;          // frame pointer for the current call
	UINT m_spBase;           // bottom of the locals region
	CObject* m_self;         // current 'self' (callable owner)
	CObject* m_thisRef;        // current 'this' (script-visible binding)

	bool m_bError;
	int m_nLine;

	bool BinaryOperator(BYTE op);

	void PushContext(UINT nPopStack);
	void PopContext();
	BOOL ExecuteBuiltIn(const TCHAR* pchName, int cchName, int nParam, CObject** rgParam, CObject*& pRetObj);

	inline int FetchInt()
	{
#if defined(_WIN32_WCE)
		int n;
		CopyMemory(&n, &m_ops[m_nop], sizeof (int));
#else
		int n = *((int*)&m_ops[m_nop]);
#endif
		m_nop += sizeof (int);
		return n;
	}

	inline UINT FetchUInt()
	{
#if defined(_WIN32_WCE)
		UINT n;
		CopyMemory(&n, &m_ops[m_nop], sizeof (UINT));
#else
		UINT n = *((UINT*)&m_ops[m_nop]);
#endif
		m_nop += sizeof (UINT);
		return n;
	}

	inline float FetchFloat()
	{
#if defined(_WIN32_WCE)
		float n;
		CopyMemory(&n, &m_ops[m_nop], sizeof (float));
#else
		float n = *((float*)&m_ops[m_nop]);
#endif
		m_nop += sizeof (float);
		return n;
	}

	inline const TCHAR* FetchString(int& cch)
	{
		cch = FetchInt();
		const TCHAR* pch = (const TCHAR*)&m_ops[m_nop];
		m_nop += cch * sizeof (TCHAR);
		return pch;
	}

	RUNCONTEXT* m_nextContext;
};

extern CRunner* g_pRunner;
extern CObject* g_pThis;

extern bool ExecuteScript(CObject* pObject, const TCHAR* szFunc);




#undef min
#undef max

class CMathClass : public CNodeClass
{
public:
	CMathClass();

	void AddRef();
	void Release();

	// Static-style helpers exposed to scripts via the NODE_FUN table.
	float abs(float number);
	float acos(float number);
	float asin(float number);
	float atan(float number);
	float atan2(float y, float x);
	float ceil(float number);
	float cos(float number);
	float exp(float number);
	float floor(float number);
	float log(float number);
	float max(float number1, float number2);
	float min(float number1, float number2);
	float pow(float base, float exponent);
	float random();
	float round(float number);
	float sin(float number);
	float sqrt(float number);
	float tan(float number);
	float projectMEnabled(); // returns 1.0 if desktop has g_useMilkdropViz set
	CStrObject* itoa(int number);

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
