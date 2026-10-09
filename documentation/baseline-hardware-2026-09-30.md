# Baseline hardware feedback, 2026-09-30

User tested build nx-amd64-fex-2640 (package 258). Installation, dependency
downloads, launcher UI, game registration and cover loading work. Neither tested
game rendered successfully. This is partial hardware validation, not a passing
game-launch regression test. No comparison with an upstream binary was supplied.

Evidence from user-provided logs:

- `autorun_runtime.log`: low-address RX and RW checks pass; sockets initialize;
  SDL Switch/opengles2 renderer initializes; two catalog entries load.
- `autorun-setup.log`: component registration reports missing DllRegisterServer
  in msdmo.dll and mciqtz32.dll (0x7f), and setup returns 1. Dependency download
  success does not mean all setup steps succeeded. Causality for either game
  is unproven.
- `SuperMeatBoy.log`: x86 PE (0x14c), Box64 selected, DXVK payload 3.1.1.
  Terminates with 0xe06d7363 and returns to launcher. Exception details do not
  identify the original application-level cause; this is not evidence of DRM
  failure or a proven Vulkan fault.
- `Wagotabi.log`: AMD64 PE (0x8664), Box64 selected. DXVK creates a D3D11 device
  at feature level 11_1. Mapping failures and write-permission exceptions appear,
  but logging continues after them. The log ends mid-line during BCrypt loading;
  no conclusive final crash/deadlock record is present. Mono AOT lookup failures
  alone do not establish missing required game files.

The user observed black screen followed by launcher return for Super Meat Boy,
and persistent black screen requiring closure for Wagotabi. Do not claim either
game Playable, attribute this to user settings, or declare the repaired Mesa
fully validated. Both runs precede any Steam implementation.

Execution backend remains outside the Steam feature scope. Steam authentication
can be developed independently of these unresolved compatibility findings.
To distinguish an existing runtime limitation from a build regression, compare
the same game files/settings with an upstream build; this remains untested.
