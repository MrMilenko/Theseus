// node_system.cpp: scene-graph node infrastructure.
//
// CObject, CNode, CNodeClass, CNodeArray, CClass, CInstance, CTimeDepNode.
// The foundation of the XAP scene graph: every dashboard element is a
// node registered via the IMPLEMENT_NODE macros, and properties (PRD)
// and functions (FND) are exposed to scripts through descriptor tables
// walked at runtime. Every body here is written from the 5960 retail XBE;
// addresses sit above each function and docs/decomp/Node.md has the long
// ones (CObject::Call, CInstance::Construct) in full.
#include "std.h"
#include "theseus.h"
#include "xap_compile.h"
#include "node.h"
#include "runner.h"
#include "lerper.h"
#include "activefile.h"
#include "xip_archive.h"

extern bool g_bParseError;
extern CRunner* g_pRunner;
extern CObject* g_pThis;

extern CObject* Dereference(CObject* pObject);
extern CObject* CreateNewObject(const TCHAR* pchClassName, int cchClassName, CObject** rgparam = NULL, int nParam = 0);
extern CObject* FindMember(CNodeClass* pClass, const TCHAR* pchName, int cchName);

// =========================================================================
// CObject: base of everything a script can hold.
// =========================================================================

// The "Object" class record and its empty PRD map (map at 0x00174418).
PRD CObject::m_rgprd[] =
{
	{ NULL, pt_null, _T("Object") },
	{ NULL, pt_null, NULL }
};

CNodeClass CObject::classCObject(_T("Object"), sizeof(class CObject), CObject::CreateNode, NULL, NULL);

// CObject::CObject / CObject::CreateNode
// Reconstructed from 5960 retail XBE @ 0x0002f500 (the factory inlines the
// constructor):
//     p = new(0x14); p->vtbl = CObject; p->m_obj = 0; p->m_nRefCount = 1;
//     p->m_parent = 0; p->m_members = 0;
// A fresh object is untyped, owned once, parentless and has no members.
CObject::CObject()
{
	m_nRefCount = 1;
	m_obj = objUndefined;
	m_parent = NULL;
	m_members = NULL;
}

CObject* CObject::CreateNode()
{
	return new CObject;
}

// CObject::~CObject
// Reconstructed from 5960 retail XBE @ 0x0002f450:
//     CLerper_RemoveObject(this);
//     if (m_members) { for each define d: d->m_node->Release(); free(d); free(m_members); }
// Drops any running lerps on this object, then frees the member list
// (CNameSpace's destructor does the per-entry release and free).
CObject::~CObject()
{
	CLerper::RemoveObject(this);
	delete m_members;
}

// CObject::AddRef / CObject::Release
// Reconstructed from 5960 retail XBE @ 0x0002f4a0 / 0x0002f4b0 (shared by
// every class that doesn't override them):
//     m_nRefCount += 1;
//     m_nRefCount -= 1; if (m_nRefCount == 0) this->vtbl[0](1);   // deleting dtor
// Plain intrusive refcounting; there is only one way to write it.
void CObject::AddRef()
{
	++m_nRefCount;
}

void CObject::Release()
{
	if (--m_nRefCount == 0)
		delete this;
}

// CObject::GetNodeClass   Reconstructed from 5960 retail XBE @ 0x0002f4c0:
//     return &classCObject;
CNodeClass* CObject::GetNodeClass() const
{
	return &classCObject;
}

// CObject::IsKindOf
// Reconstructed from 5960 retail XBE @ 0x0002f4d0:
//     for (c = this->GetNodeClass(); c != 0; c = c->m_baseClass)   // +0x20
//         if (c == pClass) return 1;
//     return 0;
bool CObject::IsKindOf(CNodeClass* pClass) const
{
	CNodeClass* pWalk = GetNodeClass();
	while (pWalk != NULL)
	{
		if (pWalk == pClass)
			return true;
		pWalk = pWalk->m_baseClass;
	}
	return false;
}

// CObject::ToStr / CObject::ToNum
// Reconstructed from 5960 retail XBE @ 0x0002f530 / 0x0002f590:
//     return new CStrObject(L"[object]");        // string at 0x00021b74
//     return new CNumObject();                   // type 2, value 0
// A bare object prints as "[object]" and counts as zero.
CStrObject* CObject::ToStr()
{
	return new CStrObject(_T("[object]"));
}

CNumObject* CObject::ToNum()
{
	return new CNumObject(0.0f);
}

// CObject::Deref / GetFunctionMap / OnSetProperty
// Default virtuals in the CObject vtable at 0x00021b00: an object derefs to
// itself, has no FND table, and accepts every property write.
CObject* CObject::Deref()
{
	return this;
}

FND* CObject::GetFunctionMap() const
{
	return NULL;
}

bool CObject::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	return true;
}

// =========================================================================
// Properties: PRD lookup and the typed write.
// =========================================================================

// CObject::FindProp
// Reconstructed from 5960 retail XBE @ 0x0002ec40:
//     for (map = this->GetPropMap(); map; map = map[0].pbOffset)
//         for (i = 1; map[i].szName; i++)
//             if (!_wcsnicmp(name, map[i].szName, cch) && map[i].szName[cch] == 0)
//                 return &map[i];
//     return 0;
// Entry 0 of every map links to the base class's map, so this walks the
// class chain; names match case-insensitively and must match in full.
const PRD* CObject::FindProp(const TCHAR* szName, int cchName)
{
	for (const PRD* pMap = GetPropMap(); pMap != NULL; pMap = (const PRD*)pMap[0].pbOffset)
	{
		for (const PRD* pEntry = pMap + 1; pEntry->szName != NULL; pEntry++)
		{
			if (_tcsnicmp(szName, pEntry->szName, cchName) == 0 && pEntry->szName[cchName] == 0)
				return pEntry;
		}
	}
	return NULL;
}

