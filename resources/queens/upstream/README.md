# Eight Queens (CP/M, Turbo Pascal 3.01A) - upstream files

From Francesco Sblendorio's [queens-cpm](https://github.com/sblendorio/queens-cpm)
(GPLv2, see `LICENSE`) - a genuine Turbo Pascal 3.01A CP/M-80 program, not
written for or modified by this project.

- `queens.pas` - the real, unmodified source (the repo's only variant;
  unlike `hanoi-cpm` there's no separate machine-specific version here).
  Fetched from `https://raw.githubusercontent.com/sblendorio/queens-cpm/master/source/queens.pas`.
- `queens.com` - the repo's own prebuilt binary. Fetched from
  `https://raw.githubusercontent.com/sblendorio/queens-cpm/master/binary/queens.com`.
- `queens.dat` - the repo's own sample saved-solutions data file (the
  program's own `F)ound solutions` browser needs this to have anything
  to show). Fetched from
  `https://raw.githubusercontent.com/sblendorio/queens-cpm/master/binary/queens.dat`.

**Why the prebuilt binary, not our own compile, unlike `resources/hanoi/`**:
`queens.pas`'s `introscreen` procedure draws its title banner with several
`write(...)` calls whose argument list is *itself* the picture - dozens of
tiny 2-6 character string literals (alternating `_REVERSE`/`_PLAIN`
video-attribute codes) as separate comma-separated arguments, one call
spanning 40-60+ arguments across many source lines. Compiling this
through the real `TURBO.COM` (the same tool `resources/hanoi/derive.sh`
uses successfully on `hanoi-p.pas`) reliably fails partway through with
`Error 99: Compiler overflow` - a genuine Turbo Pascal 3.01A compiler
resource limit, not a syntax error. Confirmed to be a real memory/
workspace limit rather than a fixed table size: with more free TPA
available (varying compiler options and, experimentally, how much this
project's own `BDOS_ENTRY` constant reports as "top of memory" to
probe the effect), the compiler consistently got further into the file
before failing - but even pushed to the safe practical maximum in this
project's own memory map (`BDOS_ENTRY` right up against `BIOS_BASE`,
leaving no real room to go higher), it still only reached line 437 of
735. The real `queens.com` binary must have been compiled on a system
with substantially more free TPA than any realistic CP/M-80 64KB
configuration this project could offer - not something to chase further
here. `queens.pas` is kept for reference/documentation despite not being
directly buildable by this project's own tooling.

The prebuilt `queens.com` itself runs correctly under this project's own
emulator - see `docs/TURBOPASCAL_REFERENCE.md` - which is real validation
of the *runtime* path (BDOS/BIOS, console I/O, file I/O for `queens.dat`)
independent of the compile-time limitation above.
