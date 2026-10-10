# CorkDeck 0.3

Windows games on Nintendo Switch with a controller-friendly Wine interface and Steam library integration. This page lists what changed since the first public version, 0.2. Русская версия: [RELEASE-0.3.ru.md](RELEASE-0.3.ru.md).

## Download

Install **CorkDeck-0.3.zip** from the release page and check its SHA-256 against `SHA256SUMS.txt`. A new installation follows [INSTALL.md](INSTALL.md); an existing one is upgraded by replacing one file (below).

## Changes since 0.2

### Compatibility

- **Missing libraries are found before a program starts.** CorkDeck reads the import table of the program's executable, looks for every DLL beside it and in the Windows folder of its architecture, and names what is missing on the game's page and when Play is pressed.
- **Microsoft Visual C++ 2015-2022 is added automatically**, MFC included. The redistributable is downloaded from Microsoft, checked against a recorded size and SHA-256, and its libraries are taken out of the archives inside it into the Windows folder. Only absent files are written; nothing existing is replaced. Microsoft's own installer is not run: it does not work in this environment.
- **The local Steamworks library answers the way games built with Visual C++ call it.** Functions that return a structure, such as a Steam ID, take the hidden result address that compiler passes; every interface has its own table in that compiler's order, overloads included. Games that read such results no longer crash seconds after starting.
- **Every Steamworks interface family exists.** Thirteen were added (UGC, HTTP, Controller, Inventory, Music, MusicRemote, Video, AppList, HTMLSurface, UnifiedMessages, GameServerStats, ParentalSettings, RemotePlay). They answer "nothing", but a game that imports them now loads.
- **The Steamworks library is inside the application.** It is written to the card when it is absent or of another version, so a new card, or an upgrade by one file, needs no manual copying.
- **Windows picture codecs are registered.** The decoders, encoders, converters and pixel formats of `windowscodecs` (PNG, JPEG, BMP, GIF and others) are in the registry at start, so Direct3D helper libraries recognise image files.
- **The Windows components are one fixed, tested set.** They are downloaded from the upstream DLL repository at one revision instead of whatever its main branch holds that day; a later change there cannot give a new installation an untested or incomplete set.

### Shell

- **Delete a game from the SD card** from its page (Manage). The question names the folder, its size and file count; saves in the user profile are kept; a shared folder is never deleted.
- **Storage**: what each game takes on the card, what unfinished downloads take, and what is free.
- **Downloads after a restart** lists unfinished downloads, to continue or delete.
- **The whole account**: the "Not installed" tab is no longer cut at 256 games, and games with many packages open for installation.
- The page of a game before installation shows its size and the language it will be installed in.
- **The version is visible**: Maintenance, About CorkDeck.
- Long Russian sentences are no longer broken in the middle of a letter when wrapped.
- The window of a download that cannot continue has a title that fits, and a cancelled download no longer shows as waiting.

### System

- **CorkDeck does not change clocks.** The per-game overclocking profiles of 0.2 are removed, together with the CPU boost during Steam installation. Overclocking is left to Horizon OC. First set-up no longer has an overclocking step and installs the patched Horizon OC loader only with the settings the card already has.
- **Developer mode**, off by default. Without it nothing is recorded beyond failures: no traces, counters, wait records or status file, and the Diagnostics page, the Steam diagnostics export and the LAN log view are hidden.
- **The Hekate boot entry is called CorkDeck.** An entry called Autorun that an earlier version created is renamed in place.
- Detailed traces can record every exception with its code and address, for finding why a program stops.

### Code and tests

- About 1900 lines of the previous top-level screens, which no path led to since the new shell, are removed from the launcher, with the translations nothing displayed.
- **The host test suite passes: 108 of 109 checks, one skipped** (it needs a native AArch64 machine). In 0.2, 26 did not run: fixtures had fallen behind the code they exercise, some needed an ARM64 processor, and some needed sources that were not on the test machine. The fixtures are brought up to date, the ARM64 ones run under an emulator, and two memory leaks in the tests themselves are closed.
- The two shell checks build everything the shell is made of with every warning treated as an error and drive the current screens.
- The suite runs in parallel in about a minute.

## Upgrade from 0.1 or 0.2

1. Close CorkDeck: **− → Power → Close CorkDeck**.
2. Replace `switch/wine/wine-nx-runtime.nro` on the SD card with the file of the same name from `CorkDeck-0.3.zip`. Nothing else has to be copied.
3. Start CorkDeck from its HOME entry. Games, settings and the Steam session stay.

Do not rename or delete `switch/wine` for an upgrade. If you used an overclocking profile in 0.2, check the CorkDeck section in the Horizon OC overlay afterwards and reset it there if you do not want it: 0.3 no longer touches it. More in [INSTALL.md](INSTALL.md#upgrade-from-01-or-02).

## Status

0.3 was installed from its package on an empty folder on a console: First set-up, the download of the Windows components, a reboot through the CorkDeck boot entry, Steam sign-in by QR code and the installation of a game from Steam were gone through, and games were started. Gameplay was not systematically tested. Known limits are in [COMPATIBILITY.md](COMPATIBILITY.md).

Thanks to the author and contributors of Autorun / Wine-NX and the underlying open-source projects. CorkDeck's changes were written mainly by AI coding agents under the direction of the fork's creator, Maj0ra'sMask; see [CREDITS](../CREDITS.md).