// Resize a typed array property and fill it from a packed buffer.
template <class TArray>
static void FillArrayProp(BYTE* pField, const void* pvValue, int cbValue, int cbElement)
{
	TArray* pArray = (TArray*)pField;
	pArray->SetSize(cbValue / cbElement);
	CopyMemory(pArray->m_value, pvValue, cbValue);
}

// CObject::SetProperty
// Reconstructed from 5960 retail XBE @ 0x0002ecd0 (byte count in EAX).
// The field lives at this + prd->pbOffset. A string write whose new text
// equals the old (both non-null, wcscmp) returns before anything else.
// Otherwise OnSetProperty (vtable +8) gets a veto, then by type:
//     pt_string     free the old copy, store a fresh NUL-terminated copy
//     pt_*array     SetSize(cb / element size), copy cb bytes into m_value
//     everything    raw copy of cb bytes; for pt_node the old node is
//     else          Released first and the new one AddRef'd after
// The binary folds the int and num arrays onto one SetSize body.
void CObject::SetProperty(const PRD* pprd, const void* pvValue, int cbValueIn)
{
	BYTE* pField = (BYTE*)this + PTR2INT(pprd->pbOffset);

	if (pprd->nType == pt_string)
	{
		const TCHAR* szHave = *(TCHAR**)pField;
		const TCHAR* szWant = *(TCHAR* const*)pvValue;
		if (szHave != NULL && szWant != NULL && _tcscmp(szHave, szWant) == 0)
			return;
	}

	if (!OnSetProperty(pprd, pvValue))
		return;

	switch (pprd->nType)
	{
	case pt_string:
		{
			TCHAR*& szField = *(TCHAR**)pField;
			const TCHAR* szSource = *(TCHAR* const*)pvValue;
			delete[] szField;
			szField = new TCHAR[cbValueIn + 1];
			CopyChars(szField, szSource, cbValueIn);
			szField[cbValueIn] = 0;
		}
		return;

	case pt_intarray:
		FillArrayProp<CIntArray>(pField, pvValue, cbValueIn, sizeof(int));
		return;

	case pt_numarray:
		FillArrayProp<CNumArray>(pField, pvValue, cbValueIn, sizeof(float));
		return;

	case pt_vec2array:
		FillArrayProp<CVec2Array>(pField, pvValue, cbValueIn, sizeof(float) * 2);
		return;

	case pt_vec3array:
		FillArrayProp<CVec3Array>(pField, pvValue, cbValueIn, sizeof(float) * 3);
		return;

	case pt_vec4array:
		FillArrayProp<CVec4Array>(pField, pvValue, cbValueIn, sizeof(float) * 4);
		return;

	default:
		break;
	}

	bool fNode = (pprd->nType == pt_node);
	if (fNode && *(CNode**)pField != NULL)
		(*(CNode**)pField)->Release();

	CopyMemory(pField, pvValue, cbValueIn);

	if (fNode && *(CNode* const*)pvValue != NULL)
		(*(CNode* const*)pvValue)->AddRef();
}

// =========================================================================
// Functions: FND lookup and the signature dispatcher.
// =========================================================================

// CObject::FindFunction
// Reconstructed from 5960 retail XBE @ 0x0002f5d0:
//     map = this->GetFunctionMap(); if (!map) return -1;
//     for (f = map; f->pfn; f++)
//         if (wcslen(f->szName) == cch && !wcsncmp(f->szName, name, cch))
//             return f - map;               // 0x18-byte entries
//     return -1;
// Exact, case-sensitive match; the result is the FND index.
int CObject::FindFunction(const TCHAR* pchFunction, int cchFunction)
{
	FND* pMap = GetFunctionMap();
	if (pMap == NULL)
		return -1;

	for (int i = 0; pMap[i].pfn.pfn != NULL; i++)
	{
		const TCHAR* szName = pMap[i].szName;
		if ((int)_tcslen(szName) == cchFunction && _tcsncmp(szName, pchFunction, cchFunction) == 0)
			return i;
	}
	return -1;
}

// Shape of each FND signature: return kind, then one letter per argument
// (i int, n number, s string, o object). Signature ids are the binary's
// switch cases. sig_default passes the raw array; sig_isi (6) has no case
// in the binary and falls through to the unknown-signature exit.
struct SIGSHAPE
{
	char ret;
	const char* args;
};

static const SIGSHAPE c_rgSigShape[] =
{
	{ 0,   NULL },		// 0  sig_default (handled separately)
	{ 'v', "" },		// 1  sig_vv
	{ 'i', "" },		// 2  sig_iv
	{ 'i', "i" },		// 3  sig_ii
	{ 'o', "i" },		// 4  sig_si
	{ 'o', "s" },		// 5  sig_ss
	{ 0,   NULL },		// 6  sig_isi (no case)
	{ 'o', "ii" },		// 7  sig_sii
	{ 'o', "" },		// 8  sig_sv
	{ 'n', "" },		// 9  sig_nv
	{ 'n', "n" },		// 10 sig_nn
	{ 'n', "nn" },		// 11 sig_nnn
	{ 'v', "s" },		// 12 sig_vs
	{ 'v', "ss" },		// 13 sig_vss
	{ 'v', "i" },		// 14 sig_vi
	{ 'o', "" },		// 15 sig_ov
	{ 'o', "s" },		// 16 sig_os
	{ 'o', "o" },		// 17 sig_oo
	{ 'v', "ii" },		// 18 sig_vii
	{ 'v', "n" },		// 19 sig_vn
	{ 'v', "nn" },		// 20 sig_vnn
	{ 'v', "o" },		// 21 sig_vo
	{ 'v', "nnn" },		// 22 sig_vnnn
	{ 'v', "nnnn" },	// 23 sig_vnnnn
	{ 'i', "ii" },		// 24 sig_iii
	{ 'i', "s" },		// 25 sig_is
	{ 'v', "iis" },		// 26 sig_viis
	{ 'n', "i" },		// 27 sig_ni
	{ 'v', "is" },		// 28 sig_vis
	{ 'i', "is" },		// 29 sig_iis
};

