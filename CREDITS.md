# Origins, credits and AI disclosure

## Where CorkDeck comes from

CorkDeck is a fork of [Autorun](https://github.com/autorunhq/autorun), formerly Wine-NX, which is itself a port of [Wine](https://www.winehq.org) to the Nintendo Switch's Horizon OS. Running Windows software on this console at all is the achievement of Autorun's author and contributors. CorkDeck is an independent continuation with a different interface; it makes no claim of authorship over their work, and it is not affiliated with or endorsed by them.

The history of the original project is kept in this repository's commit log, and its introduction in [README.upstream.md](README.upstream.md).

## The fork

**Maj0ra'sMask** ([@plv890](https://github.com/plv890) on GitHub) created CorkDeck and is responsible for its concept, product decisions, systems engineering and testing on hardware.

## Use of AI

The fork's code and documentation were written mainly by AI coding agents (OpenAI Codex / GPT and Anthropic Claude) under the creator's direction. The results were checked with the project's host tests and with runs on a real console; what was and was not verified is recorded in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md). This applies to CorkDeck's own changes only.

## Projects CorkDeck stands on

Wine, FEX, Box64, Mesa and mesa-switch, DXVK, VKD3D-Proton, libnx and devkitPro, Atmosphère, Hekate, Horizon OC, and the other components listed in [NOTICES.md](NOTICES.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The existing `AUTHORS` file, copyright statements and licence files remain authoritative for each of them.

The interface typeface is Manrope, under the SIL Open Font License.

The local Steamworks compatibility library is a limited local implementation built against compatibility headers recorded at GBE revision `7103add7ca6ef3ef8353279219880068f2494fc8`. It is not Valve's library and not a full Steam implementation. Its sources are in [release-support/steamworks-local](release-support/steamworks-local).

## Licences and trademarks

Wine-derived source keeps its LGPL-2.1-or-later licence. Dependencies and bundled components keep their own licences; no blanket replacement licence is applied.

Nintendo Switch, Steam, Steam Deck, SteamOS, Windows and other product names belong to their owners. No endorsement is implied.
