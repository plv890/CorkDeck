#!/bin/sh
set -eu
version=${1:-1}
case "$version" in 1|2) ;; *) echo 'Only test versions 1 and 2' >&2; exit 1;; esac
cd "$(dirname "$0")"
mkdir -p build
prefix=/opt/devkitpro
"$prefix/devkitA64/bin/aarch64-none-elf-gcc" -g -O0 -march=armv8-a \
    -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -DPROBE_VERSION="$version" \
    -I"$prefix/libnx/include" main.c -L"$prefix/libnx/lib" \
    -specs="$prefix/libnx/switch.specs" -lnx -o "build/probe-v$version.elf"
"$prefix/tools/bin/elf2nro" "build/probe-v$version.elf" "build/probe-v$version.nro"
