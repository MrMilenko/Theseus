// recovery_node.cpp: dashboard recovery process node. Decompiled from
// the 5960 retail XBE.
//
// The retail dashboard's recovery flow performed an HDD wipe + restore
// via the private DashRecovery() helper. On modded hardware that
// responsibility lives at the modchip / BIOS layer, so StartRecovery
// is stubbed to instant-complete: the XAP recovery flow still runs
// end-to-end (progress callback fires once at 100%, OnRecoveryComplete
// fires next), and the user-visible "RECOVERY COMPLETE" dialog leads
// to FinishRecovery, which is the only path the dashboard XAPs
// actually need from this node. FinishRecovery reboots the box on
// Xbox and triggers the dashboard restart hook on desktop.
#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

bool ResetScreenSaver();

#ifndef _XBOX
extern bool g_desktopRestartRequested;
#endif

class CRecovery : public CNode
{
	DECLARE_NODE(CRecovery, CNode)
public:
	CRecovery();
	~CRecovery();
	void StartRecovery();
	void FinishRecovery();
	float m_recoveryProgress;

protected:
	void Advance(float nSeconds);
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

private:
	bool m_bDone;
	bool m_bRunning;
	UINT m_nPercent;
	UINT m_nLastPercent;
};

IMPLEMENT_NODE("Recovery", CRecovery, CNode)

START_NODE_PROPS(CRecovery, CNode)
	NODE_PROP(pt_number, CRecovery, recoveryProgress)
END_NODE_PROPS()

#define _FND_CLASS CRecovery
START_NODE_FUN(CRecovery, CNode)
	NODE_FUN_VV(StartRecovery)
	NODE_FUN_VV(FinishRecovery)
END_NODE_FUN()
#undef _FND_CLASS

CRecovery::CRecovery()
	: m_recoveryProgress(0.0f), m_bDone(false), m_bRunning(false),
	  m_nPercent(0), m_nLastPercent(0)
{
}

CRecovery::~CRecovery() {}

void CRecovery::StartRecovery()
{
	m_bRunning = true;
	m_nLastPercent = m_nPercent;
	m_nPercent = 100;
	m_recoveryProgress = 1.0f;
	m_bDone = true;
}

void CRecovery::FinishRecovery()
{
#ifdef _XBOX
	HalReturnToFirmware(HalRebootRoutine);
#else
	g_desktopRestartRequested = true;
#endif
}

void CRecovery::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_bRunning)
	{
		if (m_bDone)
		{
			m_bRunning = false;
			CallFunction(this, _T("OnRecoveryComplete"));
		}
		else if (m_nLastPercent != m_nPercent)
		{
			CallFunction(this, _T("OnRecoveryProgressChanged"));
			m_nLastPercent = m_nPercent;
			m_recoveryProgress = m_nPercent / 100.0f;
			ResetScreenSaver();
		}
	}
}
