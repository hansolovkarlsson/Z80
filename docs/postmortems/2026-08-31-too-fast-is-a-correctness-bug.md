# Too fast is a correctness bug

**Date:** 2026-08-31
**Target:** ABC802 (cassette on SIO channel B)
**One-line lesson:** For a device the guest services by interrupt, the
*gap between* events is part of the contract — delivering data as fast as
the host allows is not "no timing model", it is the wrong timing model.

## What happened

`LOAD "CAS:name"` read a recording back byte-for-byte correctly and then
rejected it with `Error 35`, "CRC or address-mark error". The received
bytes were verified against the file and matched exactly; the recording
was verified to be everything the ROM had transmitted.

The receiver was being handed one byte per emulated instruction.

## Root cause

The ROM collects cassette bytes in an interrupt handler, but the routine
that advances the record state — noticing the 256-byte block has ended,
taking the `0x03` end mark and the two checksum bytes — runs in the
**mainline**, between interrupts.

With a byte always waiting, there was effectively no "between". Each
`RETI` was followed immediately by another interrupt, and the mainline got
a handful of instructions per byte. The first record survived that; the
second stopped after its 256 data bytes, never took its checksum, and
reported the error.

Slowing delivery to one byte per 500-25,000 T-states makes it work. The
range is 50x wide, so this is not a tuned constant — it is simply the
difference between "the guest gets to run" and "it does not".

## Why it survived

**Nothing else in this emulator wants to be slow.** Every other decision
here is "go as fast as the host allows, correctness does not depend on
wall time". The Z80 core, the video decode, the floppy card — all of them
are pure functions of state, and running them a thousand times faster than
real changes nothing. That habit is correct almost everywhere, and it is
exactly what made this invisible.

**The floppy card had already worked this way and got away with it.**
`abcbus/disk.c` answers a bus command the instant it is issued, with no
delay, and real DOS software is happy. The difference is that the ROM
*polls* the card and consumes each byte in a loop it controls; nobody has
to be given time. A device that raises an interrupt inverts that.

**The symptom pointed somewhere else entirely.** A truncated record that
fails its checksum looks exactly like a checksum problem, which is what
went into the first write-up: a confident, mechanical-sounding claim that
the SIO's hardware CRC generator was unimplemented. Two register bits
already visible in a captured trace refuted it —
[a roadmap's "why" is the least-tested prose in the repo](2026-08-31-roadmap-why-lines-are-untested.md)
covers that half.

## What changed

The cassette delivers one byte every 2500 T-states by default, with
`ABC802_CASSETTE_TSTATES` to override — which is how the working range was
measured rather than guessed. `cassette-load-round-trip` performs the load
in a second process, and an injection that restores one-byte-per-
instruction delivery reds it.

The reusable part:

**When a device interrupts, ask what the guest does between interrupts.**
If the answer is "real work", the delivery rate is a parameter with a
correct range, not an implementation detail to be maximised.

**Sweep the rate before assuming the data is wrong.** It costs one env var
and a loop. Here it separated two hypotheses that produce an identical
symptom in about a minute, after a much longer stretch of reasoning about
CRC polynomials that were never going to matter.

**A wide working plateau is evidence you understand the constraint; a
knife-edge is evidence you do not.** 500 to 25,000 T-states all work,
which says the requirement is qualitative ("slower than the ROM's loop")
rather than a specific baud rate this emulator would be pretending to know.
