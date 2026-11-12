# Camera

The 5960 retail dashboard has exactly one camera. It's a `Camera` node object sitting in
.data at `0x00187fb0`, and everything that moves the view (viewpoints, camera paths,
the noisy wobble) goes through it. `CameraPath` is the node that flies it along a pair
of interpolators. No shipping XAP uses `CameraPath`; level arrivals start one from code.

## Registration

| Class | Name string | Size | Factory | vtable |
|-------|-------------|------|---------|--------|
| CCamera | "Camera" | 0x90 | `0x00050640` | `0x00028350` |
| CCameraPath | "CameraPath" | 0x38 | `0x00050a70` | `0x000282f0` |

Camera props: `mode` (string, 0x1c), `lookat` (node, 0x20). Neither is read anywhere
in the binary. CameraPath props: `isActive` (0x20), `backward` (0x21), `interval`
(0x1c), `position` (0x24), `orientation` (0x28).

## Camera fields

| Offset | What |
|--------|------|
| 0x24 | position (vec3) |
| 0x30 | orientation (quaternion) |
| 0x40 / 0x4c | position and orientation when the last move started |
| 0x5c / 0x68 | where the move is going |
| 0x78 | move start time (double, from the global clock at `0x0017e808`) |
| 0x80 | move duration in seconds, 0 when idle |
| 0x84 | the position the last view matrix was built from |

The factory sets the duration to 0, position to (0, 0, 20), orientation to identity
(the 1.0 at `0x0002bed8` in w), mode to NULL, and also clears the noisy camera flag
at `0x0017c614`. Lookat is left as whatever the allocator returned. The destructor
(`0x00050760`) frees `mode` and runs the node destructor.

## Set (`0x000508e0`)

Copies the target into the "going to" slots, copies the current pose into the "came
from" slots, stamps the start time and stores the duration. A duration of zero or
less snaps position and orientation straight to the target.

## Advance (`0x000509a0`)

If a move is running, `f = (now - start) / duration`. At `f >= 1.0` the duration is
zeroed and `f` held at 1. Position is the straight line `old + (new - old) * f`
(written out per component) and orientation is `D3DXQuaternionSlerp`. Then it always
calls UpdateViewMatrix. It does not call the node Advance.

## UpdateViewMatrix (`0x00050780`)

Works on copies of position and orientation. When the noisy flag is set it builds a
small wobble: `t = now * 0.25` (double at `0x0002c178`), yaw `sin(t * PI/2)` and pitch
`sin(t * PI)`, each times one degree in radians (`0x0002c174`), roll 0, and multiplies
it onto the orientation copy. The constants mean the head drifts at most one degree.

It stores the position at 0x84, then

    D3DXMatrixAffineTransformation(&g_matPosition /*0x17f4a8*/, 1.0, NULL, &rot, &pos);
    D3DXMatrixInverse(&g_matView /*0x17f468*/, NULL, &g_matPosition);
    g_matView._31.._34 = 0.0 - each;      // SUBSS from a zeroed register

copies the 16 floats into the device's view transform at `pDevice + 0xc60`, and ORs
`0xff0200` into the dirty mask at `0x00156080`. Negating the third row is what turns
the scene's right handed coordinates into D3D's left handed view.

## CameraPath

The factory sets interval 1.0, both flags false and both node pointers NULL.

**Advance** (`0x00050b60`) runs the node Advance, then does nothing unless
`isActive`. Otherwise `t = (now - start) / interval`; at `t >= 1` it clamps to 1 and
clears `isActive`. With `backward` set, `t = 1 - t`. It starts from the camera's
current pose, replaces the position with `CPositionInterpolator::Interpolate(t)`
(`0x0005d300`) with z negated, replaces the orientation with
`COrientationInterpolator::Interpolate(t)` (`0x0005d5a0`) when those nodes are set,
and calls `Set` with a zero duration so the camera snaps there every frame.

**OnSetProperty** (`0x00050c90`) restamps the start time whenever the field at 0x20
(`isActive`) is written, and lets the write through.

**Destructor** (`0x00050af0`) releases `position` through vtable slot 4 but only
nulls `orientation` without releasing it.

The level code starts a path by setting `isActive`, the start time and `backward`
inline (see [SceneGroups.md](SceneGroups.md)).

## Theseus notes

- The camera's constructor keeps the noisy flag the config loader set; the binary's
  factory clears it.
- Theseus routes the view matrix through `TheseusSetTransform` instead of writing the
  device block directly.
