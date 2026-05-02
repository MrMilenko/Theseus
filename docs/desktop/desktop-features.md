# Desktop Features

## Beyond a Port

UIX Desktop isn't just the Xbox dashboard running on PC. It adds desktop-native features that turn it into a usable game launcher and development tool. The dashboard's scripting system handles the UI, while the platform layer adds PC-specific functionality underneath.

## Game Launching

Games are added and managed through the **Title Maker** (F3). You don't need to create files or edit configs by hand: the Title Maker handles everything: game entries, icons, launch commands, and categories.

### Adding Games

- **Manually**: Enter a name, launch command, and optional icon
- **Steam import**: One-click scan of your Steam library: discovers installed games, downloads icons from Steam CDN, and creates entries with `steam://` launch URIs
- **Xbox ISOs**: Browse for an ISO file: the Title Maker extracts the game title and cover art automatically, then creates an xemu launch entry
- **xemu setup**: A "Find" button checks common install paths on each platform (`/Applications/xemu.app`, `/usr/bin/xemu`, `C:\Program Files\xemu\`, etc.)

Games appear on the dashboard organized by category (Games, Applications, Homebrew, Emulators, etc.) and drive letter, just like the Xbox.

### How It Works (Under the Hood)

The Title Maker writes to `games.ini`, which the virtual filesystem layer reads at runtime. When the dashboard scans for games (via `FindFirstFile`), the platform layer injects virtual directory entries from `games.ini` alongside any real folders in `xboxfs/E/Games/`. The dashboard doesn't know the difference: it sees what looks like real Xbox game folders.

Icons are stored in `xboxfs/C/UIX Configs/icons/` and mapped via `Icons.ini`. The dashboard's icon selector searches both UDATA title images and the desktop icon database.

### Launching

When you select a game, the dashboard:
1. Hides the SDL window
2. Spawns the game process (`fork`/`exec` on POSIX, `CreateProcess` on Windows)
3. Waits for the process to exit
4. Restarts itself via `execv()` (POSIX) or relaunches via `CreateProcess` (Windows) with `--preview` to skip the preloader

Launch commands can be direct executables, Steam URIs (`steam://rungameid/...`), xemu with an ISO path, or any shell command. The dispatch logic lives in `theseus/desktop/launch.cpp` and follows three rules:

- **`steam://...`**: invokes the `steam` binary directly on Linux to bypass KIO. KDE/Plasma's URL chain often has no registered handler for the `steam` scheme; calling `steam` directly avoids the "Unknown protocol" error path entirely. macOS and Windows still go through their native URL handlers, which work fine there.
- **Other URLs** (`http://`, `https://`, `file://`, ...): system URL opener (`xdg-open` / `open` / `ShellExecute`).
- **Anything else**: runs through the shell (`/bin/sh -c` on POSIX, `cmd /C` on Windows). Raw executables, paths with arguments, bash scripts, multi-token commands like `firefox --new-window URL` all work. Quote any path containing spaces.

The same dispatcher is used by Title Maker's "Test Launch" button (fire-and-forget, no window hide) and by the dashboard's `.uixshortcut` / virtual-game launches (full hide + wait + restart pipeline).

## Title Maker (F3)

The Title Maker is the central tool for managing your game library. It supports adding games manually, importing from Steam, and adding Xbox ISOs with automatic metadata extraction. Each entry gets a name, launch command, icon, category, and drive letter. Icons can be PNG, JPG, or BMP: Steam imports download icons automatically from Steam CDN.

## Save Game Browser

The dashboard's Memory section is fully functional on desktop. It reads real Xbox save data from `xboxfs/E/UDATA/` (or from a mounted qcow2 FATX image) and renders the same save game browser as the original Xbox, complete with:

- **Title pods** with game icons loaded from `TitleImage.xbx`
- **Title headers** with game names parsed from `TitleMeta.xbx` (UTF-16LE with localization support)
- **Save icon rows** with per-save images (falls back to title-level `SaveImage.xbx` when a save doesn't have its own icon)
- **Save metadata** (name, date, block count) exposed via node functions for the XAP script to display
- **Smooth scrolling** between titles with animated transitions
- **D-pad navigation** to browse titles and individual saves

The rendering is adapted from the Xbox source's custom `RenderLoop`: it positions pods, headers, and icon rows using D3D transformation matrices rather than the normal scene graph. This matches the Xbox's approach where `SavedGameGrid` was a separate rendering subsystem.

To test with your own saves, copy Xbox `UDATA` folders into `xboxfs/E/UDATA/`. Each title folder should contain `TitleMeta.xbx` (the game name) and optionally `TitleImage.xbx` (the icon), with save subfolders containing `SaveMeta.xbx`.

## Development Tools

Development tools are gated behind **Development Mode** (set via startup chooser or Settings > General > Startup Mode). In Dashboard Mode, only user-facing features (Title Maker, HDD Browser) appear in the Tools menu. In Development Mode, the Inspector and XAP Editor become available.

### XAP Script Editor (F2)

A live script editor for dashboard development, built on ImGuiColorTextEdit:

- **Syntax highlighting** for VRML/XAP keywords, node types, strings, numbers, and comments
- **Undo/redo** (Ctrl+Z / Ctrl+Y) with full edit history
- **Live reload**: Edit a script, save, and the dashboard reloads the scene without restarting
- **Scene browser**: Navigate inline scenes and browse .xap files on disk
- **Error reporting**: Compiler errors show line numbers and descriptions
- **Floating ImGui panel**: Independent from all other tools, resizable and repositionable

### Scene Inspector (F1)

A read-only scene graph browser for understanding what the dashboard is doing:

- **Scene tree**: Expandable hierarchy of all nodes with search/filter
- **Visibility toggles**: Click to hide/show any node or subtree
- **3D click-to-select**: Click any object in the viewport to select it in the tree (pulsing green highlight)
- **Right-click context menu**: Solo/unsolo, show/hide, recursive operations on subtrees
- **Node properties** (read-only): Transform (position, scale, rotation, alpha), material (diffuse, emissive, transparency), texture (name, dimensions, thumbnail preview), geometry info, mesh file
- **Script variables**: All CInstance members shown with types and current values
- **Stats bar**: Draw call count, visible/total nodes, group count, wireframe toggle

Property editing is currently read-only. A future update will wire edits back to the XAP source for round-trip editing.

### Xbox HDD Browser (F5)

Browse and manage Xbox hard drive images:

- **qcow2 support**: Mount Xbox HDD images (the format xemu uses)
- **FATX filesystem**: Full read-write access to the Xbox file system
- **Title enumeration**: Browse installed games, saves, and system files
- **Integration**: SavedGameGrid reads titles and icons from mounted images

## CRT Post-Processing

An optional retro display filter accessible from Settings (F4) or the View menu, implemented as a full-screen GLSL fragment shader with 8 independent parameters:

- **Scanlines**: Horizontal darkened lines simulating a CRT display
- **Curvature**: Barrel distortion simulating a curved CRT screen
- **Phosphor mask**: RGB sub-pixel pattern
- **Vignette**: Corner and edge darkening
- **Bloom**: Glow/light bleed from bright areas
- **Flicker**: Brightness variation simulating unstable signal
- **Color bleed**: Horizontal color smear
- **Brightness**: Overall intensity adjustment

**Presets** (Off, Subtle, Classic, Heavy) set all 8 parameters at once, or each can be tuned individually in the Settings window. Settings persist in `desktop.ini`. CRT effects also apply to media playback.

## Audio

The desktop build uses SDL_mixer for audio playback:

- **Dashboard sounds**: Button clicks, transitions, navigation feedback
- **Background music**: Soundtrack playback from the music directory
- **Mute toggle**: Ctrl+M to mute/unmute all audio (persistent toast overlay)
- **Auto-mute during game launch**: Audio pauses when a game is running
- **IMA ADPCM decoding**: Xbox audio format decoded in software

## Media Player

The dashboard's original Xbox DVD player UI (`dvd.xap`) has been wired to libmpv for file-based media playback. When a media file is opened (File > Open Media), the DVD player scene loads and renders video through the XAP's `texture_panel` mesh while the OSD overlay shows playback info.

See [Media Player Design](media-player-design.md) for the full technical breakdown.

## Preloader

When the application starts (without a startup-mode CLI flag), a startup screen lets you choose:

- **Dashboard Mode**: Uses compiled `.xip` binary archives (production mode, user-facing features only)
- **Development Mode**: Uses extracted script directories (supports live editing, enables Inspector and XAP Editor)

The choice is saved to `desktop.ini` and can be changed later in Settings > General > Startup Mode.

## CLI Flags

Pass these on the command line when launching the binary:

| Flag             | What it does                                                                  |
|------------------|-------------------------------------------------------------------------------|
| `--scale N`      | UI scale factor for the ImGui debug overlay (`1.5`, `2.0` for high-DPI / 4K) |
| `--fullscreen`   | Borderless fullscreen at native resolution                                    |
| `--4k`           | Convenience: `--fullscreen` + `--scale 2.0`                                   |
| `--no-toolbar`   | Start with the menu bar hidden (toggle back with F10)                        |
| `--dashboard`    | Start in dashboard mode (skip the preloader picker)                           |
| `--development`  | Start in development mode (enables Inspector, XAP Editor)                     |
| `--muted`        | Start with audio muted                                                        |
| `--preview`      | Legacy alias for `--dashboard`                                                |

`--scale` writes to `ImGui::GetIO().FontGlobalScale`. ImGui's `imgui.ini` only stores window layout (positions, sizes, docking state); UI scale is not persisted there and must be re-supplied each launch.

## Menu Bar Visibility

The top menu bar can be toggled three ways:

- **F10**: global hotkey, toggles in and out
- **File menu, Hide Menu Bar**: discoverable menu item (gone with the bar; F10 brings it back)
- **`--no-toolbar`**: CLI flag, starts hidden

Useful when running fullscreen and you want a clean viewport.

## Configuration

Desktop-specific settings are stored in `xboxfs/C/UIX Configs/desktop.ini`:

```ini
XemuPath=/Applications/xemu.app
QcowPath=/path/to/xbox.qcow2
StartupMode=1
CRT_Enabled=0
CRT_Scanlines=0.3
CRT_Curvature=0.2
CRT_Phosphor=0.3
CRT_Vignette=0.3
CRT_Bloom=0.15
CRT_Flicker=0.2
CRT_ColorBleed=1.0
CRT_Brightness=1.05
```

Dashboard settings (skins, video mode, language, etc.) use the same `config.ini` as the Xbox version. The configuration system is shared between platforms.

## Controller Support

SDL2's GameController API provides native support for:

- Xbox controllers (wired and wireless via Bluetooth/USB)
- PlayStation DualShock 4 and DualSense controllers
- Nintendo Switch Pro Controller
- Any SDL2-compatible gamepad

The dashboard maps controller input to the same navigation model as the Xbox: D-pad for movement, A for select, B for back, triggers for page scrolling. All XAP-visible callbacks, the secret key sequence system, screen saver reset, and typomatic repeat logic are preserved.
