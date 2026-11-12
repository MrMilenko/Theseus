# Desktop Features

UIX Desktop is the Xbox dashboard first and a PC launcher second. The scene scripts drive everything you see; the desktop build adds what a PC needs underneath them, plus a set of ImGui tool windows for managing your library and building skins and scenes. This page is the tour. Setup steps for users are in the main [README](../../README.md).

## The games grid

On the Xbox, the dashboard found games by walking `E:\Games` and reading each `default.xbe` for its title and icon. The desktop build keeps that exact flow and feeds it entries that mostly aren't folders at all:

- **`Configs/games.ini`**: everything added in Title Maker, including Steam and RetroArch imports. One section per entry with `TitleID`, `Launch`, `Drive` and `Category`.
- **ES-DE**: read live from your ES-DE gamelists every launch, under one ES-DE category with a folder per system.
- **Xbox Cloud Gaming**: your cloud library and consoles, fetched after sign in and injected for the session.
- **Real folders** under `Library/Games`, `Library/Applications` and `Library/Emulators`, the same as on an Xbox.

The file layer makes the first three list like folders, and their `default.xbe` opens as a synthesized header carrying the name and title ID. Icons come from `Configs/icons/<TitleID>.jpg` or `.png`. For each Title Maker entry the desktop also writes a small `Library/UDATA/<TitleID>/TitleMeta.xbx`, so the Memory screen shows a title pod per game the way the Xbox did.

### Launching

`theseus/desktop/launch.cpp` takes the launch line from the entry:

- `xcloud://` and `xhome://` start a cloud or remote play stream inside the dashboard window.
- `steam://` links go to Steam. On Linux the `steam` binary is called directly, because some desktop environments have no handler registered for the scheme.
- Other URLs go to the system URL opener.
- Anything else runs as a shell command from the program's own folder, so executables, scripts, commands with arguments and `$VARIABLES` all work. Quote paths with spaces.

A real launch fades the screen to black the way the Xbox's launch level did, spawns the game, and minimizes the dashboard so the game gets focus. Title Maker's Test Launch uses the same path without the fade or the minimize, and shows how the spawn went. The full command line is always in the log.

## Title Maker (F3)

The central tool for the library. Tabs:

- **Main**: every entry, with search and a category filter. Edit the name, launch line, icon and category; Test Launch; Delete. Create entries by hand, add Xbox ISOs as xemu entries (the title and art are read from the ISO), and Fix Names to tidy up imported titles.
- **Steam**: finds the Steam install (or browse to it), imports every installed game with icons from Steam, adds betas and demos by App ID, opens store pages.
- **RetroArch**: finds the install, imports recently played titles with the right core and box art from RetroArch's thumbnails, or adds a ROM with a core picked from the list. Launch fullscreen is a toggle.
- **ES-DE**: point it at the ES-DE data folder. Rescan rereads it; Compress Art re-encodes everything in `Configs/icons` to 256 pixel JPEGs, which keeps big collections light.

Steam, RetroArch and ES-DE can each be hidden under Optional Tabs. Hiding one only hides the tab.

## Media

- **Local library**: Movies and TV folders, with TMDB posters and synopses if you add a key.
- **Plex**: sign in with a PIN, browse and stream your server.
- **Jellyfin**: server URL plus Quick Connect.
- **Playlist Maker (F6)**: build playlists from the library.

How the player is wired is in [media-player-design.md](media-player-design.md).

## Music

The dashboard's music screen reads from your music folder (Settings > Media > Local Library > Music Root): every subfolder is a soundtrack, `.m3u` files become soundtracks, loose files collect in one called Music. Formats: MP3, WAV, OGG, FLAC, WMA, M4A, AAC. Tags and durations are read once and cached.

