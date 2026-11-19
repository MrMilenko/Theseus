// node.h: the object model every scene node and script value sits on.
// Reflection tables (PRD for properties, FND for script functions), the
// CObject / CNode bases, groups, script classes and instances. Bodies live
// in render/node_system.cpp; the binary write-up is docs/decomp/Node.md.
//
// Layouts below are checked against the 5960 retail XBE's constructors and
// factories (sizes are the operator new argument, offsets are the stores):
//   CObject 0x14: vtbl, m_nRefCount 0x04, m_obj 0x08, m_parent 0x0c, m_members 0x10
//   CNode 0x1c: m_behavior 0x14, m_visible 0x18
//   CGroup 0x44: m_children 0x1c, m_bboxCenter 0x28, m_bboxSize 0x34, flags 0x40/0x41
//   CInstance 0x54: m_class 0x44, m_vars 0x48
//   CTimeDepNode 0x30: m_loop 0x1c, m_startTime 0x20, m_stopTime 0x24,
//     m_isActive 0x28, m_lastStartTime 0x2c
//   CNodeClass 0x2c: name 0x14, size 0x18, factory 0x1c, base 0x20, props 0x24, next 0x28
//   CClass: m_url 0x2c, m_file 0x30, m_nVarCount 0x68
//   CProperty: m_node 0x14, m_pprd 0x18
//   CNodeArray: length 0, alloc 4, pointers 8. Value arrays: alloc 0, size 4, data 8.
// The CObject vtable order matches the binary through Deref (slot 9, +0x24):
// dtor, GetPropMap, OnSetProperty, AddRef, Release, GetFunctionMap,
// GetNodeClass, ToNum, ToStr, Deref. CFunction carries m_name, which 5960
// doesn't have (its code size sits at 0x14).

#pragma once

#include "activefile.h"

// MSVC x64 gives member pointers of a forward-declared class the 24 byte
// "unknown inheritance" form, and the FND casts only fill 8 of it. Pin both.
#ifdef _MSC_VER
class __single_inheritance CObject;
class __single_inheritance CNode;
#else
class CObject;
class CNode;
#endif
class CNodeClass;
class CNumObject;
class CStrObject;
class CSurfx;
class CFunction;
class CRunner;
struct TAGID;

// What kind of value a CObject is (m_obj). Scripts and the VM switch on these.
enum
{
	objUndefined,
	objNull,
	objNumber,
	objString,
	objVariable,
	objNode,
	objClass,
	objNodeArray,
	objFunctionRef,
	objMember,
	objMemberVar,
	objInstance,
	objUse,
	objFunction,
	objMemberFunction,
	objArray,
	objVec3
};

// Storage kind of a reflected property. The parser picks its reader by this.
enum PROP_TYPE
{
	pt_null,
	pt_integer,
	pt_boolean,
	pt_number,
	pt_children,
	pt_vec3,
	pt_vec4,
	pt_quaternion,
	pt_color,
	pt_node,
	pt_string,
	pt_nodearray,
	pt_intarray,
	pt_numarray,
	pt_vec2array,
	pt_vec3array,
	pt_vec4array
};

// One property: where it lives in the object, its kind and its script name.
// Entry 0 of every table points at the base class table instead.
struct PRD
{
	const void* pbOffset;
	PROP_TYPE nType;
	const TCHAR* szName;
};

// Signature ids are the case numbers in CObject::Call. Letters: return kind
// first, then the arguments (v void, i int, n float, s string, o object).
// 24 and up are ones Theseus added.
enum
{
	sig_default,
	sig_vv,
	sig_iv,
	sig_ii,
	sig_si,
	sig_ss,
	sig_isi,
	sig_sii,
	sig_sv,
	sig_nv,
	sig_nn,
	sig_nnn,
	sig_vs,
	sig_vss,
	sig_vi,
	sig_ov,
	sig_os,
	sig_oo,
	sig_vii,
	sig_vn,
	sig_vnn,
	sig_vo,
	sig_vnnn,
	sig_vnnnn,
	sig_iii,
	sig_is,
	sig_viis,
	sig_ni,
	sig_vis,
	sig_iis
};

