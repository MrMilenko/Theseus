# AudioVisualizer

`AudioVisualizer` is the node behind the music player's moving pictures. It never draws
to the screen itself: it's handed an 8 bit `CSurfx` (the TMAP surface) through
`RenderDynamicTexture` every frame and scribbles the current audio into it, and the
TMAP fade does the rest. Everything lives at `0x00063760..0x00064630` in the 5960
retail XBE.

## Registration and layout

| Class | Name string | Size | Factory | vtable |
|-------|-------------|------|---------|--------|
| CAudioVisualizer | "AudioVisualizer" | 0x834 | `0x00063770` | `0x0002a938` |

| Offset | What |
|--------|------|
| 0x1c | `type` (string) |
| 0x20 | `channel` (string) |
| 0x24 | `source` (node, an AudioClip) |
| 0x28 | `scale`, 1.0 from the factory |
| 0x2c | `offset`, 0 |
| 0x030 / 0x230 | left / right samples, 256 shorts each |
| 0x430 / 0x530 | left / right spectrum, 128 shorts each |
| 0x630 | spectrum valid flag |
| 0x632 | mono mix, 256 shorts |
| 0x832 | mono valid flag |

The destructor (`0x00063800`) frees both strings and releases the source. `Advance`
(`0x00063870`) runs the node advance and then advances the source, since the clip isn't
in the scene graph on its own.

## RenderDynamicTexture (`0x00063940`, 1363 bytes)

1. Bail unless `source` is set, its class is AudioClip (`GetNodeClass` compared with
   `0x0018864c`), its transport (+0x50) is 1 (playing), it has a sound buffer (+0x68),
   that buffer has sample data (vtable +0x1c) and the byte count (vtable +0x20) is at
   least 4.
2. Frames = bytes / 4, capped at 256. Copy each stereo frame's left and right shorts
   into the two arrays, zero the rest. Clear both cache flags (0x630, 0x832).
3. Cap frames at the surface height. Channel is right when `channel` starts with `r` or
   `R`, otherwise left. Style from `type`'s first letter through a jump table at
   `0x00063ea0`: `a`/`A` 3, `c`/`C` 2, `s`/`S` 1, anything else 0. The clock is read
   once from `0x0017e808`.
4. Style 0, line: start at (w/2, 0); for each frame y draw `Line` (`0x0005fdf0`) to
   (w/2 + (sample * w >> 16), y) in 255.
5. Style 1, spinner: step 1/w from 0 while <= 1.0. Amplitude is
   `sample[(int)(frames * t)] * (1/32767) * scale + offset` (constant at `0x0002bf10`),
   along is `(t - 0.5) * 2.1` (`0x0002c160`). Rotate by `g_now * 0.2` (`0x0002c01c`),
   x = cos * along + sin * amp, y = sin * along - cos * amp, and join each point to the
   last except at t = 0. It calls `rand()` once up front and ignores the result.
6. Style 2 has a case number but no code: the jump lands on the exit.
7. Style 3, analyzer: refresh the spectrum (`0x00063900`, below). Level per band is
   `ln(bin) * 8` (`FYL2X` with `FLDLN2`, 8.0 at `0x0002bf34`), 0 when not above 0, 255
   when not below 255.0 (`0x0002bf38`). Bands 0..127 take the left spectrum reversed,
   128..255 the right one in order. Peaks live in a 256 int global at `0x00188f90`:
   each frame a peak above 2 drops by 2, anything else goes to 0, and it's raised to
   level * 2 if lower. Then for each column x (band = x * 256 / w) a bar of height
   level * h / 256 (`0x0002c064` is 1/256) is filled from the bottom up with 255
   counting down, and one 255 pixel goes at h - peak * 0.5 * h / 256. Neither write is
   clipped. Last it runs the two overlays below.

## Spectrum (`0x000638a0`, `0x00063900`)

`0x00063900` checks the valid flag at 0x630 and, when clear, runs `0x000638a0` on the
left then right samples and sets it. `0x000638a0` makes the FFT state once (global
`0x00188f54`), runs it, and stores `sqrt(power) * (1/256)` for 129 bins into a 128 entry
array, so the last bin lands on the next field.

## Flash overlay (`0x00063ef0`)

Bass is `(sum of right bins 0..4 >> 4 + sum of left bins 0..4 >> 4) / 10`. Globals at
`0x0017e6f4..0x0017e70c` keep a recent max, the previous max, when it rolled, the same
three for the min, and an "on" flag; `0x00188f50` counts frames. The max and min take
the new value when it beats them, and every time more than 10 frames have passed since
the last roll the recent value becomes the previous one and resets to 0. When bass
beats `(prevMax * 6 + prevMin * 4) / 10` and the flag is off, the whole surface is
filled with 0xff and the flag goes on. After bumping the counter, the flag goes off
again once bass drops under `(prevMax * 4 + prevMin * 6) / 10`.

## Wave overlay (`0x00064130`, 1761 bytes)

- Style counter at `0x0017603c` starts at -1. When it's negative or the clock passes
  the next change time (double at `0x00188f48`) it goes up by one and the next change
  is `g_now + 3 + rand() * (5/32767)`. Over 4 it wraps to 0.
- Cos and sin tables (`0x0017e778` / `0x0017e780`, width in `0x0017e774` / `0x0017e77c`)
  are rebuilt when the surface width changes: every 4th entry i holds
  `cos/sin(i / w * pi + pi/2)` (`0x0002bffc`, `0x0002c134`).
- Gap is `30 * h >> 8` (`0x00176038`), amplitude 50 (`0x00176034`), color 128
  (`0x00176040`). The starting height of both waves is `((L[0] + R[0]) >> 9) * h * 50
  >> 12`, held at 0 or above in style 3.
- Every 4 pixels, frame index `(i << 8) / w / 5`, the right wave height is the high
  byte of that right sample `* h * 50 >> 12`, the left one likewise. Style 0 draws the
  left wave below the middle, 1 adds the right wave above it, 2 draws both channels
  as two horizontal and two vertical lines either side of the middle, 3 and 4 bend the
  waves round a half circle using the tables (3 holds them at 0 or above). 3 and 4 then
  join the two ends with one more line.
- Last, two passes of 64 2x2 dots (`0x000640a0`, each pixel clipped to the surface) in
  255. Pass j runs a counter k from the global at `0x00188f40`:
  `x = cos(k / (80 + 107.2j))`, `y = sin(k / (1.756 * (80 + 74.4j)))`, both scaled by
  the surface height, rotated by k * 0.001 (`0x0002c050`) and centered. The counter is stored back after
  the second pass, so it advances 64 per frame.

## Theseus notes

- The desktop pulls the mixer's last 256 frames every frame instead of reading the
  source clip, since desktop music doesn't play through an AudioClip. The source is
  still advanced.
- Style 2 draws a mirrored half circle of the waveform (Theseus's own; 5960 has the
  case but no code).
- The analyzer takes `ln(bin + 1)`, so a silent band reads 0 instead of going through
  the log of zero, and the peak dot is clipped to the surface.
- The spinner doesn't make 5960's unused `rand()` call.
- The curve dots are scaled by h * 50 / 256.
