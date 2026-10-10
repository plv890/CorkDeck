# Compatibility and known limits

Evidence is taken from the project's recorded checks, not new tests performed while preparing this page.

| Game / feature | Recorded result | Scope |
| --- | --- | --- |
| Call of Duty 4 | Interrupted download resumed, installation completed, intro logos reached | Final 0.2; gameplay not checked |
| Fallout 3 | Launcher and main menu reached | Final 0.2; gameplay not checked. Use Fallout3Launcher.exe; its PLAY button needs the cursor |
| Catlateral Damage | Main menu reached | Earlier development builds |
| Dishonored | Intro and main menu reached, about 24 FPS in the menu | Final 0.2; gameplay not checked. Crashes observed in earlier builds with TSO disabled |
| Library and Steam uninstalled tab | Opened successfully | Final 0.2 |

These are limited observations, not a list of fully playable games.

## Games known not to run

Games that were examined and do not run, with the reason. This list grows from bug reports: if a game fails for you, open an [issue](https://github.com/plv890/CorkDeck/issues) with its log (see the steps in [Installation](INSTALL.md), Developer mode) and it is looked into and added here.

| Game | Result | Reason | Can it be fixed in CorkDeck |
| --- | --- | --- | --- |
| Mirror's Edge (Steam) | Stops with "Application load error" before the game starts | The program is wrapped in Steam's own copy protection. Before starting the game the wrapper checks that the Steam client library it talks to carries Valve's digital signature. CorkDeck runs games without the Steam client and cannot provide a library signed by Valve | No. The same applies to other games whose Steam wrapper makes this check |
| Call of Duty: Black Ops (Steam) | Exits without showing a frame | Steam protects it with Custom Executable Generation: the files Steam serves contain a placeholder program, and the real one is built for each owner by Valve's servers and delivered only through the Steam client, which runs Valve's own code on the machine first | No. The same applies to other games that use this protection |
| 3DMark (Steam) | Does not start | Needs the Steam client and a separately installed system-information service; Windows installers do not work in this environment | Not planned |

These are limits of running a game without the Steam client, not faults of a particular console or installation. CorkDeck does not remove or bypass copy protection.

- The underlying cause of SD session closures remains unresolved. Recovery is implemented, but covers may remain missing for that session. Recovery during gameplay is unverified.
- The restart/cancel download dialog was exercised on a console with a download made to fail twice.
- Some screens retain English text or the earlier layout.
- Downloads may need to be resumed from the game page after restarting the app.
- Updating an existing game installation in place is not implemented as a validated transaction.
- Some games crash with `0xC0000005`. Turning off x86 TSO can improve speed in specific games but has caused crashes in others; leave it enabled unless you are checking a particular game.
- Docked behaviour and long sessions have not been validated. Since 0.3 CorkDeck does not set clocks at all; a profile that 0.2 stored in Horizon OC is left for you to reset there.
- Only Microsoft Visual C++ 2015-2022 is added automatically. Older Visual C++ runtimes, DirectX helper libraries, .NET and XNA are named as missing but not installed.
- The Windows components are downloaded from the upstream project's DLL repository, at one fixed revision that this version was tested with. If that repository goes away, a new installation cannot complete First set-up.
- First set-up and a few older screens are in English only.
- Legacy Steam titles need Valve's own `Steam.dll`. It is not in the package; since 0.3b CorkDeck downloads it from Valve's update server the first time such a game is started (not yet exercised on a console).
- Since 0.3b a Steam game is installed from the file sets the account is licensed for, and files of 4 GiB and more are written as multi-part files. The first was checked on a console; no game with a file that large has been installed on one yet.
- Automatic CorkDeck updates are unavailable.

Recorded host checks for 0.3: 108 passed, 1 skipped, none failed. The skipped check needs a native AArch64 machine with FEX's core built for it; the fixtures that run ARM64 code were run under an emulator, without sanitizers. (0.2 recorded 80 passed and 26 failed; those failures were stale fixtures and missing sources on the test host, and are repaired in 0.3.)

0.3 was installed from its package on an empty `switch/wine`: First set-up, the Windows components download, a reboot through the CorkDeck boot entry, Steam sign-in by QR code and the installation of a game from Steam were gone through on a console.
