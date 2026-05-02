// node_system.cpp: scene-graph node infrastructure.
//
// CObject, CNode, CNodeClass, CNodeArray, CClass, CInstance, CTimeDepNode.
// The foundation of the XAP scene graph: every dashboard element is a
// node registered via the IMPLEMENT_NODE macros, and properties (PRD)
// and functions (FND) are exposed to scripts through descriptor tables
// walked at runtime. Decompiled from the 5960 retail XBE; see
// docs/decomp/Node.md.
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

int SizeOfType(PROP_TYPE type)
{
	switch (type)
	{
	default:
		return 0;

	case pt_boolean:
		return sizeof (bool);

	case pt_integer:
		return sizeof (int);

	case pt_number:
		return sizeof (float);

	case pt_string:
		return sizeof (TCHAR*);

	case pt_children:
		return sizeof (CNodeArray);

	case pt_vec3:
	case pt_color:
		return sizeof (D3DXVECTOR3);

	case pt_vec4:
	case pt_quaternion:
		return sizeof (D3DXVECTOR4);

	case pt_node:
		return sizeof (CNode*);
	}
}
extern CObject* Dereference(CObject* pObject);
extern CObject* CreateNewObject(const TCHAR* pchClassName, int cchClassName, CObject** rgparam = NULL, int nParam = 0);
extern CObject* FindMember(CNodeClass* pClass, const TCHAR* pchName, int cchName);

// =========================================================================
// Section 1, CObject: base class for all script-visible objects.
// =========================================================================

PRD CObject::m_rgprd[] =
{
	{ NULL, pt_null, _T("Object") },
	{ NULL, pt_null, NULL }
};

CNodeClass CObject::classCObject(_T("Object"), sizeof(class CObject), CObject::CreateNode, NULL, NULL);

CObject::CObject()
{
	m_obj = objUndefined;
	m_nRefCount = 1;
	m_members = NULL;
	m_parent = NULL;
}

CObject::~CObject()
{
	CLerper::RemoveObject(this);
	delete m_members;
}

void CObject::AddRef()  { m_nRefCount++; }

void CObject::Release()
{
	m_nRefCount--;
	if (m_nRefCount == 0)
		delete this;
}

CObject* CObject::CreateNode() { return new CObject; }

CNodeClass* CObject::GetNodeClass() const { return &classCObject; }

bool CObject::IsKindOf(CNodeClass* pClass) const
{
	for (CNodeClass* pCheck = GetNodeClass(); pCheck != NULL; pCheck = pCheck->m_baseClass)
	{
		if (pClass == pCheck)
			return true;
	}
	return false;
}

CStrObject* CObject::ToStr() { return new CStrObject(_T("[object]")); }
CNumObject* CObject::ToNum() { return new CNumObject(0.0f); }
CObject* CObject::Deref()    { return this; }
FND* CObject::GetFunctionMap() const { return NULL; }

bool CObject::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	return true;
}

// =========================================================================
// Section 2, property system: PRD lookup and SetProperty dispatch.
// =========================================================================

// Walk the PRD chain (linked via first entry's pbOffset) to find a named property
const PRD* CObject::FindProp(const TCHAR* szName, int cchName)
{
	const PRD* rgprd = GetPropMap();
	while (rgprd != NULL)
	{
		const PRD* pNext = (const PRD*)rgprd[0].pbOffset;
		for (int i = 1; rgprd[i].szName != NULL; i++)
		{
			if (_tcsnicmp(szName, rgprd[i].szName, cchName) == 0 && rgprd[i].szName[cchName] == '\0')
				return &rgprd[i];
		}
		rgprd = pNext;
	}
	return NULL;
}


