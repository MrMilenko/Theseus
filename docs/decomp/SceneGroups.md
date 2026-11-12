# Scene Groups (scene_groups.cpp)

Container and grouping nodes in the XAP scene graph. These handle
hierarchy, transforms, level transitions, and viewport management.

## Node Classes

All node type strings confirmed in the 4920-5960 retail XBE:

| Node | String Address | Class String Address | Parent |
|------|---------------|----------------------|--------|
| Group | 0x00027f04 | 0x00027ef4 "CGroup" | CNode |
| Transform | 0x00027ecc | 0x00027eb4 "CTransform" | CGroup |
| Inline | 0x00027d54 | 0x00027d44 "CInline" | CGroup |
| Spinner | 0x00027ce0 | 0x00027ccc "CSpinner" | CGroup |
| Waver | 0x00027ca0 | 0x00027c90 "CWaver" | CSpinner |
| Layout | 0x00027c74 | 0x00027c64 "CLayout" | CGroup |
| Switch | 0x00027c40 | 0x00027c30 "CSwitch" | CNode |
| Billboard | 0x00027bf4 | 0x00027bdc "CBillboard" | CGroup |
| Layer | 0x00029a10 | 0x00029a00 "CLayer" | CGroup |
| Background | 0x000281d4 | 0x000281bc "CBackground" | CNode |
| Level | 0x00029f00 | 0x00029ef0 "CLevel" | CGroup |

## Class Registration Data

FND table entry confirmed for CTransform:
- `_c_rgfnd_CTransform` at 0x00014b68

PRD table entry confirmed for CInline:
- `_m_rgprd_CInline` at 0x00021a94

Class registration objects:
- `_classCTransform` at 0x0001eb64
- `_classCInline` at 0x00013dac
- `_classCSpinner` at 0x000145cc
- `_classCSwitch` at 0x00013db4
- `_classCBillboard` at 0x00014b3c
- `_classCLayout` at 0x00014b38
- `_classCLayer` at 0x0001f33c
- `_classCLevel` at 0x000213d8 (vtable `___7CLevel__6B_`)

## Level Transition Globals

Level transition state found in .data section:
- `g_timeToNextLevel` at 0x0001f558 (labeled `_g_timeToNextLevel__3NA`)

## Properties (from FND/PRD tables)

### CGroup
- `children` (pt_children)

### CTransform
- `center` (pt_vec3), `scaleOrientation` (pt_vec4), `scale` (pt_vec3)
- `rotation` (pt_vec4), `translation` (pt_vec3)
- `fade` (pt_number), `moving` (pt_boolean), `alpha` (pt_number)
- Functions: SetScale, SetScaleOrientation, SetTranslation, SetCenter,
  SetRotation, SetAlpha, DisappearAfter

### CInline
- `url` (pt_string), `preload` (pt_boolean), `fadeInDelayLoad` (pt_boolean)

### CSpinner
- `rpm` (pt_number), `axis` (pt_vec3), `angle` (pt_number)

### CWaver (extends CSpinner)
- `field` (pt_number)

### CLayout
- `direction` (pt_vec3), `spacing` (pt_number)

### CSwitch
- `whichChoice` (pt_integer), `choice` (pt_nodearray)

### CBillboard
- `axisOfRotation` (pt_vec3)

### CLayer
- `viewpoint` (pt_node), `navigationInfo` (pt_node)
- `fade` (pt_number), `transparency` (pt_number)

### CBackground
- `skyColor` (pt_vec3), `backdrop` (pt_node), `isBound` (pt_boolean)
- `fade` (pt_number), `alpha` (pt_number)
- Functions (FND `0x00187f10`): SetAlpha, DisappearAfter

### CLevel
- `control` (pt_node), `tunnel` (pt_node), `path` (pt_node), `shell` (pt_node)
- `archive` (pt_string), `unloadable` (pt_boolean), `fade` (pt_boolean)
- Functions: GoTo, GoBackTo
- Script callbacks: OnActivate, OnArrival, OnDeactivate

## Notes

- CWaver inherits CSpinner but its IMPLEMENT_NODE parent is CGroup, not
  CSpinner.
- CInline has a thread handle member for background class loading, but
  the retail binary does not spawn a thread for this.