With **Use projectM visualizer** on (Settings > Audio), X + Y on the keyboard or a pad opens [projectM](https://github.com/projectM-visualizer/projectm), the open source MilkDrop, in its own window, fed from whatever the dashboard is playing. F or F11 makes it fullscreen. Configure projectM picks presets.

## Xbox Cloud Gaming and remote play

Sign in under Settings > Media > Xbox / xCloud with a device code. Your Game Pass cloud titles and your consoles appear in the games grid; launching one streams it inside the dashboard window over WebRTC (libdatachannel), with FFmpeg decoding the video and Opus the audio. The same panel lists your consoles with a Stream button for each.

## Skins

A skin is a folder in `Data/Skins/`: a `<SkinName>.xbx` file listing material colors, plus any textures and meshes it replaces. The Stock skin is the retail 5960 dashboard, and its colors live in the source (`theseus/render/materials_retail.inl`), so `Stock.xbx` lists nothing. Other skins only list the materials they change.

The **Skin Editor** (Tools > Skin Editor) lists your skins, applies one live, creates a new one from Stock, and edits material colors with a picker before saving them to the skin. UIX Lite community skins work as they are.

## CRT effect

A post process pass over the finished frame, video included, with eight controls: scanlines, curvature, phosphor mask, vignette, bloom, flicker, color bleed and brightness. Subtle, Classic, Heavy and Off presets set them all at once. It's in Settings > Display and View > CRT Effect, and saved in `desktop.ini`.

## Development tools

- **Scene inspector (F1)**: the live scene graph as a tree with search. Click in the viewport to select what's under the cursor, hide or solo nodes and subtrees, and read transforms, materials, textures (with a thumbnail), meshes and script variables. A stats bar shows draw calls and node counts, and there's a wireframe toggle.
- **XAP editor (F2)**: a script editor with XAP syntax highlighting, undo and redo, and compile errors with line numbers. Save and the scene reloads in place.
- **Xbox HDD browser (F5)**: opens an xemu qcow2 hard drive image read only, lists its FATX partitions, browses them and exports files. Set the image in Settings > General and the Memory screen reads saves from it too.

**Development Mode** (chosen at launch when Startup Mode is Ask on launch, or set in Settings > General) unpacks the shipped `.xip` archives into folders under `Data/Xips/` in your user data folder and runs the scenes from those loose files, so script edits show up without repacking. The Development menu only appears in that mode.

## Display and window

Settings > Display covers display mode (windowed, borderless, fullscreen), resolution, vsync, FPS cap, MSAA, renderer, hardware video decode and Emulate Xbox Resolution, which keeps scenes framed in the original 720x480 design space. F11 toggles fullscreen, F12 a borderless window, F10 the menu bar (it can also auto hide).

### Command line

| Flag | What it does |
|---|---|
| `--fullscreen` | Start fullscreen |
| `--4k` | Fullscreen plus 2x UI scale |
| `--scale N` | Scale the tool windows (for example `1.5` or `2.0` on a high DPI screen) |
| `--no-toolbar` | Start with the menu bar hidden |
| `--muted` | Start muted |
| `--minimized` | Start minimized |
| `--dashboard` | Skip the startup choice and run the dashboard |
| `--development` | Start in Development Mode |
| `--graphics-debug` | Extra renderer diagnostics |

## Controllers

SDL2's GameController API covers Xbox, PlayStation, Switch Pro and most other pads. The dashboard reads them exactly like the Xbox's four ports: D-pad and stick to move, A to select, B to back out, the triggers and Black and White where the scenes use them. LT + B hands the pad to the tool windows, with a prompt bar showing what each button does and an on screen keyboard for text fields.

## Settings files

All in `Configs/` inside the user data folder:

| File | What it holds |
|---|---|
| `desktop.ini` | Desktop settings: tool paths (xemu, Steam, RetroArch, ES-DE, qcow2 image), display and CRT, audio, library folders, and Plex, Jellyfin and Xbox sign in tokens |
| `config.ini` | The dashboard's own settings, the same file the Xbox build uses (current skin, main menu wiring, clock and so on) |
| `games.ini`, `Icons.ini`, `icons/` | The launcher library |
| `playlists.ini` | Playlists from the Playlist Maker |

Every save writes a new file, flushes it to disk and swaps it in, keeping the previous one as `.old`.