void CObject::SetProperty(const PRD* pprd, const void* pvValue, int cbValueIn)
{
	BYTE* pbDest = (BYTE*)this + PTR2INT(pprd->pbOffset);

	// String shortcut: skip if value unchanged
	if (pprd->nType == pt_string)
	{
		TCHAR* szOld = *(TCHAR**)pbDest;
		TCHAR* szNew = *(TCHAR**)pvValue;
		if (szOld != NULL && szNew != NULL && _tcscmp(szOld, szNew) == 0)
			return;
	}

	if (!OnSetProperty(pprd, pvValue))
		return;

	switch (pprd->nType)
	{
	case pt_intarray:
		{
			CIntArray* pArr = (CIntArray*)pbDest;
			pArr->SetSize(cbValueIn / sizeof(int));
			CopyMemory(pArr->m_value, pvValue, cbValueIn);
		}
		break;

	case pt_numarray:
		{
			CNumArray* pArr = (CNumArray*)pbDest;
			pArr->SetSize(cbValueIn / sizeof(float));
			CopyMemory(pArr->m_value, pvValue, cbValueIn);
		}
		break;

	case pt_vec2array:
		{
			CVec2Array* pArr = (CVec2Array*)pbDest;
			pArr->SetSize(cbValueIn / (sizeof(float) * 2));
			CopyMemory(pArr->m_value, pvValue, cbValueIn);
		}
		break;

	case pt_vec3array:
		{
			CVec3Array* pArr = (CVec3Array*)pbDest;
			pArr->SetSize(cbValueIn / (sizeof(float) * 3));
			CopyMemory(pArr->m_value, pvValue, cbValueIn);
		}
		break;

	case pt_vec4array:
		{
			CVec4Array* pArr = (CVec4Array*)pbDest;
			pArr->SetSize(cbValueIn / (sizeof(float) * 4));
			CopyMemory(pArr->m_value, pvValue, cbValueIn);
		}
		break;

	case pt_string:
		{
			TCHAR* szOld = *(TCHAR**)pbDest;
			delete[] szOld;

			TCHAR* sz = *(TCHAR**)pvValue;
			ASSERT((int)_tcslen(sz) == cbValueIn);
			TCHAR* szNew = new TCHAR[cbValueIn + 1];
			CopyChars(szNew, sz, cbValueIn);
			szNew[cbValueIn] = 0;
			*(TCHAR**)pbDest = szNew;
		}
		break;

	default:
		if (pprd->nType == pt_node)
		{
			CNode* pNode = *(CNode**)pbDest;
			if (pNode != NULL) pNode->Release();
		}

		ASSERT(SizeOfType(pprd->nType) == cbValueIn);
		CopyMemory(pbDest, pvValue, cbValueIn);

		if (pprd->nType == pt_node)
		{
			CNode* pNode = *(CNode**)pvValue;
			if (pNode != NULL) pNode->AddRef();
		}
		break;
	}
}

// =========================================================================
// Section 3, FND function dispatch via CObject::Call.
// The big dispatch switch that bridges script calls to C++ member functions
// =========================================================================

int CObject::FindFunction(const TCHAR* pchFunction, int cchFunction)
{
	FND* rgfnd = GetFunctionMap();
	if (rgfnd == NULL)
		return -1;

	for (FND* pfnd = rgfnd; pfnd->pfn.pfn != NULL; pfnd++)
	{
		if ((int)_tcslen(pfnd->szName) == cchFunction && _tcsncmp(pfnd->szName, pchFunction, cchFunction) == 0)
			return (int)(pfnd - rgfnd);
	}

	return -1;
}

typedef CObject* (__cdecl CObject::*NODE_PFN_DEFAULT)(CObject**, int);