- CBackground has no Render() override in the retail binary. Only
  RenderBackdrop() is used.
- The node vtables have no light slot. Slot 19 (`+0x4c`) of both the
  Group and Switch vtables is the shared stub at `0x00041da0`; the slots
  around it are GetRadius (18), RenderDynamicTexture (20), GetGroundHeight
  (21) and GetTextureSurface (22).

## Group.cpp, Background, Layer and Level in the 5960 retail XBE

| Range | Contents |
|---|---|
| `0x0004d3b0` to `0x0004f3d0` | Group, Transform, Inline, Spinner, Waver, Layout, Switch, Billboard |
| `0x0004f3d0` | Billboard's matrix helper (only called from its Render) |
| `0x0004fc40` to `0x000504c0` | Background |
| `0x0005a290` to `0x0005ab30` | Layer |
| `0x0005cbd0` to `0x0005d230` | Level |

Every world matrix change in this code is the same three steps: a call on the matrix stack at
`0x0017e7fc` (Push `+0x10`, Pop `+0x0c`, LoadIdentity `+0x14`, MultMatrixLocal `+0x20`,
RotateAxisLocal `+0x28`, TranslateLocal `+0x40`), a 16 dword copy of the stack top (`+0x44`)
into the device's world matrix with dirty bit `0x200`, then `0x0005ca00`, which recomputes and
uploads the vertex shader constants. `0x0002c490` is that upload on its own and `0x0002c4d0` is
Pop plus upload. The effect alpha that nodes multiply into is the float at `0x00175ca0`, the
frame clock is the double at `0x0017e808`.

### Object layouts

```
CGroup      0x44   +0x1c children (count, capacity, array)  +0x28 bboxCenter
                   +0x34 bboxSize  +0x40 bboxSpecified  +0x41 bboxDirty
CTransform  0x1a8  +0x44 rotation (axis, angle)  +0x54 center  +0x60 scaleOrientation
                   +0x70 scale  +0x7c rotation quaternion  +0x8c translation
                   +0xb4/+0xc0/+0xd0 scale/rotation/translation start
                   +0xf8/+0x104/+0x114 scale/rotation/translation end
                   +0x120 alpha  +0x124 alpha start  +0x128 alpha end
                   +0x12c moving  +0x130 dirty (dword)  +0x134 matrix  +0x174 fade
                   +0x178, +0x17c two floats zeroed and never read
                   +0x180 scale clock  +0x188 rotation clock  +0x190 translation clock
                   +0x198 alpha clock  +0x1a0 disappear time (all doubles)
CInline     0x2e0  +0x44 url  +0x48 preload  +0x49 class loaded  +0x4a fadeInDelayLoad
                   +0x4b dirty  +0x4c loading  +0x4d first load  +0x50 XIP
                   +0x58 time of load (double)  +0x60 CClass  +0xd0 thread handle
                   +0xd4 absolute url (260 wide chars)
CSpinner    0x58   +0x44 rpm  +0x48 axis  +0x54 angle
CWaver      0x5c   +0x58 field
CLayout     0x54   +0x44 direction  +0x50 spacing
CSwitch     0x2c   +0x1c whichChoice  +0x20 choice (count, capacity, array)
CBillboard  0x90   +0x44 axisOfRotation  +0x50 matrix
CLayer      0x5c   +0x44 viewpoint  +0x48 navigationInfo  +0x4c fade  +0x50 transparency
                   +0x54 vertex buffer  +0x58 alpha (not set by the constructor)
CBackground 0x60   +0x1c backdrop  +0x20 skyColor  +0x2c isBound  +0x30 fade
                   +0x38 alpha clock (double)  +0x40 alpha start  +0x44 alpha end
                   +0x48 alpha  +0x50 disappear time (double)  +0x58 vertex buffer
CLevel      0x68   +0x44 control  +0x48 tunnel  +0x4c path  +0x50 shell  +0x54 archive
                   +0x58 unloadable  +0x59 fade  +0x5c a float never touched
                   +0x60 XIP  +0x64 arrive
```

