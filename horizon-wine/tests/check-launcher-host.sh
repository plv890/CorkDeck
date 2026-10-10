#!/bin/sh
# Host run of the launcher with SDL's dummy video driver: builds it with ASan
# and every warning an error, stages the programs of a built card as symlinks
# under "sdmc:", drives CorkDeck's shell with tests/launcher_host.c's script and
# checks what it wrote and returned. Screenshots go to $LAUNCHER_SHOTS when it
# is set.
# Needs Homebrew's sdl2 (sdl2-compat), sdl3, sdl2_ttf and libpng.
set -eu
export SDL_AUDIODRIVER=dummy
# Homebrew's SDL2 is sdl2-compat, which opens SDL3 by name: somewhere the
# loader looks without being told, which /opt/homebrew/lib is not.
[ -d /opt/homebrew/lib ] && export DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
horizon_wine="$root/horizon-wine"
# Wine's Notepad, the program that stands in for the games under several names,
# is the DLL repository's.
notepad="$root/horizon-dlls/switch/wine/drive_c/windows/syswow64/notepad.exe"
[ -f "$notepad" ] || { echo "no $notepad: check out horizon-dlls" >&2; exit 1; }
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-launcher.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
font="${LAUNCHER_FONT:-/System/Library/Fonts/Supplemental/Arial.ttf}"
shots="${LAUNCHER_SHOTS:-$build}"

# The icons embedded in the launcher must match assets/, and fill as SVG does.
python3 "$horizon_wine/tools/make-launcher-icons.py" --check
clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$horizon_wine/tests/launcher_svg.c" "$horizon_wine/source/launcher_svg.c" -lm -o "$build/launcher_svg"
"$build/launcher_svg"

clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$horizon_wine/tests/launcher_catalog.c" "$horizon_wine/source/launcher_catalog.c" -o "$build/launcher_catalog"

# The three NCAs a forwarder is made of, built here and taken apart again: the
# loader's NPDM comes out of whichever build dir has one, since it is what the
# address space is patched into.
npdm=""
for dir in build-switch-wow64-mesa-switch build-switch-amd64; do
    [ -f "$horizon_wine/$dir/hbl-main.npdm" ] && { npdm="$horizon_wine/$dir/hbl-main.npdm"; break; }
done
if [ -n "$npdm" ]; then
    mkdir -p "$build/switch-shim" "$build/ncas"
    cp "$horizon_wine/tests/switch_shim.h" "$build/switch-shim/switch.h"
    clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I "$horizon_wine/tests" -I "$build/switch-shim" \
        "$horizon_wine/tests/forwarder_build.c" -o "$build/forwarder_build"
    "$build/forwarder_build" "$npdm" "$horizon_wine/assets/autorun.jpg" "$build/ncas"
else
    echo "forwarder: skipped, no hbl-main.npdm in any build directory"
fi
"$build/launcher_catalog"

clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    $(sdl2-config --cflags) "$horizon_wine/tests/launcher_audio.c" $(sdl2-config --libs) -lm -o "$build/launcher_audio"
"$build/launcher_audio"

clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    $(sdl2-config --cflags) -I/opt/homebrew/include \
    "$horizon_wine/tests/launcher_image.c" "$horizon_wine/source/launcher_image.c" \
    $(sdl2-config --libs) -L/opt/homebrew/lib -lpng -lturbojpeg -o "$build/launcher_image"
"$build/launcher_image" "$horizon_wine/assets/logo.png" "$horizon_wine/assets/autorun.jpg"


python3 "$horizon_wine/tools/make-embed.py" "$build/forwarder-icon.c" wine_nx_icon_any "$horizon_wine/assets/autorun.jpg" \
    corkdeck_font "$horizon_wine/assets/fonts/Manrope-SemiBold.ttf"
# Everything the shell is made of, except what tests/launcher_host.c stands in
# for (the DLL manager's screens and the platform): the list tests/shell-stand.sh
# builds for pictures, here with the sanitizers and no warning let through.
sources="$horizon_wine/tests/launcher_host.c $build/forwarder-icon.c $horizon_wine/vendor/qrcodegen/qrcodegen.c $horizon_wine/source/diagnostics/lan_log.c"
for name in launcher launcher_catalog launcher_ui launcher_audio launcher_graphics launcher_forwarder \
            launcher_image launcher_setup setup_boot steamgriddb launcher_svg; do
    sources="$sources $horizon_wine/source/$name.c"
