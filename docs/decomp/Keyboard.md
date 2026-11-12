# On-Screen Keyboard

The `Keyboard` node in the 5960 retail binary: the grid of keys behind every name, passcode and
soundtrack prompt. The script moves a highlight around the grid and presses keys; the node keeps a
31 character edit string with a cursor and hands it to a Text node to draw.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x0005ab30 | CKeyboard::GetPropMap | Returns the PRD at `0x00175448` |
| 0x0005ab40 | CKeyboard::GetFunctionMap | Returns the FND at `0x00188250` |
| 0x0005ab50 | CKeyboard::CreateNode | Factory for `Keyboard`, 0x9c bytes |
| 0x0005ab70 | CKeyboard::GetNodeClass | Returns the node class at `0x00188224` |
| 0x0005ab80 | CKeyboard::CKeyboard | Constructor |
| 0x0005ac20 | CKeyboard::~CKeyboard | Destructor |
| 0x0005acb0 | GetAsteriskFormattingState | Reads the *heading* flag |
| 0x0005acc0 | SetAsteriskFormattingState | Writes the *heading* flag |
| 0x0005ace0 | selectKey | Puts the highlight on a cell |
| 0x0005ad40 | selectUp | Highlight up |
| 0x0005adb0 | selectDown | Highlight down |
| 0x0005ae60 | selectLeft | Highlight left |
| 0x0005af50 | selectRight | Highlight right |
| 0x0005b040 | TIDactivate | Presses whatever key a USB keyboard just typed |
| 0x0005b340 | activate | Presses the highlighted key |
| 0x0005b630 | Backspace | Erases left of the cursor |
| 0x0005b6a0 | Delete | Erases under the cursor |
| 0x0005b700 | CursorLeft | Cursor left |
| 0x0005b740 | CursorRight | Cursor right |
| 0x0005b780 | Shift | Toggles shift |
| 0x0005b7b0 | CycleMode | Next page |
| 0x0005b7e0 | Insert | Inserts a string at the cursor |
| 0x0005b890 | (grid char test) | Whether a character is on any page of the current grid |
| 0x0005b970 | CKeyboard::OnSetProperty | Loads `string` into the edit buffer |
| 0x0005b9d0 | CKeyboard::Render | Frame, text, then one node per key |
| 0x0005bca0 | CKeyboard::Advance | Advances the control node too |

## The object

