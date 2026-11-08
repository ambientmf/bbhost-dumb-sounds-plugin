#!/bin/sh
# Downloads the default hit and headshot sounds and writes them as 16-bit
# 48 kHz mono WAVs, the format of the game's own hit sounds (sound/sprj_main.fsb).
#   tools/fetch-sounds.sh [OUT_DIR]
# OUT_DIR: default build/sounds; tools/fsb5.py rebuild swaps them into a bank.
# Needs yt-dlp and ffmpeg on the PATH ($FFMPEG names another ffmpeg).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$here/build/sounds}
ffmpeg=${FFMPEG:-ffmpeg}
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fetch() {  # name url start end: the audible part, in seconds
    yt-dlp -q --no-playlist -f bestaudio -o "$tmp/$1.%(ext)s" "$2"
    "$ffmpeg" -v error -y -ss "$3" -to "$4" -i "$tmp/$1".* -af "afade=t=out:st=$(echo "$4 $3" | awk '{printf "%.3f", $1 - $2 - 0.01}'):d=0.01" -ac 1 -ar 48000 -c:a pcm_s16le "$out/$1.wav"
    echo "wrote $out/$1.wav"
}

fetch hit "https://www.youtube.com/watch?v=HD9xkEv1sGM" 0.340 0.660
fetch headshot "https://www.youtube.com/watch?v=a3AXbqbRHWg" 2.700 3.880
