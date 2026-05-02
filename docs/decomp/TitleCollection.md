# TitleCollection

Title and saved game scanner. Enumerates installed titles and their save data from the Xbox HDD. Backed by `title_collection.cpp` in the active source tree.

## CTitleArray

The primary class. Scans title directories on the Xbox hard drive, reading metadata from the standard Xbox title files:

- **TitleMeta.xbx**: title name and publisher info
- **TitleImage.xbx**: title icon / thumbnail image
- **SaveMeta.xbx**: individual saved game name and timestamp
- **SaveImage.xbx**: saved game icon

Each title entry maintains a sub-array of its saved games. The dashboard uses this to populate the Memory management screens.

## Features

- **Sorted enumeration**: Titles can be retrieved in alphabetical order for display.
- **Block size calculation**: Computes the storage size of each title and save in blocks (the Xbox's user-facing unit of HDD space, 16KB per block).
- **Title image lookup**: Retrieves the icon texture for a given title ID.
- **Background update**: Directory scanning can run on a background thread with critical section protection around the title/save arrays. This keeps the UI responsive while the HDD is being enumerated.

## Integration

Not a node class. Used directly by `SavedGameGrid` and `CopyGames` to provide data for the memory management and file copy UI screens.
