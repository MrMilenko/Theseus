// memory_monitor.cpp: the MemoryMonitor node behind the Memory pane. It
// mounts memory units as they come and go, tracks which one is selected
// and reports their names and free space. Rebuilt from the 5960 retail
// XBE (0x4be70..0x4cd10); see docs/decomp/MemoryMonitor.md.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "runner.h"
#include "locale_node.h"
#include "title_collection.h"

enum { PATH_UP, PATH_DOWN, PATH_LEFT, PATH_RIGHT };
#define PATH_END 0xff

// Where each memory unit slot moves to, per direction: the first entry
// that's the hard drive or a mounted unit wins. Units are numbered by
// controller port and slot (port * 2 + slot), laid out on screen as
//     port 1 | port 4
//     port 2 | port 3
// with the hard drive in the middle. 5960 table @ 0x00174b38.
static const BYTE s_paths[8][4][3] =
{
	//  up                          down                        left                        right
	{ { PATH_END },               { Dev2Unit1, Dev2Unit2, PATH_END }, { PATH_END },               { Dev1Unit2, Dev0, PATH_END } },
	{ { PATH_END },               { Dev2Unit2, Dev2Unit1, PATH_END }, { Dev1Unit1, PATH_END },    { Dev0, PATH_END } },
	{ { Dev1Unit1, Dev1Unit2, PATH_END }, { PATH_END },               { PATH_END },               { Dev2Unit2, Dev0, PATH_END } },
	{ { Dev1Unit2, Dev1Unit1, PATH_END }, { PATH_END },               { Dev2Unit1, PATH_END },    { Dev0, PATH_END } },
	{ { Dev4Unit1, Dev4Unit2, PATH_END }, { PATH_END },               { Dev0, PATH_END },         { Dev3Unit2, PATH_END } },
	{ { Dev4Unit2, Dev4Unit1, PATH_END }, { PATH_END },               { Dev3Unit1, Dev0, PATH_END }, { PATH_END } },
	{ { PATH_END },               { Dev3Unit1, Dev3Unit2, PATH_END }, { Dev0, PATH_END },         { Dev4Unit2, PATH_END } },
	{ { PATH_END },               { Dev3Unit2, Dev3Unit1, PATH_END }, { Dev4Unit1, Dev0, PATH_END }, { PATH_END } },
};

class CMemoryMonitor : public CNode
{
	DECLARE_NODE(CMemoryMonitor, CNode)
public:
	CMemoryMonitor();
	~CMemoryMonitor();

	void Advance(float nSeconds);

	void selectUp();
	void selectDown();
	void selectLeft();
	void selectRight();
	int selectDevUnit(int DriveLetter);

	CStrObject *FormatDeviceName(int devUnit);
	CStrObject *FormatTotalBlocks();
	CStrObject *FormatFreeBlocks(int devUnit);

	float GetFreeTotalRatio(int devUnit);
	int GetTotalFreeBlocks(int devUnit);
	void FormatMemoryUnit(int devUnit);

	int HaveDeviceTop(int nDevice);
	int HaveDeviceBottom(int nDevice);

	void SetMUName(int devUnit, const TCHAR *szName);

	CStrObject *FormatTotalSlots();
	CStrObject *FormatFreeSlots(int devUnit);
	int GetTotalFreeSlots(int devUnit);

	int m_curDevUnit;
	int m_invalidDevUnit;
	bool m_blockInsertion;
	bool m_enumerationOn;

	bool FormatDeviceName2(int devUnit, TCHAR *szBuf);

protected:
	void UpdateValidDeviceFlags();
	bool GetTotalAndFreeBlocks(int devUnit, int *pnTotalBlocks, int *pnFreeBlocks);

	int m_nTotalBlocks;
	int m_nFreeBlocks;
	int m_nPrefRow;
	int m_nPrefCol;
	int m_nPrefUnit;

	DWORD m_dwPendingDevUnits;
	bool m_rgbValidDevUnit[8];
	char m_rgchMount[8];

	DECLARE_NODE_PROPS();
	DECLARE_NODE_FUNCTIONS();

private:
	void MountPending(int devUnit);
	void HandleRemoval(int devUnit);
	void FollowPath(int direction);
	void SelectionChanged();
	int FreeBlocksOf(int devUnit);
	int FreeSlotsOf(int devUnit);
};

CMemoryMonitor *g_pMemoryMonitor;