// CObject::Call
// Reconstructed from 5960 retail XBE @ 0x0002f640 (2818 bytes, walked
// through in docs/decomp/Node.md). First every argument is dereferenced in
// place (Deref at vtable +0x24 until it stops changing). Then the FND's
// signature picks a case: sig_default hands the raw (array, count) to the
// method; every other case checks the count exactly, converts each
// argument with ToNum (truncated to int for i) or ToStr (NULL text becomes
// ""), calls, wraps an int or float result in a new CNumObject, and
// Releases the converted temporaries. A wrong count reports "invalid number
// of parameters" through the running script; an unknown signature returns
// NULL. Case 29 is int from (int, string), which LiveAccounts.VerifyPassword
// uses. Theseus adds the CHECK on a negative index, which would read before
// the FND table.
CObject* CObject::Call(int nFunction, CObject** rgparam, int nParam)
{
	if (!CHECK(nFunction >= 0))
		return NULL;

	const FND* pfnd = &GetFunctionMap()[nFunction];
	const FSIG f = pfnd->pfn;

	for (int iArg = 0; iArg < nParam; iArg++)
		rgparam[iArg] = Dereference(rgparam[iArg]);

	if (pfnd->sig == sig_default)
		return (this->*f.pfn_default)(rgparam, nParam);

	if (pfnd->sig >= sizeof(c_rgSigShape) / sizeof(c_rgSigShape[0]) || c_rgSigShape[pfnd->sig].args == NULL)
		return NULL;

	const SIGSHAPE& shape = c_rgSigShape[pfnd->sig];
	if (nParam != (int)strlen(shape.args))
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	// Convert the arguments; the converted objects are held until after the call.
	int nArg[4];
	float flArg[4];
	const TCHAR* szArg[4];
	CObject* pArgObj[4];
	CObject* rgTemp[4] = { NULL, NULL, NULL, NULL };

	for (int iArg = 0; iArg < nParam; iArg++)
	{
		switch (shape.args[iArg])
		{
		case 'i':
		case 'n':
			{
				CNumObject* pNum = rgparam[iArg]->ToNum();
				rgTemp[iArg] = pNum;
				flArg[iArg] = pNum->m_value;
				nArg[iArg] = (int)pNum->m_value;
			}
			break;

		case 's':
			{
				CStrObject* pStr = rgparam[iArg]->ToStr();
				rgTemp[iArg] = pStr;
				szArg[iArg] = pStr->GetSz();
			}
			break;

		case 'o':
			pArgObj[iArg] = rgparam[iArg];
			break;
		}
	}

	int nRet = 0;
	float flRet = 0.0f;
	CObject* pRet = NULL;

	switch (pfnd->sig)
	{
	case sig_vv:	(this->*f.pfn_vv)(); break;
	case sig_vi:	(this->*f.pfn_vi)(nArg[0]); break;
	case sig_vii:	(this->*f.pfn_vii)(nArg[0], nArg[1]); break;
	case sig_vis:	(this->*f.pfn_vis)(nArg[0], szArg[1]); break;
	case sig_viis:	(this->*f.pfn_viis)(nArg[0], nArg[1], szArg[2]); break;
	case sig_vs:	(this->*f.pfn_vs)(szArg[0]); break;
	case sig_vss:	(this->*f.pfn_vss)(szArg[0], szArg[1]); break;
	case sig_vn:	(this->*f.pfn_vn)(flArg[0]); break;
	case sig_vnn:	(this->*f.pfn_vnn)(flArg[0], flArg[1]); break;
	case sig_vnnn:	(this->*f.pfn_vnnn)(flArg[0], flArg[1], flArg[2]); break;
	case sig_vnnnn:	(this->*f.pfn_vnnnn)(flArg[0], flArg[1], flArg[2], flArg[3]); break;
	case sig_vo:	(this->*f.pfn_vo)(pArgObj[0]); break;

	case sig_iv:	nRet = (this->*f.pfn_iv)(); break;
	case sig_ii:	nRet = (this->*f.pfn_ii)(nArg[0]); break;
	case sig_iii:	nRet = (this->*f.pfn_iii)(nArg[0], nArg[1]); break;
	case sig_is:	nRet = (this->*f.pfn_is)(szArg[0]); break;
	case sig_iis:	nRet = (this->*f.pfn_iis)(nArg[0], szArg[1]); break;

	case sig_nv:	flRet = (this->*f.pfn_nv)(); break;
	case sig_ni:	flRet = (this->*f.pfn_ni)(nArg[0]); break;
	case sig_nn:	flRet = (this->*f.pfn_nn)(flArg[0]); break;
	case sig_nnn:	flRet = (this->*f.pfn_nnn)(flArg[0], flArg[1]); break;

	case sig_sv:	pRet = (this->*f.pfn_sv)(); break;
	case sig_si:	pRet = (this->*f.pfn_si)(nArg[0]); break;
	case sig_sii:	pRet = (this->*f.pfn_sii)(nArg[0], nArg[1]); break;
	case sig_ss:	pRet = (this->*f.pfn_ss)(szArg[0]); break;
	case sig_ov:	pRet = (this->*f.pfn_ov)(); break;
	case sig_os:	pRet = (this->*f.pfn_os)(szArg[0]); break;
	case sig_oo:	pRet = (this->*f.pfn_oo)(pArgObj[0]); break;
	}

	if (shape.ret == 'i')
		pRet = new CNumObject((float)nRet);
	else if (shape.ret == 'n')
		pRet = new CNumObject(flRet);

	for (int iArg = 0; iArg < nParam; iArg++)
	{
		if (rgTemp[iArg] != NULL)
			rgTemp[iArg]->Release();
	}

	return pRet;
}

