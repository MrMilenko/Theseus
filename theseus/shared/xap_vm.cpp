// xap_vm.cpp: the XAP script runner and the value objects it pushes around.
//
// One byte per opcode, operands inline, a fixed operand stack and a linked
// list of saved frames for nested calls. Everything on the stack is a
// ref-counted CObject. Bodies follow the 5960 retail XBE's runner code
// (0x36f70 to 0x39b90); docs/decomp/VM.md has the behavior in more detail.
#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "camera.h"
#include "xap_compile.h"

//#define LOCALTRACE TRACE
#define LOCALTRACE 1 ? (void)0 : ::Trace

// Shared runner state. 5960 keeps the same four at 0x17aea0..0x17aea8.
CObject* g_pThis;
CRunner* g_pRunner;
CObject** g_rgParam;
int g_nParam;

extern bool g_bInputEnable;
extern CCamera theCamera;

CObject* LookupMember(CObject* pThis, const TCHAR* pchName, int cchName);

// =========================================================================
// Numbers and vectors
// =========================================================================

// CNumObject constructors   [type 2, vtable 0x22c50]
// Reconstructed from 5960 retail XBE @ 0x00038fa0 (float form; the empty form
// only shows up inlined). Decompile:
//     m_nRefCount = 1; m_obj = 2; m_value = xmm0;
// The string form has no standalone copy in the binary; CStrObject::ToNum
// @ 0x32200 does the same wcstod inline. Kept for the header contract.
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

// CNumObject::ToStr
// Reconstructed from 5960 retail XBE @ 0x00038fd0. Decompile:
//     n = swprintf(buf20, L"%f", (double)m_value); p = buf + n - 1;
//     while (*p == '0') p--;  if (*p == '.') p--;  p[1] = 0;
//     return new CStrObject(buf);
// So 2.500000 prints as "2.5" and 3.000000 as "3". The binary's 20 characters
// overflow from 1e12 up, so Theseus makes room for the biggest float.
CStrObject* CNumObject::ToStr()
{
	TCHAR szNum[64];
	int cch = _stprintf(szNum, _T("%f"), m_value);

	TCHAR* pchLast = &szNum[cch - 1];
	while (*pchLast == '0')
		pchLast--;
	if (*pchLast == '.')
		pchLast--;
	pchLast[1] = '\0';

	return new CStrObject(szNum);
}

// Script-side Vector3. 5960 registers it as node class "Vector3" with x/y/z
// number props (factory @ 0x39070, prop map @ 0x36fa0).
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

// CVec3Object constructors   [type 16, vtable 0x22c88]
// Reconstructed from 5960 retail XBE @ 0x00039070 (empty, via the factory)
// and @ 0x000390c0 (copy). Decompile:
//     m_obj = 0x10; m_x = v[0]; m_y = v[1]; m_z = v[2];     (or all 0)
CVec3Object::CVec3Object()
{
	m_obj = objVec3;
	m_x = 0.0f;
	m_y = 0.0f;
	m_z = 0.0f;
}

CVec3Object::CVec3Object(const D3DXVECTOR3& v)
{
	m_obj = objVec3;
	m_x = v.x;
	m_y = v.y;
	m_z = v.z;
}

// CVec3Object::ToStr
// Reconstructed from 5960 retail XBE @ 0x000390f0. Decompile:
//     swprintf(buf60, L"%f %f %f", x, y, z); return new CStrObject(buf);
// No zero trimming here, unlike numbers. Theseus sizes it for three huge floats.
CStrObject* CVec3Object::ToStr()
{
	TCHAR szVec[160];
	_stprintf(szVec, _T("%f %f %f"), m_x, m_y, m_z);
	return new CStrObject(szVec);
}

// =========================================================================
// Locals and late-bound names
// =========================================================================

// A stack slot for a script local. opLocal creates one the first time a
// slot is touched; it holds one reference to whatever the local points at.
class CLocalVariable : public CObject
{
public:
	CLocalVariable();
	~CLocalVariable();
	CObject* Deref();
	void Assign(CObject* pObject);
	CObject* m_pValue;
};

// CLocalVariable   [vtable 0x22e48]
// Reconstructed from 5960 retail XBE @ 0x00037300 (ctor), 0x37340 (dtor),
// 0x373a0 (Assign), 0x373d0 (Deref). Decompile:
//     ctor:   m_value = NULL;
//     dtor:   if (m_value) m_value->Release();
//     Assign: if (m_value) m_value->Release(); m_value = p; if (p) p->AddRef();
//     Deref:  v = m_value; if (v) v->AddRef(); this->Release(); return v;
// Deref trades the reference to the slot for one to its value.
CLocalVariable::CLocalVariable()
{
	m_pValue = NULL;
}

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
	if (pObject != NULL)
		pObject->AddRef();
}

CObject* CLocalVariable::Deref()
{
	CObject* pValue = m_pValue;
	if (pValue != NULL)
		pValue->AddRef();
	Release();
	return pValue;
}

// CVarObject constructor / destructor   [type 4, vtable 0x22cf8]
// Reconstructed from 5960 retail XBE @ 0x00039400 and @ 0x000394c0. Decompile:
//     m_length = cch; m_capacity = cch + 1; m_text = new WCHAR[cch + 1];
//     memcpy(m_text, pch, cch * 2); m_text[cch] = 0;
//     dtor: free(m_text);
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

// CVarObject::Deref
// Reconstructed from 5960 retail XBE @ 0x000394e0. Decompile:
//     if (!wcscmp(m_text, L"Math"))        obj = &g_Math;      (0x17fac8)
//     else if (!wcscmp(m_text, L"camera")) obj = &theCamera;   (0x187fb0)
//     else { obj = LookupMember(runner->m_self, m_text, len);
//            if (!obj && runner->m_self != runner->m_thisRef)
//                obj = LookupMember(runner->m_thisRef, m_text, len);
//            if (!obj) { Error(L"Unknown object: %s", m_text); return 0; } }
//     obj->AddRef(); r = obj->Deref(); this->Release(); return r;
// The self-then-this lookup is CRunner::LookupVariable, inlined there.
CObject* CVarObject::Deref()
{
	CObject* pFound;

	if (_tcscmp(m_text, _T("Math")) == 0)
	{
		extern CMathClass g_Math;
		pFound = &g_Math;
	}
	else if (_tcscmp(m_text, _T("camera")) == 0)
	{
		pFound = &theCamera;
	}
	else
	{
		pFound = g_pRunner->LookupVariable(m_text, _tcslen(m_text));
		if (pFound == NULL)
		{
			g_pRunner->Error(_T("Unknown object: %s"), m_text);
			return NULL;
		}
	}

	pFound->AddRef();
	CObject* pValue = pFound->Deref();
	Release();
	return pValue;
}

// CVarObject::Assign
// Reconstructed from 5960 retail XBE @ 0x000395b0. Decompile:
//     obj = (self lookup, then this lookup, as in Deref);
//     if (!obj) { Error(L"Unknown variable: %s", m_text); return; }
//     obj->Assign(value);
void CVarObject::Assign(CObject* pNewObject)
{
	CObject* pTarget = g_pRunner->LookupVariable(m_text, _tcslen(m_text));
	if (pTarget == NULL)
	{
		g_pRunner->Error(_T("Unknown variable: %s"), m_text);
		return;
	}
	pTarget->Assign(pNewObject);
}

