// memory_monitor.cpp: CMemoryMonitor XAP node. Drives the Memory
// pane (device list, per-device free space, title and savegame
// totals, navigation between devices and the save grid). Decompiled
// from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "node.h"
#include "runner.h"
#include "locale_node.h"
#include "title_collection.h"

#define up 0
#define down 1
#define left 2
#define right 3

#define end 0xff

struct PATHS
{
	const BYTE *rgpath[4];
};

static const BYTE pathDev1Unit1Up[] = {end};
static const BYTE pathDev1Unit1Down[] = {Dev2Unit1, Dev2Unit2, /*Dev0,*/ end};
static const BYTE pathDev1Unit1Left[] = {end};
static const BYTE pathDev1Unit1Right[] = {Dev1Unit2, Dev0, end};

static const BYTE pathDev1Unit2Up[] = {end};
static const BYTE pathDev1Unit2Down[] = {Dev2Unit2, Dev2Unit1, /*Dev0,*/ end};
static const BYTE pathDev1Unit2Left[] = {Dev1Unit1, end};
static const BYTE pathDev1Unit2Right[] = {Dev0, end};

static const BYTE pathDev2Unit1Up[] = {Dev1Unit1, Dev1Unit2, /*Dev0,*/ end};
static const BYTE pathDev2Unit1Down[] = {end};
static const BYTE pathDev2Unit1Left[] = {end};
static const BYTE pathDev2Unit1Right[] = {Dev2Unit2, Dev0, end};

static const BYTE pathDev2Unit2Up[] = {Dev1Unit2, Dev1Unit1, /*Dev0,*/ end};
static const BYTE pathDev2Unit2Down[] = {end};
static const BYTE pathDev2Unit2Left[] = {Dev2Unit1, end};
static const BYTE pathDev2Unit2Right[] = {Dev0, end};

static const BYTE pathDev3Unit1Up[] = {Dev4Unit1, Dev4Unit2, /*Dev0,*/ end};
static const BYTE pathDev3Unit1Down[] = {end};
static const BYTE pathDev3Unit1Left[] = {Dev0, end};
static const BYTE pathDev3Unit1Right[] = {Dev3Unit2, end};

static const BYTE pathDev3Unit2Up[] = {Dev4Unit2, Dev4Unit1, /*Dev0,*/ end};
static const BYTE pathDev3Unit2Down[] = {end};
static const BYTE pathDev3Unit2Left[] = {Dev3Unit1, Dev0, end};
static const BYTE pathDev3Unit2Right[] = {end};

static const BYTE pathDev4Unit1Up[] = {end};
static const BYTE pathDev4Unit1Down[] = {Dev3Unit1, Dev3Unit2, /*Dev0,*/ end};
static const BYTE pathDev4Unit1Left[] = {Dev0, end};
static const BYTE pathDev4Unit1Right[] = {Dev4Unit2, end};

static const BYTE pathDev4Unit2Up[] = {end};
static const BYTE pathDev4Unit2Down[] = {Dev3Unit2, Dev3Unit1, /*Dev0,*/ end};
static const BYTE pathDev4Unit2Left[] = {Dev4Unit1, Dev0, end};
static const BYTE pathDev4Unit2Right[] = {end};

static const PATHS pathsDev1Unit1 =
	{
		pathDev1Unit1Up,
		pathDev1Unit1Down,
		pathDev1Unit1Left,
		pathDev1Unit1Right,
};

static const PATHS pathsDev1Unit2 =
	{
		pathDev1Unit2Up,
		pathDev1Unit2Down,
		pathDev1Unit2Left,
		pathDev1Unit2Right,
};

static const PATHS pathsDev2Unit1 =
	{
		pathDev2Unit1Up,
		pathDev2Unit1Down,
		pathDev2Unit1Left,
		pathDev2Unit1Right,
};

static const PATHS pathsDev2Unit2 =
	{
		pathDev2Unit2Up,
		pathDev2Unit2Down,
		pathDev2Unit2Left,
		pathDev2Unit2Right,
};