// =========================================================================
// Members: the Dot operator and the per-object name list.
// =========================================================================

// CObject::Dot
// Reconstructed from 5960 retail XBE @ 0x000301c0. Only a name (objVariable)
// can be looked up; it resolves in this order:
//     FND index    -> new CFunctionObject{index, owner = this}, this->AddRef()
//     GetMember    -> the member itself, AddRef'd             (vtable +0x34)
//     FindProp     -> new CProperty(this, prd)
//     otherwise    -> NULL
// Theseus adds: a member that is a script function comes back wrapped in a
// CMemberFunctionObject bound to this, so the call runs with the right self.
CObject* CObject::Dot(CObject* pObj)
{
	if (pObj->m_obj != objVariable)
		return NULL;

	const CVarObject* pName = (const CVarObject*)pObj;
	const TCHAR* pchName = pName->m_text;
	int cchName = pName->m_length;

	int iFunction = FindFunction(pchName, cchName);
	if (iFunction >= 0)
	{
		CFunctionObject* pRef = new CFunctionObject;
		pRef->m_functionIndex = iFunction;
		pRef->m_owner = this;
		AddRef();
		return pRef;
	}

	CObject* pMember = GetMember(pchName, cchName);
	if (pMember != NULL)
	{
		if (pMember->m_obj == objFunction)
		{
			AddRef();
			return new CMemberFunctionObject(this, (CFunction*)pMember);
		}

		pMember->AddRef();
		return pMember;
	}

	const PRD* pprd = FindProp(pchName, cchName);
	if (pprd != NULL)
		return new CProperty(this, pprd);

	return NULL;
}

// CObject::AddMember / SetMember / GetMember
// Reconstructed from 5960 retail XBE @ 0x00030310 / 0x00030380 / 0x000303c0:
//     if (!m_members) m_members = new CNameSpace;     // 4 bytes, head = 0
//     AddMember: push a new define at the head and point it at the object
//     SetMember: find-or-add the define, point it at the object
//     GetMember: m_members ? lookup by name : 0
// AddMember always adds, so a duplicate name shadows the older entry.
void CObject::AddMember(const TCHAR* pchName, int cchName, CObject* pObject)
{
	if (m_members == NULL)
		m_members = new CNameSpace;
	m_members->Add(pchName, cchName)->m_node = (CNode*)pObject;
}

void CObject::SetMember(const TCHAR* pchName, int cchName, CObject* pObject)
{
	if (m_members == NULL)
		m_members = new CNameSpace;
	m_members->Get(pchName, cchName)->m_node = (CNode*)pObject;
}

CObject* CObject::GetMember(const TCHAR* pchName, int cchName)
{
	return m_members != NULL ? m_members->Lookup(pchName, cchName) : NULL;
}

// CObject::FindMemberFunction
// Reconstructed from 5960 retail XBE @ 0x000303e0:
//     f = this->GetMember(name);
//     if (f && f->m_obj == 13) return f;
//     for (c = this->GetNodeClass(); c; c = c->m_baseClass)
//         if ((f = c->GetMember(name)) != 0) return f->m_obj == 13 ? f : 0;
//     return 0;
// The object's own members win, then the first class up the chain that
// knows the name decides. FindMember is that class walk.
CFunction* CObject::FindMemberFunction(const TCHAR* pchName, int cchName)
{
	CObject* pFound = GetMember(pchName, cchName);
	if (pFound != NULL && pFound->m_obj == objFunction)
		return (CFunction*)pFound;

	pFound = FindMember(GetNodeClass(), pchName, cchName);
	if (pFound != NULL && pFound->m_obj == objFunction)
		return (CFunction*)pFound;

	return NULL;
}

// CObject::Dump
// Debug-only slot in the vtable; the retail XBE has no body for it, so this
// prints nothing.
void CObject::Dump() const
{
}

// =========================================================================
// CNodeClass: the registry of node types.
// =========================================================================

// Head of the class list; 0x0017e730 in the binary.
CNodeClass* CNodeClass::c_pFirstClass = NULL;

// CNodeClass::CNodeClass
// Reconstructed from 5960 retail XBE @ 0x0002e900:
//     CObject fields reset; m_className = name; m_nObjectSize = size;
//     m_pfnCreateNode = create; m_baseClass = base; m_rgprd = prd;
//     m_nextClass = c_pFirstClass; c_pFirstClass = this;
// Every class record pushes itself on the front of one global list.
CNodeClass::CNodeClass(const TCHAR* szClassName, int nObjectSize,
	CObject* (*pfnCreateNode)(), CNodeClass* pBaseClass, const PRD* rgprd)
{
	m_className = szClassName;
	m_nObjectSize = nObjectSize;
	m_pfnCreateNode = pfnCreateNode;
	m_baseClass = pBaseClass;
	m_rgprd = rgprd;

	m_nextClass = c_pFirstClass;
	c_pFirstClass = this;
}

// CNodeClass::~CNodeClass
// Reconstructed from 5960 retail XBE @ 0x0002e970:
//     for (pp = &c_pFirstClass; *pp && *pp != this; pp = &(*pp)->m_nextClass) ;
//     if (*pp == this) *pp = m_nextClass;
//     then the inlined CObject destructor
CNodeClass::~CNodeClass()
{
	CNodeClass** ppLink = &c_pFirstClass;
	while (*ppLink != NULL && *ppLink != this)
		ppLink = &(*ppLink)->m_nextClass;

	if (*ppLink == this)
		*ppLink = m_nextClass;
}

// CNodeClass::CreateNode   Inlined into CreateNewObject @ 0x00037580:
//     return m_pfnCreateNode();
CObject* CNodeClass::CreateNode()
{
	return m_pfnCreateNode();
}