IMPLEMENT_NODE("MemoryMonitor", CMemoryMonitor, CNode)

START_NODE_PROPS(CMemoryMonitor, CNode)
NODE_PROP(pt_integer, CMemoryMonitor, curDevUnit)
NODE_PROP(pt_integer, CMemoryMonitor, invalidDevUnit)
NODE_PROP(pt_boolean, CMemoryMonitor, blockInsertion)
NODE_PROP(pt_boolean, CMemoryMonitor, enumerationOn)
END_NODE_PROPS()

#define _FND_CLASS CMemoryMonitor
START_NODE_FUN(CMemoryMonitor, CNode)
NODE_FUN_VV(selectUp)
NODE_FUN_VV(selectDown)
NODE_FUN_VV(selectLeft)
NODE_FUN_VV(selectRight)
NODE_FUN_II(selectDevUnit)
NODE_FUN_SI(FormatDeviceName)
NODE_FUN_SV(FormatTotalBlocks)
NODE_FUN_SI(FormatFreeBlocks)
NODE_FUN_II(GetTotalFreeBlocks)
NODE_FUN_II(HaveDeviceTop)
NODE_FUN_II(HaveDeviceBottom)
NODE_FUN_NI(GetFreeTotalRatio)
NODE_FUN_VIS(SetMUName)
NODE_FUN_VI(FormatMemoryUnit)
NODE_FUN_SV(FormatTotalSlots)
NODE_FUN_SI(FormatFreeSlots)
NODE_FUN_II(GetTotalFreeSlots)
END_NODE_FUN()
#undef _FND_CLASS

static int PortOf(int devUnit) { return devUnit / 2; }
static int SlotOf(int devUnit) { return devUnit % 2; }

// XGetDevices bits: the top slot's ports in the low word, the bottom's in the high.
static DWORD DeviceBit(int devUnit) { return (SlotOf(devUnit) ? 0x10000u : 1u) << PortOf(devUnit); }

// CMemoryMonitor::CMemoryMonitor()
// Reconstructed from 5960 retail XBE @ 0x0004be70: becomes the global
// monitor, hard drive selected, nothing mounted or invalid, totals unknown
// (-1), insertion allowed, enumeration off. Every unit already plugged in
// starts out pending.
CMemoryMonitor::CMemoryMonitor()
{
	g_pMemoryMonitor = this;

	m_curDevUnit = Dev0;
	m_invalidDevUnit = -1;
	m_blockInsertion = false;
	m_enumerationOn = false;

	m_nTotalBlocks = -1;
	m_nFreeBlocks = -1;
	m_nPrefRow = 0;
	m_nPrefCol = 0;
	m_nPrefUnit = 0;

	memset(m_rgbValidDevUnit, 0, sizeof(m_rgbValidDevUnit));
	memset(m_rgchMount, 0, sizeof(m_rgchMount));

	m_dwPendingDevUnits = XGetDevices(XDEVICE_TYPE_MEMORY_UNIT);
	UpdateValidDeviceFlags();
}

CMemoryMonitor::~CMemoryMonitor()
{
}

bool XboxFormatMemoryUnit(UINT nDevicePort, UINT nMemoryUnit);

// A unit went away: unmount it, drop its titles, and fall back to the hard
// drive if it was selected. A bad unit that leaves stops being the bad one.
void CMemoryMonitor::HandleRemoval(int devUnit)
{
	if (m_rgbValidDevUnit[devUnit])
	{
		XUnmountMU(PortOf(devUnit), SlotOf(devUnit));
		m_rgchMount[devUnit] = 0;
		g_titles[devUnit].SetRoot(0, false);
		m_rgbValidDevUnit[devUnit] = false;

		if (m_curDevUnit == devUnit)
		{
			m_curDevUnit = Dev0;
			CallFunction(this, _T("OnCurDevUnitChange"));
		}
	}

	if (m_invalidDevUnit == devUnit)
	{
		m_invalidDevUnit = -1;
		CallFunction(this, _T("OnInvalidMURemoved"));
	}

	CallFunction(this, _T("OnDeviceChange"));
}

