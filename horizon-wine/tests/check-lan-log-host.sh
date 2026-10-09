#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
    "$root/tests/lan_log_host.c" "$root/source/diagnostics/lan_log.c" -pthread -o "$build/lan"
"$build/lan"
