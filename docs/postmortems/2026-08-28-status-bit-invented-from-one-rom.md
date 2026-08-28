# A status bit invented from one ROM

**Date**: 2026-08-28
**Component**: `abcbus/disk.c` (then `abc802/emu/src/disk.c`) — the
synthetic ABC-bus floppy controller
**Found by**: sharing the controller with a second machine target

## What happened

The ABC80 target's floppy support was being converted from a PC-address
trap to the real ABC-bus card the ABC802 target already had. The card was
moved to `abcbus/`, the ABC80's port decode was wired to it, and the real
ABC-DOS ROM's own protocol code was allowed to run.

It booted, selected the drive, and sent correct four-byte command headers
for exactly the right sectors. Then `RUN LIB` reported `ERR 21` — file not
found. The card's trace showed the directory sector being requested and
**not one byte of it being transferred**.

Fixing that produced a different failure: `ERR 48` at boot, before any
input, on a pristine disk image that listed perfectly. The trace showed the
DOS reading the free-space bitmap and then writing it back five times in a
row before giving up — a retry counter, exhausted.

## Root cause

One bit. The card modeled status bit 3 as an error flag:

```c
#define STAT_ERROR 0x08
```

Bit 3 is not an error flag. It is its exact complement: *"this command has
not failed."* The ABC80's ROM reads it twice, and both readings are
load-bearing:

- `0x6118`, after a command header: `BIT 3,A / JR Z` treats bit 3 **clear**
  as "this command produced nothing", and jumps away to read the result
  without transferring. With the bit modeled as an error flag it was clear
  on every *successful* command, so the ROM skipped every transfer. That
  was `ERR 21`.
- `0x60E9`, at the end of every command: `IN A,(01h) / CPL / AND 08h`,
  whose `Z` flag the write path returns on as success (`RET Z` at
  `0x60C1`). So the bit must **still** be set once the command has finished
  and the controller is back at idle. That was `ERR 48`: every successful
  write read as a failure.

Failures are reported through the auxiliary status byte on the INP port
instead, which both machines' ROMs test with `OR A`. An idle, healthy
controller reports `0x89`; one that has just failed reports `0x81`.

## Why it survived

**The only consumer could not observe the bit.** The ABC802's DOS ROM
never reads bit 3 — its post-header poll is `AND 05h / XOR 01h` (bits 0
and 2) and its completion check is the auxiliary byte. So no ABC802
program, disk image, or test could have produced a different result with
the bit set, clear, or randomly chosen.

That is worth separating from ordinary under-testing. The existing
[boot-screen postmortem](2026-08-28-boot-screen-cannot-validate.md) is
about a test whose *input* happened not to reach the code. This is
stronger and worse: no possible test on that target could have reached it,
because the machine on the other side of the interface does not use the
field. More coverage would not have helped. Only a second, different
consumer could.

The deeper habit at fault is visible in the code as it was written. The
file's own header comment carefully derives four status bits from what the
ROM demonstrably tests — the `0x00`/`0xFF` "no device" rule, bit 7 for
idle, bit 0 for byte-ready, the `AND 05h` post-header poll — each with the
ROM address that pins it. Bit 3 appears in the `#define` block with no
such derivation and is mentioned nowhere in that comment. It was the one
value in the file that nobody could point at a ROM address for, and it was
the one value that was wrong. **The absence of a citation was itself the
warning**, sitting in plain sight in a file otherwise scrupulous about
providing them.

There is a second-order lesson about naming. `STAT_ERROR` reads as
meaningful and self-evidently correct — a status byte having an error bit
is exactly what one expects — which is precisely why nobody re-examined
it. A name that encodes an assumption makes the assumption invisible.

## What changed

- Bit 3 is now `STAT_OK`, set whenever `aux_status == 0`, with the three
  ROM addresses that pin it written into the header comment beside the
  four bits that were already grounded.
- The `status` variable it lived in is gone entirely; error reporting goes
  through the auxiliary status byte, which is what both ROMs actually read.
- Bit 2 gained a comment too. It must never be set, because the ABC80's
  ROM loads it straight into the low byte of the transfer address
  (`AND 04h` … `LD L,A` at `0x6120`) — another bit the ABC802 could not
  have told us about.
- `abc802/docs/ABC802_REFERENCE.md` gained a status-byte table with a
  "pinned by" column, so a bit without a citation is visible as such
  rather than looking like the rest.

What makes the class harder to reintroduce is the sharing itself. The card
now has two independent consumers with genuinely different ROMs — three,
counting `UFD80V20.bin` — and a field that one of them ignores is likely
to be a field another one reads. That is not a test suite, but it is a
structural check that no amount of testing on a single target could have
substituted for.

## Footnote: the fix was not visible from either symptom

Neither `ERR 21` nor `ERR 48` says anything about a status bit, and the
two look like unrelated bugs in unrelated subsystems — one in reading, one
in writing. What made this tractable in minutes rather than hours was
adding a trace to the card (`ABCBUS_TRACE=1`) that logs every command
header it completes, and then noticing what the trace *did not* contain:
correct requests, no transfers. The absence of an expected log line was
the whole diagnosis. That facility is committed rather than thrown away.
