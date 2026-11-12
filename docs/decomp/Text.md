# Font & Text Rendering

Vector fonts, text meshes and the `Text` node in the 5960 retail binary. Glyphs are stored
pretriangulated in `.xtf` files; a string becomes one D3DX mesh, which the node draws and
scrolls.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x000537c0 | (XTF1 setup) | Creates the LZX decompressor and the block cache for compressed fonts |
| 0x00053840 | (font read) | `ReadFile` wrapper that refuses anything over 0x1400000 bytes |
| 0x00053870 | CFont::Close | Closes the file, frees loaded shapes and every table |
| 0x00053970 | (XTF0 open) | Reads the header, glyph set and glyph records |
| 0x00053b00 | (XTF1 open) | Same, plus a per glyph offset table |
| 0x00053ce0 | CFont::Open | Opens the file, verifies it, dispatches on the magic |
| 0x00053d80 | (XTF1 shape parse) | Copies one glyph shape out of a decompressed block |
| 0x00053ed0 | CFont::LoadGlyph (XTF0) | Seeks to a glyph shape and reads it |
| 0x00054010 | CFont::LoadGlyph (XTF1) | Finds or decompresses the block holding a glyph |
| 0x000541c0 | CFont::FindGlyphIndex | Code point to glyph index |
| 0x00054290 | HorizontalFade | Fades vertex alpha out toward a column edge |
| 0x00054380 | VerticalFade | Fades vertex alpha at the top and bottom of a scroll window |
| 0x00054440 | CFont::CreateTextMesh | Lays out a string into a mesh |
| 0x00055030 | CFont::CreateCursorMesh | Appends the blinking cursor quad |
| 0x000552b0 | InitFontTable | Scans the font directory |
| 0x00055640 | GetFont | Face name to an open font |
| 0x00055750 | Text_Exit | Frees the font table |
| 0x000557b0 | CTextNode::CreateNode | Factory for `Text` |
| 0x000557e0 | CTextNode::CTextNode | Constructor |
| 0x00055880 | CTextNode::~CTextNode | Destructor |
| 0x00055900 | CTextNode::Advance | Vertical scroll |
| 0x000559a0 | FaceFromFont | `body` / `heading` to a face name |
| 0x000559e0 | CTextNode::Render | Builds and draws the mesh |
| 0x00055f20 | CTextNode::OnSetProperty | Drops the mesh on any property but `scroll` |
| 0x00055f60 | FindText | Ten slot cache of throwaway text nodes |

Globals: the keyboard's string `0x0017e748`, cursor index `0x00175220` (starts -1), last built
cursor `0x00175224`, the asterisk formatting flag `0x0017e74c` (a BSS byte, so it starts off;
only the keyboard's `SetAsteriskFormattingState` / `GetAsteriskFormattingState` touch it), the
font table `0x001763e8` (ten 0x30 byte slots: face, file, then the CFont) and its count
`0x00175228` (starts 1, slot 0 is `HGGothicM` / `HGGothicM.xtf`).

## The XTF file

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic, `XTF0` (`0x30465458`) or `XTF1` (`0x31465458`) |
| 4 | 4 | header length, 0x20 |
| 8 | 0x20 | face name, ASCII |
| 0x28 | 4 | glyph set size `cb` |
| 0x2c | cb - 4 | glyph set: `flAccel`, glyph count, range count, then (first code point, count) WORD pairs |
| | count * 0x1c | glyph records: six floats (black box x, y, origin x, y, advance x, y) and the shape's file offset |

A glyph shape is a WORD index count, a WORD point count, the indices, then the points as float
(x, y) pairs. In memory a shape is 0xc bytes: the two counts, then pointers to the index and point
arrays. The record's offset dword is overwritten with the shape pointer once loaded.

The CFont object: file handle `+0`, magic `+4`, glyph set `+8`, glyph records `+0xc`, loaded flags
`+0x10`, then XTF1 only: per glyph block offsets `+0x14`, decompressor `+0x18`, largest
compressed block `+0x1c`, compressed buffer `+0x20`, block cache `+0x24`.

## Opening a font

**Open (`0x00053ce0`)** opens the file read only (`GENERIC_READ`, `FILE_SHARE_READ`,
`OPEN_EXISTING`), then checks it against the dash's signed file hash table (`0x0003eae0`, which
calls `HalReturnToFirmware` on a mismatch). It reads the magic: `XTF0` goes to `0x00053970`,
`XTF1` sets up decompression (`0x000537c0`: `LDICreateDecompression` with 0x8000 byte blocks and
a 0x20000 window, a compressed buffer of the size it reports, and a zeroed ten block cache) and
goes to `0x00053b00`; anything else closes the file and fails.

