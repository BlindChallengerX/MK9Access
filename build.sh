#!/bin/sh
# Builds MK9Access with the 64-bit w64devkit. Output in out/:
#   out/plugins/mk9access.dll      the port code plugin (loaded from game\plugins\, mk9_plugins = true)
#   out/MK9Access/MK9Access.dll    the mod
set -e
cd "$(dirname "$0")"
W64DEVKIT="${W64DEVKIT:-$HOME/tools/w64x64/w64devkit}"
export PATH="$W64DEVKIT/bin:$PATH"
MH=third_party/minhook
# The updater reads the mod version from both places (MOD_VERSION on its command line, the plugin's
# VERSIONINFO when run by hand): they must match.
V=$(sed -n 's/^#define MOD_VERSION "\(.*\)"/\1/p' src/access.h)
grep -q "\"ProductVersion\", \"$V\"" src/plugin.rc || { echo "src/plugin.rc version is not $V (MOD_VERSION)"; exit 1; }
mkdir -p out/MK9Access out/plugins
# Honest VERSIONINFO so Defender's ML heuristic does not flag the small DLL
# (the proxy-DLL FP, Bearfoos.B!ml, that hit the old version.dll loader). Keep it.
windres src/plugin.rc -O coff -o out/plugin_res.o
gcc -O2 -Wall -Wextra -shared -s -o out/plugins/mk9access.dll src/plugin.c out/plugin_res.o -static-libgcc
gcc -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type -shared -o out/MK9Access/MK9Access.dll \
  src/dllmain.c src/speech.c src/guest.c src/pad.c src/imgui.c src/health.c src/movelist.c src/game.c src/menu.c src/audio.c src/fight.c src/fatality.c src/sysdialog.c src/update.c src/minigame.c src/keys.c src/story.c src/a11ymenu.c src/towerlight.c \
  $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c \
  -I$MH/include -Ithird_party/prism -Isrc -static-libgcc -lole32 -luuid -lsapi -lwinmm -lversion
cp third_party/prism/prism.dll out/MK9Access/
mkdir -p out/MK9Access/movelists && cp data/movelists/*.txt out/MK9Access/movelists/
echo built
