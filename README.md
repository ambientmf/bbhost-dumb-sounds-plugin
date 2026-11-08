# Dumb Sounds

A meme plugin for [bbhost](https://github.com/ambientmf/bbhost) (Bloodborne
1.09): dumb sounds in place of the game's own.

| Moment | Sounds (F9 > Dumb Sounds) |
|---|---|
| Your bullet hits a body | the Call of Duty hit marker, or Off |
| Your bullet hits a hunter's head | the Hunt: Showdown headshot, or Off |
| An enemy staggered open for a visceral: a gun parry, a heal parry (PvP) or a charged attack to the back (F9's Parry) | Chapelle, the Metal Gear Solid alert, or Off |
| A Hunter Pistol or Evelyn shot | GoldenEye (silenced, silenced 2, silenced with its casing, loud, loud with a reload), Mega Man, Mega Man X, or Off |
| Beast Roar | Link (stumble, hyaa), Mario (hoo-hoo, hurt, oof, whoa), the Windows XP error, the Wilhelm scream, or Off |

Each also has **Custom**: `custom_hit.wav`, `custom_headshot.wav`,
`custom_parry.wav`, `custom_hunter_pistol.wav`, `custom_evelyn.wav` and
`custom_beast_roar.wav` in the plugin's folder, which
the plugin makes at its first start as copies of the first sound, to
overwrite with your own WAV (then Reload sounds.txt in F9 if the game is
running). An update extracted over the plugin leaves them alone. Off leaves the game's own sound.
Each has a volume of its own beside it and a Play button (in game) to hear
what is picked, and a master volume covers them all.

The game plays its sounds by event name (`z006021058`, a bullet on a body).
The plugin hooks that call: for an event one of its rules names, it answers as
the game does with its sound off, so the game's own sound never starts, and
plays its own file through miniaudio on an audio device of its own. The
game's sound files stay as they are.

## Install

Extract the release zip into bbhost's data folder, `%LOCALAPPDATA%\bbhost\data`
by default (or the `paths.data` of your `bbhost.toml`):

```
plugins\dumb_sounds.dll
plugins\dumb_sounds\        the sounds, and sounds.txt once it has run
```

The data folder stays put when bbhost is updated.

## Rules: sounds.txt

The plugin writes `plugins\dumb_sounds\sounds.txt` at its first start: one rule
a line, the first whose event matches is used.

```
NAME  EVENT  FILE  [VOLUME]  [OPTIONS]

shot c000000720                                   # a gunshot (for "shot" rules)
parry       c999999991  parry.wav           priority=3   # the stagger: parries and backstabs
headshot    z006021060  headshot.wav        shot priority=2 mute=s000006001,c999999990
hit         z006021*    hit.wav             shot priority=1 mute=s000006001
beast_roar  s000202002  link-fall-down.wav
```

- `NAME`: one word, shown in `sounds.log`. `parry`, `headshot`, `hit` and
  `beast_roar` take their sound and volume from F9; others their `FILE`.
- `EVENT`: the game's event; a name ending in `*` covers every name it starts.
- `FILE`: a WAV, MP3 or FLAC in that folder, or a full path; `-` silences the
  event with nothing in its place. A file that does not load leaves the
  game's sound.
- `VOLUME`: 0-2, 1 (the default) the file as it is.
- `shot`: only in the second after a gunshot, so an enemy's blow on you, which
  plays the same impact events, keeps its sound.
- `priority=N`: a higher rule's sound cuts off a lower one's from the same
  moment (a parry over the hit marker of its bullet) and blocks it just after.
- `mute=a,b`: these events are silenced just after this one.

F9 > Dumb Sounds > Reload sounds.txt reads it again in game. To find an event's
name, turn on Log sounds there: every event the game plays goes to
`sounds.log`, one line a frame number and a name (`-> name` for one a rule
took). Make the sound in game a few times and look for the name that comes
each time.

## Build

```sh
tools/build-plugin.sh <bbhost checkout>   # build/dumb_sounds.dll (clang, mingw-w64 target)
tools/fetch-sounds.sh                     # build/sounds/unleveled/{hit,headshot}.wav (yt-dlp, ffmpeg)
tools/level-sounds.sh                     # build/sounds/*.wav at the level they play at volume 1
tools/package.sh <bbhost checkout>        # all of the above into build/dumb_sounds-<version>.zip
```

The other sounds are cuts kept in `build/sounds/unleveled/`.

`third_party/miniaudio.h` is [miniaudio](https://miniaud.io) 0.11.22 (public
domain or MIT-0).

`tools/fsb5.py` reads the game's FMOD sound banks (`sound/*.fsb`): it lists
their samples and extracts one as a WAV (`tools/build-at9dec.sh` builds the
ATRAC9 decoder it uses, from a bbhost checkout's LibAtrac9).

## License

GPL-3.0-or-later (`LICENSE`), as a plugin built against bbhost's plugin API.
