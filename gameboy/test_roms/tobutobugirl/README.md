# Tobu Tobu Girl

Fetched from <https://github.com/SimonLarsen/tobutobugirl> (`tobu.gb`),
MIT-licensed (`LICENSE`, Copyright (c) 2017 Tangram Games) - found via
<https://hh.gbdev.io/> (gbdev's community Homebrew Hub, backed by
<https://github.com/gbdev/database>, whose `entries/tobutobugirl/game.json`
names this exact repository), same explicit-permissive-license bar
`dmg-acid2/` and `2048-gb/` were already held to before being committed
here.

A real, unmodified, well-known homebrew action game - a "flap to fly"
platformer, originally made for a game jam and widely used across the
Game Boy dev community as a real-game test target. A second real-game
validation ROM alongside `2048-gb` (`gameboy/docs/GAMEBOY_ROADMAP.md`'s
Phase 6), this time an action/platformer rather than a puzzle game -
exercises MBC1 banking (same as `2048-gb`) but a substantially larger
ROM (2Mbit vs. 2048-gb's 256Kbit) with real sprite-heavy scrolling
gameplay.

**Regression coverage** (`make gameboy-tobu-test`): `reference_frame.ppm`
is the rendered title/splash screen (`--frames 60`, confirmed
byte-for-byte deterministic across repeated runs), diffed with a plain
`cmp` against a fresh capture - the same boot-stability regression
`dmg-acid2` already establishes (a static single-frame check, not a
scripted-gameplay one like `2048-gb`'s tile-merge test - this game's
actual controls weren't reverse-engineered as part of adding this).
