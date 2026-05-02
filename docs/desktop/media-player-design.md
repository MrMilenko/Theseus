# Media Player (libmpv + DVD XAP)

The dashboard's original Xbox DVD player UI (`dvd.xap`) is wired to libmpv for file-based media playback on desktop. Video renders through the XAP scene's `texture_panel` mesh while the OSD overlay shows playback info using the same script that ran on the Xbox.

## How Xbox DVD Playback Worked

### XAP Script Flow

1. `theDiscDrive` (DiscDrive node) has property `discType` (string)
2. When `discType` changes, XAP callbacks fire:
   - `OnDiscInserted()` checks `discType`: "Audio", "Video", "Title", "unknown"
   - `OnDiscRemoved()` stops playback, returns to launcher
3. For "Video": calls `StartDVDPlayer()` which sets `theDVDPlayerInline.visible = true`
4. This loads `dvd.xap` via Inline node (lazy load)
5. dvd.xap's `onLoad()` calls `theDVDLevel.GoTo()` to transition to the player scene
6. Script calls `theDVDPlayer.play()`, `.stop()`, `.pause()`, etc. on the DVDPlayer node

On the original Xbox, video frames came from a hardware MPEG-2 decoder that wrote to a D3D overlay surface. The `texture_panel` mesh was a black placeholder that the decoder composited over. On desktop, libmpv renders frames to an OpenGL FBO texture that gets bound to the `texture_panel` material during rendering.

### dvd.xap Scene Structure

Pure 3D scene with these key elements:
- `texture_panel`: flat mesh with material "Black80", no ImageTexture. This is where the video frame appears.
- `text_panel`: flat mesh for time/chapter text overlay
- `theText`: Text node for elapsed/chapter display
- `DVD_buttons` group with 6 button meshes (rewind, play, stop, fast-forward, prev chapter, next chapter)
- `dvd_shell`: panel frame meshes
- `icons` group: transport control icon meshes (play/pause/stop symbols as geometry)

## Desktop Implementation

### Architecture

```
File > Open Media
       |
       v
MediaPlayer_Open(path)  -->  mpv_command("loadfile", path)
       |
       v
Set theDiscDrive.discType = "Video"
       |
       v
XAP reacts: dvd.xap loads, DVDPlayer.play() called
       |
       v
Each frame:
  MediaPlayer_Update()  -->  mpv_render_context_render() to FBO
  CDVDPlayer::Advance() -->  reads mpv position/chapter/track info
                             updates XAP properties (hours, minutes, etc.)
  D3D8 draw call        -->  detects "Black80" material on texture_panel
                             binds video FBO texture instead of black
```

Three files handle the integration:

- **media_player.h/.cpp**: libmpv wrapper. Creates an mpv instance with OpenGL render context, renders video frames to an FBO, exposes transport controls (play/pause/stop/seek), chapter navigation, audio/subtitle track cycling, zoom/pan, A-B looping, frame stepping, and playback speed. The FBO texture ID is returned via `MediaPlayer_GetVideoTexture()` for the renderer to bind.

- **CDVDPlayer** (desktop_nodes.cpp): The XAP node implementation. Bridges all 34 DVDPlayer script functions to `MediaPlayer_*` calls. The `Advance()` method runs every frame, querying mpv for current position, chapter, track info, and updating the node's properties so the XAP OSD text stays current.

- **dvd.xap** (original Xbox script): Defines the joystick handlers that map controller buttons to DVDPlayer functions, the OSD layout, and the menu overlay. Unmodified from the Xbox version.

### Video Rendering

The video texture injection happens in the D3D8 translation layer. When the renderer encounters the `texture_panel` mesh (identified by its "Black80" material), it checks if `MediaPlayer_GetVideoTexture()` returns a valid GL texture. If so, it binds that texture instead of the material's default. When playback stops, the override is removed and the mesh goes back to black.

The CRT post-processing shader works on top of this: scanlines and curvature are applied to the final composited frame including video, giving media playback the same retro look as the rest of the dashboard.

### Playback Modes

The DVDPlayer node's `playbackMode` property maps to these states (from the Xbox DVD navigator):

| Mode | Value | Meaning |
|------|-------|---------|
| DPM_STOPPED | 0 | Stopped / no media |
| DPM_PAUSED | 1 | Paused |
| DPM_STILL | 2 | Still frame |
| DPM_PLAYING | 3 | Normal playback |
| DPM_NONE | 4 | No mode |
| DPM_SCANNING | 5 | Fast forward / rewind |
| DPM_TRICKPLAY | 8 | Frame stepping |

### Scan Speed Ramp

Forward/backward scan cycles through speed multipliers on each press:
- **Playing + forward scan**: 2x, 4x, 8x, 16x, 32x (via mpv `speed` property)
- **Paused + forward scan**: Slow motion 1/2x, 1/4x, 1/8x, 1/16x
- **Backward scan**: Simulated via periodic reverse seeking in Advance() (mpv doesn't support native reverse playback)

### Track Info Mapping

| Xbox property | mpv source |
|---------------|------------|
| audioFormat | `audio-codec-name` mapped to enum (0=AC3, 1=MPEG1, 2=MPEG2, 3=PCM/Stereo, 4=DTS) |
| audioChannels | `audio-params/channel-count` |
| audioLanguage | `current-tracks/audio/lang` |
| subTitleLanguage | `current-tracks/sub/lang` |
| subTitle | `sid` (subtitle track index) |

### Function Mapping

| Xbox function | mpv equivalent |
|---------------|---------------|
| play/pause/stop | `pause` property, `stop` command |
| forwardScan/backwardScan | `speed` property (forward), periodic seeking (backward) |
| stopScan | Reset `speed` to 1.0 |
| frameAdvance/frameReverse | `frame-step` / `frame-back-step` commands |
| nextChapter/prevChapter | `add chapter +/-1` |
| nextAudioStream | `cycle audio` |
| nextSubtitle | `cycle sub` |
| setScale | `video-zoom` (log2 scale) |
| setZoomPos | `video-pan-x` / `video-pan-y` |
| abRepeat | `ab-loop-a` / `ab-loop-b` properties |

Functions that are no-ops for file playback: menu, titleMenu, selectUp/Down/Left/Right, activate, goUp, nextAngle, enableWideScreen, disableWideScreen, refreshAudioSettings.

### Audio

mpv has its own audio output (CoreAudio on macOS, ALSA/PulseAudio on Linux, WASAPI on Windows). Dashboard audio uses SDL_mixer separately. When media playback starts, dashboard background music is muted via `DashAudio_MuteAll()` and restored on stop via `DashAudio_UnmuteAll()`.
