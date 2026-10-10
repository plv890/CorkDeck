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
- Some legacy Steam titles require a separately supplied `Steam.dll`, absent from the package.
- Automatic CorkDeck updates are unavailable.

Recorded host checks for 0.3: 108 passed, 1 skipped, none failed. The skipped check needs a native AArch64 machine with FEX's core built for it; the fixtures that run ARM64 code were run under an emulator, without sanitizers. (0.2 recorded 80 passed and 26 failed; those failures were stale fixtures and missing sources on the test host, and are repaired in 0.3.)

0.3 was installed from its package on an empty `switch/wine`: First set-up, the Windows components download, a reboot through the CorkDeck boot entry, Steam sign-in by QR code and the installation of a game from Steam were gone through on a console.
