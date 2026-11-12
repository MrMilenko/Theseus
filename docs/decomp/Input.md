# Joystick Input

The `Joystick` node in the 5960 retail binary: it reads the pads (and the IR remote) and turns them
into script events. Only the bound joystick (`0x0017e73c`, with the previous one at `0x0017e740`)
raises events.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x00040620 | CJoystick::CJoystick | 20 polls a second (`0x0002bf04`), remote and gamepad enabled, waiting for its first poll; binds itself when no joystick is bound |
| 0x00040750 | CJoystick::~CJoystick | Drops itself from the previous binding, hands the binding back if it held it, frees the secret key |
| 0x00040790 | BindJoystick | NULL clears both bindings; a Joystick node (class `0x00181f80`) is bound |
| 0x000407f0 | CJoystick::Bind | Current binding becomes the previous one; once polled before, rereads the devices |
| 0x00040820 | CJoystick::OnSetProperty | `isBound`, `secretKey`, `enableSecretKey`, and storing `eatSecretKey`, `enableGamepad`, `enableRemote` itself |
| 0x00040b70 | EnableGlobalInput | Sets the global input switch (`0x0017c058`) |
| 0x00041040 | CJoystick::Advance | The per frame poll, below |
| 0x00041790 | CheckSecretKeySequence | Steps the secret key with one letter, returning begin (1), verified (2) and end (4) flags |
| 0x00041800 | ProcessSecretKeySequence | OnKeyVerificationEnter, OnKeyVerificationExit, OnKeyVerified for the flags set |

## Advance

- A remote that went away more than its grace time ago raises OnRemoveRemote once.
- Each port's state is read. The IR remote's codes become their own events (repeating every other
  report after the 8th of a held key). Pads are merged.
- A change while the screen saver is showing only wakes it.
- Stick values are `raw / 32767`, zeroed between -0.9 and 0.9. When the left stick rests the d-pad
  sets the axes instead.
- A held direction fires OnMoveLeft/OnMoveRight and then OnMoveDown/OnMoveUp (secret key letters
  L, R, D, U) at once, after 0.3 s (`0x0002c0b8`), then every 0.12 s (`0x0002c1f4`). Coming back to
  rest fires OnMoveCenter.
- Each analog button crossing 0x20 fires its down or up event (tables at `0x00176640` and
  `0x00176620`, secret key letters `ABXYbwlr`). Each digital button change fires its event (tables
  at `0x00176600` and `0x001765e0`, letters at `0x000259a0`: none for the d-pad, then S, P, Q, W).
  Start and Back raise OnADown/OnBDown and OnAUp/OnBUp.
- A typed USB keyboard key raises OnTIDKeyDown.
- Start with A and B down raises OnReset.
- Holding the progressive scan combo toggles progressive scan once (see the video path notes).

Every event goes through the binding and `enableGamepad` checks (`enableRemote` for remote events).

Theseus's desktop node reads an SDL pad and the keyboard as one player, adds media keys, has no
remote or progressive scan combo, accepts pad events even with `enableGamepad` off, and fires
OnMoveCenter after the button events rather than before them.