// =========================================================================
// Class members, bound methods, native function refs
// =========================================================================

// CMemberVarObject   [type 10, vtable 0x22da0]
// Reconstructed from 5960 retail XBE @ 0x00039640 (ctor), 0x396c0 (dtor),
// 0x39720 (Deref), 0x39760 (Assign). Decompile:
//     ctor:   m_owner = inst; m_memberIndex = i; inst->AddRef();
//     dtor:   m_owner->Release();
//     Deref:  v = m_owner->m_vars[i] (NULL if i out of range);
//             if (v) v->AddRef(); this->Release(); return v;
//     Assign: old = m_owner->m_vars[i]; if (old) old->Release();
//             m_owner->m_vars[i] = p (if in range); if (p) p->AddRef();
// The range checks are CNodeArray::GetNode/SetNode. Theseus adds the ASSERTs.
CMemberVarObject::CMemberVarObject(CInstance* pObject, int nMember)
{
	ASSERT(pObject != NULL);
	ASSERT(nMember >= 0 && nMember < pObject->m_vars.GetLength());

	m_obj = objMemberVar;
	m_owner = pObject;
	m_memberIndex = nMember;
	pObject->AddRef();
}

CMemberVarObject::~CMemberVarObject()
{
	ASSERT(m_owner != NULL);
	m_owner->Release();
}

CObject* CMemberVarObject::Deref()
{
	ASSERT(m_memberIndex >= 0 && m_memberIndex < m_owner->m_vars.GetLength());

	CObject* pValue = m_owner->m_vars.GetNode(m_memberIndex);
	if (pValue != NULL)
		pValue->AddRef();
	Release();
	return pValue;
}

void CMemberVarObject::Assign(CObject* pNewObject)
{
	ASSERT(m_memberIndex >= 0 && m_memberIndex < m_owner->m_vars.GetLength());

	CObject* pOld = m_owner->m_vars.GetNode(m_memberIndex);
	if (pOld != NULL)
		pOld->Release();
	m_owner->m_vars.SetNode(m_memberIndex, (CNode*)pNewObject);
	if (pNewObject != NULL)
		pNewObject->AddRef();
}

// CMemberFunctionObject   [type 14, vtable 0x22d68]
// Reconstructed from 5960 retail XBE @ 0x000397b0 (ctor) and @ 0x00039810
// (Deref). Decompile:
//     ctor:  m_owner = owner; m_function = fn;            (no AddRef)
//     Deref: CRunner r(m_owner); r.ResetFunc(); r.m_ops = m_function->m_rgop;
//            ret = r.Run(); this->Release(); return ret;
// Reading a method without calling it runs it on a fresh runner. The owner
// is a plain CObject in Theseus so non-instance nodes can hand these out.
CMemberFunctionObject::CMemberFunctionObject(CObject* pOwner, CFunction* pFunction)
{
	m_obj = objMemberFunction;
	m_owner = pOwner;
	m_function = pFunction;
}

CMemberFunctionObject::~CMemberFunctionObject()
{
}

CObject* CMemberFunctionObject::Deref()
{
	CRunner runner(m_owner);
	runner.SetFunc(m_function);
	CObject* pResult = runner.Run();
	Release();
	return pResult;
}

// CFunctionObject   [type 8, vtable 0x22cc0]
// Reconstructed from 5960 retail XBE @ 0x00039920 (ctor), 0x39960 (dtor),
// 0x399c0 (Call). Decompile:
//     ctor: m_obj = 8; m_owner = NULL;
//     dtor: if (m_owner) m_owner->Release();
//     Call: return m_owner->Call(m_functionIndex, rgparam, nParam);
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

// CFunction   [type 13, vtable 0x22c18]
// Reconstructed from 5960 retail XBE: the constructor is inlined in
// CCompiler::CreateFunction @ 0x34670 (m_obj = 0xd, m_codeSize = 0) and the
// destructor @ 0x39b00 does nothing of its own. m_name is Theseus's debug
// name for the function, so Theseus frees it here.
CFunction::CFunction()
{
	m_obj = objFunction;
	m_codeSize = 0;
	m_name = NULL;
}

CFunction::~CFunction()
{
	delete[] m_name;
}

// CMember   [type 9, vtable 0x22dd8]
// Reconstructed from 5960 retail XBE: ctor inlined in
// CClassCompiler::ParseMemberVar @ 0x36ad0 (m_obj = 9, m_memberIndex = -1),
// dtor @ 0x39b30, Deref @ 0x39b40, Assign @ 0x39b90. Decompile:
//     Deref:  v = g_pThis->m_vars[i]; if (v) v->AddRef(); Release(); return v;
//     Assign: old = g_pThis->m_vars[i]; if (old) old->Release();
//             g_pThis->m_vars[i] = p; if (p) p->AddRef();
// Same as CMemberVarObject but the instance is whatever g_pThis is right
// now (LookupMember sets it). Theseus adds the ASSERTs.
CMember::CMember()
{
	m_obj = objMember;
	m_memberIndex = -1;
}

CMember::~CMember()
{
}

CObject* CMember::Deref()
{
	ASSERT(g_pThis != NULL);
	ASSERT(g_pThis->m_obj == objInstance);

	CInstance* pInstance = (CInstance*)g_pThis;
	ASSERT(m_memberIndex >= 0 && m_memberIndex < pInstance->m_vars.GetLength());

	CObject* pValue = pInstance->m_vars.GetNode(m_memberIndex);
	if (pValue != NULL)
		pValue->AddRef();
	Release();
	return pValue;
}

void CMember::Assign(CObject* pNewValue)
{
	ASSERT(g_pThis != NULL);
	ASSERT(g_pThis->m_obj == objInstance);

	CInstance* pInstance = (CInstance*)g_pThis;
	ASSERT(m_memberIndex >= 0 && m_memberIndex < pInstance->m_vars.GetLength());

	CObject* pOld = pInstance->m_vars.GetNode(m_memberIndex);
	if (pOld != NULL)
		pOld->Release();
	pInstance->m_vars.SetNode(m_memberIndex, (CNode*)pNewValue);
	if (pNewValue != NULL)
		pNewValue->AddRef();
}

// CObject::Assign
// Reconstructed from 5960 retail XBE @ 0x00039900 (shared by CObject and
// CNode). Decompile:
//     Error(L"Cannot assign to objects");
void CObject::Assign(CObject* pObject)
{
	g_pRunner->Error(_T("Cannot assign to objects"));
}

// =========================================================================
// Node arrays as script arrays
// =========================================================================

// CNodeArrayObject constructor   [type 7, vtable 0x22d30]
// Reconstructed from 5960 retail XBE: inlined in CProperty::Deref @ 0x38b00.
//     m_obj = 7; m_nodeArray = (CNodeArray*)(node + prop offset);
CNodeArrayObject::CNodeArrayObject(CNodeArray* pNodeArray)
{
	m_obj = objNodeArray;
	m_nodeArray = pNodeArray;
}