Defaults: Group bbox size `-1.0` on all three axes with specified and dirty set; Transform
rotation and scaleOrientation `(0,0,1,0)`, scale `(1,1,1)`, alpha 1, dirty 1; Inline
fadeInDelayLoad 1, dirty 1; Spinner rpm 1, axis `(0,1,0)`; Waver field `pi/4`; Layout
direction `(1,0,0)`, spacing 1; Switch whichChoice -1; Billboard axis `(0,1,0)`; Background
alpha 1; Level unloadable 1, fade 1, visible 0. Level's constructor also zeroes the shared
transition clock. Background's constructor makes the new node the bound background (global
`0x0017e824`) when there isn't one yet.

Level globals: current level `0x0017c8f0`, level being left `0x0017c8ec`, current tunnel
`0x0017c8e8`, transition end time `0x0017c8e0` (double), transition flag `0x0017c8d8`.
The current Level writes the motion blur strength at `0x00179f54`.

### Function map

| Address | Function | What it does |
|---|---|---|
| `0x0004d3d0` | Group factory | `new` 0x44, constructor inlined |
| `0x0004d470` | CGroup::~CGroup | ReleaseAll on the children, then CNode teardown |
| `0x0004d4d0` | CGroup::GetBBox | See below |
| `0x0004d770` | CGroup::GetRadius | Largest child radius, starting from 0 |
| `0x0004d7e0` | CGroup::Render | Render each child whose visible byte is set |
| `0x0004d820` | CGroup::Advance | CNode::Advance, then every child |
| `0x0004d860` | CGroup::RenderDynamicTexture | Every non-null child |
| `0x0004d8a0` | CGroup::GetTextureSurface | First non-null child texture |
| `0x0004d910` | CTransform::CTransform | Defaults above |
| `0x0004da40` | CTransform::OnLoad | CNode::OnLoad, then the quaternion from the axis and angle |
| `0x0004da60` | CTransform::OnSetProperty | Rotation rebuilds the quaternion; alpha goes through SetAlpha and returns 0 |
| `0x0004dae0` | CTransform::Advance | Alpha fade step, disappear timer |
| `0x0004dbb0` | CTransform::CalcMatrix | See below |
| `0x0004df30`..`0x0004e250` | Transform script functions | See below |
| `0x0004e270` | CTransform::Render | See below |
| `0x0004e3b0` | CInline::CInline | Defaults above, CClass constructed in place |
| `0x0004e470` | CInline::~CInline | Free the url, CClass teardown, group teardown |
| `0x0004e4d0` | CInline::Init | See below |
| `0x0004e560` | CInline::FinishLoad | See below |
| `0x0004e5d0` | CInline::Render | See below |
| `0x0004e740` | CInline::Advance | See below |
| `0x0004e8c0` | Spinner and Waver Render | Push, rotate about axis by angle, upload, children, Pop |
| `0x0004e980` | CSpinner::Advance | See below |
| `0x0004eaf0` | CWaver::Advance | See below |
| `0x0004ebd0` | CLayout::Render | See below |
| `0x0004ef00` | CLayout::GetBBox | See below |
| `0x0004f000` | CLayout::GetRadius | Sum of child radii, plus `(count - 1) * spacing * 0.5` when count > 1 |
| `0x0004f100` | CSwitch::~CSwitch | ReleaseAll on the choices, CNode teardown |
| `0x0004f160` | CSwitch::Render | Render the picked choice if in range and visible |
| `0x0004f190` | CSwitch::GetBBox | Picked choice's box, else zeros |
| `0x0004f1e0` | CSwitch::GetRadius | Picked choice's radius, else 0 |
| `0x0004f210` | CSwitch::Advance | CNode::Advance, then every choice |
| `0x0004f330` | CBillboard::Render | Matrix helper, Push, MultMatrixLocal, upload, children, Pop |
| `0x0004f3d0` | Billboard matrix | See below |
| `0x0004fc90` | CBackground::CBackground | Defaults above |
| `0x0004fd30` | CBackground::~CBackground | Release the vertex buffer and backdrop, unbind if bound |
| `0x0004fde0`, `0x0004fe20` | Background SetAlpha, DisappearAfter | Same shape as Transform's, on the Background fields |
| `0x0004fe30` | CBackground::Advance | See below |
| `0x00050110` | CBackground::RenderBackdrop | See below |
| `0x00050450` | CBackground::OnSetProperty | isBound true binds; alpha goes through SetAlpha and returns 0 |
| `0x0005a350` | CLayer::~CLayer | Release viewpoint, navigationInfo, vertex buffer |
| `0x0005a400` | CLayer::OnSetProperty | See below |
| `0x0005a4a0` | CLayer::Render | See below |
| `0x0005aaf0` | CLayer::Advance | Group advance, then navigationInfo, then viewpoint |
| `0x0005cc20` | CLevel::CLevel | Defaults above |
| `0x0005ccd0` | CLevel::~CLevel | Clear itself out of the level globals, release tunnel, control, path, shell, free archive |
| `0x0005cd80` | CLevel::Render | See below |
| `0x0005ce90` | CLevel::Activate | See below |
| `0x0005cf90` | CLevel::GoTo | See below |
| `0x0005d040` | CLevel::GoBackTo | See below |
| `0x0005d120` | CLevel::Advance | See below |
| `0x0005d200` | CLevel::Deactivate | Hide, `OnDeactivate`, drop the XIP's mesh buffers when unloadable and not locked |