static const PATHS pathsDev3Unit1 =
	{
		pathDev3Unit1Up,
		pathDev3Unit1Down,
		pathDev3Unit1Left,
		pathDev3Unit1Right,
};

static const PATHS pathsDev3Unit2 =
	{
		pathDev3Unit2Up,
		pathDev3Unit2Down,
		pathDev3Unit2Left,
		pathDev3Unit2Right,
};

static const PATHS pathsDev4Unit1 =
	{
		pathDev4Unit1Up,
		pathDev4Unit1Down,
		pathDev4Unit1Left,
		pathDev4Unit1Right,
};

static const PATHS pathsDev4Unit2 =
	{
		pathDev4Unit2Up,
		pathDev4Unit2Down,
		pathDev4Unit2Left,
		pathDev4Unit2Right,
};

static const PATHS *rgpaths[] =
	{
		&pathsDev1Unit1,
		&pathsDev1Unit2,
		&pathsDev2Unit1,
		&pathsDev2Unit2,
		&pathsDev3Unit1,
		&pathsDev3Unit2,
		&pathsDev4Unit1,
		&pathsDev4Unit2,
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

	// 5960 additions - MU slot counting
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

	//
	//  Tracking the status of found memory units
	//
	DWORD m_dwPendingDevUnits;
	bool m_rgbValidDevUnit[8];
	char m_rgchMount[8];

	DECLARE_NODE_PROPS();
	DECLARE_NODE_FUNCTIONS();
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
// 5960 additions (MU slot counting, separate from block counting)
NODE_FUN_SV(FormatTotalSlots)
NODE_FUN_SI(FormatFreeSlots)
NODE_FUN_II(GetTotalFreeSlots)
END_NODE_FUN()
#undef _FND_CLASS

CMemoryMonitor::CMemoryMonitor()
{
	ASSERT(g_pMemoryMonitor == NULL);
	g_pMemoryMonitor = this;

	ZeroMemory(m_rgchMount, sizeof(m_rgchMount));

	m_nTotalBlocks = -1;
	m_nFreeBlocks = -1;

	m_nPrefRow = 0;
	m_nPrefCol = 0;
	m_nPrefUnit = 0;
	m_curDevUnit = Dev0;

	ZeroMemory(m_rgbValidDevUnit, sizeof(m_rgbValidDevUnit));

	m_invalidDevUnit = -1;
	m_blockInsertion = false;
	m_enumerationOn = false;

	m_dwPendingDevUnits = XGetDevices(XDEVICE_TYPE_MEMORY_UNIT);

	UpdateValidDeviceFlags();
}

CMemoryMonitor::~CMemoryMonitor()
{
}

bool XboxFormatMemoryUnit(UINT nDevicePort, UINT nMemoryUnit);

void CMemoryMonitor::UpdateValidDeviceFlags()
{

	//
	//  If the current scene does not require
	//  enumeration, then just don't bother.
	//
	if (!m_enumerationOn)
		return;

	DWORD dwMUInsertions;
	DWORD dwMURemovals;
	DWORD dwMask;

	if (XGetDeviceChanges(XDEVICE_TYPE_MEMORY_UNIT, &dwMUInsertions, &dwMURemovals) || m_dwPendingDevUnits)
	{
		//
		//  Stave off the screen saver on device removal or insertion
		//
		ResetScreenSaver();

		//
		//  Update the pending list
		//
		m_dwPendingDevUnits &= ~dwMURemovals;
		m_dwPendingDevUnits |= dwMUInsertions;

		for (int i = 0; i < 8; i += 1)
		{
			dwMask = ((i % 2) ? 0x10000 : 1) << (i >> 1);

			//
			//  Handle Removal
			//
			if (dwMURemovals & dwMask)
			{
				//
				//  If device was mounted then unmount it.
				//
				if (m_rgbValidDevUnit[i])
				{
					DWORD dwError = XUnmountMU(i / 2, i & 1);
					if (dwError != ERROR_SUCCESS)
						TRACE(_T("\001XUnmountMU(%d, %d) failed: %d\n"), i / 2, i & 1, dwError);
					else
						TRACE(_T("Unmounted %d %d (was %c:)\n"), i / 2, i & 1, m_rgchMount[i]);
					m_rgchMount[i] = 0;
					g_titles[i].SetRoot(0, false);
					m_rgbValidDevUnit[i] = false;

					if (m_curDevUnit == i)
					{
						TRACE(_T("Pulled out current device; selecting hard drive!\n"));
						m_curDevUnit = Dev0;
						CallFunction(this, _T("OnCurDevUnitChange"));
					}
				}

				if (m_invalidDevUnit == i)
				{
					// Don't fire this more than once
					m_invalidDevUnit = -1;
					CallFunction(this, _T("OnInvalidMURemoved"));
				}

				CallFunction(this, _T("OnDeviceChange"));
			}
			//
			//  If we are not handling a bad unit, then
			//  try to mount any devices on the m_dwPendingDevUnits
			//  list.
			//
			if (!m_blockInsertion && (m_dwPendingDevUnits & dwMask))
			{
				int retry;
				DWORD dwError;

				ASSERT(0 == m_rgchMount[i]);

				//
				//  Clear the pending flag, we will either
				//  mount it, or it becomes the current
				//  bad unit.
				//
				m_dwPendingDevUnits &= ~dwMask;

				//
				//  Make up to three attempts to mount the
				//  thing.
				//
				for (retry = 0; retry < 3; retry++)
				{
					dwError = XMountMURoot(i / 2, i & 1, &m_rgchMount[i]);
					if (dwError == ERROR_SUCCESS)
					{
						XCleanMUFromRoot(m_rgchMount[i], 0);
						break;
					}
				}

				if (dwError == ERROR_SUCCESS)
				{
					TRACE(_T("Mounted %d %d as devUnit %d drive %c:\n"), i / 2, i & 1, i, m_rgchMount[i]);
					m_rgbValidDevUnit[i] = true;
					g_titles[i].SetRoot(m_rgchMount[i], false);
					CallFunction(this, _T("OnDeviceChange"));
				}
				else
				{
					bool bFormatSucceeded = false;

					m_invalidDevUnit = i;

					TRACE(_T("\002XMountMU(%d, %d) failed: %d\n"), i / 2, i & 1, dwError);

					//
					//  Reformat, this will also attempt the mount
					//

					FormatMemoryUnit(i);
					CallFunction(this, _T("OnDeviceChange"));

					//
					//  Notify script that we found a bad device.
					//
					CObject *rgparam[2];
					rgparam[0] = new CNumObject((float)i);
					rgparam[1] = new CNumObject(m_rgbValidDevUnit[i] ? 1.0f : 0.0f);

					CallFunction(this, _T("OnInvalidMU"), 2, rgparam);
				}
			}
		}
	}
}

int CMemoryMonitor::HaveDeviceTop(int nDevice)
{
	if (nDevice < 0 || nDevice > 3)
		return 0;

	return m_rgbValidDevUnit[nDevice * 2];
}

int CMemoryMonitor::HaveDeviceBottom(int nDevice)
{
	if (nDevice < 0 || nDevice > 3)
		return 0;

	return m_rgbValidDevUnit[nDevice * 2 + 1];
}

bool CMemoryMonitor::GetTotalAndFreeBlocks(int devUnit, int *pnTotalBlocks, int *pnFreeBlocks)
{
	char szPath[4];

	if (devUnit == Dev0)
	{
		szPath[0] = 'C';
	}
	else
	{
		szPath[0] = m_rgchMount[devUnit];
		if (szPath[0] == 0)
		{
			TRACE(_T("Attempted GetTotalAndFreeBlocks on unmounted devUnit %d\n"), devUnit);
			return false;
		}
	}

	szPath[1] = ':';
	szPath[2] = '\\';
	szPath[3] = 0;

	ULARGE_INTEGER qwAvailBytes, qwTotalBytes, qwFreeBytes;
	if (GetDiskFreeSpaceEx(szPath, &qwAvailBytes, &qwTotalBytes, &qwFreeBytes))
	{
		int nTotalBlocks = (int)((qwTotalBytes.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE);
		int nFreeBlocks = (int)((qwFreeBytes.QuadPart + BLOCK_SIZE - 1) / BLOCK_SIZE);

		nTotalBlocks -= 1; // we always have a root dir that uses at least one block...

		if (pnTotalBlocks != NULL)
			*pnTotalBlocks = nTotalBlocks;
		if (pnFreeBlocks != NULL)
			*pnFreeBlocks = nFreeBlocks;
	}
	else
	{
		DWORD dwError = GetLastError();
		TRACE(_T("GetDiskFreeSpaceEx(%c:) failed (%d)\n"), szPath, dwError);

		if (devUnit != Dev0)
		{
			m_rgbValidDevUnit[devUnit] = false;
		}

		if (pnTotalBlocks != NULL)
			*pnTotalBlocks = 0;
		if (pnFreeBlocks != NULL)
			*pnFreeBlocks = 0;

		return false;
	}

	return true;
}

void CMemoryMonitor::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	UpdateValidDeviceFlags();

	if (m_curDevUnit != Dev0 && !m_rgbValidDevUnit[m_curDevUnit])
	{
		// We lost the curent memory unit; select the hard drive...
		TRACE(_T("We lost the curent memory unit; select the hard drive...\n"));
		m_curDevUnit = Dev0;
		CallFunction(this, _T("OnCurDevUnitChange"));
	}

	{
		int totalBlocks = 0;
		int freeBlocks = 0;

		GetTotalAndFreeBlocks(m_curDevUnit, &totalBlocks, &freeBlocks);

		if (totalBlocks != m_nTotalBlocks || freeBlocks != m_nFreeBlocks)
		{
			m_nTotalBlocks = totalBlocks;
			m_nFreeBlocks = freeBlocks;
			CallFunction(this, _T("OnTotalFreeChanged"));
		}
	}
}

void CMemoryMonitor::selectUp()
{
	if (m_curDevUnit == Dev0)
	{
		return;
		/*
				m_curDevUnit = (m_nPrefCol == 0 ? Dev1Unit1 : Dev4Unit1) + m_nPrefUnit;
				if (!m_rgbValidDevUnit[m_curDevUnit])
				{
					m_curDevUnit = (m_nPrefCol == 0 ? Dev1Unit1 : Dev4Unit1) + (1 - m_nPrefUnit);
					if (!m_rgbValidDevUnit[m_curDevUnit])
					{
						m_curDevUnit = (m_nPrefCol != 0 ? Dev1Unit1 : Dev4Unit1) + m_nPrefUnit;
						if (!m_rgbValidDevUnit[m_curDevUnit])
						{
							m_curDevUnit = (m_nPrefCol != 0 ? Dev1Unit1 : Dev4Unit1) + (1 - m_nPrefUnit);
							if (!m_rgbValidDevUnit[m_curDevUnit])
							{
								m_curDevUnit = Dev0;
								return;
							}
						}
					}
				}
		*/
	}
	else
	{
		const PATHS *ppath = rgpaths[m_curDevUnit];
		for (const BYTE *path = ppath->rgpath[up]; *path != end; path += 1)
		{
			if (*path == Dev0 || m_rgbValidDevUnit[*path])
			{
				m_curDevUnit = *path;
				break;
			}
		}
	}

	m_nPrefRow = 0;

	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = m_curDevUnit & 1;
		m_nPrefCol = (m_curDevUnit / 2) >= 2;
	}

	CallFunction(this, _T("OnCurDevUnitChange"));
}

