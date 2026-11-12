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

## Palette Presets (10 styles)

0: Firestorm, 1: Aqua, 2: Purple & Blues, 3: Color Wheel,
4: Bizarro Mystery Unveiled, 5: Bizarro Color Wheel, 6: Dark Rainbow,
7: Ice Nightshade, 8: Mystery Unveiled, 9: Roundabout

## Notes

- CSurfx operates on a raw BYTE array with manual pitch calculation.
- The FFT is a standard radix-2 Cooley-Tukey with precomputed twiddle factors. Buffer size is 256 samples (FFT_BUFFER_SIZE_LOG=8).
- CDynamicTexture uses the Xbox Swizzler API for texture upload.
