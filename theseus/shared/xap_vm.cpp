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

// Shared runner state. 5960 keeps the same four at 0x17ae9c..0x17aea8.
CObject* g_pThis;
CRunner* g_pRunner;
CObject** g_rgParam;
int g_nParam;

extern bool g_bInputEnable;
extern CCamera theCamera;
extern CMathClass g_Math;

CObject* LookupMember(CObject* pThis, const TCHAR* pchName, int cchName);

// =========================================================================
// Numbers and vectors
// =========================================================================

// CNumObject constructors   [type 2, vtable 0x22c50]
// Reconstructed from 5960 retail XBE @ 0x00038fa0 (float form; the empty form
// only shows up inlined). Decompile:
//     m_nRefCount = 1; m_obj = 2; m_value = xmm0;
// The string form has no standalone copy in the binary; CStrObject::ToNum
// @ 0x32200 does the same (float)wcstod(sz, NULL) inline. Kept for runner.h.
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
	void Assign(CObject* pValue);
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

void CLocalVariable::Assign(CObject* pValue)
{
	if (m_pValue != NULL)
		m_pValue->Release();
	m_pValue = pValue;
	if (pValue != NULL)
		pValue->AddRef();
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
	m_text = new TCHAR[cch + 1];
	memcpy(m_text, pch, cch * sizeof(TCHAR));
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
		pFound = &g_Math;
	else if (_tcscmp(m_text, _T("camera")) == 0)
		pFound = &theCamera;
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
	CObject* pResolved = pFound->Deref();
	Release();
	return pResolved;
}

