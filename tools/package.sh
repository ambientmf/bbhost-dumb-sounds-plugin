#!/bin/sh
# Builds the plugin and packs it with its sounds into build/dumb_sounds-<version>.zip,
# laid out for bbhost's data folder (extract into %LOCALAPPDATA%\bbhost\data):
#   plugins/dumb_sounds.dll
#   plugins/dumb_sounds/*.wav (every sound F9 can pick)
#   INSTALL.txt
#   tools/package.sh [BBHOST_SRC]   (or set $BBHOST_SRC)
# The sounds are build/sounds/*.wav, leveled by tools/level-sounds.sh from build/sounds/unleveled
# (hit and headshot fetched by tools/fetch-sounds.sh when missing).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
bbhost=${1:-${BBHOST_SRC:?usage: tools/package.sh BBHOST_SRC (or set BBHOST_SRC)}}
version=$(sed -n 's/.*"Dumb Sounds", "\([0-9.]*\)".*/\1/p' "$here/plugin/dumb_sounds.c")
"$here/tools/build-plugin.sh" "$bbhost"
[ -f "$here/build/sounds/unleveled/hit.wav" ] && [ -f "$here/build/sounds/unleveled/headshot.wav" ] ||
    "$here/tools/fetch-sounds.sh"
"$here/tools/level-sounds.sh"
# The others are hand-made cuts in build/sounds/unleveled; a missing one stops the package.

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/plugins/dumb_sounds"
cp "$here/build/dumb_sounds.dll" "$stage/plugins/"
cp "$here/build/sounds/headshot.wav" "$here/build/sounds/hit.wav" "$stage/plugins/dumb_sounds/"
# Every sound F9 can pick (CHOICES in plugin/dumb_sounds.c).
for f in parry mgs-alert link-fall-down link-hyaa sm64_mario_hoohoo sm64_mario_hurt sm64_mario_oof sm64_mario_whoa \
    xp-error wilhelm goldeneye-pistol-silenced goldeneye-pistol-silenced2 goldeneye-pistol-silenced-shell-casing \
    goldeneye-pistol-loud goldeneye-loud-reload mm_megabuster_pew mmx_shot; do
    if [ -f "$here/build/sounds/$f.wav" ]; then cp "$here/build/sounds/$f.wav" "$stage/plugins/dumb_sounds/"
    else echo "package: no build/sounds/$f.wav (its cut belongs in build/sounds/unleveled)" >&2; exit 1; fi
done
# custom_*.wav stay out: the plugin makes them at its first start, and an
# update extracted over an install then leaves a player's own sounds alone.
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

Install: extract this zip into bbhost's data folder, by default
    %LOCALAPPDATA%\\bbhost\\data
(paste that into Explorer's address bar), so that you have
    %LOCALAPPDATA%\\bbhost\\data\\plugins\\dumb_sounds.dll
    %LOCALAPPDATA%\\bbhost\\data\\plugins\\dumb_sounds\\ (the sounds)
If you set your own data folder (paths.data in bbhost.toml), extract it there.
It stays put when you update bbhost.

Your own sounds: plugins\\dumb_sounds\\sounds.txt (written at the first start)
names the game's sound events and the file each one plays; put a WAV, MP3
or FLAC in that folder and name it there. F9 in game -> Dumb Sounds: the
volume, Reload sounds.txt, and Log sounds (every sound the game plays, to
sounds.log, to find an event's name).

Your own sound for one of them: after the first start, overwrite
custom_hit.wav, custom_headshot.wav, custom_parry.wav, custom_hunter_pistol.wav,
custom_evelyn.wav or custom_beast_roar.wav in plugins\\dumb_sounds with your
WAV (any rate, mono or stereo), pick Custom for it in F9 -> Dumb Sounds, and
press Reload sounds.txt there if the game was running. Updating the plugin
later leaves those files alone.

Uninstall: delete plugins\\dumb_sounds.dll and the plugins\\dumb_sounds folder.
EOF
sed -i 's/$/\r/' "$stage/INSTALL.txt"

out="$here/build/dumb_sounds-$version.zip"
rm -f "$out"
(cd "$stage" && python3 -I -m zipfile -c "$out" INSTALL.txt plugins)
echo "wrote $out"
