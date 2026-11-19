# SavedGameGrid

The Memory blade's list in the 5960 retail binary. It shows every title on one device, each with a grid of
save icons, plus a soundtrack row on the hard drive. Theseus's version is
`theseus/shared/savegame_grid.cpp` (Xbox build). Vtable 0x000267b0.

## Fields that matter

curTitle `+0x8c`, scroll `+0x88`, icons per row `+0x84` (4), last title `+0x90`, curGridItem
`+0x94`, icon row scroll `+0xa4`, curDevUnit `+0xb0`, copyProgress `+0xb4`, freeBlocks `+0xb8`,
isActive `+0xc0`, busy `+0xc1`, preferred column `+0xc4`, total blocks `+0xd0`, scroll target
`+0xd4`, scroll start `+0xd8` (double), copying `+0xe4`, soundtrack row and count `+0x100`/`+0x104`,
Live account row and count `+0x108`/`+0x10c`, cached title and item sizes `+0x110`/`+0x114`,
copy dots tick/count/text `+0x118`/`+0x11c`/`+0x120`, list thread `+0x920`, size worker thread
and events `+0x924..+0x930`, query pending `+0x934`, quit `+0x935`, list end time `+0x938`,
delete thread `+0x940`, deleting `+0x944`.

## Functions

| Address | Name | Notes |
|---------|------|-------|
| 0x00047320 | constructor | Defaults, three events, the size worker (0x47c70) |
| 0x00047520 | destructor | Releases 25 nodes, stops the worker |
| 0x000476f0 | GetTitleCount | Titles + soundtrack row + Live account row |
| 0x000477b0 | title blocks | Soundtrack row sizes `c:\tdata\fffe0000\music`; Live row its count |
| 0x00047970 | GetTitleName2 | Device row: "Xbox Hard Disk", the MU's name, or "memory unit %d%c" |
| 0x00047bb0 | TitlesEnumThread | Update the device, place the soundtrack and Live rows |
| 0x00047e50 | Advance | See below |
| 0x00048450 | GetUpdateString | "Copying" plus 0..3 dots, every 500 ms |
| 0x00048530 | OnSetProperty | curDevUnit (0xb0) selects the first row at once; curTitle (0x8c) selects; isActive (0xc0) going off cancels the size query |
| 0x00048690 | RenderIconRow | |
| 0x000488a0 | Render | |
| 0x00048910 | RenderLoop | See below |
| 0x00049410 | SelectTitle | |
| 0x00049590..0x000497b0 | selectUp/Down/Left/Right | |
| 0x00049840 | setSelImage | |
| 0x00049940..0x0004a3e0 | Format* | Name from SaveMeta.xbx `Name`, time from the folder, sizes from the cache |
| 0x0004a700 | GetSavedGamePath | `<drive>:\<save>`, U: on the hard drive |
| 0x0004aac0 | StartSavedGameCopy | Folder blocks plus whichever of TitleMeta/TitleImage/SaveImage the destination lacks |
| 0x0004b160 | title id parse | 8 hex digits |
| 0x0004b1d0 | DeleteThread | |
| 0x0004b630 | StartDelete | |
| 0x0004b6b0 | CanCopy | |

## Advance

```
CNode::Advance;
if (curDevUnit == -1 || !isActive) return;
if (!listThread) {
    if (device mounted && dirty) {
        rows = -1; endAt = now + 1.0; OnUpdatingTitlesBegin;
        listThread = CreateThread(TitlesEnumThread);
        if (listThread) return;
        TitlesEnumThread(); OnUpdatingTitlesEnd;
    }
} else {
    if (now <= endAt || thread still running) return;
    OnUpdatingTitlesEnd; CloseHandle; listThread = 0;
}
if (copying) {
    progress changed -> OnCopyProgressChanged;
    error -> Finish, OnCopyError;  done && now >= start + 0.5 -> Finish, OnCopyComplete;
    return;
}
if (deleting) { thread done -> OnDeleteEnd, OnSelChange, restart pulse; return; }
if (device unmounted) { OnDeviceRemoved; curDevUnit = -1; return; }
if (dirty) return;
... size cache upkeep, wake the worker ...
free/total blocks = ceil(bytes / 0x4000) (total - 1); a failed query is 1 total, 0 free;
    change (or new language) -> OnTotalFreeChanged, OnSelChange;
if (scrollStart) { t = (now - start) * 4; clamp, stop at 1; scroll = (1-t)*scroll + t*target; }
```

Theseus splits the size cache and free space work into helpers. It also invalidates its DLC
count cache when the list is rebuilt.

## RenderLoop

The frame is a rotation of -1.571 about -x times a uniform 0.05942 scale. Starting at `y = scroll`,
each row (the device row first, except on the copy destination view) is:

- pod at (-0.3292, y, -0.0271); the selected one, once settled, from a remembered matrix;
- header at the same spot, with its name at (-0.3292 - 0.05942, y + 0.05942, -0.0271 + 0.6 * 0.05942)
  scaled 0.06442, tinted 170,170,170 when selected and 140,201,25 otherwise;
- `y -= 0.0092`; first icon row at (-0.3292, y, 0.01144), `y -= 0.2455`;
- second row at (-0.3301, y, 0.01144), `y -= 0.24499`, then further rows at -0.3412, up to three
  visible rows, with the more-down arrow at (-0.17, y - 0.015) and more-up at (-0.17, y - 0.07);
- `y -= 0.09` between titles.

Icons sit `smallIconSpacing` apart. Rows stop drawing below -1.25 and only draw when
`y - 0.25 < 0.5`. Unselected rows draw at half strength, easing as the selection moves. Called with
render off, it returns the selected row's offset from the top, which SelectTitle scrolls to.

## SelectTitle

```
lastTitle = curTitle; curTitle = n;
if (device mounted && dirty) return;
clamp curTitle to the row count; items = grid items; rows = ceil(items / perRow);
target = -RenderLoop(false);
if (target != scrollTarget) { scrollTarget = target; instant ? scroll = target, start = 0.001 : start = now; }
pref = (pref >= 0 && item != -1) ? item % perRow : (pref != -1 ? 0 : -1);
if (pref == -1) { item = -1; rowScroll = 0; return; }
if (curTitle > lastTitle) { item = min(pref, items - 1); rowScroll = 0; }
else { item = items ? min(pref + (rows - 1) * perRow, items - 1) : -1; rowScroll = max(rows - 3, 0); }
```

5960 counts every grid item here; Theseus counts saves only.

## DeleteThread

With no item selected, the whole row goes: all soundtracks, the Live accounts, or a title. A title
is removed from the list and from `<udata>\<id>`. The publisher's `<pub>ffff` tdata folder goes too
when it was the publisher's last title. On the hard drive, `<tdata>\<id>` and the cache partition
also go, except for `fffe0000`. The rows below move up one. With an item selected, just that
soundtrack, account or save folder goes, and the selection and row scroll are pulled back into
range. Both cases reset the cached sizes.
