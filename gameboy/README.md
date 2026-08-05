# Game Boy emulator - directory layout

See `docs/GAMEBOY_ROADMAP.md` for project status and phases. This file
just covers the two ROM directories and why they're treated differently.

- `src/` - the emulator source itself, completely separate from
  `emu/src/` (the Z80/CP-M emulator elsewhere in this repo). See
  `docs/GAMEBOY_ROADMAP.md` for why this isn't sharing code with the
  Z80 core, at least not yet.

- `test_roms/` - open-source correctness test suites (Blargg's
  `gb-test-roms`, the Mooneye GB test suite, etc.) - the direct
  equivalent of `emu/zexall/` for the Z80 core. Safe to commit once
  fetched, same reasoning `resources/bdsc/upstream/README.md` already
  documents for BDS C: verify the actual license before adding anything
  here, and note where it came from.

- `roms/` - real cartridge dumps, gitignored (`roms/.gitignore`) and
  **never committed, not even to this private repo**. Unlike the CP/M
  side of this project (where a judgment call was already made to keep
  a real dBASE II binary in `cpm_disk/`), commercial Game Boy ROMs are
  Nintendo's copyrighted work, actively and specifically enforced -
  meaningfully different risk than 1980s CP/M software whose publishers
  mostly no longer exist or have released it. Keep your own dumps here
  locally; they'll never leave your machine via this repo.
