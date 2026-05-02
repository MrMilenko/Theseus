# Date

CDateObject implements a JavaScript-style Date object for the XAP script VM. Exposes local and UTC time accessors, string formatters, and system clock setting. Backed by `date_node.cpp` in the active source tree.

## Binary Analysis (4920-5960 retail XBE)

### FND Table Strings
Function names found in the binary string table:
- `getDate` at `0x00021e60`
- `toGMTString` at `0x00021fec`
- `isLeapYear` at `0x0002203c`
- `getDaysInMonth` at `0x00022054`
- `SetSystemClock` at `0x00022074`

### FND Table Data
The CDateObject FND entries live in the `.bss` region around `0x000c9e00-0x000ca200`.
These are 24-byte entries (function pointer + signature + name pointer), populated at runtime
by the IMPLEMENT_NODE / START_NODE_FUN macros.

### SetSystemClock
Identified via `ExReadWriteRefurbInfo` import (kernel ordinal 0x0c).
Two callsites found at `0x00092257` and `0x00092a77`, one is SetSystemClock,
the other is likely the recovery/refurb path.

SetSystemClock calls:
- `FileTimeToSystemTime` to convert stored FILETIME
- `XapiSetLocalTime` to set the clock
- `GetLocalTime` + `SystemTimeToFileTime` to verify the result
- Checks offset > 1 minute (600000000 * 100ns = 60s), re-sets if DST boundary crossed
- Reads refurb info via `ExReadWriteRefurbInfo`, writes FirstSetTime if zero

### Local Time Accessors (getDate, getDay, getFullYear, etc.)
Trivial pattern: `FileTimeToLocalFileTime` -> `FileTimeToSystemTime` -> return field.
Each accessor is a thin wrapper returning one SYSTEMTIME field.

### UTC Accessors (getUTCDate, getUTCDay, etc.)
Same pattern without the `FileTimeToLocalFileTime` step.

### Constructor
Accepts 0 args (current time) or 3-7 args (year, month, day, [hour, min, sec, ms]).
Two-digit years get +1900. Month is 0-based (JS convention), converted to 1-based for SYSTEMTIME.

