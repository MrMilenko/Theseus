# Games

On Xbox hardware, this is where game rips and homebrew games live, each in its own subfolder containing a `default.xbe`. The title scanner walks this tree at boot, reads each XBE's certificate for its title ID and name, and fills the games panel.

The folder is empty in the repo. It exists so the desktop build's `E:` drive has the standard Xbox layout to list at boot. A folder here on desktop can carry a `default.uixshortcut` file with a `Launch=` line, which the launcher uses when the folder has no `games.ini` entry.

On desktop, the copy of this folder that counts is the one in your user data folder (`Library/` inside `~/Library/Application Support/Theseus/` on macOS, `~/.local/share/theseus/` on Linux, `%APPDATA%\Theseus\` on Windows), which is seeded from this one on first run. Most entries on desktop come from Title Maker and live in `Configs/games.ini`, so this folder can stay empty. On Xbox hardware your real `E:\Games\` is what gets read.
