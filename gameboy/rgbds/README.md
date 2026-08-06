# RGBDS

The chosen toolchain for any future custom Game Boy test content -
`brew install rgbds` (or your platform's equivalent), version 1.0.3 at
the time this was set up. Not vendored or built from source here; it's
a real, independent, third-party project
(<https://github.com/gbdev/rgbds>), used the same way any of the real
homebrew ROMs already committed under `gameboy/test_roms/` were built.

**Why RGBDS instead of extending this project's own `z80asm`
(`cpm/asm/src/`)**: considered and rejected. `z80asm`'s macro/
preprocessing, expression evaluator, symbol table, and generic
directives are already CPU-agnostic - only its instruction encoder
(`cpm/asm/src/encode.c`) is Z80-specific, so a `CPU Z80`/`CPU GB`
directive selecting between two encoders while sharing the rest was a
real, well-scoped option, genuinely smaller than the CPU-*emulator*
sharing this project already declined (see
`gameboy/docs/GAMEBOY_ROADMAP.md`'s "Architecture decision" section - the
SM83 diverges too much from the Z80 at the dispatch/ALU level for that
one to have been worth it). But RGBDS is already the de facto standard
the entire real Game Boy homebrew scene uses - `2048-gb`, `Tobu Tobu
Girl`, and `Droneboy` (`gameboy/test_roms/`) are all built with RGBDS or
GBDK (which itself builds on RGBDS's assembler) - so adopting it costs
nothing, against real ongoing effort maintaining a second instruction
set inside `z80asm`. Writing test content is a means to an end for this
project (exercising the emulator), not its own mission, unlike the Z80
assembler itself.

**`examples/hello.asm`**: the smallest possible real program, proving
the round-trip (`rgbasm` → `rgblink` → `rgbfix` → this project's own
`bin/gameboy`) actually works, not just that RGBDS itself does - it
emits `"HELLO GAMEBOY"` one character at a time over the serial port
(`SB`/`SC`, `$FF01`/`$FF02`), the same internal-clock-transfer
convention Blargg's own test ROMs use and `gameboy/src/mmu.c`'s serial
hook already captures. `make gameboy-rgbds-test` assembles, links,
fixes, runs it, and greps the output for that exact string - a real
regression check, not just "the build didn't fail". Opt-in, same
external-dependency reasoning as `make gtk`/`make gameboy-gtk`: never
part of `make`/`make test`/`make gameboy-test`, so the default build
stays free of the RGBDS dependency for anyone who doesn't have it
installed.