void CMemoryMonitor::selectDown()
{
	if (m_curDevUnit == Dev0)
	{
		return;
		/*
				m_curDevUnit = (m_nPrefCol == 0 ? Dev2Unit1 : Dev3Unit1) + m_nPrefUnit;
				if (!m_rgbValidDevUnit[m_curDevUnit])
				{
					m_curDevUnit = (m_nPrefCol == 0 ? Dev2Unit1 : Dev3Unit1) + (1 - m_nPrefUnit);
					if (!m_rgbValidDevUnit[m_curDevUnit])
					{
						m_curDevUnit = (m_nPrefCol != 0 ? Dev2Unit1 : Dev3Unit1) + m_nPrefUnit;
						if (!m_rgbValidDevUnit[m_curDevUnit])
						{
							m_curDevUnit = (m_nPrefCol != 0 ? Dev2Unit1 : Dev3Unit1) + (1 - m_nPrefUnit);
							if (!m_rgbValidDevUnit[m_curDevUnit])
							{
								m_curDevUnit = Dev0;
								return;
							}
						}
					}
				}
		*/
	}
	else
	{
		const PATHS *ppath = rgpaths[m_curDevUnit];
		for (const BYTE *path = ppath->rgpath[down]; *path != end; path += 1)
		{
			if (*path == Dev0 || m_rgbValidDevUnit[*path])
			{
				m_curDevUnit = *path;
				break;
			}
		}
	}

	m_nPrefRow = 1;

	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = m_curDevUnit & 1;
		m_nPrefCol = (m_curDevUnit / 2) >= 2;
	}

	CallFunction(this, _T("OnCurDevUnitChange"));
}

