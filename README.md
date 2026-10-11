# CorkDeck

**Play Windows PC games and your Steam library on a Nintendo Switch.**

[Русский](README.ru.md) · [Download](https://github.com/plv890/CorkDeck/releases/latest) · [Installation](docs/INSTALL.md) · [Compatibility](docs/COMPATIBILITY.md) · [Credits](CREDITS.md)

![CorkDeck game library on a Nintendo Switch, with Steam games installed](docs/screenshots/library.jpg)

CorkDeck is a homebrew application that runs Windows games and programs on the Nintendo Switch, directly under the console's own Horizon OS. It puts a controller-friendly game library, Steam sign-in and downloads, per-game settings and launch diagnostics into one interface, in the spirit of the Steam Deck's game mode.

Under the interface are [Wine](https://www.winehq.org), the [FEX](https://github.com/FEX-Emu/FEX) and [Box64](https://github.com/ptitSeb/box64) x86 translators, and the Mesa, [DXVK](https://github.com/doitsujin/dxvk) and [VKD3D-Proton](https://github.com/HansKristian-Work/vkd3d-proton) graphics stack.

> **CorkDeck is a fork of [Autorun](https://github.com/autorunhq/autorun) (formerly Wine-NX).**
> Everything that makes Windows software run on the Switch at all comes from that project, its author and its contributors. CorkDeck builds a different shell and a Steam workflow on top of their work. Thank you.

CorkDeck is experimental. Some games run, many do not yet, and performance depends heavily on the game. Please read [what has actually been checked](docs/COMPATIBILITY.md) before you expect a particular title to work.

## Screenshots

Unedited captures from a Nintendo Switch running CorkDeck 0.2. The interface comes in English and Russian.

| | |
| --- | --- |
| ![Game page showing dependencies, translator, renderer and the last session](docs/screenshots/game-page.jpg) | ![Per-game settings with the purpose and possible effect of the selected option](docs/screenshots/per-game-settings.jpg) |
| Game page: what the game will run with, and how its last session ended | Per-game settings: every option says what it is for and what it may affect |

![Fallout 3 (Steam version) at its main menu on a Nintendo Switch, started from CorkDeck 0.2](docs/screenshots/fallout3-on-switch.jpg)

Fallout 3 (Steam version) at its main menu on a Nintendo Switch, started from CorkDeck 0.2.

## Download and install

1. Download **`CorkDeck-0.3e.zip`** from [Releases](https://github.com/plv890/CorkDeck/releases/latest). The "Source code" archives on that page are for developers; the installable package is the ZIP named CorkDeck.
2. Extract it and copy the `switch` folder to the root of the SD card.
3. Open the Homebrew Menu over a game (hold **R** while starting one), start `wine-nx-runtime` and follow First set-up. It adds a CorkDeck entry to the HOME menu and downloads the Windows components.

The full guide, including moving over from Autorun and updating from an earlier CorkDeck, is in [docs/INSTALL.md](docs/INSTALL.md).

### What you need

- A Nintendo Switch that can run homebrew, with Atmosphère. Use emuMMC only.
- An SD card: about 60 MB for CorkDeck, about 1 GB for the Windows components it downloads, plus room for games.
- Wi-Fi for the first set-up and for Steam.
- Your own games. None are included.
- Optional: [Horizon OC](https://github.com/Horizon-OC/Horizon-OC), if you overclock. CorkDeck itself never changes clocks.

## What it does

- **Library.** Add any Windows `.exe` from the SD card, or install games from your Steam account.
- **Steam on the Switch.** Sign in with a QR code or password, browse the games you own, download and install them on the console.
- **Per-game settings** for the translator, the renderer, threads and memory.
- **Missing libraries found before a game starts**, and Microsoft Visual C++ 2015-2022 added automatically.
- **Diagnostics.** An FPS and frame-time display, a report for every launch, and a developer mode that is off by default.

## What CorkDeck adds to Autorun

CorkDeck keeps Autorun's runtime, the part that actually executes Windows code, and rebuilds the application around it.

**Interface**

- A new shell with a home screen (recent games, and a "needs attention" list that names what is wrong), a library and a page for every game.
- Library tabs for all, favourite, installed, not installed and local programs, your own categories, sorting, a grid or a list, and search.
- A game page that shows before you press Play which Windows components, DXVK version, translator and renderer will be used, how long you have played, and how the last session ended. A game with several executables lets you pick which one starts.
- Settings where **every option explains its purpose and its possible effect**, and shows where the current value comes from: the default, the global settings or the game's own profile. One button resets it.
- The main menu on **−** and a status panel on **+** from any screen; notifications in a corner instead of blocking windows.
- A launch screen with the game's artwork that stays until the game draws its first frame, and windows of launchers and configuration tools that remain visible before rendering starts.
- English and Russian interface, with a new typeface (Manrope).
- After a game exits, you are back in the shell.

**Steam**

- Sign-in inside the shell, by QR code or password, with the session kept between starts.
- The games of your account in a "Not installed" tab, with covers fetched from Steam.
- A downloads screen with progress, speed over the last two minutes and time left.
- Downloads that resume after an interruption. When one cannot continue, CorkDeck offers to start it over, cancel it or try again, and logs why it failed.
- English files are installed when a game has none in the system language.
- Steam install scripts are applied (registry entries and folder copies), which games such as Fallout 3 need in order to start at all.
- Games built on Steamworks.NET no longer close right after starting.

**Running games**

- Pressing Play downloads missing Windows components by itself.
- Before a program starts, CorkDeck reads which libraries it imports and names the ones that are missing. The Microsoft Visual C++ 2015-2022 runtime is downloaded from Microsoft and added by itself.
- A local Steamworks library inside the application, laid out the way games built with Visual C++ call it, with every interface family present.
- Windows picture decoders (PNG, JPEG and others) are registered, so games that load images through Direct3D helpers find them.
- CorkDeck does not change the console's clocks or voltages. Overclocking stays in Horizon OC, where you set it.
- A game can be deleted from the SD card from its page, and Storage shows what each game takes.
- A global switch for the FPS and frame-time display.
- If the system closes the application's access to the SD card, CorkDeck reconnects without a restart.

**Diagnostics**

- Every launch gets its own report folder: the game's log, the settings that were applied, the modules that were loaded, and how and when it ended.
- A session history per game, with duration and exit code.
- The logs of the two previous starts are kept.

**Removed:** the application no longer updates itself over the network. A new version is installed by replacing one file.

CorkDeck is not a drop-in replacement: both projects use the `switch/wine` folder, so install one or the other. See [FORK.md](FORK.md) for the fork point and [docs/RELEASE-0.3.md](docs/RELEASE-0.3.md) for this version's changes (the quick updates: [0.3b](docs/RELEASE-0.3b.md), [0.3c](docs/RELEASE-0.3c.md), [0.3e](docs/RELEASE-0.3e.md); 0.2: [RELEASE-0.2](docs/RELEASE-0.2.md)).

If something is broken in CorkDeck, please report it [here](https://github.com/plv890/CorkDeck/issues) and not to the Autorun project. They did not write the parts CorkDeck changed.

## Questions people ask

**Is this SteamOS or Linux on the Switch?**
No. CorkDeck does not install another operating system and does not need Linux or Android on the console. It is a homebrew application for the Switch's own system software. The interface borrows the idea of the Steam Deck's game mode, and the resemblance ends there.

**Is this the real Steam client?**
No. CorkDeck has its own small Steam client that signs in to your account, lists the games you own and downloads them. Friends, achievements, Steam Cloud, the overlay and online multiplayer are not provided.

**Which games work?**
There is no compatibility list yet, only a short set of [recorded results](docs/COMPATIBILITY.md). Older Direct3D 9 to 11 games are the realistic target. The Switch has a 2015 mobile chip and runs x86 code through a translator, so set expectations accordingly.

**Does it work on every Switch?**
It has been developed and tested on one console. Reports from other models are welcome.

**Is it safe for my console?**
CorkDeck runs under custom firmware, which Nintendo does not permit. Keep it on emuMMC. First set-up can install boot patches that some games need; back up the `atmosphere` and `bootloader` folders before that step, or skip it.

## Authorship and use of AI

CorkDeck is made by **Maj0ra'sMask** ([@plv890](https://github.com/plv890) on GitHub), who is responsible for its concept, product decisions, systems engineering and testing on hardware.

**The fork's code and documentation were written mainly by AI coding agents** (OpenAI Codex / GPT and Anthropic Claude) working under that direction, and checked by automated host tests and by runs on a real console. This is stated plainly so that anyone reading or reusing the code knows how it was produced. It applies to CorkDeck's own changes only; Autorun, Wine and every other component keep their own authors and credit.

## Building from source

See [BUILDING.md](BUILDING.md). The scripts and inputs of the package that are not part of the main tree, such as the build and test runners and the Mesa changes, are in [release-support](release-support); the local Steamworks library is built from `horizon-wine/assets/steamworks/source`. Working notes from development are in [docs/research](docs/research).

## Contributing

Bug reports and precise compatibility results are welcome, in English or Russian. See [CONTRIBUTING.md](CONTRIBUTING.md). Review logs before sharing them, and never upload `steam-session.vault`, passwords, tokens or sign-in QR codes.

## Licence and credits

Wine and the Horizon changes to it are under the LGPL-2.1-or-later; see [LICENSE](LICENSE) and [COPYING.LIB](COPYING.LIB). Other components keep their own licences, listed in [NOTICES.md](NOTICES.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and inside the package under `switch/wine/licenses`. Full acknowledgements are in [CREDITS.md](CREDITS.md). The original project's introduction is kept as [README.upstream.md](README.upstream.md).

CorkDeck is not affiliated with or endorsed by Nintendo, Valve, Microsoft or the Autorun project. Nintendo Switch, Steam, Steam Deck, SteamOS and Windows are trademarks of their owners.