// Member pointer shapes, one per signature. The table stores them all as NODE_PFN.
typedef CObject* (__cdecl CObject::*NODE_PFN)(void);
typedef CObject* (CObject::*NODEFN_default)(CObject** rgparam, int nParam);
typedef void (CObject::*NODEFN_vv)(void);
typedef void (CObject::*NODEFN_vi)(int);
typedef void (CObject::*NODEFN_vii)(int, int);
typedef void (CObject::*NODEFN_vis)(int, const TCHAR*);
typedef void (CObject::*NODEFN_viis)(int, int, const TCHAR*);
typedef void (CObject::*NODEFN_vs)(const TCHAR*);
typedef void (CObject::*NODEFN_vss)(const TCHAR*, const TCHAR*);
typedef void (CObject::*NODEFN_vn)(float);
typedef void (CObject::*NODEFN_vnn)(float, float);
typedef void (CObject::*NODEFN_vnnn)(float, float, float);
typedef void (CObject::*NODEFN_vnnnn)(float, float, float, float);
typedef void (CObject::*NODEFN_vo)(CObject*);
typedef int (CObject::*NODEFN_iv)(void);
typedef int (CObject::*NODEFN_ii)(int);
typedef int (CObject::*NODEFN_iii)(int, int);
typedef int (CObject::*NODEFN_is)(const TCHAR*);
typedef int (CObject::*NODEFN_isi)(const TCHAR*, int);
typedef int (CObject::*NODEFN_iis)(int, const TCHAR*);
typedef float (CObject::*NODEFN_nv)(void);
typedef float (CObject::*NODEFN_ni)(int);
typedef float (CObject::*NODEFN_nn)(float);
typedef float (CObject::*NODEFN_nnn)(float, float);
typedef CStrObject* (CObject::*NODEFN_sv)(void);
typedef CStrObject* (CObject::*NODEFN_si)(int);
typedef CStrObject* (CObject::*NODEFN_sii)(int, int);
typedef CStrObject* (CObject::*NODEFN_ss)(const TCHAR*);
typedef CObject* (CObject::*NODEFN_ov)(void);
typedef CObject* (CObject::*NODEFN_os)(const TCHAR*);
typedef CObject* (CObject::*NODEFN_oo)(CObject*);

// pfn has to stay first: the tables brace-initialize through it.
union FSIG
{
	NODE_PFN pfn;

	NODEFN_default pfn_default;
	NODEFN_vv pfn_vv;
	NODEFN_vi pfn_vi;
	NODEFN_vii pfn_vii;
	NODEFN_vis pfn_vis;
	NODEFN_viis pfn_viis;
	NODEFN_vs pfn_vs;
	NODEFN_vss pfn_vss;
	NODEFN_vn pfn_vn;
	NODEFN_vnn pfn_vnn;
	NODEFN_vnnn pfn_vnnn;
	NODEFN_vnnnn pfn_vnnnn;
	NODEFN_vo pfn_vo;
	NODEFN_iv pfn_iv;
	NODEFN_ii pfn_ii;
	NODEFN_iii pfn_iii;
	NODEFN_is pfn_is;
	NODEFN_isi pfn_isi;
	NODEFN_iis pfn_iis;
	NODEFN_nv pfn_nv;
	NODEFN_ni pfn_ni;
	NODEFN_nn pfn_nn;
	NODEFN_nnn pfn_nnn;
	NODEFN_sv pfn_sv;
	NODEFN_si pfn_si;
	NODEFN_sii pfn_sii;
	NODEFN_ss pfn_ss;
	NODEFN_ov pfn_ov;
	NODEFN_os pfn_os;
	NODEFN_oo pfn_oo;
};

// One script-callable function: the method, its signature id and its name.
struct FND
{
	union FSIG pfn;
	WORD sig;
	const TCHAR* szName;
};

// FND tables. The .cpp defines _FND_CLASS to the class whose methods follow.
#define START_NODE_FUN(className, baseClassName) \
	FND className::c_rgfnd [] = {

#define END_NODE_FUN() \
	{ NULL, 0, NULL } };