**XTF0 body (`0x00053970`).**

```c
read 4 (header length, not checked here); read 0x20 (face); read 4 (cb);
if (cb < 0x14) fail;
set = new(cb); set->cbThis = cb; read cb - 4 into set + 4;
if (bytes read != cb - 4 || set->cRanges * 4 + 0x10 != cb) fail;
glyphs = new(set->cGlyphs * 0x1c); read them; check the byte count;
loaded = new(set->cGlyphs); zero it;
```

Every read checks both the return value and the byte count. A failure returns false without
closing anything; Close cleans up later. The XTF1 body reads a WORD per glyph before the records:
where the glyph's shape sits inside its decompressed block. The record's offset then points at
the compressed block.

**LoadGlyph, XTF0 (`0x00053ed0`).**

```c
if (loaded[n]) return 1;
off = glyphs[n].shape; if (off == 0) return 0;
if (SetFilePointer(hFile, off, 0, FILE_BEGIN) == -1) return 0;
s = new(0xc);
read 2 -> s->nIndices; s->indices = new(nIndices * 2);
read 2 -> s->nPoints;  s->points  = new(nPoints * 8);
read the indices, then the points, each through the capped reader and size checked;
glyphs[n].shape = s; loaded[n] = 1; return 1;
```

**LoadGlyph, XTF1 (`0x00054010`)** keeps ten decompressed blocks (0x8008 bytes each: last use
tick, block offset, data). A hit refreshes the tick; a miss takes the oldest slot, reads the
block's WORD compressed and decompressed sizes (no more than the largest compressed block and
0x8000), the compressed bytes, runs `LDIDecompress` and checks the output size. `0x00053d80` then
copies the shape out of the block, bounds checking every read against 0x8000.

**FindGlyphIndex (`0x000541c0`).**

```c
for (;;) {
    base = 0;
    for (i = 0; i < set->cRanges; i++) {
        if (r[i].first <= ch && ch < r[i].first + r[i].count) return ch - r[i].first + base;
        base += r[i].count;
    }
    if (ch == 0x7f) return 0;
    ch = 0x7f;           // anything the font lacks draws as the DEL box
}
```

**Close (`0x00053870`)** closes the handle, frees each loaded, non-null shape (indices, points,
shape), destroys the decompressor, then frees the glyph set, records, loaded flags, block table,
compressed buffer and block cache.

## Font table

