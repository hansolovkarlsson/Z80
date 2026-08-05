# Handoff Summary — Z80/CP/M Emulator Project

## Core Goal
Maintain and validate a Z80 CPU emulator + CP/M-80 toolchain (`bin/z80` emulator, `bin/z80asm` assembler, `bin/z80dasm` disassembler) written in C, at `/Users/hans/Projects/Z80` (private GitHub repo, branch `main`; moved out of `Documents/Projects` since `Documents` constantly syncs with the cloud). Phase 1 (Z80 core, passes ZEXALL/ZEXDOC) and Phase 2 (assembler/disassembler) are done. Current work is Phase 3: validating the CP/M BDOS/BIOS emulation (`emu/src/cpm.c`) against **real, historical CP/M-80 software**, using bugs found in real programs as the primary way to discover gaps — not synthetic tests. Session convention: find a bug via real software → root-cause it against real reference source (CCP source, terminal specs, etc.) → fix → add a permanent regression test independent of the triggering software → update `CLAUDE.md`/`docs/ROADMAP.md`/`docs/CPM_REFERENCE.md` → commit + push.

## Current Status & Progress

### Fully completed this session (all committed + pushed to `origin/main`)
1. **GTK terminal (`gtk/src/main.c`)**: Fixed a `fdwalk()` stack-overflow crash (capped `RLIMIT_NOFILE` before touching GTK/VTE). A *separate*, still-unfixable intermittent crash was diagnosed as a confirmed macOS 26 OS bug (Apple Developer Forums thread 821081, "xzone" malloc allocator) — not fixable from application code, documented as such.
2. **dBASE II bug** (`emu/src/cpm.c`): `find_or_reopen_file()` extended to sequential `F_READ`/`F_WRITE` was *not* this fix — the original dBASE fix was random I/O (`F_READRAND`/`F_WRITERAND`) on a closed-but-reused FCB, fixing both "Disk is full" and "End of file found unexpectedly". Real Ashton-Tate dBASE II 2.43 binary (`DBASE.COM`/`DBASEOVR.COM`/`DBASEMSG.TXT`) added to `cpm_disk/`.
3. **ADM-3A terminal translation** (`emu/src/cpm.c`, `console_emit()`): dBASE's full-screen editor uses Lear-Siegler ADM-3A cursor codes (`ESC = row col`, `^Z` clear-screen) instead of ANSI. Added a translation state machine.
4. **BDS C auxiliary tools validation** — the main thread of this session, four real bugs found and fixed:
   - **`write_default_fcb()` never expanded `*`→`?`** (`emu/src/main.c`) — found via `LDIR.COM` reporting "no matching members" for a real library.
   - **Sequential `F_READ`/`F_WRITE` didn't tolerate a closed-then-reused FCB** (`emu/src/cpm.c`) — found compiling BDS C's own `L2.C`; `find_or_reopen_file()` extended from random I/O to sequential I/O too.
   - **FCB2 written *after* the command tail instead of before** (`emu/src/main.c`) — a real CP/M memory-overlap quirk (FCB2 at `0x006C`-`0x008F` overlaps the tail buffer at `0x0080`+) implemented backwards, silently truncating the tail on any 2nd+ command-line argument. Found linking `L2.COM` with `l2 t -d`.
   - **VT52/H19 terminal codes untranslated** (`emu/src/cpm.c`) — Edward Ream's RED editor (built from real BDS C source under this project's own toolchain) uses `ESC Y row col`/`ESC K`/`ESC l`, a different protocol from dBASE's ADM-3A. Extended the same translation state machine (renamed `console_adm3a_state`→`console_term_state`).

   **Result**: `LDIR.COM`, `LBREXT.COM`, `UNCRUNCH.COM`, `L2.COM` (built from source), and `RED.COM` (built from 13 real source files) all work correctly under the emulator now.

### Pending / not completed
- **CDB debugger**: compiles and runs, but is **not fully operational**. It requires a binary `.CDB` symbol-file format that `L2 -d` does not produce (that flag writes an unrelated plain-text `.SYM` symbol listing). No grounded primary-source documentation for the real `.CDB` binary layout was available, so implementation was deliberately **not attempted by guessing** — left open, documented honestly in `docs/ROADMAP.md`.
- RED's own source (Edward Ream / Copyright © 1986 Enteleki, Inc. — a *different* copyright holder from Leor Zolman's public-domain BDS C compiler it's bundled with) was **not committed to the repo**, unlike `DBASE.COM`/`CC.COM`/`CLINK.COM`.

