// xap_vm.cpp: XAP script virtual machine.
//
// Bytecode interpreter, runtime object system, and property binding.
// Stack-based architecture with reference-counted objects, late-bound
// variable lookup, and a context stack for nested function calls.
// Executes bytecode produced by xap_compile.cpp. Decompiled from the
// 5960 retail XBE; see docs/decomp/VM.md.
#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "camera.h"
#include "xap_compile.h"

#ifdef _XBOX
#include "..\..\ntos\inc\xlaunch.h"
#endif

//#define LOCALTRACE TRACE
#define LOCALTRACE 1 ? (void)0 : ::Trace

// =========================================================================
// Globals
// =========================================================================

CObject* g_pThis;
CRunner* g_pRunner;
CObject** g_rgParam;
int g_nParam;

extern bool g_bInputEnable;
extern CCamera theCamera;

CObject* LookupMember(CObject* pThis, const TCHAR* pchName, int cchName);

// =========================================================================
// Section 1: Runtime object types
// =========================================================================

// CNumObject: numeric value (all XAP numbers are float).

CNumObject::CNumObject()
{
	m_obj = objNumber;
	m_value = 0.0f;
}

CNumObject::CNumObject(float nValue)
{
	m_obj = objNumber;
	m_value = nValue;
}

CNumObject::CNumObject(const TCHAR* szValue)
{
	m_obj = objNumber;
	m_value = (float)_tcstod(szValue, NULL);
}

CStrObject* CNumObject::ToStr()
{
	TCHAR szBuf[20];
	TCHAR* pch = szBuf + _stprintf(szBuf, _T("%f"), m_value) - 1;
	// Strip trailing zeros
	while (*pch == '0') pch--;
	if (*pch == '.') pch--;
	pch++;
	*pch = '\0';
	return new CStrObject(szBuf);
}

// CVec3Object: 3D vector for XAP scripts.

