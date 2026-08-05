# Resources

Reference material for this project: links and documents relating to the
Z80 CPU, CP/M, and related topics (assemblers, BDOS/BIOS specs, etc.) —
reading material to inform the work in `emu/`, `asm/`, and future phases
(see `cpm/docs/ROADMAP.md`). `sites/` holds bookmarked web pages (`.webloc`
files) used as reference while working on this project. A few other
subdirectories are an exception to the "reading material" theme:
`tastybasic/`, `sargon/`, `adventure/`, `ccp/`, and `turbopascal/` hold
real, unmodified third-party CP/M programs (source plus a derived,
buildable copy where source is available; a prebuilt binary — sometimes
still with its own `derive.sh`, e.g. `turbopascal/`'s terminal-profile
patch — where it isn't) used for real-world validation testing against
the emulator/assembler — see each directory's own files and
`cpm/docs/ROADMAP.md`'s "Real-world validation"/CCP entries for what that's
found and fixed.
