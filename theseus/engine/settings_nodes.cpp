// settings_nodes.cpp: CSettings INI reader / writer plus the screensaver
// idle timer. Decompiled from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "settingsfile.h"

// CSettings: INI file reader / writer exposed to XAP scripts.

class CSetting;

class CSettings : public CNode
{
	DECLARE_NODE(CSettings, CNode)
public:
	CSettings();
	~CSettings();

	CObject* Dot(CObject* pObject);

	TCHAR* m_file;
	TCHAR* m_section;

	void SetValue(const TCHAR* szName, const TCHAR* szValue);
	CStrObject* GetValue(const TCHAR* szName);
	// Drop the in-memory copy and re-read from disk. Use this after a
	// native-side writer (e.g. theTitleScanner) has rewritten the file
	// out from under us; otherwise GetValue keeps serving stale cached
	// data until something else evicts m_sfile.
	void Reload();

protected:
	CSettingsFile m_sfile;

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

class CSetting : public CObject
{
public:
	CSetting();
	~CSetting();

	void Assign(CObject* pObject);
	CStrObject* ToStr();
	CObject* Deref();

	CSettings* m_pSettings;
	TCHAR* m_textName;
};

IMPLEMENT_NODE("Settings", CSettings, CNode)

START_NODE_PROPS(CSettings, CNode)
	NODE_PROP(pt_string, CSettings, file)
	NODE_PROP(pt_string, CSettings, section)
END_NODE_PROPS()

#define _FND_CLASS CSettings
START_NODE_FUN(CSettings, CNode)
	NODE_FUN_SS(GetValue)
	NODE_FUN_VSS(SetValue)
	NODE_FUN_VV(Reload)
END_NODE_FUN()
#undef _FND_CLASS

CSettings::CSettings()  { m_file = NULL; m_section = NULL; }
CSettings::~CSettings() { delete[] m_file; delete[] m_section; }

CObject* CSettings::Dot(CObject* pObj)
{
	CObject* pResult = CNode::Dot(pObj);
	if (pResult != NULL)
		return pResult;

	if (pObj->m_obj == objVariable)
	{
		CVarObject* pVar = (CVarObject*)pObj;
		CSetting* pSetting = new CSetting;
		pSetting->m_pSettings = this;
		int cch = _tcslen(pVar->m_text) + 1;
		pSetting->m_textName = new TCHAR[cch];
		CopyChars(pSetting->m_textName, pVar->m_text, cch);
		return pSetting;
	}

	return NULL;
}

void CSettings::Reload()
{
	m_sfile.Close();
	if (m_file != NULL)
		m_sfile.Open(m_file);
}

void CSettings::SetValue(const TCHAR* szName, const TCHAR* szValue)
{
	if (m_sfile.GetFileName() == NULL || _tcsicmp(m_sfile.GetFileName(), m_file) != 0)
	{
		m_sfile.Close();
		m_sfile.Open(m_file);
	}
	m_sfile.SetValue(m_section, szName, szValue);
}

CStrObject* CSettings::GetValue(const TCHAR* szName)
{
	if (m_sfile.GetFileName() == NULL || _tcsicmp(m_sfile.GetFileName(), m_file) != 0)
	{
		m_sfile.Close();
		if (!m_sfile.Open(m_file))
			return new CStrObject(_T(""));
	}
	TCHAR szBuf[1024];
	m_sfile.GetValue(m_section, szName, szBuf, countof(szBuf));
	return new CStrObject(szBuf);
}

CSetting::CSetting()  { m_pSettings = NULL; m_textName = NULL; }
CSetting::~CSetting() { delete[] m_textName; }

void CSetting::Assign(CObject* pObject)
{
	CStrObject* pStr = pObject->Deref()->ToStr();
	m_pSettings->SetValue(m_textName, pStr->GetSz());
}

CStrObject* CSetting::ToStr()
{
	return m_pSettings->GetValue(m_textName);
}

CObject* CSetting::Deref()
{
	CObject* pObject = m_pSettings->GetValue(m_textName);
	Release();
	return pObject;
}

// =========================================================================
// CScreenSaver -- idle timer with multi-stage delay callbacks
// =========================================================================

class CScreenSaver : public CNode
{
	DECLARE_NODE(CScreenSaver, CNode)
public:
	CScreenSaver();
	~CScreenSaver();

