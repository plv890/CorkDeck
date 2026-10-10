#!/bin/sh
# Host tests of the application services under source/app: built with ASan and
# UBSan, no SDL and no libnx. Each service is linked only with what its header
# says it needs, so a dependency that crept in fails here first.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc=${CC:-cc}
flags='-std=gnu11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie'

$cc $flags "$root/tests/app_settings.c" "$root/source/app/app_settings.c" -o "$build/app_settings"
"$build/app_settings"
