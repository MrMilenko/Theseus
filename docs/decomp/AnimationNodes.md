# Animation & Viewpoint Nodes

The keyframe and binding nodes in the 5960 retail binary: `TimeSensor`, the two
interpolators, `Viewpoint` and `NavigationInfo`. All of them follow VRML97 names, but
the behavior is the dashboard's own and smaller than the spec.

## Registration

| Class | Name | Size | Factory | vtable |
|-------|------|------|---------|--------|
| CTimeSensor | "TimeSensor" | 0x50 | `0x00041bf0` | `0x00026018` |
| CPositionInterpolator | "PositionInterpolator" | 0x34 | `0x0005d250` | `0x00029f70` |
| COrientationInterpolator | "OrientationInterpolator" | 0x34 | `0x0005d470` | `0x00029f10` |
| CViewpoint | "Viewpoint" | 0x48 | `0x0004f770` | `0x00027f10` |
| CNavigationInfo | "NavigationInfo" | 0x40 | `0x000504d0` | `0x000281f0` |

## TimeSensor

On top of the time dependent node (loop 0x1c, startTime 0x20, stopTime 0x24,
isActive 0x28) it adds cycleInterval 0x30, enabled 0x34, cycleTime 0x38 (double),
fraction_changed 0x40 and time 0x48 (double). The factory sets cycleInterval to 1.0
and enabled to true; cycleTime, fraction_changed and time are left unset.

**OnSetProperty** (`0x00041c70`): a write to `time` (0x48) widens the incoming float
into the double and returns 0 so the generic copy is skipped. Everything else passes.

**Advance** (`0x00041c90`) reads the clock first, runs the time dependent Advance
(`0x0002f0d0`), and returns unless active. Then:

    time = now;
    if (cycleTime + cycleInterval <= now) {           // x87, double precision
        if (loop) cycleTime += cycleInterval;
        else { isActive = 0; this->OnIsActiveChanged(); }   // vtable +0x5c
    }
    f = (float)((now - startTime) / cycleInterval);
    fraction_changed = f - (float)(int)f;
    if (fraction_changed == 0.0 && startTime < now) fraction_changed = 1.0;
    CallFunction(this, L"fraction_changed", 0, 0);

The 1.0 case makes the final frame of a cycle report 1 instead of wrapping to 0.

**OnIsActiveChanged** (`0x00041d50`): turning on stamps `cycleTime = now`; turning off
calls the script's `OnActiveChanged`.

## Interpolators

Both keep `key` (a number array at 0x1c) and `keyValue` (count at 0x2c, data at
0x30). `key` is never read: lookups spread the input evenly over the keyValue entries.

**PositionInterpolator::Interpolate** (`0x0005d300`):

    k = (count - 1) * key; i = (int)floor(k); frac = k - floor(k);
    if (i < 0) return v[0];
    if (count - 1 <= i) return v[count - 1];
    if (0 < i && i < count - 2 && 3 < count)
        D3DXVec3CatmullRom(&out, &v[i-1], &v[i], &v[i+1], &v[i+2], frac);
    else
        out = (v[i+1] - v[i]) * frac + v[i];        // per component

So interior spans are Catmull-Rom splines and the two end spans are straight lines.

**OrientationInterpolator::OnLoad** (`0x0005d520`) turns each stored VRML axis+angle
into a quaternion in place with `D3DXQuaternionRotationAxis(axis, 0.0 - angle)`.
**Interpolate** (`0x0005d5a0`) uses the same index math and clamps, and slerps between
neighbors with `D3DXQuaternionSlerp`.

## Viewpoint

Fields: isBound 0x1c, fieldOfView 0x20 (0.785398 from `0x0002c028`), jump 0x24
(true), orientation 0x28 (0, 0, 1, 0), position 0x38 (0, 0, 10 from `0x0002c024`),
description 0x44. The bound viewpoint is the global at `0x0017e81c`; the projection
dirty flag is the byte at `0x0017e7f9`.

- **Destructor** (`0x0004f830`) frees the description and clears the global if it
  pointed here.
- **OnLoad** (`0x0004f860`) binds when nothing is bound yet, so the first viewpoint
  a scene loads takes the camera.
- **BindViewpoint** (`0x0004f880`) walks the node's class chain looking for the
  Viewpoint class object (`0x00187e88`) and binds on a match.
- **Bind** (`0x0004f8b0`): does nothing if already bound; otherwise unbinds the old
  one, takes the global, builds the camera rotation with
  `D3DXQuaternionRotationAxis(&orientation.xyz, 0.0 - orientation.w)`, flips the
  position's z, calls the camera's `Set` with 0 seconds for `jump` or 1.0 otherwise,
  and marks the projection dirty.
- **OnSetProperty** (`0x0004f940`): writing true to isBound binds; writing
  fieldOfView marks the projection dirty. Always returns 1.

## NavigationInfo

Fields: isBound 0x1c, avatarSize 0x20 (0.25, 1.6, 0.75), headlight 0x2c (true),
speed 0x30 (1.0), type 0x34, visibilityLimit 0x38, shape 0x3c. The bound one is the
global at `0x0017e820`.

- **Destructor** (`0x00050580`) frees type and clears the global if it pointed here.
- **OnLoad** (`0x000505b0`): if nothing is bound, takes the global, sets isBound and
  marks the projection dirty.
- **OnSetProperty** (`0x000505e0`): isBound written true sets it and marks dirty;
  avatarSize (0x20) and visibilityLimit (0x38) mark dirty. Returns 1.

Avatar size and visibility limit are what the projection code turns into near and far
planes.
