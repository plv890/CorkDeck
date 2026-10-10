# Install CorkDeck 0.3c

## Requirements

- Homebrew-capable Nintendo Switch, Atmosphere and emuMMC.
- SD card: approximately 60 MB for CorkDeck, 1 GB for downloaded Windows components, plus games.
- Internet access for initial component downloads and Steam features.
- The supplied setup documentation specifies Hekate and Atmosphere 1.11.2 for boot patches. Other combinations have not been established here.
- Horizon OC is optional. CorkDeck does not set clocks; if you use the patched Horizon OC loader, First set-up keeps the settings you already have (the recorded environment used 2.5.1).

## Fresh installation

1. Back up your existing setup. If Autorun already occupies `switch/wine`, rename that directory, for example to `switch/wine.old`, before installing CorkDeck. Both applications use the same paths.
2. Extract `CorkDeck-0.3c.zip`. Copy `switch` to the SD card root.
3. Open Homebrew Menu by holding R while launching a game, using full-memory mode. Start `wine-nx-runtime` at `switch/wine/wine-nx-runtime.nro`.
4. Follow First set-up: install the HOME entry, download Windows components and, where required, install boot patches and the Hekate entry. Back up `atmosphere` and `bootloader` before the boot-patch step. You can leave that step with B; some games work without the patches.
5. If you installed the boot entry, reboot and select **CorkDeck** in Hekate (an entry called Autorun that an earlier version created is renamed in place). Launch CorkDeck from its HOME entry thereafter.

The NRO is launched directly; it does not need installation through DBI. First set-up can be reopened through **− → Maintenance → First set-up**. Labels may differ with interface language.

## Upgrade from 0.1 or 0.2

1. Close CorkDeck: **− → Power → Close CorkDeck**.
2. Back up `switch/wine/wine-nx-runtime.nro` if you want a way back.
3. Replace `switch/wine/wine-nx-runtime.nro` with the file of the same name from `CorkDeck-0.3c.zip`. Nothing else has to be copied: the Steam library is inside the application now, and it writes it to the card by itself.
4. Start CorkDeck from its HOME entry. Games, settings, the library and the Steam session stay as they were. Do not rename or delete `switch/wine` for an upgrade.

After the upgrade:

- **If you used an overclocking profile in 0.2**, open the Horizon OC overlay and look at the section of the application CorkDeck: a profile 0.2 wrote there may still be stored, and 0.3 does not touch it. CorkDeck tells you once when it finds the mark of such a profile on the card.
- The Hekate entry keeps working under its old name. To have it called CorkDeck, run **− → Maintenance → First set-up** again; this rewrites the boot entry and patches, so back up `atmosphere` and `bootloader` first.
- **Maintenance → About CorkDeck** shows the version: it should read 0.3c.
- Diagnostics are off by default now. Turn on **Settings → Diagnostics → Developer mode** before collecting logs for a report.

## Add games

Copy a Windows game directory to the SD card, for example `switch/wine/drive_c/Games/<game>`. In Library, press X, choose to add a program from the SD card and select its EXE. A opens its page; choose Play.

For Steam, use **− → Maintenance → Steam account**, sign in with QR or password, then open the library's uninstalled tab. Games are not supplied with CorkDeck.

## Controls and reports

| Button | Action |
| --- | --- |
| − / + | Main menu / status panel |
| A / B | Select / back |
| X / Y | Actions / search |
| L / R | Library tabs |

Exit through **− → Power → Close CorkDeck**. Reports are under `switch/wine/reports`, logs under `switch/wine/logs`. Review and redact before sharing; never share `steam-session.vault`.

## Removal

Back up games and saves first. Removing `switch/wine` also removes data stored inside it. Remove the HOME entry separately. Boot patches and the Hekate entry were installed separately; deleting the application directory does not undo those changes.
