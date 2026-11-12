# HUD & Picture

The two screen-space overlay nodes in the 5960 retail binary. `HUD` is a group that
flattens its children onto the screen; `Picture` is a single textured (or flat
colored) quad positioned in pixels. In the shipping XAPs only `dvd.xap` uses `HUD`,
and nothing uses `Picture`.

## Registration

Both classes are registered from the static init code at `0x000cd010` and
`0x000cd040`.

| Class | Name string | Class object | Size | Factory | Base | Prop map | vtable |
|-------|-------------|--------------|------|---------|------|----------|--------|
| CHud | "HUD" `0x0002891c` | `0x00188148` | 0x44 | `0x00052ae0` | Group `0x00187c68` | `0x00175190` | `0x00028870` |
| CPicture | "Picture" `0x00028900` | `0x00188174` | 0x78 | `0x00052db0` | Node `0x0017f528` | `0x001751a8` | `0x00028810` |

Virtual slots used below: 0 scalar deleting dtor, 1 GetPropMap, 2 OnSetProperty,
4 Release, 6 GetNodeClass, 14 OnLoad, 15 Advance, 16 Render, 22 GetTextureSurface.

The HUD prop map has no entries of its own and chains to Group. The Picture map,
in table order:

| Offset | Type | Name |
|--------|------|------|
| 0x20 | number | top |
| 0x1c | number | left |
| 0x24 | number | width |
| 0x28 | number | height |
| 0x2c | node | texture |
| 0x30 | number | transparency |
| 0x34 | number | fade |
| 0x38 | color | color |

Neither class has a function table, so neither exposes script methods.

## CHud

The factory (`0x00052ae0`) is the Group constructor inlined: zero children, bbox
center 0, bbox size -1 on every axis, both bbox flags set, then the HUD vtable. The
destructor (`0x00052b80`) only resets the vtable and runs Group's.

**Render (`0x00052b90`).** Every device call is inlined. Decoded, in order:

```c
D3DXMatrixOrthoLH(&ortho, 720.0, 480.0, -10000.0, 10000.0);
savedProj = g_matProjection /* 0x0017f4e8 */;
g_matProjection = ortho;  SetTransform(PROJECTION, &g_matProjection);   // 0x00144e30
savedView = g_matView /* 0x0017f468 */;
g_matView = g_matIdentity /* 0x00176338 */;  SetTransform(VIEW, ...);    // inline copy to device +0xc60
lighting = rs[LIGHTING] /* 0x0017a218 */;  rs[LIGHTING] = 0;
worldStack->Push(); worldStack->LoadIdentity();                         // stack 0x0017e7fc, slots 4, 5
SetTransform(WORLD, worldStack->GetTop()); FUN_0005ca00();              // falloff shader constants
rs[EDGEANTIALIAS] = 0; rs[MULTISAMPLEANTIALIAS] = 1;
CGroup::Render();                                                       // 0x0004d7e0
worldStack->Pop(); SetTransform(WORLD, worldStack->GetTop()); FUN_0005ca00();
rs[LIGHTING] = lighting;
g_matProjection = savedProj; SetTransform(PROJECTION, ...);
g_matView = savedView;       SetTransform(VIEW, ...);
```

The ortho box is a fixed 720x480, so HUD children are laid out in that space no
matter the video mode.

## CPicture

| Offset | Field |
|--------|-------|
| 0x1c..0x28 | left, top, width, height |
| 0x2c | texture node |
| 0x30 | transparency |
| 0x34 | fade (seconds) |
| 0x38..0x40 | color |
| 0x44 | alpha |
| 0x48 | fade start time (double) |
| 0x50..0x6c | start/end pairs for left, top, width, height |
| 0x70 | vertex buffer |

**Factory (`0x00052db0`).** left = top = 0, width = height = -1.0 (`0x0002bef0`),
texture = 0, transparency = fade = 0, color = (1, 1, 1), alpha = 1.0, fade start =
0.0, vertex buffer = 0. The start/end pairs are not touched.

**Destructor (`0x00052e70`).** Releases the vertex buffer, then calls Release on
the texture node.

**Render (`0x00052f10`).**