// Mount a pending unit, three tries. One that won't mount becomes the bad
// unit: it's formatted (which mounts it again if it can) and the scene
// hears OnInvalidMU(devUnit, mounted now).
void CMemoryMonitor::MountPending(int devUnit)
{
	m_dwPendingDevUnits &= ~DeviceBit(devUnit);

	DWORD dwError = ERROR_SUCCESS;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		dwError = XMountMURoot(PortOf(devUnit), SlotOf(devUnit), &m_rgchMount[devUnit]);
		if (dwError == ERROR_SUCCESS)
			break;
	}

	if (dwError == ERROR_SUCCESS)
	{
		XCleanMUFromRoot(m_rgchMount[devUnit], 0);
		m_rgbValidDevUnit[devUnit] = true;
		g_titles[devUnit].SetRoot(m_rgchMount[devUnit], false);
		CallFunction(this, _T("OnDeviceChange"));
		return;
	}

	m_invalidDevUnit = devUnit;
	FormatMemoryUnit(devUnit);
	CallFunction(this, _T("OnDeviceChange"));

	CObject *rgArgs[2];
	rgArgs[0] = new CNumObject((float)devUnit);
	rgArgs[1] = new CNumObject(m_rgbValidDevUnit[devUnit] ? 1.0f : 0.0f);
	CallFunction(this, _T("OnInvalidMU"), 2, rgArgs);
}

// CMemoryMonitor::UpdateValidDeviceFlags()
// Reconstructed from 5960 retail XBE @ 0x0004bf50. Only while the scene
// wants enumeration, and only when XGetDeviceChanges reports something or a
// unit is still pending: wakes the screen saver, folds the changes into the
// pending set, then per unit handles a removal and, unless insertion is
// blocked, mounts a pending unit.
void CMemoryMonitor::UpdateValidDeviceFlags()
{
	if (!m_enumerationOn)
		return;

	DWORD dwInserted, dwRemoved;
	if (!XGetDeviceChanges(XDEVICE_TYPE_MEMORY_UNIT, &dwInserted, &dwRemoved) && m_dwPendingDevUnits == 0)
		return;

	ResetScreenSaver();

	m_dwPendingDevUnits = (m_dwPendingDevUnits & ~dwRemoved) | dwInserted;

	for (int devUnit = 0; devUnit < 8; devUnit++)
	{
		DWORD dwBit = DeviceBit(devUnit);

		if (dwRemoved & dwBit)
			HandleRemoval(devUnit);

		if (!m_blockInsertion && (m_dwPendingDevUnits & dwBit))
			MountPending(devUnit);
	}
}

// CMemoryMonitor::HaveDeviceTop(port)      [script fn 'HaveDeviceTop', FND 0x00187b60, sig 3]
// CMemoryMonitor::HaveDeviceBottom(port)   [script fn 'HaveDeviceBottom', FND 0x00187b78, sig 3]
// Reconstructed from 5960 retail XBE @ 0x0004c1f0 and 0x0004c210: whether
// that port's top or bottom slot holds a mounted unit; 0 for a bad port.
int CMemoryMonitor::HaveDeviceTop(int nDevice)
{
	return (nDevice >= 0 && nDevice < 4) ? m_rgbValidDevUnit[nDevice * 2] : 0;
}

int CMemoryMonitor::HaveDeviceBottom(int nDevice)
{
	return (nDevice >= 0 && nDevice < 4) ? m_rgbValidDevUnit[nDevice * 2 + 1] : 0;
}