	void StartAfter(float nTime);
	void SetDelay2(float nTime);
	void SetDelay3(float nTime);

	bool  m_enabled;
	bool  m_isActive;
	bool  m_isActive2;
	bool  m_isActive3;
	float m_delay;
	float m_delay2;
	float m_delay3;
	float m_shortDelay;

	void reset();
	void Advance(float nSeconds);

	XTIME m_timeOfLastEvent;

	static CScreenSaver* c_pTheScreenSaver;
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("ScreenSaver", CScreenSaver, CNode)

START_NODE_PROPS(CScreenSaver, CNode)
	NODE_PROP(pt_boolean, CScreenSaver, enabled)
	NODE_PROP(pt_boolean, CScreenSaver, isActive)
	NODE_PROP(pt_boolean, CScreenSaver, isActive2)
	NODE_PROP(pt_boolean, CScreenSaver, isActive3)
	NODE_PROP(pt_number, CScreenSaver, delay)
	NODE_PROP(pt_number, CScreenSaver, delay2)
	NODE_PROP(pt_number, CScreenSaver, delay3)
END_NODE_PROPS()

#define _FND_CLASS CScreenSaver
START_NODE_FUN(CScreenSaver, CNode)
	NODE_FUN_VV(reset)
	NODE_FUN_VN(StartAfter)
	NODE_FUN_VN(SetDelay2)
	NODE_FUN_VN(SetDelay3)
END_NODE_FUN()
#undef _FND_CLASS

CScreenSaver* CScreenSaver::c_pTheScreenSaver = NULL;
XTIME g_timeOfLastEvent;

CScreenSaver::CScreenSaver()
	: m_enabled(true), m_isActive(false), m_isActive2(false), m_isActive3(false),
	  m_delay(300.0f), m_delay2(0.0f), m_delay3(0.0f)
{
	c_pTheScreenSaver = this;
	m_timeOfLastEvent = TheseusGetNow();
}

CScreenSaver::~CScreenSaver()
{
	if (this == c_pTheScreenSaver)
		c_pTheScreenSaver = NULL;
}

void CScreenSaver::StartAfter(float nTime) { m_delay = nTime; }
void CScreenSaver::SetDelay2(float nTime)  { m_delay2 = nTime; }
void CScreenSaver::SetDelay3(float nTime)  { m_delay3 = nTime; }

void CScreenSaver::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	XTIME now = TheseusGetNow();

	if (!m_enabled && m_isActive)
	{
		m_isActive = false;
		CallFunction(this, _T("OnEnd"));
	}

	if (m_enabled && !m_isActive && now >= m_timeOfLastEvent + m_delay)
	{
		m_isActive = true;
		CallFunction(this, _T("OnStart"));
	}

	if (m_delay2 > 0.0f && !m_isActive2 && now >= m_timeOfLastEvent + m_delay2)
	{
		m_isActive2 = true;
		CallFunction(this, _T("OnDelay2"));
	}

	if (m_delay3 > 0.0f && !m_isActive3 && now >= m_timeOfLastEvent + m_delay3)
	{
		m_isActive3 = true;
		CallFunction(this, _T("OnDelay3"));
	}
}

void CScreenSaver::reset()
{
	if (m_isActive)
	{
		m_isActive = false;
		CallFunction(this, _T("OnEnd"));
	}
	m_isActive2 = false;
	m_isActive3 = false;
	m_timeOfLastEvent = TheseusGetNow();
}

bool CScreenSaver::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_enabled))
	{
		if (*(bool*)pvValue)
			reset();
		m_enabled = *(bool*)pvValue;
	}
	return true;
}

bool ResetScreenSaver()
{
	g_timeOfLastEvent = TheseusGetNow();

	if (CScreenSaver::c_pTheScreenSaver != NULL)
	{
		bool bRet = CScreenSaver::c_pTheScreenSaver->m_isActive;
		CScreenSaver::c_pTheScreenSaver->reset();

#ifdef _XBOX
		XAutoPowerDownResetTimer();
#endif
		return bRet;
	}

	return false;
}
