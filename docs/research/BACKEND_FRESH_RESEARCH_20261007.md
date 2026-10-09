# Primary-source backend check — 2026-10-07

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


Checked current upstream release and commit metadata, saved in
`../artifacts/backend-research-20261007/fresh-metadata.json`.

* [FEX-2609](https://github.com/FEX-Emu/FEX/releases/tag/FEX-2609), released
  2026-09-08, already matches `horizon-dlls/tools/fex_payload.py` revision
  `395b132f346b1a45def246d10c52245edba1ef02`. The generic DLL manifest's
  short source pin is not this upstream FEX commit. Keep TSO enabled.
  Cortex-A57 does not gain newer ARM instruction features by forcing their
  flags. FEX core execution tests requiring ARM64 cannot be claimed passed
  in the x86_64 WSL environment.
* [Box64 v0.4.4](https://github.com/ptitSeb/box64/releases/tag/v0.4.4),
  released 2026-08-02, is already the pinned version. This integration uses
  the execution core with Autorun's own Windows environment and mappings;
  a Linux distribution's bundled Wine wrappers are not interchangeable.
* [Mesa Switch](https://github.com/danfromtico/mesa-switch) current pin
  `d4a00ea0ab3f59afb967cc5d779e4263d237bd77` already includes Mesa 26.2.3
  and the Horizon shader-cache contention change. The local Switch WSI
  already presents block-linear GPU images directly without a CPU staging
  copy. Do not add another supposed zero-copy rewrite.
* [HayatoG/switch-nvk](https://github.com/HayatoG/switch-nvk) is an older
  Mesa 25.0.7 alternative, with a claimed fast presentation path. Those
  numbers are upstream claims, not measurements in Autorun. Its GPL-2.0-or-
  later source needs a license-compatible integration plan before copying.
* [nxvk switch branch](https://github.com/PalindromicBreadLoaf/nxvk/tree/switch)
  uses Mesa 26.2.2 and has per-driver cache isolation work. Some advertised
  file-descriptor extensions are described as placeholders. It is not a
  demonstrated compatibility/performance upgrade over the current backend.

The next useful experiment is the existing DXVK 3.1.1 option
`dxvk.enableGraphicsPipelineLibrary=True`, which preserves GPL but suppresses
background optimized-pipeline compilation. `Auto` still permits that work;
`False` disables GPL. This may trade GPU efficiency/memory for fewer compiler
stalls, and the driver may fall back to compiling an optimized pipeline on
link failure. Use the actual loaded DXVK build and its source, not a label
from an older handoff. Latest Dishonored logs loaded ordinary DXVK 3.1.1;
Ninja loaded the GPLAsync variant.

Compare on an unchanged scene and caches, pin `dxvk.numCompilerThreads=3`,
and use fixed actual CPU/GPU/memory conditions. Earlier scheduler runs were
confounded by three versus four compiler workers and different scene times.
The restored HOC frequencies invalidate any casual before/after attribution
to thread affinity alone. First resolve repeatable hangs, then measure.