CObject* CObject::Call(int nFunction, CObject** rgparam, int nParam)
{
	ASSERT(nFunction >= 0);

	FND* pfnd = &GetFunctionMap()[nFunction];
	union FSIG fsig = pfnd->pfn;
	CObject* pRetObject = NULL;

	for (int i = 0; i < nParam; i++)
		rgparam[i] = Dereference(rgparam[i]);

	switch (pfnd->sig)
	{
	default:
		ASSERT(FALSE);
		return NULL;

	case sig_default:
		pRetObject = (this->*fsig.pfn_default)(rgparam, nParam);
		break;

	case sig_vv:
		if (nParam != 0) goto LBadParamCount;
		(this->*fsig.pfn_vv)();
		break;

	case sig_vi:
		{
			if (nParam != 1) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			(this->*fsig.pfn_vi)((int)p1->m_value);
			p1->Release();
		}
		break;

	case sig_vii:
		{
			if (nParam != 2) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			(this->*fsig.pfn_vii)((int)p1->m_value, (int)p2->m_value);
			p1->Release(); p2->Release();
		}
		break;

	case sig_vis:
		{
			if (nParam != 2) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CStrObject* p2 = rgparam[1]->ToStr();
			(this->*fsig.pfn_vis)((int)p1->m_value, p2->GetSz());
			p1->Release(); p2->Release();
		}
		break;

	case sig_viis:
		{
			if (nParam != 3) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			CStrObject* p3 = rgparam[2]->ToStr();
			(this->*fsig.pfn_viis)((int)p1->m_value, (int)p2->m_value, p3->GetSz());
			p1->Release(); p2->Release(); p3->Release();
		}
		break;

	case sig_iv:
		if (nParam != 0) goto LBadParamCount;
		pRetObject = new CNumObject((float)(this->*fsig.pfn_iv)());
		break;

	case sig_sv:
		if (nParam != 0) goto LBadParamCount;
		pRetObject = (this->*fsig.pfn_sv)();
		break;

	case sig_vs:
		{
			if (nParam != 1) goto LBadParamCount;
			CStrObject* p1 = rgparam[0]->ToStr();
			(this->*fsig.pfn_vs)(p1->GetSz());
			p1->Release();
		}
		break;

	case sig_vss:
		{
			if (nParam != 2) goto LBadParamCount;
			CStrObject* p1 = rgparam[0]->ToStr();
			CStrObject* p2 = rgparam[1]->ToStr();
			(this->*fsig.pfn_vss)(p1->GetSz(), p2->GetSz());
			p1->Release(); p2->Release();
		}
		break;

	case sig_ii:
		{
			if (nParam != 1) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			pRetObject = new CNumObject((float)(this->*fsig.pfn_ii)((int)p1->m_value));
			p1->Release();
		}
		break;

	case sig_ni:
		{
			if (nParam != 1) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			pRetObject = new CNumObject((this->*fsig.pfn_ni)((int)p1->m_value));
			p1->Release();
		}
		break;

	case sig_iii:
		{
			if (nParam != 2) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			pRetObject = new CNumObject((float)(this->*fsig.pfn_iii)((int)p1->m_value, (int)p2->m_value));
			p1->Release(); p2->Release();
		}
		break;

	case sig_si:
		{
			if (nParam != 1) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			pRetObject = (this->*fsig.pfn_si)((int)p1->m_value);
			p1->Release();
		}
		break;

	case sig_sii:
		{
			if (nParam != 2) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			pRetObject = (this->*fsig.pfn_sii)((int)p1->m_value, (int)p2->m_value);
			p1->Release(); p2->Release();
		}
		break;

	case sig_ss:
		{
			if (nParam != 1) goto LBadParamCount;
			CStrObject* p1 = rgparam[0]->ToStr();
			pRetObject = (this->*fsig.pfn_ss)(p1->GetSz());
			p1->Release();
		}
		break;

	case sig_nv:
		if (nParam != 0) goto LBadParamCount;
		pRetObject = new CNumObject((this->*fsig.pfn_nv)());
		break;

	case sig_nn:
		{
			if (nParam != 1) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			pRetObject = new CNumObject((this->*fsig.pfn_nn)(p1->m_value));
			p1->Release();
		}
		break;

	case sig_nnn:
		{
			if (nParam != 2) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			pRetObject = new CNumObject((this->*fsig.pfn_nnn)(p1->m_value, p2->m_value));
			p1->Release(); p2->Release();
		}
		break;

	case sig_vn:
		{
			if (nParam != 1) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			(this->*fsig.pfn_vn)(p1->m_value);
			p1->Release();
		}
		break;

	case sig_vnn:
		{
			if (nParam != 2) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			(this->*fsig.pfn_vnn)(p1->m_value, p2->m_value);
			p1->Release(); p2->Release();
		}
		break;

	case sig_vnnn:
		{
			if (nParam != 3) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			CNumObject* p3 = rgparam[2]->ToNum();
			(this->*fsig.pfn_vnnn)(p1->m_value, p2->m_value, p3->m_value);
			p1->Release(); p2->Release(); p3->Release();
		}
		break;

	case sig_vnnnn:
		{
			if (nParam != 4) goto LBadParamCount;
			CNumObject* p1 = rgparam[0]->ToNum();
			CNumObject* p2 = rgparam[1]->ToNum();
			CNumObject* p3 = rgparam[2]->ToNum();
			CNumObject* p4 = rgparam[3]->ToNum();
			(this->*fsig.pfn_vnnnn)(p1->m_value, p2->m_value, p3->m_value, p4->m_value);
			p1->Release(); p2->Release(); p3->Release(); p4->Release();
		}
		break;

	case sig_ov:
		if (nParam != 0) goto LBadParamCount;
		pRetObject = (this->*fsig.pfn_ov)();
		break;

	case sig_os:
		{
			if (nParam != 1) goto LBadParamCount;
			CStrObject* p1 = rgparam[0]->ToStr();
			pRetObject = (this->*fsig.pfn_os)(p1->GetSz());
			p1->Release();
		}
		break;

	case sig_oo:
		if (nParam != 1) goto LBadParamCount;
		pRetObject = (this->*fsig.pfn_oo)(rgparam[0]);
		break;

	case sig_vo:
		if (nParam != 1) goto LBadParamCount;
		(this->*fsig.pfn_vo)(rgparam[0]);
		break;

	case sig_is:
		{
			if (nParam != 1) goto LBadParamCount;
			CStrObject* p1 = rgparam[0]->ToStr();
			pRetObject = new CNumObject((float)(this->*fsig.pfn_is)(p1->GetSz()));
			p1->Release();
		}
		break;
	}

	return pRetObject;

LBadParamCount:
	g_pRunner->Error(_T("invalid number of parameters"));
	return NULL;
}

