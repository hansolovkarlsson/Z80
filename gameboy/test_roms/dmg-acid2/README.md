# dmg-acid2

Fetched from <https://github.com/mattcurrie/dmg-acid2> (v1.0 release
ROM + `img/reference-dmg.png`), MIT-licensed (`LICENSE`, Copyright (c)
2020 Matt Currie) - unlike Blargg's `cpu_instrs`/`instr_timing` (used
locally during Phase 1/2 but never committed - see
`gameboy/docs/GAMEBOY_ROADMAP.md`'s licensing note), this one has an explicit
permissive license, so it's safe to keep here.

The standard PPU correctness test in the Game Boy dev community - see
`make gameboy-visual-test` (`gameboy/tests/compare_frame.py`) to run it
and compare the result against `reference-dmg.png` pixel-for-pixel.

Per the ROM's own README, it depends on `LY`=`LYC` coincidence
interrupts to perform several mid-frame register writes (window `WX`
toggling, `LCDC` bit 0 toggling to hide/show hair, etc.) - a full,
pixel-perfect pass needs Phase 4's interrupt dispatch, not implemented
as of Phase 3. See `gameboy/docs/GAMEBOY_ROADMAP.md`'s Phase 3 status for the
current match percentage and root-cause breakdown.
