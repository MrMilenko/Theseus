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

## Palette Presets (10 styles)

0: Firestorm, 1: Aqua, 2: Purple & Blues, 3: Color Wheel,
4: Bizarro Mystery Unveiled, 5: Bizarro Color Wheel, 6: Dark Rainbow,
7: Ice Nightshade, 8: Mystery Unveiled, 9: Roundabout

## Delta Field Styles (24 styles)

0: Radial Breakaway, 1: Hip-no-therapy, 2: Sunburst Many, 3: Theta Divergence,
4-5: Turbo Flow Out, 6: Boxilite, 7: Collapse & Turn, 8: Constant Out,
9-10: Directrix Expand, 11: Equalateral Hyperbola, 12: Expand & Turn,
13: Gravity, 14: In/Out Inner Turn, 15: Left Turn Flow, 16: Linear Spread,
17: Noise Field, 18: Right Turn, 19: Scattered Flow, 20: Sine-Sphere,
21: Sine Multi-Circ, 22: Sphere, 23: Sunburst Few

## Notes

- CSurfx operates on a raw BYTE array with manual pitch calculation.
- The FFT is a standard radix-2 Cooley-Tukey with precomputed twiddle factors. Buffer size is 256 samples (FFT_BUFFER_SIZE_LOG=8).
- CDynamicTexture uses the Xbox Swizzler API for texture upload.