// CMemoryMonitor::GetTotalAndFreeBlocks(devUnit, &total, &free)
// Reconstructed from 5960 retail XBE @ 0x0004c3b0. The hard drive is C:,
// a unit its mount letter (unmounted fails). Both sizes are rounded up to
// 16 KB blocks and one block comes off the total for the root directory.
// If the drive can't be asked, a unit is marked gone and both read 0.
// Theseus range checks devUnit first; the hard drive (Dev0) is in range.
bool CMemoryMonitor::GetTotalAndFreeBlocks(int devUnit, int *pnTotalBlocks, int *pnFreeBlocks)
{
	if (devUnit < Dev1Unit1 || devUnit > Dev0)
		return false;

	char szRoot[4] = { 'C', ':', '\\', 0 };
	if (devUnit != Dev0)
	{
		szRoot[0] = m_rgchMount[devUnit];
		if (szRoot[0] == 0)
			return false;
	}

	ULARGE_INTEGER qwAvail, qwTotal, qwFree;
	if (!GetDiskFreeSpaceEx(szRoot, &qwAvail, &qwTotal, &qwFree))
	{
		if (devUnit != Dev0)
			m_rgbValidDevUnit[devUnit] = false;
		if (pnTotalBlocks != NULL)
			*pnTotalBlocks = 0;
		if (pnFreeBlocks != NULL)
			*pnFreeBlocks = 0;
		return false;
	}

	if (pnTotalBlocks != NULL)
		*pnTotalBlocks = (int)((qwTotal.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE) - 1;
	if (pnFreeBlocks != NULL)
		*pnFreeBlocks = (int)((qwFree.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE);
	return true;
}

// CMemoryMonitor::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x0004c480: enumerate, fall back to
// the hard drive if the selected unit is gone, then refresh the selected
// drive's totals and fire OnTotalFreeChanged when either moved.
void CMemoryMonitor::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	UpdateValidDeviceFlags();

	if (m_curDevUnit != Dev0 && !m_rgbValidDevUnit[m_curDevUnit])
	{
		m_curDevUnit = Dev0;
		CallFunction(this, _T("OnCurDevUnitChange"));
	}

	int nTotal = 0, nFree = 0;
	GetTotalAndFreeBlocks(m_curDevUnit, &nTotal, &nFree);
	if (nTotal != m_nTotalBlocks || nFree != m_nFreeBlocks)
	{
		m_nTotalBlocks = nTotal;
		m_nFreeBlocks = nFree;
		CallFunction(this, _T("OnTotalFreeChanged"));
	}
}

// Move along the selected unit's path in one direction (s_paths).
void CMemoryMonitor::FollowPath(int direction)
{
	for (const BYTE *pNext = s_paths[m_curDevUnit][direction]; *pNext != PATH_END; pNext++)
	{
		if (*pNext == Dev0 || m_rgbValidDevUnit[*pNext])
		{
			m_curDevUnit = *pNext;
			return;
		}
	}
}

// The selection moved; the scene redraws from OnCurDevUnitChange.
void CMemoryMonitor::SelectionChanged()
{
	CallFunction(this, _T("OnCurDevUnitChange"));
}

// CMemoryMonitor::selectUp() / selectDown()   [script fns, FND 0x00187a40, 0x00187a58, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0004c510 and 0x0004c580. Nothing
// from the hard drive. Otherwise follow the path, prefer the top (up) or
// bottom (down) row from now on, note the unit's slot and side (ports 3
// and 4 are the right side), and fire OnCurDevUnitChange.
void CMemoryMonitor::selectUp()
{
	if (m_curDevUnit == Dev0)
		return;
	FollowPath(PATH_UP);
	m_nPrefRow = 0;
	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = SlotOf(m_curDevUnit);
		m_nPrefCol = PortOf(m_curDevUnit) > 1;
	}
	SelectionChanged();
}

void CMemoryMonitor::selectDown()
{
	if (m_curDevUnit == Dev0)
		return;
	FollowPath(PATH_DOWN);
	m_nPrefRow = 1;
	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = SlotOf(m_curDevUnit);
		m_nPrefCol = PortOf(m_curDevUnit) > 1;
	}
	SelectionChanged();
}

// CMemoryMonitor::selectLeft() / selectRight()   [script fns, FND 0x00187a70, 0x00187a88, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0004c600 and 0x0004c6e0. From the
// hard drive, try that side's port in the preferred row, then the other
// row; left tries the bottom slot first, right the top. Nothing mounted on
// that side leaves the hard drive selected and fires nothing. Otherwise
// follow the path. Then prefer that side, note the slot and row (ports 2
// and 3 are the bottom row) and fire OnCurDevUnitChange.
void CMemoryMonitor::selectLeft()
{
	if (m_curDevUnit == Dev0)
	{
		int nearPort = m_nPrefRow == 0 ? Dev1Unit1 : Dev2Unit1;
		int farPort = m_nPrefRow == 0 ? Dev2Unit1 : Dev1Unit1;
		const int tries[4] = { nearPort + 1, nearPort, farPort + 1, farPort };
		int i = 0;
		while (i < 4 && !m_rgbValidDevUnit[tries[i]])
			i++;
		if (i == 4)
		{
			m_curDevUnit = Dev0;
			return;
		}
		m_curDevUnit = tries[i];
	}
	else
	{
		FollowPath(PATH_LEFT);
	}

	m_nPrefCol = 0;
	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = SlotOf(m_curDevUnit);
		m_nPrefRow = PortOf(m_curDevUnit) == 1 || PortOf(m_curDevUnit) == 2;
	}
	SelectionChanged();
}

