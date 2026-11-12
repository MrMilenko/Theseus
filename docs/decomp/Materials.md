# Materials

How the 5960 retail dashboard colors its meshes. A mesh names its material with a
`MaxMaterial` node; the name is looked up in a list of 127 material objects built by
`Material_Init` (`0x00057cb0`), and that object's one virtual, `Setup`, programs the
texture stages and render states for the draw. The full retail list, with colors, is in
`theseus/render/materials_retail.inl`.

## Render state decoding

Every device call in these functions is inlined. The texture stage shadow table starts at
`0x001539c0` (0x80 bytes a stage, one dword per `D3DTSS_*` value) and the render state
shadow at `0x0017a080` (one dword per `D3DRS_*` value), so for example `0x0017a2d0` is
TEXTUREFACTOR and `0x0017a2dc` / `0x0017a2e0` are EDGEANTIALIAS / MULTISAMPLEANTIALIAS.
The push buffer writers are `0x00143010` (TEXTUREFACTOR), `0x00142c40` (EDGEANTIALIAS),
`0x001433f0` (MULTISAMPLEANTIALIAS), `0x00142d20` (CULLMODE) and `0x00143790`
(TEXCOORDINDEX, stage in ESI). The "fade" below is the scene fade at `0x00175ca0`.

## The material object

| Offset | Field |
|--------|-------|
| 0x00 | vtable (the material type) |
| 0x04 | name |
| 0x08 | flags: 1 the mesh's own material, 2 cull none, 4 / 8 / 0x10 / 0x20 pick a mask, 0x40 no edge AA on a solid |
| 0x0c | solid types: R, G, B, A bytes. Falloff types: side color |
| 0x10 | falloff front color |
| 0x14 | falloff shader effect: 1 falloff, 2 aniso, 3 textured falloff, 4 reflect |
| 0x18 | falloff: a copy of the name |
| 0x220, 0x221 | key: bright, text |

`Material_Init` inlines every constructor: append to `g_rgMatInfo` (`0x0017c6b8`, count at
`0x0017e754`), store name and flags, then the type's fields. Afterwards the list is sorted
with `qsort` by `wcscmp` of the names (comparator `0x00057c70`).

## Base Setup (`0x00057650`)

Stage 0 TEXCOORDINDEX and TEXTURETRANSFORMFLAGS go to 0. Flag 1 returns 0, which tells the
mesh to use its own material. Flag 2 sets CULLMODE to none. Otherwise returns 1.

## The types

| vtable | Setup | Type | What it programs |
|--------|-------|------|------------------|
| `0x00028bf4` | `0x000576b0` | Solid | stage 0 color = TFACTOR, alpha = DIFFUSE x TFACTOR; factor = (a x fade, r, g, b); edge AA unless flag 0x40; multisample off |
| `0x00028bf0` | `0x00057790` | Falloff | needs a mesh; effect shader for the mesh's FVF; stage 0 off; edge AA on; side and front go to two vertex shader constants |
| `0x00028be8` | `0x00057940` | FalloffTex | effect 3; stage 0 color and alpha = DIFFUSE x TEXTURE; stage 1 off |
| `0x00028be0` | `0x00057af0` | Aniso | Falloff with effect 2 and no edge AA writes |
| `0x00028b6c` | `0x00056610` | InnerWall | FalloffTex plus wrap on U and V and cull none |
| `0x00028be4` | `0x00056d90` | Reflect | effect 4; constants (229, 229, 229, 0) / (229, 229, 229, 255); `SetReflectShaderFrameValues` (`0x0005c8d0`); color = TEXTURE x DIFFUSE, alpha = DIFFUSE |
| `0x00028bec` | `0x00056c70` | ModulateTexture | color = TEXTURE, alpha = TFACTOR; factor alpha pulses (below); edge AA on |
| `0x00028bf8` | `0x00056ea0` | Backing | mask on stage 0; color = TFACTOR, alpha = TEXTURE x TFACTOR |
| `0x00028bdc` | `0x00056940` | MaskTexture | mask on stage 1, which multiplies alpha and adds color; stage 2 multiplies alpha by the factor (fade x 255) |
| `0x00028bd0` | `0x000567d0` | FreeSpace | the memory gauge, below |
| `0x00028bd8` | `0x000572d0` | Icon | title images, below |
| `0x00028bcc` | `0x00057120` | Key | keyboard keys, below |
| `0x00028b68` | `0x00056f80` | EggGlowPulse | below |
| `0x00028b60` | `0x00057000` | EggGlow | below |
| `0x00028bd4` | `0x00056a50` | OrangeEggGlow | below |

The mask for Backing and MaskTexture is the first of flags 4, 8, 0x10, 0x20 that's set:
plain, edge, main edge, AV. With none set the stage keeps whatever texture it had.

**The pulse.** ModulateTexture, EggGlowPulse and the Live keys share a clock at
`0x0017c8c0` and remember which material last restarted it (`0x0017e750`). The level is
`1 - |sin((now - start) * 0.5 * PI)|`, one dip every two seconds. ModulateTexture restarts
the clock when a new material takes it and the fade is exactly 1, and multiplies its alpha
by `level * 0.25 + 0.75`. EggGlowPulse resets the start to 2.0 instead of now when it takes
the clock, uses side (178, 208, 0, 0) and front (255, 255, 128) with alpha
`level * 128 + 64`, then runs the Falloff Setup.