// CNodeArrayObject::Call
// Reconstructed from 5960 retail XBE @ 0x000391a0. Decompile:
//     0: return new CNumObject((float)m_nodeArray->m_length);
//     1: p = rgparam[0]->Deref(); p->AddRef(); m_nodeArray->AddNode(p);
//     2: p = rgparam[0]->Deref(); m_nodeArray->RemoveNode(p); p->Release();
//     return NULL;
// add keeps both the Deref reference and an extra one, so the array owns one.
CObject* CNodeArrayObject::Call(int nFunction, CObject** rgparam, int nParam)
{
	if (nFunction == 0)
		return new CNumObject((float)m_nodeArray->GetLength());

	if (nFunction == 1)
	{
		CObject* pAdd = rgparam[0]->Deref();
		pAdd->AddRef();
		m_nodeArray->AddNode((CNode*)pAdd);
	}
	else if (nFunction == 2)
	{
		CObject* pRemove = rgparam[0]->Deref();
		m_nodeArray->RemoveNode((CNode*)pRemove);
		pRemove->Release();
	}

	return NULL;
}

// CNodeArrayObject::Dot
// Reconstructed from 5960 retail XBE @ 0x00039260. Decompile:
//     if (key->m_obj != objVariable) {
//         i = (int)key->m_value;
//         if (i < 0 || i >= m_nodeArray->m_length)
//             { Error(L"Array reference out of bounds"); return 0; }
//         n = m_nodeArray->m_rgpNode[i]; n->AddRef(); return n; }
//     "length"(6) / "add"(3) / "remove"(6) -> new CFunctionObject
//         { m_functionIndex = 0 / 1 / 2, m_owner = this }; this->AddRef();
//     else return CObject::Dot(key);
CObject* CNodeArrayObject::Dot(CObject* pObj)
{
	if (pObj->m_obj != objVariable)
	{
		int iItem = (int)((CNumObject*)pObj)->m_value;
		if (iItem < 0 || iItem >= m_nodeArray->GetLength())
		{
			g_pRunner->Error(_T("Array reference out of bounds"));
			return NULL;
		}

		CNode* pItem = m_nodeArray->GetNode(iItem);
		pItem->AddRef();
		return pItem;
	}

	CVarObject* pName = (CVarObject*)pObj;
	int nFunction = -1;
	if (pName->m_length == 6 && _tcsncmp(pName->m_text, _T("length"), 6) == 0)
		nFunction = 0;
	else if (pName->m_length == 3 && _tcsncmp(pName->m_text, _T("add"), 3) == 0)
		nFunction = 1;
	else if (pName->m_length == 6 && _tcsncmp(pName->m_text, _T("remove"), 6) == 0)
		nFunction = 2;

	if (nFunction < 0)
		return CObject::Dot(pObj);

	CFunctionObject* pRef = new CFunctionObject;
	pRef->m_functionIndex = nFunction;
	pRef->m_owner = this;
	AddRef();
	return pRef;
}

// =========================================================================
// Node properties seen from script
// =========================================================================

// CProperty constructor / destructor   [vtable 0x22e10]
// Reconstructed from 5960 retail XBE @ 0x00038a20 and @ 0x00038aa0. Decompile:
//     ctor: m_node = node; m_pprd = prd; node->AddRef();
//     dtor: m_node->Release();
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

// CProperty::Deref
// Reconstructed from 5960 retail XBE @ 0x00038b00. Reads the field at
// m_node + prd offset and wraps it by type: integer / boolean / number become
// a CNumObject, vec3 a CVec3Object, node is returned AddRef'd as is, string
// becomes a CStrObject, children and nodearray become a CNodeArrayObject over
// the field itself. Any other type: Error(L"Unknown property type"). The
// property reference is released either way. Theseus reads the field before
// the release so the value can't go away under it.
CObject* CProperty::Deref()
{
	void* pvField = (BYTE*)m_node + (int)m_pprd->pbOffset;
	CObject* pValue;

	switch (m_pprd->nType)
	{
	case pt_integer:
		pValue = new CNumObject((float)*(int*)pvField);
		break;

	case pt_boolean:
		pValue = new CNumObject((float)*(bool*)pvField);
		break;

	case pt_number:
		pValue = new CNumObject(*(float*)pvField);
		break;

	case pt_children:
	case pt_nodearray:
		pValue = new CNodeArrayObject((CNodeArray*)pvField);
		break;

	case pt_vec3:
		pValue = new CVec3Object(*(D3DXVECTOR3*)pvField);
		break;

	case pt_node:
		pValue = *(CNode**)pvField;
		if (pValue != NULL)
			pValue->AddRef();
		break;

	case pt_string:
		pValue = new CStrObject(*(TCHAR**)pvField);
		break;

	default:
		g_pRunner->Error(_T("Unknown property type"));
		return NULL;
	}

	Release();
	return pValue;
}

// CProperty::Assign
// Reconstructed from 5960 retail XBE @ 0x00038d70. Decompile:
//     if (!value && type != pt_node) { Error(L"Illegal null assignement"); return; }
//     integer: n = value->Deref()->ToNum(); SetProperty(prd, &(int)n->m_value);
//     boolean: b = n->m_value != 0;         SetProperty(prd, &b);
//     number:  SetProperty(prd, &n->m_value);              (then n->Release())
//     vec3:    v = value->Deref(); if (v->m_obj == objVec3) SetProperty(prd, &v->m_x);
//     node:    p = value ? value->Deref() : NULL; SetProperty(prd, &p);
//     string:  s = value->Deref()->ToStr(); SetProperty(prd, &s->m_text); s->Release();
// "assignement" is the binary's spelling. Theseus passes the byte count to
// SetProperty and traces when a vec3 gets something else.
void CProperty::Assign(CObject* pObject)
{
	if (pObject == NULL && m_pprd->nType != pt_node)
	{
		g_pRunner->Error(_T("Illegal null assignement"));
		return;
	}

	switch (m_pprd->nType)
	{
	case pt_integer:
		{
			CNumObject* pNum = pObject->Deref()->ToNum();
			int nValue = (int)pNum->m_value;
			m_node->SetProperty(m_pprd, &nValue, sizeof(int));
			pNum->Release();
		}
		break;

	case pt_boolean:
		{
			CNumObject* pNum = pObject->Deref()->ToNum();
			bool bValue = pNum->m_value != 0.0f;
			m_node->SetProperty(m_pprd, &bValue, sizeof(bool));
			pNum->Release();
		}
		break;

	case pt_number:
		{
			CNumObject* pNum = pObject->Deref()->ToNum();
			m_node->SetProperty(m_pprd, &pNum->m_value, sizeof(float));
			pNum->Release();
		}
		break;

	case pt_vec3:
		{
			CVec3Object* pVec = (CVec3Object*)pObject->Deref();
			if (pVec->m_obj == objVec3)
				m_node->SetProperty(m_pprd, &pVec->m_x, sizeof(float) * 3);
			else
				TRACE(_T("\001Expected a Vector3 object!\n"));
		}
		break;

	case pt_node:
		{
			CNode* pNode = (pObject != NULL) ? (CNode*)pObject->Deref() : NULL;
			m_node->SetProperty(m_pprd, &pNode, sizeof(CNode*));
		}
		break;

	case pt_string:
		{
			CStrObject* pStr = pObject->Deref()->ToStr();
			const TCHAR* szValue = pStr->GetSz();
			m_node->SetProperty(m_pprd, &szValue, pStr->GetLength());
			pStr->Release();
		}
		break;
	}
}

