# Audio System

DirectSound-based audio pipeline used by the 4920-5960 retail dashboard. Three layers: a static buffer wrapper, a streaming pump base class with a worker thread, and the XAP nodes that the scene graph interacts with.

## Class Hierarchy

```
CAudioBuf           : static DSound buffer (sound effects, short clips)
  CAudioPump        : streaming buffer base, worker thread + segment notify
    CFilePump       : WAV file streaming
```

`CAudioClip` (XAP node) holds a `CAudioBuf*`: which may actually point to a `CFilePump`: and dispatches `Play/Pause/Stop/SetAttenuation/...` through the virtual interface.

## CAudioBuf

Wraps a single `IDirectSoundBuffer8` allocated through `DSoundManager::DSoundCreateSoundBuffer` with `DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY`.

| Method | Purpose |
|---|---|
| `Initialize(WAVEFORMATEX*, int nBufferBytes, void* pvSamples=NULL)` | Create the buffer; if samples provided, copy them in via Lock/Unlock |
| `Lock()/Unlock(void*)` | Standard DSound locking for direct PCM writes |
| `Play(bLoop)` | Start playback (looping or one-shot) |
| `Stop/Pause/IsPaused/IsPlaying` | Transport control |
| `SetAttenuation(float dB)` | 0..100 dB attenuation (mapped to DSound millibels) |
| `SetPan(float)` / `SetFrequency(float)` | DSound pan/frequency control |
| `GetPlaybackTime()` / `GetPlaybackLength()` | Position in seconds (length returns 0 if unknown) |

Buffer format is captured at Initialize as `m_nBytesPerSecond` for time conversion.

## CAudioPump

Streaming buffer for content too large to load in full. Inherits from `CAudioBuf` and replaces the static buffer with a circular DSound buffer divided into N segments, refilled by a worker thread.

### State Machine

```
PUMPSTATE_STOPPED   -> idle
PUMPSTATE_BUFFERING -> filling initial segments before play
PUMPSTATE_RUNNING   -> normal playback, worker refills consumed segments
PUMPSTATE_STOPPING  -> draining; worker zeros remaining segments and exits
```

### Initialization

`CAudioPump::Initialize(DWORD dwStackSize, WAVEFORMATEX*, int nBufferBytes, int nSegmentsPerBuffer=4, int nPrebufferSegments=1)`

1. Allocates `m_nSegmentsPerBuffer` Win32 events (`m_ahNotify[]`).
2. Creates terminate event, run event, and a mutex.
3. Spawns the worker thread with `dwStackSize` bytes via `CreateThread(StartThread, this)`.
4. Creates a DSound buffer of size `nSegmentsPerBuffer * nBufferBytes` with `DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY`.
5. Sets `DSBPOSITIONNOTIFY` entries so each segment boundary signals one of the `m_ahNotify` events.

### Worker Loop

`ThreadProc()` waits on the segment-completion events. When a segment finishes playing, the worker calls `FillBuffer(nBuffer)`:

1. Lock the DSound buffer at `nBuffer * m_nBufferBytes`.
2. Call the pure virtual `GetData(BYTE*, int cbBuffer)` to fill the segment.
3. If `GetData` returned fewer bytes than requested, transition to `PUMPSTATE_STOPPING` and zero the remainder.
4. Unlock and bookkeep `m_nFilledBuffers` / `m_nCompletedBuffers`.
5. Once `m_nFilledBuffers >= m_nSegmentsPerBuffer` during BUFFERING, transition to RUNNING.

### Subclass Contract

Subclasses must implement `int GetData(BYTE* pbBuffer, int cbBuffer)` to source PCM. Optionally override `OnAudioEnd()`. The worker thread calls `GetData`: thread-safety of the source is the subclass's responsibility.

### Stack Sizing

The worker thread stack size is per-pump. Decoder-backed pumps must size for their decoder's scratch usage on top of standard ~4 KB Win32 thread overhead. Pumps that just memcpy from a file handle (CFilePump) work with 8 KB.

## CFilePump

Streams a WAV file directly off disk. Holds a `HANDLE m_hFile`, a start position (data chunk offset), and a buffer-size that is `0x2000` rounded to the format's block alignment.