// =========================================================================
// Section 4, member access: Dot operator and member namespace.
// =========================================================================

CObject* CObject::Dot(CObject* pObj)
{
	if (pObj->m_obj == objVariable)
	{
		CVarObject* pVar = (CVarObject*)pObj;

		// Check FND table first
		int nFunction = FindFunction(pVar->m_text, pVar->m_length);
		if (nFunction >= 0)
		{
			CFunctionObject* pFun = new CFunctionObject;
			pFun->m_functionIndex = nFunction;
			pFun->m_owner = this;
			AddRef();
			return pFun;
		}

		// Then instance members
		CObject* pObject = GetMember(pVar->m_text, pVar->m_length);
		if (pObject != NULL)
		{
			pObject->AddRef();
			return pObject;
		}

		// Then PRD properties
		const PRD* pprd = FindProp(pVar->m_text, pVar->m_length);
		if (pprd != NULL)
			return new CProperty(this, pprd);
	}

	return NULL;
}

void CObject::AddMember(const TCHAR* pchName, int cchName, CObject* pObject)
{
	if (m_members == NULL)
		m_members = new CNameSpace;
	CDefine* pDefine = m_members->Add(pchName, cchName);
	pDefine->m_node = (CNode*)pObject;
}

void CObject::SetMember(const TCHAR* pchName, int cchName, CObject* pObject)
{
	if (m_members == NULL)
		m_members = new CNameSpace;
	CDefine* pDefine = m_members->Get(pchName, cchName);
	pDefine->m_node = (CNode*)pObject;
}

CObject* CObject::GetMember(const TCHAR* pchName, int cchName)
{
	if (m_members == NULL)
		return NULL;
	return m_members->Lookup(pchName, cchName);
}

CFunction* CObject::FindMemberFunction(const TCHAR* pchName, int cchName)
{
	// Instance functions
	CFunction* pFunction = (CFunction*)GetMember(pchName, cchName);
	if (pFunction != NULL && pFunction->m_obj == objFunction)
		return pFunction;

	// Class hierarchy functions
	pFunction = (CFunction*)FindMember(GetNodeClass(), pchName, cchName);
	if (pFunction != NULL && pFunction->m_obj == objFunction)
		return pFunction;

	return NULL;
}


void CObject::Dump() const
{
	static const TCHAR* rgszType [] =
	{
		_T("Unknown"),
		_T("Null"),
		_T("Number"),
		_T("String"),
		_T("Variable"),
		_T("Node"),
		_T("Class"),
		_T("NodeArray"),
		_T("FunctionRef"),
		_T("Member"),
		_T("MemberVar"),
		_T("Instance"),
		_T("Use"),
		_T("Function"),
		_T("MemberFunction"),
		_T("Array"),
	};

	TRACE(_T("{\nObject: 0x%08x\n"), this);
	if (this != NULL)
	{
		TRACE(_T("Type: %s\n"), rgszType[m_obj]);
		TRACE(_T("Parent: 0x%08x\n"), m_parent);
		TRACE(_T("RefCount: %d\n"), m_nRefCount);

		CNodeClass* pNodeClass = GetNodeClass();
		if (pNodeClass != NULL)
			TRACE(_T("Class: %s\n"), pNodeClass->m_className);
		else
			TRACE(_T("Class: null\n"));

		if (m_members != NULL)
		{
			TRACE(_T("members:\n"));
			m_members->Dump();
		}
	}

	TRACE(_T("}\n"));
}