// CNode::Dot / CNode::ToStr   [CNode vtable 0x219f8 slots 12 and 8]
// Reconstructed from 5960 retail XBE @ 0x00038f30 and @ 0x00038f40. Decompile:
//     Dot:   return CObject::Dot(key);
//     ToStr: return new CStrObject(GetNodeClass()->m_className);
CObject* CNode::Dot(CObject* pObj)
{
	return CObject::Dot(pObj);
}

CStrObject* CNode::ToStr()
{
	return new CStrObject(GetNodeClass()->m_className);
}

// =========================================================================
// Free helpers
// =========================================================================

// Dereference
// Reconstructed from 5960 retail XBE @ 0x000372e0. Decompile:
//     while (p) { q = p->Deref(); if (q == p) break; p = q; } return p;
// Keeps following references until something answers with itself.
CObject* Dereference(CObject* pObject)
{
	while (pObject != NULL)
	{
		CObject* pNext = pObject->Deref();
		if (pNext == pObject)
			break;
		pObject = pNext;
	}
	return pObject;
}

// LookupClass
// Reconstructed from 5960 retail XBE: inlined at the top of CreateNewObject
// @ 0x37580 as g_classes->Lookup(name) (0x32ca0) plus an objClass check.
CClass* LookupClass(const TCHAR* pchName, int cchName)
{
	extern CNameSpace* g_classes;
	CObject* pFound = g_classes->Lookup(pchName, cchName);
	if (pFound != NULL && pFound->m_obj == objClass)
		return (CClass*)pFound;
	return NULL;
}

// CreateNewObject
// Reconstructed from 5960 retail XBE @ 0x00037580. Decompile:
//     g_rgParam = rgparam; g_nParam = nParam;
//     c = g_classes->Lookup(name); if (c && c->m_obj == objClass) return c->CreateNode();
//     nc = CNodeClass::FindByName(name); if (nc && (o = nc->CreateNode())) return o;
//     return 0;
// The constructor args ride in g_rgParam / g_nParam for Date, Array and friends.
CObject* CreateNewObject(const TCHAR* pchClassName, int cchClassName, CObject** rgparam, int nParam)
{
	g_rgParam = rgparam;
	g_nParam = nParam;

	CClass* pScripted = LookupClass(pchClassName, cchClassName);
	if (pScripted != NULL)
		return pScripted->CreateNode();

	return NewNode(pchClassName, cchClassName);
}

// ExecuteScript
// Reconstructed from 5960 retail XBE @ 0x00036fb0. Decompile:
//     CFunctionCompiler c; c.ParseBlock(sz); if (c.m_bError) return false;
//     c.Write(opRet); f = c.CreateFunction();
//     CRunner r(obj); r.ResetFunc(); r.m_ops = f->m_rgop; r.Run();
//     delete f; return true;
bool ExecuteScript(CObject* pObject, const TCHAR* szScript)
{
	CFunctionCompiler compiler;
	compiler.ParseBlock(szScript);
	if (compiler.HadError())
		return false;

	compiler.Write(opRet);
	CFunction* pScript = compiler.CreateFunction();

	CRunner runner(pObject);
	runner.SetFunc(pScript);
	runner.Run();

	delete pScript;
	return true;
}

// CallFunction
// Reconstructed from 5960 retail XBE @ 0x000371a0. Decompile:
//     g_pThis = obj; CRunner r(obj); r.ResetFunc();
//     if (!(r.m_ops = r.LookupFunction(name, wcslen(name), r.m_self))) return false;
//     for (i = 0; i < n; i++) r.m_stack[r.m_sp++] = rgParam[i];
//     r.Run(); return true;
// The arguments go on the stack as is; the caller's references move with them.
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

// FindMember   [no binary symbol; LookupMember @ 0x399e0 does this inline]
//     for (c = cls; c; c = c->m_baseClass) if ((m = c->GetMember(name))) return m;
// node_system.cpp uses it to find member functions too.
CObject* FindMember(CNodeClass* pClass, const TCHAR* pchName, int cchName)
{
	while (pClass != NULL)
	{
		CObject* pMember = pClass->GetMember(pchName, cchName);
		if (pMember != NULL)
			return pMember;
		pClass = pClass->m_baseClass;
	}
	return NULL;
}

// LookupMember
// Reconstructed from 5960 retail XBE @ 0x000399e0. Decompile:
//     g_pThis = obj;
//     if (obj->m_obj == objNode && (prd = obj->FindProp(name)))
//         { p = new CProperty(obj, prd); p->m_nRefCount = 0; return p; }
//     if ((m = obj->GetMember(name))) return m;
//     for (c = obj->GetNodeClass(); c; c = c->m_baseClass)
//         if ((m = c->GetMember(name))) return m;
//     for (p = obj->m_parent; p; p = p->m_parent)
//         if ((m = LookupMember(p, name))) return m;
//     return 0;
// Order is: node property, own members, class chain, then each ancestor.
// The CProperty starts at zero refs; the caller's AddRef is the first one.
CObject* LookupMember(CObject* pThis, const TCHAR* pchName, int cchName)
{
	ASSERT(pThis != NULL);

	g_pThis = pThis;

	if (pThis->m_obj == objNode)
	{
		CNode* pNode = (CNode*)pThis;
		const PRD* pprd = pNode->FindProp(pchName, cchName);
		if (pprd != NULL)
		{
			CObject* pProp = new CProperty(pNode, pprd);
			pProp->m_nRefCount = 0;
			return pProp;
		}
	}

	CObject* pFound = pThis->GetMember(pchName, cchName);
	if (pFound != NULL)
		return pFound;

	pFound = FindMember(pThis->GetNodeClass(), pchName, cchName);
	if (pFound != NULL)
		return pFound;

	for (CObject* pAncestor = pThis->m_parent; pAncestor != NULL; pAncestor = pAncestor->m_parent)
	{
		pFound = LookupMember(pAncestor, pchName, cchName);
		if (pFound != NULL)
			return pFound;
	}

	return NULL;
}

// =========================================================================
// CRunner setup and the operand stack
// =========================================================================

// CRunner::CRunner / ~CRunner
// Reconstructed from 5960 retail XBE: the runner is built on the stack in
// CallFunction @ 0x371a0, ExecuteScript @ 0x36fb0 and
// CMemberFunctionObject::Deref @ 0x39810, always the same way:
//     m_sp = m_nop = 0; m_ops = NULL; m_self = m_thisRef = obj;
//     m_bError = false; m_nextContext = NULL; zero the stack;
// and torn down with ResetFunc. 5960's stack holds 100 entries; Theseus's
// holds 256 (see runner.h). Theseus also starts the line at 0 so an error
// before the first statement doesn't print a junk line number.
CRunner::CRunner(CObject* pObject)
{
	ASSERT(pObject != NULL);

	m_nLine = 0;
	m_wakeup = 0.0f;
	m_nop = 0;
	m_ops = NULL;
	m_sp = 0;
	m_spFrame = 0;
	m_spBase = 0;
	m_self = pObject;
	m_thisRef = pObject;
	m_bError = false;
	m_nextContext = NULL;
	ZeroMemory(m_stack, sizeof(m_stack));
}

