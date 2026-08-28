# Block I/O opcodes were missing from the shared Z80 core

**Date**: 2026-08-18 (ABC802 Milestone 1); written up 2026-08-28
**Area**: `z80core/alu.c`, `z80core/z80.c`
**Severity**: a whole instruction group absent from a core described as
complete, undetected across two working machine targets

## What happened

The first attempt to boot the real ABC802 BASIC II ROM halted almost
immediately on an unimplemented opcode. The ROM's boot code configures its
Z80 DART and CTC using `OTIR`/`OUTI` — and **the entire Z80 block I/O
group was missing** from the shared core: `INI`, `INIR`, `IND`, `INDR`,
`OUTI`, `OTIR`, `OUTD`, `OTDR`.

This core had by then been running two independent machine targets. It had
executed real CP/M software — Turbo Pascal, WordStar, dBASE II, MBASIC,
BDS C — and the whole ABC80 BASIC ROM, and it passed both ZEXALL and
ZEXDOC at 67/67 OK with zero errors.

## Root cause

The instructions were simply never implemented. The interesting question
is not why they were missing but why nothing noticed for so long.

**ZEXALL and ZEXDOC exercise no I/O instructions at all.** They are the
project's primary correctness oracle, and a very good one — they are
exhaustive about arithmetic, flags, and the undocumented behaviors most
emulators get wrong. But they run as CP/M programs on a machine with no
devices, so `IN`, `OUT`, and the block I/O group are entirely outside
their scope. A clean 67/67 says nothing whatsoever about them.

The two existing targets did not close the gap either:

- The **CP/M** target has no emulated devices; its programs reach hardware
  through BDOS calls, never through ports.
- The **ABC80** target does use ports, but that ROM happens to drive its
  PIO with plain `OUT (n),A` — the ordinary, long-implemented form.

So the core had a hole that neither the oracle nor any consumer had ever
put weight on, and the reported confidence — "a complete Z80 core, ZEXALL
clean" — was accurate about what had been tested and overconfident about
what had not.

## Why it survived

Because the coverage gap was **invisible from inside the project's own
success criteria**. Every signal available said the core was complete:
a purpose-built exhaustive exerciser passing, plus two real machines
running real period software. Nothing in that set could distinguish "these
instructions work" from "these instructions have never been executed."

It took a third consumer, with genuinely different hardware — a machine
whose peripheral chips are configured by block-transferring a
register table to a port — to put weight on the hole.

## What changed

- The full block I/O group is implemented in `z80core/alu.c`.
- **`asm/examples/gaps_test.asm` check 7 is the permanent regression
  test.** It writes three bytes from memory to a port with `OTIR`, reads
  them back into a second buffer with `INIR`, and asserts both that the
  buffers match and that `B` reaches 0 with `Z` set — the flag result both
  instructions are actually defined for. It runs on every `make test`.

`gaps_test.asm` exists precisely because ZEXALL has known blind spots; it
already covered I/O ports, `IM`, `RETI`/`RETN`, and the `LD A,I` family
for the same reason. This finding added the group that was still missing
from the list of known gaps — a list that was, by construction, only ever
as complete as the last thing someone thought to check.

## The lesson

**An oracle's authority extends exactly as far as its coverage, and no
further.** ZEXALL is close to definitive about the Z80's arithmetic and
flags — which makes it dangerously easy to hear "ZEXALL passes" as "the
CPU is correct." The honest statement was always "every instruction ZEXALL
exercises is correct," and the difference between those two sentences was
an entire instruction group.

The practical defense is the one already in place and worth continuing:
maintain an explicit, written list of what the primary oracle does *not*
cover, with a hand-written regression test for each entry. That list is
the real measure of confidence — and every time a new consumer finds
something on it, the list was incomplete rather than the core uniquely
unlucky.

This is the same shape as [the ABC802 boot screen
near-miss](2026-08-28-boot-screen-cannot-validate.md): a real, authentic,
high-quality input that happened not to touch the code in question. Found
independently, ten days and one subsystem apart.