// =========================================================================
// Section 5, CNodeClass: class registry and factory.
// =========================================================================

CNodeClass* CNodeClass::c_pFirstClass = NULL;

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

CNodeClass::~CNodeClass()
{
	CNodeClass** pp;
	for (pp = &c_pFirstClass; *pp != NULL && *pp != this; pp = &(*pp)->m_nextClass)
		;
	if (*pp == this)
		*pp = m_nextClass;
#ifdef _DEBUG
	else
		ASSERT(FALSE);
#endif
}

CObject* CNodeClass::CreateNode()
{
	return (*m_pfnCreateNode)();
}

CNodeClass* CNodeClass::FindByName(const TCHAR* pchNodeClass, int cchNodeClass)
{
	for (CNodeClass* pClass = c_pFirstClass; pClass != NULL; pClass = pClass->m_nextClass)
	{
		if (_tcsncmp(pClass->m_className, pchNodeClass, cchNodeClass) == 0 &&
			pClass->m_className[cchNodeClass] == '\0')
			return pClass;
	}
	return NULL;
}

const PRD* CNodeClass::FindProp(const TCHAR* szName, int cchName)
{
	const PRD* rgprd = GetPropMap();
	while (rgprd != NULL)
	{
		const PRD* pNext = (const PRD*)rgprd[0].pbOffset;
		for (int i = 1; rgprd[i].szName != NULL; i++)
		{
			if (_tcsnicmp(szName, rgprd[i].szName, cchName) == 0 && rgprd[i].szName[cchName] == '\0')
				return &rgprd[i];
		}
		rgprd = pNext;
	}
	return NULL;
}

CObject* NewNode(const TCHAR* pchNodeClass, int cchNodeClass)
{
	CNodeClass* pNodeClass = CNodeClass::FindByName(pchNodeClass, cchNodeClass);
	if (pNodeClass == NULL)
		return NULL;
	return pNodeClass->CreateNode();
}

// =========================================================================
// Section 6, CNode: base scene-graph node.
// =========================================================================

PRD CNode::m_rgprd[] =
{
	{ NULL, pt_null, _T("Node") },
	NODE_PROP(pt_boolean, CNode, visible)
	{ NULL, pt_null, NULL }
};

CNodeClass CNode::classCNode(_T("Node"), sizeof(class CNode), CNode::CreateNode, NULL, CNode::m_rgprd);

CNode::CNode()
{
	m_obj = objNode;
	m_behavior = NULL;
	m_visible = true;
}

CNode::~CNode()
{
	delete m_behavior;
}

CObject* CNode::CreateNode() { return new CNode; }

CNodeClass* CNode::GetNodeClass() const { return &classCNode; }

void CNode::OnLoad()
{
	CFunction* pFunction = FindMemberFunction(_T("behavior"), 8);
	if (pFunction != NULL)
	{
		m_behavior = new CRunner(this);
		m_behavior->SetFunc(pFunction);
	}
}

void CNode::Advance(float nSeconds)
{
	if (m_behavior != NULL && !m_behavior->IsSleeping())
	{
		g_pRunner = m_behavior;
		g_pThis = this;
		do
		{
			if (!m_behavior->Step())
			{
				m_behavior->ResetFunc();
			}
		}
		while (!m_behavior->IsSleeping());
	}
}

void CNode::Render()             { m_visible = false; }
void CNode::GetBBox(BBox* pBBox) { ZeroMemory(pBBox, sizeof(BBox)); }
float CNode::GetRadius()         { return 0.0f; }
void CNode::SetLight(int& nLight, D3DCOLORVALUE& ambient) {}
LPDIRECT3DTEXTURE8 CNode::GetTextureSurface() { return NULL; }
void CNode::RenderDynamicTexture(CSurfx* pSurfx) {}
const DWORD* CNode::GetPalette() { return NULL; }