// CVarObject::Assign
// Reconstructed from 5960 retail XBE @ 0x000395b0. Decompile:
//     obj = (self lookup, then this lookup, as in Deref);
//     if (!obj) { Error(L"Unknown variable: %s", m_text); return; }
//     obj->Assign(value);
void CVarObject::Assign(CObject* pValue)
{
	CObject* pTarget = g_pRunner->LookupVariable(m_text, _tcslen(m_text));
	if (pTarget != NULL)
		pTarget->Assign(pValue);
	else
		g_pRunner->Error(_T("Unknown variable: %s"), m_text);
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
// The range checks are what CNodeArray::GetNode/SetNode already do.
CMemberVarObject::CMemberVarObject(CInstance* pInstance, int iMember)
{
	m_obj = objMemberVar;
	m_owner = pInstance;
	m_memberIndex = iMember;
	pInstance->AddRef();
}

CMemberVarObject::~CMemberVarObject()
{
	m_owner->Release();
}

CObject* CMemberVarObject::Deref()
{
	CObject* pValue = m_owner->m_vars.GetNode(m_memberIndex);
	if (pValue != NULL)
		pValue->AddRef();
	Release();
	return pValue;
}

void CMemberVarObject::Assign(CObject* pValue)
{
	CNodeArray& vars = m_owner->m_vars;
	CObject* pOld = vars.GetNode(m_memberIndex);
	if (pOld != NULL)
		pOld->Release();
	vars.SetNode(m_memberIndex, (CNode*)pValue);
	if (pValue != NULL)
		pValue->AddRef();
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
	CRunner vm(m_owner);
	vm.SetFunc(m_function);
	CObject* pResult = vm.Run();
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

CObject* CFunctionObject::Call(CObject** rgArgs, int nArgs)
{
	return m_owner->Call(m_functionIndex, rgArgs, nArgs);
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
// now (LookupMember sets it). 5960 just casts g_pThis; Theseus checks first.
static CInstance* MemberOwner()
{
	if (!CHECK(g_pThis != NULL && g_pThis->m_obj == objInstance))
		return NULL;
	return (CInstance*)g_pThis;
}

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
	CInstance* pInstance = MemberOwner();
	CObject* pValue = (pInstance != NULL) ? pInstance->m_vars.GetNode(m_memberIndex) : NULL;
	if (pValue != NULL)
		pValue->AddRef();
	Release();
	return pValue;
}

void CMember::Assign(CObject* pValue)
{
	CInstance* pInstance = MemberOwner();
	if (pInstance == NULL)
		return;

	CObject* pOld = pInstance->m_vars.GetNode(m_memberIndex);
	if (pOld != NULL)
		pOld->Release();
	pInstance->m_vars.SetNode(m_memberIndex, (CNode*)pValue);
	if (pValue != NULL)
		pValue->AddRef();
}

// CObject::Assign
// Reconstructed from 5960 retail XBE @ 0x00039900 (shared by CObject and
// CNode). Decompile:
//     Error(L"Cannot assign to objects");
void CObject::Assign(CObject* pValue)
{
	g_pRunner->Error(_T("Cannot assign to objects"));
}

// =========================================================================
// Node arrays as script arrays
// =========================================================================

// CNodeArrayObject constructor   [type 7, vtable 0x22d30]
// Reconstructed from 5960 retail XBE: inlined in CProperty::Deref @ 0x38b00.
//     m_obj = 7; m_nodeArray = (CNodeArray*)(node + prop offset);
CNodeArrayObject::CNodeArrayObject(CNodeArray* pArray)
{
	m_obj = objNodeArray;
	m_nodeArray = pArray;
}

// CNodeArrayObject::Call
// Reconstructed from 5960 retail XBE @ 0x000391a0. Decompile:
//     0: return new CNumObject((float)m_nodeArray->m_length);
//     1: p = rgparam[0]->Deref(); p->AddRef(); m_nodeArray->AddNode(p);
//     2: p = rgparam[0]->Deref(); m_nodeArray->RemoveNode(p); p->Release();
//     return NULL;
// add keeps both the Deref reference and an extra one, so the array owns one.
CObject* CNodeArrayObject::Call(int nFunction, CObject** rgArgs, int nArgs)
{
	switch (nFunction)
	{
	case 0:
		return new CNumObject((float)m_nodeArray->GetLength());

	case 1:
		{
			CObject* pItem = rgArgs[0]->Deref();
			pItem->AddRef();
			m_nodeArray->AddNode((CNode*)pItem);
		}
		break;

	case 2:
		{
			CObject* pItem = rgArgs[0]->Deref();
			m_nodeArray->RemoveNode((CNode*)pItem);
			pItem->Release();
		}
		break;
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
CObject* CNodeArrayObject::Dot(CObject* pKey)
{
	if (pKey->m_obj != objVariable)
	{
		int iItem = (int)((CNumObject*)pKey)->m_value;
		if (iItem < 0 || iItem >= m_nodeArray->GetLength())
		{
			g_pRunner->Error(_T("Array reference out of bounds"));
			return NULL;
		}

		CNode* pItem = m_nodeArray->GetNode(iItem);
		pItem->AddRef();
		return pItem;
	}

	static const struct { const TCHAR* sz; int cch; } c_rgMethod[] =
	{
		{ _T("length"), 6 },
		{ _T("add"), 3 },
		{ _T("remove"), 6 },
	};

	CVarObject* pName = (CVarObject*)pKey;
	for (int iMethod = 0; iMethod < 3; iMethod++)
	{
		if (pName->m_length != c_rgMethod[iMethod].cch)
			continue;
		if (_tcsncmp(pName->m_text, c_rgMethod[iMethod].sz, c_rgMethod[iMethod].cch) != 0)
			continue;

		CFunctionObject* pRef = new CFunctionObject;
		pRef->m_functionIndex = iMethod;
		pRef->m_owner = this;
		AddRef();
		return pRef;
	}

	return CObject::Dot(pKey);
}

// =========================================================================
// Node properties seen from script
// =========================================================================

// CProperty constructor / destructor   [vtable 0x22e10]
// Reconstructed from 5960 retail XBE @ 0x00038a20 and @ 0x00038aa0. Decompile:
//     ctor: m_node = node; m_pprd = prd; node->AddRef();
//     dtor: m_node->Release();
CProperty::CProperty(CObject* pOwner, const PRD* pprd)
{
	m_node = pOwner;
	m_pprd = pprd;
	pOwner->AddRef();
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
	BYTE* pbField = (BYTE*)m_node + (int)m_pprd->pbOffset;
	CObject* pWrapped;

	switch (m_pprd->nType)
	{
	case pt_integer:
		pWrapped = new CNumObject((float)*(int*)pbField);
		break;

	case pt_boolean:
		pWrapped = new CNumObject((float)*(bool*)pbField);
		break;

	case pt_number:
		pWrapped = new CNumObject(*(float*)pbField);
		break;

	case pt_children:
	case pt_nodearray:
		pWrapped = new CNodeArrayObject((CNodeArray*)pbField);
		break;

	case pt_vec3:
		pWrapped = new CVec3Object(*(D3DXVECTOR3*)pbField);
		break;

	case pt_node:
		pWrapped = *(CNode**)pbField;
		if (pWrapped != NULL)
			pWrapped->AddRef();
		break;

	case pt_string:
		pWrapped = new CStrObject(*(TCHAR**)pbField);
		break;

	default:
		g_pRunner->Error(_T("Unknown property type"));
		return NULL;
	}

	Release();
	return pWrapped;
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
// SetProperty.
void CProperty::Assign(CObject* pValue)
{
	const PROP_TYPE type = m_pprd->nType;
	if (pValue == NULL && type != pt_node)
	{
		g_pRunner->Error(_T("Illegal null assignement"));
		return;
	}

	if (type == pt_integer || type == pt_boolean || type == pt_number)
	{
		CNumObject* pNumber = pValue->Deref()->ToNum();
		float n = pNumber->m_value;
		if (type == pt_integer)
		{
			int i = (int)n;
			m_node->SetProperty(m_pprd, &i, sizeof(i));
		}
		else if (type == pt_boolean)
		{
			bool b = (n != 0.0f);
			m_node->SetProperty(m_pprd, &b, sizeof(b));
		}
		else
		{
			m_node->SetProperty(m_pprd, &n, sizeof(n));
		}
		pNumber->Release();
	}
	else if (type == pt_vec3)
	{
		CObject* pVec = pValue->Deref();
		if (pVec->m_obj == objVec3)
			m_node->SetProperty(m_pprd, &((CVec3Object*)pVec)->m_x, 3 * sizeof(float));
	}
	else if (type == pt_node)
	{
		CObject* pTarget = (pValue == NULL) ? NULL : pValue->Deref();
		m_node->SetProperty(m_pprd, &pTarget, sizeof(pTarget));
	}
	else if (type == pt_string)
	{
		CStrObject* pText = pValue->Deref()->ToStr();
		const TCHAR* szText = pText->GetSz();
		m_node->SetProperty(m_pprd, &szText, pText->GetLength());
		pText->Release();
	}
}

// CNode::Dot / CNode::ToStr   [CNode vtable 0x219f8 slots 12 and 8]
// Reconstructed from 5960 retail XBE @ 0x00038f30 and @ 0x00038f40. Decompile:
//     Dot:   return CObject::Dot(key);
//     ToStr: return new CStrObject(GetNodeClass()->m_className);
CObject* CNode::Dot(CObject* pKey)
{
	return CObject::Dot(pKey);
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
CObject* Dereference(CObject* p)
{
	for (;;)
	{
		if (p == NULL)
			return NULL;
		CObject* pNext = p->Deref();
		if (pNext == p)
			return p;
		p = pNext;
	}
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
CObject* CreateNewObject(const TCHAR* pchClassName, int cchClassName, CObject** rgArgs, int nArgs)
{
	g_rgParam = rgArgs;
	g_nParam = nArgs;

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
	CFunctionCompiler fc;
	fc.ParseBlock(szScript);
	if (fc.HadError())
		return false;

	fc.Write(opRet);
	CFunction* pScript = fc.CreateFunction();

	CRunner vm(pObject);
	vm.SetFunc(pScript);
	vm.Run();
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
bool CallFunction(CObject* pObject, const TCHAR* szFunc, int nArgs, CObject** rgArgs)
{
	g_pThis = pObject;

	CRunner vm(pObject);
	if (!vm.SetFunc(szFunc))
		return false;

	for (int iArg = 0; iArg < nArgs; iArg++)
		vm.Push(rgArgs[iArg]);

	vm.Run();
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
CObject* LookupMember(CObject* pObj, const TCHAR* pchName, int cchName)
{
	g_pThis = pObj;

	if (pObj->m_obj == objNode)
	{
		const PRD* pprd = pObj->FindProp(pchName, cchName);
		if (pprd != NULL)
		{
			CProperty* pProp = new CProperty(pObj, pprd);
			pProp->m_nRefCount = 0;
			return pProp;
		}
	}

	CObject* pFound = pObj->GetMember(pchName, cchName);
	if (pFound == NULL)
		pFound = FindMember(pObj->GetNodeClass(), pchName, cchName);

	for (CObject* pUp = pObj->m_parent; pFound == NULL && pUp != NULL; pUp = pUp->m_parent)
		pFound = LookupMember(pUp, pchName, cchName);

	return pFound;
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
CRunner::CRunner(CObject* pOwner)
{
	memset(m_stack, 0, sizeof(m_stack));
	m_sp = 0;
	m_spFrame = 0;
	m_spBase = 0;
	m_ops = NULL;
	m_nop = 0;
	m_self = pOwner;
	m_thisRef = pOwner;
	m_nextContext = NULL;
	m_bError = false;
	m_nLine = 0;
	m_wakeup = 0.0f;
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
	BYTE* pbCode = LookupFunction(szFunc, _tcslen(szFunc), m_thisRef);
	m_ops = pbCode;
	return pbCode != NULL;
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
		CObject* pSlot = Pop();
		if (pSlot != NULL)
			pSlot->Release();
	}

	m_bError = false;
	m_nop = 0;
	m_wakeup = 0.0f;
	m_spFrame = 0;
}

// CRunner::Push family
// Reconstructed from 5960 retail XBE @ 0x00037450 / 0x374b0 (numbers),
// 0x37510 (string). Decompile:
//     m_stack[m_sp++] = new CNumObject(n);
//     if (cch == -1) cch = wcslen(sz); m_stack[m_sp++] = new CStrObject(sz, cch);
// The object form is inlined everywhere as m_stack[m_sp++] = p. 5960 never
// checks the top; Theseus drops the push instead of running off the end.
void CRunner::Push(float n)
{
	Push(new CNumObject(n));
}

void CRunner::Push(int n)
{
	Push(new CNumObject((float)n));
}

void CRunner::Push(const TCHAR* pch, int cch)
{
	if (cch == -1)
		cch = _tcslen(pch);
	Push(new CStrObject(pch, cch));
}

void CRunner::Push(CObject* pObject)
{
	if (!CHECK(m_sp < countof(m_stack)))
	{
		if (pObject != NULL)
			pObject->Release();
		return;
	}
	m_stack[m_sp] = pObject;
	m_sp++;
}

// CRunner::Pop   [inlined in 5960 as p = m_stack[--m_sp]; m_stack[m_sp] = 0]
// Theseus refuses to pop an empty stack.
CObject* CRunner::Pop()
{
	if (!CHECK(m_sp != 0))
		return NULL;
	CObject*& rTop = m_stack[--m_sp];
	CObject* pTop = rTop;
	rTop = NULL;
	return pTop;
}

// CRunner::Error
// Reconstructed from 5960 retail XBE @ 0x000375e0. Decompile:
//     if (m_bError) return;
//     vswprintf(msg, 512, fmt, args);
//     if (m_nLine == 0) swprintf(out, L"###...\n###Runtime Error\n\n%s\n###...", msg);
//     else swprintf(out, L"###...\n###Runtime Error\n\nLine: %d\n\n%s\n###...", m_nLine, msg);
//     m_bError = 1;
// Only the first error of a run is reported; Run stops on the flag. Retail
// builds the text and shows it nowhere. Theseus keeps the core of that text
// without the # banners, sends it to the trace output with the house error
// marker, and pops a message box on desktop.
void CRunner::Error(const TCHAR* szFmt, ...)
{
	if (m_bError)
		return;
	m_bError = true;

	TCHAR szWhat[512];
	va_list args;
	va_start(args, szFmt);
	_vsntprintf(szWhat, countof(szWhat), szFmt, args);
	va_end(args);

	TCHAR szReport[1024];
	if (m_nLine == 0)
		_stprintf(szReport, _T("Runtime Error\n\n%s"), szWhat);
	else
		_stprintf(szReport, _T("Runtime Error\n\nLine: %d\n\n%s"), m_nLine, szWhat);

	Trace(_T("\001\007%s\n"), szReport);

#ifdef THESEUS_DESKTOP
	extern int TheseusMessageBox(const char* szText, unsigned int uType);
	TheseusMessageBox(szReport, 0);
#endif
}

// CRunner::PushContext
// Reconstructed from 5960 retail XBE @ 0x00037690. Decompile:
//     c = new RUNCONTEXT;  c->m_ops = m_ops; c->m_nop = m_nop;
//     c->m_spFrame = m_spFrame; c->m_self = m_self; c->m_thisRef = m_thisRef;
//     c->m_sp = m_sp - nParam - 1; c->m_next = m_nextContext; m_nextContext = c;
// The saved sp sits under the callee and its arguments, so returning drops both.
void CRunner::PushContext(UINT nArgs)
{
	RUNCONTEXT* pSaved = new RUNCONTEXT;
	pSaved->m_nextContext = m_nextContext;
	pSaved->m_ops = m_ops;
	pSaved->m_nop = m_nop;
	pSaved->m_spFrame = m_spFrame;
	pSaved->m_self = m_self;
	pSaved->m_thisRef = m_thisRef;
	pSaved->m_sp = m_sp - (nArgs + 1);
	m_nextContext = pSaved;
}

// Releases and clears a run of stack slots, bottom first. 5960 writes this
// loop out inline in PopContext, opNew and opCall.
static void ReleaseRange(CObject** rgSlot, UINT cSlot)
{
	for (UINT iSlot = 0; iSlot < cSlot; iSlot++)
	{
		if (rgSlot[iSlot] != NULL)
		{
			rgSlot[iSlot]->Release();
			rgSlot[iSlot] = NULL;
		}
	}
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
	CObject* pResult = Dereference(Pop());

	m_ops = pSaved->m_ops;
	m_nop = pSaved->m_nop;
	m_spFrame = pSaved->m_spFrame;
	m_self = pSaved->m_self;
	m_thisRef = pSaved->m_thisRef;
	g_pThis = pSaved->m_thisRef;

	ReleaseRange(&m_stack[pSaved->m_sp], m_sp - pSaved->m_sp);
	m_sp = pSaved->m_sp;

	m_nextContext = pSaved->m_nextContext;
	delete pSaved;

	Push(pResult);
}

// CRunner::Run
// Reconstructed from 5960 retail XBE @ 0x000377b0. Decompile:
//     m_spBase = m_sp;
//     while (!m_bError) if (!Step(&ret)) break;
// Theseus hands back whatever the outermost return left (NULL on error).
CObject* CRunner::Run()
{
	m_spBase = m_sp;

	CObject* pResult = NULL;
	while (!m_bError && Step(&pResult))
		pResult = NULL;

	return m_bError ? NULL : pResult;
}

// =========================================================================
// One instruction
// =========================================================================

// CRunner::Step
// Reconstructed from 5960 retail XBE @ 0x00037800 (1880 bytes, one switch on
// the opcode byte). Sets g_pRunner, fetches the opcode and runs it; returns
// false to stop (outermost return, error, or an opcode it doesn't know).
// Each case is described next to its code below; the full write-up with the
// decompile excerpts is in docs/decomp/VM.md. Theseus adds the stack checks
// on opFrame and opCall and the null check on opCall.
bool CRunner::Step(CObject** ppRetObj)
{
	g_pRunner = this;

	const BYTE opcode = m_ops[m_nop];
	m_nop++;

	switch (opcode)
	{
	// Push null.
	case opNull:
		Push((CObject*)NULL);
		return true;

	// int nArgs, string class. Build it with the arguments sitting on the
	// stack, release them, and leave the new object in their place.
	case opNew:
		{
			int nArgs = FetchInt();
			int cchClass;
			const TCHAR* pchClass = FetchString(cchClass);
			UINT spArgs = m_sp - nArgs;

			CObject* pNew = CreateNewObject(pchClass, cchClass, &m_stack[spArgs], nArgs);
			if (pNew == NULL)
			{
				TCHAR szClass[256];
				int cchShown = (cchClass < 255) ? cchClass : 255;
				memcpy(szClass, pchClass, cchShown * sizeof(TCHAR));
				szClass[cchShown] = '\0';
				Error(_T("Failed to create new object: \"%s\""), szClass);
				return false;
			}

			ReleaseRange(&m_stack[spArgs], nArgs);
			m_sp = spArgs;
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
			CObject* pTest = Dereference(Pop());
			bool bTaken = false;
			if (pTest != NULL)
			{
				CNumObject* pAsNum = pTest->ToNum();
				bTaken = (pAsNum->m_value != 0.0f);
				pAsNum->Release();
				pTest->Release();
			}

			UINT nopElse = FetchUInt();
			if (!bTaken)
				m_nop = nopElse;
		}
		return true;

	// byte nArgs. The callee sits under its arguments. A native function
	// ref runs right here; a bound method or a named script function gets a
	// new frame; a bare name tries the built-ins first, then the script
	// function lookup.
	case opCall:
		{
			int nArgs = m_ops[m_nop];
			m_nop++;

			// Theseus: not enough on the stack is a stop, not a wild read.
			if (!CHECK(m_sp > (UINT)nArgs))
				return false;

			UINT spCallee = m_sp - nArgs - 1;
			CObject* pCallee = m_stack[spCallee];
			CObject** rgArgs = &m_stack[spCallee + 1];

			// Theseus: calling null is an error instead of a crash.
			if (pCallee == NULL)
			{
				Error(_T("call on null"));
				return false;
			}

			if (pCallee->m_obj == objFunctionRef)
			{
				m_stack[spCallee] = NULL;
				CObject* pResult = ((CFunctionObject*)pCallee)->Call(rgArgs, nArgs);
				pCallee->Release();

				ReleaseRange(rgArgs, nArgs);
				m_sp = spCallee;
				Push(pResult);
				return true;
			}

			if (pCallee->m_obj == objMemberFunction)
			{
				CMemberFunctionObject* pMethod = (CMemberFunctionObject*)pCallee;
				pMethod->AddRef();
				PushContext(nArgs);
				m_spFrame = spCallee + 1;
				m_self = pMethod->m_owner;
				m_ops = pMethod->m_function->m_rgop;
				m_nop = 0;
				return true;
			}

			CVarObject* pName = (CVarObject*)pCallee;
			CObject* pResult = NULL;
			if (ExecuteBuiltIn(pName->m_text, pName->m_length, nArgs, rgArgs, pResult))
			{
				// The built-in consumed the argument references.
				memset(rgArgs, 0, nArgs * sizeof(CObject*));
				m_stack[spCallee] = NULL;
				m_sp = spCallee;
				Push(pResult);
				pCallee->Release();
				return true;
			}

			CObject* pOwner;
			BYTE* pbCode = LookupFunction(pName->m_text, pName->m_length, pOwner);
			if (pbCode == NULL)
			{
				TCHAR szName[32];
				int cchShown = (pName->m_length < 31) ? pName->m_length : 31;
				_tcsncpy(szName, pName->m_text, cchShown);
				szName[cchShown] = '\0';
				Error(_T("unknown function: %s"), szName);
				return false;
			}

			PushContext(nArgs);
			m_self = pOwner;
			g_pThis = pOwner;
			m_spFrame = spCallee + 1;
			m_ops = pbCode;
			m_nop = 0;
		}
		return true;

	// Return. Inside a call, restore the caller and leave the result on its
	// stack. At the outermost level, hand back the top of stack and stop.
	case opRet:
		if (m_nextContext == NULL)
		{
			if (ppRetObj != NULL)
				*ppRetObj = (m_sp == 0) ? NULL : Pop();
			return false;
		}
		PopContext();
		return true;

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

	case opAdd: case opSub: case opMul: case opDiv:
	case opEQ: case opNE: case opLT: case opGT: case opLE: case opGE:
	case opMod: case opSHL: case opSHR:
	case opAnd: case opOr: case opXor:
		return BinaryOperator(opcode);

	// a.b pushes a then the name b; a[i] pushes a then i, which gets
	// resolved first. Either way the left side is resolved one step and
	// asked for the member.
	case opDot:
	case opArray:
		{
			CObject* pKey = Pop();
			CObject* pBase = Pop();
			if (pBase == NULL || pKey == NULL)
			{
				Error(_T("illegal null reference"));
				return false;
			}

			if (opcode == opArray)
				pKey = Dereference(pKey);

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
			CObject*& rSlot = m_stack[m_spFrame + FetchInt()];
			if (rSlot == NULL)
				rSlot = new CLocalVariable;
			rSlot->AddRef();
			Push(rSlot);
		}
		return true;

	// Push the current self.
	case opThis:
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
			float n = ((CNumObject*)pValue)->m_value;
			pValue->Release();
			Push(-n);
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
			for (int nLocals = FetchInt(); nLocals > 0; nLocals--)
			{
				CObject* pLocal = Pop();
				if (pLocal != NULL)
					pLocal->Release();
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
			m_wakeup = ((CNumObject*)pDelay)->m_value + TheseusGetNow();
			pDelay->Release();
		}
		return true;
	}

	// Any other byte stops the runner.
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
// conversion for the bit ops.
bool CRunner::BinaryOperator(BYTE op)
{
	CObject* pB = Dereference(Pop());
	CObject* pA = Dereference(Pop());

	if (pA == NULL || pB == NULL || (pA->m_obj == objNode && pB->m_obj == objNode))
	{
		if (op != opEQ && op != opNE)
		{
			Error(_T("illegal null reference"));
			return false;
		}

		bool bSame = (pA == pB);
		Push((op == opEQ) == bSame ? 1.0f : 0.0f);

		if (pB != NULL)
			pB->Release();
		if (pA != NULL)
			pA->Release();
		return true;
	}

	const int typeA = pA->m_obj;
	const int typeB = pB->m_obj;

	if (typeA == objString && typeB == objString && op != opAdd)
	{
		int nOrder = _tcscmp(((CStrObject*)pA)->GetSz(), ((CStrObject*)pB)->GetSz());
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
	else if (typeA == objNumber && typeB == objNumber)
	{
		float a = ((CNumObject*)pA)->m_value;
		float b = ((CNumObject*)pB)->m_value;
		float fResult;

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
		CStrObject* pTextA = pA->ToStr();
		CStrObject* pTextB = pB->ToStr();
		Push(pTextA->concat(pTextB->GetSz()));
		pTextA->Release();
		pTextB->Release();
	}
	else
	{
		Error(_T("type mismatch error"));
		return false;
	}

	pA->Release();
	pB->Release();
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
//   alert(s)        debug print of s with "%s"             else "Bad alert"
//   log(s)          nothing in retail                      else "Bad log!"
// Theseus prints log() on desktop, routes alert() through Alert, and
// launches through the desktop launcher on desktop.
static bool IsBuiltIn(const TCHAR* pchName, int cchName, const TCHAR* szBuiltIn)
{
	return cchName == (int)_tcslen(szBuiltIn) && _tcsncmp(pchName, szBuiltIn, cchName) == 0;
}

BOOL CRunner::ExecuteBuiltIn(const TCHAR* pchName, int cchName, int nArgs, CObject** rgArgs, CObject*& pRetObj)
{
	pRetObj = NULL;

	if (IsBuiltIn(pchName, cchName, _T("EnableInput")))
	{
		if (nArgs == 1)
			g_bInputEnable = (Dereference(rgArgs[0])->ToNum()->m_value != 0.0f);
		else
			Error(_T("Invalid parameter"));
		return TRUE;
	}

	if (IsBuiltIn(pchName, cchName, _T("eval")))
	{
		if (nArgs == 1)
		{
			CObject* pArg = Dereference(rgArgs[0]);
			CStrObject* pScript = pArg->ToStr();
			ExecuteScript(m_self, pScript->GetSz());
			pScript->Release();
			pArg->Release();
		}
		else
			Error(_T("Bad launch"));
		return TRUE;
	}

	if (IsBuiltIn(pchName, cchName, _T("launch")))
	{
		if (nArgs < 1 || nArgs > 2)
		{
			Error(_T("Bad launch"));
			return TRUE;
		}

		CObject* pPathArg = Dereference(rgArgs[0]);
		CStrObject* pPath = pPathArg->ToStr();
		const TCHAR* szPath = pPath->GetSz();

		// Theseus: script paths longer than MAX_PATH get cut instead of overrunning.
		TCHAR szDir[MAX_PATH];
		szDir[MAX_PATH - 1] = '\0';
		if (nArgs == 1)
		{
			_tcsncpy(szDir, szPath, MAX_PATH - 1);
			TCHAR* pchSep = _tcsrchr(szDir, '/');
			if (pchSep != NULL)
				*pchSep = '\\';
			pchSep = _tcsrchr(szDir, '\\');
			// Theseus: a bare file name keeps the whole path instead of writing through NULL.
			if (CHECK(pchSep != NULL))
				*pchSep = '\0';
		}
		else
		{
			CObject* pDirArg = Dereference(rgArgs[1]);
			CStrObject* pDir = pDirArg->ToStr();
			_tcsncpy(szDir, pDir->GetSz(), MAX_PATH - 1);
			pDir->Release();
			pDirArg->Release();
		}

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

	if (IsBuiltIn(pchName, cchName, _T("DebugBreak")))
		return TRUE;

	if (IsBuiltIn(pchName, cchName, _T("alert")) || IsBuiltIn(pchName, cchName, _T("log")))
	{
		bool bAlert = (cchName == 5);
		if (nArgs != 1)
		{
			Error(bAlert ? _T("Bad alert") : _T("Bad log!"));
			return TRUE;
		}

		CObject* pArg = Dereference(rgArgs[0]);
		CStrObject* pText = pArg->ToStr();
		if (bAlert)
			Alert(_T("%s"), pText->GetSz());
#ifdef THESEUS_DESKTOP
		else
			Trace(_T("%s\n"), pText->GetSz()); // Theseus: desktop prints log() lines
#endif
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
// A non-function hit comes back as a raw pointer, as in the binary.
BYTE* CRunner::LookupFunction(const TCHAR* pchName, int cchName, CObject*& pOwner)
{
	pOwner = m_self;

	CFunction* pOwn = m_self->FindMemberFunction(pchName, cchName);
	if (pOwn != NULL)
		return pOwn->m_rgop;

	for (CObject* pUp = m_self->m_parent; pUp != NULL; pUp = pUp->m_parent)
	{
		CObject* pFound = LookupMember(pUp, pchName, cchName);
		if (pFound != NULL)
		{
			pOwner = pUp;
			return (pFound->m_obj == objFunction) ? ((CFunction*)pFound)->m_rgop : (BYTE*)pFound;
		}
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