// CNodeClass::FindByName
// Reconstructed from 5960 retail XBE @ 0x0002ea20:
//     for (c = c_pFirstClass; c; c = c->m_nextClass)
//         if (!wcsncmp(c->m_className, name, cch) && c->m_className[cch] == 0)
//             return c;
//     return 0;
// Case-sensitive, full-length match against the registered names.
CNodeClass* CNodeClass::FindByName(const TCHAR* pchNodeClass, int cchNodeClass)
{
	for (CNodeClass* pClass = c_pFirstClass; pClass != NULL; pClass = pClass->m_nextClass)
	{
		const TCHAR* szName = pClass->m_className;
		if (_tcsncmp(szName, pchNodeClass, cchNodeClass) == 0 && szName[cchNodeClass] == 0)
			return pClass;
	}
	return NULL;
}

// CNodeClass::FindProp
// The binary has no separate copy: the class compiler (CClassCompiler::
// ParseProps @ 0x00036440) calls CObject::FindProp on the class record,
// whose GetPropMap is m_rgprd. Same walk here.
const PRD* CNodeClass::FindProp(const TCHAR* szName, int cchName)
{
	return CObject::FindProp(szName, cchName);
}

// NewNode
// Inlined into CreateNewObject @ 0x00037580: FindByName, then the class
// factory, NULL when the name isn't registered.
CObject* NewNode(const TCHAR* pchNodeClass, int cchNodeClass)
{
	CNodeClass* pClass = CNodeClass::FindByName(pchNodeClass, cchNodeClass);
	return pClass != NULL ? pClass->CreateNode() : NULL;
}

// =========================================================================
// CNode: the base scene-graph node.
// =========================================================================

// "Node" with one script property, visible (PRD map at 0x001743a8).
PRD CNode::m_rgprd[] =
{
	{ NULL, pt_null, _T("Node") },
	NODE_PROP(pt_boolean, CNode, visible)
	{ NULL, pt_null, NULL }
};

CNodeClass CNode::classCNode(_T("Node"), sizeof(class CNode), CNode::CreateNode, NULL, CNode::m_rgprd);

// CNode::CNode / CNode::CreateNode
// Reconstructed from 5960 retail XBE @ 0x0002ebf0 (factory, ctor inlined):
//     p = new(0x1c); CObject fields; p->m_obj = 5; p->m_behavior = 0; p->m_visible = 1;
// A node starts visible with no behavior script.
CNode::CNode()
{
	m_obj = objNode;
	m_behavior = NULL;
	m_visible = true;
}

CObject* CNode::CreateNode()
{
	return new CNode;
}

// CNode::~CNode
// Reconstructed from 5960 retail XBE @ 0x0002ea80:
//     if (m_behavior) { m_behavior->ResetFunc(); free(m_behavior); }   // delete
CNode::~CNode()
{
	delete m_behavior;
}

// CNode::GetNodeClass   Reconstructed from 5960 retail XBE @ 0x0002ec30.
CNodeClass* CNode::GetNodeClass() const
{
	return &classCNode;
}

// CNode::OnLoad
// Reconstructed from 5960 retail XBE @ 0x0002eaf0 (FindMemberFunction
// inlined): look up a script function named "behavior" on the node or up
// its class chain; if there is one, give the node a runner bound to itself
// and point it at that function.
void CNode::OnLoad()
{
	CFunction* pBehavior = FindMemberFunction(_T("behavior"), 8);
	if (pBehavior == NULL)
		return;

	m_behavior = new CRunner(this);
	m_behavior->SetFunc(pBehavior);
}

// CNode::Advance
// Reconstructed from 5960 retail XBE @ 0x0002ee60:
//     r = m_behavior; if (!r) return;
//     if (r->m_wakeup != 0) { if (now < r->m_wakeup) return; r->m_wakeup = 0; }
//     g_pRunner = r; g_pThis = this;
//     for (;;) {
//         do { if (!r->Step()) r->ResetFunc(); } while (r->m_wakeup == 0);
//         if (now < r->m_wakeup) break;
//         r->m_wakeup = 0;
//     }
// The behavior script runs until it sleeps into the future, restarting
// from the top whenever it finishes. IsSleeping is that wakeup test.
void CNode::Advance(float nSeconds)
{
	if (m_behavior == NULL || m_behavior->IsSleeping())
		return;

	g_pRunner = m_behavior;
	g_pThis = this;

	do
	{
		bool fRunning = m_behavior->Step();
		if (!fRunning)
			m_behavior->ResetFunc();
	}
	while (!m_behavior->IsSleeping());
}

// CNode default virtuals (CNode vtable at 0x000219f8).
//     Render               @ 0x0002ee50   m_visible = 0
//     GetBBox              @ 0x0002ef00   zero all six floats
//     GetRadius            @ 0x0002ef30   return 0.0
//     RenderDynamicTexture @ 0x0002ef40   nothing
// SetLight, GetTextureSurface and GetPalette are the matching do-nothing
// slots. A plain node that gets asked to render just marks itself unseen.
void CNode::Render()
{
	m_visible = false;
}

void CNode::GetBBox(BBox* pBBox)
{
	ZeroMemory(pBBox, sizeof(BBox));
}

float CNode::GetRadius()
{
	return 0.0f;
}

void CNode::SetLight(int& nLight, D3DCOLORVALUE& ambient)
{
}

LPDIRECT3DTEXTURE8 CNode::GetTextureSurface()
{
	return NULL;
}

void CNode::RenderDynamicTexture(CSurfx* pSurfx)
{
}

const DWORD* CNode::GetPalette()
{
	return NULL;
}

// CNode::Dump: same story as CObject::Dump.
void CNode::Dump() const
{
}

// =========================================================================
// CNodeArray: growable list of node pointers.
// =========================================================================

// CNodeArray::CNodeArray / ~CNodeArray
// Inlined everywhere in the binary: start empty; destruction is ReleaseAll.
CNodeArray::CNodeArray()
{
	m_length = 0;
	m_nAlloc = 0;
	m_rgpNode = NULL;
}

