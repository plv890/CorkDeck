# CorkDeck 0.3c

A second quick update made from a bug report. Installation and the main features are the same as in [0.3](RELEASE-0.3.md); to update, replace `switch/wine/wine-nx-runtime.nro` with the one from `CorkDeck-0.3c.zip`. It includes everything in [0.3b](RELEASE-0.3b.md).

- **Steam downloads that failed at once with error 3004 ("No supported official HTTPS CDN server or token").** Steam names two kinds of content servers the Steam client downloads from: Valve's own and those of contracted networks. CorkDeck took only the second kind, and only under one domain. Many regions are given nothing but Valve's own servers, so there every game failed before anything was downloaded. Both kinds are used now, and a server entry CorkDeck cannot use is passed over instead of spoiling the whole list.

What was checked: the cause was read from a user's failure log and confirmed against Steam's public directory of content servers, where about half of the regions asked returned only the kind that was refused. A download with 0.3c was run on a console in a region that has both kinds. It could not be run in an affected region; a report from one is welcome. Host checks: 108 passed, 1 skipped, none failed.
