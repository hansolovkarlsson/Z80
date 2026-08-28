# `--type` fed raw UTF-8 bytes to the keyboard

**Date**: 2026-08-28 (defect introduced with `--type` in Milestone 1)
**Area**: `abc802/emu/src/main.c`, `abc802/gtk/src/main.c`
**Severity**: real, user-visible, silently wrong output

## What happened

```
$ bin/abc802 --columns 80 --type 'PRINT "ÅÄÖ"
'
ABC802
PRINT "
                        ← blank lines
"
Error 234.
```

BASIC received a syntax error where it should have printed `ÅÄÖ`. The
identical text, typed into the *same emulator* through `--interactive` or
piped to it, worked correctly.

## Root cause

`--type` handed its argument to the emulated keyboard one **host byte** at
a time:

```c
char ch = type_text[type_pos++];
abc802_keyboard_send((uint8_t)(ch == '\n' ? 0x0D : ch));
```

A shell argument containing `Å` is UTF-8: the two bytes `0xC3 0x85`. Both
were sent to the DART as if they were two keystrokes. The ABC802's
character set is a Swedish/Finnish ISO 646 variant where `Å` is the single
byte `0x5D`, so the ROM saw two meaningless high-bit characters inside a
string literal and eventually gave up.

The interactive paths had always done this correctly: `poll_keyboard_byte()`
buffers a UTF-8 lead byte, waits for its continuation, decodes the
codepoint, and looks it up in the machine's charset table. So the emulator
contained **two implementations of "type this text," which disagreed** —
and the correct one was not the one the documentation's own examples used.

## Why it survived

- **Every existing `--type` example was pure ASCII.** The roadmap's
  examples, the milestone verifications, the smoke tests — `PRINT 6*7`,
  `10 FOR I=1 TO 5`, `RUN`. The one feature that would have exposed it,
  Swedish character support, was verified through the *interactive* path
  because that was the path being built at the time.
- **The two paths were never compared.** Each was verified against the
  ROM's behavior separately, and each looked right on its own inputs.
  Nothing asked whether they agreed with each other.
- **The failure was mistakable for a BASIC error.** `Error 234.` looks
  like something the user typed wrong, not like an emulator bug. Without
  the side-by-side comparison it would be easy to conclude the ROM simply
  did not like the string.

## What changed

One shared converter, `abc802_utf8_to_chars()`, lives in `render.c`
directly beside the charset table it uses — so the decode and encode
cannot drift apart, the same reason that table already drove both
directions. Both `bin/abc802`'s `--type` and `bin/abc802-gtk`'s now call
it. It maps newline to `0x0D`, drops codepoints the machine has no
character for (matching what the interactive keyboard does with one), and
skips continuation bytes of a malformed sequence rather than feeding them
to the ROM.

Verified after the fix in both binaries: `PRINT "ÅÄÖ"` echoes and prints
`ÅÄÖ`, in the text dump and as chargen pixels.

## How it was found

By the headless `--screenshot` flag added in Milestone 4 — built so the
GTK app could be verified without automating a desktop screen capture.
Rendering `PRINT "ÅÄÖ"` through it produced a picture of the failure. The
bug was over a milestone old and had never been *seen*, because nothing
had previously drawn a picture of what that command actually did.

A verification tool built for one purpose found a bug in another, on its
first real use. That is an argument for making output *visible* rather
than merely asserted.

## The lesson

**Two code paths that do "the same thing" will diverge unless one of them
calls the other.** Not "unless both are tested" — both *were* tested, and
both passed, because each was tested on the inputs its author had in mind.
The structural fix is a single implementation, not two implementations and
a test.

Worth noticing too: the defect lived in the *convenience* path, and the
convenience path is the one every example, every doc snippet, and every
future regression check reaches for first. Correctness in test scaffolding
is not a lower bar than correctness in the emulator, because scaffolding
is what everything else is measured with.
