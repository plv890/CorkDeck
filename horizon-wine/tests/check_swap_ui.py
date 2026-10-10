"""The experimental SD swap (WINE_NX_SWAP_POC, off unless a build asks for it).

Its size was chosen in the old Settings menu, which CorkDeck's shell replaced.
There is no screen for it now: a build with the option reads swap-mb from
launcher.txt, where it is written by hand. This keeps the three ends together:
the option reaches every part that pages, the runtime reads the key, and the
launcher neither offers the experiment nor drags its headers along."""
from pathlib import Path

horizon_wine = Path(__file__).resolve().parents[1]
launcher = (horizon_wine / 'source/launcher.c').read_text() + (horizon_wine / 'source/shell/shell.inc').read_text()
runtime = (horizon_wine / 'source/runtime.c').read_text()
cmake = (horizon_wine / 'CMakeLists.txt').read_text()

assert 'option(WINE_NX_SWAP_POC "Build experimental SD-backed guest memory paging" OFF)' in cmake
assert 'target_compile_definitions(wine-nx-runtime PRIVATE WINE_NX_SWAP_POC)' in cmake
assert 'foreach(target wine-horizon-real wine-virtual-real wine-system-real wine-win32u-real)' in cmake
assert 'target_compile_definitions(${target} PRIVATE WINE_NX_SWAP_POC)' in cmake

assert 'launcher_kv_get_int( &kv, "swap-mb", 0 )' in runtime
assert 'swap_file_open( &game_swap, RUNTIME_DIR "/swap-poc", size )' in runtime

for gone in ('SET_SWAP', 'launcher_swap_prepare', 'swap_store_remove', '"swap-mb"', 'launcher_swap.h', 'swap_store.h'):
    assert gone not in launcher, gone
# The keys of the experiment's first form are still taken out of launcher.txt.
assert 'launcher_kv_set( &l->look, "swap-poc-mb", NULL );' in launcher

print('SD swap: the build option reaches the runtime and the paging code; the shell has no screen for it')
