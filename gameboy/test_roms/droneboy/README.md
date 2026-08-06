# Droneboy

Fetched from <https://github.com/purefunktion/Droneboy> (`obj/droneboy.gb`
build), MIT-licensed (`LICENSE`, Copyright (c) 2022 purefunktion) - found
via <https://hh.gbdev.io/> (gbdev's community Homebrew Hub, itself backed
by <https://github.com/gbdev/database>, whose `entries/droneboy/game.json`
names this exact repository), same explicit-permissive-license bar
`dmg-acid2/` and `2048-gb/` were already held to before being committed
here.

A real, unmodified "drone music application for Game Boys" (its own
README's description) - four-channel continuous tones/chords, written
in C with GBDK-2020. Used for `gameboy/docs/GAMEBOY_ROADMAP.md`'s Phase 7
audio testing: unlike `2048-gb` (a single ~0.05s startup blip, then
total silence) this ROM produces real, sustained multi-channel audio
from the moment it boots, without needing any scripted input at all -
a genuinely useful "does live playback actually work" test the way
`dmg-acid2` is for the PPU.

**Honestly reported, not oversold**: this ROM's own README describes
interactive control (volume/duty/frequency, chord sequencing, MIDI via
an Arduinoboy), but scripted joypad presses (`--input`, A/Start/Up at
various frames) produced *no observable difference* in this project's
own testing - the rendered audio is identical with or without them,
and playback goes silent around 5.7 seconds into every run regardless.
Not deeply investigated further (plausibly the real controls need a
specific sequence/hold this project's simple `--input` script format
didn't happen to hit, or genuinely expect the MIDI/Arduinoboy link
rather than plain joypad input) - if that's ever chased down, this
note should be updated rather than silently removed.

**Regression coverage** (`make gameboy-droneboy-test`): `reference_audio.wav`
is a 2-second `--wav` capture (comfortably inside the audio window
described above), confirmed byte-for-byte deterministic across repeated
runs, diffed with a plain `cmp` - same reasoning as `2048-gb`'s own
frame-based regression test, just for audio instead of video.
