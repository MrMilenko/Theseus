# Settings & ScreenSaver Nodes

Two small script nodes in the 5960 retail binary. `Settings` exposes one section of an
INI style file to script; `ScreenSaver` is the idle timer the dashboard uses to dim the
screen. The file format itself is in [Settings.md](Settings.md).

## Settings

0x30 bytes, factory `0x000418a0`, vtable `0x00025f70`. Props: `file` (string, 0x1c) and
`section` (string, 0x20). The settings file object sits at 0x24. Function table:
`GetValue` (`0x00182010`, sig 5) and `SetValue` (`0x00182028`, sig 13).

Both script functions first check the open file: if none is open, or its name doesn't
match `file` ignoring case, they close it and open `file`.

- **SetValue** (`0x000419e0`) finds the section, creating it if needed, sets the value,
  and when that changed anything marks the file dirty and saves it straight away.
- **GetValue** (`0x00041a50`) returns an empty string when the file won't open;
  otherwise it reads the value into a 0x400 character buffer and returns it as a new
  string.
- **Dot** (`0x00041950`) tries the normal member lookup first. Any other bare name
  becomes a `CSetting`, a 0x1c byte value object (vtable `0x00025f38`) holding the
  Settings node at 0x14 and a copy of the name at 0x18. That's what makes
  `theSettings.Name` read and write the file.
- **CSetting**: `Assign` (`0x00041b80`) dereferences the value, takes its text and calls
  `SetValue(name, text)`. `ToStr` (`0x00041bb0`) is `GetValue(name)`. `Deref`
  (`0x00041bc0`) is `GetValue(name)` followed by releasing itself. The destructor
  (`0x00041b60`) frees the name.
- The destructor (`0x00041920`) frees both strings and closes the file.

## ScreenSaver

0x38 bytes, factory `0x000400f0`, vtable `0x00025758`. Fields: enabled 0x1c,
isActive 0x1d, isActive2 0x1e, isActive3 0x1f, delay 0x20, delay2 0x24, delay3 0x28,
time of the last input 0x30 (double). The factory sets enabled, delay 300 seconds
(`0x0002c030`), the other two delays 0, all three active flags off, stamps the last
input with the clock and stores itself in the global at `0x0017e738`. The destructor
(`0x00040190`) clears that global if it points here.

**Advance** (`0x000401b0`), all comparisons in double precision:

    CNode::Advance(secs);
    if (!enabled) { if (isActive) { isActive = 0; OnEnd(); } }
    else if (!isActive && lastInput + delay <= now) { isActive = 1; OnStart(); }
    if (0 < delay2 && !isActive2 && lastInput + delay2 <= now) { isActive2 = 1; OnDelay2(); }
    if (0 < delay3 && !isActive3 && lastInput + delay3 <= now) { isActive3 = 1; OnDelay3(); }

The handlers are script functions on the node, called by name. The second and third
stages don't look at `enabled`.

**reset** (`0x000402a0`, script `reset`, FND `0x00181f50`, sig 1): ends an active saver
with `OnEnd`, clears the other two flags and stamps the last input time.

**OnSetProperty** (`0x000402d0`): writing `enabled` runs the same reset when the new
value is true, then stores the value itself.

**ResetScreenSaver** (`0x00040320`) is what the input code calls on every pad or
keyboard event. It stamps the global last input time (`0x0017c030`), and if a saver
exists it remembers whether it was active, resets it, calls
`XAutoPowerDownResetTimer` and returns the remembered flag, so the caller can swallow an
event that only woke the screen.

## Theseus notes

- `Settings.Reload` and `ScreenSaver.StartAfter` / `SetDelay2` / `SetDelay3` are
  Theseus additions.
- The power down timer reset is Xbox only.
