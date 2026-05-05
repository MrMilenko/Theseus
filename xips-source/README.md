# xips-source

Theseus-original XAP and XIP source. This directory holds the editable scene scripts and asset manifests for new dashboard content that ships alongside Theseus. Compiled `.xip` archives live under [`xboxfs/Q/Xips/`](../xboxfs/Q/Xips/); this is where their plain-text source comes from.

## Why this directory exists

The shipped `xboxfs/Q/Xips/` tree is mostly binary XIP archives carried forward from the UIX Lite release. Theseus runs those archives unmodified to preserve compatibility with community skins and the dashboard's existing scene contract. When Theseus needs to *add* its own scenes (a new launcher tab, the media player, future Theseus-only features), the source for those scenes goes here. The binary output gets packed into `xboxfs/Q/Xips/` separately.

We are deliberately not yet doing a full extract of every shipped XIP into this tree. UIX Lite's XIPs remain the upstream canonical source for that content, and we don't want to fork them in the public repo until the divergence story is settled.

## Layout

```
xips-source/
  README.md             this file
  mediaplayer/          Theseus media player (replaces dashboard DVD player on desktop)
```

Each subdirectory holds one logical unit of work: typically one or more `.xap` script files plus any geometry, texture, or layout source they reference. A subdirectory is roughly one shipping `.xip` (with the option to inject loose `.xap` files into an existing archive like `default.xip`).

## Workflow

1. **Write loose `.xap` source here.** The XAP language is documented in [`docs/xap-contract.md`](../docs/xap-contract.md). Reference the existing dashboard XAPs (extracted via `xiptool`, see below) for scene patterns.
2. **Iterate against a dev-mode build.** Theseus's desktop build supports loose-XAP loading at runtime when the dashboard's loose-load mode is enabled, which lets you edit-and-reload without repacking.
3. **Pack to `.xip` when shipping.** The packing step uses `xiptool` from the UIX Desktop tools (`UIX-Desktop/tools/xiptool/`). Once a Python `xiptool.py` port lands in this repo, that becomes the canonical packer.
4. **Drop the packed `.xip` into `xboxfs/Q/Xips/`** so the runtime build picks it up. For new entries injected into `default.xip`, the packer needs to merge into the existing archive rather than create a new one.

## Current units

### `mediaplayer/`

Generic libmpv-backed video media player. Replaces the dashboard's DVD player on desktop. Repurposes the XONLINE main-menu tab via `xboxfs/C/UIX Configs/config.ini` (`Button3Action=GoToMedia()`).

Backed by C++ node `CMediaCollection` (new, in `theseus/desktop/desktop_nodes.cpp`). `CDVDPlayer` is preserved as a no-op contract surface so XAPs still referencing it compile clean. Library roots + TMDB API key live in `xboxfs/C/UIX Configs/desktop.ini` under `[Library]`, editable in-app via the Settings window.

Three-scene state machine (browser → action menu → seasons/episodes), then standalone fullscreen mpv playback layered on top of the dashboard. See `xips-source/mediaplayer/README.md` for the directory layout.

## Note on UIX Lite divergence

When Theseus's `xboxfs/Q/Xips/` content begins to diverge from UIX Lite's upstream archives (a Theseus-original `mediaplayer.xip`, modifications to `default.xip` to add `mediaplayer.xap`, etc.), the changes are intentionally Theseus-side and do not flow back to UIX Lite. The divergence is a deliberate split: UIX Lite's XIPs continue to work on a Theseus-powered dashboard unchanged, but Theseus's shipped XIPs may carry Theseus-only scenes that UIX Lite's dashboard binary would not load.