**InitFontTable (`0x000552b0`).** Builds `<app dir>fonts\*.xtf` and runs `FindFirstFileA` on it,
falling back to `<app dir>*.xtf`. For each hit, while the count is under ten: strip a `file:`
prefix and turn `/` into `\`, open, read the magic (`XTF0` or `XTF1`), the header length (must be
0x20) and the 32 byte face. The face is widened; if it equals slot 0's face it's freed, otherwise
the face and a copy of the full path go into the next slot. A bad header skips the file without
closing it.

**GetFont (`0x00055640`).**

```c
if (count == 1) InitFontTable();
for (i = 0; i < count; i++) if (!_wcsicmp(face, fonts[i].face)) break;
if (i == count) i = 0;
if (fonts[i].font.hFile == -1) {
    path = wcschr(file, ':') ? file : swprintf(L"%s%s", appdir, file);
    if (!Open(path) && fonts[0].font.hFile == -1 && !Open(fonts[0].file)) return NULL;
}
return &fonts[i].font;
```

**Text_Exit (`0x00055750`)** frees the face and file strings of every slot past the first and
closes each font.

**FaceFromFont (`0x000559a0`)**: NULL or `body` gives `XBox Book`, `heading` gives `Xbox`,
anything else passes through.

## Text meshes

Vertices are FVF `0x52` (XYZ, NORMAL, DIFFUSE), 28 bytes: position, normal (0, 0, 1), color.
Constants: `1.0` (`0x0002bed8`), `0.5` (`0x0002bed4`), `2.0` (`0x0002bef4`), `3.0`
(`0x0002bf00`), `255.0` (`0x0002bf38`), `1.5` (`0x0002c184`), `0.1` (`0x0002bfc0`), `0.75`
(`0x0002c018`), `pi` (`0x0002bffc`), `16.556152` (`0x0002c1b4`).

**CreateTextMesh (`0x00054440`)** takes the font, the string, its length (-1 for `wcslen`), the
mesh out pointer, bounding box min and max, a format width, a don't-break flag, a scale and an
indent (width, height); `ret 0x2c`.

A heading is a column that starts and ends with `*` (at least two characters before the tab or
line end). Every heading test also requires the asterisk formatting flag.

First pass, per line:

1. If the line opens a heading, skip the `*` and remember it. The memory isn't cleared at the next
   line, only at a tab.
2. Per character: a remembered heading's closing `*` (next character is tab, line end or the end)
   is skipped. Without don't-break, `\r` (eating a following `\n`) and `\n` end the line, and a tab
   switches to column 2, marks the text as multi column and checks for a heading after it.
3. Otherwise look the glyph up and load it (XTF0 or XTF1 loader by magic). A glyph that won't load
   has its record's shape slot zeroed and is skipped.
4. If the vertex total would pass 0xffff, the string is cut at this character and the pass ends.
5. Add the glyph's advance to the column's width, index count / 3 to the face count and its points
   to the vertex count.

The widest first and second columns are kept. A cursor index from 0 to the length adds two faces
and four vertices. No faces means no mesh.

Then with a positive format width: a single column takes the whole width; two columns wider than
it shrink the first to `width - col2` and turn fading on. Without a width fading is off; otherwise
fading starts as the don't-break flag.

`XAppCreateMeshFVF(faces, verts, D3DXMESH_MANAGED (0x220), 0x52)` (`0x0003d2f0`, which frees
caches and retries on out of memory), then both buffers are locked and written without checks.

Second pass. Color is white with the blue byte from a float that headings set to 0.5 and their
closing `*` sets back to 1.0: `0xffffff00 | (int)(blue * 255)`. x starts at the indent width.
Per line, column 1 with the first column width as its limit; per character:

- Past the end reads as `\r`. `\r` eats a following `\n`.
- Line end: with don't-break and fading, fade the column (`limit - 1` to `limit`) and keep going on
  the same line. Otherwise column 2 shifts right by `col2 max - col2 width`, column 1 fades if
  fading is on, and unless this is the end of the string y drops 1.0 (in a heading, after `\r`, or
  with two columns) or 1.5 (a new paragraph), with x going back to 0 below the indent height and to
  the indent width inside it.
- Tab (without don't-break): fade the column from `limit - 2` if fading, then column 2 starts at
  `width - col2 max` with that as its limit, and a heading may open after it.
- A heading's closing `*` restores blue to 1.0 and is skipped.
- With a positive width, without don't-break, in column 1, a non space glyph that would end past
  the width wraps: y drops 1.0, x goes back to 0 or the indent, and the string, vertex and index
  positions rewind to the last break point. With no break point on the line fading turns on
  instead.
- A glyph with no shape is skipped. Otherwise its indices go in offset by the vertex position and
  its points go in at (x + px, y + py).
- If the character just read is at the cursor index, the cursor quad goes in at (x, y).
- x advances by the glyph's advance (column 2 also adds it to its width). A space, anything from
  0x3040 up to 0xf000, or 0xff66 and up becomes the new break point.

A cursor index equal to the length gets the quad after the last character, visible only while
x < 16.556152 (the edge of the keyboard's edit box). Then every counted vertex is scaled (x, y,
z) and, given both pointers, `D3DXComputeBoundingBox` runs over the written vertices.

**HorizontalFade (`0x00054290`).** Vertices in EAX, end in XMM1, direction in BL, then count and
start on the stack.

```c
inv = 1.0 / (end - start);
for (i < n) {
    a = (v.x - start) * inv;
    a = a <= 0 ? 0 : a >= 1 ? 1 : (3 - 2a) * a * a;
    if (out) a = 1 - a;
    v.alpha = (int)(v.alpha * a);
}
```

CreateTextMesh only ever calls it fading out.

**VerticalFade (`0x00054380`).** Vertices in ECX, count in EDX, top in XMM4, bottom in XMM5,
scroll on the stack.

```c
y = v.y + scroll;
if (y > top || y < bottom)  a = 0;
else if (y > top - 1)       a = 1 - (y - (top - 1));
else if (y < bottom + 1)    a = 1 - ((bottom + 1) - y);
else                        a = 1;
alpha byte = (int)(ease(a) * 255);      // same ease as above
```

**CreateCursorMesh (`0x00055030`).**

```c
a = |sin((now + now) * pi)|;  if (!visible) a = 0;     // x87, now is the double clock
color = (int)(a * 255) << 24 | 0xffffff;
indices += { v, v+1, v+2, v, v+2, v+3 };
(x, y - 0.1) (x + 0.1, y - 0.1) (x + 0.1, y + 0.75) (x, y + 0.75), normal (0, 0, 1)
```

## CTextNode

0x78 bytes. Prop map `0x00175230`, vtable `0x00028928`.

| Offset | Field | Default |
|--------|-------|---------|
| 0x1c | text | |
| 0x20 | font | |
| 0x24 | justify | |
| 0x28 | deviation | 0.01 |
| 0x2c | extrusion | 1.0 |
| 0x30 | width | 0 |
| 0x34 | translate | 1 |
| 0x38 | height | 0 |
| 0x3c | scroll | 0 |
| 0x40 | scrollRate | 0 |
| 0x44 | scrollDelay | 0 |
| 0x48 | indentWidth | 0 |
| 0x4c | indentHeight | 0 |
| 0x50 | mesh | |
| 0x54 | bbox min, max | |
| 0x6c | language the mesh was built in | 0 |
| 0x70 | time scrolling starts (double) | 0 |

**Destructor (`0x00055880`)** frees the three strings and releases the mesh.

**Advance (`0x00055900`).**

```c
CNode::Advance(t);
if (scrollRate > 0 && mesh) {
    if (timeToScroll == 0) timeToScroll = scrollDelay + now;
    h = bbox.max.y - bbox.min.y;
    if (timeToScroll <= now && h > height) {
        scroll = height * t * scrollRate + scroll;
        if (h + 1.5 <= scroll) scroll -= h + 1.5;
    }
}
```

**Render (`0x000559e0`).**

1. If the keyboard left a string: when the text, or the cursor against the last built cursor,
   differs, copy it in, record the cursor and rebuild. A cursor of -1 always rebuilds. Clear the
   string.
2. No text, or empty: return.
3. `<clock>` and `<date>` prefixes: format the local time (`FormatTime`) or date (`FormatDate`,
   `0x0003cd40`) into a 32 character buffer; if it differs from what follows the prefix, the text
   becomes the prefix plus the value and the mesh rebuilds. The node stops translating and the
   string after the prefix is what gets drawn.
4. Translating and the current language differs from the mesh's: rebuild. Rebuild releases the
   mesh.
5. No mesh: translate if translating, record the language, then
   `CreateTextMesh(GetFont(FaceFromFont(font)), text, -1, &mesh, &min, &max, |width|, width < 0,
   1.0, indentWidth, indentHeight)`. Still no mesh: return.
6. Justify `middle` or `end` offsets x by half or all of the drawn width: the bounding box width,
   capped at |width| when width is set.
7. Twice: if height is set and the text is taller, lock the vertices and `VerticalFade` with top
   `1 - scroll` (one higher while scroll is 0), bottom `top - height - 2` and, for the second
   copy, an offset of `-(h + 1.5)`; the second copy is skipped while `height + scroll < h`.
   Without scrolling there's no second copy. Each draw pushes the world matrix, translates by
   (x offset, scroll, plus `-(h + 1.5)` for the second copy), loads it into the device, sets
   CULLMODE to NONE, `DrawSubset(0)`, CULLMODE back to CCW, pops and reloads the world matrix.

**OnSetProperty (`0x00055f20`).** Any property but `scroll` (offset 0x3c) releases the mesh and
zeroes the scroll start time and the scroll.

**FindText (`0x00055f60`).** Ten (node, last used) slots.

```c
for (i < 10) {
    if (node && node->text && !wcscmp(node->text, s) && node->width == w) { used = now; return slot; }
    if (!empty) { if (!node) empty = slot; else if (!oldest || used < oldest->used) oldest = slot; }
}
if (!empty && oldest) { delete oldest->node; zero the slot; empty = oldest; }
empty->node = new CTextNode; node->text = copy(s); node->width = w; empty->used = now;
```
