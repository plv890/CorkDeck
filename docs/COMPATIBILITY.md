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
- The restart/cancel download dialog has host-test coverage; its final layout was not exercised on the console.
- Some screens retain English text or the earlier layout.
- Downloads may need to be resumed from the game page after restarting the app.
- Updating an existing game installation in place is not implemented as a validated transaction.
- Some games crash with `0xC0000005`. Turning off x86 TSO can improve speed in specific games but has caused crashes in others; leave it enabled unless you are checking a particular game.
- Long sessions with clock profiles and docked behaviour have not been validated. Previous clock settings are restored when the shell starts again.
- Some legacy Steam titles require a separately supplied `Steam.dll`, absent from the package.
- Automatic CorkDeck updates are unavailable.

Recorded host checks for 0.2: 80 passed, 26 failed. The checkpoint records the same failures as the earlier baseline and attributes them to the host environment. This is not a fully passing test suite.