void CMemoryMonitor::selectRight()
{
	if (m_curDevUnit == Dev0)
	{
		int nearPort = m_nPrefRow == 0 ? Dev4Unit1 : Dev3Unit1;
		int farPort = m_nPrefRow == 0 ? Dev3Unit1 : Dev4Unit1;
		const int tries[4] = { nearPort, nearPort + 1, farPort, farPort + 1 };
		int i = 0;
		while (i < 4 && !m_rgbValidDevUnit[tries[i]])
			i++;
		if (i == 4)
		{
			m_curDevUnit = Dev0;
			return;
		}
		m_curDevUnit = tries[i];
	}
	else
	{
		FollowPath(PATH_RIGHT);
	}

	m_nPrefCol = 1;
	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = SlotOf(m_curDevUnit);
		m_nPrefRow = PortOf(m_curDevUnit) == 1 || PortOf(m_curDevUnit) == 2;
	}
	SelectionChanged();
}

// CMemoryMonitor::selectDevUnit(letter)   [script fn 'selectDevUnit', FND 0x00187aa0, sig 3]
// Reconstructed from 5960 retail XBE @ 0x0004c7d0. T and U mean the hard
// drive, a unit letter means that unit; anything else fails. Enumerates
// once even if the scene hasn't asked to, then selects the drive if it's
// there. 5960's unit letters are F to M; Theseus mounts units at H to O.
int CMemoryMonitor::selectDevUnit(int DriveLetter)
{
	int letter = toupper(DriveLetter);
	int devUnit;
	if (letter == 'T' || letter == 'U')
		devUnit = Dev0;
	else if (letter >= 'H' && letter <= 'O')
		devUnit = letter - 'H';
	else
		return false;

	bool bWasOn = m_enumerationOn;
	m_enumerationOn = true;
	UpdateValidDeviceFlags();
	m_enumerationOn = bWasOn;

	if (devUnit != Dev0 && !m_rgbValidDevUnit[devUnit])
		return false;

	m_curDevUnit = devUnit;
	return true;
}

// Name lookup for the save grid; an unmounted unit gives "".
void FormatDeviceName(int devUnit, TCHAR *szBuf)
{
	if (!g_pMemoryMonitor->FormatDeviceName2(devUnit, szBuf))
		szBuf[0] = 0;
}

// CMemoryMonitor::FormatDeviceName(devUnit)   [script fn 'FormatDeviceName', FND 0x00187ab8, sig 4]
// Reconstructed from 5960 retail XBE @ 0x0004c840 and its helper @ 0x0004c8c0:
// -1 means the selected unit; a mounted unit gives its name
// (XMUNameFromDriveLetter), anything else "".
CStrObject *CMemoryMonitor::FormatDeviceName(int devUnit)
{
	TCHAR szName[MAX_MUNAME];
	if (!FormatDeviceName2(devUnit, szName))
		szName[0] = 0;
	return new CStrObject(szName);
}

bool CMemoryMonitor::FormatDeviceName2(int devUnit, TCHAR *szBuf)
{
	if (devUnit == -1)
		devUnit = m_curDevUnit;
	if (devUnit < Dev1Unit1 || devUnit > Dev4Unit2 || m_rgchMount[devUnit] == 0)
		return false;

	TCHAR szName[MAX_MUNAME];
	if (XMUNameFromDriveLetter(m_rgchMount[devUnit], szName, MAX_MUNAME) != ERROR_SUCCESS)
		return false;
	_tcscpy(szBuf, szName);
	return true;
}

// Free blocks of a drive, or the cached figure for -1. Left untouched
// (garbage) when the drive can't be read, as in 5960.
int CMemoryMonitor::FreeBlocksOf(int devUnit)
{
	int nFree;
	if (devUnit == -1)
		nFree = m_nFreeBlocks;
	else
		GetTotalAndFreeBlocks(devUnit, NULL, &nFree);
	return nFree;
}

// FormatTotalBlocks / FormatFreeBlocks / GetTotalFreeBlocks   [FND 0x00187ad0, 0x00187ae8, 0x00187b00]
// Reconstructed from 5960 retail XBE @ 0x0004cbc0, 0x0004ca90, 0x0004cb90:
// the cached total, or a drive's free blocks (-1 for the cached free count),
// printed as a number with anything past 50000 shown as "50000+".
CStrObject *CMemoryMonitor::FormatTotalBlocks()
{
	TCHAR szBlocks[16];
	FormatBlocks(szBlocks, m_nTotalBlocks);
	return new CStrObject(szBlocks);
}