### CGroup::GetBBox (`0x0004d4d0`)

A size of `-1` on every axis clears the specified flag. When dirty and not specified, an empty
group gets a zero box. Otherwise it takes the first child's box as `lo = center - size`,
`hi = center + size`, and for each later child keeps the smaller value on both corners:

```c
if (b.c - b.s < lo) lo = b.c - b.s;
if (b.c + b.s < hi) hi = b.c + b.s;      // smaller, not larger
center = (hi + lo) * 0.5;  size = hi - lo;
```

So the far corner shrinks rather than grows, and the result is not a union. Dirty is never
cleared, so this reruns on every call.

### CTransform

**CalcMatrix (`0x0004dbb0`).** Identity, then for each of scale, translation and rotation
whose clock is above 0: set moving, `t = (now - clock) / fade`, and at `t >= 1` stop the clock
and clamp. Scale and translation lerp component by component; rotation goes through
`D3DXQuaternionSlerp`. The matrix is then built as scale (skipped when `(1,1,1)`), times the
quaternion's rotation (skipped when its w is 0), times translation (skipped when zero). Dirty
ends up as `scaleClock + rotationClock + translationClock > 0`. Center and scaleOrientation
are stored but never used here.

**Script functions.** SetScale (`0x0004df30`) and SetTranslation (`0x0004e010`): with
`fade > 0`, start the clock at now, copy the current value to start, the arguments to end, set
moving; otherwise store directly. Both set dirty. SetRotation (`0x0004e0f0`) builds the
quaternion first, then either fades from the current quaternion (leaving the axis/angle
property alone) or stores both. SetCenter and SetScaleOrientation store and set dirty. SetAlpha
(`0x0004e200`) fades or stores alpha without touching dirty. DisappearAfter (`0x0004e250`)
stores `t + now` added in single precision, as a double.

**Advance (`0x0004dae0`).** Group advance, then the alpha fade (same clock math), then if the
disappear time is set and reached, clear it and set alpha to 0.

**Render (`0x0004e270`).** Clear moving, CalcMatrix when dirty, Push, MultMatrixLocal, upload.
Only with alpha above 0: multiply the effect alpha by it, render the children, restore. Pop
and upload.

### CInline

**Init (`0x0004e4d0`).** Clear dirty, ReleaseAll on the children, stop if there's no url.
Make the url absolute and replace everything from its last `/` with `.xip`, so `Foo/default.xap`
names `Foo.xip`. If there was a slash: set loading and first load and call LoadXIP; on success
return and let Advance finish. Otherwise clear loading and FinishLoad right away.

**FinishLoad (`0x0004e560`).** `LoadAbsURL` on the node's CClass with the absolute url at
`+0xd4`. On success: CreateNode, call its `initialize`, append it to the children (growing 16
slots at a time), set its parent, call the Inline's own `onLoad`. Then clear first load and
set class loaded. The absolute url is only written by Advance, after the XIP arrives.

**Render (`0x0004e5d0`).** Nothing without a url or while loading. When the XIP is loaded, not
reloading, has vertex buffers and its first buffer is gone, it sets loading, starts the XIP
reload and returns. Otherwise Init when dirty, and once loaded: `a = (now - timeOfLoad) * 0.5`,
capped at 1 with fadeInDelayLoad, 1 without. The effect alpha is multiplied by `a`, the
current directory is saved and set from the url, the children render, and both are restored.

