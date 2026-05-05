# mediaplayer

Theseus media player. libmpv-backed movie/TV browser + standalone fullscreen video playback. Replaces the dashboard's DVD player on the desktop build and ships in place of the XONLINE button on the main menu.

## What's in this directory

- `mediaplayer.xap` — wrapper script. Defines `GoToMedia()`, the three-scene state machine (browser, action menu, episode picker), and binds to the C++ `theMediaCollection` node. Gets injected into `default.xip` as a loose XAP alongside the other top-level dashboard scenes.
- `mediaplayer/` — extracted source for `mediaplayer.xip` (browser scene assets: meshes, textures, default.xap layer).
- `mediaplayer_action/` — extracted source for `mediaplayer_action.xip` (action-menu scene assets: cloned from `music_playedit2`).

The packed `.xip` archives live in `../../xboxfs/Q/Xips/` so runtime builds pick them up directly. Repacking is only needed if you edit the scene assets here.

## Backing C++ node

`CMediaCollection` in [`theseus/desktop/desktop_nodes.cpp`](../../theseus/desktop/desktop_nodes.cpp) owns the media database, scanning, TMDB enrichment, and playback handoff. The XAP wrapper queries it via FND-bound methods (`GetMovieCount`, `PlayMovie`, `SetSelectedShow`, etc.) and never reaches into libmpv directly.

`CDVDPlayer` is preserved as a no-op contract surface so XAPs that still DEF a DVDPlayer node compile clean.

## Configuration

Library roots and the optional TMDB API key live in [`xboxfs/C/UIX Configs/desktop.ini`](../../xboxfs/C/UIX%20Configs/desktop.ini):

```ini
[Library]
MusicRoot=/path/to/your/music
MoviesRoot=/path/to/your/movies
TvRoot=/path/to/your/tv
TMDBKey=
```

Empty `TMDBKey` disables online metadata enrichment; the catalog still works, posters/synopses just won't populate. Settings can also be edited in-app via the Settings window's Media Library tab.

## Repackaging

If you edit anything inside `mediaplayer/` or `mediaplayer_action/`, repack with `xiptool` (currently lives in the UIX Desktop tooling repo) and drop the resulting `.xip` files into `../../xboxfs/Q/Xips/`. A Python port of `xiptool` is on the roadmap.

`mediaplayer.xap` is loose-loaded at runtime in dev mode, so you can iterate on the wrapper script without repacking. Final ship form bakes it into `default.xip`.