void CMemoryMonitor::selectLeft()
{
	if (m_curDevUnit == Dev0)
	{
		m_curDevUnit = (m_nPrefRow == 0 ? Dev1Unit1 : Dev2Unit1) + 1;
		if (!m_rgbValidDevUnit[m_curDevUnit])
		{
			m_curDevUnit = (m_nPrefRow == 0 ? Dev1Unit1 : Dev2Unit1) + 0;
			if (!m_rgbValidDevUnit[m_curDevUnit])
			{
				m_curDevUnit = (m_nPrefRow != 0 ? Dev1Unit1 : Dev2Unit1) + 1;
				if (!m_rgbValidDevUnit[m_curDevUnit])
				{
					m_curDevUnit = (m_nPrefRow != 0 ? Dev1Unit1 : Dev2Unit1) + 0;
					if (!m_rgbValidDevUnit[m_curDevUnit])
					{
						m_curDevUnit = Dev0;
						return;
					}
				}
			}
		}
	}
	else
	{
		const PATHS *ppath = rgpaths[m_curDevUnit];
		for (const BYTE *path = ppath->rgpath[left]; *path != end; path += 1)
		{
			if (*path == Dev0 || m_rgbValidDevUnit[*path])
			{
				m_curDevUnit = *path;
				break;
			}
		}
	}

	m_nPrefCol = 0;

	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = m_curDevUnit & 1;
		m_nPrefRow = (m_curDevUnit / 2) == 1 || (m_curDevUnit / 2) == 2;
	}

	CallFunction(this, _T("OnCurDevUnitChange"));
}

