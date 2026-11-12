# Media Player

The Xbox could play DVDs, provided you bought the little IR remote dongle that unlocked it. On desktop there's no disc to spin, so UIX Desktop plays your files and your Plex and Jellyfin libraries instead, through libmpv, behind scenes styled like the rest of the dashboard.

## The pieces

```
Main menu button  ->  mediaplayer.xap scenes    (browse, action menu, seasons and episodes)
                           |
                           v
                   MediaCollection node        (media_db.cpp: scan, cache, TMDB, queries)
                           |  PlayMovie / PlayEpisode
                           v
                   media_ui.cpp                (fullscreen video, OSD, track menu, queue)
                           |
                           v
                   media_player.cpp            (libmpv, software render, bgfx texture)
```

Plex and Jellyfin follow the same shape with their own nodes (`PlexLibrary`, `JellyfinLibrary`) and scenes (`plex.xip`, `jellyfin.xip`), streaming from the server instead of reading files.

## Getting there

On the desktop build the main menu's Xbox Live button is pointed at the media library. `Configs/config.ini` holds the main menu wiring, and its `Button3Action=GoToMedia()` makes that button run `GoToMedia()` from the desktop `default.xap`, which loads the media player scenes. The scripts are in `xips-source/mediaplayer/` and the packed archives are `mediaplayer.xip` and `mediaplayer_action.xip` in `Data/Xips/`.

## The library: MediaCollection

`CMediaCollection` (in `theseus/desktop/media_db.cpp`) owns the library. It scans the Movies and TV roots set in Settings > Media > Local Library:

- **Movies**: one folder per movie (the first video file in it, or one level down), or video files straight in the root
- **TV**: one folder per show, with `Season N` or `Specials` folders inside, or episodes straight in the show folder. Episodes sort by `S01E02` style numbers, then by name
- **Video types**: `.mkv`, `.mp4`, `.m4v`, `.avi`, `.mov`, `.webm`

Results are cached in `Library/MediaDB.cache` so the next launch doesn't rescan. With a TMDB API key the scan also fetches posters, synopses and ratings; without one the library still works, just with plain titles.

The scenes talk to it through script functions: `GetMovieCount`, `GetMovieTitle`, `SetCurrentShow`, `GetSeasonCount`, `GetEpisodeTitle`, `GetSelectedPlot`, `PlayMovie`, `PlayEpisode`, `RefreshLibrary` and friends. The scripts never touch libmpv directly.

## Playback: media_ui and media_player

`PlayMovie` and `PlayEpisode` hand a path and a title to `media_ui.cpp`, which takes over the screen: video fullscreen, an on screen display in the dashboard's style, an audio and subtitle track picker, and a play queue for playlists. Esc, Q or Backspace stops and returns you to the library scene.

`media_player.cpp` wraps libmpv. Every play creates a fresh mpv instance and every stop tears it down completely, because partial stops left the render pipeline wedged on Apple Silicon. mpv renders each frame in software into a CPU buffer, which is uploaded to a bgfx texture and drawn with a fullscreen blit. That keeps video independent of whichever GPU backend bgfx picked, and lets the CRT effect apply to video too. Hardware decoding is a toggle in Settings > Display.

mpv brings its own audio output (CoreAudio, PulseAudio or ALSA, WASAPI). Dashboard sounds and music are muted while video plays and come back when it stops.

## Playlists

The Playlist Maker (F6) builds playlists out of the media library. They're stored as file paths in `Configs/playlists.ini`, so they survive rescans and renamed metadata, and `PlaylistCollection` exposes them to scripts. During playback `[` and `]` step through the queue.

## The DVDPlayer node

The dashboard's own DVD scene, `dvd.xap`, drives a `DVDPlayer` node: play, pause, scan, chapters, angles, subtitles, A/B repeat, zoom, and a stream of callbacks (`OnTimeChange`, `OnChapterChange` and so on) that the on screen display listens to. The desktop `DVDPlayer` still honors that whole contract and maps it onto libmpv: scans become playback speed (reverse scan is periodic seeking backward, since mpv can't play in reverse), chapters map to mpv chapters, and audio and subtitle stream changes cycle mpv's tracks. Its per frame update fires the callbacks in the same order the 5960 binary does.

On desktop the disc drive only ever reports audio CDs, though, so in normal use nothing opens `dvd.xap`. The node is there so the scene and its scripts keep loading and stay correct; the media library above is how you actually watch things.

## Audio CDs

`DiscDrive` on desktop polls the first optical drive for an audio CD and reports it to the dashboard, which opens the music screen the way the Xbox did. Detection works on Linux and Windows (not macOS yet), and playback goes through libmpv.
