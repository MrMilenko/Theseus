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
