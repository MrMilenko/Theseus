# TMAP System

Dynamic texture rendering pipeline in the 4920-5960 retail binary. Renders procedural effects into palettized 8-bit textures using pixel displacement fields, HSV palette cycling, FFT-based audio visualization, and Bresenham line drawing.

## Node Classes

All node type strings confirmed in the 4920-5960 retail XBE.

| Node | String Address | Class String Address |
|------|---------------|----------------------|
| Palette | 0x0002a2bc | 0x0002a2a8 "CPalette" |
| DynamicTexture | 0x0002a23c | 0x0002a21c "CDynamicTexture" |
| ImageFader | 0x0002a1e0 | 0x0002a1c8 "CImageFader" |
| AudioVisualizer | 0x0002a9e8 | 0x0002a9c4 "CAudioVisualizer" |
| DotField | 0x0002a490 | 0x0002a47c "CDotField" |

## Properties

### CPalette

- `type` (pt_integer): palette preset index (0-9)
- `changePeriod` (pt_number): seconds between automatic palette changes
- `changePeriodRandomness` (pt_number): random jitter added to change period

### CDynamicTexture

- `children` (pt_children): nodes that render via RenderDynamicTexture()
- `size` (pt_integer): pixel dimension (texture is size x size, backed by 512x512)
- `erase` (pt_boolean): clear to black before each frame
- `fps` (pt_number): update rate cap
- `palette` (pt_node): CPalette node for color mapping

### CImageFader

- `type` (pt_integer): delta field warp style (0-23)
- `changePeriod` (pt_number): seconds between style changes
- `changePeriodRandomness` (pt_number): jitter

### CAudioVisualizer

- `scale` (pt_number), `offset` (pt_number): amplitude controls
- `type` (pt_string): "line", "spinner", "circle", "analyzer"
- `channel` (pt_string): "left", "right" (default left)
- `source` (pt_node): CAudioClip providing PCM data

### CDotField

A particle effect rendered into the dynamic texture pipeline. Renders a field of dots that respond to audio levels and time. Used for the dashboard background animation.

## Architecture

The rendering pipeline works as:

1. CDynamicTexture owns a CSurfx (8-bit pixel buffer) and a D3DFMT_P8 texture.
2. Each frame, children (CImageFader, CAudioVisualizer, CDotField, CPalette) draw into the CSurfx.
3. CImageFader applies a DeltaField warp via bilinear interpolation displacement.
4. CAudioVisualizer draws PCM waveforms and FFT spectra using CSurfx::Line.
5. The 8-bit buffer is swizzled into the Xbox texture via the Swizzler API.
6. CPalette uploads a 256-entry hardware palette via D3DPalette.

## CSurfx (8-bit surface)

A 0x24 byte object, allocated inline by the DynamicTexture update at `0x0005dde0`.

| Offset | Field |
|--------|-------|
| 0x00 | width |
| 0x04 | height |
| 0x08 | pixels, `new[(height + 1) * width]` (one spare row) |
| 0x0c | bytes per row (= width) |
| 0x10 | line width, always 1 |
| 0x14..0x20 | clip rect `{0, 0, width, height}` |

Constructor `0x0005f3f0` bumps a live count at `0x0017e75c`. The DynamicTexture destructor
(`0x0005dbc0`) frees the pixels, drops the count, and when it hits zero frees the shared fade
scratch buffer (`0x0017e764`, size at `0x0017e760`).

### Delta cells

A delta field is 3 bytes per pixel, read as a 32-bit load then masked. Bits 12..23 hold the
source x offset, bits 0..11 the source y offset, both in 1/32 pixel steps and biased by 63
pixels (`0x7e0`). `0x7e07e0` means "this pixel pulls from itself".

### Fade, out of place (`0x0005f430`)

```c
src += -0x3f * srcPitch - 0x3f;
for (row...) {
  for (x = 0; x < w; x++) {
    code = *(uint *)grad; grad += 3;
    p  = src + x + ((code >> 12 & 0xfff) >> 5) + ((code & 0xfff) >> 5) * srcPitch;
    fx = code >> 12 & 0x1f; fy = code & 0x1f;
    dest[x] = ((p[pitch]*fy + p[0]*(32-fy))*(32-fx) + (p[pitch+1]*fy + p[1]*(32-fy))*fx) * 0x1f >> 0xf;
  }
  dest += destPitch; src += srcPitch;
}
```