void CMemoryMonitor::selectRight()
{
	if (m_curDevUnit == Dev0)
	{
		m_curDevUnit = (m_nPrefRow == 0 ? Dev4Unit1 : Dev3Unit1) + 0;
		if (!m_rgbValidDevUnit[m_curDevUnit])
		{
			m_curDevUnit = (m_nPrefRow == 0 ? Dev4Unit1 : Dev3Unit1) + 1;
			if (!m_rgbValidDevUnit[m_curDevUnit])
			{
				m_curDevUnit = (m_nPrefRow != 0 ? Dev4Unit1 : Dev3Unit1) + 0;
				if (!m_rgbValidDevUnit[m_curDevUnit])
				{
					m_curDevUnit = (m_nPrefRow != 0 ? Dev4Unit1 : Dev3Unit1) + 1;
					if (!m_rgbValidDevUnit[m_curDevUnit])
					{
						m_curDevUnit = Dev0;
						return;
					}
				}
			}
		}
	}
	else
	{
		const PATHS *ppath = rgpaths[m_curDevUnit];
		for (const BYTE *path = ppath->rgpath[right]; *path != end; path += 1)
		{
			if (*path == Dev0 || m_rgbValidDevUnit[*path])
			{
				m_curDevUnit = *path;
				break;
			}
		}
	}

	m_nPrefCol = 1;

	if (m_curDevUnit != Dev0)
	{
		m_nPrefUnit = m_curDevUnit & 1;
		m_nPrefRow = (m_curDevUnit / 2) == 1 || (m_curDevUnit / 2) == 2;
	}

	CallFunction(this, _T("OnCurDevUnitChange"));
}