| Offset | Field |
|--------|-------|
| +0x1c | `keys`, a nodearray (length at +0x1c, entries at +0x24) |
| +0x28 | `frame` |
| +0x2c | `text` |
| +0x30 | `control` |
| +0x34 | `string` |
| +0x38 | `shift` (byte) |
| +0x39 | `caps` (byte) |
| +0x3a | `TIDKeyUsed` (byte) |
| +0x3b | `PlaySound` (byte) |
| +0x3c | `CloseKeyboard` (byte) |
| +0x40 | `mode`, the western page |
| +0x44 | `jmode`, the Japanese page |
| +0x48 | edit buffer, 32 WCHARs |
| +0x88 | edit length |
| +0x8c | cursor, -1 when the whole string is selected |
| +0x90 | highlighted row |
| +0x94 | highlighted column |
| +0x98 | a WORD key code that stands in for the grid during one `activate` (TIDactivate's) |

The constructor (`0x0005ab80`, and the same stores inlined into CreateNode) zeroes every field
and sets the cursor to -1. The destructor releases `text`, `frame` and `control` in that order,
frees `string` and releases the nodearray.

## Properties

PRD at `0x00175448`, dumped with DumpPRD:

| Name | Type | Offset |
|------|------|--------|
| keys | 11 (nodearray) | 0x1c |
| frame | 9 (node) | 0x28 |
| text | 9 (node) | 0x2c |
| control | 9 (node) | 0x30 |
| mode | 1 (integer) | 0x40 |
| jmode | 1 (integer) | 0x44 |
| shift | 2 (boolean) | 0x38 |
| caps | 2 (boolean) | 0x39 |
| TIDKeyUsed | 2 (boolean) | 0x3a |
| PlaySound | 2 (boolean) | 0x3b |
| CloseKeyboard | 2 (boolean) | 0x3c |
| string | 10 (string) | 0x34 |

## Script functions

FND at `0x00188250`: selectKey (sig 18), selectUp, selectDown, selectLeft, selectRight, activate,
TIDactivate, Backspace, Delete, CursorLeft, CursorRight, Shift, CycleMode (all sig 1), Insert
(sig 12), SetAsteriskFormattingState (sig 14), GetAsteriskFormattingState (sig 2).

Callbacks it raises: `OnDone` and `OnError`.

## Globals

| Address | What |
|---------|------|
| 0x0017c024 | current language; 1 (Japanese) switches to the 9 row grid |
| 0x0017e748 | the string the next Text node draws |
| 0x00175220 | the cursor index for that Text node, -1 for none |
| 0x0017e74c | *heading* formatting flag, a BSS byte (starts off) |
| 0x0017e74d | "this key is highlighted", read by the key nodes |
| 0x0017f460 | WORD key code of the last USB keyboard keystroke |
| 0x0017f462 | WORD character of that keystroke |

The two keystroke words belong to the input object: the poll at `0x00040520` walks the eight
keyboard handles, takes the first `XInputGetKeystroke` that succeeds, resets the screen saver and
copies the 8 byte keystroke to the object's +0x28. The joystick node's Advance (`0x00041040`)
raises `OnTIDKeyDown` when the key code is nonzero and that joystick is the bound one.

## The grids

Six WORD tables, 11 columns each, listed through `0x00175430` (alpha `0x000211b8`, symbols
`0x00021228`, accents `0x00021298`, hiragana `0x00021308`, katakana `0x000213d0`, Japanese
English `0x00021498`). Western grids are 5 rows, Japanese 9. Codes 1 to 14 are function keys:

| Code | Key | Label |
|------|-----|-------|
| 1 | shift | SHIFT |
| 2 | caps lock | CAPS LOCK |
| 3 | alphabet page | ALPHABET |
| 4 | symbols page | SYMBOLS |
| 5 | accents page | ACCENTS |
| 6 | cursor left | `<` |
| 7 | cursor right | `>` |
| 8 | backspace | BACKSPACE |
| 9 | done | DONE |
| 10 | space | SPACE |
| 11 | hiragana page | HIRAGANA |
| 12 | katakana page | KATAKANA |
| 13 | English page | ENGLISH |
| 14 | blank cell | a space, never drawn |

Western alpha page (symbols and accents swap the characters and the page keys):

```
DONE     1 2 3 4 5 6 7 8 9 0
SHIFT    a b c d e f g h i j
CAPS     k l m n o p q r s t
ACCENTS  u v w x y z   BACKSPACE x4
SYMBOLS  SPACE x6      < < > >
```

The western bottom two rows repeat a key across cells (BACKSPACE in 4, SPACE in 6, `<` and `>` in
2 each), which is why a western keyboard has 45 key nodes for 55 cells. The Japanese grids put the
page keys, SPACE, BACKSPACE, `<`, `>` and DONE down column 10 and leave a few blank cells (code 14)
in rows 0 and 7.

## Moving the highlight

**selectKey (`0x0005ace0`).**

```c
last = (lang == 1) ? 8 : 4;
row = (row < 0 || row > last) ? 0 : row;
col = (col >= 0 && col < 11) ? col : 0;
```

**selectUp / selectDown (`0x0005ad40`, `0x0005adb0`).**

```c
do { row = row ? row - 1 : last; } while (grid[row*11 + col] == 14);          // up
do { row = (row == last) ? 0 : row + 1; } while (grid[row*11 + col] == 14);   // down
```

**selectLeft (`0x0005ae60`).**

```c
for (;;) {
    while (col == 0) { col = 10; if (grid[row*11 + 10] != 14) return; }
    if (lang != 1) {
        if (row == 3) { if (col > 6) { col = 6; return; } }
        else if (row == 4) { if (col > 8) { col = 8; return; } col = (col < 7) ? 0 : 6; return; }
        col--; return;
    }
    col--; if (grid[row*11 + col] != 14) return;
}
```

The row 4 step is compiled as `((col < 7) - 1) & 6`.

**selectRight (`0x0005af50`).**

```c
for (;;) {
    while (col == 10) { col = 0; if (grid[row*11] != 14) return; }
    if (lang != 1) break;
    col++; if (grid[row*11 + col] != 14) return;
}
if (row == 3) col = (col < 7) ? col + 1 : 0;
else if (row == 4) col = (col >= 9) ? 0 : (col == 0) ? 1 : (col > 6) ? 9 : 7;
else col++;
```

## Pressing a key

**activate (`0x0005b340`).** The key is the +0x98 code if set, otherwise `grid[row*11 + col]`.

```c
switch (key) {
case 1:  Shift(); return;
case 2:  if (mode == 0 || mode == 2) { caps = !caps; return; } break;
case 3: case 4: case 5:    mode = key - 3; return;
case 11: case 12: case 13: jmode = key - 11; return;
case 6:  CursorLeft(); return;
case 7:  CursorRight(); return;
case 8:  Backspace(); return;
case 9:
    for (i = 0; i < len && buf[i] == ' '; i++) ;
    if (i == len) break;                         // empty or all blanks
    for (p = &buf[len-1]; *p == ' '; p--) *p = 0;
    for (q = buf; *q == ' '; q++) ;
    if (q != buf) memmove(buf, q, (wcslen(q) + 1) * 2);
    free(string); string = new WCHAR[len + 1];
    copy len WCHARs; string[len] = 0;
    CallFunction(this, L"OnDone"); return;
case 10:
    if (cursor == -1) { len = 0; cursor = 0; }
    if (len > 30) return;                        // full: no error
    open a gap at cursor; buf[cursor++] = ' '; len++; return;
default:
    if (cursor == -1) { len = 0; cursor = 0; }
    if (len < 31) {
        open a gap at cursor;
        buf[cursor++] = (caps != shift) ? CharUpperW(key) : key;
        shift = 0; len++; return;
    }
}
CallFunction(this, L"OnError");
```

DONE copies `len` characters even after trimming, so trailing spaces arrive in `string` as
nulls and leading ones as whatever sat past the shifted text's terminator. SPACE does not clear
shift, and a key that bounces off a full string leaves shift on.

## Editing

Each editing function opens the same way: a cursor of -1 means the whole string is selected,
so the edit starts from an empty string with the cursor at 0.

**Backspace (`0x0005b630`).**

```c
if (cursor == -1) { len = 0; cursor = 0; }
if (cursor > 0) { memmove(&buf[cursor-1], &buf[cursor], (len - cursor) * 2); len--; cursor--; }
else CallFunction(this, L"OnError");
```

**Delete (`0x0005b6a0`).** After the selection check, if `cursor < len` it steps the cursor
right one (an inlined CursorRight, whose -1 case can't happen any more) and calls Backspace;
otherwise OnError.

**CursorLeft / CursorRight (`0x0005b700`, `0x0005b740`).** From -1 they land on 0 and on `len`
respectively; otherwise they step one, raising OnError at either end.

**Shift (`0x0005b780`).** OnError unless `mode` is 0 or 2, else toggles `shift`.

**CycleMode (`0x0005b7b0`).** `mode++` and `jmode++`, each reset to 0 once above 2.

**Insert (`0x0005b7e0`).**

```c
if (cursor == -1) { len = 0; cursor = 0; }
n = wcslen(s);
if (len + n > 31) { CallFunction(this, L"OnError"); return; }
memmove(&buf[cursor + n], &buf[cursor], (len - cursor) * 2);
copy n WCHARs to &buf[cursor]; cursor += n; len += n; shift = 0;
```

**Get/SetAsteriskFormattingState (`0x0005acb0`, `0x0005acc0`).** Read and write the global
byte at `0x0017e74c` (`param != 0`). The flag isn't per keyboard; the Text node reads it.

## Node hooks

**OnSetProperty (`0x0005b970`).** Only the `string` offset (0x34) does anything:

```c
s = *value ? *value : L"";
n = wcslen(s); if (n > 31) n = 31;
copy n WCHARs to buf; len = n; cursor = -1;
return 1;
```

**Advance (`0x0005bca0`).** `CNode::Advance`, then `control->Advance` if there is one.

**Render (`0x0005b9d0`).**

```c
grid = (lang == 1) ? japanese[jmode] : western[mode];
if (frame) frame->Render();
if (text) { buf[len] = 0; g_textCursor = cursor; g_text = buf; text->Render(); g_textCursor = -1; }
if (lang == 1) { if (keys.count != 99) return; cells = 99; }
else           { if (keys.count != 45) return; cells = 55; }
node = 0;
for (i = 0; i < cells; i++) {
    if (lang != 1 && i > 0 && grid[i] == grid[i-1]) continue;
    k = keys.Get(node);                       // bounds checked, null past the end
    hi = row*11 + col;
    if (lang != 1) {                          // a wide key lights from any of its cells
        if (41 <= hi && hi <= 43) hi = 40;
        else if (46 <= hi && hi <= 50) hi = 45;
        else if (hi == 52) hi = 51;
        else if (hi == 54) hi = 53;
    }
    g_text = label for grid[i];               // the table above, or the character itself,
                                              // CharUpperW'd on western grids when caps != shift
    g_activeKey = (i == hi);
    if (grid[i] != 14) k->Render();
    node++;
}
```

## TIDactivate

`0x0005b040` turns a USB keyboard keystroke into a key press. The keyboard XIPs call it from
`OnTIDKeyDown`, then read `CloseKeyboard`, `PlaySound` and `TIDKeyUsed` to pick between closing,
the key click and the error sound.

```c
CloseKeyboard = 0; PlaySound = 1; TIDKeyUsed = 1; code = 0;
switch (keycode) {                            // 0x0017f460
case 0x35: case 0x88: case 0x90: PlaySound = 0; break;
case 0x4f: code = 7; break;                   // right arrow: cursor right
case 0x50: code = 6; break;                   // left arrow: cursor left
}
if (code == 0) {
    switch (ch) {                             // 0x0017f462
    case 0:    break;
    case 8:    code = 8; break;               // backspace
    case 10:   code = 9; break;               // enter: DONE
    case 0x1b: CloseKeyboard = 1; break;      // escape
    case 0x20: code = 10; break;              // space
    default:
        if (grid char test(ch)) {
            code = ch;
            page = first of the current language's three grids with a cell equal to ch
                   or to CharUpperW of the cell;
            jmode = page;                     // western grids write jmode too, not mode
        }
    }
}
if (code) { activate(); code = 0; TIDKeyUsed = 1; }
else      { TIDKeyUsed = 0; }
```

`code` is the +0x98 field. Both switches are jump tables: the key code one covers 0x35..0x90,
the character one 0..0x20, and everything else falls to the default. The key codes are USB HID
usages (0x4f right arrow, 0x50 left arrow; 0x35, 0x88 and 0x90 are the grave key and the Japanese
input mode keys, which press nothing and stay quiet).

**Grid char test (`0x0005b890`).** Takes the character in DI. Returns false for 1 to 14 (the
function key codes). Otherwise walks the three grids of the current language (55 or 99 cells
each) and returns true on the first cell equal to the character or to `CharUpperW` of the cell.

A typed character goes in through activate's default case, so it is still uppercased again when
caps and shift disagree, and it still clears shift.
