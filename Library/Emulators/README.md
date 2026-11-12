# Emulators

On Xbox hardware, this is where emulator apps (Surreal64, FCE Ultra, ScummVM and friends) live, each in its own subfolder containing a `default.xbe` plus its ROM library.

The folder is empty in the repo. It exists so the desktop build's `E:` drive has the standard Xbox layout to list at boot.

On desktop, the copy of this folder that counts is the one in your user data folder (`Library/` inside `~/Library/Application Support/Theseus/` on macOS, `~/.local/share/theseus/` on Linux, `%APPDATA%\Theseus\` on Windows), which is seeded from this one on first run. Most entries on desktop come from Title Maker and live in `Configs/games.ini`, so this folder can stay empty. On Xbox hardware your real `E:\Emulators\` is what gets read.