int CMemoryMonitor::selectDevUnit(int DriveLetter)
{
	int nDevUnit;

	DriveLetter = toupper(DriveLetter);

	// Convert drive letter to device unit
	if (DriveLetter == 'T' || DriveLetter == 'U')
	{
		nDevUnit = Dev0;
	}
	else if (DriveLetter >= 'H' && DriveLetter <= 'O')
	{
		nDevUnit = DriveLetter - 'H';
		ASSERT(nDevUnit >= Dev1Unit1 && nDevUnit <= Dev4Unit2);
	}
	else
	{
		TRACE(_T("Invalid drive letter: %c\n"), DriveLetter);
		return false;
	}

	// Save enumeration state
	bool bSaveEnumerationState = m_enumerationOn;

	// Force enumeration in case we haven't done that before
	m_enumerationOn = true;
	UpdateValidDeviceFlags();

	// Restore enumeration state
	m_enumerationOn = bSaveEnumerationState;

	if (nDevUnit != Dev0 && !m_rgbValidDevUnit[nDevUnit])
	{
		return false;
	}

	m_curDevUnit = nDevUnit;
	return true;
}

void FormatDeviceName(int devUnit, TCHAR *szBuf)
{
	ASSERT(g_pMemoryMonitor != NULL);
	if (!g_pMemoryMonitor->FormatDeviceName2(devUnit, szBuf))
		szBuf[0] = 0;
}

CStrObject *CMemoryMonitor::FormatDeviceName(int devUnit)
{
	TCHAR szBuf[MAX_MUNAME];

	if (!FormatDeviceName2(devUnit, szBuf))
		szBuf[0] = 0;

	return new CStrObject(szBuf);
}

bool CMemoryMonitor::FormatDeviceName2(int devUnit, TCHAR *szBuf)
{
	if (devUnit == -1)
		devUnit = m_curDevUnit;

	if (devUnit < Dev1Unit1 || devUnit > Dev4Unit2 || m_rgchMount[devUnit] == 0)
	{
		TRACE(_T("Attempt to get name of unmounted unit: %d\n"), devUnit);
		return false;
	}

	TCHAR szName[MAX_MUNAME];
	DWORD dwError = XMUNameFromDriveLetter(m_rgchMount[devUnit], szName, MAX_MUNAME);
	if (dwError != ERROR_SUCCESS)
		return false;

	_tcscpy(szBuf, szName);
	return true;
}

CStrObject *CMemoryMonitor::FormatFreeBlocks(int devUnit)
{
	int nFreeBlocks;
	if (devUnit == -1)
		nFreeBlocks = m_nFreeBlocks;
	else
		GetTotalAndFreeBlocks(devUnit, NULL, &nFreeBlocks);

	TCHAR szBuf[16];

	FormatBlocks(szBuf, nFreeBlocks);

	return new CStrObject(szBuf);
}

int CMemoryMonitor::GetTotalFreeBlocks(int devUnit)
{
	int nFreeBlocks;
	if (devUnit == -1)
		nFreeBlocks = m_nFreeBlocks;
	else
		GetTotalAndFreeBlocks(devUnit, NULL, &nFreeBlocks);

	return nFreeBlocks;
}