done
for file in "$horizon_wine"/source/steam/*.c "$horizon_wine"/source/app/*.c "$horizon_wine"/source/shell/*.c; do
    [ -f "$file" ] && sources="$sources $file"
done
# shellcheck disable=SC2086
clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$horizon_wine/source" $(sdl2-config --cflags) -I/opt/homebrew/include -I/opt/homebrew/opt/openssl/include \
    $sources $(sdl2-config --libs) -L/opt/homebrew/lib -lSDL2_ttf -lpng -lturbojpeg -lcurl -lwslay -lmbedcrypto \
    -lminizip -llzma -lzstd -lz -L/opt/homebrew/opt/openssl/lib -lcrypto -lpthread -lm -o "$build/launcher_host"
# sdl2-compat looks for SDL3 next to the program, not in Homebrew's lib folder.
[ ! -f /opt/homebrew/lib/libSDL3.0.dylib ] || ln -s /opt/homebrew/lib/libSDL3.0.dylib "$build/libSDL3.dylib"

# The floating keyboard: what its buttons and taps send, and its picture.
clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$horizon_wine/source" $(sdl2-config --cflags) -I/opt/homebrew/include \
    "$horizon_wine/tests/osk.c" "$horizon_wine/source/osk.c" \
    $(sdl2-config --libs) -L/opt/homebrew/lib -lSDL2_ttf -lpng -o "$build/osk"
"$build/osk" "$font" "$shots"


card="$build/card/sdmc:"
mkdir -p "$card/switch/wine/drive_c/openttd" "$card/games/deep/er/still"
ln -s "$notepad" "$card/switch/wine/drive_c/"
ln -s "$notepad" "$card/switch/wine/drive_c/7zr.exe"
ln -s "$notepad" "$card/switch/wine/drive_c/openttd/openttd.exe"
ln -s "$notepad" "$card/games/deep/er/still/Deep.exe"
catalog="$card/switch/wine/launcher-library-v2.ini"

# One run of the shell: shell NAME FOLDER [arguments of launcher_host]. The
# script is $build/NAME.txt and what the run said goes to $build/NAME-out.txt.
shell() {
    name=$1 folder=$2
    shift 2
    ( cd "$folder" && SDL_VIDEODRIVER=dummy "$build/launcher_host" "$font" "$build/$name.txt" "$@" \
        > "$build/$name-out.txt" 2>&1 ) || { cat "$build/$name-out.txt"; exit 1; }
}
# said NAME TEXT: the run's output has the line.
said() {
    grep -qF -- "$2" "$build/$1-out.txt" || { cat "$build/$1-out.txt"; echo "missing: $2" >&2; exit 1; }
}

# Programs are added by hand: a card with several of them and no catalog shows
# an empty library. Library (R), Actions (X), "Add a program from the card",
# then the browser: the card, the folder, the program, and Add in the review.
# Adding is not starting.
cat > "$build/add.txt" <<SCRIPT
wait 30
shot $shots/library-empty.png
key r
wait 12
key x
wait 10
key a
wait 10
key a
wait 8
key a
wait 8
key a
wait 8
key a
wait 12
shot $shots/library-added.png
SCRIPT
shell add "$build/card"
said add "[LAUNCHER] 0 catalog games (0 registered, 0 shown)"
said add "[LAUNCHER] Added sdmc:/switch/wine/drive_c/openttd/openttd.exe to the library"
said add "launcher returned 0 target ''"
grep -q '^version=3$' "$catalog"
grep -q '^path=sdmc:/switch/wine/drive_c/openttd/openttd.exe$' "$catalog"
test "$(grep -c '^\[game ' "$catalog")" -eq 1
echo "launcher host run: an empty library, a program added from the card and not started"

# The host has no DXVK to download; Wine's own renderer needs nothing.
printf 'd3d=wine\n' > "$card/switch/wine/drive_c/openttd/openttd.wine-nx.txt"

# From the saved catalog: with nothing played yet the shell opens on the
# library; A on the card opens the application's page, and Play there starts
# it. The other programs on the card stay out of the library.
cat > "$build/play.txt" <<SCRIPT
wait 30
shot $shots/library-one.png
key a
wait 16
shot $shots/application-page.png
key a
wait 10
SCRIPT
shell play "$build/card"
said play "[LAUNCHER] 1 catalog games (1 registered, 1 shown)"
said play "launcher returned 1 target 'sdmc:/switch/wine/drive_c/openttd/openttd.exe'"
test "$(grep -c '^\[game ' "$catalog")" -eq 1

# What was played is on Home, first.
cat > "$build/home.txt" <<SCRIPT
wait 30
shot $shots/home-played.png
key a
wait 16
key a
wait 10
SCRIPT
shell home "$build/card"
said home "launcher returned 1 target 'sdmc:/switch/wine/drive_c/openttd/openttd.exe'"
echo "launcher host run: the catalog is kept, the application's page starts it, Home has what was played"

# Eight played programs and a walk through the screens around them: the main
# menu (-), the quick panel (+), the library, an application's page, its
# settings and its management list, and back out of each.
cat > "$build/walk.txt" <<SCRIPT
wait 35
shot $shots/home.png
key right
key right
wait 10
key minus
wait 12
shot $shots/main-menu.png
key b
wait 8
key plus
wait 12
shot $shots/quick-panel.png
key b
wait 8
key r
wait 14
shot $shots/library.png
key down
key right
wait 8
key a
wait 16
shot $shots/page.png
key right
key right
key a
wait 12
shot $shots/page-settings.png
key down
key down
wait 6
key b
wait 8
key x
wait 12
shot $shots/page-manage.png
key b
wait 8
key b
wait 8
SCRIPT
shell walk "$build/card" --carousel-fixture
said walk "[LAUNCHER] 8 catalog games (8 registered, 8 shown)"
said walk "launcher returned 0 target ''"
echo "launcher host run: Home, main menu, quick panel, library, page, settings and management passed"

# A library larger than a screen, with the buttons coming while the icon worker
# is still decoding covers and posting an event for each one: every game is
# loaded, the list scrolls to its end and back, and nothing is lost or leaked.
big="$build/big/sdmc:"
mkdir -p "$big/switch/wine"
python3 "$horizon_wine/tests/launcher_shot.py" stage "$big" "$notepad" 120
{
    echo "wait 18"
    for step in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
        echo "key down"; echo "wait 2"
    done
    echo "wait 30"
    echo "shot $shots/library-scrolled.png"
    for step in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
        echo "key up"; echo "wait 2"
    done
    echo "wait 20"
} > "$build/scroll.txt"
shell scroll "$build/big"
said scroll "[LAUNCHER] 120 catalog games (120 registered, 120 shown)"
said scroll "launcher returned 0 target ''"
echo "launcher host run: a library of 120 is loaded and scrolled while its covers are still being decoded"
