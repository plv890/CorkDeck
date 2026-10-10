#!/bin/sh
# What the host tests need beyond the build mirror, put in place once. Run as
# root inside WSL:
#
#     sh "/mnt/d/My Personal Fork/builds/host_test_env.sh"
#
# - packages: an ARM64 cross compiler and emulator for the fixtures that run
#   ntdll's ARM64 code (tests/arm64_host.py), cmake and ninja for FEX's core;
# - the sources three groups of checks read, at the revisions the project pins:
#   FEX with the Horizon patch, DXVK, and mesa-switch's Nouveau driver.
# Each is skipped when it is already there. Nothing here is part of the NRO.
set -eu
mirror=/opt/fork-build
hw="$mirror/horizon-wine"

need=""
for pair in aarch64-linux-gnu-gcc:gcc-aarch64-linux-gnu aarch64-linux-gnu-g++:g++-aarch64-linux-gnu qemu-aarch64:qemu-user cmake:cmake ninja:ninja-build; do
    command -v "${pair%%:*}" >/dev/null || need="$need ${pair##*:}"
done
[ -f /usr/aarch64-linux-gnu/lib/libc.a ] || need="$need libc6-dev-arm64-cross"
if [ -n "$need" ]; then
    apt-get update -qq
    # shellcheck disable=SC2086
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $need
fi

# fetch FOLDER URL REVISION: a checkout of exactly that commit, without history.
fetch() {
    if [ ! -d "$1/.git" ]; then
        mkdir -p "$1"
        git -C "$1" init -q
        git -C "$1" config core.autocrlf false
        git -C "$1" remote add origin "$2"
    fi
    if [ "$(git -C "$1" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
        git -C "$1" fetch -q --depth=1 ${4:-} origin "$3"
        git -C "$1" checkout -q --detach FETCH_HEAD
    fi
}

# FEX, as horizon-dlls/tools/build-fex.sh takes it, with the Horizon patch.
fex="$hw/toolchains/fex-2609"
fetch "$fex" https://github.com/FEX-Emu/FEX.git "$(sed -n 's/^revision=//p' "$mirror/horizon-dlls/tools/build-fex.sh")"
if ! git -C "$fex" apply --reverse --check "$hw/fex/horizon.patch" 2>/dev/null; then
    [ ! -f "$fex/.git/horizon-applied.patch" ] || git -C "$fex" apply --reverse "$fex/.git/horizon-applied.patch"
    git -C "$fex" apply "$hw/fex/horizon.patch"
fi
cp "$hw/fex/horizon.patch" "$fex/.git/horizon-applied.patch"
git -C "$fex" submodule update -q --init --depth=1 \
    External/fmt External/xxhash External/range-v3 External/unordered_dense \
    External/rpmalloc Source/Common/cpp-optparse

# DXVK, as horizon-dlls/tools/build-dxvk.py takes it; the check reads one file.
fetch "$hw/vendor/dxvk" https://github.com/doitsujin/dxvk.git \
    "$(sed -n "s/^REVISION = '\(.*\)'.*/\1/p" "$mirror/horizon-dlls/tools/dxvk_payload.py")"

# mesa-switch, where tests/check_vulkan_host_import.py looks for it. Only the
# Nouveau driver and the headers are read, so only they are checked out.
mesa="$HOME/mesa-switch"
if [ ! -d "$mesa/.git" ]; then
    mkdir -p "$mesa"
    git -C "$mesa" init -q
    git -C "$mesa" remote add origin https://github.com/danfromtico/mesa-switch.git
    git -C "$mesa" config core.sparseCheckout true
    printf 'src/nouveau/\ninclude/\n' > "$mesa/.git/info/sparse-checkout"
fi
fetch "$mesa" https://github.com/danfromtico/mesa-switch.git "${MESA_SWITCH_REVISION:-d4a00ea0ab3f59afb967cc5d779e4263d237bd77}" --filter=blob:none
echo "host test environment: ready"
