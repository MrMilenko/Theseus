# xips-source

Editable source for the scene archives Theseus adds or changes. The dashboard's scenes are XAP scripts plus meshes (`.xm`) and textures (`.xbx`), packed into `.xip` archives. The packed archives that ship are in [`Data/Xips/`](../Data/Xips/); this folder holds their plain text source so changes can be read, diffed and reviewed.

## Why it exists

Most of `Data/Xips/` is UIX Lite's XIP set, carried forward so community skins and the dashboard's scene contract keep working. When Theseus needs its own scenes (the media player, Plex and Jellyfin, desktop settings, orbs) or a change to a stock one, the source lands here. UIX Lite's XIPs still run on Theseus unchanged; Theseus's own archives can carry scenes UIX Lite's dashboard wouldn't load.

## Layout

| Folder | Packs into | What it is |
|---|---|---|
| `default-desktop/` | `default.xip` (desktop) | The top level scripts for the desktop build: main menu, settings, hard drive, music, media player, Plex, Jellyfin, orbs |
| `default-xbox/` | the Xbox `default.xip` (shipped here as `default-xbox.xip`) | The same top level scripts for the console build |
| `default/` | | The earlier common copy both of those were forked from |
| `mainmenu5/` | `mainmenu5.xip` | Main menu scene: the game pods and their meshes |
| `settings3/` | `settings3.xip` | Settings scene |
| `music/` | `music2.xip`, `music_copy3.xip`, `music_playedit2.xip` | Music screens |
| `mediaplayer/` | `mediaplayer.xip`, `mediaplayer_action.xip` | The media library browser and action menu ([README](mediaplayer/README.md)) |
| `plex/`, `jellyfin/` | `plex.xip`, `jellyfin.xip` | Plex and Jellyfin browsers |

The desktop and Xbox `default.xip` are separate because a few scenes reference nodes that only exist on one build, and a scene that names an unknown node fails to load. When you change one, check whether the other needs the same change.

## Working on scenes

1. **Read the contract.** Node types, functions, properties and callbacks are catalogued in [`docs/xap-contract.md`](../docs/xap-contract.md). The stock XAPs here are the best examples of scene patterns.
2. **Iterate in Development Mode.** Pick Development Mode at launch (or set Startup Mode in Settings > General). It unpacks the shipped archives into folders under `Data/Xips/` in your user data folder and runs the scenes from those loose files. Edit there, or in the XAP editor (F2), and the scene reloads without repacking.
3. **Bring the change back here** so it's reviewable, then repack the archive into `Data/Xips/`. There isn't an XIP packer in this repo yet, so repacking is done with an external tool for now.
