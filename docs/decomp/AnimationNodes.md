# Animation & Viewport Nodes

Animation and viewport nodes confirmed in the 4920-5960 retail binary.

## Nodes

| Class | Address | FND String | Address |
|-------|---------|------------|---------|
| CTimeSensor | 0x000260fc | (none) | (none) |
| CPositionInterpolator | 0x0002a04c | "PositionInterpolator" | 0x0002a078 |
| COrientationInterpolator | 0x00029fcc | "OrientationInterpolator" | 0x0002a000 |
| CViewpoint | 0x00027fd4 | (none) | (none) |
| CNavigationInfo | 0x000282b0 | (none) | (none) |

## CTimeSensor

Time-driven animation trigger. Properties: startTime, stopTime, loop, cycleInterval.

## CPositionInterpolator

Keyframe-based vec3 interpolation. Drives position animations between key/keyValue pairs.

## COrientationInterpolator

Keyframe-based quaternion slerp. Drives orientation animations between key/keyValue pairs.

## CViewpoint

Camera position, orientation, and fieldOfView binding.

## CNavigationInfo

Avatar size, speed, and visibility limit parameters.

