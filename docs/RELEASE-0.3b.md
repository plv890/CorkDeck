# CorkDeck 0.3b

A small, quick update made from the first bug reports. Installation and the main features are the same as in [0.3](RELEASE-0.3.md); to update, replace `switch/wine/wine-nx-runtime.nro` with the one from `CorkDeck-0.3b.zip`.

- **Steam downloads that failed at once.** A game lists every file set it has, and an account's licence covers only some of them: add-ons sold separately, a language sold in one region. CorkDeck asked for all of them, and the first refusal ended the download with "License or depot access unavailable" (error 1015). File sets the licence does not cover are now left out, as Steam leaves them. When the refused set held the chosen language, the game is installed in English.
- **Games that installed without a program to start.** File sets Steam marks as optional were never asked for. Some games keep their executables in such a set, so the game was downloaded whole and had nothing to run. Optional sets are now installed when the account's licence covers them.
- **Files of 4 GiB and more.** A game holding such a file was refused (error 3105). The file is now written as a multi-part file of the console's own file system, which programs see as one file, so it does not depend on how the card is formatted.
- **`Steam.dll` for older Steam games.** Games of the first Steam generation load Valve's own `Steam.dll`, which is not CorkDeck's to distribute. It is now downloaded from Valve's update server the first time such a game is started, and checked by SHA-256.

What was checked: the first two changes on a console, with two games that failed with error 1015 on 0.3 and now install, one of them getting its programs from an optional set. The other two were checked on a PC only (the archive reader against Valve's real package); no game with a file over 4 GiB and no game that needs `Steam.dll` was installed on a console for this version. Host checks: 108 passed, 1 skipped, none failed.
