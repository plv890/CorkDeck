# CorkDeck 0.2

Windows games on Nintendo Switch with a controller-friendly Wine interface and Steam library integration.

## Download

Install **CorkDeck-0.2.zip**. Check its SHA-256 against the accompanying checksum file. Read the included installation instructions before copying files.

## Changes

- Steam game setup applies registry entries and folder copies from installation scripts. It does not execute arbitrary processes from those scripts.
- Fixed a Steamworks.NET restart request that caused affected games to close at startup.
- Steam downloads fall back to English when files for the selected language are absent, and can recover from changed download plans.
- Stalled downloads offer restart, cancel or another resume attempt; failures are logged.
- The shell reconnects after a closed SD filesystem session.
- Launcher and configuration windows stay visible before the game begins rendering.
- Per-game Horizon OC clock profiles, bounded by available clocks and the system configuration.
- Global FPS/frame-time display settings and more accessible TSO settings.
- Reports for individual game launches.

## Installation and upgrade

For a fresh installation, copy `switch` to the SD root and follow [First set-up](INSTALL.md). Back up an existing Autorun installation before migration: both projects use `switch/wine`.

From CorkDeck 0.1, replace `switch/wine/wine-nx-runtime.nro` and copy the supplied `switch/wine/steamworks-local` contents over the existing files. Preserve the remaining directories, games, settings and Steam session.

## Status

Final 0.2 was checked for shell and library startup, recovery of a Call of Duty 4 installation through its intro logos, and Fallout 3 and Dishonored up to their main menus. Gameplay was not checked in this version; other observations come from earlier development builds. See [compatibility and limitations](COMPATIBILITY.md).

Thanks to the author and contributors of Autorun / Wine-NX and the underlying open-source projects. CorkDeck's changes were written mainly by AI coding agents under the direction of the fork's creator, Maj0ra'sMask; see [CREDITS](../CREDITS.md).
