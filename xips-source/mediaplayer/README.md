# mediaplayer

The media library: a movie and TV browser backed by libmpv, styled like the dashboard's own memory and music screens. On the desktop build it takes the place of the Xbox Live button on the main menu.

## What's here

- `mediaplayer.xap`: the wrapper script. Defines `GoToMedia()`, runs the three scene flow (browser, action menu, seasons and episodes), and talks to the `theMediaCollection` node. The desktop `default.xip` carries its own copy (`default-desktop/MediaPlayer.xap`).
- `mediaplayer/`: source for `mediaplayer.xip`, the browser scene (meshes, textures, its `default.xap`).
- `mediaplayer_action/`: source for `mediaplayer_action.xip`, the action menu scene, built from the music screen's `music_playedit2`.

The packed archives are in [`Data/Xips/`](../../Data/Xips/).

## The node behind it

`CMediaCollection` in [`theseus/desktop/media_db.cpp`](../../theseus/desktop/media_db.cpp) owns the library: scanning, the `Library/MediaDB.cache` cache, TMDB lookups and the handoff to fullscreen playback. The scripts query it through node functions (`GetMovieCount`, `GetShowTitle`, `SetSelectedShow`, `PlayMovie`, `PlayEpisode` and so on) and never touch libmpv themselves. The whole path is described in [`docs/desktop/media-player-design.md`](../../docs/desktop/media-player-design.md).

## Wiring and settings

The main menu button runs whatever `Button3Action` says in `Configs/config.ini`; the desktop ships it as `GoToMedia()`.

Library folders and the optional TMDB key are set in Settings > Media > Local Library and saved in `Configs/desktop.ini` in the user data folder:

```ini
[Library]
MusicRoot=/path/to/music
MoviesRoot=/path/to/movies
TvRoot=/path/to/tv
TMDBKey=
```

With no TMDB key the library still works; posters and synopses just stay empty.

## Editing

In Development Mode the scenes run from loose files, so script changes show up without repacking. See [`xips-source/README.md`](../README.md) for the workflow.