void CNode::Dump() const
{
	CObject::Dump();

	for (const PRD* rgprd = GetPropMap(); rgprd != NULL; rgprd = (const PRD*)rgprd[0].pbOffset)
	{
		int i;
		for (i = 1; rgprd[i].szName != NULL; i += 1)
		{
			TRACE(_T("prop: '%s::%s' = "), rgprd[0].szName, rgprd[i].szName);
			const BYTE* pbThisValue = (const BYTE*)this + (int)rgprd[i].pbOffset;
			switch (rgprd[i].nType)
			{
			case pt_integer:
				TRACE(_T("%d"), *((const int*)pbThisValue));
				break;

			case pt_boolean:
				TRACE(_T("%s"), *((const bool*)pbThisValue) ? _T("true") : _T("false"));
				break;

			case pt_number:
				TRACE(_T("%f"), *((const float*)pbThisValue));
				break;

			case pt_string:
				TRACE(_T("%s"), *((const TCHAR**)pbThisValue));
				break;

			// FUTURE: Dump other types too!
			}

			TRACE(_T("\n"));
		}
	}
}


// =========================================================================
// Section 7, CNodeArray: dynamic array of node pointers.
// =========================================================================

CNodeArray::CNodeArray()  { m_length = 0; m_nAlloc = 0; m_rgpNode = NULL; }
CNodeArray::~CNodeArray() { ReleaseAll(); }

void CNodeArray::AddNode(CNode* pNode)
{
	if (m_nAlloc < m_length + 1)
		Allocate(m_nAlloc + 16);
	m_rgpNode[m_length++] = pNode;
}

void CNodeArray::RemoveNode(CNode* pNode)
{
	int i;
	for (i = 0; i < m_length; i++)
	{
		if (m_rgpNode[i] == pNode)
		{
			CopyMemory(&m_rgpNode[i], &m_rgpNode[i + 1], (m_length - i - 1) * sizeof(CNode*));
			m_length--;
			i--;
		}
	}
}

void CNodeArray::ReleaseAll()
{
	for (int i = 0; i < m_length; i++)
	{
		if (m_rgpNode[i] != NULL)
			m_rgpNode[i]->Release();
	}
	RemoveAll();
}

void CNodeArray::RemoveAll()
{
	m_length = 0;
	m_nAlloc = 0;
	delete[] m_rgpNode;
	m_rgpNode = NULL;
}

void CNodeArray::Allocate(int nLength)
{
	if (m_nAlloc < nLength)
	{
		CNode** rgp = new CNode*[nLength];
		if (m_rgpNode != NULL)
		{
			CopyMemory(rgp, m_rgpNode, m_nAlloc * sizeof(CNode*));
			delete[] m_rgpNode;
		}
		m_rgpNode = rgp;
		m_nAlloc = nLength;
	}
}

void CNodeArray::SetLength(int nLength)
{
	Allocate(nLength);
	if (nLength > m_length)
		ZeroMemory(m_rgpNode + m_length, (nLength - m_length) * sizeof(CNode*));
	m_length = nLength;
}

// =========================================================================
// Section 8, CTimeDepNode: timer-driven base node.
// =========================================================================

IMPLEMENT_NODE("TimeDepNode", CTimeDepNode, CNode)

START_NODE_PROPS(CTimeDepNode, CNode)
	NODE_PROP(pt_boolean, CTimeDepNode, loop)
	NODE_PROP(pt_number, CTimeDepNode, startTime)
	NODE_PROP(pt_number, CTimeDepNode, stopTime)
	NODE_PROP(pt_boolean, CTimeDepNode, isActive)
END_NODE_PROPS()

CTimeDepNode::CTimeDepNode()
	: m_loop(false), m_startTime(0), m_stopTime(0), m_isActive(false)
{
	m_lastStartTime = 0.0f;
}

void CTimeDepNode::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_isActive)
	{
		if (m_stopTime > m_startTime && TheseusGetNow() >= m_stopTime)
		{
			m_isActive = false;
			OnIsActiveChanged();
		}
	}
	else
	{
		if (TheseusGetNow() >= m_startTime && m_startTime != m_lastStartTime &&
			(m_stopTime == 0.0f || TheseusGetNow() < m_stopTime))
		{
			m_lastStartTime = m_startTime;
			m_isActive = true;
			OnIsActiveChanged();
		}
	}
}

void CTimeDepNode::OnIsActiveChanged() {}
void CTimeDepNode::OnCycleEnded() {}