CRunner::~CRunner()
{
	ResetFunc();
}

// CRunner::SetFunc   [inlined at each runner site in 5960]
// By name: ResetFunc, then m_ops = LookupFunction(name) (see CallFunction).
// By function: ResetFunc, then m_ops = fn->m_rgop with nop/sp/frame at 0.
// Theseus also clears the line number and the sleep deadline here.
bool CRunner::SetFunc(const TCHAR* szFunc)
{
	ResetFunc();
	m_ops = LookupFunction(szFunc, _tcslen(szFunc), m_thisRef);
	return m_ops != NULL;
}

void CRunner::SetFunc(CFunction* pFunction)
{
	ResetFunc();
	m_ops = pFunction->m_rgop;
	m_nop = 0;
	m_sp = 0;
	m_spFrame = 0;
	m_nLine = 0;
	m_wakeup = 0.0f;
}

// CRunner::ResetFunc
// Reconstructed from 5960 retail XBE @ 0x00037400. Decompile:
//     while (m_sp) { p = m_stack[--m_sp]; m_stack[m_sp] = 0; if (p) p->Release(); }
//     m_bError = 0; m_nop = 0; m_wakeup = 0.0; m_sp = 0; m_spFrame = 0;
void CRunner::ResetFunc()
{
	while (m_sp != 0)
	{
		m_sp--;
		CObject* pSlot = m_stack[m_sp];
		m_stack[m_sp] = NULL;
		if (pSlot != NULL)
			pSlot->Release();
	}

	m_bError = false;
	m_nop = 0;
	m_wakeup = 0.0f;
	m_sp = 0;
	m_spFrame = 0;
}

// CRunner::Push family
// Reconstructed from 5960 retail XBE @ 0x00037450 / 0x374b0 (numbers),
// 0x37510 (string). Decompile:
//     m_stack[m_sp++] = new CNumObject(n);
//     if (cch == -1) cch = wcslen(sz); m_stack[m_sp++] = new CStrObject(sz, cch);
// The object form is inlined everywhere as m_stack[m_sp++] = p. Theseus
// refuses to run off the end of the stack.
void CRunner::Push(float nValue)
{
	Push(new CNumObject(nValue));
}

void CRunner::Push(int nValue)
{
	Push(new CNumObject((float)nValue));
}