CStrObject *CMemoryMonitor::FormatTotalBlocks()
{
	TCHAR szBuf[16];

	FormatBlocks(szBuf, m_nTotalBlocks);

	return new CStrObject(szBuf);
}

// 5960 additions: MU slot counting
// On HDD (devUnit > 8), total slots is always 8.
// On MU, slots = number of save game "slots" available.

CStrObject *CMemoryMonitor::FormatTotalSlots()
{
	TCHAR szBuf[16];
	int nSlots = m_nTotalBlocks;
	if (nSlots > 8)
		nSlots = 8;
	FormatBlocks(szBuf, nSlots);
	return new CStrObject(szBuf);
}

CStrObject *CMemoryMonitor::FormatFreeSlots(int devUnit)
{
	int nFreeSlots;
	if (devUnit == -1)
		nFreeSlots = m_nFreeBlocks;
	else
		GetTotalAndFreeBlocks(devUnit, NULL, &nFreeSlots);

	if (nFreeSlots > 8)
		nFreeSlots = 8;

	TCHAR szBuf[16];
	FormatBlocks(szBuf, nFreeSlots);
	return new CStrObject(szBuf);
}

int CMemoryMonitor::GetTotalFreeSlots(int devUnit)
{
	int nFreeSlots;
	if (devUnit == -1)
		nFreeSlots = m_nFreeBlocks;
	else
		GetTotalAndFreeBlocks(devUnit, NULL, &nFreeSlots);

	if (nFreeSlots > 8)
		nFreeSlots = 8;
	return nFreeSlots;
}

float CMemoryMonitor::GetFreeTotalRatio(int devUnit)
{
	int nTotalBlocks, nFreeBlocks;
	if (!GetTotalAndFreeBlocks(devUnit, &nTotalBlocks, &nFreeBlocks))
		return 0.0f;

	ASSERT(nTotalBlocks != 0);

	static float nLast[9];
	float nReturn = (float)nFreeBlocks / nTotalBlocks;
	if (nReturn != nLast[devUnit])
	{
		TRACE(_T("GetFreeTotalRatio(%d) %f\n"), devUnit, nReturn);
		nLast[devUnit] = nReturn;
	}

	return (float)nFreeBlocks / nTotalBlocks;
}

void CMemoryMonitor::SetMUName(int devUnit, const TCHAR *szName)
{
	if (devUnit < Dev1Unit1 || devUnit > Dev4Unit2 || m_rgchMount[devUnit] == 0)
	{
		TRACE(_T("Attempt to set name of unmounted unit: %d\n"), devUnit);
		return;
	}

	if (_tcslen(szName) + 1 > MAX_MUNAME)
	{
		TRACE(_T("%s is too long!\n"), szName);
		return;
	}

	XMUWriteNameToDriveLetter(m_rgchMount[devUnit], szName);
}

void CMemoryMonitor::FormatMemoryUnit(int devUnit)
{
	if (m_rgchMount[devUnit] != 0)
	{
		XUnmountMU(devUnit / 2, devUnit & 1);
		m_rgchMount[devUnit] = 0;
		g_titles[devUnit].SetRoot(0, false);
	}

	m_rgbValidDevUnit[devUnit] = false;
	XboxFormatMemoryUnit(devUnit / 2, devUnit & 1);

	DWORD dwError = XMountMURoot(devUnit / 2, devUnit & 1, &m_rgchMount[devUnit]);
	if (dwError == ERROR_SUCCESS)
	{
		TRACE(_T("Mounted %d %d as devUnit %d drive %c:\n"), devUnit / 2, devUnit & 1, devUnit, m_rgchMount[devUnit]);
		m_rgbValidDevUnit[devUnit] = true;

		//
		// Set root drive and force update right away so that the dirty flag
		// won't show up in the next CSavedGameGrid::Advance.  Besides, this
		// should be fast since we just format it to be empty.
		//
		g_titles[devUnit].SetRoot(m_rgchMount[devUnit], true);
	}
}
