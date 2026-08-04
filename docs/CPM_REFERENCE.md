# CP/M 2.2 BDOS/BIOS Reference

A reference for the CP/M 2.2 system-call interface this project's BDOS
emulation (`emu/src/cpm.c`) targets, gathered from the CP/M 2.2 Interface
Guide/Programmer's Guide and the [Seasip CP/M information
archive](https://www.seasip.info/Cpm/) (both describing Digital Research's
original, public CP/M 2.2 specification — not this project's code). This
document is the Phase 3 groundwork item from `docs/ROADMAP.md`: pin down
real BDOS/BIOS semantics before extending `cpm.c` past the two functions
(2, 9) it was bootstrapped with for ZEXALL's narrow console-output needs.

Where this project's own implementation status matters, it's called out
explicitly — see [Implementation status](#implementation-status) at the
end. Everything else describes real CP/M 2.2 behavior, independent of this
codebase.

## The calling convention

A CP/M program requests a BDOS service with `CALL 0005h` (not a real
subroutine — address 5 holds a jump into the actual BDOS entry point,
wherever the BDOS itself is loaded in high memory). On entry:

- **`C`** holds the function number.
- **`DE`** (or just `E` for single-byte parameters) holds the input
  parameter — usually a character, or the address of an FCB or buffer.

On return, results land in **`A`** and/or **`HL`** (`A` is always the low
byte of `HL` for functions that return a 16-bit result, so code that only
cares about the low byte can just check `A`). This matches Intel PL/M's
own calling convention, which CP/M's BDOS interface was deliberately built
to agree with.

`main.c`/`cpm.c` in this project intercept `PC == 0x0005` directly in
`z80_step()` rather than actually executing a jump through BDOS code that
doesn't exist in this emulator's memory image — see [Implementation
status](#implementation-status).

## BDOS function reference (0–40)

| # | Name | Input | Output | Description |
|---|---|---|---|---|
| 0 | `P_TERMCPM` | — | — | Terminate program, return to CCP. |
| 1 | `C_READ` | — | `A`=char | Wait for a console keypress, echo it, return it. |
| 2 | `C_WRITE` | `E`=char | — | Write one character to the console. |
| 3 | `A_READ` | — | `A`=char | Read from the auxiliary (reader) device. |
| 4 | `A_WRITE` | `E`=char | — | Write to the auxiliary (punch) device. |
| 5 | `L_WRITE` | `E`=char | — | Write to the list (printer) device. |
| 6 | `C_RAWIO` | `E`=code | `A`=varies | Direct console I/O: `E`=0FFh polls for a waiting char (0 if none); other `E` values write that character. |
| 7 | Get I/O byte | — | `A`=IOBYTE | Read the IOBYTE (device-assignment byte) from address 0003h. |
| 8 | Set I/O byte | `E`=byte | — | Store a new IOBYTE at address 0003h. |
| 9 | `C_WRITESTR` | `DE`=addr | — | Print a `$`-terminated string. |
| 10 | `C_READSTR` | `DE`=addr | — | Buffered line input into a caller-supplied buffer (byte 0 = max length, byte 1 = actual length written, data follows). |
| 11 | `C_STAT` | — | `A`=status | Console input status: 0 = none waiting, nonzero = a character is ready. |
| 12 | `S_BDOSVER` | — | `B`=type, `A`=version | System/version number. |
| 13 | `DRV_ALLRESET` | — | — | Reset disk system: log out all drives, flush buffers, select drive A. |
| 14 | `DRV_SET` | `E`=drive (0=A:) | `A`=0 or 0FFh | Select the current disk drive. |
| 15 | `F_OPEN` | `DE`=FCB | `A`=0–3 or 0FFh | Open a file (matches the FCB against the directory, fills in `EX`/`S1`/`S2`/`RC`/`AL`). `A`=0FFh means not found. |
| 16 | `F_CLOSE` | `DE`=FCB | `A`=0–3 or 0FFh | Close a file, flushing any buffered writes. |
| 17 | `F_SFIRST` | `DE`=FCB | `A`=0–3 or 0FFh | Find the first directory match (`?` wildcards allowed). |
| 18 | `F_SNEXT` | — | `A`=0–3 or 0FFh | Find the next match after a prior `F_SFIRST`/`F_SNEXT`. |
| 19 | `F_DELETE` | `DE`=FCB | `A`=0–3 or 0FFh | Delete all matching files (wildcards allowed). |
| 20 | `F_READ` | `DE`=FCB | `A`=0 (OK)/1 (EOF)/9 (bad FCB) | Read the next 128-byte sequential record into the DMA buffer. |
| 21 | `F_WRITE` | `DE`=FCB | `A`=0 (OK)/1 (dir full)/2 (disk full) | Write the next 128-byte sequential record from the DMA buffer. |
| 22 | `F_MAKE` | `DE`=FCB | `A`=0–3 or 0FFh | Create a new file (0FFh if the directory is full). |
| 23 | `F_RENAME` | `DE`=FCB (new name at `FCB+16`) | `A`=0–3 or 0FFh | Rename a file. |
| 24 | `DRV_LOGINVEC` | — | `HL`=bitmap | Bitmap of currently logged-in drives (bit 0 = A:). |
| 25 | `DRV_GET` | — | `A`=drive | Current default drive number. |
| 26 | `F_DMAOFF` | `DE`=addr | — | Set the DMA (disk-transfer buffer) address for subsequent read/write calls; defaults to 0080h. |
| 27 | `DRV_ALLOCVEC` | — | `HL`=addr | Address of the current drive's allocation bitmap. |
| 28 | `DRV_SETRO` | — | — | Mark the current drive read-only in software until the next reset. |
| 29 | `DRV_ROVEC` | — | `HL`=bitmap | Bitmap of drives currently software-write-protected. |
| 30 | `F_ATTRIB` | `DE`=FCB | `A`=0–3 or 0FFh | Set file attributes (the `T1'`/`T2'`/`T3'` flag bits — see [FCB](#the-file-control-block-fcb)). |
| 31 | `DRV_DPB` | — | `HL`=addr | Address of the current drive's Disk Parameter Block — see [DPB](#the-disk-parameter-block-dpb). |
| 32 | `F_USERNUM` | `E`=num (0FFh to query) | `A`=num | Set or get the current user number (0–15). |
| 33 | `F_READRAND` | `DE`=FCB | `A`=0 (OK)/1 (EOF)/… | Random-access read using the record number in `R0`–`R2`. |
| 34 | `F_WRITERAND` | `DE`=FCB | `A`=0 (OK)/1/2/… | Random-access write; may create sparse extents. |
| 35 | `F_SIZE` | `DE`=FCB | `R0`–`R2` set | Set the FCB's random-record fields to the file's size in records. |
| 36 | `F_RANDREC` | `DE`=FCB | `R0`–`R2` set | Set the FCB's random-record fields from its current sequential position. |
| 37 | `DRV_RESET` | `DE`=drive bitmap | `A`=0 or 0FFh | Selectively reset drives (clears their software read-only flag). |
| 38 | `DRV_ACCESS` | `DE`=drive bitmap | `A`=0 or 0FFh | Mark drives as having open files, so `DRV_RESET`/login can't disturb them. |
| 39 | `DRV_FREE` | `DE`=drive bitmap | — | Release the locks `DRV_ACCESS` set. |
| 40 | `F_WRITEZF` | `DE`=FCB | `A`=0/1/2/… | Random write with zero-fill for any newly allocated blocks skipped over. |

General error convention: disk/file functions return `A`=0FFh on failure
(file not found, directory full, disk full); success returns a small
non-negative directory code (0–3), not a fixed `0`. Functions 20/21/33/34/
40 use a distinct small set of numeric codes instead (see table).

## The File Control Block (FCB)

A 36-byte structure the caller fills in (mostly just drive + filename +
type) and passes by address in `DE` to any file-oriented BDOS call; BDOS
fills in the rest as the file is opened/read/written.

| Offset | Field | Size | Purpose |
|---|---|---|---|
| `+00h` | `DR` | 1 | Drive: 0 = default, 1–16 = A:–P:. |
| `+01h`–`+08h` | `F1`–`F8` | 8 | Filename, 7-bit ASCII, space-padded. High bit of each byte doubles as an attribute flag when set via `F_ATTRIB`. |
| `+09h`–`+0Bh` | `T1`–`T3` | 3 | Filetype, 7-bit ASCII. High bits: `T1'`=read-only, `T2'`=system/hidden, `T3'`=archive (unchanged since last copy). |
| `+0Ch` | `EX` | 1 | Current extent (a file >16KB spans multiple 16KB "extents"); zero this before `F_OPEN`. |
| `+0Dh` | `S1` | 1 | Reserved (zero it). |
| `+0Eh` | `S2` | 1 | Extent high byte; bit 7 is set internally as a "file written" flag. |
| `+0Fh` | `RC` | 1 | Record count in this extent; zero before `F_OPEN`. |
| `+10h`–`+1Fh` | `AL` | 16 | Allocation vector — which disk blocks belong to this file/extent; filled in by BDOS, not the caller. |
| `+20h` | `CR` | 1 | Current record within the extent, for sequential I/O. |
| `+21h`–`+23h` | `R0`–`R2` | 3 | Random-access record number (16-bit in CP/M 2.2, `R2` used only as an overflow byte). |

A caller only needs to fill in `DR`/`F1`-`F8`/`T1`-`T3` (and zero the rest)
before `F_OPEN`/`F_MAKE`; the rest is BDOS's bookkeeping.

## The BIOS jump table

The BIOS is a 17-entry table of 3-byte `JP` instructions at a
system-dependent base address (`b`), immediately below the CCP/BDOS in
high memory. The BDOS calls through this table for every device/disk
operation instead of touching hardware directly — this is CP/M's
portability boundary: porting CP/M to new hardware means rewriting the
BIOS, not the BDOS.

| Offset | Vector | Entry | Exit | Description |
|---|---|---|---|---|
| `+00h` | `BOOT` | — | — | Cold boot: load CCP/BDOS, initialize hardware. |
| `+03h` | `WBOOT` | — | — | Warm boot: reload the CCP (and BDOS, on some systems). |
| `+06h` | `CONST` | — | `A`=0 (none) / 0FFh (ready) | Poll console input status without blocking. |
| `+09h` | `CONIN` | — | `A`=char | Block until a console character is available. |
| `+0Ch` | `CONOUT` | `C`=char | — | Write a character to the console. |
| `+0Fh` | `LIST` | `C`=char | — | Write a character to the printer (blocks if not ready). |
| `+12h` | `PUNCH` | `C`=char | — | Write to the punch/auxiliary output device. |
| `+15h` | `READER` | — | `A`=char | Read from the tape reader/auxiliary input device (returns `^Z`/26 if none attached). |
| `+18h` | `HOME` | — | — | Move the selected drive's head to track 0. |
| `+1Bh` | `SELDSK` | `C`=drive, `E`=login flag | `HL`=DPH addr or 0 | Select a disk drive; returns the address of its Disk Parameter Header (0 = invalid drive). |
| `+1Eh` | `SETTRK` | `BC`=track | — | Set the track number for the next disk I/O. |
| `+21h` | `SETSEC` | `BC`=sector | — | Set the sector number for the next disk I/O. |
| `+24h` | `SETDMA` | `BC`=addr | — | Set the memory address for the next disk read/write. |
| `+27h` | `READ` | — | `A`=0 (OK)/1 (error)/0FFh (media changed) | Read the currently set track/sector into the DMA address. |
| `+2Ah` | `WRITE` | `C`=deblocking code | `A`=0 (OK)/1 (error)/2 (read-only)/0FFh (media changed) | Write the DMA buffer to the currently set track/sector. |
| `+2Dh` | `LISTST` | — | `A`=0 (not ready) / 0FFh (ready) | Printer status, non-blocking. |
| `+30h` | `SECTRAN` | `BC`=logical sector, `DE`=translate table | `HL`=physical sector | Apply disk sector skewing/interleave. |

### The Disk Parameter Header / Disk Parameter Block

`SELDSK` returns a pointer to a 16-byte **Disk Parameter Header (DPH)**
per drive (translate table address, 3 scratch words the BDOS uses
internally, directory buffer address, **DPB** address, checksum-vector
address, allocation-vector address). The DPH's DPB pointer is what
actually describes the drive's geometry:

| Offset | Field | Size | Purpose |
|---|---|---|---|
| `00h` | `SPT` | 2 | Sectors per track. |
| `02h` | `BSH` | 1 | Block shift factor (log2 of the allocation block size in sectors). |
| `03h` | `BLM` | 1 | Block mask (`2^BSH - 1`). |
| `04h` | `EXM` | 1 | Extent mask (how many logical extents fit in one directory extent, given the block size). |
| `05h` | `DSM` | 2 | Total disk storage, in allocation blocks, minus one. |
| `07h` | `DRM` | 2 | Number of directory entries minus one. |
| `09h` | `AL0`/`AL1` | 2 | Bitmap of which allocation blocks the reserved directory entries occupy. |
| `0Bh` | `CKS` | 2 | Size of the directory checksum vector (0 for a fixed/non-removable disk). |
| `0Dh` | `OFF` | 2 | Number of reserved (system) tracks at the start of the disk. |

This level of detail (DPH/DPB, real disk geometry) only matters once
`cpm.c` emulates actual disk I/O against a CP/M disk image or host
filesystem — the BDOS file functions (15–23, 33–40) can be implemented
against a *host* filesystem first without a literal DPB, by mapping FCB
filenames to host files directly, deferring "real" disk geometry emulation
until (if ever) booting an unmodified CP/M disk image is the goal (see
`docs/ROADMAP.md`'s Phase 3 list).

## Zero-page memory layout

CP/M reserves fixed meanings for the first 256 bytes of the 64KB TPA
(Transient Program Area) — a `.com` program is loaded at 0100h specifically
so this region is available below it:

| Address | Size | Purpose |
|---|---|---|
| `0000h`–`0002h` | 3 | `JP` to `WBOOT` (also how a program locates the BIOS: the target address of this jump, minus BIOS-table offsets, gives the BIOS base). |
| `0003h` | 1 | IOBYTE — logical-to-physical device assignment (rarely used in practice). |
| `0004h` | 1 | Current drive (low nibble) / user number (high nibble). |
| `0005h`–`0007h` | 3 | `JP` to the BDOS entry point — this is the address every BDOS `CALL 0005h` actually jumps through. |
| `0008h`–`003Ah` | — | Intel 8080 restart (`RST`)/interrupt vector space. |
| `005Ch`–`006Bh` | 16 | Default FCB 1, auto-parsed from the command line by the CCP before the program starts. |
| `006Ch`–`007Fh` | 20 | Default FCB 2 (a second command-line filename argument, if any) — overlaps/gets overwritten if FCB 1 is opened, since CP/M assumes most programs only need one file open at a time from the command line. |
| `0080h` | 1 | Command-line tail length, *and* the default DMA buffer address (128 bytes, `0080h`–`00FFh`) if not overridden via `F_DMAOFF`. |
| `0081h`–`00FFh` | 127 | Command-line tail text (space-separated, unparsed) / default DMA buffer contents. |

This confirms the choices this project's `main.c` already made independent
of this research: `.com` programs load at `0100h`. `0000h` gets a real
`JP` into the minimal BIOS jump table `cpm_bios_init()` installs (see the
BIOS section below) rather than just a bare `RET`, since some real
software reads this jump's target to locate the BIOS. `0005h`-`0007h`
similarly gets a real `JP <BDOS_ENTRY>` (`BDOS_ENTRY` in `emu/src/cpm.h`,
not just a bare `RET` at `0005h` like an earlier version of this project
had) — `check_cpm_bdos()` intercepts calls to `0005h` before ever
fetching this, so the instruction itself is never actually executed, but
the *address* stored at `0006h`-`0007h` still matters: real software
sometimes reads it back (`LHLD 6`/`LD HL,(6)`) as a proxy for "how much
TPA is free," and a plausible-looking address there is enough to satisfy
that check even with no real resident BDOS code behind it. Turbo Pascal's
`TINST.COM` terminal-configuration utility is what surfaced this — with
only a bare `RET` at `0005h` (leaving `0006h`-`0007h` at zero), it read
back "no memory available" and immediately printed `Not enough memory` /
`Program aborted`, refusing to start at all.

## Implementation status

`emu/src/cpm.c`'s `check_cpm_bdos()` currently implements **0**
`P_TERMCPM`, console output (**2** `C_WRITE`, **9** `C_WRITESTR`), console
input (**1** `C_READ`, **6** `C_RAWIO`, **10** `C_READSTR`, **11**
`C_STAT`), and **12** `S_BDOSVER` — intercepted directly at `PC == 0x0005`
rather than by placing real BDOS code in memory and executing a real
`CALL`/jump to it. There *is* now a minimal, real BIOS jump table (see
below), and a fake but internally-consistent DPH/DPB (see the Implementation
status note near the end of the File I/O section) — real disk geometry
still isn't emulated, but "how much disk space is free" now gets a
plausible answer instead of whatever garbage happened to be in `HL`.

Console input needs the host terminal in raw mode (character-at-a-time, no
local echo) to behave like real CP/M hardware; `cpm_console_init()`
handles this via `termios`, only when stdin is a real TTY (a piped/
redirected stdin — e.g. `tests/run_tests.sh`'s `console_test.asm` check —
is left alone and just reads via a blocking `read()`, with EOF mapped to
`^Z`/26 so a non-interactive run can't hang forever). Function 6's
poll-and-read (`E`=0FFh) does *not* echo, matching real "raw" I/O; function
10's line editing (echo, backspace/DEL, stop at CR) is deliberately
minimal — no `^R`/`^X`/`^C` line-editing repertoire like real CP/M's CCP
has, just enough for a program to read a line.

Two host-terminal translations turned out to matter once a real program
(Tasty Basic) was actually typed at interactively: `ICRNL` (on by
default) silently rewrites the real `CR` (0x0D) a physical Enter key
sends into `LF` (0x0A) before `read()` ever sees it — CP/M software
written against a genuine raw serial line (where no such translation
happens) can end up ignoring `LF` as noise while waiting for a real `CR`
that never arrives, making Enter look dead; `cpm_console_init()` now
clears `ICRNL`/`INLCR`/`IGNCR` too. Separately, a modern keyboard's
Backspace/Delete key sends `DEL` (0x7F) in raw mode, but CP/M-era software
was written against terminals using the classic `BS` (0x08) erase
convention and often only recognizes that byte — `console_read_char()`
translates `DEL` to `BS` so backspace works without reconfiguring the
host terminal's erase key.

Function 0 (`P_TERMCPM`, "quit to CP/M") needed a two-part fix, not just
setting `cpu->pc = 0x0000`: real CP/M's warm boot never returns to the
caller, but `z80_step()` would still fetch and execute the `RET` stub
`main.c` preloads at address `0x0000` right after `check_cpm_bdos()`
returned, popping the real return address off the stack and undoing the
termination before `main.c`'s own "`PC==0` means done" check at the top of
its *next* loop iteration ever ran. `z80_step()` now checks for `PC==0`
immediately after `check_cpm_bdos()` and returns without fetching/
executing anything in that case — found and fixed while testing Tasty
Basic's `BYE` command, which uses this function to exit.

The file functions are now implemented too: **15** `F_OPEN`, **16**
`F_CLOSE`, **17**/**18** `F_SFIRST`/`F_SNEXT`, **19** `F_DELETE`, **20**/
**21** `F_READ`/`F_WRITE` (sequential), **22** `F_MAKE`, **23**
`F_RENAME`, **26** `F_DMAOFF`, **33**/**34**/**40** `F_READRAND`/
`F_WRITERAND`/`F_WRITEZF`, **35** `F_SIZE`, plus the drive/user
bookkeeping stubs **13** `DRV_ALLRESET`, **14** `DRV_SET`, **25**
`DRV_GET`, **32** `F_USERNUM`. The design question flagged above was
resolved with the simplest option: every drive and user number is
collapsed onto **one host directory** (`cpm_disk/`, created next to
wherever `bin/z80` is invoked from — see `cpm_fileio_init()`), with FCB
names mapped straight onto host filenames (`FOO.TXT` → `cpm_disk/FOO.TXT`,
uppercased, trailing spaces trimmed). Concretely, this means:

- `DRV_SET`/`F_USERNUM` just record a number for `DRV_GET`/`F_USERNUM` to
  echo back — they don't actually change which files are visible. A
  program that writes `1:FOO.TXT` and reads back `2:FOO.TXT` gets the same
  file, since drive/user aren't part of the host path.
- There's no real disk image or block allocation — `F_OPEN`/`F_MAKE`/etc.
  go straight through `fopen`/`fclose`/`remove`/`rename` on the mapped
  directory. There *is* now a fake, static DPH/DPB (`DRV_DPB`/
  `DRV_ALLOCVEC`, BDOS functions 31/27, plus BIOS `SELDSK` — see the
  Implementation status note below), but it doesn't back real block
  allocation: it exists purely to give real software a plausible,
  non-garbage answer when it asks how much disk space is free.
  `F_SFIRST`/`F_SNEXT`'s `'?'`-wildcard matching
  (`fcb_pattern_match()`) and the 32-byte directory-entry image they write
  into the DMA buffer are real, but always report the match in "slot 0"
  of the notional 4-per-record packing real disk directories use, since
  that packing is a real-hardware storage-density detail with no
  equivalent here.
- Sequential I/O (`F_READ`/`F_WRITE`) tracks position via the FCB's real
  `EX`/`CR` fields (one 16KB extent = 128 records), so a program reading a
  file sequentially past 16KB sees `EX` roll over exactly like on real
  CP/M. `F_OPEN` honors a caller-supplied nonzero `EX` (computing `RC`
  relative to that extent's base record) rather than always resetting to
  0 — real CP/M's `F_OPEN` searches the directory for the extent matching
  whatever `EX`/`S1`/`S2` the caller already set, and some real programs
  reposition mid-file this way (a CP/M port of Colossal Cave Adventure's
  own data-file paging is what surfaced this — see `docs/ROADMAP.md`).
  The common `EX==0` case (a fresh, from-the-start open) still resets `CR`
  to 0 as before, since plenty of real programs assume `F_OPEN` does that
  for them. Random I/O (`F_READRAND`/`F_WRITERAND`/`F_WRITEZF`) uses `R0`-`R2`
  as a 24-bit linear record number directly; `F_WRITEZF`'s "zero-fill
  skipped blocks" falls out for free from writing past EOF via `fseek`, so
  it's handled identically to plain random write.
- The open-file table (`open_files[]` in `cpm.c`) is keyed by the FCB's
  own memory address, not a separate handle — matching how CP/M programs
  themselves have no notion of a file descriptor beyond the FCB they
  passed to `F_OPEN`/`F_MAKE`.
- **The fake DPH/DPB** (`DPH_BASE`/`DPB_BASE`/`DIRBUF_BASE`/`ALV_BASE` in
  `cpm.c`, written once by `cpm_bios_init()`): before this existed,
  `DRV_DPB` (31), `DRV_ALLOCVEC` (27), and BIOS `SELDSK` weren't handled
  at all, so a caller got back whatever `HL` already held — not a real
  DPB address, just leftover register content from its own prior code.
  Found live-testing two independent real programs: Turbo Pascal's `D`ir
  command showed `Bytes Remaining On A: 0k` despite writes succeeding
  (see `docs/TURBOPASCAL_REFERENCE.md`) — **confirmed fixed**, now
  reports a plausible `8160k`. dBASE II (a real Ashton-Tate 2.43 binary,
  not yet otherwise documented in this project) printed `Disk is full`
  on `QUIT` for a database it had just written correctly, which looked
  like the same symptom and prompted this fix — but turned out to have
  a *different*, still-open root cause (dBASE reads/writes a file's FCB
  again after already closing it, without reopening, which real CP/M's
  `F_CLOSE` also wouldn't allow; unclear yet whether that's a genuine
  dBASE bug or a real BDOS behavior this project doesn't replicate), so
  this specific message is unaffected by the DPB fix even though the fix
  itself is real and independently confirmed via Turbo Pascal. Built
  with a real, internally-consistent DPB describing a plausible ~8MB
  fixed disk — values
  computed per the actual formulas in the CP/M 2.2 Alteration Guide
  (ch. 6): 4096-byte blocks (`BSH`=5, `BLM`=31), `DSM`=2039 (2040 blocks
  × 4096 = 8,355,840 bytes), `DRM`=1023 (1024 directory entries, so the
  first 8 blocks — all of `AL0` — are reserved for the directory),
  `CKS`=0 and `OFF`=0 since there's no removable-media or reserved-track
  concept here. The allocation vector (`ALV_BASE`) is all zero — nothing
  ever marked "in use" — so free space always reports as the whole fake
  disk, which is representationally fine since this project doesn't
  track real block-level allocation. Not modeling any specific real
  drive, the same spirit as `BDOS_ENTRY`'s "plausible ~61KB of free
  memory" (see the zero-page section above).

`asm/examples/file_test.asm` covers `F_MAKE`/`F_WRITE`/`F_CLOSE`/
`F_RENAME`/`F_OPEN`/`F_READ`/`F_SFIRST`/`F_DELETE` end to end (create,
rename, read back, wildcard-search, delete, confirm gone).

What this design can't do: express CP/M's actual drive-switching or
per-user file areas (two programs on "different drives" see the same
files), or run an unmodified real CP/M disk image with real block-level
storage (the fake DPH/DPB above reports a plausible free-space figure,
but doesn't back real allocation tracking or multiple distinct drives).
Revisit only if something concrete actually needs one of those — most
CP/M-80 transient programs don't.

### BIOS

There's now a real, minimal 17-vector BIOS jump table (`cpm_bios_init()`
in `cpm.c`, called once from `main.c`), at a fixed `BIOS_BASE` (`0xFC00`)
near the top of the 64KB address space, plus a genuine `JP <wboot>` at
address `0x0000` — not just a bare `RET` like before. This exists because
some real CP/M software calls directly into the BIOS instead of going
through BDOS, bypassing its function-number dispatch overhead for
performance; MBASIC (Microsoft's BASIC-80) is a concrete example that
does exactly this for console I/O, and needed this to run at all — before
this existed, its first attempted character output silently ended the
program (see below).

Every one of the 17 vectors is written as a genuine 3-byte `JP <self>` -
not a bare `RET` - specifically because some software (MBASIC again)
doesn't just call the vector directly; it reads the vector's *own jump
target* once at startup (the 2 bytes right after its `JP` opcode) and
self-patches that address into its own code, bypassing the jump table
entirely afterward for speed - a second well-known, standard CP/M
optimization technique. A bare `RET` would give that technique nothing
useful to find; a self-referencing `JP` means it doesn't matter whether a
caller reaches a vector by calling it directly or by reading-then-calling
its target - both land on the identical address, and `check_cpm_bios()`
intercepts either path identically before any fetch/execute happens.

`check_cpm_bios()` gives real behavior to `WBOOT` (warm boot - never
returns to its caller, same as `P_TERMCPM`), `CONST`/`CONIN`/`CONOUT`
(reusing the same host-terminal plumbing as the BDOS console functions -
note `CONOUT` takes its character in `C`, not `E` like BDOS `C_WRITE`),
and sensible fixed responses for `READER` (`^Z`, no reader attached),
`SELDSK` (`HL`=the fake DPH described in the File I/O section above -
every drive number is "valid" here, matching the one-host-directory
design, so this never returns 0), `READ`/`WRITE` (error, no BIOS-level
disk I/O - see the File I/O section above for the BDOS-level equivalent
that *does* work), `LISTST` (never ready, no printer), and `SECTRAN`
(identity, no sector skewing). Every other vector (`BOOT`, `LIST`,
`PUNCH`, `HOME`, `SETTRK`, `SETSEC`, `SETDMA`) is a harmless no-op. Real
CP/M programs that jump directly into the BIOS for genuine disk I/O
still won't work correctly (no real block-level storage backs any of
this), but console-only BIOS use - the common case - now does.

Un-stubbed BDOS drive/allocation-vector functions (24, 28, 29, 37, 38,
39) are the remaining BDOS gap; **27** `DRV_ALLOCVEC` and **31**
`DRV_DPB` are now implemented (see the fake DPH/DPB note in the File I/O
section above).

### CCP (Console Command Processor)

`bin/z80 --ccp <path>` boots a real CP/M CCP (the `A>` shell) instead of
running a single program — see `resources/ccp/` for the genuine,
unmodified Digital Research source this is built from (translated from
its original 8080 mnemonics to Z80, since CP/M predates the Z80) and
`docs/ROADMAP.md`'s "Get a real CP/M 2.2 CCP booting" entry for the full
story of what that took. The CCP is loaded at `CCP_BASE` (`0xE400`,
`cpm.h`) instead of `0x100`, and `check_cpm_bios()`'s `WBOOT` handling
re-enters it there (seeding register `C` from the persisted disk/user
byte at `0x0004` first, matching what a real BIOS's `WBOOT` does) instead
of halting the emulator — the same mechanism a running program's `jp 0`/
BDOS function 0 (`P_TERMCPM`) both already route through. Everything else
the CCP needs — searching the directory for `DIR`, opening/reading a
`.COM` file to run it by name, `TYPE`ing a file's contents — is ordinary
BDOS file I/O this project already implements; no CCP-specific emulator
support was needed beyond the warm-boot re-entry itself. Programs typed
at the `A>` prompt are found via the same host-mapped `cpm_disk/`
directory as everything else (see the File I/O section above) — `DIR`
silently skips any file whose name doesn't fit CP/M's real 8.3 filename
limit, correct CP/M behavior rather than a bug. See
`docs/CCP_REFERENCE.md` for the built-in commands (`DIR`/`ERA`/`TYPE`/
`SAVE`/`REN`/`USER`) and general command-line syntax.