CNodeArray::~CNodeArray()
{
	ReleaseAll();
}

// CNodeArray::AddNode
// Inlined at its callers (e.g. CInstance::Construct @ 0x00033f00):
//     if (m_nAlloc < m_length + 1) Allocate(m_nAlloc + 16);
//     m_rgpNode[m_length++] = node;
// Grows sixteen slots at a time. Takes over the caller's reference.
void CNodeArray::AddNode(CNode* pNode)
{
	if (m_length + 1 > m_nAlloc)
		Allocate(m_nAlloc + 16);

	m_rgpNode[m_length] = pNode;
	m_length++;
}

// CNodeArray::RemoveNode
// Reconstructed from 5960 retail XBE @ 0x0002ef60: every slot holding the
// node is closed up by sliding the tail down one, count shrinks, no
// Release. The binary's slide moves (length - i - 1) bytes rather than that
// many pointers; Theseus moves the whole tail, which is what was meant.
void CNodeArray::RemoveNode(CNode* pNode)
{
	int i = 0;
	while (i < m_length)
	{
		if (m_rgpNode[i] != pNode)
		{
			i++;
			continue;
		}

		for (int j = i; j < m_length - 1; j++)
			m_rgpNode[j] = m_rgpNode[j + 1];
		m_length--;
	}
}

// CNodeArray::ReleaseAll / RemoveAll
// Reconstructed from 5960 retail XBE @ 0x0002efa0 (RemoveAll inlined):
//     for (i = 0; i < m_length; i++) if (m_rgpNode[i]) m_rgpNode[i]->Release();
//     m_length = 0; m_nAlloc = 0; free(m_rgpNode); m_rgpNode = 0;
void CNodeArray::ReleaseAll()
{
	for (int i = 0; i < m_length; i++)
	{
		CNode* pNode = m_rgpNode[i];
		if (pNode != NULL)
			pNode->Release();
	}
	RemoveAll();
}

void CNodeArray::RemoveAll()
{
	delete[] m_rgpNode;
	m_rgpNode = NULL;
	m_length = 0;
	m_nAlloc = 0;
}

// CNodeArray::Allocate
// Reconstructed from 5960 retail XBE @ 0x0002eff0:
//     if (m_nAlloc < n) { p = new CNode*[n]; copy the old m_nAlloc slots;
//                         free the old block; m_rgpNode = p; m_nAlloc = n; }
// Never shrinks; new slots past the old capacity are left uninitialized.
void CNodeArray::Allocate(int nLength)
{
	if (nLength <= m_nAlloc)
		return;

	CNode** rgpGrown = new CNode*[nLength];
	if (m_rgpNode != NULL)
	{
		for (int i = 0; i < m_nAlloc; i++)
			rgpGrown[i] = m_rgpNode[i];
		delete[] m_rgpNode;
	}

	m_rgpNode = rgpGrown;
	m_nAlloc = nLength;
}

// CNodeArray::SetLength
// Inlined into CInstance::CInstance @ 0x00033df0:
//     Allocate(n); if (m_length < n) zero slots m_length..n-1; m_length = n;
void CNodeArray::SetLength(int nLength)
{
	Allocate(nLength);
	for (int i = m_length; i < nLength; i++)
		m_rgpNode[i] = NULL;
	m_length = nLength;
}

// =========================================================================
// CTimeDepNode: base for nodes that switch on and off on the clock.
// =========================================================================

IMPLEMENT_NODE("TimeDepNode", CTimeDepNode, CNode)

// PRD map at 0x001743d0.
START_NODE_PROPS(CTimeDepNode, CNode)
	NODE_PROP(pt_boolean, CTimeDepNode, loop)
	NODE_PROP(pt_number, CTimeDepNode, startTime)
	NODE_PROP(pt_number, CTimeDepNode, stopTime)
	NODE_PROP(pt_boolean, CTimeDepNode, isActive)
END_NODE_PROPS()

// CTimeDepNode::CTimeDepNode
// Reconstructed from 5960 retail XBE @ 0x0002f050 (factory, ctor inlined):
//     CNode fields; m_loop = 0; m_startTime = 0; m_stopTime = 0;
//     m_isActive = 0; m_lastStartTime = 0;
CTimeDepNode::CTimeDepNode()
{
	m_loop = false;
	m_startTime = 0.0f;
	m_stopTime = 0.0f;
	m_isActive = false;
	m_lastStartTime = 0.0f;
}

// CTimeDepNode::Advance
// Reconstructed from 5960 retail XBE @ 0x0002f0d0:
//     CNode::Advance();
//     if (!m_isActive) {
//         if (start <= now && start != lastStart && (stop == 0 || now < stop))
//             { lastStart = start; m_isActive = 1; OnIsActiveChanged(); }
//     } else if (start < stop && stop <= now)
//             { m_isActive = 0; OnIsActiveChanged(); }        // vtable +0x5c
// A node starts once per new startTime and stops at stopTime when one is
// set after the start. The binary narrows now to float for the start test;
// Theseus compares at double precision throughout.
void CTimeDepNode::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	XTIME now = TheseusGetNow();

	if (!m_isActive)
	{
		bool fNewStart = now >= m_startTime && m_startTime != m_lastStartTime;
		bool fBeforeStop = m_stopTime == 0.0f || now < m_stopTime;
		if (fNewStart && fBeforeStop)
		{
			m_lastStartTime = m_startTime;
			m_isActive = true;
			OnIsActiveChanged();
		}
		return;
	}

	if (m_startTime < m_stopTime && now >= m_stopTime)
	{
		m_isActive = false;
		OnIsActiveChanged();
	}
}

// Empty hooks for subclasses (vtable slot +0x5c and the one after it).
void CTimeDepNode::OnIsActiveChanged()
{
}

void CTimeDepNode::OnCycleEnded()
{
}

// =========================================================================
// CClass: a node class defined by a script file.
// =========================================================================

