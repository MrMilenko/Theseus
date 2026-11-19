# NtIoSvc

The CD-ROM service in the 5960 retail binary. One global instance lives at `0x00188780`: the drive
handle, then the audio track count (`0x00188784`), then the start frame of every track
(`0x00188788`). Audio CD detection, the disc's track times and the CD audio streamer all read it.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x00064da0 | CNtIoctlCdromService::CNtIoctlCdromService | Handle -1, no tracks |
| 0x00064db0 | CNtIoctlCdromService::Close | Closes an open handle, sets it to -1, zeroes the track count |
| 0x00064dd0 | CNtIoctlCdromService::Open | Opens `CDROM0:` for reading; a failed open returns the error as an HRESULT; a disc with no usable table of contents closes it again but still returns 0 |
| 0x00064f20 | CNtIoctlCdromService::GetTableOfContents | Reads the TOC and fills the track list, below |
| 0x000650c0 | CNtIoctlCdromService::Read | Raw CDDA read with retries, below |

## GetTableOfContents

```
if (!DeviceIoControl(h, IOCTL_CDROM_READ_TOC, 0, 0, &toc, 0x324, &n, 0)) {
    hr = HRESULT_FROM_WIN32(GetLastError());
    if (hr < 0) return false;            // ERROR_NOT_READY gets one try, same result
}
if (toc.LastTrack < toc.FirstTrack) return false;
i = 0;
do {
    TrackAddr[i] = (Address[1] * 60 + Address[2]) * 75 + Address[3];
    if (TrackNumber == 0xAA) break;      // lead out
    if (Control & 4) {                   // data track
        Address[1] -= 2; Address[2] += 30;
        if (Address[2] < 60) Address[1] -= 1; else Address[2] -= 60;
        TrackAddr[i] = (recomputed); break;
    }
} while (++i <= LastTrack - FirstTrack + 1);    // i stays at the last entry read
LastTrack = i;
return i > 0;
```

A data track (an enhanced CD) is moved back two and a half minutes, the gap the drive leaves before
the second session, so the audio track before it ends where its sound does.

## Read

```
rri.DiskOffset = (start - 150) * 2048;  rri.SectorCount = count;  rri.TrackMode = CDDA;
for (;;) {
    if (DeviceIoControl(h, IOCTL_CDROM_RAW_READ, &rri, 0x10, buf, count * 2352, &n, 0)) return 0;
    hr = HRESULT_FROM_WIN32(GetLastError());
    if (hr >= 0) break;
    if (retries-- == 0) return hr;
}
return hr;
```

## Track times

The disc drive node's `FormatTotalTime` (`0x0003a060`) and `FormatTrackTime` (`0x0003a150`) read the
list directly: the length is `TrackAddr[LastTrack] - TrackAddr[0]` (or `[n + 1] - [n]` for a track
inside `0..LastTrack-1`), printed `%02d:%02d` as `(BYTE)(frames / 4500)` and `frames % 4500 / 75`.
With no disc open, or a track out of range, they return an empty string.

Theseus adds a disc id (each track's start frame as six hex digits) and title, artist and track name
slots that nothing fills yet.
