# cpm_disk

A ready-to-run collection of `.com` programs, built from this repo's own
source (`asm/examples/`, `resources/tastybasic/`, `resources/sargon/`)
plus prebuilt third-party binaries (`resources/Mbasic.com`,
`resources/adventure/`) that have no assembleable source. Every filename
here fits CP/M's real 8.3 limit (8-character base name, 3-character
extension) specifically so `DIR` at the CCP shell (see below) can list
and run every single one of them — no name here is the same as its
`asm/examples/`/`resources/` source's own filename for that reason. Run
any of them from the **repo root**:

```
bin/z80 cpm_disk/hello.com
bin/z80 cpm_disk/tastybas.com
bin/z80 cpm_disk/sargon.com
bin/z80 cpm_disk/mbasic.com
bin/z80 cpm_disk/adventur.com
bin/z80 cpm_disk/turbo.com
```

Or boot a real CP/M shell instead of a single program — a genuine `A>`
prompt, `DIR`/`TYPE`/`ERA`/`REN`/etc., and the ability to run any program
above just by typing its name:

```
bin/z80 --ccp cpm_disk/ccp.com
```

`adventur.com` needs its data file `Phrogz.din`, also in this directory
— `bin/z80`'s own file I/O maps every drive/user onto a `cpm_disk/`
relative to wherever it's invoked from (see `CLAUDE.md`'s File I/O
section), so running from the repo root as shown above means this
directory *is* that mapped disk, and it just works.

**Careful**: that also means any file I/O a program here actually does —
`SAVE` in Tasty Basic or MBASIC, `filetest.com`'s create/rename/delete
checks — writes into this same tracked directory. `tests/run_tests.sh`
deliberately runs from an isolated throwaway directory instead of here
for exactly that reason (see `CLAUDE.md`). Check `git status` before
committing after playing around, and revert anything that shouldn't be
tracked.

## What's here

- `hello.com`, `selftest.com`, `macrotst.com`, `incltest.com`,
  `repttest.com`, `gapstest.com`, `console.com`, `filetest.com` — this
  project's own `asm/examples/*.asm` regression-test programs (`hello`,
  `selftest`, `macro_test`, `include_test`, `rept_test`, `gaps_test`,
  `console_test`, `file_test` respectively), assembled with `bin/z80asm`.
  Most print `OK`/`FAIL` lines and exit; `console.com` and `filetest.com`
  need specific input (see each source `.asm` file's header comment, or
  `tests/run_tests.sh` for exactly what to pipe in).
- `tastybas.com` — Tasty Basic, assembled from
  `resources/tastybasic/tastybasic_cpm.asm`. See
  `docs/TASTYBASIC_REFERENCE.md`.
- `sargon.com` — SARGON chess, assembled from
  `resources/sargon/sargon_cpm.asm`.
- `mbasic.com` — Microsoft BASIC-80, a prebuilt binary (no source
  available). See `docs/MBASIC_REFERENCE.md`.
- `adventur.com` + `Phrogz.din` — Colossal Cave Adventure, a prebuilt
  binary + its data file (no source available; `adventur.com` is this
  upstream binary's own original 8.3-safe filename, unchanged). See
  `resources/adventure/README.md`.
- `ccp.com` — Digital Research's CP/M 2.2 CCP (shell), assembled from
  `resources/ccp/ccp_cpm.asm`, run with `--ccp` rather than as a plain
  program (see above). See `docs/CCP_REFERENCE.md` for its built-in
  commands (`DIR`/`ERA`/`TYPE`/`SAVE`/`REN`/`USER`).
- `turbo.com` + `turbo.msg` + `turbo.ovr` — Borland's Turbo Pascal 3.01A,
  a prebuilt binary (no source available), reconfigured for a real ANSI
  terminal via `resources/turbopascal/derive.sh` (it ships defaulting to
  a "Microbee VDU" terminal profile otherwise). A genuine integrated
  environment — full-screen editor, compiler, and compile-and-run, all
  in one program, not just a compiler. See
  `resources/turbopascal/upstream/README.md`.

Regenerate the assembled ones (after any assembler/source change) with:

```
bin/z80asm asm/examples/hello.asm -o cpm_disk/hello.com
bin/z80asm asm/examples/selftest.asm -o cpm_disk/selftest.com
bin/z80asm asm/examples/macro_test.asm -o cpm_disk/macrotst.com
bin/z80asm asm/examples/include_test.asm -o cpm_disk/incltest.com
bin/z80asm asm/examples/rept_test.asm -o cpm_disk/repttest.com
bin/z80asm asm/examples/gaps_test.asm -o cpm_disk/gapstest.com
bin/z80asm asm/examples/console_test.asm -o cpm_disk/console.com
bin/z80asm asm/examples/file_test.asm -o cpm_disk/filetest.com
bin/z80asm resources/tastybasic/tastybasic_cpm.asm -o cpm_disk/tastybas.com
bin/z80asm resources/sargon/sargon_cpm.asm -o cpm_disk/sargon.com
bin/z80asm resources/ccp/ccp_cpm.asm -o cpm_disk/ccp.com
```

`turbo.com`/`turbo.msg`/`turbo.ovr` aren't assembled from source (no
source available) — regenerate them by re-running
`resources/turbopascal/derive.sh` (which patches
`resources/turbopascal/upstream/TURBO.COM` for an ANSI terminal) and
copying its output into `cpm_disk/`.
