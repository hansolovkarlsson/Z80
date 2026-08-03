# cpm_disk

A ready-to-run collection of `.com` programs, built from this repo's own
source (`asm/examples/`, `resources/tastybasic/`, `resources/sargon/`)
plus prebuilt third-party binaries (`resources/Mbasic.com`,
`resources/adventure/`) that have no assembleable source. Run any of
them from the **repo root**:

```
bin/z80 cpm_disk/hello.com
bin/z80 cpm_disk/tastybasic.com
bin/z80 cpm_disk/sargon.com
bin/z80 cpm_disk/mbasic.com
bin/z80 cpm_disk/adventure.com
```

Or boot a real CP/M shell instead of a single program — a genuine `A>`
prompt, `DIR`/`TYPE`/`ERA`/`REN`/etc., and the ability to run any program
above just by typing its name:

```
bin/z80 --ccp cpm_disk/ccp.com
```

`DIR` only shows files whose name fits CP/M's real 8-character limit —
`console_test.com`, `tastybasic.com`, `adventure.com`, etc. exceed it and
are silently invisible to the CCP (correct CP/M behavior, not a bug); run
those directly (`bin/z80 cpm_disk/tastybasic.com`) instead.

`adventure.com` needs its data file `Phrogz.din`, also in this directory
— `bin/z80`'s own file I/O maps every drive/user onto a `cpm_disk/`
relative to wherever it's invoked from (see `CLAUDE.md`'s File I/O
section), so running from the repo root as shown above means this
directory *is* that mapped disk, and it just works.

**Careful**: that also means any file I/O a program here actually does —
`SAVE` in Tasty Basic or MBASIC, `file_test.com`'s create/rename/delete
checks — writes into this same tracked directory. `tests/run_tests.sh`
deliberately runs from an isolated throwaway directory instead of here
for exactly that reason (see `CLAUDE.md`). Check `git status` before
committing after playing around, and revert anything that shouldn't be
tracked.

## What's here

- `hello.com`, `selftest.com`, `macro_test.com`, `include_test.com`,
  `rept_test.com`, `gaps_test.com`, `console_test.com`, `file_test.com` —
  this project's own `asm/examples/*.asm` regression-test programs,
  assembled with `bin/z80asm`. Most print `OK`/`FAIL` lines and exit;
  `console_test.com` and `file_test.com` need specific input (see each
  `.asm` file's header comment, or `tests/run_tests.sh` for exactly what
  to pipe in).
- `tastybasic.com` — Tasty Basic, assembled from
  `resources/tastybasic/tastybasic_cpm.asm`. See
  `docs/TASTYBASIC_REFERENCE.md`.
- `sargon.com` — SARGON chess, assembled from
  `resources/sargon/sargon_cpm.asm`.
- `mbasic.com` — Microsoft BASIC-80, a prebuilt binary (no source
  available). See `docs/MBASIC_REFERENCE.md`.
- `adventure.com` + `Phrogz.din` — Colossal Cave Adventure, a prebuilt
  binary + its data file (no source available). See
  `resources/adventure/README.md`.
- `ccp.com` — Digital Research's CP/M 2.2 CCP (shell), assembled from
  `resources/ccp/ccp_cpm.asm`, run with `--ccp` rather than as a plain
  program (see above). See `docs/CPM_REFERENCE.md`'s CCP section.

Regenerate the assembled ones (after any assembler/source change) with:

```
bin/z80asm asm/examples/hello.asm -o cpm_disk/hello.com
bin/z80asm asm/examples/selftest.asm -o cpm_disk/selftest.com
bin/z80asm asm/examples/macro_test.asm -o cpm_disk/macro_test.com
bin/z80asm asm/examples/include_test.asm -o cpm_disk/include_test.com
bin/z80asm asm/examples/rept_test.asm -o cpm_disk/rept_test.com
bin/z80asm asm/examples/gaps_test.asm -o cpm_disk/gaps_test.com
bin/z80asm asm/examples/console_test.asm -o cpm_disk/console_test.com
bin/z80asm asm/examples/file_test.asm -o cpm_disk/file_test.com
bin/z80asm resources/tastybasic/tastybasic_cpm.asm -o cpm_disk/tastybasic.com
bin/z80asm resources/sargon/sargon_cpm.asm -o cpm_disk/sargon.com
bin/z80asm resources/ccp/ccp_cpm.asm -o cpm_disk/ccp.com
```
