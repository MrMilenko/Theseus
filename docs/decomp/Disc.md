# Disc Drive

The `DiscDrive` node and disc type detection in the 5960 retail binary. The boot disc type lives
at `0x0017aee4`; the node's type names are the table at `0x0017453c` (`none`, `unknown`, `Title`,
`Audio`, `Video`).

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x00039d70 | CDiscDrive::CDiscDrive | Joins the drive list, takes the starting type and tray state, below |
| 0x00039ec0 | CDiscDrive::~CDiscDrive | Leaves the list (freeing it when empty), frees the type string |
| 0x00039f20 | CDiscDrive::Advance | Deferred event, then a tray poll every 0.04 s, below |
| 0x0003a030 | CDiscDrive::getTrackCount | Audio track count when the boot type is Audio, else 0 |
| 0x0003a060 | CDiscDrive::FormatTotalTime | Disc length as `%02d:%02d` (see NtIoSvc.md) |
| 0x0003a150 | CDiscDrive::FormatTrackTime | One track's length the same way |
| 0x0003a260 | CDiscDrive::LaunchDisc | Keeps the display up; a game disc reboots the console, anything else `XLaunchNewImage(NULL)` |
| 0x0003a2b0 | GetDiscType | Below |

## Constructor

```
if (g_bHasLaunchData && reason == XLD_LAUNCH_DASHBOARD_DVDPLAYER && DoesFileExist("CDROM0:\video_ts\video_ts.ifo"))
    g_BootType = DISC_DVD;
if ((g_bHasLaunchData && reason != 0x0C) || g_BootType == DISC_DVD || g_BootType == DISC_BAD)
    { m_discKind = g_BootType; HalReadSMCTrayState(&m_trayState, 0); }
else
    { m_discKind = DISC_NONE; m_trayState = 0x40; }      // the first poll reports the disc
```

Reason `0x0C` is a return from the online dashboard.

## Advance

```
if (m_bDeferNotification && !m_locked) { m_bDeferNotification = false; fire inserted or removed; }
if (now - lastPoll < 0.04) return;                // 0x0002c1c8, one clock for every drive
lastPoll = now;
if (HalReadSMCTrayState(&tray, 0) < 0 || tray == m_trayState) return;
m_trayState = tray; ResetScreenSaver();
g_BootType = GetDiscType();
if (g_BootType == m_discKind) return;
m_discKind = g_BootType; replace discType string;
if (m_locked) m_bDeferNotification = true; else fire OnDiscInserted (type != none) or OnDiscRemoved;
```

## GetDiscType

```
close the CD service; IoDismountVolumeByName("\??\CdRom0:");
if (HalReadSMCTrayState(&tray, 0) >= 0 && tray != 0x60 /* still detecting */) {
    if (tray != 0x40 /* no media */) return DISC_NONE;
    h = CreateFile("cdrom0:"); if (h == -1) return DISC_NONE;
    bad = DeviceIoControl(h, IOCTL_CDROM_CHECK_VERIFY) || GetLastError() == ERROR_UNRECOGNIZED_MEDIA;
    CloseHandle(h); return bad ? DISC_BAD : DISC_NONE;
}
open the CD service; if it found audio tracks return DISC_AUDIO;
type = DISC_BAD;
for each of default.xbe (Title), video_ts\video_ts.ifo (Video), track01.cda (Audio) on CDROM0:
    if it exists return its type;
    an error other than file or path not found retries the same file once after Sleep(100);
if (HalReadSMCTrayState(&tray, 0) >= 0 && tray != 0x60) type = DISC_NONE;    // gone meanwhile
return type;
```

Theseus launches a game disc's `default.xbe` directly (after its Discord relay) instead of
rebooting, and adds `OpenTray`, `CloseTray` and the CD name lookups.