Bilinear 5-bit weights total 1024, so `* 31 >> 15` is the blend times 31/32: every pass dims
the image a little, which is what makes trails fade out.

### Fade, in place (`0x0005f540`)

Takes the DeltaFieldData (`{negY rows, cells}`), the pixels, pitch, width and height.

1. Grows the shared scratch buffer to `negY * pitch` if needed (free, then new).
2. Clamps the ring height to the image height.
3. Runs the out-of-place pass for the first `negY` rows into the scratch buffer.
4. For each later row: copies the oldest scratch row back into the image row `negY` above,
   then computes the current row into that scratch slot (`slot = (slot + 1) % negY`).
5. Flushes the remaining scratch rows in order, `width >> 2` dwords per row.

A pixel can pull from at most `negY` rows above, so a row only goes back into the image once
nothing still needs the old values.

Quirks: the bilinear tap reads one row and one pixel past the source pixel, so the bottom-right
pixel reads one byte past the image (the spare row covers everything else). A y offset of more
than 63 rows up underflows its 12-bit field and borrows from the x field.

## Line (`0x0005fdf0`)

`__fastcall`, with the surface and color on the stack.

- Each coordinate is folded as `((v >> 1 ^ v) & 0x3fffffff) ^ (v >> 1)`: low 30 bits kept, sign
  copied into bits 30 and 31.
- Line width 4 and up is scaled by direction: with squared run lengths `dx2`, `dy2`,
  `w = (lineWidth * (128 + 55 * minor / major) + 64) >> 7`, so diagonals get up to 1.43x.
- Clipping: the clip rect is inset by `w >> 1`. If the start is outside, the ends swap; if the
  end is outside too, nothing is drawn. Step counts are trimmed so the walk stops one short of
  the right and bottom edges.
- Width 1: integer Bresenham, one pixel per step, `x`-major when `dx >= dy`.
- Wider: first a round cap at the start, then per step a `w` pixel span across the minor axis
  (`x`-major when `dx > dy`). Cap insets per row come from byte tables:

| Width | Table | Insets |
|-------|-------|--------|
| 2 | 0x0002a364 | 0 0 |
| 3 | 0x0002a360 | 1 0 1 |
| 4 | 0x0002a358 | 1 0 0 1 |
| 5 | 0x0002a350 | 1 0 0 0 1 |
| 6 | 0x0002a348 | 1 0 0 0 0 1 |
| 7 | 0x0002a340 | 2 1 0 0 0 1 2 |
| 8 | 0x0002a334 | 2 1 0 0 0 0 1 2 |
| 9 | 0x0002a328 | 3 1 1 0 0 0 1 1 3 |
| 10 | 0x0002a31c | 3 1 1 0 0 0 0 1 1 3 |
| 11 | 0x0002a310 | 4 2 1 1 0 0 0 1 1 2 4 |

  From 12 up the inset is computed: `half - (int)fsqrt(half*half - k*k)` for `k = -half..`.

Nothing in the 5960 dash changes the line width from 1, so only the 1 pixel path runs.

## DeltaField

A 0x3c byte object built inline by ImageFader (`0x0005e120`).

| Offset | Field |
|--------|-------|
| 0x00 | style |
| 0x04 | current row (-1 = never sized) |
| 0x08 | most negative y offset so far (1/32 px) |
| 0x0c..0x18 | x, y, r, theta of the cell being computed |
| 0x1c, 0x20 | x scale, y scale |
| 0x24, 0x25, 0x26 | polar, has r term, has theta term |
| 0x28, 0x2c | width, height |
| 0x30 | aspect 1:1 |
| 0x34, 0x38 | DeltaFieldData: ring rows, cells |

**Assign + SetSize (`0x0005e4e0`, also inlined twice in ImageFader).** Sets aspect, polar, r and
theta flags to 1, frees and reallocates the cells as `w * h * 3 + 0x40`, sets
`xs = 2.0 / w`, `ys = 2.0 / h`, makes both the larger of the two when aspect is on, then
`ys *= -1.0` (`0x0002bef0`) so +y is up. Resets the row and the negative extent to 0.

