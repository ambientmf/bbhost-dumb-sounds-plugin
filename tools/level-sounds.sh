#!/bin/sh
# Writes each sound at the level it plays at with its volume at 1:
# build/sounds/unleveled/<name>.wav (as cut or fetched) times its gain below
# -> build/sounds/<name>.wav, 16-bit, the rate and channels kept.
#   tools/level-sounds.sh
# Needs ffmpeg on the PATH ($FFMPEG names another).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
ffmpeg=${FFMPEG:-ffmpeg}
src=$here/build/sounds/unleveled
out=$here/build/sounds
while read -r name gain; do
    [ -n "$name" ] || continue
    if [ ! -f "$src/$name.wav" ]; then
        echo "level: no $src/$name.wav" >&2
        continue
    fi
    "$ffmpeg" -v error -y -i "$src/$name.wav" -af "volume=$gain" -c:a pcm_s16le "$out/$name.wav"
    echo "$name x$gain"
done <<'EOF'
hit 0.25
headshot 0.35
parry 0.5
mgs-alert 0.15
link-fall-down 0.35
sm64_mario_hoohoo 0.21
sm64_mario_hurt 0.175
sm64_mario_oof 0.21
sm64_mario_whoa 0.21
xp-error 0.35
link-hyaa 0.4
goldeneye-silenced-pp7 1
goldeneye-pp7-silenced-with-shell-casing 1
goldeneye-silenced2 1
goldeneye-silenced2-shell-casing 1
goldeneye-loud-reload 1
goldeneye-loud-reload2 1
EOF
