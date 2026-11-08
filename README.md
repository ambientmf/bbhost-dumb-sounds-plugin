# Hitmarker

A meme plugin for [bbhost](https://github.com/ambientmf/bbhost) (Bloodborne
1.09): your own sounds in place of the game's when a bullet lands. By default
a headshot plays the Hunt: Showdown headshot and a bullet's impact plays the
Call of Duty hit marker.

The game plays its sounds by event name (`headshot` is one). The plugin hooks
that call: for an event named in its settings it answers as the game does with
its sound off, so the game's own sound never starts, and plays the plugin's
file through its own audio device. The game's sound files stay as they are.

## Install

1. Build `hitmarker.dll` (below) and put it in bbhost's plugins folder
   (`<config>/plugins`, next to any other plugins you have).
2. Put the sounds in the plugin's data folder, `<data>/plugins/hitmarker/`:
   `headshot.wav` and `hit.wav` (`tools/fetch-sounds.sh` makes both), or any
   WAV, MP3 or FLAC files named in the settings.

## Settings

Under `[hitmarker]` in `bbhost.toml`, or in game with F9; every one applies at once.

| Key | Default | |
|---|---|---|
| `headshot_volume` | `0.35` | 0 to 2 |
| `hit_volume` | `0.25` | 0 to 2 |
| `headshot_sound` | `headshot.wav` | a file in the data folder, or a full path |
| `hit_sound` | `hit.wav` | |
| `headshot_names` | `z006021060` | the game's events the headshot sound replaces, comma-separated: a bullet on a hunter's head |
| `hit_names` | `z006021*` | the events the hit sound replaces: a bullet on a body (walls and the ground are `z006020*`) |
| `mute_names` | | events always silenced |
| `after_hit_names` | `s000006001,c999999990` | events silenced in the few frames after a hit or a headshot: the impact's pop, the game's headshot sound |
| `shot_names` | `c000000720` | a gunshot: hits and headshots count only in the second after one, so an enemy hitting you keeps the game's sound |

A name ending in `*` covers every name it starts.
A headshot wins over a hit: the hit sound of the same moment stops.

## Build

```sh
tools/build-plugin.sh <bbhost checkout>     # build/hitmarker.dll (clang, mingw-w64 target)
tools/fetch-sounds.sh                       # build/sounds/{hit,headshot}.wav (yt-dlp, ffmpeg)
```

`third_party/miniaudio.h` is [miniaudio](https://miniaud.io) 0.11.22 (public
domain or MIT-0).

`tools/fsb5.py` reads the game's FMOD sound banks (`sound/*.fsb`): it lists
their samples and extracts one as a WAV (`tools/build-at9dec.sh` builds the
ATRAC9 decoder it uses, from a bbhost checkout's LibAtrac9).

## License

GPL-3.0-or-later (`LICENSE`), as a plugin built against bbhost's plugin API.