**CalcSome (`0x0005e570`).** One row per call (two when starting on row 0). Top and bottom rows
and the first and last cell of every row are `0x7e07e0`. For the rest:

```c
y = (h - row*2) * ys * 0.5;            fx = 32.0 / xs;  fy = 32.0 / ys;
x = (col*2 - w) * xs * 0.5;
if (hasR) r = sqrt(x*x + y*y);
if (hasTheta) theta = fpatan(y, x);
GetXY(init = (col == 1 && row == 1), r, theta, x, y) -> X, Y, polar
if (polar) { X, Y = X*cos(Y), X*sin(Y); }
dx = (int)((X - x) * fx);   dy = (int)((y - Y) * fy);
clamp col + (dx >> 5) into [0, w-1) and row + (dy >> 5) into [0, h-1) by rewriting dx, dy
negY = min(negY, dy);
cell = (dx + 0x7e0) << 12 | (dy + 0x7e0);
```

When the last row is done the ring height becomes `1 - (negY >> 5)`. Returns rows left.

**GetXY (`0x0005e7d0`).** A switch on the style; out-of-range styles are rewritten to 0. Style
parameters `A0`, `A1`, `A2` are globals at `0x00188f60`, `0x00188f5c`, `0x00188f58`, rolled
only on the init cell. `rnd(n)` below is `rand() * (n / 32767)`; the binary stores each
`n / 32767` as one constant. Results are polar (radius, angle) unless noted.

| Style | Init | X | Y |
|-------|------|---|---|
| 0 | | `(atan(0.55 - r) * 0.16 + 1) * r` | `theta - 0.01` |
| 1 | | `r * 0.87` | `theta - 0.075` |
| 2 | `A0 = (int)rnd(40) * pi` | `((sin(A0*theta) + 1) * 0.03 + 0.92) * r` | `theta` |
| 3 | `A0 = 2 + rnd(16)`, `A1 = 0.01 + rnd(0.05)`, `A2 = 0.002 + rnd(0.006)` | `max(r - A2, 0)` | `sin(A0*r) * A1 + theta` |
| 4 | | `r * 0.87` | `theta - 0.009` |
| 5 | | `r * 0.8` | `theta - 0.008` |
| 6 | `A0 = 2 + rnd(16)`, `A1 = 0.01 + rnd(0.05)` | `sin(A0*y) * A1 + x` | `sin(A0*x) * A1 + y` (x/y) |
| 7 | | `r * 1.01` | `theta + 0.021` |
| 8 | | `r - 0.01` | `theta` |
| 9 | `A0 = rnd(0.0045)`, `A1 = 0.1 + rnd(0.06)`, `A2 = 0.003 + rnd(0.005)` | `(0.99 - pow(abs(x), 1.3) * A1) * r - rnd(A0)` | `theta - A2` |
| 10 | as 9 but `A2 = 0.003 + rnd(0.006)` | as 9 with `abs(y)` | `theta + A2` |
| 11 | | `r - x * y * 0.18` | `theta - 0.005` |
| 12 | | `r * 0.96` | `theta - 0.021` |
| 13 | | `rnd(0.01) + x - 0.005` | `y - rnd(0.01) - 0.005 - (y - 1.3) * 0.04` (x/y) |
| 14 | `A0 = 0.95 + rnd(0.06)` | `A0 * r` | `theta - max(1 - r, 0) * 0.035` |
| 15 | | `r * 0.94` | `theta - 0.007` |
| 16 | | `rnd(0.008) + x * 0.9 - 0.004` | same with y (x/y) |
| 17 | | `(rnd(2) - 1) * 0.1 + x` | same with y (x/y) |
| 18 | | `r * 0.99` | `theta + 0.009` |
| 19 | | `(rnd(0.05) + 0.92) * r` | `theta + 0.003` |
| 20 | | `sin(r * 6.2831855) * 0.04 + r` | `theta + 0.015` |
| 21 | | `((sin(r * 15) + 1) * 0.05 + 0.87) * r` | `theta` |
| 22 | `A0 = rnd(0.1)`, `A1 = 3 + rnd(7)`, `A2 = 0.9 + rnd(0.3)` | `((r - A2) * 0.13 + 1) * r` | `sin(A1*r) * A0 + theta` |
| 23 | `A0 = (int)(rnd(2.1) + 1) * pi` | `((cos(A0*theta) + 1) * 0.05 + 0.89) * r` | `theta + 0.005` |