// CClass::CClass
// Reconstructed from 5960 retail XBE @ 0x00033970: registers itself as a
// "[class]" node class with no factory, base or PRD map, then clears its
// own fields and marks itself objClass (6).
CClass::CClass() : CNodeClass(_T("[class]"), 0, NULL, NULL, NULL)
{
	m_obj = objClass;
	m_url = NULL;
	m_constructor = NULL;
	m_nVarCount = 0;
}

// CClass::~CClass
// Reconstructed from 5960 retail XBE @ 0x00033a20:
//     free(m_url); if (m_constructor) m_constructor->Release();
//     m_instanceFunctions.ReleaseAll(); m_file.~CActiveFile(); ~CNodeClass();
// The member destructors cover the last three.
CClass::~CClass()
{
	delete[] m_url;
	if (m_constructor != NULL)
		m_constructor->Release();
}

// CClass::CreateNode   Reconstructed from 5960 retail XBE @ 0x00033ad0:
//     return new CInstance(this);               // 0x54 bytes
CNode* CClass::CreateNode()
{
	return new CInstance(this);
}

// Shared tail of Load and LoadAbsURL (both inline it in the binary): keep a
// copy of the URL, fetch the file, hand its text to the parser and free it.
static bool FetchAndParseClass(CClass* pClass, const TCHAR* szURL)
{
	int cchURL = _tcslen(szURL) + 1;
	delete[] pClass->m_url;
	pClass->m_url = new TCHAR[cchURL];
	CopyChars(pClass->m_url, szURL, cchURL);

	if (!pClass->m_file.Fetch(szURL))
		return false;

#ifdef _UNICODE
	pClass->m_file.MakeUnicode();
#endif

	BYTE* pbText = pClass->m_file.DetachContent();
	bool fParsed = pClass->ParseFile(szURL, (const TCHAR*)pbText);
	TheseusFreeMemory(pbText);
	return fParsed;
}

// CClass::Load
// Reconstructed from 5960 retail XBE @ 0x00033b30: saves the current
// directory, moves it to the class file's folder (so relative references
// inside resolve there), fetches and parses, then restores the directory
// on every path out. CDirPush is that save and restore.
bool CClass::Load(const TCHAR* szURL)
{
	CDirPush dirPush(szURL);
	return FetchAndParseClass(this, szURL);
}

// CClass::LoadAbsURL
// Reconstructed from 5960 retail XBE @ 0x00033c70: the same fetch and parse
// without touching the current directory.
bool CClass::LoadAbsURL(const TCHAR* szURL)
{
	return FetchAndParseClass(this, szURL);
}

// CClass::ParseFile
// Reconstructed from 5960 retail XBE @ 0x00033d00:
//     StartParse(file, name);                  // file name, line 1, no error
//     end = ParseClassBody(text);
//     if (*end) SyntaxError(L"Stuff past expected end of file!");
//     EndParse(); return !g_bParseError;
bool CClass::ParseFile(const TCHAR* szFileName, const TCHAR* szFile)
{
	StartParse(szFile, szFileName);

	const TCHAR* pchEnd = ParseClassBody(szFile);
	if (*pchEnd != 0)
		SyntaxError(_T("Stuff past expected end of file!"));

	EndParse();
	return !g_bParseError;
}

// CClass::ParseClassBody
// Reconstructed from 5960 retail XBE @ 0x00033d60: run a class compiler for
// this class over the text, keep the function it builds as the
// constructor, and return where parsing stopped.
const TCHAR* CClass::ParseClassBody(const TCHAR* pch)
{
	CClassCompiler compiler(this);
	const TCHAR* pchEnd = compiler.Compile(pch);
	m_constructor = compiler.CreateFunction();
	return pchEnd;
}

// =========================================================================
// CInstance: a live copy of a script class.
// =========================================================================

// CInstance::CInstance
// Reconstructed from 5960 retail XBE @ 0x00033df0: a CGroup marked
// objInstance (11) that holds a reference to its class, sizes its variable
// slots to the class's count (all NULL), builds its children by running the
// class constructor, then gets OnLoad like any other node.
CInstance::CInstance(CClass* pClass)
{
	m_obj = objInstance;
	m_class = pClass;
	pClass->AddRef();

	m_vars.SetLength(pClass->GetVariableCount());

	Construct();
	OnLoad();
}

// CInstance::~CInstance
// Reconstructed from 5960 retail XBE @ 0x000342c0: release the class, then
// m_vars.ReleaseAll() and the CGroup destructor (member and base dtors).
CInstance::~CInstance()
{
	m_class->Release();
}

// CInstance::GetNodeClass: an instance reports its script class.
CNodeClass* CInstance::GetNodeClass() const
{
	return m_class;
}

// CInstance::Render: draws as the group it is.
void CInstance::Render()
{
	CGroup::Render();
}

// CInstance::Dot
// Reconstructed from 5960 retail XBE @ 0x00034330: a name found in the
// class's member list becomes a CMemberVarObject (objMember, 9) or a
// CMemberFunctionObject (objFunction, 13) bound to this instance; anything
// else goes to the ordinary node lookup.
CObject* CInstance::Dot(CObject* pObj)
{
	if (pObj->m_obj == objVariable && m_class->m_members != NULL)
	{
		const CVarObject* pName = (const CVarObject*)pObj;
		CObject* pEntry = m_class->m_members->Lookup(pName->m_text, pName->m_length);
		if (pEntry != NULL)
		{
			if (pEntry->m_obj == objMember)
				return new CMemberVarObject(this, ((CMember*)pEntry)->m_memberIndex);
			if (pEntry->m_obj == objFunction)
				return new CMemberFunctionObject(this, (CFunction*)pEntry);
		}
	}

	return CNode::Dot(pObj);
}

// Reader over constructor bytecode (ints are stored unaligned).
struct CTORREADER
{
	BYTE* pb;

