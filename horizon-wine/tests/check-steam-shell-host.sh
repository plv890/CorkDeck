#!/bin/sh
# CorkDeck's shell with the Steam client behind it, and the start of a program
# added by hand. Synthetic PE/artwork, no Steam account and no network; the
# fixtures are launcher_host's.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
python3 "$root/tools/make-embed.py" "$build/icon.c" wine_nx_icon_any "$root/assets/autorun.jpg" \
    corkdeck_font "$root/assets/fonts/Manrope-SemiBold.ttf"
# What both programs are made of: everything of the shell but launcher.c, which
# the adapter's own test stands in for.
common="$build/icon.c $root/vendor/qrcodegen/qrcodegen.c $root/source/diagnostics/lan_log.c"
for name in launcher_catalog launcher_ui launcher_audio launcher_graphics launcher_forwarder \
            launcher_image launcher_setup setup_boot steamgriddb launcher_svg; do
    common="$common $root/source/$name.c"
done
for file in "$root"/source/steam/*.c "$root"/source/app/*.c "$root"/source/shell/*.c; do
    [ -f "$file" ] && common="$common $file"
done
for target in launcher steam-adapter; do
    if [ "$target" = launcher ]; then
        sources="$root/tests/launcher_host.c $root/source/launcher.c"
    else
        sources="$root/tests/steam_launcher_adapter_host.c"
    fi
    # shellcheck disable=SC2086
    clang -std=gnu11 -Wall -Wextra -Werror -no-pie -g -fsanitize=address,undefined \
        -I "$root/source" $(sdl2-config --cflags) $sources $common \
        $(sdl2-config --libs) -lSDL2_ttf -lpng -lturbojpeg -lcurl -lwslay -lmbedcrypto \
        -lminizip -llzma -lzstd -lz -lcrypto -lpthread -lm -o "$build/$target"
done
mkdir -p "$build/card/sdmc:/switch/wine/logs" "$build/shots"
(cd "$build/card" && "$build/steam-adapter")

# Eight programs added by hand and no account: Downloads from the main menu
# (-) has nothing to show, the library's "Not installed" tab says where the
# account is signed in, and A there opens the sign-in screen, which B leaves.
cat > "$build/script" <<EOF
wait 30
shot $build/shots/home.png
key minus
wait 12
key down
key down
key a
wait 20
shot $build/shots/downloads.png
key b
wait 8
key r
wait 14
key up
key right
key right
key right
wait 14
shot $build/shots/not-installed.png
key a
wait 20
shot $build/shots/steam-sign-in.png
key b
wait 10
key b
wait 8
EOF
(cd "$build/card" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$build/launcher" "${LAUNCHER_FONT:-$root/../fonts/tahoma.ttf}" "$build/script" --carousel-fixture > "$build/result" 2>&1) || { cat "$build/result"; exit 1; }
cat "$build/result"
if [ -n "${STEAM_UI_SHOTS:-}" ]; then mkdir -p "$STEAM_UI_SHOTS"; cp "$build/shots/"*.png "$STEAM_UI_SHOTS/"; fi
grep -q '8 catalog games (8 registered, 8 shown)' "$build/result"
grep -q "launcher returned 0 target ''" "$build/result"
# launcher_host deliberately has no graphics-manager object. Select the Wine
# renderer in the fixtures, so that Play can run without installing DXVK.
# Production defaults and launch code are unchanged.
for settings in "$build/card/sdmc:/switch/wine/drive_c/"*/Game.wine-nx.txt; do
    printf '\nd3d=wine\n' >> "$settings"
done
# Home's first card is the program played last; A opens its page, Play starts it.
cat > "$build/manual-script" <<EOF
wait 30
key a
wait 16
key a
wait 10
EOF
(cd "$build/card" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$build/launcher" "${LAUNCHER_FONT:-$root/../fonts/tahoma.ttf}" "$build/manual-script" > "$build/manual-result" 2>&1) || { cat "$build/manual-result"; exit 1; }
cat "$build/manual-result"
grep -q "launcher returned 1 target 'sdmc:/switch/wine/drive_c/Afterlight/Game.exe'" "$build/manual-result"
echo 'The shell with Steam behind it (Downloads, Not installed, sign-in) and the start of a program added by hand passed.'
