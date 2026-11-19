# TitleCollection

The list of game titles and their saves on one storage device, as the 5960 retail binary keeps
it for the memory screens. Nine arrays sit side by side: slots 0..7 are the memory units, slot 8
the hard drive. Theseus's version is `theseus/shared/title_collection.cpp` (Xbox build); desktop
links empty stubs against the same header.

## Layout

5960's array object is a lot bigger than Theseus's: past the root, dirty flag, count, capacity and
title pointer at `+0x00..+0x14` it carries a 0x700 byte Xbox Live account table at `+0x224` and its
count at `+0x924`, and the critical section at `+0x928`. Each title record is 0x28 bytes: id, name,
save count, saves (0x30 each), save blocks, content count, content list (0x34 each), content blocks,
total blocks, broken flag. A save record is the folder name (16 wide chars) at `+4`, the write time
at `+0x24`, flags at `+0x2c`. Theseus keeps only the title and save parts; Live accounts and
downloadable content are read by the save grid.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x00041dd0 | constructor | Critical section, root `{0, ':', '\\'}`, empty list, dirty, Live account count -1 |
| 0x00041e40 | DeleteAll | Frees every title's id, name, saves and content, empties the list, marks dirty, optionally runs Update |
| 0x00041f20 | AddTitle | Reads `TitleName` (64 chars) from `<udata>\<id>\TitleMeta.xbx`; "Broken Game" (translated) and the broken flag when that fails. Grows the list by 50 |
| 0x00042230 | Update | Lists `<udata>\*.*` under the lock and adds each folder, then sorts by name and scans every title's saves and content |
| 0x00042740 | GetSavedGameCount | Scans a title's saves the first time it is asked |
| 0x00042c20 | FindTitle | First id that matches with `_wcsicmp`, else -1 |
| 0x00042cb0 | AddSavedGame | Adds a save (and its title if new), resorts, resets the title's sizes |
| 0x00042e40 | RemoveTitle | Frees one title and closes the gap |
| 0x00042ef0 | RemoveSavedGame(n, s) | Closes the gap in the save list, resets sizes |
| 0x00042f40 | RemoveSavedGame(id, dir) | Finds the title and the save by folder name, then removes it |
| 0x00043390 | GetTitleTotalBlocks | Cached udata count plus tdata on the hard drive |
| 0x00043460 | ComputeTitleTotalBlocks | `GetDirectoryBlocks(<root>\<id>, 0x4000, recursive, cancel)` |
| 0x000434f0 | ScanSavedGames | Lists a title folder for save folders, newest first |
| 0x00043c30 | GetUData | Root, `c:\udata` for the hard drive, `""` when unmounted |
| 0x00043c50 | IsDirty | Mounted and dirty |
| 0x00043d00 | GetSavedGameImageName | Save image, title's save image, or the Xbox logo |
| 0x00044240 | IsPublisherExists | Any title id that shares the first 4 characters (`_wcsnicmp`) |

## Update

```
EnterCriticalSection(&lock);
if (root[0] == 0) { LeaveCriticalSection(&lock); dirty = 0; return; }
... Live accounts for this device ...
h = FindFirstFileA(narrow(udata + "\\*.*"), &fd);
if (h == -1) {
    if (GetLastError() != 0x48f) dirty = 0;      // ERROR_DEVICE_NOT_CONNECTED
    LeaveCriticalSection(&lock); return;
}
do { if (fd.attrib & 0x10) AddTitle(widen(fd.cFileName)); } while (FindNextFileA(h, &fd));
if (GetLastError() == 0x48f) { CloseHandle(h); DeleteAll(false); Leave...; return; }
CloseHandle(h);
qsort(titles, count, 0x28, by name);
for each title {
    scratch = new CSave[0x1000];
    title.saveCount = ScanSavedGames(udata, title.id, scratch, NULL);
    title.saves = new CSave[title.saveCount]; memcpy(...); delete[] scratch;
    ... the same for downloadable content under tdata ...
}
LeaveCriticalSection(&lock); dirty = 0;
```

A device that drops out while being listed is treated as not there; anything else that stops the
listing still counts as a finished scan. Theseus leaves out the Live account and content parts,
and skips the `fffe0000` folder (the dashboard's own data), which 5960 lists like any title.

## ScanSavedGames

Returns -1 straight away if the cancel event is set. Lists `<root>\<id>\*.*`; for each entry it
checks the cancel event (-1 when set), stops at 0x1000 saves, skips files, and for a folder zeroes
the record, keeps it only when `wcslen(name) + 1 < 0x11`, copies the name and `ftLastWriteTime` and
sets flag 2 (image unknown). The result is sorted with the newest save first.

## GetTitleTotalBlocks

```
if (n < 0 || n >= count) return 0;
if (title.blocks == -1) {
    title.blocks = ComputeTitleTotalBlocks(udata, title.id, cancel);
    if (root[0] == 'c' && title.blocks >= 0 && wcscmp(title.id, L"fffe0000") != 0)
        title.blocks += ComputeTitleTotalBlocks(tdata, title.id, cancel);
}
if (cancel && WaitForSingleObject(cancel, 0) == 0) title.blocks = -1;
return title.blocks;
```

The last line makes a count that was cut short get taken again next time.

## GetSavedGameImageName

```
if (save.flags & 2) {
    save.flags &= ~2;
    path = udata\id\save\SaveImage.xbx;
    if (DoesFileExist(path)) { save.flags |= 1; return; }
}
path = udata\id;
if (save.flags & 1) path += \save;
path += \SaveImage.xbx;
if (!DoesFileExist(path)) path = <default image>;
```