	BYTE Op() { return *pb++; }
	BYTE Peek() const { return *pb; }

	int Int()
	{
		int n;
		CopyMemory(&n, pb, sizeof(int));
		pb += sizeof(int);
		return n;
	}

	const TCHAR* Name(int cch)
	{
		const TCHAR* pch = (const TCHAR*)pb;
		pb += cch * sizeof(TCHAR);
		return pch;
	}

	void Prd(PRD* pprd)
	{
		CopyMemory(pprd, pb, sizeof(PRD));
		pb += sizeof(PRD);
	}
};

// CInstance::Construct
// Reconstructed from 5960 retail XBE @ 0x00033f00 (915 bytes, walked
// through in docs/decomp/Node.md). Runs the class constructor's bytecode
// (ops 100..108) to build the instance's scene graph, keeping a stack of
// open nodes and a stack of open child arrays (100 deep each), and returns
// where it stopped (the first byte that isn't one of those ops).
//     NewNode      create by name, parent = this, append to the open array
//     NewNodeProp  same but stored into a node property of the open node;
//                  a UseNode right after stores an existing variable instead
//     DefNode      the next node created is also kept in variable slot n
//     UseNode      append variable n (AddRef'd) to the open array
//     EndNode      OnLoad the open node and pop back to its parent
//     InitProp     write a literal (strings copied out and NUL-terminated)
//     InitArray    open the node-array property at byte offset n
//     EndArray     close it
//     Function     bind instance function n under a name on the open node
// Theseus adds CHECKs so a stray EndNode or EndArray stops construction
// instead of popping past the bottom, and a literal with no node to land
// on is skipped instead of crashing.
BYTE* CInstance::Construct()
{
	CTORREADER code;
	code.pb = m_class->m_constructor->m_rgop;
	if (code.pb == NULL)
		return NULL;

	CNode* rgpOpenNode[100];
	int cOpenNode = 0;
	CNode* pCurrent = NULL;

	CNodeArray* rgpOpenArray[100];
	int cOpenArray = 0;
	CNodeArray* pTargetArray = &m_children;

	int iPendingVar = -1;

	for (;;)
	{
		BYTE op = code.Op();

		if (op == opNewNode)
		{
			int cch = code.Int();
			const TCHAR* pchClass = code.Name(cch);

			rgpOpenNode[cOpenNode++] = pCurrent;
			pCurrent = (CNode*)CreateNewObject(pchClass, cch);
			pCurrent->m_parent = this;
			pTargetArray->AddNode(pCurrent);

			if (iPendingVar != -1)
			{
				pCurrent->AddRef();
				m_vars.SetNode(iPendingVar, pCurrent);
				iPendingVar = -1;
			}
		}
		else if (op == opNewNodeProp)
		{
			PRD prd;
			code.Prd(&prd);

			if (code.Peek() == opUseNode)
			{
				code.Op();
				CNode* pExisting = m_vars.GetNode(code.Int());
				pCurrent->SetProperty(&prd, &pExisting, sizeof(CNode*));
				continue;
			}

			if (code.Peek() == opDefNode)
			{
				code.Op();
				iPendingVar = code.Int();
			}

			code.Op();	// the NewNode
			int cch = code.Int();
			const TCHAR* pchClass = code.Name(cch);

			CNode* pOwner = pCurrent;
			rgpOpenNode[cOpenNode++] = pOwner;
			pCurrent = (CNode*)CreateNewObject(pchClass, cch);
			pCurrent->m_parent = this;

			// SetProperty takes its own reference, so drop the one from creation.
			pOwner->SetProperty(&prd, &pCurrent, sizeof(CNode*));
			pCurrent->Release();

			if (iPendingVar != -1)
			{
				pCurrent->AddRef();
				m_vars.SetNode(iPendingVar, pCurrent);
				iPendingVar = -1;
			}
		}
		else if (op == opDefNode)
		{
			iPendingVar = code.Int();
		}
		else if (op == opUseNode)
		{
			CNode* pExisting = m_vars.GetNode(code.Int());
			pExisting->AddRef();
			pTargetArray->AddNode(pExisting);
		}
		else if (op == opEndNode)
		{
			if (!CHECK(cOpenNode > 0))
				return code.pb - 1;
			pCurrent->OnLoad();
			pCurrent = rgpOpenNode[--cOpenNode];
		}
		else if (op == opInitProp)
		{
			PRD prd;
			code.Prd(&prd);
			int cbValue = code.Int();
			const void* pvValue = code.pb;
			code.pb += cbValue;

			if (!CHECK(pCurrent != NULL))
				continue;

			// Strings sit bare in the bytecode; SetProperty wants a pointer to a C string.
			TCHAR szText[1024];
			const TCHAR* pszText;
			if (prd.nType == pt_string)
			{
				CopyChars(szText, pvValue, cbValue);
				szText[cbValue] = 0;
				pszText = szText;
				pvValue = &pszText;
#ifdef _UNICODE
				code.pb += cbValue;
#endif
			}

			pCurrent->SetProperty(&prd, pvValue, cbValue);
		}
		else if (op == opInitArray)
		{
			int cbOffset = code.Int();
			rgpOpenArray[cOpenArray++] = pTargetArray;
			pTargetArray = (CNodeArray*)((BYTE*)pCurrent + cbOffset);
		}
		else if (op == opEndArray)
		{
			if (!CHECK(cOpenArray > 0))
				return code.pb - 1;
			pTargetArray = rgpOpenArray[--cOpenArray];
		}
		else if (op == opFunction)
		{
			int cch = code.Int();
			const TCHAR* pchName = code.Name(cch);
			CObject* pFunction = m_class->m_instanceFunctions.GetNode(code.Int());
			pFunction->AddRef();
			pCurrent->SetMember(pchName, cch, pFunction);
		}
		else
		{
			return code.pb - 1;
		}
	}
}
