# App Shell

Start up and the frame loop of the 5960 retail binary. The app object lives at `0x0017e7a0`.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x0002c520 | Alert | Formats into a 512 character buffer; retail drops the text |
| 0x0002c570 | app constructor | Frame clock from `GetTickCount`, root class, launch data (below) |
| 0x0002c660 | XboxInitRenderState | Startup render states one by one, motion blur factor 1 |
| 0x0002ccf0 | InitD3D | Present parameters by AV pack, see the video notes; creates the device |
| 0x0002d0e0 | Draw | Below |
| 0x0002d7e0 | InitApp | Below |
| 0x0002d930 | CleanupApp | XOnline cleanup, matrix stack, scene, class, `Class_Exit`, XIP sections, `Text_Exit`, DirectSound, app dir |
| 0x0002da00 | AppShell_Run | `InitApp`, then forever: clock, keyboard poll, camera, lerpers, scene advance, deferred deletes, `Draw` |
| 0x0002dbc0 | GetStartupClassFile | `y:\default.xap`, current dir `y:/default.xap` |
| 0x0002dc80 | main | Crash handler unless the hardware is a debug kit, then `AppShell_Run` |
| 0x0002dcc0 | XboxFormatMemoryUnit | Device object into a 32 byte name, `XapiFormatFATVolume`, close |
| 0x0002dd20 | Xbox_Init | Below |
| 0x00062750 | DSound_Init | Below |

## Launch data

When `*LaunchDataPage` is set and its type is 1 (`LDT_LAUNCH_DASHBOARD`), the 3 KB of launch data
are copied into the app (title id, reason, context, two parameters), the page pointer is cleared and
the page freed with `MmFreeContiguousMemory`.

## InitApp and Xbox_Init

```
_set_new_handler(...); open the CD service; g_BootType = GetDiscType();
Xbox_Init():
    PAL-I or PAL-M: clear the 480p video flag
    XSetFileCacheSize(1 MB); drive links; xboxdashdata.<id> or the default data dir
    create C:\TDATA\fffe0000\music; T:\NoisyCamera; keyboard languages
    XInitDevices(0, 0); wait while XGetDeviceEnumerationStatus() is busy; open keyboards
    delete t:\tempcda.cda and t:\tempwma.wma; remember the game region
srand(GetTickCount()); version lock with the online dash; XOnline start up (may reboot into it)
InitD3D(); LoadXIP; Class_Init; title root; Material_Init;
load y:\default.xap, create it, D3DXCreateMatrixStack, call its initialize
```

DirectSound is not touched here. `DSound_Init` runs the first time a sound buffer is asked for:

```
if (g_BootType == DISC_DVD) return false;
speaker override = (XGetAudioFlags() low word == mono);
DirectSoundCreate(0, &g_pDirectSound, 0);
SetMixBinHeadroom(bin, 0) for all 31 mix bins;
CreateThread(worker): while (g_pDirectSound) { DirectSoundDoWork(); Sleep(100); }
```

## Draw

Projection when dirty (field of view, aspect, near and far from the bound viewpoint and navigation
info). The sky color (or dark blue `0xff00004c`) clears the frame, and a background's backdrop
draws over it; while a transition blurs (factor under 1, sky black, no backdrop, past the first two
frames) a black quad with that alpha fades the last frame instead. A default material (grey, made
once), the scene from an identity world, then Present.