**EggGlow.** Script writes 1 into the MaxMaterial's `param` to fire it. Setup sees the
exact 1.0 (compared as raw bits), stores the time instead, and while `param > 0` fades
`t = (now - param) * 0.8`; at `t >= 1` param goes back to 0. Front is (254, 255, 188) with
alpha `(1 - t) * 255`, so the glow lasts 1.25 seconds. The constructor shifts its
arguments one slot, so the object's flags hold `0xE4FEFFBC` and its side is 0; Setup
overwrites the front every frame, so only the side and flags show the slip.

**Key.** The byte at `0x0017e74d` picks the palette. Clear: bright keys side
(243, 255, 107, 255) front (25, 225, 0, 34), other keys side (243, 255, 107, 222) front
(20, 192, 0, 15); text keys draw TFACTOR (190, 250, 94) with alpha fade x 178. Set (the
Live look): side (178, 208, 0, 255) front (178, 208, 0, 190), text factor black with alpha
fade x 192, and the pulse clock is taken when the fade is above 0. Both end with edge AA
on and the Falloff Setup.

**Icon.** Uses the current title image (`0x0017c8cc`) unless it's the selection icon,
which uses the selected one (`0x0017c8c8`). The stock logos load from the XIP:
`xboxlogo64.xbx`, `xboxlogo128.xbx` and `xboxlogo64a.xbx` for the current image,
`xboxlogo64.xbx`, `xboxlogo128.xbx`, `XboxLiveAccount64.tga` and `XboxLiveAccount128.tga`
for the selected one. With a texture: cull none, no Z write, alpha blend
SRCALPHA / INVSRCALPHA, the image on stage 0 (TEXCOORDINDEX and transform flags 0),
alpha = TEXTURE x TFACTOR, and the factor alpha fades in over a quarter second
(`(now - loaded) * 4`, capped at 1) when the icon fades in. Without one the factor is 0 and
the quad draws clear. When the byte at 0x0f is set, stage 1 gets the edge mask so the icon
has soft edges; every icon in the retail list has it except `MotdIcon`. It never runs the
base Setup.

**FreeSpace.** Stage 0 color = TEXTURE, alpha = TEXTURE x TFACTOR with factor
(fade x 70, 255, 255, 255). The texture transform is identity with
`_32 = ((1 - param) * 2 - 1) * 0.3 + 0.1`, flags COUNT2 and V clamped, so `param` slides the
gauge texture along the bar. Stage 1 color off. Returns 1 without the base Setup.

**OrangeEggGlow.** Sets the FVF `0x10102`, builds a texture matrix from the inverse of the
current view times a scale (`0x0002c19c`, `0x0002c198`) with an `fmod` animated offset,
and generates stage 0 coordinates from camera space position (TEXCOORDINDEX `0x20000`,
COUNT3). Stage 0 modulates diffuse with the texture, stage 1 multiplies alpha by the factor
(fade alpha), stage 2 off; edge AA on.

## Mask textures

`GenerateRadialAlphaMask` (`0x00056110`) writes a 32 bit alpha only circle: per pixel
`d = sqrt(dx^2 + dy^2)` with dx, dy running -1..1 over the drawn block,
`a = clamp((1 - clamp(d, 0, 1)) * factor, 0, max)` and alpha `(int)(a * 255)`. Only the top
left `width * scale` by `height * scale` block is drawn and the rest is cleared. The four
builders (`0x000562d0`, `0x000563a0`, `0x00056470`, `0x00056540`) each make one 256 by 256
A8R8G8B8 texture on first use, render the mask into a temporary buffer and swizzle it in:

| Builder | factor, max, scale | Used by |
|---------|--------------------|---------|
| plain | 1, 1, 1 | flag 4 |
| AV | 1, 1, 0.6875 | flag 0x20 |
| edge | 10, 0.8, 1 | flag 8 and every icon |
| main edge | 3, 1, 1 | flag 0x10 |

## MaxMaterial

0xa8 bytes, factory `0x00059ff0`, vtable `0x00028b70`: the Material fields, then name
(0x90), param (0x98, double) and the bound material object (0xa0).

- **OnSetProperty** (`0x0005a0f0`): a new name drops the binding so the next Render looks it
  up again; a param write widens the script's float into the double itself.
- **Render** (`0x0005a130`): binds by `bsearch` on first use (comparator `0x00057c90`) and
  draws nothing when the name isn't in the list. Then edge AA off and multisample on, the
  Tubes override flag (`0x0017c8d0`) set for "Tubes", "TubesFade", "TubesQ" and "Tube", and
  the material's Setup.
- **Destructor** (`0x0005a0d0`) frees the name.

## Theseus notes

- The colors come from `materials_retail.inl` and the skin, which can override any listed
  material; Key, EggGlow and EggGlowPulse take their colors from the skin too (the retail
  values reproduce the constants above).
- `orangeEggGlow` is a plain solid color; the desktop renderer has no camera space
  texture coordinate generation for its projection yet.
- Desktop draws the AV mask at full size.
- The skin editor's color access (`MatInfo_ColorCount` and friends) is a Theseus addition.