## Key Decisions Made
- **Never guess at undocumented binary formats.** When CDB's `.CDB` file format couldn't be grounded in real documentation, the work stopped rather than reverse-engineering/guessing — consistent with this project's strict "grounded in primary sources" rule.
- **Extend existing mechanisms over adding new ones.** The VT52/H19 fix reused and renamed the existing ADM-3A state machine in `console_emit()` rather than adding a parallel translator, since both are structurally identical (cursor-addressing + a couple of single-letter codes).
- **Every fix gets a permanent, software-independent regression test.** File I/O fixes went into `asm/examples/file_test.asm` (checks 6, 7); the terminal-translation fix went into a *new* file `asm/examples/term_test.asm` plus a dedicated raw-byte-grep check in `tests/run_tests.sh`, because (unlike file I/O) there's no CP/M-visible way for a program to read back its own translated console output — an in-program OK/FAIL self-check is impossible for this class of bug.
- **Copyright-aware commits.** dBASE II was committed to `cpm_disk/` (repo is private; Zolman's BDS C compiler itself is public domain since 2002). RED's source was *not* committed, since it carries its own, separate, undocumented-license copyright notice.
- **Root-cause everything against real primary sources**, not modern assumptions: the FCB/tail-ordering fix was confirmed against the actual instruction sequence in `resources/ccp/upstream/ccp.asm`, not inferred from CP/M documentation summaries.

## Files & Paths Touched
- `emu/src/cpm.c` — `find_or_reopen_file()` extended to F_READ/F_WRITE; `console_emit()`/`console_term_state` translation state machine (ADM-3A + VT52/H19).
- `emu/src/main.c` — `write_default_fcb()` wildcard expansion; FCB-before-tail write ordering fix.
- `gtk/src/main.c` — `lower_fd_limit()` fdwalk fix.
- `asm/examples/file_test.asm` — checks 6 (random I/O on closed FCB) and 7 (sequential read resuming on closed FCB).
- `asm/examples/term_test.asm` — new, terminal-translation regression test.
- `tests/run_tests.sh` — new `check_term_test()` function.
- `cpm_disk/DBASE.COM`, `DBASEOVR.COM`, `DBASEMSG.TXT` — added (real dBASE II 2.43 binaries).
- `cpm_disk/filetest.com` — rebuilt binary for file_test.asm.
- `CLAUDE.md`, `docs/ROADMAP.md`, `docs/CPM_REFERENCE.md`, `gtk/README.md` — documentation for all of the above.
- Scratch work (not in repo): `/Users/hans/.claude/jobs/30d21e0d/tmp/bdsc_aux2/` — working directory with the full `bdsc-all.zip` distribution extracted, `L2.COM`/`RED.COM` built from source, `CDB.COM` uncrunched but not runnable.

## Failed/Rejected Approaches
- **Piping `/dev/null` to interactive CP/M programs** (dBASE, `CLINK.COM`, `RED.COM`) doesn't work — they either silently misbehave (dBASE consumed a whole piped script without reacting) or spin forever polling for a keystroke that never comes (CLINK's "type a CRL filename" prompt burned 3+ minutes of CPU before being killed). **Fix**: always drive interactive CP/M programs via a Python `pty.fork()` script with paced keystrokes (~0.15-0.3s between chars), never plain stdin redirection.
- **`timeout` command doesn't exist on macOS.** Use background (`&`) + `sleep` + `kill -0`/`kill -9` polling instead.
- **`LBREXT.COM`'s argument syntax** isn't space-separated like `LDIR.COM`/`CC.COM` — several guesses failed (`/U` flag, comma-joined `*.*,U`, space-separated `T -d`-style) before finding the real syntax: `library=filespec` (equals-joined), with U/O options apparently needing yet another format never fully resolved (worked around by running `UNCRUNCH.COM` as a separate step instead).
- **Assumed BDS C's `main(argc, argv)` reads command-line args the same way `CC.COM`/`CLINK.COM` do** — tested with a small `ARGTEST.C` program and got garbage `argv[0]`/`argc=1`; BDS C's argc/argv convention turned out to be unrelated to the actual bug (the FCB/tail-ordering bug), which was found instead via direct tail-byte dumping with a tiny asm test program (`DUMPTAIL.COM`) — a more reliable diagnostic technique than trusting a compiled C program's runtime behavior.
- **Guessing CDB's `.CDB` binary format** from `L2 -d`'s `.SYM` output or by renaming files — tried copying `T.SYM`→`T.CDB` directly, got `read error on T.CDB` (wrong internal format, confirmed by reading CDB's own `openit()` source in the extracted `BUILD.C`, which expects a 2-byte length-prefixed binary structure `.SYM` doesn't provide). Correctly abandoned rather than reverse-engineered blind.

## Immediate Next Step
No task is currently pending — the last user-directed thread ("finish RED/CDB") is closed out and reported. For the **next session**, pick one:
1. **Resume CDB**: if the real BDS C User's Guide (CDB chapter) becomes available, implement the real `.CDB` binary format and get `CDB.COM` fully operational against `RED.COM` or another built target.
2. **New validation target**: the other previously-discussed option was scoping a **GameBoy emulator** as a new project (bigger, fresh scope, different CPU — LR35902 — not yet started).
3. Just ask the user "what's next?" — this is the natural point to check in, since the immediately-prior concrete task is done.

Repo state: branch `main`, latest commit `123057d`, working tree clean except the user's own untracked scratch files (`resources/ideas.txt` modified, `cpm_disk/CTEST.*`/`ctest.c` untracked) — never touch these, they belong to the user.