Styles 9, 10, 13, 16, 17 and 19 call `rand()` per cell, so their fields differ every time
they're built.

## Palettes

### HSV2RGB (`0x0005f7e0`)

Takes H, S, V on the stack and the output pointer in EBX. `h6 = (H - floor(H)) * 6`, sector
`= ftol(h6)`, `f = h6 - sector`. S and V are clamped to 0..1, then with `v = V * 255`
(`0x0002bf38`), `f` is replaced by `1 - f` on even sectors so one value serves as both
"falling" and "rising" channel:

```c
m = (int)(v * (1 - S));   n = (int)(v * (1 - S * f));   v = (int)v;
switch (sector) { 1: (n,v,m)  2: (m,v,n)  3: (m,n,v)  4: (n,m,v)  5: (v,m,n)  default: (v,n,m) }
```

The packer at `0x0005f770` clamps each channel to 0..255 and returns `0x00RRGGBB`.

### GetHSV (`0x0005f920`)

Recipe in EAX, outputs through EBX/EDI/ESI (H/S/V), intensity `i` and time `t` on the stack.
Stores the recipe in a global (`0x00175e70`) first; above 9 it stores 0 and runs recipe 0.
`wrap(x)` is `x - floor(x)` (`0x0005f3d0`). Exponents are doubles fed to `CIpow`.

| Recipe | H | S | V |
|--------|---|---|---|
| 0 | `pow(i, 1.9) * 0.166` | `1` | `pow(i, 0.9)` |
| 1 | `0.45` | `pow(1 - i, 0.3)` | `pow(i, 0.7)` |
| 2 | `pow(i, 1.4) * 0.166 + 0.666` | `(pow(i * 1.8 - 1, 2) + 1) * 0.5` | `pow(i, 0.4)` |
| 3 | `wrap(t * 0.03)` | `1 - pow(i, 2.5) * 0.6` | `i` |
| 4 | `wrap(0.3 - t * 0.003)` | `pow(i, 2.9)` | `pow(i, 0.9)` |
| 5 | `wrap(t * 0.02)` | `1` | `1 - pow(i, 1.4)` |
| 6 | `pow(i, 1.6)` | `1` | `i` |
| 7 | `wrap(pow(0.7 - i * 0.4, 0.6))` | `0.9` | `pow(i, 0.9)` |
| 8 | `wrap(t * 0.002)` | `pow(1 - i, 1.5)` | `pow(i, 0.4)` |
| 9 | `wrap(t * 0.05 + (pow(cos(i * 4), 2) + pow(i, 2)) * 0.2)` | `i * 0.5 + 0.85` | `pow(i, 0.55)` |

### Default palette (`0x0005fc30`)

Used when a DynamicTexture has no palette node. State is global: next switch time
(`0x00188f38`), blend start (`0x00188f88`), blend recipe (`0x0017d0fc`) and blend amount
(`0x0017d0f8`); the current recipe is GetHSV's global.

- When `now > next`: `next = rand() * (5 / 32767) + now + 10.0`, blend recipe = current,
  current += 1, amount = 0, start = now.
- 256 entries, `i` stepping by `1/255` (`0x0002becc`). While blending, a second GetHSV for the
  blend recipe mixes in as `(1 - amount) * old + amount * new`. That second call also rewrites
  the current-recipe global, so from entry 1 on both lookups return the old recipe.
- After the loop `amount += (now - start) * 0.5`, and the blend ends at 1.0. The amount
  accumulates on every call instead of tracking elapsed time.

### Palette node

| Offset | Field |
|--------|-------|
| 0x1c | type (recipe) |
| 0x20 | changePeriod |
| 0x24 | changePeriodRandomness |
| 0x28 | blend time, 2.0 |
| 0x2c | D3D palette |
| 0x30 | 256 `0x00RRGGBB` entries |
| 0x430 | blend recipe, -1 = none |
| 0x434 | blend amount |
| 0x438 | next change (double) |
| 0x440 | blend start (double) |

**Advance (`0x0005d7d0`).** When `changePeriod > 0` and the time has passed:
`next = rand() * randomness / 32767 + changePeriod + now`, blend = type, type += 1,
amount = 0, start = now. Then Update, then `amount = (now - start) / blendTime`, ending the
blend at 1.0.