| Method | Behavior |
|---|---|
| `Initialize(HANDLE hFile, int nFileBytes, WAVEFORMATEX*)` | Records file/length, calls `CAudioPump::Initialize(8192, ...)` |
| `Stop()` | Repositions file pointer back to data chunk start |
| `GetData(pbBuffer, cbBuffer)` | `ReadFile` directly into the locked DSound segment |
| `GetPlaybackLength()` | Returns total file bytes / nAvgBytesPerSec |

The file handle is owned by the pump after Initialize and closed in the destructor.

## CAudioClip Node

`AudioClip` at `IMPLEMENT_NODE("AudioClip", CAudioClip, CTimeDepNode)`. Subclass of `CTimeDepNode` so the scene graph drives `Advance(float seconds)` every frame.

### Properties

| Name | Type |
|---|---|
| volume | number |
| pan | number |
| frequency | number |
| fade | number |
| url | string |
| transportMode | integer (0=stop, 1=play, 2=pause) |
| removeVoice | boolean |
| sendProgress | boolean |
| pause_on_moving | boolean |
| progress | number |

### Functions

`Play`, `Stop`, `Pause`, `PlayOrPause`, `getMinutes`, `getSeconds`.

### URL Schemes

`CAudioClip::Initialize` parses `m_url` and dispatches:

| Prefix | Source |
|---|---|
| `cd:` | CD audio playback (CD player UI: DVD/CD playback path) |
| `st:` | Soundtrack from `MusicCollection` (song ID after the prefix) |
| (file extension) | Local file: `.wav` -> `OpenWaveFile()` |

`OpenWaveFile()` parses the RIFF/WAVE header (`fmt ` chunk -> `m_format`, `data` chunk -> `dwDataSize`). If the data payload is over 64 KB it instantiates a `CFilePump`; otherwise a static `CAudioBuf` and the entire payload is locked/copied/unlocked.

### Lifecycle

- `OnSetProperty` flips `m_bDirty` when `url` changes; the next `Advance` re-runs `Initialize`.
- `volume` and `pan` setters with non-zero `fade` interpose a `CLerper` to ramp the value over `fade` seconds.
- `OnIsActiveChanged` calls `Play()` / `Stop()` based on `m_isActive`.
- `Advance` propagates `m_volume`/`m_pan`/`m_frequency` to the underlying buffer, polls `IsPaused/IsPlaying`, and writes `progress` if `sendProgress` is set.

### Format Storage

`m_format` is `XBOXADPCMWAVEFORMAT` (DSound's superset of `WAVEFORMATEX` to carry XBADPCM coefficients). The format tag is asserted to be `WAVE_FORMAT_PCM` or `WAVE_FORMAT_XBOX_ADPCM`.

## CMusicCollection Node

`MusicCollection` at `IMPLEMENT_NODE("MusicCollection", CMusicCollection, CNode)`. Used by the music UI to enumerate soundtracks and songs, and to drive copy/edit operations against the system soundtrack store.

### Properties

| Name | Type |
|---|---|
| copyProgress | number |
| error | integer |

### Functions

Read side: `GetSoundtrackCount`, `GetSoundtrackID`, `GetSoundtrackIndexFromID`, `GetSoundtrackName`, `FormatSoundtrackTime`, `GetSoundtrackSongCount`, `GetSoundtrackSongID`, `GetSoundtrackSongName`, `FormatSoundtrackSongTime`.

Edit side: `AddSoundtrack`, `DeleteSoundtrack`, `ClearCopyList`, `AddSongToCopyList`, `StartCopy`, `SetSongName`, `SetSoundtrackName`, `MoveSongUp`, `MoveSongDown`, `DeleteSong`, `CreateSoundtrackName`, `GetUpdateString`.

Songs returned by these functions are referenced by ID. The `st:<id>` URL scheme on `CAudioClip` resolves the ID back to a playable source through the collection.

## Pump vs Static Buffer Decision

`OpenWaveFile` chooses based on the data chunk size:

- **<= 64 KB (`0x10000`)**: static `CAudioBuf`. Whole payload memcpy'd into a single DSound buffer at load time. Used for sound effects.
- **> 64 KB**: `CFilePump`. Streamed off disk. Used for music and long ambient loops.

The threshold balances DSound buffer allocation cost (static is cheaper but holds RAM) against worker thread overhead (the pump needs an event-driven thread per buffer).

## DSound Mixing Headroom

`CAudioBuf::SetAttenuation` writes attenuation in dB rather than linear. The attenuation is offset against the headroom configured by `DSoundManager` (1200 millibels by default) to leave room for mixed signal peaks. See `DSoundManager.md`.
