# Building the unchanged baseline

Baseline revision: `91107d45eb3204ff80d3637bfa37cefd021a3a1c`.

Steam host checks (Linux/WSL, synthetic data):

```sh
sh horizon-wine/tests/check-steam-host.sh
sh horizon-wine/tests/check-steam-ui-host.sh
sh horizon-wine/tests/check-steam-cdn-host.sh
sh horizon-wine/tests/check-steam-shell-host.sh
```

The CDN check uses an ephemeral local HTTPS server, a locally generated test CA,
and normal certificate/hostname verification. It proves connection reuse on the
host, not Horizon throughput. The shell regression uses upstream synthetic PE
fixtures with the existing Wine renderer because the host harness does not create
a graphics-manager object. It confirms the existing manual Play path returns its
executable without Steam; it does not execute a Windows game on the host.
Status: baseline NRO and ZIP built successfully on 2026-09-30. Upstream package
verification and ZIP integrity checks passed. **Hardware validation is pending**;
the full host test suite also has the failures documented below.

## Host versus target

The target is Nintendo Switch/Horizon, not Linux. Linux/macOS runs the build
tools; devkitA64 cross-compiles the native Switch executable inside Docker.
On Windows, use WSL2 for the upstream POSIX shell scripts and Docker engine.
WSL is not a dependency installed on the Switch and does not replace its runtime.

This workstation uses Ubuntu 22.04 in the existing `AvatarLab` WSL distribution.
Docker and build dependencies were installed there for this task. Its other
application files have not been changed.

```sh
sudo apt-get update
sudo apt-get install docker.io bison flex build-essential pkg-config curl git python3 xz-utils
sudo systemctl start docker
sudo docker info
```

Use a shell authorized to access Docker. That access is effectively root-level
access on the build host; it is not required on the game console.

## Sources and dependencies

Prefer a Linux filesystem checkout with LF line endings and a path without spaces.
Windows Git's automatic CRLF conversion can make `sh build.sh` fail with
`set: Illegal option` before any compilation starts.

```sh
git -c core.autocrlf=false clone https://github.com/autorunhq/autorun.git autorun
cd autorun
git checkout 91107d45eb3204ff80d3637bfa37cefd021a3a1c
git switch -c feature/steam-provider
sh build.sh --jobs 4
```

`build.sh` obtains:

- The exact `horizon-dlls` gitlink revision (do not use submodule `--remote`).
- LLVM-MinGW `20260505`, selecting the Linux x86_64, Linux ARM64 or macOS archive.
- `ghcr.io/autorunhq/switch-dev:2026.09.28.4`, named in
  `horizon-wine/switch-dev.txt`. Record its resolved digest after a successful pull.
- The pinned upstream Atmosphere/Horizon-OC boot payload inputs, followed by the
  existing Wine/Box64/FEX build inputs. No Steam-related runtime upgrades.

The Docker image contains the Switch compiler, libnx, Mesa and other native
dependencies. The script sets `WINE_NX_LLVM_MINGW` and `WINE_NX_JOBS`; it enables
the normal FEX/DXVK/VKD3D package configuration itself. Do not replace component
versions or disable components just to make a Steam build pass.

`--dlls` additionally rebuilds changed Windows DLLs; this is not necessary merely
to obtain the unchanged runtime package. `--x86` selects the optional upstream
x86 runtime; it is not the default baseline. Avoid `--clean` during investigation
unless the generated build directories are expendable.

## Outputs and evidence

The expected native artifact is
`horizon-wine/build-switch-amd64/wine-nx-runtime.nro`; packaging writes
`horizon-wine/build-autorun/autorun-NNN.zip`. The archive contains the SD layout.
The upstream packaging tool validates it against the DLL repository; the Windows
DLL payload is not bundled into this archive.

Before claiming Milestone 0 complete, save the successful build log, image digest,
tool versions, submodule revision, `build-manifest.json`, and SHA-256 of the NRO
and ZIP. Run the upstream package verification. Hardware validation remains a
separate step: launch the baseline, open the existing library and run a previously
working manually added game. Existing logs are under `sdmc:/switch/wine/logs`.

## Current observations

- First attempt: Windows CRLF checkout caused shell failure before compilation.
- Checkout normalization: restore exact upstream bytes for data files that
  intentionally contain CRLF; do not commit blanket line-ending changes.
- Docker 29.1.3 is installed and starts in WSL; the pinned image is downloadable.
- Resolved toolchain image digest:
  `sha256:b0799c2a76177bf0c34a74b6601e4db958fd181800190d46da4402be225d555a`.
- Image inspection: devkitA64 GCC 15.2.0, mbedTLS 2.28.10; curl, libsodium,
  liblzma and zstd are available. Availability alone is not a dependency audit.
- DLL gitlink: `ffb73f3074914f79b04a7c5635b478eaf0ab19e2`.
- Baseline compilation and package verification completed using the repaired
  image described below. No Steam functionality is present in these artifacts.
- Build-only fix: both packaging scripts now honor `WINE_NX_LLVM_MINGW`, already
  exported by `build.sh`, instead of always selecting the macOS compiler path.
  The macOS fallback is preserved. Linux LLVM-MinGW compiled the existing
  `autorun_setup.c` successfully; `llvm-readobj` confirmed COFF-i386 output.
- Existing `launcher_catalog` and `launcher_svg` host tests passed on Linux with
  GCC, AddressSanitizer and UndefinedBehaviorSanitizer. The SVG link requires
  `-lm` on Linux. These checks do not validate Horizon, Steam or the full launcher.
