# Naming a source is not consulting it

**Date**: 2026-08-30
**Component**: the ABC806 memory decode and its PAL16L8
**Found by**: eventually going and reading the two documents that had been
named as the answer

## What happened

Twice in two days, an investigation stalled, correctly identified the
document that would unblock it, wrote that down — and then kept going
without it.

**First: MAME's driver.** After two sessions of tracing, the ABC806's
graphics still drew nothing and the reason was unknown. The roadmap
recorded: *"Settling it needs the ABC806 schematic or MAME's own `abc806`
memory handler; guessing further from the ROM alone has reached its
limit."* That was true. The next session then spent its whole first half
doing more ROM archaeology.

When MAME's driver was finally opened, the answer was sitting in it as a
commented-out TODO: *"0..30k read from videoram if fetch opcode from
7800-7fff"*. That is the fetch-window rule the emulator now implements.

**Second: the schematic.** The PAL work then stalled on three unknown
input levels, and the roadmap recorded that the ABC806 schematic would
settle them. The schematic is in the same public archive this project had
already been using for ROMs and disk images — the one the user had pointed
at days earlier. Rendered at 600 dpi it shows the PAL, its complete
pinout, a 22k pull-up on the input in question, and the fact that its two
main outputs are inter-board *disable* lines, which dissolved a separate
open question entirely.

## Root cause

There is a real satisfaction in correctly identifying what you do not know.
Writing *"this needs the schematic"* feels like progress — it is precise,
it is honest, it converts confusion into a well-formed question. And it
**is** progress.

But it reads, afterwards, like a conclusion. The note sits in the roadmap
looking finished, and the next person to read it — including the same
person the next day — treats the named source as a dependency to wait on
rather than an errand to run.

## Why it survived

**Because the alternative was locally productive.** More ROM tracing kept
producing findings: real ones, worth committing. Nothing announced that the
marginal value of the current method had gone to zero; it just quietly had.
An investigation that is still yielding *something* does not feel stuck,
even when a different method would yield the whole answer in twenty
minutes.

The second occurrence is the more instructive one, because the lesson from
the first had already been written up in the journal the previous day. Being
able to describe a mistake is not the same as having the reflex to avoid
it.

## What changed

The two documents are now used and cited, and the facts taken from them are
in [`ABC806_REFERENCE.md`](../../abc806/docs/ABC806_REFERENCE.md) rather
than only in a commit message.

The habit this is meant to install:

> **When a note names what would settle a question, that sentence is a task
> — not a status.** Go and get it in the same session it is written, or say
> explicitly why it cannot be got.

Two supporting practices came out of it:

- **Read the reference implementation, not only its behaviour.** MAME's
  PAL lookup is commented out, so no amount of comparing *what MAME does*
  would ever have surfaced the rule. It was only in the source. The same
  blind spot hid two ROM entries this project had recorded as
  "no MAME entry to check against" — MAME carries both, in a region it
  never reads.
- **A public archive already trusted for one artefact is trusted for
  others.** The schematics sat beside the ROM images and disk dumps that
  had been in use here for a week.

## Cost, for calibration

The fetch-window rule took roughly two sessions of tracing to reach
behaviourally, and would have taken one search of a file already on disk.
The PAL's three unknown inputs took an afternoon of dead ends and one
crop of a PDF.

Neither investigation was wasted — the behavioural derivation is why the
MAME sketch was trusted immediately rather than doubted, and it produced an
independent confirmation of the same rule. But it was the expensive way
round, twice.
