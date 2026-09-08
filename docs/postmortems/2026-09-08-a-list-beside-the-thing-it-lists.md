# A list beside the thing it lists

**Date**: 2026-09-08
**Component**: the ABC802 regression suite, and this repository's own build
documentation
**Found by**: an audit that compared what the suite *reports* against what
the script *defines*, rather than reading either

## What happened

Two findings in one sweep. They looked unrelated for most of an hour, and
they are the same finding.

**A check that vanished.** `abc802/tests/run_tests.sh` names four checks in
the loop that skips its media-gated block, and the block below it runs
five. So on any tree without `disk001.img`, which is every fresh clone
since that media is deliberately uncommitted, `abcdisk-list-real-media`
neither ran nor skipped. It disappeared from the report entirely. The suite
announced 27 checks where the script defines 28, and nothing anywhere
compared those two numbers.

The lost one is the most load-bearing of the five. It has `bin/abcdisk`
read real media it did not write, which is the only thing pinning its
directory constant to something other than its own value. The comment
directly above it says why, and says it from experience: a deliberate
sabotage of `dir_first` showed that a writer and a reader sharing one wrong
constant agree perfectly.

**Documents that had stopped being true.** `CLAUDE.md`'s Build & Run block
listed three suites, three binaries, two GTK apps, two ASCII-art fixtures
and two `*_TEST_DISKS` variables. The tree has four, four, four, three and
three, and there was no `make test-abc806` line at all for a target `make
test` has depended on since 2026-08-30. `README.md` described the ABC806 as
"at milestone 2 ... No live session or high-resolution graphics", eleven
completed entries after that stopped being true, and carried two different
`abcbus/` bullets, one of which said the ABC80's disk path "has not been
tested" while eight ABC80 disk checks pass on every run. The `Makefile`'s
own comment introducing the target still read "memory map and boot only, no
video/keyboard/disk yet".

## Root cause

Every one of those is a hand-written copy of a set the tree already defines.

The skip list is a second copy of the gated block's membership. The `make`
block is a second copy of `.PHONY`. "The two GTK apps" is a second copy of
the `*-gtk` targets. "Two ASCII-art fixtures" is a second copy of what
`git ls-files '*fixtures/chargen.txt'` returns. In each case the authority
is somewhere else in the repository and the copy was written beside it for
a reader's convenience.

A copy of a set is correct on the day it is written and drifts every time
the set grows. Nothing here grew quietly: every addition was a deliberate,
committed, well-documented piece of work. What none of them did was go back
to the places that had counted the old set, because **a count does not read
like a claim.** "All three suites" is a description; it is only a claim
about the world once you notice it is arithmetic.

## Why it survived

**Because both copies pass the only test anyone applies to them, which is
reading.** The skip list reads perfectly: four plausible check names,
correctly spelled, correctly indented, in a loop that plainly works. The
Build & Run block reads perfectly too, and every command in it runs. Nothing
is wrong *on the page*. What is wrong is only visible by holding the page
against the tree, and that comparison is arithmetic rather than
comprehension.

**And the evidence was printed on every single run.** `abc802: 21 passed, 0
failed, 6 skipped` is not a suspicious line. It becomes one only once
somebody knows the script defines 28, and nobody did, because knowing it
requires counting the definitions rather than reading the output.

The sharpest part is that **the fix already existed in a sibling, written
the next day.** `abc806/tests/run_tests.sh` holds its gated names in a
`DISK_CHECKS` variable used both for the skip loop and as the block's own
membership, so there is one list rather than two. That went in on
2026-08-30; the abc802 defect was written on 2026-08-29 and never revisited.
The ABC80 suite hand-writes all eight of its gated names across two loops
and currently agrees, which is the more dangerous of the three states:
correct by attention rather than by construction.

This is also the **second time in five days** this repository has found this
exact shape. On 2026-09-04, `bin/abcdisk` escaped a list of binaries that a
sibling suite maintained correctly, and that day's journal entry called it
"a pattern applied to one sibling and not the other" and judged it too
narrow for a postmortem, on the grounds that
[a binary oracle hides its premises](2026-08-30-binary-oracle-hides-its-premises.md)
already covered the general shape. That judgement was wrong, and the way it
was wrong is worth keeping: the entry treated the finding as being about
*guards*, when it was about *duplicated lists*. Seen once it looked like a
detail. Seen twice, with five instances in one sweep, it is a class.

## What changed

- The abc802 skip list is now a `DISK_CHECKS` variable used by both the loop
  and the block, adopting the ABC806 shape. The suite reports 28 checks
  where it reported 27, and `abcdisk-list-real-media` skips loudly with the
  same reason as its four siblings.
- `CLAUDE.md`, `README.md` and the `Makefile` comment are corrected against
  the tree, each claim re-checked against the command that decides it.

The habit this is meant to install:

> **A list written beside the thing it lists is a duplicate, and it will
> drift.** Derive it, or make one copy serve both uses, as `DISK_CHECKS`
> does. Where neither is possible, the list is a claim and inherits
> [a roadmap's "why" is the least-tested prose in the repo](2026-08-31-roadmap-why-lines-are-untested.md):
> it needs measuring, not re-reading.

And the instrument, which is cheap enough that there is no excuse:

> **Compare what a suite reports against what its script defines.** 27
> against 28 is one line of arithmetic. Ten days of reading the same output
> found nothing.

## Cost, for calibration

The vanishing check was introduced on 2026-08-29 and was absent from every
clean-tree run for ten days across five working sessions, including two
that specifically examined suite output: the 2026-09-04 session that fixed
the `abcdisk` prerequisite, and a project status the same day. Both looked
at `abc802: 21 passed, 0 failed, 6 skipped` and read it as healthy, which it
was, in the sense that everything reported had passed.

Nothing was actually broken by it, which is the uncomfortable part. The
check would have passed had it run, on any machine with the media. The cost
is entirely in what was not being verified while everyone believed it was.