#define NODE_FUN_TYPED(name, sigId, pfnType) \
	{ (NODE_PFN)(pfnType)&_FND_CLASS::name, sigId, _T(#name) },

#define NODE_FUN(name)			NODE_FUN_TYPED(name, sig_default, NODEFN_default)
#define NODE_FUN_VV(name)		NODE_FUN_TYPED(name, sig_vv, NODEFN_vv)
#define NODE_FUN_VI(name)		NODE_FUN_TYPED(name, sig_vi, NODEFN_vi)
#define NODE_FUN_VII(name)		NODE_FUN_TYPED(name, sig_vii, NODEFN_vii)
#define NODE_FUN_VIS(name)		NODE_FUN_TYPED(name, sig_vis, NODEFN_vis)
#define NODE_FUN_VIIS(name)		NODE_FUN_TYPED(name, sig_viis, NODEFN_viis)
#define NODE_FUN_VS(name)		NODE_FUN_TYPED(name, sig_vs, NODEFN_vs)
#define NODE_FUN_VSS(name)		NODE_FUN_TYPED(name, sig_vss, NODEFN_vss)
#define NODE_FUN_VN(name)		NODE_FUN_TYPED(name, sig_vn, NODEFN_vn)
#define NODE_FUN_VNN(name)		NODE_FUN_TYPED(name, sig_vnn, NODEFN_vnn)
#define NODE_FUN_VNNN(name)		NODE_FUN_TYPED(name, sig_vnnn, NODEFN_vnnn)
#define NODE_FUN_VNNNN(name)	NODE_FUN_TYPED(name, sig_vnnnn, NODEFN_vnnnn)
#define NODE_FUN_VO(name)		NODE_FUN_TYPED(name, sig_vo, NODEFN_vo)
#define NODE_FUN_IV(name)		NODE_FUN_TYPED(name, sig_iv, NODEFN_iv)
#define NODE_FUN_II(name)		NODE_FUN_TYPED(name, sig_ii, NODEFN_ii)
#define NODE_FUN_III(name)		NODE_FUN_TYPED(name, sig_iii, NODEFN_iii)
#define NODE_FUN_IS(name)		NODE_FUN_TYPED(name, sig_is, NODEFN_is)
#define NODE_FUN_IIS(name)		NODE_FUN_TYPED(name, sig_iis, NODEFN_iis)
#define NODE_FUN_NV(name)		NODE_FUN_TYPED(name, sig_nv, NODEFN_nv)
#define NODE_FUN_NI(name)		NODE_FUN_TYPED(name, sig_ni, NODEFN_ni)
#define NODE_FUN_NN(name)		NODE_FUN_TYPED(name, sig_nn, NODEFN_nn)
#define NODE_FUN_NNN(name)		NODE_FUN_TYPED(name, sig_nnn, NODEFN_nnn)
#define NODE_FUN_SV(name)		NODE_FUN_TYPED(name, sig_sv, NODEFN_sv)
#define NODE_FUN_SI(name)		NODE_FUN_TYPED(name, sig_si, NODEFN_si)
#define NODE_FUN_SII(name)		NODE_FUN_TYPED(name, sig_sii, NODEFN_sii)
#define NODE_FUN_SS(name)		NODE_FUN_TYPED(name, sig_ss, NODEFN_ss)
#define NODE_FUN_OV(name)		NODE_FUN_TYPED(name, sig_ov, NODEFN_ov)
#define NODE_FUN_OS(name)		NODE_FUN_TYPED(name, sig_os, NODEFN_os)
#define NODE_FUN_OO(name)		NODE_FUN_TYPED(name, sig_oo, NODEFN_oo)
// Tagged sig_is, so Call passes the string only. Nothing uses it yet.
#define NODE_FUN_ISI(name)		NODE_FUN_TYPED(name, sig_is, NODEFN_isi)

// PRD tables. Entry 0 chains to the base class; the null-object member
// address is how a constant initializer gets a field offset here.
#define START_NODE_PROPS(className, baseClassName) \
	PRD className::m_rgprd [] = { \
		{ &baseClassName::m_rgprd[0], pt_null, _T(#className) },

#define END_NODE_PROPS() \
	{ NULL, pt_null, NULL } };

#define NODE_PROP(theType, theClass, name) \
	{ &((theClass*)0)->m_##name, theType, _T(#name) },

// Per-class declarations. Each one leaves the class body in public:.
#define NODE_CLASS(className) (&className::class##className)

#define DECLARE_NODE(className, baseClassName) \
public: \
	static CNodeClass class##className; \
	static CObject* CreateNode(); \
	virtual CNodeClass* GetNodeClass() const;

#define DECLARE_NODE_PROPS() \
public: \
	static PRD m_rgprd []; \
	virtual const PRD* GetPropMap() const { return m_rgprd; }

#define DECLARE_NODE_FUNCTIONS() \
public: \
	static FND c_rgfnd []; \
	virtual FND* GetFunctionMap() const { return c_rgfnd; }

// The class object registers itself (CNodeClass ctor links it into the list).
#define IMPLEMENT_NODE(friendlyName, className, baseClassName) \
	CObject* className::CreateNode() \
	{ \
		return new className; \
	} \
	CNodeClass className::class##className(_T(friendlyName), sizeof (class className), \
		className::CreateNode, NODE_CLASS(baseClassName), className::m_rgprd); \
	CNodeClass* className::GetNodeClass() const \
	{ \
		return NODE_CLASS(className); \
	}

// Growable property arrays (MFInt32, MFFloat, MFVec2f/3f/4f). SetSize lives
// in node_system.cpp.
#define DECLARE_VALUE_ARRAY(className, elemType) \
	class className \
	{ \
	public: \
		className(); \
		~className(); \
		void SetSize(int nSize); \
		int m_nAlloc; \
		int m_nSize; \
		elemType* m_value; \
	};

DECLARE_VALUE_ARRAY(CIntArray, int)
DECLARE_VALUE_ARRAY(CNumArray, float)
DECLARE_VALUE_ARRAY(CVec2Array, D3DXVECTOR2)
DECLARE_VALUE_ARRAY(CVec3Array, D3DXVECTOR3)
DECLARE_VALUE_ARRAY(CVec4Array, D3DXVECTOR4)

struct BBox
{
	D3DXVECTOR3 center;
	D3DXVECTOR3 size;
};

// A DEF name and its node. The name is allocated inline past the struct.
struct CDefine
{
	CDefine* m_next;
	CNode* m_node;
	TCHAR m_name[ANYSIZE_ARRAY];

#pragma push_macro("new")
#undef new
	void* operator new(size_t cb, int cchName)
	{
		return ::operator new(cb + cchName * sizeof (TCHAR));
	}
#pragma pop_macro("new")
};

// DEF/USE names for one scope, newest first, searched front to back.
struct CNameSpace
{
	CNameSpace();
	~CNameSpace();

	bool Define(const TCHAR* name, int nameLen, CNode* node);
	CDefine* Add(const TCHAR* name, int nameLen);
	CDefine* Get(const TCHAR* name, int nameLen);
	CNode* Lookup(const TCHAR* name, int nameLen);
	CDefine* Lookup(CNode* node);

	CDefine* m_firstDefine;
};

// Children / MFNode storage. Out of range reads give NULL, writes are ignored.
class CNodeArray
{
public:
	CNodeArray();
	~CNodeArray();

	int GetLength() { return m_length; }

	CNode* GetNode(int iNode)
	{
		return (iNode >= 0 && iNode < m_length) ? m_rgpNode[iNode] : NULL;
	}

	void SetNode(int iNode, CNode* pNode)
	{
		if (iNode >= 0 && iNode < m_length)
			m_rgpNode[iNode] = pNode;
	}

	void AddNode(CNode* node);
	void RemoveNode(CNode* node);
	void RemoveAll();
	void ReleaseAll();
	void Allocate(int count);
	void SetLength(int count);

	void Sort(int (__cdecl *compare)(const void*, const void*))
	{
		qsort(m_rgpNode, m_length, sizeof (CNode*), compare);
	}

protected:
	int m_length;
	int m_nAlloc;
	CNode** m_rgpNode;
};

// Root of everything the VM touches: nodes, numbers, strings, classes.
class CObject
{
public:
	CObject();
	virtual ~CObject();

	static CNodeClass classCObject;
	static CObject* CreateNode();
	static PRD m_rgprd [];

	virtual const PRD* GetPropMap() const { return m_rgprd; }
	virtual bool OnSetProperty(const PRD* prop, const void* value);
	virtual void AddRef();
	virtual void Release();
	virtual FND* GetFunctionMap() const;
	virtual CNodeClass* GetNodeClass() const;
	virtual CNumObject* ToNum();
	virtual CStrObject* ToStr();
	virtual CObject* Deref();
	virtual void Assign(CObject* obj);
	virtual CObject* Call(int index, CObject** args, int argc);
	virtual CObject* Dot(CObject* obj);
	virtual CObject* GetMember(const TCHAR* name, int nameLen);
	virtual void Dump() const;

	const PRD* FindProp(const TCHAR* name, int nameLen);
	void SetProperty(const PRD* prop, const void* value, int size);
	int FindFunction(const TCHAR* funcName, int funcNameLen);
	bool IsKindOf(CNodeClass* cls) const;

	void AddMember(const TCHAR* name, int nameLen, CObject* obj);
	void SetMember(const TCHAR* name, int nameLen, CObject* obj);
	CFunction* FindMemberFunction(const TCHAR* name, int nameLen);

	int m_nRefCount;
	int m_obj;
	CObject* m_parent;
	CNameSpace* m_members;
};

// Runtime class record. Built-in node classes are static instances that
// link themselves into c_pFirstClass at startup.
class CNodeClass : public CObject
{
public:
	CNodeClass(const TCHAR* className, int objSize, CObject* (*factory)(), CNodeClass* base, const PRD* props);
	~CNodeClass();

	static CNodeClass* c_pFirstClass;
	static CNodeClass* FindByName(const TCHAR* className, int classNameLen);

	CObject* CreateNode();
	const PRD* FindProp(const TCHAR* propName, int propNameLen);
	const PRD* GetPropMap() const { return m_rgprd; }

	const TCHAR* m_className;
	int m_nObjectSize;
	CObject* (*m_pfnCreateNode)();
	CNodeClass* m_baseClass;
	const PRD* m_rgprd;
	CNodeClass* m_nextClass;
};

// Compiled script function: a name and its bytecode, allocated in one block.
class CFunction : public CObject
{
public:
	CFunction();
	~CFunction();

	TCHAR* m_name;
	int m_codeSize;
	BYTE m_rgop[ANYSIZE_ARRAY];

#pragma push_macro("new")
#undef new
	void* operator new(size_t cb, int cbCode)
	{
		return ::operator new(cb + cbCode);
	}
#pragma pop_macro("new")
};

// A script-defined class loaded from a .xap file.
class CClass : public CNodeClass
{
public:
	CClass();
	~CClass();

	bool Load(const TCHAR* url);
	bool LoadAbsURL(const TCHAR* url);
	bool ParseFile(const TCHAR* fileName, const TCHAR* text);
	const TCHAR* ParseClassBody(const TCHAR* src);
	CNode* CreateNode();

	int GetVariableCount() { return m_nVarCount; }

	TCHAR* m_url;
	CActiveFile m_file;
	CNodeArray m_instanceFunctions;
	CFunction* m_constructor;
	int m_nVarCount;
};

// Base of every scene node: a behavior script and a visible flag.
class CNode : public CObject
{
	DECLARE_NODE(CNode, CObject)
	DECLARE_NODE_PROPS()
public:
	CNode();
	virtual ~CNode();

	virtual void OnLoad();
	virtual void Advance(float dt);
	virtual void Render();
	virtual void GetBBox(BBox* box);
	virtual float GetRadius();
#ifdef _LIGHTS
	virtual void SetLight(int& lightIndex, D3DCOLORVALUE& ambient);
#endif
	virtual const DWORD* GetPalette();
	virtual void RenderDynamicTexture(CSurfx* surface);
	virtual LPDIRECT3DTEXTURE8 GetTextureSurface();

	CObject* Dot(CObject* obj);
	CStrObject* ToStr();
	void Dump() const;

	CRunner* m_behavior;
	bool m_visible;
};

class CGroup : public CNode
{
	DECLARE_NODE(CGroup, CNode)
	DECLARE_NODE_PROPS()
public:
	CGroup();
	~CGroup();

	void Advance(float dt);
	void Render();
	void GetBBox(BBox* box);
	float GetRadius();
#ifdef _LIGHTS
	void SetLight(int& lightIndex, D3DCOLORVALUE& ambient);
#endif
	void RenderDynamicTexture(CSurfx* surface);
	LPDIRECT3DTEXTURE8 GetTextureSurface();

	CNodeArray m_children;
	D3DXVECTOR3 m_bboxCenter;
	D3DXVECTOR3 m_bboxSize;
	bool m_bboxSpecified;
	bool m_bboxDirty;
};

// A live object of a script class: a group plus the class's variable slots.
class CInstance : public CGroup
{
public:
	CInstance(CClass* cls);
	~CInstance();

	CNodeClass* GetNodeClass() const;
	CObject* Dot(CObject* obj);
	void Render();
	BYTE* Construct();

	CClass* m_class;
	CNodeArray m_vars;
};

// Shared start/stop/loop clock for sensors and players.
class CTimeDepNode : public CNode
{
	DECLARE_NODE(CTimeDepNode, CNode)
	DECLARE_NODE_PROPS()
public:
	CTimeDepNode();

	void Advance(float dt);

	virtual void OnIsActiveChanged();
	virtual void OnCycleEnded();

	bool m_loop;
	float m_startTime;
	float m_stopTime;
	bool m_isActive;
	float m_lastStartTime;
};

// "node.prop" as a script value: reads and writes go through the PRD.
class CProperty : public CObject
{
public:
	CProperty(CObject* node, const PRD* prop);
	~CProperty();

	CObject* Deref();
	void Assign(CObject* obj);

	CObject* m_node;
	const PRD* m_pprd;
};

CObject* NewNode(const TCHAR* className, int classNameLen);
