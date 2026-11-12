# Screen and Recovery

Two small nodes from the 5960 retail binary.

## Screen

A 0x44 byte node with no script functions; scenes only set its properties (PRD order: width,
height, fullScreen, showFramerate, title, icon, border, letterbox, wideScreen, brightness).

| Address | Name | What it does |
|---------|------|--------------|
| 0x0004f9c0 | CScreen::CScreen | 640 x 480, flags off, no strings, brightness 1; the first Screen becomes the app's screen (`0x0017e818`) |
| 0x0004fa50 | CScreen::~CScreen | Frees title, icon and border; clears the app's screen if it is this one |
| 0x0004fa90 | CScreen::OnSetProperty | width or height mark the size dirty, title the title; a brightness other than 1 drops any running lerp and starts a 2 second one instead of storing the value |
| 0x0004fb20 | CScreen::Advance | Clears both dirty flags; a changed brightness goes to the gamma ramp |
| 0x0004fb70 | (set brightness) | Builds a ramp of `(BYTE)(i * brightness)` for all three channels and stores it into the device's pending ramp slot when it differs (an inlined `SetGammaRamp`) |

Theseus adds TakeScreenShot, WireFrameStart/Stop and MotionStart/Stop as script functions.

## Recovery

| Address | Name | What it does |
|---------|------|--------------|
| 0x0003ba60 | CRecovery::CreateNode | 0x2c bytes, everything zero |
| 0x0003bb20 | StartRecovery | Clears done, sets running, syncs the last percentage and starts the restore thread; if the thread can't start, progress becomes 1 and done is set |
| 0x0003bb70 | CRecovery::Advance | While running: done stops it and calls OnRecoveryComplete; a new percentage calls OnRecoveryProgressChanged, sets `recoveryProgress` to percent x 0.01 and resets the screen saver |
| 0x0002dc50 | FinishRecovery | `HalReturnToFirmware(1)` |

Theseus has no disk restore to run, so StartRecovery always takes the finished path at 100%, and
desktop restarts the dashboard instead of rebooting.
