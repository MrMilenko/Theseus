# Camera System

Camera nodes confirmed in the 4920-5960 retail binary.

## Nodes

| Class | Address | FND String | Address |
|-------|---------|------------|---------|
| CCamera | 0x00028420 | "Camera" | 0x00028430 |
| CCameraPath | 0x000383d4 | "CameraPath" | 0x000383ec |

## CCamera

Scene camera with position and orientation interpolation. Uses quaternion slerp for smooth orientation transitions.

The "camera" global is accessible from XAP scripts for binding and controlling the active view.

## CCameraPath

Spline-based camera animation path. Drives the level transition fly-through sequences between dashboard panels.