```c
if (alpha <= 0) return;
surface = 0;
if (texture && (surface = texture->GetTextureSurface()) == 0) return;
if (!vb) vb = CreateVertexBuffer(0x90);              // inline XMemAlloc, 4 x 0x24 bytes
w = width; h = height;
if (w < 0 || h < 0) {
    if (!surface) return;
    GetLevelDesc(surface, &desc);                      // 0x0014bec0
    if (w < 0) w = desc.Width;  if (h < 0) h = desc.Height;
    if (texture is a CTexture /* class 0x001885f4 */) {
        w = (texture->imageWidth  / w) * w;            // fields +0x28, +0x2c
        h = (texture->imageHeight / h) * h; } }
u = v = 1.0;
if (texture is a CTexture) { GetLevelDesc(surface, &desc);
    u = imageWidth / desc.Width;  v = imageHeight / desc.Height; }
SetTexture(0, surface);                                 // 0x00145160
quad[0] = { left+w, top+h, color 0xffffffff, u, v };
quad[1] = { left,   top+h, color 0xffffffff, 0, v };
quad[2] = { left+w, top,   color 0xffffffff, u, 0 };
quad[3] = { left,   top,   color 0xffffffff, 0, 0 };
for each corner {                                       // view size at 0x0017e7f0 / 0x0017e7f4
    x -= viewWidth * 0.5;  y = 0.0 - (y - viewHeight * 0.5);
    z = 0; normal = (x, y, 1.0); }
rs[ALPHABLENDENABLE] = 1; rs[SRCBLEND] = 0x302; rs[DESTBLEND] = 0x303;
rs[TEXTUREFACTOR] = ARGB(alpha*255, r*255, g*255, b*255);   // 255.0 at 0x0002bf38, ftol each
if (surface) { COLOROP = MODULATE(TEXTURE, TFACTOR); ALPHAOP = MODULATE(TEXTURE, TFACTOR);
               MINFILTER = MAGFILTER = POINT; }
else         { COLOROP = SELECTARG1(TFACTOR); ALPHAOP = SELECTARG1(TFACTOR); }
rs[EDGEANTIALIAS] = 0; rs[MULTISAMPLEANTIALIAS] = 1;
lock vb (wait on the GPU if busy); copy 0x24 dwords;
SetStreamSource(0, vb, 0x24); SetVertexShader(0x152);   // XYZ | NORMAL | DIFFUSE | TEX1
DrawPrimitive(TRIANGLESTRIP, 0, 2);
SetTexture(0, NULL); rs[ALPHABLENDENABLE] = 0;
MINFILTER = MAGFILTER = LINEAR;
```

The vertex buffer create and lock are never checked. The two filter states go back
to linear even on the untextured path, where they were never changed.

**Advance (`0x000534c0`).**

```c
CNode::Advance(s);
if (texture) texture->Advance(s);
if (fade != 0 && fadeStart != 0.0) {
    t = (float)((now /* 0x0017e808 */ - fadeStart) / fade);   // x87, rounded once at the end
    if (t > 1.0) { fadeStart = 0.0; t = 1.0; }
    left   = startLeft   * (1 - t) + endLeft   * t;
    top    = startTop    * (1 - t) + endTop    * t;
    width  = startWidth  * (1 - t) + endWidth  * t;
    height = startHeight * (1 - t) + endHeight * t; }
```

Alpha isn't animated here; transparency fades go through a lerper instead.

**OnLoad (`0x000535c0`).** alpha = 1.0 - transparency, each start and end value =
the current left/top/width/height, then CNode::OnLoad.

**OnSetProperty (`0x00053620`).** Keyed on the PRD offset:

```c
case 0x30 /* transparency */:
    a = 1.0 - value;
    if (fade == 0) alpha = a;
    else new CLerper(this, &alpha, a, fade);   // 0x28 bytes, linked at the head of 0x0017aeac
    return 1;
case 0x34 /* fade */:
    if (value == 0) fadeStart = 0.0;
    CLerper::RemoveObject(this);
    return 1;
case 0x1c /* left */:  endLeft = value;   startLeft = left;     break;
case 0x20 /* top */:   endTop = value;    startTop = top;       break;
case 0x24 /* width */: endWidth = value;  startWidth = width;   break;
case 0x28 /* height */:endHeight = value; startHeight = height; break;
default: return 1;
}
if (fade != 0) { fadeStart = now; return 0; }   // veto: Advance moves it there
return 1;
```

A move with a fade set returns 0, so the property itself keeps its old value and
Advance walks it from start to end over `fade` seconds.
