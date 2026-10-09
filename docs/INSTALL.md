# Install CorkDeck 0.2

## Requirements

- Homebrew-capable Nintendo Switch, Atmosphere and emuMMC.
- SD card: approximately 60 MB for CorkDeck, 1 GB for downloaded Windows components, plus games.
- Internet access for initial component downloads and Steam features.
- The supplied setup documentation specifies Hekate and Atmosphere 1.11.2 for boot patches. Other combinations have not been established here.
- Horizon OC is optional for clock profiles; the recorded environment used 2.5.1.

## Fresh installation

1. Back up your existing setup. If Autorun already occupies `switch/wine`, rename that directory, for example to `switch/wine.old`, before installing CorkDeck. Both applications use the same paths.
2. Extract `CorkDeck-0.2.zip`. Copy `switch` to the SD card root.
3. Open Homebrew Menu by holding R while launching a game, using full-memory mode. Start `wine-nx-runtime` at `switch/wine/wine-nx-runtime.nro`.
4. Follow First set-up: install the HOME entry, download Windows components and, where required, install boot patches and the Hekate entry. Back up `atmosphere` and `bootloader` before the boot-patch step. You can leave that step with B; some games work without the patches.
5. If you installed the boot entry, reboot and select **Autorun** in Hekate (the entry still uses the upstream name). Launch CorkDeck from its HOME entry thereafter.

The NRO is launched directly; it does not need installation through DBI. First set-up can be reopened through **− → Maintenance → First set-up**. Labels may differ with interface language.

## Upgrade from 0.1

Back up your installation. Replace `switch/wine/wine-nx-runtime.nro` and merge the supplied `switch/wine/steamworks-local` files into the existing directory. Keep games, settings and the Steam session. Do not rename your existing CorkDeck directory for this upgrade.

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
