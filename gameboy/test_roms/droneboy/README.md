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

**A real emulator gap found through this ROM, not a ROM problem**: a
user testing the live GTK front end noticed the Sweep/Square (the two
leftmost) volume faders on Droneboy's own volume page didn't respond
to input, while Wave/Noise did. Traced to Droneboy's own source
(`src/volume.c`'s `updateSweepVolume()`/`updateSquareVolume()`) using
"zombie mode" - a real, pandocs-documented DMG APU quirk (writing
`NRx2` repeatedly with the envelope in increase mode and a period of
zero nudges a channel's *live* volume without retriggering) -
explicitly cited in the ROM's own source comment. This emulator didn't
implement it at all until this was traced (`apu.h`'s own comment had
already flagged it as a known, deliberately-deferred gap - this was
the first time it actually mattered). Now implemented
(`apply_zombie_mode_increment()` in `apu.c`, regression-tested in
`gameboy/tests/test_apu.c`) - the earlier scripted-input attempts that
found "no observable difference" were a separate issue (the `--input`
script's timing didn't actually reach Droneboy's interactive volume
page within the test window), not related to the zombie-mode gap
itself, and still not resolved - scripting real navigation through
Droneboy's UI (SELECT+direction to change pages, per its own manual)
would need correct timing this project hasn't worked out yet.

**Regression coverage** (`make gameboy-droneboy-test`): `reference_audio.wav`
is a 2-second `--wav` capture (comfortably inside the audio window
described above), confirmed byte-for-byte deterministic across repeated
runs, diffed with a plain `cmp` - same reasoning as `2048-gb`'s own
frame-based regression test, just for audio instead of video.