class CVec3Object : public CObject
{
	DECLARE_NODE(CVec3Object, CObject)
public:
	CVec3Object();
	CVec3Object(const D3DXVECTOR3& v);
	CStrObject* ToStr();
	float m_x, m_y, m_z;
	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Vector3", CVec3Object, CObject)

START_NODE_PROPS(CVec3Object, CObject)
	NODE_PROP(pt_number, CVec3Object, x)
	NODE_PROP(pt_number, CVec3Object, y)
	NODE_PROP(pt_number, CVec3Object, z)
END_NODE_PROPS()

CVec3Object::CVec3Object()
{
	m_obj = objVec3;
	m_x = m_y = m_z = 0.0f;
}

CVec3Object::CVec3Object(const D3DXVECTOR3& v)
{
	m_obj = objVec3;
	m_x = v.x;
	m_y = v.y;
	m_z = v.z;
}

CStrObject* CVec3Object::ToStr()
{
	TCHAR szBuf[60];
	_stprintf(szBuf, _T("%f %f %f"), m_x, m_y, m_z);
	return new CStrObject(szBuf);
}

// CLocalVariable: stack frame local with ref-counted value.

class CLocalVariable : public CObject
{
public:
	CLocalVariable();
	~CLocalVariable();
	CObject* Deref();
	void Assign(CObject* pObject);
	CObject* m_pValue;
};

CLocalVariable::CLocalVariable()  { m_pValue = NULL; }

CLocalVariable::~CLocalVariable()
{
	if (m_pValue != NULL)
		m_pValue->Release();
}

void CLocalVariable::Assign(CObject* pObject)
{
	if (m_pValue != NULL)
		m_pValue->Release();
	m_pValue = pObject;
	if (m_pValue != NULL)
		m_pValue->AddRef();
}

CObject* CLocalVariable::Deref()
{
	CObject* pObject = m_pValue;
	if (pObject != NULL)
		pObject->AddRef();
	Release();
	return pObject;
}

// CVarObject: late-bound variable name.

CVarObject::CVarObject(const TCHAR* pch, int cch)
{
	m_obj = objVariable;
	m_length = cch;
	m_capacity = cch + 1;
	m_text = new TCHAR[m_capacity];
	CopyChars(m_text, pch, cch);
	m_text[cch] = '\0';
}

CVarObject::~CVarObject()
{
	delete[] m_text;
}

CObject* CVarObject::Deref()
{
	CObject* pObject;

	// Built-in globals
	if (_tcscmp(m_text, _T("Math")) == 0)
	{
		extern CMathClass g_Math;
		pObject = &g_Math;
	}
	else if (_tcscmp(m_text, _T("camera")) == 0)
	{
		pObject = &theCamera;
	}
	else
	{
		pObject = g_pRunner->LookupVariable(m_text, _tcslen(m_text));
		if (pObject == NULL)
		{
			g_pRunner->Error(_T("Unknown object: %s"), m_text);
			return NULL;
		}

		if (pObject->m_obj == objMember)
		{
			LOCALTRACE(_T("member\n"));
		}
	}

	pObject->AddRef();
	pObject = pObject->Deref();
	Release();
	return pObject;
}

void CVarObject::Assign(CObject* pNewObject)
{
	CObject* pObject = g_pRunner->LookupVariable(m_text, _tcslen(m_text));
	if (pObject == NULL)
	{
		g_pRunner->Error(_T("Unknown variable: %s"), m_text);
		return;
	}
	pObject->Assign(pNewObject);
}

// CMemberVarObject: reference to an instance variable by index.

CMemberVarObject::CMemberVarObject(CInstance* pObject, int nMember)
{
	ASSERT(pObject != NULL);
	ASSERT(nMember >= 0 && nMember < pObject->m_vars.GetLength());

	m_obj = objMemberVar;
	m_owner = pObject;
	m_memberIndex = nMember;
	m_owner->AddRef();
}

CMemberVarObject::~CMemberVarObject()
{
	ASSERT(m_owner != NULL);
	m_owner->Release();
}

CObject* CMemberVarObject::Deref()
{
	ASSERT(m_memberIndex >= 0 && m_memberIndex < m_owner->m_vars.GetLength());
	CObject* pObj = m_owner->m_vars.GetNode(m_memberIndex);
	if (pObj != NULL)
		pObj->AddRef();
	Release();
	return pObj;
}

void CMemberVarObject::Assign(CObject* pNewObject)
{
	ASSERT(m_memberIndex >= 0 && m_memberIndex < m_owner->m_vars.GetLength());
	CObject* pOldObj = m_owner->m_vars.GetNode(m_memberIndex);
	if (pOldObj != NULL)
		pOldObj->Release();
	m_owner->m_vars.SetNode(m_memberIndex, (CNode*)pNewObject);
	if (pNewObject != NULL)
		pNewObject->AddRef();
}

// CMemberFunctionObject: reference to a scripted member function.

CMemberFunctionObject::CMemberFunctionObject(CInstance* pInstance, CFunction* pFunction)
{
	m_obj = objMemberFunction;
	m_owner = pInstance;
	m_function = pFunction;
}

CMemberFunctionObject::~CMemberFunctionObject() {}

CObject* CMemberFunctionObject::Deref()
{
	CRunner runner(m_owner);
	runner.SetFunc(m_function);
	CObject* pObject = runner.Run();
	Release();
	return pObject;
}

// CFunctionObject: reference to a native function.

CFunctionObject::CFunctionObject()
{
	m_obj = objFunctionRef;
	m_owner = NULL;
}

CFunctionObject::~CFunctionObject()
{
	if (m_owner != NULL)
		m_owner->Release();
}

CObject* CFunctionObject::Call(CObject** rgparam, int nParam)
{
	return m_owner->Call(m_functionIndex, rgparam, nParam);
}

// CFunction: compiled bytecode.

CFunction::CFunction()
{
	m_obj = objFunction;
	m_codeSize = 0;
	m_name = NULL;
}

CFunction::~CFunction()
{
	delete [] m_name;
}

// CMember: named member variable (class scope).

CMember::CMember()
{
	m_obj = objMember;
	m_memberIndex = -1;
}

CMember::~CMember() {}

CObject* CMember::Deref()
{
	ASSERT(g_pThis != NULL);
	ASSERT(g_pThis->m_obj == objInstance);

	CInstance* pThis = (CInstance*)g_pThis;

	ASSERT(m_memberIndex >= 0 && m_memberIndex < pThis->m_vars.GetLength());

	CObject* pObj = pThis->m_vars.GetNode(m_memberIndex);
	if (pObj != NULL)
		pObj->AddRef();
	Release();
	return pObj;
}

void CMember::Assign(CObject* pNewValue)
{
	ASSERT(g_pThis != NULL);
	ASSERT(g_pThis->m_obj == objInstance);

	CInstance* pThis = (CInstance*)g_pThis;

	ASSERT(m_memberIndex >= 0 && m_memberIndex < pThis->m_vars.GetLength());

	CObject* pObj = pThis->m_vars.GetNode(m_memberIndex);
	if (pObj != NULL)
		pObj->Release();
	pThis->m_vars.SetNode(m_memberIndex, (CNode*)pNewValue);
	if (pNewValue != NULL)
		pNewValue->AddRef();
}

void CObject::Assign(CObject* pObject)
{
	g_pRunner->Error(_T("Cannot assign to objects"));
}

// CNodeArrayObject: script-accessible wrapper for CNodeArray.

CNodeArrayObject::CNodeArrayObject(CNodeArray* pNodeArray)
{
	m_obj = objNodeArray;
	m_nodeArray = pNodeArray;
}

CObject* CNodeArrayObject::Call(int nFunction, CObject** rgparam, int nParam)
{
	switch (nFunction)
	{
	case 0: // length
		return new CNumObject((float)m_nodeArray->GetLength());

	case 1: // add
		{
			CObject* pObj = rgparam[0]->Deref();
			pObj->AddRef();
			m_nodeArray->AddNode((CNode*)pObj);
		}
		break;

	case 2: // remove
		{
			CObject* pObj = rgparam[0]->Deref();
			m_nodeArray->RemoveNode((CNode*)pObj);
			pObj->Release();
		}
		break;
	}

	return NULL;
}

CObject* CNodeArrayObject::Dot(CObject* pObj)
{
	if (pObj->m_obj == objVariable)
	{
		CVarObject* pVar = (CVarObject*)pObj;

		int nFunc = -1;
		if (pVar->m_length == 6 && _tcsncmp(pVar->m_text, _T("length"), pVar->m_length) == 0)
			nFunc = 0;
		else if (pVar->m_length == 3 && _tcsncmp(pVar->m_text, _T("add"), pVar->m_length) == 0)
			nFunc = 1;
		else if (pVar->m_length == 6 && _tcsncmp(pVar->m_text, _T("remove"), pVar->m_length) == 0)
			nFunc = 2;

		if (nFunc >= 0)
		{
			CFunctionObject* pFun = new CFunctionObject;
			pFun->m_functionIndex = nFunc;
			pFun->m_owner = this;
			AddRef();
			return pFun;
		}
	}
	else
	{
		// Array indexing
		int n = (int)((CNumObject*)pObj)->m_value;
		if (n < 0 || n >= m_nodeArray->GetLength())
		{
			g_pRunner->Error(_T("Array reference out of bounds"));
			return NULL;
		}

		CNode* pNode = m_nodeArray->GetNode(n);
		pNode->AddRef();
		return pNode;
	}

	return CObject::Dot(pObj);
}

// =========================================================================
// Section 2: Property system. Bridges XAP scripts to C++ node properties.
// =========================================================================

CProperty::CProperty(CObject* pNode, const PRD* pprd)
{
	m_node = pNode;
	m_pprd = pprd;
	pNode->AddRef();
}

CProperty::~CProperty()
{
	m_node->Release();
}

CObject* CProperty::Deref()
{
	void* pvValue = (BYTE*)m_node + (int)m_pprd->pbOffset;

	switch (m_pprd->nType)
	{
	case pt_string:
		{
			CStrObject* pStr = new CStrObject(*(TCHAR**)pvValue);
			Release();
			return pStr;
		}
	case pt_number:
		{
			float n = *(float*)pvValue;
			Release();
			return new CNumObject(n);
		}
	case pt_integer:
		{
			int n = *(int*)pvValue;
			Release();
			return new CNumObject((float)n);
		}
	case pt_boolean:
		{
			bool n = *(bool*)pvValue;
			Release();
			return new CNumObject((float)n);
		}
	case pt_node:
		{
			CNode* pNode = *(CNode**)pvValue;
			if (pNode != NULL) pNode->AddRef();
			Release();
			return pNode;
		}
	case pt_vec3:
		{
			D3DXVECTOR3 v = *(D3DXVECTOR3*)pvValue;
			Release();
			return new CVec3Object(v);
		}
	case pt_nodearray:
	case pt_children:
		{
			CNodeArrayObject* pRet = new CNodeArrayObject((CNodeArray*)pvValue);
			Release();
			return pRet;
		}
	}

	g_pRunner->Error(_T("Unknown property type"));
	return NULL;
}

void CProperty::Assign(CObject* pObject)
{
	if (pObject == NULL && m_pprd->nType != pt_node)
	{
		g_pRunner->Error(_T("Illegal null assignement"));
		return;
	}

	switch (m_pprd->nType)
	{
	case pt_string:
		{
			CStrObject* pStr = pObject->Deref()->ToStr();
			const TCHAR* sz = pStr->GetSz();
			m_node->SetProperty(m_pprd, &sz, pStr->GetLength());
			pStr->Release();
		}
		break;
	case pt_number:
		{
			CNumObject* pNum = pObject->Deref()->ToNum();
			m_node->SetProperty(m_pprd, &pNum->m_value, sizeof(float));
			pNum->Release();
		}
		break;
	case pt_integer:
		{
			CNumObject* pNum = pObject->Deref()->ToNum();
			int n = (int)pNum->m_value;
			m_node->SetProperty(m_pprd, &n, sizeof(int));
			pNum->Release();
		}
		break;
	case pt_boolean:
		{
			CNumObject* pNum = pObject->Deref()->ToNum();
			bool b = pNum->m_value != 0.0f;
			m_node->SetProperty(m_pprd, &b, sizeof(bool));
			pNum->Release();
		}
		break;
	case pt_vec3:
		{
			CVec3Object* pVec = (CVec3Object*)pObject->Deref();
			if (pVec->m_obj != objVec3)
				TRACE(_T("\001Expected a Vector3 object!\n"));
			else
				m_node->SetProperty(m_pprd, &pVec->m_x, sizeof(float) * 3);
		}
		break;
	case pt_node:
		{
			CNode* pNode = NULL;
			if (pObject != NULL)
				pNode = (CNode*)pObject->Deref();
			m_node->SetProperty(m_pprd, &pNode, sizeof(CNode*));
		}
		break;
	}
}

CObject* CNode::Dot(CObject* pObj)
{
	return CObject::Dot(pObj);
}

CStrObject* CNode::ToStr()
{
	return new CStrObject(GetNodeClass()->m_className);
}

// =========================================================================
// Section 3: Helper functions
// =========================================================================

CObject* Dereference(CObject* pObject)
{
	CObject* pLast = pObject;
	while (pObject != NULL)
	{
		pObject = pLast->Deref();
		if (pObject == pLast)
			break;
		pLast = pObject;
	}
	return pObject;
}

CClass* LookupClass(const TCHAR* pchName, int cchName)
{
	extern CNameSpace* g_classes;
	CObject* pObject = g_classes->Lookup(pchName, cchName);
	if (pObject == NULL || pObject->m_obj != objClass)
		return NULL;
	return (CClass*)pObject;
}

CObject* CreateNewObject(const TCHAR* pchClassName, int cchClassName, CObject** rgparam, int nParam)
{
	g_rgParam = rgparam;
	g_nParam = nParam;

	// Check scripted classes first
	CClass* pClass = LookupClass(pchClassName, cchClassName);
	if (pClass != NULL)
		return pClass->CreateNode();

	// Try built-in node types
	CObject* pNode = NewNode(pchClassName, cchClassName);
	if (pNode != NULL)
		return pNode;

	return NULL;
}

bool ExecuteScript(CObject* pObject, const TCHAR* szScript)
{
	CFunctionCompiler compiler;
	compiler.ParseBlock(szScript);
	if (compiler.HadError())
		return false;

	compiler.Write(opRet);
	CFunction* pFunction = compiler.CreateFunction();

	CRunner runner(pObject);
	runner.SetFunc(pFunction);
	runner.Run();

	delete pFunction;
	return true;
}

bool CallFunction(CObject* pObject, const TCHAR* szFunc, int nParam, CObject** rgParam)
{
	g_pThis = pObject;
	CRunner runner(pObject);
	if (!runner.SetFunc(szFunc))
		return false;

	for (int i = 0; i < nParam; i++)
		runner.Push(rgParam[i]);

	runner.Run();
	return true;
}

// =========================================================================
// Section 4: Member lookup. Resolves names through the scene hierarchy.
// =========================================================================

CObject* FindMember(CNodeClass* pClass, const TCHAR* pchName, int cchName)
{
	for ( ; pClass != NULL; pClass = pClass->m_baseClass)
	{
		CObject* pObj = pClass->GetMember(pchName, cchName);
		if (pObj != NULL)
			return pObj;
	}
	return NULL;
}

CObject* LookupMember(CObject* pThis, const TCHAR* pchName, int cchName)
{
#ifdef _DEBUG0
	TCHAR szBuf [256];
	int cch = cchName;
	if (cch > 255)
		cch = 255;
	CopyChars(szBuf, pchName, cch);
	szBuf[cch] = 0;
	TRACE(_T("LookupMember: 0x%08x '%s'\n"), pThis, szBuf);
#endif

	ASSERT(pThis != NULL);

	g_pThis = pThis;

	// Check node properties
	if (pThis->m_obj == objNode)
	{
		CNode* pNode = (CNode*)pThis;
		const PRD* pprd = pNode->FindProp(pchName, cchName);
		if (pprd != NULL)
		{
			CObject* pObj = new CProperty(pNode, pprd);
			pObj->m_nRefCount = 0;
			return pObj;
		}
	}

	// Check instance members
	{
		CObject* pObject = pThis->GetMember(pchName, cchName);
		if (pObject != NULL)
			return pObject;
	}

	// Check class hierarchy
	{
		CObject* pObj = FindMember(pThis->GetNodeClass(), pchName, cchName);
		if (pObj != NULL)
			return pObj;
	}

	// Walk parent chain
	{
		for (CObject* pObject = pThis->m_parent; pObject != NULL; pObject = pObject->m_parent)
		{
			CObject* pObj = LookupMember(pObject, pchName, cchName);
			if (pObj != NULL)
				return pObj;
		}
	}

	return NULL;
}

// =========================================================================
// Section 5: CRunner, the bytecode interpreter.
// =========================================================================

CRunner::CRunner(CObject* pObject)
{
	ASSERT(pObject != NULL);

	m_nop = 0;
	m_ops = NULL;
	m_sp = 0;
	m_spBase = 0;
	m_spFrame = 0;
	m_self = pObject;
	m_thisRef = pObject;
	m_bError = false;
	m_nextContext = NULL;
	m_wakeup = 0.0f;
	ZeroMemory(m_stack, sizeof(m_stack));
}

CRunner::~CRunner()
{
	ResetFunc();
}

bool CRunner::SetFunc(const TCHAR* szFunc)
{
	ResetFunc();
	m_ops = LookupFunction(szFunc, _tcslen(szFunc), m_thisRef);
	return m_ops != NULL;
}

void CRunner::SetFunc(CFunction* pFunction)
{
	ResetFunc();
	m_nop = 0;
	m_sp = 0;
	m_spFrame = 0;
	m_ops = pFunction->m_rgop;
	m_nLine = 0;
	m_wakeup = 0.0f;
}

void CRunner::ResetFunc()
{
	while (m_sp > 0)
	{
		m_sp--;
		CObject* pObj = m_stack[m_sp];
		m_stack[m_sp] = NULL;
		if (pObj != NULL)
			pObj->Release();
	}
	m_bError = false;
	m_nop = 0;
	m_sp = 0;
	m_spFrame = 0;
	m_wakeup = 0.0f;
}

// Stack operations

void CRunner::Push(float nValue)   { Push(new CNumObject(nValue)); }
void CRunner::Push(int nValue)     { Push(new CNumObject((float)nValue)); }

void CRunner::Push(const TCHAR* szValue, int nLen)
{
	if (nLen == -1) nLen = _tcslen(szValue);
	Push(new CStrObject(szValue, nLen));
}

void CRunner::Push(CObject* pObject)
{
	if (m_sp >= countof(m_stack))
	{
		TRACE(_T("\001Script stack overflow!\n"));
		pObject->Release();
		return;
	}
	m_stack[m_sp++] = pObject;
}

CObject* CRunner::Pop()
{
	ASSERT(m_sp > 0);
	m_sp--;
	CObject* pObject = m_stack[m_sp];
	m_stack[m_sp] = NULL;
	return pObject;
}

void CRunner::Error(const TCHAR* szFmt, ...)
{
	if (m_bError)
		return;

	va_list args;
	va_start(args, szFmt);

	TCHAR szBuffer[512];
	_vsntprintf(szBuffer, countof(szBuffer), szFmt, args);

	TCHAR szMessage[1024];
	if (m_nLine == 0)
		_stprintf(szMessage, _T("Runtime Error\n\n%s"), szBuffer);
	else
		_stprintf(szMessage, _T("Runtime Error\n\nLine: %d\n\n%s"), m_nLine, szBuffer);

	Trace(_T("\001\007%s\n"), szMessage);

#ifdef _DEBUG
	DumpStack();
#endif

#ifdef UIX_DESKTOP
	extern int TheseusMessageBox(const char* szText, unsigned int uType);
	TheseusMessageBox(szMessage, 0);
#endif

	va_end(args);
	m_bError = true;
}

// Context stack for nested calls

void CRunner::PushContext(UINT nParam)
{
	RUNCONTEXT* pCtx = new RUNCONTEXT;
	pCtx->m_ops = m_ops;
	pCtx->m_nop = m_nop;
	pCtx->m_spFrame = m_spFrame;
	pCtx->m_self = m_self;
	pCtx->m_thisRef = m_thisRef;
	pCtx->m_sp = m_sp - (nParam + 1);
	pCtx->m_nextContext = m_nextContext;
	m_nextContext = pCtx;
}

void CRunner::PopContext()
{
	RUNCONTEXT* pCtx = m_nextContext;
	ASSERT(pCtx != NULL);

	CObject* pRetObject = Dereference(Pop());

	m_ops = pCtx->m_ops;
	m_nop = pCtx->m_nop;
	m_spFrame = pCtx->m_spFrame;
	m_self = pCtx->m_self;
	m_thisRef = pCtx->m_thisRef;
	g_pThis = m_thisRef;

	UINT i;
	for (i = pCtx->m_sp; i < m_sp; i++)
	{
		if (m_stack[i] != NULL)
		{
			m_stack[i]->Release();
			m_stack[i] = NULL;
		}
	}

	m_sp = pCtx->m_sp;
	m_nextContext = pCtx->m_nextContext;
	delete pCtx;

	Push(pRetObject);
}

// Main execution loop

CObject* CRunner::Run()
{
	m_spBase = m_sp;
	for (;;)
	{
		if (m_bError) return NULL;

#ifdef _DEBUG
		// Make sure everything on the stack has been cleaned up!
		{
			for (UINT i = 0; i < 20; i += 1)
				ASSERT(m_stack[m_sp + i] == NULL);
		}
#endif
		CObject* pRetObj = NULL;
		if (!Step(&pRetObj))
			return pRetObj;
	}
}

// =========================================================================
// Section 6: Opcode interpreter
// =========================================================================

bool CRunner::Step(CObject** ppRetObj)
{
	g_pRunner = this;

	BYTE op = m_ops[m_nop++];
	switch (op)
	{
	default:
		ASSERT(FALSE); // Lost in space!
		return NULL;

	case opSleep:
		{
			CObject* pObj = Dereference(Pop());
			if (pObj == NULL)      { Error(_T("illegal null reference")); return false; }
			if (pObj->m_obj != objNumber) { Error(_T("expected a number")); return false; }
			m_wakeup = TheseusGetNow() + ((CNumObject*)pObj)->m_value;
			pObj->Release();
			return true;
		}

	case opStatement:
		m_nLine = FetchInt();
		LOCALTRACE(_T("opStatement: line %d, sp %d\n"), m_nLine, m_sp);
		break;

	case opDrop:
		{
			LOCALTRACE(_T("opDrop\n"));
			CObject* pObject = Pop();
			if (pObject != NULL) pObject->Release();
		}
		break;

	case opFrame:
		{
			int nFrameSize = FetchInt();
			LOCALTRACE(_T("opFrame: %d\n"), nFrameSize);

#ifdef _DEBUG
			// Make sure everything on the stack has been cleaned up!
			{
				for (UINT i = m_sp; i < m_sp + nFrameSize; i += 1)
					ASSERT(m_stack[i] == NULL);
			}
#endif
			m_sp += nFrameSize;
			m_spBase = m_sp;
			ASSERT(m_sp < sizeof (m_stack) / sizeof (CObject*));
		}
		break;

	case opEndFrame:
		{
			int nFrameSize = FetchInt();
			LOCALTRACE(_T("opEndFrame: %d\n"), nFrameSize);
			while (nFrameSize-- > 0)
			{
				m_sp--;
				if (m_stack[m_sp] != NULL)
				{
					m_stack[m_sp]->Release();
					m_stack[m_sp] = NULL;
				}
			}
			m_spBase = m_sp;
		}
		break;

	case opNull:
		Push((CObject*)NULL);
		break;

	case opThis:
		ASSERT(m_self != NULL);
		m_self->AddRef();
		Push(m_self);
		break;

	case opNew:
		{
			LOCALTRACE(_T("opNew\n"));
			int nParam = FetchInt();
			int cch;
			const TCHAR* pch = FetchString(cch);
			CObject** rgparam = &m_stack[m_sp - nParam];

			CObject* pObj = CreateNewObject(pch, cch, rgparam, nParam);
			if (pObj == NULL)
			{
				TCHAR szBuf[256];
				if (cch > 255) cch = 255;
				CopyChars(szBuf, pch, cch);
				szBuf[cch] = '\0';
				Error(_T("Failed to create new object: \"%s\""), szBuf);
				return NULL;
			}

			for (int i = 0; i < nParam; i++)
			{
				if (rgparam[i] != NULL) { rgparam[i]->Release(); rgparam[i] = NULL; }
			}
			m_sp -= nParam;
			Push(pObj);
		}
		break;

	case opAssign:
		{
			LOCALTRACE(_T("opAssign\n"));
			CObject* pRight = Dereference(Pop());
			CObject* pLeft = Pop();
			if (pLeft == NULL) { Error(_T("cannot assign to null")); return NULL; }
			pLeft->Assign(pRight);
			pLeft->Release();
			Push(pRight);
		}
		break;

	case opDot:
	case opArray:
		{
			LOCALTRACE(_T("%d %s: \"%s\"\n"), m_nop - 1, op == opDot ? "opDot" : "opArray");

			CObject* pRight = Pop();
			CObject* pLeft = Pop();

			if (pRight == NULL || pLeft == NULL) { Error(_T("illegal null reference")); return NULL; }

			if (op == opArray)
				pRight = Dereference(pRight);

			ASSERT(pRight->m_obj == objVariable || pRight->m_obj == objNumber || pRight->m_obj == objString);

			CObject* pRealLeft = pLeft->Deref();
			if (pRealLeft == NULL) { Error(_T("illegal null reference")); return NULL; }

			CObject* pDot = pRealLeft->Dot(pRight);
			if (pDot == NULL)
			{
				if (pRight->m_obj == objVariable)
					Error(_T("Unknown member: %s"), ((CVarObject*)pRight)->m_text);
				else
				{
					CStrObject* pStr = pRight->ToStr();
					Error(_T("Unknown member: %s"), pStr->GetSz());
					pStr->Release();
				}
			}

			Push(pDot);
			pRealLeft->Release();
			pRight->Release();
		}
		break;

	case opNeg:
		{
			CObject* pObj = Dereference(Pop());
			if (pObj == NULL || pObj->m_obj != objNumber) { Error(_T("expected a number")); return NULL; }
			Push(-((CNumObject*)pObj)->m_value);
			pObj->Release();
		}
		break;

	case opAdd: case opSub: case opMul: case opDiv: case opMod:
	case opEQ:  case opNE:  case opLT:  case opLE:  case opGT: case opGE:
	case opAnd: case opXor: case opOr:  case opSHL: case opSHR:
		if (!BinaryOperator(op))
			return NULL;
		break;

	case opCall:
		{
			LOCALTRACE(_T("opCall sp=%d\n"), m_sp);

			int nParam = m_ops[m_nop++];
			ASSERT(m_sp >= (UINT)(nParam + 1));
			CObject* pFun = (CVarObject*)m_stack[m_sp - (nParam + 1)];
			CObject** rgparam = &m_stack[m_sp - nParam];

			if (pFun->m_obj == objFunctionRef)
			{
				m_stack[m_sp - (nParam + 1)] = NULL;

				CFunctionObject* pFunction = (CFunctionObject*)pFun;
				CObject* pRetObj = pFunction->Call(rgparam, nParam);
				pFun->Release();

				for (int i = 0; i < nParam; i++)
				{
					if (rgparam[i] != NULL) { rgparam[i]->Release(); rgparam[i] = NULL; }
				}
				m_sp -= nParam + 1;
				LOCALTRACE(_T("opCall(done) sp=%d\n"), m_sp);
				Push(pRetObj);
			}
			else if (pFun->m_obj == objMemberFunction)
			{
				CMemberFunctionObject* pFunction = (CMemberFunctionObject*)pFun;
				pFunction->AddRef();

#ifdef _DEBUG
				LOCALTRACE(_T("Call2 %s\n"), pFunction->m_function->m_name);
#endif
				PushContext(nParam);

				m_spFrame = m_sp - nParam;

				ASSERT(pFunction->m_owner != NULL);
				m_self = pFunction->m_owner;
				m_ops = pFunction->m_function->m_rgop;
				m_nop = 0;
			}
			else
			{
				CVarObject* pFunction = (CVarObject*)pFun;
				int cch = pFunction->m_length;
				const TCHAR* pch = pFunction->m_text;

				CObject* pRetObj = NULL;
				if (ExecuteBuiltIn(pch, cch, nParam, rgparam, pRetObj))
				{
					for (int i = 0; i < nParam; i++)
						rgparam[i] = NULL;
					m_sp -= nParam + 1;
					Push(pRetObj);
					pFun->Release();
				}
				else
				{
					CObject* pThis;
					BYTE* pop = LookupFunction(pch, cch, pThis);
					if (pop == NULL)
					{
						TCHAR szFunc[32];
						if (cch > countof(szFunc) - 1) cch = countof(szFunc) - 1;
						_tcsncpy(szFunc, pch, cch);
						szFunc[cch] = 0;
						Error(_T("unknown function: %s"), szFunc);
						return NULL;
					}

					PushContext(nParam);
					m_self = pThis;
					g_pThis = pThis;
					m_spFrame = m_sp - nParam;
					m_ops = pop;
					m_nop = 0;
				}
			}
		}
		break;

	case opRet:
		if (m_nextContext != NULL)
		{
			PopContext();
		}
		else
		{
			if (ppRetObj != NULL)
				*ppRetObj = m_sp > 0 ? Pop() : NULL;
			return false;
		}
		break;

	case opCond:
		{
			LOCALTRACE(_T("opCond\n"));
			bool bCond = false;
			CObject* pObj = Dereference(Pop());
			if (pObj != NULL)
			{
				CNumObject* pNum = pObj->ToNum();
				if (pNum->m_value != 0.0f)
					bCond = true;
				pNum->Release();
				pObj->Release();
			}

			if (bCond)
				m_nop += sizeof(UINT);
			else
				m_nop = FetchUInt();
		}
		break;

	case opJump:
		LOCALTRACE(_T("opJump\n"));
		m_nop = FetchUInt();
		break;

	case opVar:
		{
			LOCALTRACE(_T("opVar\n"));
			int cch;
			const TCHAR* pch = FetchString(cch);

#ifdef _DEBUG
			{
				TCHAR szBuf [256];
				CopyChars(szBuf, pch, cch);
				szBuf[cch] = '\0';
				LOCALTRACE(_T("opVar: \"%s\"\n"), szBuf);
			}
#endif

			Push(new CVarObject(pch, cch));
		}
		break;

	case opLocal:
		{
			LOCALTRACE(_T("opLocal\n"));
			int nLocal = FetchInt();

			ASSERT(m_spFrame + nLocal < m_sp);
			CObject* pObject = m_stack[m_spFrame + nLocal];
			if (pObject == NULL)
			{
				pObject = new CLocalVariable;
				m_stack[m_spFrame + nLocal] = pObject;
			}
			pObject->AddRef();
			Push(pObject);
		}
		break;

	case opStr:
		{
			int cch;
			const TCHAR* pch = FetchString(cch);
			Push(pch, cch);
		}
		break;

	case opNum:
		Push(FetchFloat());
		break;
	}

	return true;
}

// =========================================================================
// Section 7: Binary operators
// =========================================================================

bool CRunner::BinaryOperator(BYTE op)
{
	LOCALTRACE(_T("opOperator: %d\n"), op);

	CObject* pRight = Dereference(Pop());
	CObject* pLeft = Dereference(Pop());

	// Null comparison
	if (pLeft == NULL || pRight == NULL || pLeft->m_obj == objNode && pRight->m_obj == objNode)
	{
		float fValue;
		switch (op)
		{
		default: Error(_T("illegal null reference")); return false;
		case opEQ: fValue = (pLeft == pRight) ? 1.0f : 0.0f; Push(fValue); break;
		case opNE: fValue = (pLeft != pRight) ? 1.0f : 0.0f; Push(fValue); break;
		}

		if (pRight != NULL) pRight->Release();
		if (pLeft != NULL) pLeft->Release();
		return true;
	}

	// String comparison (non-add operators)
	if (op != opAdd && pLeft->m_obj == objString && pRight->m_obj == objString)
	{
		int nCmp = _tcscmp(((CStrObject*)pLeft)->GetSz(), ((CStrObject*)pRight)->GetSz());
		bool bValue;

		switch (op)
		{
		default: Error(_T("type mismatch")); return false;
		case opEQ: bValue = nCmp == 0; break;
		case opNE: bValue = nCmp != 0; break;
		case opLT: bValue = nCmp < 0;  break;
		case opLE: bValue = nCmp <= 0; break;
		case opGT: bValue = nCmp > 0;  break;
		case opGE: bValue = nCmp >= 0; break;
		}
		Push(bValue);
	}
	else if (pLeft->m_obj == objNumber && pRight->m_obj == objNumber)
	{
		// Numeric operations
		float fL = ((CNumObject*)pLeft)->m_value;
		float fR = ((CNumObject*)pRight)->m_value;
		float fValue;

#ifdef _DEBUG
		double intptr;
#endif

		switch (op)
		{
		case opEQ:  fValue = (fL == fR) ? 1.0f : 0.0f; break;
		case opNE:  fValue = (fL != fR) ? 1.0f : 0.0f; break;
		case opLT:  fValue = (fL < fR)  ? 1.0f : 0.0f; break;
		case opLE:  fValue = (fL <= fR) ? 1.0f : 0.0f; break;
		case opGT:  fValue = (fL > fR)  ? 1.0f : 0.0f; break;
		case opGE:  fValue = (fL >= fR) ? 1.0f : 0.0f; break;
		case opAdd: fValue = fL + fR; break;
		case opSub: fValue = fL - fR; break;
		case opMul: fValue = fL * fR; break;
		case opDiv: fValue = fL / fR; break;

		case opAnd:
#ifdef _DEBUG
			if (modf(fL, &intptr) != 0 || modf(fR, &intptr) != 0)
			{
				Error(_T("type mismatch"));
				return false;
			}
#endif
			fValue = (float)((ULONG)fL & (ULONG)fR);
			break;

		case opXor:
#ifdef _DEBUG
			if (modf(fL, &intptr) != 0 || modf(fR, &intptr) != 0)
			{
				Error(_T("type mismatch"));
				return false;
			}
#endif
			fValue = (float)((ULONG)fL ^ (ULONG)fR);
			break;

		case opOr:
#ifdef _DEBUG
			if (modf(fL, &intptr) != 0 || modf(fR, &intptr) != 0)
			{
				Error(_T("type mismatch"));
				return false;
			}
#endif
			fValue = (float)((ULONG)fL | (ULONG)fR);
			break;

		case opSHL: fValue = (float)((ULONG)fL << (ULONG)fR); break;
		case opSHR: fValue = (float)((ULONG)fL >> (ULONG)fR); break;
		}

		Push(fValue);
	}
	else if (op == opAdd)
	{
		// String concatenation
		CStrObject* pStrL = pLeft->ToStr();
		CStrObject* pStrR = pRight->ToStr();
		Push(pStrL->concat(pStrR->GetSz()));
		pStrL->Release();
		pStrR->Release();
	}
	else
	{
		Error(_T("type mismatch error"));
		return false;
	}

	pLeft->Release();
	pRight->Release();
	return true;
}

// =========================================================================
// Section 8: Built-in functions (EnableInput, eval, launch, alert, log)
// =========================================================================

BOOL CRunner::ExecuteBuiltIn(const TCHAR* pchName, int cchName, int nParam, CObject** rgParam, CObject*& pRetObj)
{
	pRetObj = NULL;

	if (cchName == 11 && _tcsncmp(pchName, _T("EnableInput"), cchName) == 0)
	{
		if (nParam == 1)
		{
			CObject* pObj = Dereference(rgParam[0]);
			CNumObject* pNum = pObj->ToNum();
			g_bInputEnable = (bool)(pNum->m_value != 0.0f);
		}
		else
			Error(_T("Invalid parameter"));
		return TRUE;
	}

	if (cchName == 4 && _tcsncmp(pchName, _T("eval"), cchName) == 0)
	{
		if (nParam == 1)
		{
			CObject* pObject = Dereference(rgParam[0]);
			CStrObject* pStr = pObject->ToStr();
			ExecuteScript(m_self, pStr->GetSz());
			pStr->Release();
			pObject->Release();
		}
		else
			Error(_T("Bad launch"));
		return TRUE;
	}

	if (cchName == 6 && _tcsncmp(pchName, _T("launch"), cchName) == 0)
	{
		if (nParam == 1 || nParam == 2)
		{
			CObject* pObject1 = Dereference(rgParam[0]);
			CStrObject* pStr1 = pObject1->ToStr();
			const TCHAR* sz1 = pStr1->GetSz();

			TCHAR sz2[MAX_PATH];
			if (nParam == 2)
			{
				CObject* pObject2 = Dereference(rgParam[1]);
				CStrObject* pStr2 = pObject2->ToStr();
				_tcscpy(sz2, pStr2->GetSz());
				pStr2->Release();
				pObject2->Release();
			}
			else
			{
				_tcscpy(sz2, sz1);
				TCHAR* pch = _tcsrchr(sz2, '/');
				if (pch != NULL) *pch = '\\';
				pch = _tcsrchr(sz2, '\\');
				ASSERT(pch != NULL);
				*pch = 0;
			}

			TRACE(_T("Launch title: %s, %s\n"), sz1, sz2);

#if defined(_XBOX)
			FSCHAR ssz1[MAX_PATH];
			FSCHAR ssz2[MAX_PATH];
			CleanFilePath(ssz1, sz1);
			CleanFilePath(ssz2, sz2);
			XWriteTitleInfoAndReboot(ssz1, ssz2, LDT_NONE, 0, NULL);
#else
			extern void DesktopLaunchTitle(const char*);
			DesktopLaunchTitle(sz2);
#endif
			pStr1->Release();
			pObject1->Release();
		}
		else
			Error(_T("Bad launch"));
		return TRUE;
	}

	if (cchName == 10 && _tcsncmp(pchName, _T("DebugBreak"), cchName) == 0)
	{
#ifdef _DEBUG
		_CrtDbgBreak();
#endif
		return TRUE;
	}

	if (cchName == 5 && _tcsncmp(pchName, _T("alert"), cchName) == 0)
	{
		if (nParam == 1)
		{
			CObject* pObject = Dereference(rgParam[0]);
			CStrObject* pStr = pObject->ToStr();
			Alert(_T("%s"), pStr->GetSz());
			pStr->Release();
			pObject->Release();
		}
		else
			Error(_T("Bad alert"));
		return TRUE;
	}

	if (cchName == 3 && _tcsncmp(pchName, _T("log"), cchName) == 0)
	{
		if (nParam == 1)
		{
			CObject* pObject = Dereference(rgParam[0]);
			CStrObject* pStr = pObject->ToStr();
			TRACE(_T("%s\n"), pStr->GetSz());
			pStr->Release();
			pObject->Release();
		}
		else
			Error(_T("Bad log!"));
		return TRUE;
	}

	return FALSE;
}

// =========================================================================
// Section 9: Function lookup. Resolves by name through scene hierarchy.
// =========================================================================

BYTE* CRunner::LookupFunction(const TCHAR* pchName, int cchName, CObject*& pOwner)
{
	pOwner = m_self;

	// Check self first
	{
		CFunction* pFunction = m_self->FindMemberFunction(pchName, cchName);
		if (pFunction != NULL)
			return pFunction->m_rgop;
	}

	// Walk parent chain
	{
		for (CObject* pObject = m_self->m_parent; pObject != NULL; pObject = pObject->m_parent)
		{
			CObject* pObj = LookupMember(pObject, pchName, cchName);
			if (pObj != NULL)
			{
				pOwner = pObject;
				if (pObj->m_obj == objFunction)
					return ((CFunction*)pObj)->m_rgop;

				ASSERT(FALSE);
				return (BYTE*)pObj;
			}
		}
	}

	pOwner = NULL;
	return NULL;
}

CObject* CRunner::LookupVariable(const TCHAR* pchName, int cchName)
{
	CObject* pObject = LookupMember(m_self, pchName, cchName);
	if (pObject != NULL || m_self == m_thisRef)
		return pObject;
	return LookupMember(m_thisRef, pchName, cchName);
}

#ifdef _DEBUG
void CRunner::DumpStack()
{
	TRACE(_T("\nStack Trace\n{\n"));

	TRACE(_T("ops: 0x%08x\n"), m_ops);
	TRACE(_T("nop: %d\n"), m_nop);
	TRACE(_T("spFrame: %d\n"), m_spFrame);
	TRACE(_T("sp: %d\n"), m_sp);
	TRACE(_T("self:\n")); m_self->Dump();
	TRACE(_T("this:\n")); m_thisRef->Dump();

	for (RUNCONTEXT* pContext = m_nextContext; pContext != NULL; pContext = pContext->m_nextContext)
	{
		TRACE(_T("{\nContext: 0x%08x\n"), pContext);

		TRACE(_T("ops: 0x%08x\n"), pContext->m_ops);
		TRACE(_T("nop: %d\n"), pContext->m_nop);
		TRACE(_T("spFrame: %d\n"), pContext->m_spFrame);
		TRACE(_T("sp: %d\n"), pContext->m_sp);
		TRACE(_T("self:\n")); pContext->m_self->Dump();
		TRACE(_T("this:\n")); pContext->m_thisRef->Dump();

		TRACE(_T("}\n"));
	}

	TRACE(_T("}\n"));
}
#endif
