// settings_nodes.cpp: the Settings node (one section of an INI style file,
// readable as settings.Name from script) and the ScreenSaver idle timer.
// Rebuilt from the 5960 retail XBE (0x400f0..0x41bc0); findings are in
// docs/decomp/SettingsNodes.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "settingsfile.h"


// ===== class layouts ========================================================
// CSettings: 0x30 bytes, vtable 0x00025f70. file 0x1c, section 0x20,
// settings file 0x24.
// CSetting: 0x1c bytes, vtable 0x00025f38. settings 0x14, name 0x18.

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

	bool OpenIfStale();

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

// FND table: GetValue (0x00182010, sig 5), SetValue (0x00182028, sig 13).
// Reload is a Theseus addition.
#define _FND_CLASS CSettings
START_NODE_FUN(CSettings, CNode)
	NODE_FUN_SS(GetValue)
	NODE_FUN_VSS(SetValue)
	NODE_FUN_VV(Reload)
END_NODE_FUN()
#undef _FND_CLASS


// ===== CSettings ============================================================

// CSettings::CSettings() / ~CSettings()
// Reconstructed from 5960 retail XBE @ 0x000418a0 (factory) and 0x00041920:
//     file = section = NULL; settings file empty;
//     ...   free(file); free(section); settings file Close();
// The settings file member closes itself on the way out.
CSettings::CSettings()
{
	m_file = NULL;
	m_section = NULL;
}

CSettings::~CSettings()
{
	delete [] m_file;
	delete [] m_section;
}

// Both script accessors start the same way in the binary: if no file is open
// or the open one isn't `file` (case blind), close and open `file`.
bool CSettings::OpenIfStale()
{
	const TCHAR* szOpen = m_sfile.GetFileName();
	if (szOpen != NULL && _tcsicmp(szOpen, m_file) == 0)
		return true;

	m_sfile.Close();
	return m_sfile.Open(m_file);
}

// CSettings::Dot(pObj)
// Reconstructed from 5960 retail XBE @ 0x00041950. Ghidra decompile:
//     r = base Dot(obj); if (r) return r;
//     if (obj->type != 4) return NULL;
//     s = new CSetting; s->settings = this; s->name = copy of obj->name;
//     return s;
// Any name that isn't a prop or function becomes a value in the section.
CObject* CSettings::Dot(CObject* pObj)
{
	CObject* pFound = CNode::Dot(pObj);
	if (pFound != NULL)
		return pFound;

	if (pObj->m_obj != objVariable)
		return NULL;

	const TCHAR* szName = ((CVarObject*)pObj)->m_text;
	int cch = (int)_tcslen(szName) + 1;

	CSetting* pSetting = new CSetting;
	pSetting->m_pSettings = this;
	pSetting->m_textName = new TCHAR [cch];
	CopyChars(pSetting->m_textName, szName, cch);
	return pSetting;
}

// CSettings::Reload()
// Theseus addition, see the declaration.
void CSettings::Reload()
{
	m_sfile.Close();
	if (m_file != NULL)
		m_sfile.Open(m_file);
}

// CSettings::SetValue(szName, szValue)   [script fn 'SetValue', FND 0x00182028, sig 13]
// Reconstructed from 5960 retail XBE @ 0x000419e0. Ghidra decompile:
//     reopen if stale (see above);
//     sec = file.FindSection(section, create = 1);
//     if (sec->SetValue(name, value)) { file.dirty = 1; file.Save(); }
// The settings file's SetValue does the find, compare and save in one.
void CSettings::SetValue(const TCHAR* szName, const TCHAR* szValue)
{
	OpenIfStale();
	m_sfile.SetValue(m_section, szName, szValue);
}

// CSettings::GetValue(szName)   [script fn 'GetValue', FND 0x00182010, sig 5]
// Reconstructed from 5960 retail XBE @ 0x00041a50. Ghidra decompile:
//     if (!reopen if stale) return new CStrObject(L"");
//     TCHAR buf[0x400]; file.GetValue(section, name, buf, 0x400);
//     return new CStrObject(buf);
CStrObject* CSettings::GetValue(const TCHAR* szName)
{
	if (!OpenIfStale())
		return new CStrObject(_T(""));

	TCHAR szValue[0x400];
	m_sfile.GetValue(m_section, szName, szValue, countof(szValue));
	return new CStrObject(szValue);
}


// ===== CSetting =============================================================

// CSetting::CSetting() / ~CSetting()
// Reconstructed from 5960 retail XBE @ 0x00041950 (inlined) and 0x00041b60:
// both pointers start NULL; the destructor frees only the name.
CSetting::CSetting()
{
	m_pSettings = NULL;
	m_textName = NULL;
}

CSetting::~CSetting()
{
	delete [] m_textName;
}

// CSetting::Assign(pObject)
// Reconstructed from 5960 retail XBE @ 0x00041b80. Ghidra decompile:
//     s = obj->Deref()->ToStr();
//     settings->SetValue(name, s->text ? s->text : L"");
// So settings.Name = x writes x as text straight to the file.
void CSetting::Assign(CObject* pObject)
{
	CStrObject* pText = pObject->Deref()->ToStr();
	m_pSettings->SetValue(m_textName, pText->GetSz());
}