CStrObject *CMemoryMonitor::FormatFreeBlocks(int devUnit)
{
	TCHAR szBlocks[16];
	FormatBlocks(szBlocks, FreeBlocksOf(devUnit));
	return new CStrObject(szBlocks);
}

int CMemoryMonitor::GetTotalFreeBlocks(int devUnit)
{
	return FreeBlocksOf(devUnit);
}

// Slots   [FND 0x00187b18, 0x00187b30, 0x00187b48]
// 5960 (0x0004c930, 0x0004c9d0, 0x0004cb60) counts Xbox Live account slots:
// eight on the hard drive less the accounts stored there, one on a unit
// with no account on it, printed with "8+" past eight. Theseus counts the
// same blocks as above, capped at eight.
int CMemoryMonitor::FreeSlotsOf(int devUnit)
{
	int nFree = FreeBlocksOf(devUnit);
	return nFree > 8 ? 8 : nFree;
}

CStrObject *CMemoryMonitor::FormatTotalSlots()
{
	TCHAR szSlots[16];
	FormatBlocks(szSlots, m_nTotalBlocks > 8 ? 8 : m_nTotalBlocks);
	return new CStrObject(szSlots);
}

CStrObject *CMemoryMonitor::FormatFreeSlots(int devUnit)
{
	TCHAR szSlots[16];
	FormatBlocks(szSlots, FreeSlotsOf(devUnit));
	return new CStrObject(szSlots);
}

int CMemoryMonitor::GetTotalFreeSlots(int devUnit)
{
	return FreeSlotsOf(devUnit);
}

// CMemoryMonitor::GetFreeTotalRatio(devUnit)   [script fn 'GetFreeTotalRatio', FND 0x00187b90, sig 27]
// Reconstructed from 5960 retail XBE @ 0x0004cc70: free / total blocks of
// the drive, 0 if it can't be read. Theseus also gives 0 for an empty
// total rather than dividing by it.
float CMemoryMonitor::GetFreeTotalRatio(int devUnit)
{
	int nTotal, nFree;
	if (!GetTotalAndFreeBlocks(devUnit, &nTotal, &nFree) || nTotal == 0)
		return 0.0f;
	return (float)nFree / nTotal;
}

// CMemoryMonitor::SetMUName(devUnit, name)   [script fn 'SetMUName', FND 0x00187ba8, sig 28]
// Reconstructed from 5960 retail XBE @ 0x0004ccd0: names a mounted unit
// when the name fits in MAX_MUNAME with its terminator.
void CMemoryMonitor::SetMUName(int devUnit, const TCHAR *szName)
{
	if (devUnit < Dev1Unit1 || devUnit > Dev4Unit2 || m_rgchMount[devUnit] == 0)
		return;
	if (_tcslen(szName) + 1 > MAX_MUNAME)
		return;
	XMUWriteNameToDriveLetter(m_rgchMount[devUnit], szName);
}

// CMemoryMonitor::FormatMemoryUnit(devUnit)   [script fn 'FormatMemoryUnit', FND 0x00187bc0, sig 14]
// Reconstructed from 5960 retail XBE @ 0x0004cd10: unmount (dropping its
// titles), format, mount again. On a good mount the titles are rescanned
// at once, so the save grid doesn't see the unit as dirty a frame later.
// Theseus refuses anything that isn't a memory unit: scripts reach this,
// and Dev0 sits one past the end of the mount arrays.
void CMemoryMonitor::FormatMemoryUnit(int devUnit)
{
	if (devUnit < Dev1Unit1 || devUnit > Dev4Unit2)
		return;

	if (m_rgchMount[devUnit] != 0)
	{
		XUnmountMU(PortOf(devUnit), SlotOf(devUnit));
		m_rgchMount[devUnit] = 0;
		g_titles[devUnit].SetRoot(0, false);
	}

	m_rgbValidDevUnit[devUnit] = false;
	XboxFormatMemoryUnit(PortOf(devUnit), SlotOf(devUnit));

	if (XMountMURoot(PortOf(devUnit), SlotOf(devUnit), &m_rgchMount[devUnit]) == ERROR_SUCCESS)
	{
		m_rgbValidDevUnit[devUnit] = true;
		g_titles[devUnit].SetRoot(m_rgchMount[devUnit], true);
	}
}