- A build-only copy is being prepared in `/opt/autorun-build` inside WSL to avoid
  cross-filesystem overhead and spaces in the host build path. The editable
  feature checkout remains `D:\autorun fork\autorun`.

### Host test caveats

Linux host-test compilation also needs `-lm` for the SVG test and an explicit
`stdint.h` include in the SteamGridDB client for `SIZE_MAX`. These are build
portability fixes, not changes to the execution pipeline.

Clang 14 ASan PIE test executables intermittently segfaulted before a diagnostic
on this WSL kernel. A local `/usr/bin/clang -no-pie` wrapper passed ten consecutive
SVG runs; it is used only for host tests, never Switch compilation. The expanded
suite then reported 12,113,216 leaked bytes in the existing `tests/osk.c` font
fixture. The leak-checking suite is **not passing**. A separate UI smoke run may
disable leak detection to continue other checks, but cannot close that finding.
That smoke run passed keyboard checks, initialized the SDL dummy/software UI and
selected the expected EXE, but `Run once` returned `0` with an empty target instead
of the expected `1`. Therefore the full launcher smoke suite also remains failing.
No launch/input/runtime changes were made to conceal or fix this unrelated
baseline behavior. Keep this distinction when comparing future Steam changes.

### Pinned image defect

The pinned amd64 image cannot link the baseline NRO: its `libvulkan.a` contains
656 AArch64 and 695 x86-64 ELF members. Link fails at operation 349/351 with
`Relocations in generic ELF (EM: 62)` and `file in wrong format`.

The pinned mesa-switch wrapper initializes `RUST_TARGET` only from
`MESA_SWITCH_RUST_TARGET`; the Linux image build did not set it. Thus Mesa's Rust
NVK members were built for the amd64 build host. A rebuild with an explicit Rust
target exposed a second problem: native procedural macro dependencies were mixed
with target dependencies. That experimental rebuild did not succeed.

`build-support/Dockerfile.switch-dev-fixed` instead uses the nine target Mesa
archives linked by Autorun from the official ARM64 variant of the **same** release,
while retaining the amd64 build tools. Its source image is pinned to
`sha256:7aa4c7d8570d4342c2e18344c02ddb8d3c5d84d0d5003ee373f7e89f384f9213`.
No ARM64 host tool is executed. Every copied archive is checked for nonempty ELF
headers and AArch64-only members; the source `libvulkan.a` has 1543 AArch64 members.
The derived image built successfully. This is a build-image repair using the same
Mesa release, not a driver source change. Linking and hardware behavior still
require independent validation.

```sh
docker build -t autorun/switch-dev:2026.09.28.4-fixed \
  -f build-support/Dockerfile.switch-dev-fixed build-support
export WINE_NX_DEVKIT_IMAGE=autorun/switch-dev:2026.09.28.4-fixed
sh build.sh --jobs 4
```

See `documentation/technical.md` and `horizon-wine/README.md` for upstream build
details. Steam architecture and validation gates are tracked in `STEAM.md`.

The Steam download layer additionally links SDK `switch-liblzma` 5.8.2-1 (0BSD)
and reuses existing Zstandard 1.5.7, zlib and minizip. They are already present in
the pinned image. New modules are listed explicitly in CMake; the execution pipeline
and its component versions are unchanged. Host Steam tests need SDL2/SDL2_ttf,
libcurl, wslay, mbedTLS, zlib, minizip, liblzma and Zstandard development packages.
On Ubuntu: `apt-get install libsdl2-dev libsdl2-ttf-dev libcurl4-openssl-dev
libwslay-dev libmbedtls-dev zlib1g-dev libminizip-dev liblzma-dev libzstd-dev
libpng-dev libturbojpeg0-dev`. The artwork module reuses Autorun's existing PNG/JPEG
decoder and SDL UI; no additional Switch dependency was added.

## Baseline artifact record (2026-09-30)

Artifacts and logs are saved in `../artifacts/baseline/` in this workspace.

- `autorun-258.zip`: SHA-256
  `49bfdc1c1f46f623959fe26bae56425f6e3981c14eadee9e55002c425f228faf`.
- `wine-nx-runtime.nro`: SHA-256
  `93e81142f5639b5f0d967c504d7dca0b61537e3ddae904ab336df1d90d82190b`.
- Derived image ID:
  `sha256:8e7dcd7f344ccadb4ac54f3db025a6688488a514b88c9b7087e31b6c526299bc`.
- Linux build copy HEAD: `485f0b2a1d4c5de3e911d5634bd1881692a43470`, plus
  the `stdint.h`/`-lm` fixes recorded in feature commit `3a3bc576`.
- Logs: `autorun-build-baseline-fixed.log`, `autorun-package-baseline.log`.
- The package's `build-manifest.json` is saved alongside them.
- `package-autorun.py` ran `verify-package.py` successfully, then tested ZIP CRCs.

The final successful invocation used `build-amd64-components.sh` with
`WINE_NX_FEX=1 WINE_NX_DXVK=1 WINE_NX_VKD3D=1 WINE_NX_JOBS=4`, the image override
above, and `WINE_NX_LLVM_MINGW` pointing to the downloaded Linux x86-64 LLVM-MinGW
20260505 directory. Packaging used `python3 horizon-wine/tools/package-autorun.py`
with the same LLVM-MinGW variable. Boot payloads were reused from the completed
baseline boot build. No execution source was modified.

Before adding Steam, validate this baseline on the Switch: open the existing
library and launch one manually added game that works with upstream Autorun.
This specifically checks the toolchain repair's native graphics output.
Test instructions are in `../artifacts/baseline/HARDWARE_TEST.md`.