**Advance (`0x0004e740`).** Init when dirty and preload. While the XIP is not loaded or is
reloading, set loading and return without advancing the children. On the first frame it is
ready: clear loading; on a first load make the absolute url and, with no thread handle,
FinishLoad; record the time of load. A thread handle that has finished is closed (nothing in
the image creates one). Then Group advance.

### Spinner and Waver

**CSpinner::Advance (`0x0004e980`).** Group advance, `angle += rpm * (1/60) * dt * 2pi`, then
wrap into `[-2pi, 2pi]` with a subtract loop above and an add loop below. Constants: `1/60` at
`0x0002c1c4`, `2pi` at `0x0002bf70`, `-2pi` at `0x0002c1c0`.

**CWaver::Advance (`0x0004eaf0`).** Group advance (not Spinner's), then in x87:

```
fld   [rpm]
fmul  qword [now]
fmul  qword [0x0002c1b8]     ; pi / 60
fsin
fmul  [field]
fmul  [0.5]
fstp  [angle]
```

### CLayout

**Render (`0x0004ebd0`).** Nothing for no children. A zero direction lays the children on a
ring of radius `(count / 4) * spacing` (integer divide). Each child gets a Push first; a visible
one is translated by `(cos(-a), 0, sin(-a)) * radius` with `a = i * 2pi / count`, rotated
`a + pi/2` about `(0, 0.1, 0)`, uploaded, rendered, popped. An invisible child's Push is never
popped. Otherwise: one Push, translate by `direction * (count + spacing) * -0.5`, then per
visible child translate `direction * 0.5`, upload, render, and unless it's the last child
translate `direction * (spacing + 0.5)`. Pop at the end.

**GetBBox (`0x0004ef00`).** Zeros for no children. Otherwise the first child's box, then for
each later child: `size += child.size`, then each axis takes the child's size if that's
smaller. The center is the first child's.

### Billboard matrix (`0x0004f3d0`)

Copies the device world matrix, inverts it and brings the camera position (`0x00187fd4`) into
local space with `D3DXVec3TransformNormal`, normalized (the eye). Axis length at most `0.001`
(`0x0002c050`): up is `(inv._14, inv._22, inv._23)` normalized, `x = normalize(up x eye)`, and
the matrix rows are x, up, eye, `(0,0,0,1)`. Otherwise the axis is normalized,
`x = normalize(axis x eye)`, `z = x x axis`, and the matrix is a rotation about the axis by
`acos(z.z)`, negated when `x.z > 0`.

### CBackground

**Advance (`0x0004fe30`).** CNode::Advance, the alpha fade and disappear timer (same clock
math as Transform, on the Background fields), the backdrop's Advance, and on first use a 4
vertex, 32 byte stride buffer at `z = -10000` (`0x0002c1a0`):

| Vertex | Position | Normal | UV |
|---|---|---|---|
| 0 | `( w/2, -h/2)` | `( w/2, -h/2, 1)` | `(1, 1)` |
| 1 | `(-w/2, -h/2)` | `( w/2,  h/2, 1)` | `(0, 1)` |
| 2 | `( w/2,  h/2)` | `( w/2,  h/2, 1)` | `(1, 0)` |
| 3 | `(-w/2,  h/2)` | `(-w/2,  h/2, 1)` | `(0, 0)` |

`w` and `h` are the view size at `0x0017e7f0` / `0x0017e7f4`.

**RenderBackdrop (`0x00050110`).** Gets the backdrop's texture (vtable `+0x58`) and does
nothing without one. Binds it to stage 0 and clears stage 1, saves projection, world and view,
sets `D3DXMatrixOrthoLH(w, h, -10000, 10000)` with identity world and view. Saves four render
states, clears two of them, sets culling to CCW (`0x901`) and turns blending on; stage 0 color
is the texture and alpha is the texture factor, `(int)(alpha * 255) << 24`. Draws the strip
and restores all of it.

### CLayer

**OnSetProperty (`0x0005a400`).** A transparency write removes any lerper on the node, then
either sets alpha to `1 - value` (fade at most 0) or starts a 0x28 byte lerper from the
current alpha to `1 - value` over fade seconds. Every write is accepted.

**Render (`0x0005a4a0`).** Nothing without a viewpoint or with alpha 0. On first use it
builds a 4 vertex, 16 byte stride buffer of screen corners `( w/2,-h/2)`, `(-w/2,-h/2)`,
`( w/2, h/2)`, `(-w/2, h/2)` at z 0, color `0xff000000`. Then, under the ortho projection with
identity world and view: stage 0 alpha = diffuse times texture factor `(int)(alpha * 255) << 24`,
color = diffuse, texture transform off, texcoord index 0, blending on, culling off, stage 1
color and alpha off; draws the strip as a triangle strip and restores the three matrices.

It then walks the navigationInfo's class chain looking for NavigationInfo (`0x00187f58`) but
doesn't act on the answer. Projection: near `0.1`, far `1000` (`0x0002bfc0`, `0x0002c060`);
with a navigationInfo, near is `avatarSize.x * 0.5` and far is `visibilityLimit` when that is
nonzero. Field of view from the viewpoint (`+0x20`), aspect `1.5` (`0x0002c184`) or `1.875`
(`0x0002c1b0`) when the widescreen flag at `0x0017e7ec` is set, written into the shared
projection at `0x0017f4e8`. View: position `(x, y, -z)` (`+0x38`), quaternion about the
orientation axis (`+0x28`) by minus its angle (`+0x34`), `D3DXMatrixAffineTransformation`
with scale 1, inverted, and the third row (`_31` to `_34`) negated, into the shared view at
`0x0017f468`. Push, LoadIdentity, upload, render the children, Pop, then put the saved
projection and view back both in the shared matrices and on the device.

### CLevel

**Render (`0x0005cd80`).** With fade: `t = (endTime - now) * 1.3333334`, clamped to `[0, 1]`
(`0x0002c104` is `1 / 0.75`). The current level uses `1 - t`, the level being left uses `t`,
and either one returns early at 0. Any other level renders at full strength. The effect alpha
is multiplied, the shell renders (no visible test), the current level sets the motion blur to
`min(a * 1.3, 1)`, the children render, the effect alpha is restored.

**Activate (`0x0005ce90`, not script visible).** Set the transition flag. Load the archive XIP
if there is one and it isn't loaded yet. If a transition is still running (a level being
left, or a current level with the end time still set), cut it short: Deactivate the level
being left (inlined) and clear it; hide the current level and drop its XIP's mesh buffers
when it has a XIP, is unloadable and the XIP isn't locked; clear the current level; hide and
clear the tunnel. Then unbind the controller (BindJoystick with no node, inlined), set the
end time to `now + 1.0`, make the current level the one being left, show this level and make
it current.

