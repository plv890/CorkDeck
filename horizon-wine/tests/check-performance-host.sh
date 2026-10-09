#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-pie -no-pie -pthread \
  "$root/source/performance.c" "$root/tests/performance_host.c" -o "$build/check"
"$build/check"
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-pie -no-pie -pthread \
  "$root/source/performance.c" "$root/tests/performance_wine_host.c" -o "$build/wine"
"$build/wine"
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-pie -no-pie -pthread \
  "$root/tests/performance_rotation_host.c" -o "$build/rotation"
"$build/rotation"
