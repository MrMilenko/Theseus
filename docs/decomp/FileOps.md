# File Operations & Copy System

The save copier and the file and folder nodes in the 5960 retail binary. Theseus's version is
`theseus/shared/file_ops.cpp` (Xbox build).

## Nodes

| Class | Vtable | FND String |
|-------|--------|------------|
| CCopyDestination | 0x000276e8 | "CopyDestination" |
| CFile | 0x000220a0 | "File" |
| CFolder | 0x00022100 | "Folder" |

CGameCopier is not a node. It's a plain object owned by the save grid that copies saves on a
worker thread.

## CGameCopier

Fields: progress `+0x00`, error `+0x04`, done `+0x05`, thread `+0x08`, source and destination
device `+0x0c`/`+0x10`, their roots `+0x14`/`+0x18`, the queue `+0x1c` with count and capacity at
`+0x20`/`+0x24`, a 16 KB buffer `+0x28`, the internal error and already-exists flags at
`+0x2c`/`+0x2d`, the current entry `+0x30`, total and copied blocks `+0x34`/`+0x38`. A queue entry is
0x14 bytes: title id, save folder, `FILETIME`, and a copied flag.

| Address | Name | What it does |
|---------|------|--------------|
| 0x0003ec90 | RemoveAll | Frees roots, queue strings and queue, zeroes counts and block total, frees the buffer |
| 0x0003ed10 | Finish | Adds every copied save to the destination's title list (removing it first when it overwrote one), then RemoveAll |
| 0x0003eda0 | thread entry | Calls ThreadProc |
| 0x0003edb0 | CopyGame | One save, see below |
| 0x0003f420 | CreateDirectory | Narrows to 260 chars, `CreateDirectoryA`, nonzero result |
| 0x0003f480 | CopyDirectory | Creates the destination, copies every entry but `.` and `..`, recursing |
| 0x0003f700 | CopyFile | 16 KB read/write loop with progress, then copies the file times |
| 0x0003f830 | DeleteFile | `SetFileAttributesA(NORMAL)` then `DeleteFileA` |
| 0x0003f8a0 | DeleteDirectory | Deletes the contents, recursing; optionally the folder itself |
| 0x0003fae0 | ThreadProc | Runs CopyGame over the queue, stops at the first error |
| 0x0003fb40 | SetSource | Device number and a copy of its user data root |
| 0x0003fba0 | SetDestination | Same for the destination |
| 0x0003fc00 | AddGame | Queues a save, growing by 10; adds its blocks to the total |
| 0x0003fd00 | Start | Resets state, allocates the buffer, `CreateThread` (or runs inline if that fails) |

### CopyGame

```
dest = root\title;  newTitle = CreateDirectory(dest);
dest += TitleImage.xbx;
if (newTitle || size(dest) == 0) CopyFile(src\title\TitleImage.xbx, dest);
if (exists(src\title\SaveImage.xbx) && size(dest\title\SaveImage.xbx) == 0) CopyFile(...);
internalError = 0; alreadyExists = 0;
dest = root\title\save;
if (!CreateDirectory(dest)) {
    if (GetLastError() != 0xb7) { internalError = 1; return; }   // ERROR_ALREADY_EXISTS
    alreadyExists = 1; DeleteDirectory(dest, false);
}
h = CreateFile(dest, GENERIC_WRITE, 0, 0, OPEN_EXISTING, BACKUP_SEMANTICS, 0);
if (h != -1) { SetFileTime(h, &t, &t, &t); CloseHandle(h); }
if (!CopyDirectory(src\title\save, dest)) {
    if (newTitle) dest = root\title;
    DeleteDirectory(dest, true); return;
}
games[n].folder = copy of the name after the last '\' in dest;
dest = root\title\TitleMeta.xbx;
if (newTitle || size(dest) == 0) {
    CopyFile(src\title\TitleMeta.xbx, dest);
    if (internalError == 1) internalError = 0;
}
```

The sizes are read by opening the file for reading with every share flag and backup semantics;
one that won't open counts as empty. A missing title image or save image never fails the copy,
and neither does the title meta file, since the last line clears the error it would set.

### CopyFile

