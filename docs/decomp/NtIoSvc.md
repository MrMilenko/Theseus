# NtIoSvc

CD-ROM device I/O service. Wraps NT `DeviceIoControl` calls for CDROM access on Xbox. Backed by `ntiosvc.cpp` in the active source tree.

## CNtIoctlCdromService

The main class. Opens a handle to `CDROM0:` (the Xbox DVD drive's device path) and provides methods to query disc information.

### TOC Reading

Reads the disc's table of contents via `IOCTL_CDROM_READ_TOC`. The TOC contains track start addresses in MSF (Minutes/Seconds/Frames) format, which are converted to absolute frame offsets for seeking and playback calculations.

## XCDROM_TOC

Structure storing parsed disc information:

- **Track addresses**: Array of start frame offsets for each track on the disc.
- **Disc ID**: A hex string computed from the track frame offsets. This serves as a unique-enough identifier for matching discs against metadata databases (title, artist, track names).
- **Metadata fields**: Optional title, artist, and per-track name strings. Populated from external lookup if available; empty otherwise.

## Usage

Used by the disc-management and music-collection layers for audio CD detection and playback. When a disc is inserted, the service reads the TOC, computes the disc ID, and hands the result to the music system for track enumeration.
