# Settings

Settings and screensaver subsystem confirmed in the 4920-5960 retail binary. Two parts: a script-accessible node layer (CSettings, CScreenSaver) and an INI-style file parser (`settingsfile.cpp`) that backs them.

## Nodes

| Class | Address |
|-------|---------|
| CSettings | 0x00026004 |
| CScreenSaver | 0x00025868 |

### CSettings

Reads and writes Xbox system settings: language, clock, timezone, video mode, audio mode, parental control, and related configuration values.

### CScreenSaver

Screen saver timer. Calls XAutoPowerDownResetTimer to manage the auto power-down idle timeout.

## Settings File Format

Standard INI layout:

```
[Section]
Name=Value
Name2=Value2

[AnotherSection]
Key=Data
```

The parser handles both Unicode and ANSI file formats at runtime. The `m_bUnicode` flag is set during file open based on the presence of a BOM (byte order mark) at the start of the file. All subsequent read/write operations branch on this flag to use the appropriate string functions.

## Key Functions

- **Open / Save**: Read an .xbx file into memory, or flush the in-memory representation back to disk.
- **GetValue / SetValue**: Look up or modify a value by section name and key name.
- **Section enumeration**: Walk all sections and their key/value pairs.

## XI_GetProgramPath()

Utility function that resolves the directory containing the running XBE at runtime. Used to locate settings files relative to the dashboard's install path (e.g. `D:\Xbox.xbx` when running from the HDD, or the DVD path during development). Calls `XGetModuleFileName` and strips the executable name to get the directory.

## CSettingsFile in the binary

| Address | Name | What it does |
|---------|------|--------------|
| 0x0003bc00 | CSettingsFile::Open | Copies the path, fetches the file with `CActiveFile` and widens it, then parses it |
| 0x0003bf00 | CSettingsFile::Save | Does nothing unless dirty; writes ANSI through `CreateFile` (`[name]\r\n`, `name=value\r\n`, each wide character narrowed) or Unicode through `_wfopen` and `fwprintf` (`[%s]\n`, `%s=%s\n`), then clears the dirty flag |
| 0x0003c100 | CSettingsFile::Close | Saves (stays open if that fails), frees the sections and the path |
| 0x0003c150 | CSettingsFile::FindSection | Linear search by name; with create set, pushes a new section on the front |
| 0x0003c1e0 | CSettingsFile::GetValue | Clears the output, tries the section then `default`, `wcsncpy` and terminates at size - 1 |
| 0x0003c260 | CSettingsFileSection::SetValue | Finds the name or pushes a new entry on the front; returns false when the value is unchanged |

The object is 12 bytes: path, section list, dirty flag at +8 and the Unicode flag at +9. A section
and a value are each 12 bytes too (next, name, then the value list or the value).

`Open` reads lines after `SkipWhite` with no comment style. `[name]` (up to 255 characters, and only
when the `]` is there) selects or creates a section. `name=value` lines (name up to 255, value up to
1023) go into the current section, creating `default` when none came first. Lines without `=` are
skipped. Overflowing any of the three buffers closes the file and fails.

Theseus differences: the value buffer is 16384 characters (long cache.ini lines), saves go to
`<file>.new` and replace the old file only when the write fully succeeded, writes can be batched
with `BeginBatch` / `EndBatch`, and `XI_GetProgramPath` and `OpenDir` are Theseus helpers.

## The Settings and ScreenSaver nodes

| Address | Name | What it does |
|---------|------|--------------|
| 0x000418a0 | CSettings::CreateNode | 0x30 bytes: `file`, `section`, then an embedded CSettingsFile at +0x24 |
| 0x00041920 | CSettings::~CSettings | Frees `file` and `section`, closes the settings file |
| 0x00041950 | CSettings::Dot | The node's own members first; any other name (a type 4 variable) becomes a 0x1c byte setting object holding the node and a copy of the name |
| 0x000419e0 | CSettings::SetValue | Reopens when no file is open or `file` differs ignoring case, sets the value in `section` and saves when it changed |
| 0x00041a50 | CSettings::GetValue | Same reopen (empty string if it fails), then reads the value into 1024 characters |
| 0x00041b60 | (setting) destructor | Frees the name |
| 0x00041b80 | (setting) Assign | `value->Deref()->ToStr()`, then `SetValue(name, text or "")` |
| 0x00041bc0 | (setting) Deref | `GetValue(name)`, then releases itself |
| 0x000400f0 | CScreenSaver::CreateNode | 0x38 bytes: enabled, nothing active, `delay` 300 (`0x0002c030`), the clock read from `0x0017e808`; registers itself at `0x0017e738` |
| 0x00040190 | CScreenSaver::~CScreenSaver | Clears the registration if it is this node |
| 0x000401b0 | CScreenSaver::Advance | See below |
| 0x000402a0 | reset | OnEnd if active, rearms stages 2 and 3, restarts the clock |
| 0x000402d0 | CScreenSaver::OnSetProperty | `enabled` set to true resets first; the value is stored either way |
| 0x00040320 | ResetScreenSaver | Stamps the global last event time (`0x0017c030`); with a ScreenSaver, resets it, calls `XAutoPowerDownResetTimer` and returns whether it was active |

`Advance`: when disabled, an active saver ends with OnEnd. When enabled and idle, `delay` seconds
after the last event it starts with OnStart. Independently of `enabled`, `delay2` and `delay3`,
when above 0, fire OnDelay2 and OnDelay3 once each after that long. The comparisons are done in
float.

The ScreenSaver's PRD (`0x001745d8`) is enabled, isActive, isActive2, isActive3, delay, delay2,
delay3 and its function table holds only `reset`. Theseus adds StartAfter, SetDelay2, SetDelay3 and
Settings.Reload.
