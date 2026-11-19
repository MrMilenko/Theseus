# Config

The `Config` node in the 5960 retail binary (`0x0003a430..0x0003b9e0`, FND table at `0x0017fdf0`).
Nearly everything is one EEPROM DWORD read with `XQueryValue` and written back with `XSetValue`;
5960 never checks either result.

| Setting | Index | Bits used |
|---------|-------|-----------|
| Language | 7 (`XC_LANGUAGE`) | whole value; 0 or past the last language reads as 1 (English) |
| Video flags | 8 | widescreen 0x10000, 720p 0x20000, 1080i 0x40000, 480p 0x80000, letterbox 0x100000, PAL60 0x400000 |
| Audio flags | 9 | low word speakers (0 stereo, 1 mono, 2 surround), Dolby Digital 0x10000, DTS 0x20000 |
| Game rating | 10 | stored as 6 - level |
| Pass code | 11 | one nibble per pad press, below |
| Movie rating | 12 | stored as 7 - level |
| Misc flags | 0x11 | don't use DST 2, hide Live today 4, legal notice accepted 8 |

## Notes per function

- `GetVideoMode` (`0x0003a860`): letterbox wins (1), else widescreen (2), else 0.
  `SetVideoMode` (`0x0003a8a0`) clears both, sets the chosen one and, if widescreen changed, flips the
  widescreen present flag, zeroes D3D's cached AV mode and resets the device; it always marks the
  projection dirty.
- `Set480Support` (`0x0003a980`): stores the bit; while a DVD plays on an HDTV pack with the 480p
  Macrovision hardware flag it also switches the display between progressive and interlaced.
- `GetAudioMode` / `SetAudioMode` (`0x0003abb0`, `0x0003abf0`): script modes 0 mono, 1 stereo,
  2 surround; the encodings in the high word stay.
- `EncodePassword` (`0x0003b0a0`): `code = code * 16 + n` per character with u1 d2 l3 r4 a5 b6 x7 y8
  B9 W10 L11 R12, anything else 0.
- `GetTimeZone` (`0x0003ae80`): with no zone set the default is 11, 60 or 25 by game region (North
  America, Japan, elsewhere). `GetDSTAllowed` / `GetDST` need both transition months set.
- `GetLaunchReason` (`0x0003b2d0`): names by reason through the jump table at `0x0003b3ac`:
  1 Error, 2 Memory, 3 Settings, 4 Music, 12 FromOnlineDash, 13 FriendsInternal, 14 VoiceChatInternal,
  15 MessagesInternal, 16 AccountManagementInternal, 17 NewAccountInternal, 18 AccountRecoveryInternal,
  19 NetworkSettingsInternal; anything else, or no launch data, gives an empty string.
- `ForceSetLanguage` / `ForceSetTimeZone` / `ForceSetClock` (`0x0003a430..0x0003a470`): reason 3 and
  bit 4, 2 or 1 of the first launch parameter.
- `BackToLauncher` (`0x0003b4b0`): a forced language, zone or clock reboots; otherwise
  `default.xbe` on `\Device\CdRom0` relaunches with the launch context.
- `BackToLauncher2` (`0x0003b550`): `settings_adoc.xip`, title 0xFFFE0000, context 'ADOC', from
  wherever the dash's own drive link points.
- `GetFontVersion` (`0x0003a520`): the top byte of the font file's first DWORD as a digit, cached in
  the node (+0x1c), -1 when there is no font file.
- `GetROMVersion` / `GetXdashVersion` (`0x0003b7b0`, `0x0003b860`): `%d.%02d.%d.%02d`; the dash
  prints 1, 0, 5960, 1.
- `GetEthernetLinkStatus` (`0x0003b9d0`): `XNetGetEthernetLinkStatus() & 1`.

Theseus differs on purpose in `NtFileExists` / `FileExists` (drive letter mapping instead of
`NtOpenFile`), `GetRecoveryKey` (empty), `GoToXOnlineDash` (logs only), `ToggleNoisyCamera` (skin
setting instead of `T:\NoisyCamera`), `GetFontVersion` (system fonts per language), and adds the
SMBus, power, network, skin, modchip and XBE title id functions.