void CRunner::Push(const TCHAR* szValue, int nLen)
{
	if (nLen == -1)
		nLen = _tcslen(szValue);
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

// CRunner::Pop   [inlined in 5960 as p = m_stack[--m_sp]; m_stack[m_sp] = 0]
CObject* CRunner::Pop()
{
	ASSERT(m_sp > 0);
	m_sp--;
	CObject* pTop = m_stack[m_sp];
	m_stack[m_sp] = NULL;
	return pTop;
}

// CRunner::Error
// Reconstructed from 5960 retail XBE @ 0x000375e0. Decompile:
//     if (m_bError) return;
//     vswprintf(msg, 512, fmt, args);
//     if (m_nLine == 0) swprintf(out, <no-line form>, msg);
//     else              swprintf(out, <line form>, m_nLine, msg);
//     m_bError = 1;
// Only the first error of a run is reported; Run stops on the flag. Retail
// builds the text and shows it nowhere. Theseus traces it, dumps the stack
// in debug, and pops a message box on desktop.
void CRunner::Error(const TCHAR* szFmt, ...)
{
	if (m_bError)
		return;

	TCHAR szDetail[512];
	va_list args;
	va_start(args, szFmt);
	_vsntprintf(szDetail, countof(szDetail), szFmt, args);
	va_end(args);

	TCHAR szMessage[1024];
	if (m_nLine != 0)
		_stprintf(szMessage, _T("Runtime Error\n\nLine: %d\n\n%s"), m_nLine, szDetail);
	else
		_stprintf(szMessage, _T("Runtime Error\n\n%s"), szDetail);

	Trace(_T("\001\007%s\n"), szMessage);

#ifdef _DEBUG
	DumpStack();
#endif

#ifdef THESEUS_DESKTOP
	extern int TheseusMessageBox(const char* szText, unsigned int uType);
	TheseusMessageBox(szMessage, 0);
#endif

	m_bError = true;
}

// CRunner::PushContext
// Reconstructed from 5960 retail XBE @ 0x00037690. Decompile:
//     c = new RUNCONTEXT;  c->m_ops = m_ops; c->m_nop = m_nop;
//     c->m_spFrame = m_spFrame; c->m_self = m_self; c->m_thisRef = m_thisRef;
//     c->m_sp = m_sp - nParam - 1; c->m_next = m_nextContext; m_nextContext = c;
// The saved sp sits under the callee and its arguments, so returning drops both.
void CRunner::PushContext(UINT nParam)
{
	RUNCONTEXT* pSaved = new RUNCONTEXT;
	pSaved->m_ops = m_ops;
	pSaved->m_nop = m_nop;
	pSaved->m_spFrame = m_spFrame;
	pSaved->m_self = m_self;
	pSaved->m_thisRef = m_thisRef;
	pSaved->m_sp = m_sp - nParam - 1;
	pSaved->m_nextContext = m_nextContext;
	m_nextContext = pSaved;
}

// CRunner::PopContext
// Reconstructed from 5960 retail XBE @ 0x000376e0. Decompile:
//     c = m_nextContext; ret = Dereference(Pop());
//     m_ops/m_nop/m_spFrame/m_self = c->...; g_pThis = m_thisRef = c->m_thisRef;
//     for (i = c->m_sp; i < m_sp; i++) if (m_stack[i]) { m_stack[i]->Release(); m_stack[i] = 0; }
//     m_sp = c->m_sp; m_nextContext = c->m_next; delete c; m_stack[m_sp++] = ret;
void CRunner::PopContext()
{
	RUNCONTEXT* pSaved = m_nextContext;
	ASSERT(pSaved != NULL);

	CObject* pResult = Dereference(Pop());

	m_ops = pSaved->m_ops;
	m_nop = pSaved->m_nop;
	m_spFrame = pSaved->m_spFrame;
	m_self = pSaved->m_self;
	m_thisRef = pSaved->m_thisRef;
	g_pThis = m_thisRef;

	for (UINT iSlot = pSaved->m_sp; iSlot < m_sp; iSlot++)
	{
		if (m_stack[iSlot] != NULL)
		{
			m_stack[iSlot]->Release();
			m_stack[iSlot] = NULL;
		}
	}

	m_sp = pSaved->m_sp;
	m_nextContext = pSaved->m_nextContext;
	delete pSaved;

	Push(pResult);
}

// CRunner::Run
// Reconstructed from 5960 retail XBE @ 0x000377b0. Decompile:
//     m_spBase = m_sp;
//     while (!m_bError) if (!Step(&ret)) break;
// Theseus hands back whatever the outermost return left (NULL on error) and
// checks in debug that nothing above the stack top was left behind.
CObject* CRunner::Run()
{
	m_spBase = m_sp;

	while (!m_bError)
	{
#ifdef _DEBUG
		for (UINT i = 0; i < 20; i++)
			ASSERT(m_stack[m_sp + i] == NULL);
#endif
		CObject* pResult = NULL;
		if (!Step(&pResult))
			return pResult;
	}

	return NULL;
}

// =========================================================================
// One instruction
// =========================================================================

// CRunner::Step
// Reconstructed from 5960 retail XBE @ 0x00037800 (1880 bytes, one switch on
// the opcode byte). Sets g_pRunner, fetches the opcode and runs it; returns
// false to stop (outermost return, error, or an opcode it doesn't know).
// Each case is described next to its code below; the full write-up with the
// decompile excerpts is in docs/decomp/VM.md. Theseus adds the stack size
// check on opFrame and the null check on opCall.
bool CRunner::Step(CObject** ppRetObj)
{
	g_pRunner = this;

	BYTE op = m_ops[m_nop++];
	switch (op)
	{
	// Push null.
	case opNull:
		Push((CObject*)NULL);
		return true;

	// int nParam, string class. Build it with the arguments sitting on the
	// stack, release them, and leave the new object in their place.
	case opNew:
		{
			int nParam = FetchInt();
			int cchClass;
			const TCHAR* pchClass = FetchString(cchClass);
			CObject** rgparam = &m_stack[m_sp - nParam];

			CObject* pNew = CreateNewObject(pchClass, cchClass, rgparam, nParam);
			if (pNew == NULL)
			{
				TCHAR szClass[256];
				if (cchClass > 255)
					cchClass = 255;
				CopyChars(szClass, pchClass, cchClass);
				szClass[cchClass] = '\0';
				Error(_T("Failed to create new object: \"%s\""), szClass);
				return false;
			}

			for (int i = 0; i < nParam; i++)
			{
				if (rgparam[i] != NULL)
				{
					rgparam[i]->Release();
					rgparam[i] = NULL;
				}
			}
			m_sp -= nParam;
			Push(pNew);
		}
		return true;

	// string name. Push an unresolved name; it binds when dereferenced.
	case opVar:
		{
			int cchName;
			const TCHAR* pchName = FetchString(cchName);
			Push(new CVarObject(pchName, cchName));
		}
		return true;

	// uint target.
	case opJump:
		m_nop = FetchUInt();
		return true;

	// uint target. Pop a value; true (nonzero number) falls through, false
	// or null jumps.
	case opCond:
		{
			bool bTrue = false;
			CObject* pTest = Dereference(Pop());
			if (pTest != NULL)
			{
				CNumObject* pNum = pTest->ToNum();
				bTrue = pNum->m_value != 0.0f;
				pNum->Release();
				pTest->Release();
			}

			if (bTrue)
				m_nop += sizeof(UINT);
			else
				m_nop = FetchUInt();
		}
		return true;

	// byte nParam. The callee sits under its arguments. A native function
	// ref runs right here; a bound method or a named script function gets a
	// new frame; a bare name tries the built-ins first, then the script
	// function lookup.
	case opCall:
		{
			int nParam = m_ops[m_nop++];
			ASSERT(m_sp >= (UINT)(nParam + 1));
			CObject** ppCallee = &m_stack[m_sp - nParam - 1];
			CObject** rgparam = &m_stack[m_sp - nParam];
			CObject* pCallee = *ppCallee;

			// Theseus: calling null is an error instead of a crash.
			if (pCallee == NULL)
			{
				Error(_T("call on null"));
				return false;
			}

			if (pCallee->m_obj == objFunctionRef)
			{
				*ppCallee = NULL;
				CObject* pResult = ((CFunctionObject*)pCallee)->Call(rgparam, nParam);
				pCallee->Release();

				for (int i = 0; i < nParam; i++)
				{
					if (rgparam[i] != NULL)
					{
						rgparam[i]->Release();
						rgparam[i] = NULL;
					}
				}
				m_sp -= nParam + 1;
				Push(pResult);
				return true;
			}

			if (pCallee->m_obj == objMemberFunction)
			{
				CMemberFunctionObject* pMethod = (CMemberFunctionObject*)pCallee;
				pMethod->AddRef();
				PushContext(nParam);
				m_spFrame = m_sp - nParam;
				ASSERT(pMethod->m_owner != NULL);
				m_self = pMethod->m_owner;
				m_ops = pMethod->m_function->m_rgop;
				m_nop = 0;
				return true;
			}

			CVarObject* pName = (CVarObject*)pCallee;
			CObject* pResult = NULL;
			if (ExecuteBuiltIn(pName->m_text, pName->m_length, nParam, rgparam, pResult))
			{
				for (int i = 0; i < nParam; i++)
					rgparam[i] = NULL;
				m_sp -= nParam + 1;
				Push(pResult);
				pCallee->Release();
				return true;
			}

			CObject* pOwner;
			BYTE* pbCode = LookupFunction(pName->m_text, pName->m_length, pOwner);
			if (pbCode == NULL)
			{
				TCHAR szName[32];
				int cchName = pName->m_length;
				if (cchName > 31)
					cchName = 31;
				_tcsncpy(szName, pName->m_text, cchName);
				szName[cchName] = '\0';
				Error(_T("unknown function: %s"), szName);
				return false;
			}

			PushContext(nParam);
			m_self = pOwner;
			g_pThis = pOwner;
			m_spFrame = m_sp - nParam;
			m_ops = pbCode;
			m_nop = 0;
		}
		return true;

	// Return. Inside a call, restore the caller and leave the result on its
	// stack. At the outermost level, hand back the top of stack and stop.
	case opRet:
		if (m_nextContext != NULL)
		{
			PopContext();
			return true;
		}
		if (ppRetObj != NULL)
			*ppRetObj = (m_sp != 0) ? Pop() : NULL;
		return false;

	// string literal.
	case opStr:
		{
			int cchText;
			const TCHAR* pchText = FetchString(cchText);
			Push(pchText, cchText);
		}
		return true;

	// float literal.
	case opNum:
		Push(FetchFloat());
		return true;

	case opAdd: case opSub: case opMul: case opDiv: case opMod:
	case opEQ:  case opNE:  case opLT:  case opGT:  case opLE: case opGE:
	case opSHL: case opSHR: case opAnd: case opOr:  case opXor:
		return BinaryOperator(op);

	// a.b pushes a then the name b; a[i] pushes a then i, which gets
	// resolved first. Either way the left side is resolved one step and
	// asked for the member.
	case opDot:
	case opArray:
		{
			CObject* pKey = Pop();
			CObject* pBase = Pop();
			if (pKey == NULL || pBase == NULL)
			{
				Error(_T("illegal null reference"));
				return false;
			}

			if (op == opArray)
				pKey = Dereference(pKey);

			ASSERT(pKey->m_obj == objVariable || pKey->m_obj == objNumber || pKey->m_obj == objString);

			CObject* pTarget = pBase->Deref();
			if (pTarget == NULL)
			{
				Error(_T("illegal null reference"));
				return false;
			}

			CObject* pMember = pTarget->Dot(pKey);
			if (pMember == NULL)
			{
				if (pKey->m_obj == objVariable)
				{
					Error(_T("Unknown member: %s"), ((CVarObject*)pKey)->m_text);
				}
				else
				{
					CStrObject* pKeyText = pKey->ToStr();
					Error(_T("Unknown member: %s"), pKeyText->GetSz());
					pKeyText->Release();
				}
			}

			Push(pMember);
			pTarget->Release();
			pKey->Release();
		}
		return true;

	// Pop the value (resolved) and the target, assign, and leave the value.
	case opAssign:
		{
			CObject* pValue = Dereference(Pop());
			CObject* pTarget = Pop();
			if (pTarget == NULL)
			{
				Error(_T("cannot assign to null"));
				return false;
			}
			pTarget->Assign(pValue);
			pTarget->Release();
			Push(pValue);
		}
		return true;

	// int index. Push the local's slot, creating it on first use.
	case opLocal:
		{
			int iLocal = FetchInt();
			ASSERT(m_spFrame + iLocal < m_sp);

			CObject*& pSlot = m_stack[m_spFrame + iLocal];
			if (pSlot == NULL)
				pSlot = new CLocalVariable;
			pSlot->AddRef();
			Push(pSlot);
		}
		return true;

	// Push the current self.
	case opThis:
		ASSERT(m_self != NULL);
		m_self->AddRef();
		Push(m_self);
		return true;

	// int count. Reserve the locals and mark the new base.
	case opFrame:
		{
			int nLocals = FetchInt();

			// Theseus: a frame that won't fit is an error, not a stack overrun.
			if (nLocals < 0 || m_sp + (UINT)nLocals > countof(m_stack))
			{
				Error(_T("script stack overflow"));
				return false;
			}

#ifdef _DEBUG
			for (UINT i = m_sp; i < m_sp + nLocals; i++)
				ASSERT(m_stack[i] == NULL);
#endif
			m_sp += nLocals;
			m_spBase = m_sp;
		}
		return true;

	// int line. Remember it for error messages.
	case opStatement:
		m_nLine = FetchInt();
		return true;

	// Negate a number.
	case opNeg:
		{
			CObject* pValue = Dereference(Pop());
			if (pValue == NULL || pValue->m_obj != objNumber)
			{
				Error(_T("expected a number"));
				return false;
			}
			// 5960 computes 0.0 - v here, which gives +0 for 0; Theseus keeps -v.
			Push(-((CNumObject*)pValue)->m_value);
			pValue->Release();
		}
		return true;

	// Throw away the top of stack.
	case opDrop:
		{
			CObject* pTop = Pop();
			if (pTop != NULL)
				pTop->Release();
		}
		return true;

	// int count. Release that many locals off the top.
	case opEndFrame:
		{
			int nLocals = FetchInt();
			for ( ; nLocals > 0; nLocals--)
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
		return true;

	// Pop a number of seconds and set the wake time; the caller checks
	// IsSleeping between steps.
	case opSleep:
		{
			CObject* pDelay = Dereference(Pop());
			if (pDelay == NULL)
			{
				Error(_T("illegal null reference"));
				return false;
			}
			if (pDelay->m_obj != objNumber)
			{
				Error(_T("expected a number"));
				return false;
			}
			m_wakeup = TheseusGetNow() + ((CNumObject*)pDelay)->m_value;
			pDelay->Release();
		}
		return true;
	}

	// Lost in space.
	ASSERT(FALSE);
	return false;
}

// =========================================================================
// Two-operand operators
// =========================================================================

// CRunner::BinaryOperator
// Reconstructed from 5960 retail XBE @ 0x00037fe0. Pops right then left,
// both fully resolved, and picks a path:
//   - either is null, or both are nodes: only == and != work, comparing
//     identity; anything else is "illegal null reference".
//   - both strings and not +: wcscmp, then == != < <= > >= push 1 or 0;
//     anything else is "type mismatch".
//   - both numbers: arithmetic, comparisons (1 or 0), and the integer ops
//     & | ^ << >> on the values truncated to integers.
//   - otherwise + concatenates the two ToStr forms, and anything else is
//     "type mismatch error".
// Full write-up in docs/decomp/VM.md. Theseus keeps its own integer
// conversion for the bit ops and adds a whole-number check in debug.
bool CRunner::BinaryOperator(BYTE op)
{
	CObject* pRight = Dereference(Pop());
	CObject* pLeft = Dereference(Pop());

	if (pLeft == NULL || pRight == NULL || (pLeft->m_obj == objNode && pRight->m_obj == objNode))
	{
		if (op == opEQ)
			Push(pLeft == pRight ? 1.0f : 0.0f);
		else if (op == opNE)
			Push(pLeft != pRight ? 1.0f : 0.0f);
		else
		{
			Error(_T("illegal null reference"));
			return false;
		}

		if (pRight != NULL)
			pRight->Release();
		if (pLeft != NULL)
			pLeft->Release();
		return true;
	}

	if (op != opAdd && pLeft->m_obj == objString && pRight->m_obj == objString)
	{
		int nOrder = _tcscmp(((CStrObject*)pLeft)->GetSz(), ((CStrObject*)pRight)->GetSz());
		bool bResult;

		switch (op)
		{
		case opEQ: bResult = (nOrder == 0); break;
		case opNE: bResult = (nOrder != 0); break;
		case opLT: bResult = (nOrder < 0);  break;
		case opGT: bResult = (nOrder > 0);  break;
		case opLE: bResult = (nOrder <= 0); break;
		case opGE: bResult = (nOrder >= 0); break;
		default:
			Error(_T("type mismatch"));
			return false;
		}
		Push(bResult);
	}
	else if (pLeft->m_obj == objNumber && pRight->m_obj == objNumber)
	{
		float a = ((CNumObject*)pLeft)->m_value;
		float b = ((CNumObject*)pRight)->m_value;
		float fResult;

#ifdef _DEBUG
		// Theseus: bit ops on fractions are a type mismatch in debug builds.
		if (op == opAnd || op == opOr || op == opXor)
		{
			double whole;
			if (modf(a, &whole) != 0 || modf(b, &whole) != 0)
			{
				Error(_T("type mismatch"));
				return false;
			}
		}
#endif

		switch (op)
		{
		case opAdd: fResult = a + b; break;
		case opSub: fResult = a - b; break;
		case opMul: fResult = a * b; break;
		case opDiv: fResult = a / b; break;

		case opEQ: fResult = (a == b) ? 1.0f : 0.0f; break;
		case opNE: fResult = (a != b) ? 1.0f : 0.0f; break;
		case opLT: fResult = (a <  b) ? 1.0f : 0.0f; break;
		case opGT: fResult = (a >  b) ? 1.0f : 0.0f; break;
		case opLE: fResult = (a <= b) ? 1.0f : 0.0f; break;
		case opGE: fResult = (a >= b) ? 1.0f : 0.0f; break;

		case opAnd: fResult = (float)((ULONG)a & (ULONG)b); break;
		case opOr:  fResult = (float)((ULONG)a | (ULONG)b); break;
		case opXor: fResult = (float)((ULONG)a ^ (ULONG)b); break;
		case opSHL: fResult = (float)((ULONG)a << (ULONG)b); break;
		case opSHR: fResult = (float)((ULONG)a >> (ULONG)b); break;

		// opMod has no case in 5960 either; it falls out of the switch.
		}

		Push(fResult);
	}
	else if (op == opAdd)
	{
		CStrObject* pLeftText = pLeft->ToStr();
		CStrObject* pRightText = pRight->ToStr();
		Push(pLeftText->concat(pRightText->GetSz()));
		pLeftText->Release();
		pRightText->Release();
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
// Built-in script functions
// =========================================================================

// CRunner::ExecuteBuiltIn
// Reconstructed from 5960 retail XBE @ 0x00038620. Matches the name by
// length then text, and returns TRUE when it handled the call:
//   EnableInput(b)  sets g_bInputEnable from the number   else "Invalid parameter"
//   eval(s)         ExecuteScript(m_self, s)               else "Bad launch" (sic)
//   launch(p [,d])  d defaults to p up to its last \ or /, then
//                   CleanFilePath both and XWriteTitleInfoAndReboot   else "Bad launch"
//   DebugBreak()    nothing in retail
//   alert(s)        debug print of s                       else "Bad alert"
//   log(s)          nothing in retail                      else "Bad log!"
// Theseus traces log(), routes alert() through Alert, launches through
// the desktop launcher on desktop, and breaks into the debugger in debug.
BOOL CRunner::ExecuteBuiltIn(const TCHAR* pchName, int cchName, int nParam, CObject** rgParam, CObject*& pRetObj)
{
	pRetObj = NULL;

	if (cchName == 11 && _tcsncmp(pchName, _T("EnableInput"), 11) == 0)
	{
		if (nParam != 1)
		{
			Error(_T("Invalid parameter"));
			return TRUE;
		}
		CNumObject* pOn = Dereference(rgParam[0])->ToNum();
		g_bInputEnable = pOn->m_value != 0.0f;
		return TRUE;
	}

	if (cchName == 4 && _tcsncmp(pchName, _T("eval"), 4) == 0)
	{
		if (nParam != 1)
		{
			Error(_T("Bad launch"));
			return TRUE;
		}
		CObject* pArg = Dereference(rgParam[0]);
		CStrObject* pScript = pArg->ToStr();
		ExecuteScript(m_self, pScript->GetSz());
		pScript->Release();
		pArg->Release();
		return TRUE;
	}

	if (cchName == 6 && _tcsncmp(pchName, _T("launch"), 6) == 0)
	{
		if (nParam != 1 && nParam != 2)
		{
			Error(_T("Bad launch"));
			return TRUE;
		}

		CObject* pPathArg = Dereference(rgParam[0]);
		CStrObject* pPath = pPathArg->ToStr();
		const TCHAR* szPath = pPath->GetSz();

		// Theseus: script paths longer than MAX_PATH get cut instead of overrunning.
		TCHAR szDir[MAX_PATH];
		if (nParam == 2)
		{
			CObject* pDirArg = Dereference(rgParam[1]);
			CStrObject* pDir = pDirArg->ToStr();
			_tcsncpy(szDir, pDir->GetSz(), MAX_PATH - 1);
			szDir[MAX_PATH - 1] = '\0';
			pDir->Release();
			pDirArg->Release();
		}
		else
		{
			_tcsncpy(szDir, szPath, MAX_PATH - 1);
			szDir[MAX_PATH - 1] = '\0';
			TCHAR* pchSlash = _tcsrchr(szDir, '/');
			if (pchSlash != NULL)
				*pchSlash = '\\';
			pchSlash = _tcsrchr(szDir, '\\');
			// Theseus: a bare file name keeps the whole path instead of writing through NULL.
			if (CHECK(pchSlash != NULL))
				*pchSlash = '\0';
		}

		TRACE(_T("Launch title: %s, %s\n"), szPath, szDir);

#if defined(_XBOX)
		FSCHAR szXbe[MAX_PATH];
		FSCHAR szXbeDir[MAX_PATH];
		CleanFilePath(szXbe, szPath);
		CleanFilePath(szXbeDir, szDir);
		XWriteTitleInfoAndReboot(szXbe, szXbeDir, LDT_NONE, 0, NULL);
#else
		// Theseus: desktop hands the title to its own launcher.
		extern void DesktopLaunchTitle(const char*);
		DesktopLaunchTitle(szDir);
#endif

		pPath->Release();
		pPathArg->Release();
		return TRUE;
	}

	if (cchName == 10 && _tcsncmp(pchName, _T("DebugBreak"), 10) == 0)
	{
#ifdef _DEBUG
		_CrtDbgBreak();
#endif
		return TRUE;
	}

	if (cchName == 5 && _tcsncmp(pchName, _T("alert"), 5) == 0)
	{
		if (nParam != 1)
		{
			Error(_T("Bad alert"));
			return TRUE;
		}
		CObject* pArg = Dereference(rgParam[0]);
		CStrObject* pText = pArg->ToStr();
		Alert(_T("%s"), pText->GetSz());
		pText->Release();
		pArg->Release();
		return TRUE;
	}

	if (cchName == 3 && _tcsncmp(pchName, _T("log"), 3) == 0)
	{
		if (nParam != 1)
		{
			Error(_T("Bad log!"));
			return TRUE;
		}
		CObject* pArg = Dereference(rgParam[0]);
		CStrObject* pText = pArg->ToStr();
		TRACE(_T("%s\n"), pText->GetSz());
		pText->Release();
		pArg->Release();
		return TRUE;
	}

	return FALSE;
}

// =========================================================================
// Name lookup
// =========================================================================

// CRunner::LookupFunction
// Reconstructed from 5960 retail XBE @ 0x000389b0. Decompile:
//     owner = m_self;
//     if ((f = m_self->FindMemberFunction(name))) return f->m_rgop;
//     for (p = m_self->m_parent; p; p = p->m_parent)
//         if ((m = LookupMember(p, name))) {
//             owner = p; return m->m_obj == objFunction ? m->m_rgop : (BYTE*)m; }
//     owner = NULL; return NULL;
// A non-function hit comes back as a raw pointer, as in the binary; Theseus
// asserts on it.
BYTE* CRunner::LookupFunction(const TCHAR* pchName, int cchName, CObject*& pOwner)
{
	pOwner = m_self;

	CFunction* pOwn = m_self->FindMemberFunction(pchName, cchName);
	if (pOwn != NULL)
		return pOwn->m_rgop;

	for (CObject* pAncestor = m_self->m_parent; pAncestor != NULL; pAncestor = pAncestor->m_parent)
	{
		CObject* pFound = LookupMember(pAncestor, pchName, cchName);
		if (pFound == NULL)
			continue;

		pOwner = pAncestor;
		if (pFound->m_obj == objFunction)
			return ((CFunction*)pFound)->m_rgop;

		ASSERT(FALSE);
		return (BYTE*)pFound;
	}

	pOwner = NULL;
	return NULL;
}

// CRunner::LookupVariable   [inlined in CVarObject::Deref / Assign]
// Try self, and if self isn't the script's this, try this too.
CObject* CRunner::LookupVariable(const TCHAR* pchName, int cchName)
{
	CObject* pFound = LookupMember(m_self, pchName, cchName);
	if (pFound == NULL && m_self != m_thisRef)
		pFound = LookupMember(m_thisRef, pchName, cchName);
	return pFound;
}

#ifdef _DEBUG
// Theseus: debug dump of the runner and its saved frames.
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
