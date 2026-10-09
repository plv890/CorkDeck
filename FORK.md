# Fork notice

This repository is an independent fork of Autorun (formerly Wine-NX), itself a
fork of Wine. It is developed separately from the original project and is not
affiliated with or endorsed by it.

| | |
| --- | --- |
| Forked from | `https://github.com/autorunhq/autorun.git` |
| Fork point | commit `dcdec975` on branch `feature/remote-switch-broker`, plus the working tree recorded as commit `3732f8fb` |
| Baseline tag | `fork-baseline-20261008-19` (equals native build 2026.10.08-19) |
| Fork date | 2026-10-08 |
| Development branch | `fork/main` |

## What the fork changes

The fork keeps the runtime that executes Windows programs on Horizon (Wine,
FEX/Box64, Mesa, DXVK/VKD3D and the Horizon layer) and restructures the
application around it: explicit services for the catalogue, launch planning,
settings, dependencies, system maintenance, Steam and diagnostics, and a new
shell on top of them. Changes made after the baseline tag are the fork's own.

## Licensing

Wine and the Horizon changes to it remain under the LGPL-2.1-or-later; see
`COPYING.LIB`, `LICENSE`, `NOTICES.md` and `THIRD_PARTY_NOTICES.md`, which are
kept as they were. Files added by the fork carry the same licence unless they
say otherwise. The original authors are listed in `AUTHORS` and in the
application's credits screen.

## Where it is published

The fork is published at `https://github.com/plv890/CorkDeck`. The public
repository holds the original project's history up to the fork point and, on
top of it, one commit for each published CorkDeck version. Nothing is pushed to
the original project.