// =========================================================================
// Section 9, CClass: scripted class definition and loading.
// =========================================================================

CClass::CClass() : CNodeClass(_T("[class]"), 0, NULL, NULL, NULL)
{
	m_obj = objClass;
	m_url = NULL;
	m_nVarCount = 0;
	m_constructor = NULL;
}

CClass::~CClass()
{
	delete[] m_url;
	if (m_constructor != NULL)
		m_constructor->Release();
}

CNode* CClass::CreateNode()
{
	return new CInstance(this);
}

bool CClass::Load(const TCHAR* szURL)
{
	CDirPush dirPush(szURL);

	int cch = _tcslen(szURL) + 1;
	delete m_url;
	m_url = new TCHAR[cch];
	CopyChars(m_url, szURL, cch);

	if (!m_file.Fetch(szURL))
		return false;

#ifdef _UNICODE
	m_file.MakeUnicode();
#endif

	BYTE* pbContent = m_file.DetachContent();
	bool fParse = ParseFile(szURL, (const TCHAR*)pbContent);
	TheseusFreeMemory(pbContent);
	return fParse;
}

bool CClass::LoadAbsURL(const TCHAR* szURL)
{
	ASSERT(szURL[0] && szURL[1] == ':');

	int cch = _tcslen(szURL) + 1;
	delete m_url;
	m_url = new TCHAR[cch];
	CopyChars(m_url, szURL, cch);

	if (!m_file.Fetch(szURL))
		return false;

#ifdef _UNICODE
	m_file.MakeUnicode();
#endif

	BYTE* pbContent = m_file.DetachContent();
	bool fParse = ParseFile(szURL, (const TCHAR*)pbContent);
	TheseusFreeMemory(pbContent);
	return fParse;
}

bool CClass::ParseFile(const TCHAR* szFileName, const TCHAR* szFile)
{
	StartParse(szFile, szFileName);
	const TCHAR* pch = ParseClassBody(szFile);
	if (*pch != '\0')
		SyntaxError(_T("Stuff past expected end of file!"));
	EndParse();
	return !g_bParseError;
}

const TCHAR* CClass::ParseClassBody(const TCHAR* pch)
{
	CClassCompiler constructor(this);
	pch = constructor.Compile(pch);
	m_constructor = constructor.CreateFunction();
	return pch;
}

// =========================================================================
// Section 10, CInstance: runtime instance of a scripted class.
// =========================================================================

CInstance::CInstance(CClass* pClass)
{
	m_obj = objInstance;
	m_class = pClass;
	m_class->AddRef();
	m_vars.SetLength(pClass->GetVariableCount());
	Construct();
	OnLoad();
}

CInstance::~CInstance()
{
	m_class->Release();
}

CNodeClass* CInstance::GetNodeClass() const
{
	return m_class;
}

void CInstance::Render()
{
	CGroup::Render();
}

CObject* CInstance::Dot(CObject* pObj)
{
	if (pObj->m_obj == objVariable)
	{
		CVarObject* pVar = (CVarObject*)pObj;

		if (m_class->m_members != NULL)
		{
			CObject* pLookup = m_class->m_members->Lookup(pVar->m_text, pVar->m_length);
			if (pLookup != NULL)
			{
				if (pLookup->m_obj == objMember)
					return new CMemberVarObject(this, ((CMember*)pLookup)->m_memberIndex);
				if (pLookup->m_obj == objFunction)
					return new CMemberFunctionObject(this, (CFunction*)pLookup);
			}
		}
	}
	return CNode::Dot(pObj);
}

// CInstance::Construct: build the scene graph from compiled bytecode.
static inline int FetchIntFromBytecode(BYTE*& pop)
{
	int n;
	CopyMemory(&n, pop, sizeof(int));
	pop += sizeof(int);
	return n;
}

