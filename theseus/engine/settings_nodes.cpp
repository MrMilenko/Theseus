// settings_nodes.cpp: the Settings node, which gives scripts one section of
// an ini file as properties, and the ScreenSaver node, the idle timer that
// fires OnStart, OnDelay2 and OnDelay3. Rebuilt function by function from
// the 5960 retail XBE (0x400f0..0x40320, 0x418a0..0x41bc0); see
// docs/decomp/Settings.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "settingsfile.h"


// ===== Settings ============================================================

// 0x30 bytes in 5960: file 0x1c, section 0x20, then the open settings file.
class CSettings : public CNode
{
	DECLARE_NODE(CSettings, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CSettings();
	~CSettings();

	CObject* Dot(CObject* member);
	CStrObject* GetValue(const TCHAR* name);
	void SetValue(const TCHAR* name, const TCHAR* value);
	// Theseus: drops the cached file and reads it again, for when native
	// code (the title scanner) rewrote it underneath.
	void Reload();

	TCHAR* m_file;
	TCHAR* m_section;

protected:
	bool OpenCurrent();

	CSettingsFile m_store;
};

// What settings.<name> evaluates to: reading it fetches the value,
// assigning to it writes the value back (vtable at 0x00025f38).
class CSetting : public CObject
{
public:
	CSetting();
	~CSetting();

	CObject* Deref();
	void Assign(CObject* newValue);
	CStrObject* ToStr();

