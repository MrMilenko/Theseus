# Memory Monitor

The `MemoryMonitor` node in the 5960 retail binary (one global instance at `0x0017c610`). Memory
units are numbered `port * 2 + slot` (0..7) with the hard drive as 8. Layout: `curDevUnit` +0x1c,
`invalidDevUnit` +0x20, `blockInsertion` +0x24, `enumerationOn` +0x25, block totals +0x28/+0x2c,
slot totals +0x30/+0x34, preferred row/side/slot +0x38..+0x40, pending units +0x44, mounted flags
+0x48, mount letters +0x50.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x0004be70 | CMemoryMonitor::CMemoryMonitor | Totals -1, hard drive selected, everything plugged in pending, one enumeration |
| 0x0004bf50 | CMemoryMonitor::UpdateValidDeviceFlags | Below |
| 0x0004c1f0 / 0x0004c210 | HaveDeviceTop / HaveDeviceBottom | Mounted flag of a port's slot, 0 for a bad port |
| 0x0004c3b0 | CMemoryMonitor::GetTotalAndFreeBlocks | `C:` or the unit's letter through `GetDiskFreeSpaceEx`, rounded up to 16 KB blocks, total less one for the root; a failure marks a unit gone and gives 0 |
| 0x0004c230 | (slot counter) | Xbox Live accounts: 8 on the hard drive less those stored, 1 on a unit without one |
| 0x0004c480 | CMemoryMonitor::Advance | Enumerate, fall back to the hard drive, refresh the selected drive's blocks, `OnTotalFreeChanged` |
| 0x0004c510..0x0004c6e0 | selectUp/Down/Left/Right | Below |
| 0x0004c7d0 | selectDevUnit | T or U is the hard drive, F..M a unit; enumerates once, selects if mounted |
| 0x0004c840 / 0x0004c8c0 | FormatDeviceName | `XMUNameFromDriveLetter` of a mounted unit (-1 is the selection), else "" |
| 0x0004cbc0 / 0x0004ca90 / 0x0004cb90 | FormatTotalBlocks / FormatFreeBlocks / GetTotalFreeBlocks | `%d`, capped as "50000+" |
| 0x0004c930 / 0x0004c9d0 / 0x0004cb60 | FormatTotalSlots / FormatFreeSlots / GetTotalFreeSlots | Slot counts, capped as "8+" |
| 0x0004cc70 | GetFreeTotalRatio | free / total, 0 when the drive can't be read |
| 0x0004ccd0 | SetMUName | Names a mounted unit when the name fits in 32 characters |
| 0x0004cd10 | FormatMemoryUnit | Unmount, format (0x0002dcc0), remount, rescan its titles |

## UpdateValidDeviceFlags

Runs only with `enumerationOn`, and only when `XGetDeviceChanges` reports something or a unit is
pending. Wakes the screen saver, then `pending = (pending & ~removed) | inserted` and for each unit
(bit `1 << port` for the top slot, `0x10000 << port` for the bottom):

- Removed: if mounted, unmount, clear its titles, and if it was selected select the hard drive
  (`OnCurDevUnitChange`). If it was the bad unit, `OnInvalidMURemoved`. Then `OnDeviceChange`.
- Pending and insertion not blocked: up to three `XMountMURoot` tries. Mounted: clean it, set its
  titles' root, `OnDeviceChange`. Not: it becomes the bad unit, is formatted, `OnDeviceChange`, then
  `OnInvalidMU(unit, mounted now ? 1 : 0)`.

## Selection

Each unit has a path per direction (table `0x00174b38`, terminated by 0xFF); the first entry that is
the hard drive or a mounted unit wins. From the hard drive, up and down do nothing; left tries the
preferred row's port 1 or 2 (bottom slot first) then the other row, right ports 4 or 3 (top slot
first); nothing there leaves the hard drive selected with no event. Every move records the
preferred row (up 0, down 1) or side (left 0, right 1) and the unit's slot, then fires
`OnCurDevUnitChange`.

Theseus mounts units at H..O rather than F..M, counts slots from blocks (capped at 8) instead of
accounts, refuses `FormatMemoryUnit` for the hard drive, and returns 0 for an empty drive's ratio.