// CSetting::ToStr()
// Reconstructed from 5960 retail XBE @ 0x00041bb0 (vtable 0x00025f38 slot 8):
//     return settings->GetValue(name);
CStrObject* CSetting::ToStr()
{
	return m_pSettings->GetValue(m_textName);
}

// CSetting::Deref()
// Reconstructed from 5960 retail XBE @ 0x00041bc0. Ghidra decompile:
//     v = settings->GetValue(name); this->Release(); return v;
CObject* CSetting::Deref()
{
	CStrObject* pValue = m_pSettings->GetValue(m_textName);
	Release();
	return pValue;
}


// ===== CScreenSaver =========================================================
// 0x38 bytes, factory 0x000400f0, vtable 0x00025758. Field offsets:
// enabled 0x1c, isActive 0x1d, isActive2 0x1e, isActive3 0x1f, delay 0x20,
// delay2 0x24, delay3 0x28, last input 0x30 (double). The live one is the
// global at 0x0017e738.

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

// FND table at 0x00181f50: reset (sig 1). StartAfter and SetDelay2/3 are
// Theseus additions.
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

// CScreenSaver::CScreenSaver()
// Reconstructed from 5960 retail XBE @ 0x000400f0 (factory). Ghidra decompile:
//     delay = 300.0; enabled = 1; isActive = isActive2 = isActive3 = 0;
//     delay2 = delay3 = 0; lastInput = now; g_pScreenSaver = this;
// The newest one built is the one input resets.
CScreenSaver::CScreenSaver()
{
	m_enabled = true;
	m_isActive = false;
	m_isActive2 = false;
	m_isActive3 = false;
	m_delay = 300.0f;
	m_delay2 = 0.0f;
	m_delay3 = 0.0f;
	m_timeOfLastEvent = TheseusGetNow();
	c_pTheScreenSaver = this;
}

// CScreenSaver::~CScreenSaver()
// Reconstructed from 5960 retail XBE @ 0x00040190: clears the global if it's us.
CScreenSaver::~CScreenSaver()
{
	if (c_pTheScreenSaver == this)
		c_pTheScreenSaver = NULL;
}

// Theseus additions: script setters for the three delays.
void CScreenSaver::StartAfter(float nTime)
{
	m_delay = nTime;
}

void CScreenSaver::SetDelay2(float nTime)
{
	m_delay2 = nTime;
}

void CScreenSaver::SetDelay3(float nTime)
{
	m_delay3 = nTime;
}

// CScreenSaver::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x000401b0. Ghidra decompile:
//     CNode::Advance(secs); now = g_now;
//     if (!enabled && isActive) { isActive = 0; CallFunction(this, L"OnEnd"); }
//     if (enabled && !isActive && lastInput + delay <= now)
//         { isActive = 1; CallFunction(this, L"OnStart"); }
//     if (0 < delay2 && !isActive2 && lastInput + delay2 <= now)
//         { isActive2 = 1; CallFunction(this, L"OnDelay2"); }
//     delay3 alike with OnDelay3.
// The two extra stages fire even when the saver itself is disabled.
void CScreenSaver::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	XTIME now = TheseusGetNow();

	if (!m_enabled)
	{
		if (m_isActive)
		{
			m_isActive = false;
			CallFunction(this, _T("OnEnd"));
		}
	}
	else if (!m_isActive && m_timeOfLastEvent + m_delay <= now)
	{
		m_isActive = true;
		CallFunction(this, _T("OnStart"));
	}

	if (m_delay2 > 0.0f && !m_isActive2 && m_timeOfLastEvent + m_delay2 <= now)
	{
		m_isActive2 = true;
		CallFunction(this, _T("OnDelay2"));
	}

	if (m_delay3 > 0.0f && !m_isActive3 && m_timeOfLastEvent + m_delay3 <= now)
	{
		m_isActive3 = true;
		CallFunction(this, _T("OnDelay3"));
	}
}

// CScreenSaver::reset()   [script fn 'reset', FND 0x00181f50, sig 1]
// Reconstructed from 5960 retail XBE @ 0x000402a0. Ghidra decompile:
//     if (isActive) { isActive = 0; CallFunction(this, L"OnEnd"); }
//     isActive2 = isActive3 = 0; lastInput = now;
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

// CScreenSaver::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x000402d0. Ghidra decompile:
//     if (prd->offset == 0x1c) { if (*(bool*)v) reset();  enabled = *(bool*)v; }
//     return 1;
// Turning the saver on counts as fresh input.
bool CScreenSaver::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) != offsetof(m_enabled))
		return true;

	bool bOn = *(const bool*)pvValue;
	if (bOn)
		reset();
	m_enabled = bOn;
	return true;
}

// ResetScreenSaver()
// Reconstructed from 5960 retail XBE @ 0x00040320. Ghidra decompile:
//     g_lastInput = now;
//     if (!g_pScreenSaver) return 0;
//     was = g_pScreenSaver->isActive; g_pScreenSaver->reset();
//     XAutoPowerDownResetTimer(); return was;
// Called on every controller or keyboard event; true means the event only
// woke the screen.
bool ResetScreenSaver()
{
	g_timeOfLastEvent = TheseusGetNow();

	CScreenSaver* pSaver = CScreenSaver::c_pTheScreenSaver;
	if (pSaver == NULL)
		return false;

	bool bWasActive = pSaver->m_isActive;
	pSaver->reset();

#ifdef _XBOX
	XAutoPowerDownResetTimer();
#endif
	return bWasActive;
}