BYTE* CInstance::Construct()
{
	BYTE* pop = m_class->m_constructor->m_rgop;

	if (pop == NULL)
		return pop;

	int nNodeStack = 0;
	CNode* nodeStack[100];

	int nNodeArrayStack = 0;
	CNodeArray* nodeArrayStack[100];

	CNode* pNode = NULL;
	CNodeArray* pNodeArray = &m_children;

	int nDefNextNodeVar = -1;

	for (;;)
	{
		BYTE op = *pop++;

		switch (op)
		{
		default:
			// Done constructing
			return pop - 1;

		case opNewNode:
			{
				int cch = FetchIntFromBytecode(pop);
				const TCHAR* pch = (const TCHAR*)pop;
				pop += cch * sizeof(TCHAR);

				nodeStack[nNodeStack] = pNode;
				nNodeStack += 1;

				pNode = (CNode*)CreateNewObject(pch, cch);
				pNode->m_parent = this;
				pNodeArray->AddNode(pNode);

				if (nDefNextNodeVar != -1)
				{
					pNode->AddRef();
					m_vars.SetNode(nDefNextNodeVar, pNode);
					nDefNextNodeVar = -1;
				}
			}
			break;

		case opNewNodeProp:
			{
				PRD prd;
				CopyMemory(&prd, pop, sizeof(PRD));
				pop += sizeof(PRD);

				if (*pop == opUseNode)
				{
					pop += 1;

					int nVar = FetchIntFromBytecode(pop);

					CNode* pNodeT = m_vars.GetNode(nVar);
					pNode->SetProperty(&prd, &pNodeT, sizeof(CNode*));
					break;
				}

				if (*pop == opDefNode)
				{
					pop += 1;

					int nVar = FetchIntFromBytecode(pop);

					nDefNextNodeVar = nVar;
				}

				ASSERT(*pop == opNewNode);
				pop += 1;

				int cch = FetchIntFromBytecode(pop);
				const TCHAR* pch = (const TCHAR*)pop;
				pop += cch * sizeof(TCHAR);

				nodeStack[nNodeStack] = pNode;
				nNodeStack += 1;

				pNode = (CNode*)CreateNewObject(pch, cch);
				pNode->m_parent = this;
				nodeStack[nNodeStack - 1]->SetProperty(&prd, &pNode, sizeof(CNode*));

				// Adjust for reference added by SetProperty...
				ASSERT(pNode->m_nRefCount == 2);
				pNode->Release();

				if (nDefNextNodeVar != -1)
				{
					pNode->AddRef();
					m_vars.SetNode(nDefNextNodeVar, pNode);
					nDefNextNodeVar = -1;
				}
			}
			break;

		case opDefNode:
			{
				int nVar = FetchIntFromBytecode(pop);

				nDefNextNodeVar = nVar;
			}
			break;

		case opUseNode:
			{
				int nVar = FetchIntFromBytecode(pop);

				CNode* pNode = m_vars.GetNode(nVar);
				pNode->AddRef();
				pNodeArray->AddNode(pNode);
			}
			break;

		case opEndNode:
			ASSERT(nNodeStack > 0);

			pNode->OnLoad();

			nNodeStack -= 1;
			pNode = nodeStack[nNodeStack];
			break;

		case opInitProp:
			{
				PRD prd;
				CopyMemory(&prd, pop, sizeof(PRD));
				pop += sizeof(PRD);

				int cbProp = FetchIntFromBytecode(pop);
				const void* pValue = pop;
				pop += cbProp;

				ASSERT(pNode != NULL);

				TCHAR szBuf[1024];
				const TCHAR* sz;
				if (prd.nType == pt_string)
				{
					CopyChars(szBuf, pValue, cbProp);
					szBuf[cbProp] = 0;
					sz = szBuf;
					pValue = &sz;
#ifdef _UNICODE
					pop += cbProp;
#endif
				}

				pNode->SetProperty(&prd, pValue, cbProp);
			}
			break;

		case opInitArray:
			{
				int nProp = FetchIntFromBytecode(pop);

				nodeArrayStack[nNodeArrayStack] = pNodeArray;
				nNodeArrayStack += 1;

				pNodeArray = (CNodeArray*)((BYTE*)pNode + nProp);
			}
			break;

		case opEndArray:
			ASSERT(nNodeArrayStack > 0);

			nNodeArrayStack -= 1;
			pNodeArray = nodeArrayStack[nNodeArrayStack];
			break;

		case opFunction:
			{
				int cch = FetchIntFromBytecode(pop);
				const TCHAR* pch = (const TCHAR*)pop;
				pop += cch * sizeof(TCHAR);
				int nFunction = FetchIntFromBytecode(pop);

				CObject* pFun = m_class->m_instanceFunctions.GetNode(nFunction);
				pFun->AddRef();
				pNode->SetMember(pch, cch, pFun);
			}
			break;
		}
	}
}
