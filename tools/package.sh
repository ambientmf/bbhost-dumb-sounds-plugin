#!/bin/sh
# Builds the plugin, its sounds inside it, and packs it into build/dumb_sounds-<version>.zip,
# laid out for bbhost's data folder (extract into %LOCALAPPDATA%\bbhost\data):
#   plugins/dumb_sounds.dll
#   INSTALL.txt
#   tools/package.sh [BBHOST_SRC]   (or set $BBHOST_SRC)
# The sounds are build/sounds/*.wav, leveled by tools/level-sounds.sh from build/sounds/unleveled
# (hit and headshot fetched by tools/fetch-sounds.sh when missing).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
bbhost=${1:-${BBHOST_SRC:?usage: tools/package.sh BBHOST_SRC (or set BBHOST_SRC)}}
version=$(sed -n 's/.*"Dumb Sounds", "\([0-9.]*\)".*/\1/p' "$here/plugin/dumb_sounds.c")
[ -f "$here/build/sounds/unleveled/hit.wav" ] && [ -f "$here/build/sounds/unleveled/headshot.wav" ] ||
    "$here/tools/fetch-sounds.sh"
"$here/tools/level-sounds.sh"
# The others are hand-made cuts in build/sounds/unleveled; a missing one stops the build.
"$here/tools/build-plugin.sh" "$bbhost"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/plugins"
cp "$here/build/dumb_sounds.dll" "$stage/plugins/"
cat > "$stage/INSTALL.txt" <<EOF
Dumb Sounds $version - a bbhost plugin (Bloodborne 1.09)

Dumb sounds in place of Bloodborne's own, each picked in F9 -> Dumb Sounds:

  Your bullet hits a body     the Call of Duty hit marker
  A bullet on a hunter's head the Hunt: Showdown headshot
  A parry or a backstab       Chapelle, or the Metal Gear Solid alert
  Hunter Pistol / Evelyn      GoldenEye (silenced, silenced 2, silenced with
                              its casing, loud, loud with a reload), Mega Man,
                              Mega Man X
  Beast Roar                  Link (stumble, hyaa), Mario (hoo-hoo, hurt, oof,
                              whoa), the Windows XP error, the Wilhelm scream

Each one can also be Custom (your own file, below) or Off (the game's own
sound), has its own volume, and a Play button to hear it (in game). They
play where the game plays its own, quieter with distance (other hunters'
shots too); the hit marker, your feedback, plays as it is.

Needs bbhost with plugin support and the game's 1.09 update.

Install: the plugin is one file, dumb_sounds.dll, its sounds inside it. Put
it in a plugins folder of bbhost's: extract this zip into bbhost's data
folder, by default
    %LOCALAPPDATA%\\bbhost\\data
(paste that into Explorer's address bar), so that you have
    %LOCALAPPDATA%\\bbhost\\data\\plugins\\dumb_sounds.dll
or copy dumb_sounds.dll into the plugins folder beside bbhost.exe. The data
folder stays put when you update bbhost.

The plugin's folder is plugins\\dumb_sounds in the data folder (your own
paths.data in bbhost.toml, if you set one); the plugin makes it at its
first start, with sounds.txt and the custom_*.wav files in it.

Your own sounds: sounds.txt there names the game's sound events and the
file each one plays; put a WAV, MP3 or FLAC in that folder and name it
there. F9 in game -> Dumb Sounds: the volume, Reload sounds.txt, and Log
sounds (every sound the game plays, to sounds.log, to find an event's name).

Your own sound for one of them: after the first start, overwrite
custom_hit.wav, custom_headshot.wav, custom_parry.wav, custom_hunter_pistol.wav,
custom_evelyn.wav or custom_beast_roar.wav in the plugin's folder with your
WAV (any rate, mono or stereo), pick Custom for it in F9 -> Dumb Sounds, and
press Reload sounds.txt there if the game was running. Updating the plugin
leaves those files alone.

Updating from 0.1.4 or older: the sound files of those versions in
plugins\\dumb_sounds (hit.wav, parry.wav, ...) can be deleted; keep
sounds.txt and custom_*.wav.

Uninstall: delete dumb_sounds.dll and the plugins\\dumb_sounds folder.
EOF
sed -i 's/$/\r/' "$stage/INSTALL.txt"

out="$here/build/dumb_sounds-$version.zip"
rm -f "$out"
(cd "$stage" && python3 -I -m zipfile -c "$out" INSTALL.txt plugins)
echo "wrote $out"
