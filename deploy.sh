#!/bin/sh
# Copies the build into the game folder (game must be closed unless devreload.txt is set).
#   game\plugins\mk9access.dll     the port code plugin (needs mk9_plugins = true in mk9.toml)
#   game\MK9Access\               the mod and its data
set -e
cd "$(dirname "$0")"
G="${MK9_GAME:-../game}"
mkdir -p "$G/plugins" "$G/MK9Access"
cp out/plugins/mk9access.dll "$G/plugins/"
# The old version.dll loader is gone (saves + graphics now set at launch, not in-process).
rm -f "$G/version.dll"
cp out/MK9Access/MK9Access.dll out/MK9Access/prism.dll "$G/MK9Access/"; cp -r out/MK9Access/movelists "$G/MK9Access/"
# Described-fatality clips + index (built by tools/gen_fatalities.py).
[ -d out/MK9Access/fatalities ] && cp -r out/MK9Access/fatalities "$G/MK9Access/"
[ -d data ] && cp -r data/. "$G/MK9Access/" 2>/dev/null || true
# Bread-and-butter combo reference (open in a browser with a screen reader).
[ -f "docs/MK9 BnB Combos.html" ] && cp "docs/MK9 BnB Combos.html" "$G/MK9 BnB Combos.html"
# Port updater: the mod starts it hidden at every game start (src/update.c); it downloads a newer
# mk9-lauta while the game runs and installs it when the game closes.
[ -f "dist/mk9update.ps1" ] && cp "dist/mk9update.ps1" "$G/MK9Access/mk9update.ps1"
# Launch wrapper: setup.ps1 writes the settings the mod needs into mk9.toml (plugins on, port banner off, saves in
# the game folder, English, lightest graphics on a first start), then the game starts with --user_data_root
# (the command line wins; the toml is read too late for that one).
cp dist/setup.ps1 "$G/MK9Access/setup.ps1"
cp dist/launch.cmd "$G/MK9 Access launch.cmd"
# The port's own update banner only works with a mouse and its installer fails here: turn it off.
if [ -f "$G/mk9.toml" ]; then
  if grep -q '^[[:space:]]*mk9_update_check[[:space:]]*=' "$G/mk9.toml"; then
    sed -i 's/^[[:space:]]*mk9_update_check[[:space:]]*=.*/mk9_update_check = false  # MK9 Access updates the port itself (MK9Access\\mk9update.ps1)/' "$G/mk9.toml"
  else
    printf '\nmk9_update_check = false  # MK9 Access updates the port itself (MK9Access\\mk9update.ps1)\n' >> "$G/mk9.toml"
  fi
fi
# Plugins are off by default: make sure they are on, or the game ignores the plugin.
if [ -f "$G/mk9.toml" ] && ! grep -q '^[[:space:]]*mk9_plugins[[:space:]]*=[[:space:]]*true' "$G/mk9.toml"; then
  printf '\nmk9_plugins = true  # MK9 Access: load game\\plugins\\*.dll\n' >> "$G/mk9.toml"
  echo "mk9_plugins = true added to mk9.toml"
fi
echo deployed