**GoTo (`0x0005cf90`).** Nothing if already current. Activate, make this level's tunnel the
current tunnel and show it. A path whose class chain reaches Viewpoint (`0x00187e88`) gets
bound; one that reaches CameraPath (`0x00188040`) is started forward (active `+0x20` = 1,
start `+0x30` = now, backward `+0x21` = 0). Then `OnActivate`.

**GoBackTo (`0x0005d040`).** Nothing if already current. Activate; the tunnel is the one of
the level being left (none if there isn't one) and is shown. If that level's path is a
CameraPath it is started backward; otherwise a Viewpoint path on this level is bound. Then
`OnActivate`.

**Advance (`0x0005d120`).** Group advance, then tunnel, path, control and shell. When this is
the current level and the end time is set and reached: clear it, hide the tunnel, Deactivate
and clear the level being left, and set arrive. While arrive is set and the XIP is absent or
loaded (`+0x28`): clear arrive, bind this level's control as the controller, call
`OnArrival`, clear the transition flag.

### Quirks

- Group's far corner and Layout's size both shrink toward the smallest child instead of
  covering all of them.
- Layout's ring pushes for every child but pops only for visible ones.
- Layer never initializes its alpha; the first transparency write sets it.
- Setting a Background's isBound to false only clears the flag; it stays the bound
  background.
- Deactivate reads the XIP's locked flag without checking the XIP exists, so an unloadable
  level with no archive faults when it is left.
- An Inline whose XIP fails to load runs FinishLoad with the absolute url never written.
- A Background, being a plain CNode, clears its own visible flag the first time a group
  renders it.