**Update (`0x0005d8b0`).** Same per-entry loop as the default palette, but copies the
recipe global back into `type` after each lookup (so an out-of-range type becomes 0). Then
creates the D3D palette on first use (inlined `XMemAlloc` of the resource header plus a 0x400
byte table) and writes every entry with alpha forced to `0xff`.

**RenderDynamicTexture (`0x0005da80`).** Fills each row `y` with `y * 256 / height`: a ramp
through the whole palette, top to bottom.

## DynamicTexture

| Offset | Field |
|--------|-------|
| 0x20 | format, `0xb` (P8) |
| 0x24 | texture |
| 0x28, 0x2c | image width, height |
| 0x30..0x38 | children (count, capacity, array) |
| 0x3c | size, default 256 |
| 0x40 | erase, default 1 |
| 0x44 | fps, default 15.0 |
| 0x48 | palette node |
| 0x4c | CSurfx |
| 0x50 | last update (double) |

**Create (`0x0005dc70`).** Always makes a 512x512, one-level texture in the node's format,
locks it, zeroes `w * h` bytes and reads the real size back. The requested size is ignored.

**Advance (`0x0005dd20`).** CNode advance, then the palette node, then each child.

**GetTextureSurface (`0x0005dd70`).** When `1 / fps + lastUpdate < now` (compared as floats) it
stores `now` and runs the update. Then, if the palette node's class chain reaches Palette, it
binds that node's D3D palette to stage 0.

**Update (`0x0005dde0`).**

1. If there's no texture and `size > 0`, Create; give up if that fails.
2. Allocate the `size x size` CSurfx on first use.
3. Only with both in place and `lastUpdate <= now`: zero the pixels when erase is set, call
   every child's RenderDynamicTexture with the surface.
4. If there's no palette node, or its GetPalette returns NULL, build the default palette into a
   stack buffer. The result is never read on this path.
5. Lock the texture and copy the surface in with an inlined Swizzler: per row, `IncU` per
   pixel, then `IncV` and `AddU(SwizzleU(imageWidth - size))` to skip the unused columns.

## ImageFader

| Offset | Field |
|--------|-------|
| 0x1c | type (warp style) |
| 0x20 | changePeriod |
| 0x24 | changePeriodRandomness |
| 0x28 | current DeltaField |
| 0x2c | next DeltaField |
| 0x30 | next change time (double) |

**RenderDynamicTexture (`0x0005e120`).**

1. No current field: build one for `type` (new 0x3c bytes, style, Assign, SetSize to the
   surface, all inlined) and set `next = changePeriod + rand() * randomness / 32767 + now`.
2. Else, when `changePeriod > 0` and `next <= now`: `type += 1`, `next = changePeriod + now`,
   free the current field and promote the next one.
3. No next field: build one for `type + 1`.
4. Current field unfinished: 10 CalcSome calls, copy its style back into `type`, return without
   warping. Building a field this way takes `height / 10` frames.
5. Next field unfinished: 5 CalcSome calls (guarded by a NULL check).
6. Finish the current field if needed and run the in-place fade on the surface.

If the timer fires on the frame after `type` was set, the promoted field is NULL and step 4
dereferences it.

**OnSetProperty (`0x0005e3f0`).** For the `type` property (offset 0x1c): free the current field,
promote the next one, clear next. Returns 1 for everything.

## AudioVisualizer

| Offset | Field |
|--------|-------|
| 0x1c | type string |
| 0x20 | channel string |
| 0x24 | source node |
| 0x28 | scale, default 1.0 |
| 0x2c | offset |
| 0x30 | 256 left samples |
| 0x230 | 256 right samples |
| 0x430 | 128 left spectrum bins |
| 0x530 | 128 right spectrum bins |
| 0x630 | spectrum valid |
| 0x632 | 256 mono samples (never filled) |
| 0x832 | mono valid |

**RenderDynamicTexture (`0x00063940`).** Returns unless the source's class is AudioClip,
its transport mode (+0x50) is 1 (playing), and its pump (+0x68) hands back a sample buffer.
Sample count is buffer size / 4, capped at 256; left/right are copied out, the rest zeroed,
both "valid" flags cleared, then the count is capped at the surface height. Channel `r`/`R`
picks the right samples. The type's first letter picks the effect:

- anything else (or no type): **line scope.** One line per sample, from the previous point to
  `(width/2 + (sample * width >> 16), row)`, color 255.
- `s`/`S`: **spinner.** Calls `rand()` and ignores it, takes `c = cos(t * 0.2)`,
  `s = sin(t * 0.2)`, then steps `p` from 0 to 1 by `1 / width`: level
  `= sample[(int)(count * p)] / 32767 * scale + offset`, `a = (p - 0.5) * 2.1`, point
  `(c*a + s*level, s*a - c*level)` scaled by half the surface, joined with lines.
- `c`/`C`: draws nothing.
- `a`/`A`: **analyzer.** Spectrum update (below), then 256 bars: left bins reversed then right
  bins, each `ln(bin) * 8` clamped to 0..255. Peaks (static ints at `0x00188f90`) fall by 2 a
  frame and jump to `2 * bar`. Each column takes bar `x * 256 / width`, filled from the bottom
  with 255 counting down, plus one 255 dot at the peak height (which can land in the spare
  row). Then the beat flash and the wave overlay.

**Spectrum (`0x000638a0`, `0x00063900`).** Once per frame: FFT of the left then right samples
(state made on first use), each of the 129 outputs becomes `(short)(sqrt(p) / 256)`. 129 bins
into a 128 entry array: the left's last bin lands on right bin 0 (rewritten right after) and
the right's lands on the valid flag (set right after).

**Beat flash (`0x00063ef0`).** Bass = average of `bin >> 4` over the 5 lowest bins of both
channels. Globals at `0x0017e6f4..0x0017e70c` track a running max and min that roll into
"last" values every 10 frames (frame counter at `0x00188f50`). When bass rises above
`(6 * lastMax + 4 * lastMin) / 10` the whole surface is filled with 255, once, until bass drops
below `(4 * lastMax + 6 * lastMin) / 10`.

**Wave overlay (`0x00064130`).** Globals: amplitude 50 (`0x00176034`), shift 30
(`0x00176038`), mode -1 (`0x0017603c`), color 128 (`0x00176040`), next switch (`0x00188f48`).

- When mode < 0 or the switch time passes: mode += 1, next = `rand() * 5 / 32767 + now + 3`;
  mode wraps from 5 to 0.
- Half circle tables `cos/sin(i / width * pi + pi / 2)` for every 4th `i`
  (`0x0017e774..0x0017e780`), rebuilt when the width changes.
- Steps of 4 pixels across the width; each step reads the high byte of sample
  `((i << 8) / width) / 5` from each channel, scaled `* height * 50 >> 12`. Lines sit
  `height * 30 >> 8` from the middle: mode 0 one left-channel line, mode 1 right above and left
  below, mode 2 the right line twice plus two vertical left lines, modes 3 and 4 both channels
  around the half circle (mode 3 clips negative values), closed with one last chord.
- Then two 64 point Lissajous trails as 2x2 dots (`0x000640a0`), color 255:
  `(cos(k / d1), sin(k / d2)) * height * 50/256` rotated by `k * 0.001` around the center, with
  `d1, d2 = 80, 140.48` and `187.2, 271.13`. `k` starts from a global (`0x00188f40`) each
  trail and the second trail's end is stored back.

## FFT

A plain 256 point radix-2 decimation-in-time FFT.

- **Tables (`0x0005f040`).** Allocates the 0x800 byte state (256 real + 256 imaginary floats),
  fills a 256 entry bit-reverse table (`0x0017ccf8`) and 128 entry cos/sin tables
  (`0x0017c8f8`, `0x0017caf8`) of `i * 2pi/256` (`0x0002bf40`).
- **Load (`0x0005f0f0`).** `re[i] = sample[bitrev[i]]`, `im[i] = 0`, then the passes and the
  power step.
- **Passes (`0x0005f290`).** 8 passes, butterfly span 1, 2, .. 128; twiddle `j * (128 / span)`;
  `t = (re*c - im*s, im*c + re*s)`, `b = a - t`, `a = a + t`.
- **Power (`0x0005f140`).** `out[i] = re^2 + im^2` for bins 0..128 (unrolled by 4), then bins 0
  and 128 times 0.25 (`0x0002beec`).
