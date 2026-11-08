#!/bin/sh
# Builds the plugin and packs it with its sounds into build/hitmarker-<version>.zip,
# laid out for bbhost's data folder (extract into %LOCALAPPDATA%\bbhost\data):
#   plugins/hitmarker.dll
#   plugins/hitmarker/headshot.wav, hit.wav
#   INSTALL.txt
#   tools/package.sh [BBHOST_SRC]   (or set $BBHOST_SRC)
# The sounds are build/sounds/*.wav, fetched by tools/fetch-sounds.sh when missing.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
bbhost=${1:-${BBHOST_SRC:?usage: tools/package.sh BBHOST_SRC (or set BBHOST_SRC)}}
version=$(sed -n 's/.*"Hitmarker", "\([0-9.]*\)".*/\1/p' "$here/plugin/hitmarker.c")
"$here/tools/build-plugin.sh" "$bbhost"
[ -f "$here/build/sounds/hit.wav" ] && [ -f "$here/build/sounds/headshot.wav" ] || "$here/tools/fetch-sounds.sh"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/plugins/hitmarker"
cp "$here/build/hitmarker.dll" "$stage/plugins/"
cp "$here/build/sounds/headshot.wav" "$here/build/sounds/hit.wav" "$stage/plugins/hitmarker/"
cat > "$stage/INSTALL.txt" <<EOF
Hitmarker $version - a bbhost plugin (Bloodborne 1.09)

A headshot on a hunter plays the Hunt: Showdown headshot; a bullet hitting
a body plays the Call of Duty hit marker.

Install: extract this zip into bbhost's data folder, by default
    %LOCALAPPDATA%\\bbhost\\data
(paste that into Explorer's address bar), so that you have
    %LOCALAPPDATA%\\bbhost\\data\\plugins\\hitmarker.dll
    %LOCALAPPDATA%\\bbhost\\data\\plugins\\hitmarker\\headshot.wav and hit.wav
If you set your own data folder (paths.data in bbhost.toml), extract it there.
It stays put when you update bbhost.

Settings: F9 in game -> Hitmarker (volumes, the sound files, which game
sounds they replace). Your own sounds: put a WAV, MP3 or FLAC in the
plugins\\hitmarker folder and name it there.

Uninstall: delete plugins\\hitmarker.dll and the plugins\\hitmarker folder.
EOF
sed -i 's/$/\r/' "$stage/INSTALL.txt"

out="$here/build/hitmarker-$version.zip"
rm -f "$out"
(cd "$stage" && python3 -I -m zipfile -c "$out" INSTALL.txt plugins)
echo "wrote $out"