	CSettings* m_pSettings;
	TCHAR* m_textName;
};

IMPLEMENT_NODE("Settings", CSettings, CNode)

// PRD at 0x00174884.
START_NODE_PROPS(CSettings, CNode)
	NODE_PROP(pt_string, CSettings, file)
	NODE_PROP(pt_string, CSettings, section)
END_NODE_PROPS()

// FND at 0x00182010; Reload is Theseus's.
#define _FND_CLASS CSettings
START_NODE_FUN(CSettings, CNode)
	NODE_FUN_SS(GetValue)
	NODE_FUN_VSS(SetValue)
	NODE_FUN_VV(Reload)
END_NODE_FUN()
#undef _FND_CLASS

// CSettings::CSettings() / ~CSettings()
// Reconstructed from 5960 retail XBE @ 0x000418a0 (0x30 byte node, file,
// section and the settings file all empty) and @ 0x00041920 (frees the two
// strings; the settings file closes itself).
CSettings::CSettings()
{
	m_file = NULL;
	m_section = NULL;
}

CSettings::~CSettings()
{
	delete[] m_file;
	delete[] m_section;
}

// CSettings::Dot(member)
// Reconstructed from 5960 retail XBE @ 0x00041950. Anything the node itself
// answers comes first; any other name (a type 4 variable) becomes a
// CSetting carrying a copy of the name.
CObject* CSettings::Dot(CObject* member)
{
	CObject* own = CNode::Dot(member);
	if (own != NULL || member->m_kind != objVariable)
		return own;

	const TCHAR* name = ((CVarObject*)member)->m_text;
	CSetting* setting = new CSetting;
	setting->m_pSettings = this;
	setting->m_textName = new TCHAR[_tcslen(name) + 1];
	_tcscpy(setting->m_textName, name);
	return setting;
}

// Reopens the file when none is open or `file` names another one (compared
// ignoring case). Returns whether the reopen worked; true when no reopen was
// needed.
bool CSettings::OpenCurrent()
{
	if (m_store.GetFileName() != NULL && _tcsicmp(m_store.GetFileName(), m_file) == 0)
		return true;
	m_store.Close();
	return m_store.Open(m_file);
}

void CSettings::Reload()
{
	m_store.Close();
	if (m_file != NULL)
		m_store.Open(m_file);
}

// CSettings::SetValue(name, value)   [script fn 'SetValue', FND 0x00182028, sig 13]
// Reconstructed from 5960 retail XBE @ 0x000419e0: reopen if needed, set
// the value in `section`, and save when it changed.
void CSettings::SetValue(const TCHAR* name, const TCHAR* value)
{
	OpenCurrent();
	m_store.SetValue(m_section, name, value);
}

// CSettings::GetValue(name)   [script fn 'GetValue', FND 0x00182010, sig 5]
// Reconstructed from 5960 retail XBE @ 0x00041a50: reopen if needed (an
// empty string if that fails), then the value from `section` read into
// 1024 characters.
CStrObject* CSettings::GetValue(const TCHAR* name)
{
	if (!OpenCurrent())
		return new CStrObject(_T(""));
	TCHAR value[1024];
	m_store.GetValue(m_section, name, value, countof(value));
	return new CStrObject(value);
}

CSetting::CSetting()
{
	m_pSettings = NULL;
	m_textName = NULL;
}

// Reconstructed from 5960 retail XBE @ 0x00041b60: frees the name.
CSetting::~CSetting()
{
	delete[] m_textName;
}

// CSetting::Assign(value)
// Reconstructed from 5960 retail XBE @ 0x00041b80:
//     s = value->Deref()->ToStr(); settings->SetValue(name, s->text or "");
void CSetting::Assign(CObject* newValue)
{
	CStrObject* text = newValue->Deref()->ToStr();
	m_pSettings->SetValue(m_textName, text->GetSz());
}

CStrObject* CSetting::ToStr()
{
	return m_pSettings->GetValue(m_textName);
}

// CSetting::Deref()
// Reconstructed from 5960 retail XBE @ 0x00041bc0: the value, and the
// CSetting lets go of itself.
CObject* CSetting::Deref()
{
	CObject* value = m_pSettings->GetValue(m_textName);
	Release();
	return value;
}


// ===== ScreenSaver =========================================================

// Three idle stages: delay starts the saver, delay2 and delay3 fire later
// hooks. The newest ScreenSaver is the one input resets.
class CScreenSaver : public CNode
{
	DECLARE_NODE(CScreenSaver, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CScreenSaver();
	~CScreenSaver();

	void Advance(float dt);
	bool OnSetProperty(const PRD* prop, const void* value);

	void reset();
	void StartAfter(float secs);
	void SetDelay2(float secs);
	void SetDelay3(float secs);

	static CScreenSaver* c_pSaver;

	bool m_enabled;
	bool m_isActive;
	bool m_isActive2;
	bool m_isActive3;
	float m_delay;
	float m_delay2;
	float m_delay3;
	XTIME m_lastInputTime;
};

IMPLEMENT_NODE("ScreenSaver", CScreenSaver, CNode)

// PRD at 0x001745d8, same order.
START_NODE_PROPS(CScreenSaver, CNode)
	NODE_PROP(pt_boolean, CScreenSaver, enabled)
	NODE_PROP(pt_boolean, CScreenSaver, isActive)
	NODE_PROP(pt_boolean, CScreenSaver, isActive2)
	NODE_PROP(pt_boolean, CScreenSaver, isActive3)
	NODE_PROP(pt_number, CScreenSaver, delay)
	NODE_PROP(pt_number, CScreenSaver, delay2)
	NODE_PROP(pt_number, CScreenSaver, delay3)
END_NODE_PROPS()

// FND at 0x00181f50 holds only reset; the three setters are Theseus's.
#define _FND_CLASS CScreenSaver
START_NODE_FUN(CScreenSaver, CNode)
	NODE_FUN_VV(reset)
	NODE_FUN_VN(StartAfter)
	NODE_FUN_VN(SetDelay2)
	NODE_FUN_VN(SetDelay3)
END_NODE_FUN()
#undef _FND_CLASS

CScreenSaver* CScreenSaver::c_pSaver = NULL;     // 0x0017e738
XTIME g_timeOfLastEvent;                                    // 0x0017c030

// CScreenSaver::CScreenSaver()
// Reconstructed from 5960 retail XBE @ 0x000400f0: enabled, nothing
// active, a 300 second delay, the others off, the clock started now, and
// this node becomes the one ResetScreenSaver talks to.
CScreenSaver::CScreenSaver()
{
	m_enabled = true;
	m_isActive = false;
	m_isActive2 = false;
	m_isActive3 = false;
	m_delay = 300.0f;
	m_delay2 = 0.0f;
	m_delay3 = 0.0f;
	m_lastInputTime = TheseusGetNow();
	c_pSaver = this;
}

// Reconstructed from 5960 retail XBE @ 0x00040190.
CScreenSaver::~CScreenSaver()
{
	if (c_pSaver == this)
		c_pSaver = NULL;
}

void CScreenSaver::StartAfter(float secs) { m_delay = secs; }
void CScreenSaver::SetDelay2(float secs)  { m_delay2 = secs; }
void CScreenSaver::SetDelay3(float secs)  { m_delay3 = secs; }

// CScreenSaver::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x000401b0. Disabled while active
// ends it (OnEnd). Enabled and idle for `delay` seconds starts it (OnStart).
// delay2 and delay3, when above 0, each fire once (OnDelay2, OnDelay3)
// after that long idle, enabled or not. 5960 does the sums in float;
// Theseus keeps the double clock.
void CScreenSaver::Advance(float dt)
{
	CNode::Advance(dt);
	XTIME now = TheseusGetNow();

	if (!m_enabled)
	{
		if (m_isActive)
		{
			m_isActive = false;
			CallFunction(this, _T("OnEnd"));
		}
	}
	else if (!m_isActive && now >= m_lastInputTime + m_delay)
	{
		m_isActive = true;
		CallFunction(this, _T("OnStart"));
	}

	if (m_delay2 > 0.0f && !m_isActive2 && now >= m_lastInputTime + m_delay2)
	{
		m_isActive2 = true;
		CallFunction(this, _T("OnDelay2"));
	}
	if (m_delay3 > 0.0f && !m_isActive3 && now >= m_lastInputTime + m_delay3)
	{
		m_isActive3 = true;
		CallFunction(this, _T("OnDelay3"));
	}
}

// CScreenSaver::reset()   [script fn 'reset', FND 0x00181f50, sig 1]
// Reconstructed from 5960 retail XBE @ 0x000402a0: ends an active saver
// (OnEnd), rearms the two later stages and restarts the idle clock.
void CScreenSaver::reset()
{
	if (m_isActive)
	{
		m_isActive = false;
		CallFunction(this, _T("OnEnd"));
	}
	m_isActive2 = false;
	m_isActive3 = false;
	m_lastInputTime = TheseusGetNow();
}

// CScreenSaver::OnSetProperty(prd, value)
// Reconstructed from 5960 retail XBE @ 0x000402d0: turning `enabled` on
// resets first; the new value is stored either way.
bool CScreenSaver::OnSetProperty(const PRD* prop, const void* value)
{
	if (PTR2INT(prop->pbOffset) != offsetof(m_enabled))
		return true;
	bool on = *(const bool*)value;
	if (on)
		reset();
	m_enabled = on;
	return true;
}

// ResetScreenSaver()
// Reconstructed from 5960 retail XBE @ 0x00040320. Any input calls this:
// it stamps the global last event time and, when there's a ScreenSaver,
// resets it, restarts the console's auto power off timer and returns
// whether the saver was showing (so the press can be swallowed).
bool ResetScreenSaver()
{
	g_timeOfLastEvent = TheseusGetNow();

	CScreenSaver* saver = CScreenSaver::c_pSaver;
	if (saver == NULL)
		return false;

	bool wasShowing = saver->m_isActive;
	saver->reset();
#ifdef _XBOX
	XAutoPowerDownResetTimer();
#endif
	return wasShowing;
}
