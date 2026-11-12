// recovery_node.cpp: the Recovery node behind the dashboard's recovery
// screens. Rebuilt from the 5960 retail XBE (0x3ba60..0x3bb70, 0x2dc50).
// 5960 runs the hard disk restore on a thread and reports its percentage;
// on modded hardware that job belongs to the BIOS, so Theseus finishes at
// once. The scripts still see one OnRecoveryComplete, and FinishRecovery
// reboots (desktop restarts the dashboard instead).

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
	bool m_bDone;           // +0x20
	bool m_bRunning;        // +0x21
	UINT m_nPercent;        // +0x24
	UINT m_nLastPercent;    // +0x28
};

IMPLEMENT_NODE("Recovery", CRecovery, CNode)

START_NODE_PROPS(CRecovery, CNode)
	NODE_PROP(pt_number, CRecovery, recoveryProgress)
END_NODE_PROPS()

// FND at 0x00180390.
#define _FND_CLASS CRecovery
START_NODE_FUN(CRecovery, CNode)
	NODE_FUN_VV(StartRecovery)
	NODE_FUN_VV(FinishRecovery)
END_NODE_FUN()
#undef _FND_CLASS

// Reconstructed from 5960 retail XBE @ 0x0003ba60: everything zero.
CRecovery::CRecovery()
{
	m_recoveryProgress = 0.0f;
	m_bDone = false;
	m_bRunning = false;
	m_nPercent = 0;
	m_nLastPercent = 0;
}

CRecovery::~CRecovery()
{
}

// CRecovery::StartRecovery()   [script fn 'StartRecovery', FND 0x00180390, sig 1]
// 5960 (@ 0x0003bb20) clears done, sets running, syncs the last percentage
// and starts the restore thread; when the thread can't start it marks
// progress 1 and done. Theseus has no restore to run, so it always takes
// that finished path at 100%.
void CRecovery::StartRecovery()
{
	m_bRunning = true;
	m_nLastPercent = m_nPercent;
	m_nPercent = 100;
	m_recoveryProgress = 1.0f;
	m_bDone = true;
}

// CRecovery::FinishRecovery()   [script fn 'FinishRecovery', FND 0x001803a8, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0002dc50: HalReturnToFirmware(1),
// a reboot. Desktop asks the app to restart the dashboard.
void CRecovery::FinishRecovery()
{
#ifdef _XBOX
	HalReturnToFirmware(HalRebootRoutine);
#else
	g_desktopRestartRequested = true;
#endif
}

// CRecovery::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x0003bb70. While running: done
// stops it and calls OnRecoveryComplete; otherwise a new percentage calls
// OnRecoveryProgressChanged, updates recoveryProgress (percent / 100) and
// keeps the screen saver off.
void CRecovery::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	if (!m_bRunning)
		return;

	if (m_bDone)
	{
		m_bRunning = false;
		CallFunction(this, _T("OnRecoveryComplete"));
		return;
	}
	if (m_nPercent != m_nLastPercent)
	{
		CallFunction(this, _T("OnRecoveryProgressChanged"));
		m_nLastPercent = m_nPercent;
		m_recoveryProgress = m_nPercent / 100.0f;
		ResetScreenSaver();
	}
}