```
in = CreateFile(src, GENERIC_READ, SHARE_READ|SHARE_WRITE, 0, OPEN_EXISTING, SEQUENTIAL_SCAN, 0);
if (in == -1) { internalError = 1; return false; }
out = CreateFile(dest, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
if (out == -1) { CloseHandle(in); internalError = 1; return false; }
while (ReadFile(in, buffer, 0x4000, &n, 0)) {
    if (n == 0) goto done;
    if (!WriteFile(out, buffer, n, &w, 0)) break;
    copied++; progress = (float)copied / (float)total;
}
internalError = 1;
done:
if (!internalError) { GetFileTime(in, &c, &a, &w); SetFileTime(out, &c, &a, &w); }
CloseHandle(in); if (!CloseHandle(out)) internalError = 1;
return !internalError;
```

### DeleteDirectory

Lists `<dir>\*.*`, skips `.` and `..`, deletes files with DeleteFile and folders with
DeleteDirectory(path, true); the first failure closes the listing, sets the internal error and
returns false. With removeSelf set it then calls `SetFileAttributesA(dir, NORMAL)` and
`RemoveDirectoryA`.

## CCopyDestination

Fields: pod `+0x1c`, (unused) `+0x20`, panelMU `+0x24`, panelMUHilite `+0x28`, panelText `+0x2c`,
panelTextHilite `+0x30`, console `+0x34`, memoryUnit `+0x38`, curDevUnit `+0x3c`, selDevUnit
`+0x40`, sourceDevUnit `+0x44`, spacing `+0x48` (0.4), isActive `+0x4c`, select `+0x50`, scroll
`+0x54`, scroll target `+0x58`, scroll start time `+0x60` (double), device list `+0x68` (9 ints),
device count `+0x8c`.

**Properties:** pod, panelMU, panelMUHilite, panelText, panelTextHilite, console, memoryUnit,
curDevUnit, selDevUnit, sourceDevUnit, spacing, isActive, select.

**Functions:** selectUp (0x4d350), selectDown (0x4d380).

### Advance (0x0004d110)

While isActive, the device list is rebuilt each frame in the order hard drive (8), then units 0..7.
The source device is left out, and so is any memory unit whose title array has no root; if that
unit was the current one it remembers the current unit was removed. If the current unit is listed
at a different row than `select`, `select`, the target and the scroll offset all jump there. Then
`select` is clamped to `0..count-1`. A new target starts a scroll at the current time; otherwise
an active scroll eases with `t = (now - start) * 4` (a quarter second), clamped to 1, which also
ends it: `scroll = target * spacing * t + (1 - t) * scroll`. Finally if `units[select]` differs from
curDevUnit it's stored and the script hears `OnDestinationUnitRemoved` (when the old one was
pulled) and then `OnCurDevUnitChange`.

### Render (0x0004cf80)

For each listed device: translate by `(0, scroll - i * spacing, 0)`, push the world matrix, draw the
pod, set selDevUnit to the device and call `UpdateMemUnitText`. The current device draws
panelMUHilite and panelTextHilite, the others panelMU and panelText; the hard drive then draws
console, anything else memoryUnit. Pop the matrix.

### selectUp / selectDown

```
if (count > 1 && 0 <= select - 1 < count) {        // + 1 for selectDown
    select -= 1; curDevUnit = units[select]; CallFunction("OnCurDevUnitChange");
}
```

## CFile

Fields: name `+0x1c`, type `+0x20`, path `+0x24`, length `+0x28` (-1 until listed), date `+0x2c`.

**Properties:** name, type, path, length, date. **Functions:** readText (0x31960).

## CFolder

Fields: path `+0x1c`, name `+0x20`, files `+0x24`, subFolders `+0x30`.

**Properties:** path, name, files, subFolders.
**Functions:** sortByName, sortByType, sortByDate, sortByLength (0x31f50..0x32010), each a qsort
of the files with `_wcsicmp` on name or type, `CompareFileTime` on the date, or a size difference.

### Refresh (0x00031b50)

Setting `path` (OnSetProperty 0x31b30 checks offset 0x1c) calls Refresh. It copies at most 255
characters of the path, appends `\*.*` and lists it. When the listing opens, the folder's name
becomes whatever follows the last `\`. Entries with hidden, system (`& 6`) or temporary (`0x100`)
set are skipped. A folder entry becomes a new CFolder with `path\name` and the name. A file entry
becomes a CFile with a CDateObject holding `ftLastWriteTime`, the name up to the last `.`, the
lowercased extension as type (empty without one), `path\name` and `nFileSizeLow`.
