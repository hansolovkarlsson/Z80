# Project journal

A running log of what was worked on, what it took, and what it taught.
Newest first.

This is not a changelog — `git log` already does that, and better. It is
the place for the things a commit message cannot hold: why an approach was
chosen over the alternatives, what turned out to be wrong on the first
attempt, and which assumptions the hardware refused to honor. The
per-target `*_COMPLETED.md` files hold the same detail organized *by
milestone*; this file holds it organized *by when*, which is the view that
answers "what was I in the middle of, and why did I do it that way."

Cross-cutting failures that are worth understanding on their own get a
full write-up in [`postmortems/`](postmortems/) rather than being buried
in an entry here.

Several entries can share a date. Where a day has more than a handful they
are numbered in the order they happened — `(1)` earliest — rather than
strung out with "later still"; the file itself stays newest-first.

---

## 2026-08-31 (6) — the ABC802's terminal render learns the row attributes

`--screen` and `--interactive` printed one glyph per character code and
knew nothing about the row attributes, so a Row Graphic screen read
correctly as a PNG and misleadingly in a terminal. Fixed by giving both
renderers the pixel renderer's own attribute walk and drawing the mosaic
font as Unicode sextants — the shape `abc806_decode_row()` already had.

Two things were worth the care.

**The font was read, not assumed.** Row Graphic ORs `0x800` into the
character-ROM address. Dumping that font's glyphs out of the committed ROM
shows a teletext 2x3 mosaic, and the bit assignment is the non-obvious
part: the six cells come from bits 0, 1, 2, 3, 4 and **6**, skipping bit 5
because teletext uses it to separate graphics codes from alphanumeric
ones. I checked that against the ROM's glyphs (`0x21` top-left alone,
`0x60` bottom-right alone, `0x7F` all six) rather than trusting the
standard, and it held.

**The two walks are not identical, and that needed a test.** The terminal
walk reads scanline 0 to decide whether a code is an attribute command;
the pixel walk re-reads whichever scanline it is drawing, including the
substituted blank and cursor rows. My first check of the invariant said 34
codes violated it — because I scanned all 16 rows, and the cursor row
genuinely differs (`0x80` becomes `0xBC`). Narrowing to the ten rows
actually scanned gives zero violations, and on the substituted rows the
*decoded command* is unchanged even though the byte is not: ATE, ATD and
the two select bits are identical, only ignored bits move. So the walks
provably agree for this font, and `chargen-attribute-invariant` now says
so rather than a comment claiming it.

### A detour worth recording

My first comparison used 40-column mode and POKEs into consecutive cells,
and the two renders disagreed badly. Not a bug: in 40-column mode the ROM
lays text out in the *even* cells and the video hardware draws each glyph
double width, so consecutive POKEs do not correspond to what gets drawn.
Comparing the two renders there compares different things. In 80-column
mode they line up cell for cell. I spent a while suspecting my sextant
mapping before noticing the test setup was the problem — the oracle was
fine, the input was wrong.

### An injection that caught nothing, correctly

Removing the mosaic-font selection from `decode_row` reddened no check.
That is not a hole: every code that is an attribute command in the
alphanumeric font is the same command in the mosaic one, so attribute
detection lands identically either way. The line stays and now carries a
comment saying it is not independently observable, which is the useful
thing to write down — otherwise the next person deletes it as dead code
and is right by every test.

Also: BASIC's own `PRINT` never puts these codes into character RAM —
`CHR$(17)` is consumed on the way — so the row has to be POKEd. That is
the honest scope of the feature: reachable by a program that writes
character RAM, not by ordinary BASIC output.

## 2026-08-31 (5) — DOSGEN's bad sectors were never a bug

Last of the day's roadmap items, and the third in a row whose stated
premise did not survive contact with a measurement. The ABC802 roadmap
carried this: on real 640K UFD-DOS media, `DOSGEN` walks cluster addresses
well past the end of the drive and reports each as `Sektor NNNN är dålig`,
cause unknown, with two hypotheses attached — that the real card answers a
beyond-media address differently, or that DOSGEN wants a drive-geometry
reply this controller does not give.

The first thing I found is what the entry did not mention: **DOSGEN
finishes**, and its answer is right — `2528 användbara sektorer`, which is
632 usable clusters of 4 on a 640-cluster drive. That reframes the whole
question from "why is it broken" to "why is it noisy".

### Getting there

Driving it was most of the work. `--type` sends one string at a fixed
pace with no way to wait inside it, and the DOS shell and DOSGEN each take
far longer to load than the string takes to type. Padding with bare
carriage returns solves it — keys arriving while nothing is reading are
discarded, and the ones that land at a prompt get an "invalid device name"
and a reprompt. Then the dialogue itself: drive, `-` for filesystem-only,
and three separate confirmations, the last being `ABSOLUT säker ??`.

### The experiment that settled it

Rather than reason about what a real controller does, I removed the
range check entirely so out-of-range sectors read as zeros and *succeed*.
DOSGEN's output came back **byte-identical** — same bad-sector lines, same
final sector 7644, same total. The card's answer has no influence at all,
which refutes both recorded hypotheses at once.

### The disk had the answer

DOSGEN writes exactly four sectors, and sector 14 is the free-list bitmap:
240 bytes, so 1920 clusters, on a drive with 640. Bits 0-8 set (system
area), 9-639 clear (the usable media), and **640-1911 set**. That last
range is 1272 clusters — exactly the 1272 out-of-range sector requests I
had counted in the bus trace, from a completely independent measurement.
It is a fixed-size structure being filled out for a smaller drive. The
noise is cosmetic and the filesystem is correct, which I then proved by
saving to it and reading back in a second process.

### I walked into the echoed-input trap

Three checks, and the injection sweep caught my own usability check being
a fake. Written as one process — save, `NEW`, load, `LIST` — it **passed
with the card's writes injected away**, because the program text is on
screen from the moment it was typed and the assertion matched the echo.
This project has a postmortem about precisely this, written after the same
thing happened in this same suite. Knowing about it did not stop me
writing it; running the sabotage did. Two processes fixes it — the second
never types the program text.

Also: `dosgen-completes` first asserted on DOSGEN's banner and failed,
because 1272 bad-sector lines scroll it off a 24-line screen. A real
failure, for a reason with nothing to do with the subject.

### Three for three

Today's three roadmap items each had a stated reason, and all three were
wrong: ABC-DOS does not scan eight drives at boot, the ABC80's slowness
was not video timing, and DOSGEN was not waiting on the controller. The
postmortem written after the second one predicted the third. That is
mildly reassuring about the postmortem and quite damning about the
roadmaps.

The cap story repeated too: my hand-run used 30,000,000,000 T-states and
took 41 seconds where 450,000,000 finishes. Milestone 10 already learned
that lesson for this suite. Measuring the budget is apparently something
you have to do every time rather than something you learn once.

## 2026-08-31 (4) — the ABC80's speed limit was a syscall, not the video timing

The roadmap had carried this for a while: `bin/abc80` runs at ~1.7M
instructions/sec, several times slower than `bin/abc802` on the identical
shared core, "not investigated; the per-instruction video-timing work is
the obvious first suspect."

The first measurement killed the suspect. A 20M-instruction batch run took
11.4 seconds, and the split was 2.8s user against **8.5s system**. System
time dominating a batch emulation means syscalls. Output redirected to
`/dev/null` changed nothing, and the run only prints 140 lines anyway.

One `sample` run: 1282 samples in `read`, 1184 in `__select`, 66 in
`abc80_step`. The loop called `select()` then possibly `read()` on stdin
once per emulated instruction.

The part that stings is that the reasoning was already written down, four
lines above the bug. `ABC80_PACING_CHECK_INTERVAL`'s comment says
`clock_gettime()` and `nanosleep()` are real syscalls and running
2,995,200 of them a second would swamp actual emulation work — and then
ends by observing that `poll_stdin_byte()` still runs every iteration and
is not gated by the interval. Someone noticed the exception, wrote it
down, and did not connect it to the performance note two files away.

Gating the poll the same way — every 500 instructions, ~2ms of emulated
time — took the batch run from 11.4s to 0.6s, throughput from 1.7M to
~75M inst/sec, and `make test-abc80` from 29.6s to 2.4s.

### Checking it rather than arguing it

The change alters *when* a key arrives, so I wanted more than "the tests
pass". Two typed BASIC sessions run through the old and new binaries give
**byte-identical rendered screens**. What differs is the run summary's
T-state count and final PC (347,134,849 against 347,135,849), which is
exactly the expected consequence of a key landing a few hundred
instructions later — the machine is at a slightly different point in its
idle loop when the cap hits.

No test guards the performance. A timing assertion would be flaky, so the
honest guard is that the numbers are recorded and the suite's own runtime
is the signal.

Re-profiled afterwards: it is now opcode handlers and
`abc80_bus_read_hook`, the floating-bus hook the ABC802 has no equivalent
of. Real work, and the residual gap has a reason.

### The lesson

A guess sitting in a roadmap for long enough starts reading like a
finding. This one was plausible — the target genuinely does per-instruction
video timing the ABC802 does not — and a single profiling run at any point
would have refuted it in under a minute. Same shape as this morning's
ABC-DOS "scans all eight drives at boot", which had also been sitting there
as a fact and also dissolved on first contact with a measurement. Two in
one day is a pattern worth naming: **the roadmap's "why" lines are the
least-tested prose in the repo.** Everything in a `*_COMPLETED.md` was
true when written because something ran; a planned-work justification is
written before anything runs, and nothing ever goes back to check it.

## 2026-08-31 (3) — ABC80's second drive, and a justification that was false

Small milestone, and the interesting part happened before any code.

The roadmap had carried "a second drive" as the top candidate for a while,
justified like this: the card supports eight units, ABC-DOS scans all
eight at boot, visible in `ABCBUS_TRACE=1` as a walk of units 0-7 reading
directory sectors 16-23, so this is mostly CLI plumbing. I went to
reproduce that trace before building on it.

It does not happen. A full boot to the prompt issues four bus commands and
every one addresses unit 0. Whatever produced that sentence was the
ABC800 family's DOS — a different ROM on a different target — and it had
been sitting in ABC80's roadmap as a fact about this machine.

The plumbing half of the claim was right, and the milestone took about
twenty lines: `abcbus_disk_attach_arg()` has parsed both `--disk FILE` and
`--disk N:FILE` since the ABC802 needed two drives, and only
`abc80_abcbus_init()` still took a single path. But the *evidence* had to
be found again. It is better evidence, as it happens: `ABCDOS80.bin` has a
device-name table at `0x6EB5`, seven seven-byte entries reading `DR0`
through `DR6`. The ROM names the drives. It just does not go looking for
them until something asks.

And something does: the real `LIB` utility walks each drive it can find
and prints that disk's own volume label, so a single run shows
`SYSTEM-DISKETT ABC-80 Vers. 2.1.` for drive 0 and `SYSTEMSKIVA VER. 1.0`
for drive 1. Two different labels, which is exactly the assertion that
distinguishes two drives from one image mounted twice — and with one
`--disk` there is no `Drive: 1` section at all, so the check discriminates
in both directions.

### The round trip is not enough on its own

`SAVE DR1:XDRIVE` / `NEW` / `LOAD DR1:XDRIVE` / `LIST` prints the program
back, and that would also happen if the save had quietly gone to drive 0
and the load read it straight back. The same shape as the suite's own
older near-miss, where a check asserted on the echoed command line rather
than BASIC's answer. So the check steps outside the emulator afterwards
and reads both images with `bin/abcdisk`: `XDRIVE` present in drive 1's
directory, absent from drive 0's, and drive 0's file still byte-identical
to the pristine archive copy. All three hold.

Two injections, both caught — the card ignoring the unit field in the
command header, and every bare `--disk` landing on drive 0. The second
correctly leaves `disk-pinned-drive` green, because `N:FILE` never goes
through the sequential counter, which is why that third check is worth
having separately.

### Media

`ABC80_TEST_DISKS` now wants `disk001.img` as well as `disk003.img`. It is
the archive's only other distinct ABC80 disk (`disk002.img` is
byte-identical to `disk001.img`), and the difference between the two
volume labels is load-bearing. The new checks skip separately from the old
five, so a directory with only `disk003.img` still runs everything it can.

## 2026-08-31 (2) — the 480-wide mode, and a check that tested nothing

The last graphics item on the ABC806 roadmap, created by the session
before it: the palette carries the horizontal resolution, no `FGCTL`
argument programs an entry whose halves differ, and so the 480-wide mode
had never been driven. The renderer decoded it; nothing had ever produced
one.

### Getting there was one fact

`hrc` is indexed by register B, and the Z80 puts B on the top half of the
address bus during `OUT (C),A`. BASIC's `OUT` takes a 16-bit port into
`BC`, so `OUT 15*256+7,v` writes entry F. I checked this rather than
assuming it — `OUT 7,v` latches entry 0, and sweeping the high byte walks
the index — but it is just the hardware being addressed the ordinary way.
No machine code, no POKE, no new emulator flag.

### Proving it needed the right instrument, twice

`hrc[F] = 0x9A` and a line in pen 3 gives `1=181 2=181` where `FGCTL 2`
gives `3=362`. Same 91 plane bytes. That is suggestive but not proof: equal
counts of red and green are also what you would get from a left half red
and a right half green. The claim is that *adjacent* pixels differ, so I
decoded the PNG and looked at run lengths — every run in the 480 case is
length 1. That is the assertion, and no count could have made it.

Then the reverse problem. A count *can* carry it once the experiment
shrinks: a dot is exactly one plane nibble, so it renders as exactly the
two pixels one entry describes, and `1=1 2=1` versus `3=2` is the whole
thing in a form the suite can assert without a PNG.

The control I nearly skipped is the one worth keeping. `hrc[F] = 0x99` —
written by hand, halves alike — puts the dot back to `1=2`. Without it,
"the picture changed when I wrote the palette directly" has a second
explanation, and the experiment does not distinguish it from the one I
wanted.

### The check that tested nothing

Six checks, then the injection sweep, and one of them was a fake.
`graphics-480-half-order` claimed to establish that the high half is the
left pixel. I swapped the two halves of every entry in the renderer and it
stayed green — obviously, since the census counts colours and a swap moves
a pixel without changing a count. I looked for a way to make position
visible to a count (clip one pixel of the pair at a screen edge) and there
isn't one: the pairs are aligned to even screen positions, so both are in
or both are out.

The ordering turned out to be covered already, by the instrument built for
exactly this in milestone 2 — `chargen-attributes`, whose synthetic plane
sets `hrc[2] = 0xA0` and whose fixture is ASCII art, so a swap moves a
character. It caught both injections. So the check was renamed to
`graphics-480-half-transparency`, which is what it does establish, and the
suite now says where position is actually tested.

This is the second time in two sessions that a check survived the
injection meant for it, and both times for the same underlying reason: I
wrote the assertion from what I believed the code did, rather than from
what the *output* could distinguish. A census cannot see position. An
absence-of-colour assertion cannot see white. Neither is subtle in
hindsight, and neither was visible without running the sabotage.

The other half of the sweep is worth recording as a positive: the
240-only decode reddens no pre-existing graphics check beyond the fixture.
Every other check in the suite goes through an `FGCTL` palette, and every
`FGCTL` palette has equal halves, so half the decode was invisible to all
of them. The gap was real.

## 2026-08-31 — the rest of FGCTL, and FGPICTURE turns out to be HRS

Two items had stood open on the ABC806 roadmap since milestone 5: the
`FGCTL` arguments beyond the single one that had been mapped, and
`FGPICTURE`, which was in the ROM's keyword table and had never been run.
They turned out to be one job, and finishing it closed a third gap nobody
had set out to touch.

### The sweep was the easy half

`ABC806_TRACE_HRC` already existed, so mapping `FGCTL` was a matter of
running all 256 arguments and keeping the final state of the palette —
fifteen seconds with `xargs -P 8`. What came back was a clean
combinatorial enumeration: bit 7 ignored, `n=0` transparent, `n=1` all
white, `n=2..71` the 70 ways of choosing four of the eight colours in
lexicographic order, `n=72..127` the 28 pairs in two pen mappings.
1 + 1 + C(8,4) + 2·C(8,2) = 128 exactly.

The counts being combinatorial suggested the ROM computes them rather than
storing a table, so I packed the expected table three ways and searched
every committed ROM image for it. Not there — which is a real finding and
not just a failed search, because the *shape* of the data predicted it.

A negative result too: no `FGCTL` argument reaches the 480-wide mode. The
palette carries the horizontal resolution, and every entry `FGCTL`
programs has both halves alike. That went on the roadmap as the one
graphics thing still undriven.

### FGPICTURE, and an error message that lied

`FGPICTURE` looked inert. Every argument pair but `0,0` gave `Error 201`,
which the BASIC II table calls "end of memory" — on a machine with 29,001
bytes free. I spent a while on the wrong hypotheses (a viewport, a
clipping window, a picture buffer) before doing the thing that settles it:
a differential profile with `ABC806_PROFILE_ALL=1`, once with the command
and once without, diffing the executed address sets. That put the routine
at `0x7E39`, and forty bytes of hand-decoding ended the guessing —
`LD A,C / RLCA×4 / OR L / OUT (06h),A`. Port 6 is HRS.

So `FGPICTURE a,b` is the machine's double-buffering command: `a` is the
bank the CPU draws through, `b` the bank the CRTC shows. The "end of
memory" is a bounds check against a byte at `0xFEF4` holding the number of
allowed banks, which is **1** on a bare machine — hence every non-zero
argument being refused. `FGPICTURE a,b,n` raises it, up to a ceiling of 16
kept at `0xFEF3`. Both were readable with `PEEK` from BASIC, which stayed
the fastest instrument throughout: this machine is its own test harness.

The lesson I'd keep: I took `Error 201` at face value for longer than I
should have, and it sent me looking for an allocation that does not exist.
An error code is the ROM's summary of a failed test, not a description of
what the test was.

### The instruments were the actual gap

Both of these were invisible to everything the emulator printed. The
summary reported plane byte counts and pen nibbles — all of it the *write*
side, none of it touching the palette or able to tell one 32K bank from
another. An emulator that ignored `hrc` entirely, or multiplied the bank
number by zero, would have left every existing line unchanged.

So `bin/abc806` gained `banks:` (which 32K banks hold plane bytes) and
`Pixels by colour:` (the rendered census, the only output that reads the
picture). The screenshot path now renders through the same buffer, so the
census and a PNG cannot disagree.

That instrument is what let `FGPICTURE` close the **HRS bank select** gap,
which had been on the roadmap separately with the note "needs a case that
actually banks". FGPICTURE *is* that case, and it was sitting in the ROM
the whole time.

### Breaking them on purpose, and one that survived

Eleven new checks, four injected regressions, all caught. The one worth
recording is the one that nearly wasn't: my first
`graphics-fgctl-0-is-transparent` asserted that colours 1-6 were absent,
which sounds right and is worthless — replacing the palette lookup with
"any nonzero byte is white" makes the lines *visible in white*, and white
cannot be distinguished from the text by colour. It passed the injection.
Pinning the whole census instead catches it. The same trap the suite's own
history already has an example of, arrived at from a different direction.

Also corrected two stale lines in the roadmap found while answering a
status question: a gaps preamble still saying "two milestones in, of five"
three sections below a table showing all five met, and a gap claiming the
PAL fuse map was unevaluated, two commits after it was decoded.

## 2026-08-30 (16) — a documentation pass, and two lessons promoted to postmortems

A deliberate sweep to get everything from this session's working memory into
files, so nothing depends on a session's context surviving.

### Two postmortems

Both are lessons that recurred often enough today to outgrow a journal
entry:

- **[A binary oracle hides its premises](postmortems/2026-08-30-binary-oracle-hides-its-premises.md)**
  — a pass/fail check discarded the correct PAL column layout *twice* and
  was then wrong about the machine's whole ROM/RAM model, because it
  encoded premises nobody had written down. Same shape as reporting three
  coloured lines as "all white" from looking at a small image instead of
  sampling its pixels. The rule: when the subject is unknown, prefer an
  instrument that renders over one that judges. Explicitly *not* an
  argument against the regression suites, which test known behaviour and
  should stay binary.
- **[Naming a source is not consulting it](postmortems/2026-08-30-naming-a-source-is-not-consulting-it.md)**
  — twice a roadmap correctly identified the document that would unblock an
  investigation, and twice the next session did more of what had already
  stopped paying. Both documents were public and both contained the answer.
  Cost: roughly two sessions.

The second happened *after* the first was written up, which is the honest
part of it. Being able to describe a mistake is not the same as having the
reflex to avoid it.

### Finished work moved out of the roadmap, again

The ABC806 roadmap's "what is next" had quietly become an archive — forty
lines of PAL narrative describing work that was *done*. That is the same
drift the roadmap/completed split was made to stop, recurring within a day
of the split, because a section named for the future is a comfortable place
to put a result.

It is now in `ABC806_COMPLETED.md` alongside the milestones and the GTK
app, and the item reads as what it is: **closed, not deferred**. The
scoping document had carried "evaluate the PAL properly" from the start as
though it would replace `memory.c`. It would not — the outputs are
inter-board disable lines, so one board's PAL cannot express a two-board
decode. Recording that is worth more than leaving an aspiration open.

### A currency sweep

Two roadmaps claimed check counts that had drifted (17 against 19, 14
against 18). Every relative link in every documentation file was resolved —
100 of them, all good once function-call syntax in backticks is discounted.

Nothing dramatic, but the counts are the kind of thing that rots silently
and then makes a reader distrust the parts that are still true.

## 2026-08-30 (15) — making the BASIC reference's claims executable

Wrote `ABC806_BASIC_REFERENCE.md` and then noticed it was a document full
of specific behavioural assertions — `FGFILL` fills a rectangle from the
cursor, `FGPAINT` floods 30,720 bytes on an empty plane — with nothing
re-checking any of them. A reference that drifts from the machine is worse
than none, because it is trusted.

So five of its claims are now checks: `graphics-fgpoint-cursor-only`,
`graphics-fgpoint-plots-a-dot`, `graphics-fgfill-rectangle`,
`graphics-fgpaint-unbounded` and `graphics-fgpaint-is-bounded`. The suite
goes from 13 media-free checks to 18, and from 6.3 to 9.4 seconds — I
measured the minimum cycle count each needed rather than reusing the
generous 600M I had been testing by hand, which halved the cost.

### The bounded flood fill is the one that matters

`FGPAINT` on an empty plane fills all 30,720 bytes, which on its own is
indistinguishable from a screen clear. Drawing a box first and filling
inside it cuts the count to 6,161. That is what makes it a *flood* fill,
and it is the assertion I would keep if I could only keep one.

It is also, along with the `FGFILL` and unbounded-`FGPAINT` checks,
sensitive to something the older checks were not: **the plane's read
path**. A flood fill has to read the plane back to find its boundaries.
Breaking reads while leaving writes intact reds exactly those three and
leaves the dot checks passing — verified by doing it.

That was worth checking rather than assuming. My first sabotage attempt
(narrowing the window's address range) reddened only two checks, and it
would have been easy to conclude the new ones were redundant. They are not;
I had simply picked a sabotage that missed the region they exercise.

### A naming detail

The checks were first generated from the command text, which produced
`graphics-FGPOINT-5151` for a case that is really about `FGFILL` — the
command line happens to start with `FGPOINT` to place the cursor. Renamed
by hand. A test whose name misdescribes it is a small thing until it fails
at three in the morning.

## 2026-08-30 (14) — bin/abc80-gtk verifies itself now

`bin/abc80-gtk --screenshot FILE` renders one frame through the identical
`draw_screen()` the live window uses, against an offscreen surface, opening
no window and claiming no audio device. `--type TEXT` types a line first,
through the real keyboard path.

Two roadmap gaps closed with one change: the app had **no automated
coverage at all**, and the reason was that it had no headless mode. It was
the last of the three GTK apps without one — a straight port of what the
ABC802's and ABC806's windows already do.

### Why this one actually needed it more

The other two windows are thin: their pixel decode is a pure function with
its own ASCII-art fixture, so the window is only a Cairo blit and there is
little left to get wrong. **This app carries its own decode**, because the
ABC80 CLI renders Unicode block glyphs instead and there was nothing to
share. So the one GTK app with real logic in it was the one with no tests.

### The check, and the half that does the work

Two checks: `gtk-headless-boot` asserts the sign-on lights more than 300
pixels, and `gtk-headless-type` asserts that typing `PRINT 6*7` lights
*more* than the boot screen did.

Counting non-background pixels rather than comparing images is deliberate —
a committed reference PNG would be hostage to the host's Cairo version. But
the interesting part is which half earns its keep. Pointing the framebuffer
at the wrong RAM address on purpose (`0x0000` instead of `0x7C00`) **reds
`gtk-headless-type` while `gtk-headless-boot` still passes**: garbage lights
plenty of pixels too. An absolute threshold would have shipped that bug; the
relative comparison caught it.

Which is a small argument for a general habit — a threshold test asks "is
there output", a differential test asks "did the thing I did change the
output", and only the second is about the feature.

### Two small tidyings on the way

`write_screenshot()` came out of the File-menu handler so the menu and the
headless flag share one path rather than being two implementations that
could drift. And the `--quickload` injection point — the ROM address
`0x02AA`, which carries a long comment explaining why it is the *only* safe
place — was duplicated between the timer tick and would have been a third
time in the headless pump; it is now `maybe_quickload()`, called by both.

## 2026-08-30 (13) — the real schematics, and a wrong oracle for the third time

Entry (12) ended saying the remaining PAL work needed the ABC806
schematic. So I went and got it, from the archive the user pointed at days
ago: `ABC806-schema.pdf`, ten sheets, Luxor Datorer AB 1983.

Rendering the PDF at 600 dpi and cropping (`pdftoppm -x -y -W -H`) makes
individual chips perfectly legible. Sheet 5, "Graphic Control", position
2D: **PAL16L8/ABC P4**, with every pin labelled.

### What it settled

- **The pinout matches MAME's comment exactly** — an independent check on
  both, since MAME's is a hand-written comment beside a ROM it never reads.
- **`RKDL` (pin 17) carries R17, a 22k pull-up to Vcc.** So it reads high
  whenever the PAL tri-states it, which is whenever `KDL` is high. One of
  my three unknown input levels, resolved from a primary source rather than
  assumed.
- **`XML` is `/XM`** from connectors P1-15 and P2-5 tied together, **`M1L`
  is `/MI`** from P3-17, **`KDL` is `/HR`** from P1-11 — active-low signals
  arriving from the processor unit.
- **`ENL` comes from a 74F189**, the 16x4 RAM sitting right below the PAL
  on the same sheet. That RAM *is* the page map, which is a pleasing thing
  to see as silicon after modelling it as an array.

### And it corroborated the overlay

`RAMD` has, among its product terms, **the single literal `A15'`**. RAM is
therefore selected across the entire low 32K unconditionally, and `ROMD`
decides only whether ROM overrides on a *read*.

That is exactly the arrangement `memory.c` implements — reads from ROM,
writes through to the DRAM beneath — which I arrived at by watching a 30K
memset that only made sense against DRAM. Second independent confirmation
of a behaviourally-derived rule in two days.

### The oracle was wrong again, and I nearly missed it again

I had been scoring candidate evaluations against "ROM low, RAM high". That
test is simply false about this machine: RAM is enabled everywhere. It is
the *third* time in this investigation that a pass/fail check encoding an
unexamined assumption told me a correct thing was wrong — twice about the
column layout, once now about the whole ROM/RAM model.

The pattern is specific enough to name, and now has its own write-up:
[a binary oracle hides its premises](postmortems/2026-08-30-binary-oracle-hides-its-premises.md):
**a binary oracle hides its own premises.** When it says False you learn nothing about *which* of its
assumptions failed, and the natural reading — "the thing under test is
wrong" — is exactly the one that keeps being false. Printing the equations
instead of scoring them is what broke the deadlock both times.

### The apparent gap was not a gap

I nearly closed this entry saying "no term enables ROM between `0x4000` and
`0x77FF`, which the machine demonstrably does — probably `ENL` being
per-page." One more crop, of the PAL's *output* side, and that hypothesis
was unnecessary.

`ROMD` (pin 12) goes to **P2-4, labelled ROMDIS**; `RAMD` (pin 19) to
**P1-7, RAMDIS**; each with a 330 ohm pull-up to Vcc. They are inter-board
**disable** lines from the video unit to the processor unit, not local chip
selects.

So the memory map is a property of *two boards*. The processor board
decodes ROM across the low 32K by itself, and this PAL only intervenes.
There was never anything missing from the array — I had been expecting one
board to answer a question that belongs to two.

Which also settles what the fuse map is *for*, here. It is not a
replacement for `emu/src/memory.c`: reading one board's PAL could not
reproduce a two-board decode even done perfectly. It is an instrument for
settling specific questions, and it has now settled two — the fetch-window
latch, and RAM being selected across the whole low 32K.

### What is left

`I3` (pin 1), untraced beyond sheet 5. And the polarity of ROMDIS/RAMDIS:
the pull-ups make the idle level high, which for a line named "disable"
ought to be the inactive state, so the names are presumably active-low —
while MAME's approximation reads its own `romd` the other way round.
Nothing depends on it yet, so it stays a question.

## 2026-08-30 (12) — the fetch-window rule, confirmed in the silicon

Went and got the datasheet, which is what entry (11) said would unblock the
PAL and what entry (10) had already recorded as a lesson about *not* just
naming a source. Two hours later the array reads as equations, and it says
something better than I expected.

### The rule is in the fuse map, as a latch

`HRAL` (pin 13) and `HRBL` (pin 14) each appear **complemented in the
other's product terms**. They are a cross-coupled SR latch, built out of
two of the PAL's own outputs. It is set by

```
A15' . I3' . A14 . B13 . B12 . B11 . M1L' . (ENL + EME') . XML
```

which is address `0x7800`-`0x7FFF` with `M1L` low — an **opcode fetch**.
`HRAL` then appears directly in `ROMD`'s and `HRE`'s terms
(`RKDL . KDL . EME' . HRAL`), so the latched state gates the memory decode.

That is exactly the rule `abc806/emu/src/memory.c` implements. It was
arrived at from watching the machine — a 30K memset that only made sense
against DRAM, a plotter whose writes vanished, a differential profile — and
now the silicon says the same thing independently.

**And it explains the part behaviour could not.** I knew *that* the
diversion had to persist through an instruction's data cycles, because the
`LDIR` clear reads and writes in the same region and a plot is a
read-modify-write. I did not know *why* a combinatorial decode would do
that. It would not: it is a latch, set by the fetch and held. Which is
precisely what `abc806_note_instruction_fetch()` reproduces by holding the
fetch PC for the whole instruction — arrived at as the only thing that
worked, and now the right thing for the right reason.

### How the column mapping got settled

The datasheet (TI SRPS016) has the PAL16L8 logic diagram, and it confirmed
the row grouping outright — "first fuse numbers" 0..224 against pin 19,
1792..2016 against pin 12, which entry (11) had derived from the fuse data
alone. Pleasant to have a guess promoted to a citation.

The column layout is the interleaved one: eight dedicated inputs on pins
2-9 alternating with the six feedback lines, pins 1 and 11 at the ends,
even columns true and odd complemented.

**And it is self-validating, which matters more than the source.** Decoded
with it, the terms come out as recognisable memory decode — `A15'.A14'.EME'`
for the bottom 16K, `A14.B13.B12.B11` for the `0x7800` window. My two
earlier guesses produced no meaningful equations at all. A wrong column
mapping does not accidentally yield sensible hardware.

### The mistake I made twice before finding it

Both times I tested a candidate layout with a pass/fail check — "does this
give ROM low and RAM high?" — and both times it printed False and I
discarded the layout. The third time I printed the *equations* instead, and
the very layout I had discarded was obviously correct on sight.

The check was wrong, not the mapping. It depended on three input levels
(`I3`, `XML`, `RKDL`) that board logic supplies and I had assumed. A binary
oracle built on an assumption I had not noticed making is worse than no
oracle: it does not fail loudly, it just says no to correct answers.

### What is left

Those same three levels. With `RKDL` high the array selects ROM at every
address; with it low the behaviour becomes address-dependent but still does
not match the plain ROM-low/RAM-high split the machine demonstrably has.
`RKDL` is genuinely an input whenever `KDL` is high — pin 17's own enable
term is `KDL'` — so its value comes from outside the chip.

Recorded as what it is. The array is now *readable*; making it *evaluable*
needs the schematic or some instrumentation of the real levels.

## 2026-08-30 (11) — the PAL's structure, and stopping one fact short

Started evaluating `ABC-P4-1.bin`, the PAL16L8 that decides the ABC806's
memory map. Got the structure and stopped before the answer, on purpose.

### What the array gave up

The file parses cleanly into 64 product rows of 32 columns. Fuse polarity
is the standard one — `1` blown, `0` intact — confirmed by the unused rows
being all-intact (every input ANDed with its own complement, which is the
conventional "never" encoding) and landing exactly where unused rows
should.

Rows group in eights, one group per output, and **the first group drives
pin 19**, not pin 12. That was my first wrong assumption and the data
corrected it with two independent checks:

- **Pin 15 (KDL) comes out permanently tri-stated with zero live terms** —
  precisely right for a pin MAME's own list marks as an *input* rather than
  an output.
- **The four signals MAME actually reads** — ROMD, HRE, MUX, RAMD, the ones
  its comment marks `>` — are exactly the always-enabled outputs carrying
  many live terms.

Under the opposite grouping both facts land on the wrong pins. Two
unrelated consistency checks agreeing is what makes that a determination
rather than a preference.

### Where it stops

The 32 columns are 16 input lines by two polarities, and **which column pair
carries which pin is not established.** A PAL16L8 interleaves dedicated
inputs with output feedback in an order the datasheet fixes, and I have no
primary source for it here.

I tried two candidate layouts. Both fail the simplest possible check — that
with EME off and KEYDTR high the array must select ROM below `0x8000` and
RAM above it. So I stopped.

That is the whole point of this entry. I could have kept going: the term
dump is suggestive, columns 25 and 28 pair up throughout, columns 4/8/12/16
travel together in a way that looks like consecutive address bits, and
column 31 appears alone as a term in three different outputs. Any of those
would support a story. **A story assembled from a structure I have already
guessed wrong twice is not evidence**, and the last few days of this journal
are a record of what happens when a plausible reading gets written down as a
finding.

So `scripts/palanalyse.py` dumps the terms, documents what is established
and what is not, and names the one thing that unblocks it: a PAL16L8
fuse-map column table from a datasheet. With that the array can be
evaluated and compared against both MAME's approximation and this
emulator's own fetch-window rule — which was derived from watching the
machine and would then finally have an independent check.

### Worth noting about the shape of the work

Three of the last four entries have been corrections. This one is the same
lesson applied *before* the mistake instead of after, which is the only
version of it that costs nothing.

## 2026-08-30 (10) — the two "unverifiable" PALs verify after all

`abc806/resources/rom/README.md` said, plainly and at some length, that the
ABC806's two PALs **"have no MAME entry to check against … unlike the other
fourteen they rest on the archive alone"**, and made a small virtue of
saying so rather than letting them look like the same standard of evidence.

Both statements were wrong. MAME carries both PALs, and both now verify
byte-for-byte on CRC32 *and* SHA1:

```
ABC-P4-1.bin     crc32=3cc5518d sha1=343cf951d01c9d361b695bb4e80eaadf0820b6bc
ABC-P3-1.bin     crc32=f3d0ba00 sha1=bcc0ee26ecac0028aef6bf5cb308133b509bb360
```

Sixteen of sixteen, not fourteen of sixteen.

### Two reasons they were missed

**MAME never reads them.** They sit in `ROM_REGION("abc_p3")` and
`ROM_REGION("abc_p4")`, but `read_pal_p4()`'s actual PAL lookup is
commented out — so they appear nowhere a search for *behaviour* would land,
which is how I had been reading that file. A ROM entry with no reader is
invisible to anyone looking for what the code does.

**They could not be compared directly even once found.** The archive ships
JEDEC ASCII, 2,769 and 2,754 bytes; MAME stores the 260-byte binary its
`jedparse` produces. Identical fuses, different containers, and a naive
checksum comparison just fails.

The conversion is 4 bytes of fuse count big-endian followed by the fuses
packed 8 per byte **least significant bit first**, uninverted. I did not
know that and did not guess it: I swept the four plausible conventions
(endianness x bit order x inversion) against MAME's published checksums.
**Two independent files agreeing on one convention** is what makes that a
determination rather than a curve fit — one file matching under some
convention would prove very little.

`scripts/jed2bin.py` now does it, so this is reproducible instead of a
claim in a document.

### What the pin lists gave for free

Above each `ROM_LOAD` MAME has a full pinout, which nothing here had
recorded. Both are now in the ROM README and the P4 one in the hardware
reference.

Two things in it are suggestive. The address inputs are split **`A15`/`A14`
but `B13`/`B12`/`B11`** — two different prefixes, where MAME's own
behavioural model feeds all five from one address — and **`M1L` is an input
alongside them**. Those are between them exactly the ingredients of the
fetch-window rule this emulator now implements, which was derived from
watching the machine rather than from the fuse map. So evaluating the array
would be a genuinely independent check on a rule I currently believe on
behavioural evidence alone. Recorded as the reason to do that work, not as
a claim about what it will find.

### The shape of the error

Worth naming, because it is the third of its kind this week. The original
claim was not careless — it was *specific*, it was hedged in the right
direction, and it was written to be honest about weak evidence. It was
still false, because "I looked and did not find it" was recorded as "it is
not there."

The fix is the same one that keeps recurring: when a document says
something cannot be checked, that sentence is a task, not a conclusion.

## 2026-08-30 (9) — bin/abc806-gtk, and the value of having nothing left to write

The third Cairo framebuffer window in this repository, and the shortest —
612 lines against the ABC802's 591 and the ABC80's 1,452. It renders the
ROM's sign-on, real coloured text, and the high-resolution layer, takes
live keyboard input, and quits cleanly on `SIGTERM`.

The interesting thing is how little of it is about the ABC806. Nothing
machine-specific lives in the file at all: `abc806_step()` was already
shared with the CLI's `--interactive` loop, and the pixel decode was
already a pure function with its own fixture. So the app turns palette
indices into a Cairo surface and stops. The colour attribute plane, the RAD
PROM's scanline substitutions, double width, the cursor and the
high-resolution layer all arrive for free — and, more usefully, *cannot
drift* from what `--screenshot` produces, because they are the same code.

That is the return on two earlier decisions that both looked like overhead
at the time: extracting `abc806_step()` in milestone 1, and splitting the
drawing out of `render.c` into a pure `text.c` so a fixture could reach it.

### Two differences from bin/abc802-gtk, both from colour

- **The framebuffer is palette-indexed, not monochrome.** That app maps
  every non-zero pixel onto one amber foreground. Here each byte is a pen
  0-7 and the surface is built through `abc806_palette()`.
- **The flash phase has to be supplied.** The ABC802 blinks its cursor in
  software through the CRTC, so pacing execution was the whole
  implementation there. The ABC806's flash is a hardware attribute with no
  software clock behind it, so the phase comes from elapsed real time.

One detail in the headless path is deliberate: **the flash phase is
pinned** rather than taken from the clock, so a screenshot is reproducible.
A render that sometimes catches the dark half of the flash cycle is a
render that sometimes fails, and that is a miserable thing to debug later.

### Verified without disturbing a desktop

The rule this repository already learned — automating a screen capture
against the user's real desktop steals focus and switches Spaces while they
are working — means the app has to verify itself. Clean build; a headless
render of the sign-on; a headless render of
`FGCTL 2:FGPOINT 10,10,1:FGLINE 200,120,1` showing white text above a red
line at 2x; and a launch/terminate cycle exiting **0** with an empty log.

Worth noting a small honesty check on that last one. My first attempt
backgrounded the process in a subshell and read `$?` from `wait`, which
reported **127** — `wait` complaining about a non-child, not the app
failing. Reporting that as the app's exit status would have been wrong in
either direction; re-running it under a real parent gave 0.

### And a pre-existing gap the work exposed

`make clean` did not remove `bin/abc802-gtk`, `bin/abc802-chargen-dump` or
either's objects — they had been added to the build without being added to
the sweep, and my new target would have joined them. All four are in now,
and a clean/rebuild cycle was run to confirm the tree really does come back
empty. That is the same class of omission as the stale roadmap claims
entry (8) turned up: things appended in one place and not the other.

## 2026-08-30 (8) — splitting the ABC806's docs, and writing its reference

Housekeeping, but the kind this project treats as part of the work rather
than after it.

`ABC806_ROADMAP.md` had reached **604 lines, 552 of them finished-work
narrative**, against 209 for the ABC80's and 217 for the ABC802's — both of
which had already split their history into a `*_COMPLETED.md` for exactly
this reason. The ABC806 had become the outlier, and the split is mechanical
once the convention exists: the milestone write-ups move verbatim, the
roadmap keeps a status table, the open gaps and a ranked "what is next".
100 lines and 570.

Nothing was rewritten in the move. The "found the hard way" notes are the
most valuable thing in those write-ups and rewriting them from memory is
how detail quietly goes missing.

### The reference was overdue, and the roadmap said so

The old roadmap opened with "there is no `ABC806_REFERENCE.md` yet … worth
writing once the machine does something visible, which it does not yet."
That condition had been satisfied several commits earlier and the sentence
had simply stayed put — which is its own small lesson about deferral notes:
they do not check themselves.

Writing it was worth more here than on the other targets, because this
machine has an unusual concentration of facts that are not inferable from a
memory map and were scattered across a roadmap, five journal entries and a
handful of source comments: the fetch-window memory rule, the M1 character
window, the inverted page map, the attribute-byte-as-command encoding, the
`RAD` scanline substitution, the palette carrying the horizontal
resolution, the RTC's clock edges and inverted CS, the latch bit
assignments, port `0x37` serving two devices on read and a DIP switch on
write, and the four-colour pen encoding.

Gathering them in one place turned up something too: the reference's own
claim that a character cell is 6x10 was worth checking rather than
asserting, and `--screenshot` reporting 480x250 for 80x25 confirms it
arithmetically.

### Two stale claims found by doing this

Both in files I had edited within the last few commits:

- The roadmap's Known gaps still said **"The high-resolution plane is not
  rendered — `--screenshot` draws the text layer only"**, two commits after
  it started rendering.
- `CLAUDE.md` still said the target was at **milestone 4** with "no
  high-resolution graphics", after milestone 5 was finished and documented.

Neither is dramatic, and both are the predictable cost of appending to a
status section instead of re-reading it. Worth noting that the *split*
found them: moving 552 lines forces a read of every line, which a normal
edit does not.

## 2026-08-30 (7) — the colours were right; I was reading a small picture by eye

Entry (6) ended by reporting that colour was broken: four lines drawn with
pens 1, 2, 4 and 7 "came out white, and two of them did not appear at all".
Both halves of that are wrong, and the way they are wrong is worth keeping.

### What is actually true

**A `FG*` command's pen argument is masked to two bits and selects the
plane nibble `0xC | (pen & 3)`.** Pen 0 writes `C`, pen 1 `D`, pen 2 `E`,
pen 3 `F`; pen 4 wraps back onto `C` and pen 7 onto `F`. It is a
four-colour mode, and `FGCTL` programs the palette for exactly those four
entries.

Three lines drawn under `FGCTL 2` render **red, green and yellow** —
`#FF0000`, `#00FF00`, `#FFFF00`, read out of the PNG's own pixels.

### Why I thought otherwise

- **"All white."** They were drawn under `FGCTL 1`, whose palette sets
  `hrc[D..F]` to `FF` — white for every pen. Nothing was wrong; I picked
  the one mode that makes every pen look identical, then judged the result
  by eye off a 480×250 screenshot rendered small in a terminal.
- **"Two vanished."** One used pen 4, which wraps onto nibble `C`,
  unprogrammed under `FGCTL 1` and therefore transparent — correct. The
  other was at a screen row still covered by text, and the layer only shows
  through where text is black — also correct, and a rule I had implemented
  and written up myself an hour earlier.

So the renderer was right, the ROM was right, and the report was wrong.

### The specific mistake

Entry (6) says, approvingly, that this was "found by *looking at the
picture*" and that the byte counts had been correct in all four runs. That
was the right instinct and I then stopped one step too early: I looked at
the picture instead of **sampling** it. Twelve lines of Python reading the
PNG's pixels would have said `#FF0000` immediately.

The general shape is familiar from this project's own postmortems: an
observation was real (the lines did look uniform), a conclusion was
attached to it without a second measurement, and the conclusion inherited
the observation's credibility. I have now done this with a *visual* check
after doing it twice with textual ones.

The fix is in the tooling rather than in resolve. `bin/abc806`'s summary
now prints the plane's **distinct byte values**, not only a count, because
a count cannot tell one pen from another — which is exactly the gap that
let a correct renderer look broken.

### Tests, and one that was weaker than it looked

Six checks pin the encoding, one per pen. They assert the plane's *full*
value set — `C0 CC` for pen 0, `D0 DD` for pen 1 — because a horizontal
line writes whole bytes along its body and a half byte at each end.

The first version asserted only the leading value, and a sabotage that
masked the low nibble off every plane write sailed through all six. With
both values asserted the same sabotage reds seven checks. Worth noticing
that the weak version *looked* thorough: six checks, one per pen, all
passing.

## 2026-08-30 (6) — the ABC806's picture, and a palette that hides it

`bin/abc806 --screenshot` now draws the high-resolution plane. A
`FGLINE` from (10,10) to (100,100) renders as a clean white diagonal
beneath the ROM's text, rising left to right — which is what that line
looks like once y is flipped. Milestone 5 is done.

### The layer was invisible for a while, correctly

The renderer worked before I could see anything from it. The plane had its
91 pixels, the code was MAME's own loop, and the screenshot was blank.

**`hrc` was all zeros**, which is the state the ROM boots in. Every nibble
looks up an entry of 0, so every dot is a *transparent* pen 0 and nothing
is drawn. `FGCTL` is what programs the palette — any non-zero argument but
128 writes three or four entries — and without it a perfectly drawn line is
genuinely invisible on real hardware too.

That is a nice property rather than a nuisance: a zero palette disables the
layer, so the renderer needs no enable flag.

### Three things about the decode worth writing down

- **The displayed bank is HRS's *low* nibble; the CPU writes through the
  *high* one.** Independent on purpose, so the machine can draw into one
  area while showing another — and a renderer reading the wrong nibble
  works perfectly until something double-buffers.
- **One byte becomes four pixels through two lookups**, because each `hrc`
  entry is itself two pixels of four bits (bit 3 opaque, bits 2:0 pen). So
  **the palette carries the horizontal resolution**: both halves alike
  gives 240 wide, different gives 480. That is a genuinely clever piece of
  hardware and not something I would have guessed from a memory map.
- **The layer is not on top.** A dot draws where its opaque bit is set *or*
  where the text left black, so text punches through its own foreground and
  neither plane needs a mask.

### The fixture, and why it is not optional

Same argument as every other visual thing on this machine, for the fourth
time: the ROM draws no high-resolution graphics on its own, so a boot
screen validates none of this. `bin/abc806-chargen-dump` now renders a
synthetic plane beside its synthetic text, covering the four things that
break independently — the bank nibble, the four-pixel expansion, the opaque
rule, and the −16 pixel offset between plane and text column 0.

The opaque row is the one I like: `hrc[2] = 0xA0` is opaque pen 2 followed
by transparent pen 0, so alternate pixels let the character glyphs show
through and the fixture shows `272727...` interleaved with the text
underneath. All three sabotages red it.

### What the picture then told me

Four lines drawn with pens 1, 2, 4 and 7 came out **white, and two of them
did not appear at all**. Pens landing on `hrc` entries that `FGCTL` never
programmed are transparent, so they vanish.

> **Corrected in [entry (7)](#2026-08-30-7--the-colours-were-right-i-was-reading-a-small-picture-by-eye).**
> The colours were right — three lines under `FGCTL 2` render red, green
> and yellow. The four "white" lines were drawn under `FGCTL 1`, whose
> palette is white for every pen, and the two that "vanished" were one
> transparent pen and one hidden behind text. Both correct.

So the third argument to `FGLINE` is not a pen index, and the mapping from
it through `hrc` is still unknown — the routine at `0x7677` duplicates a
nibble into both halves of a byte and something further masks it. Recorded
as open. It is a BASIC/ROM-level question rather than a renderer one, and
the renderer is right by three independent checks regardless.

Worth noting how that was found: by *looking at the picture*. The count of
non-zero plane bytes was correct in every one of those four runs.

## 2026-08-30 (5) — the ABC806 draws, and the switch was not a switch

The ABC806 now draws into its high-resolution plane. `FGPOINT 10,10,7` then
`FGLINE 100,100,7` writes 91 bytes, and replaying them gives a clean 45°
diagonal — x from 108 down to 18 as y runs 139 to 229, which is
`(10,10)`→`(100,100)` with y flipped and the +8 viewport origin the ROM
keeps at `0xFEF8`.

### The answer was in MAME after all — as a TODO it doesn't implement

Entry (4) ended by saying this needed the schematic or MAME's own handler
and that the ROM had given up everything it was going to. The right move
was therefore to go *read* MAME, which I had not done: the whole ABC800
family lives in one file, `src/mame/luxor/abc80x.cpp`, and
`abc806_state::read_pal_p4()` carries this commented out:

```c
/*
    if (!m1l && (offset < 0x7800)
    {
        TODO 0..30k read from videoram if fetch opcode from 7800-7fff
        ...
    }
*/
```

**When the executing instruction was fetched from `0x7800`-`0x7FFF`,
accesses below `0x7800` go to the plane instead of ROM.** Where the code
runs from *is* the switch. Nothing is switched — which is exactly why two
sessions of hunting for a software trigger found nothing, and why every
elimination in entry (4) was correct and yet led nowhere. I was looking for
a thing that does not exist.

Everything that had refused to fit falls into place at once. The ROM's
30,720-byte memset lives at `0x7CAC`, inside the window, so its `LDIR`
reads *and* writes reach the plane and the propagate works. `FGLINE`'s
plotter at `0x7E31` is in the window. `FGPOINT`'s executor at `0x763B` is
not — and `FGPOINT` correctly draws nothing. The interpreter's data reads
run from code all over the low 32K, outside the window, so they still get
ROM.

In entry (3) I wrote that the clear routine sitting immediately above the
region it clears was "not where you would put a fill routine by accident."
That was the mechanism, visible a day before I could read it.

### The lesson I actually want to keep

(Written up as [naming a source is not consulting
it](postmortems/2026-08-30-naming-a-source-is-not-consulting-it.md), after
it happened a second time the next day.)

Entry (4) is a good piece of work — five mechanisms eliminated, each with
evidence — and it ends with "the ROM alone has given up everything it is
going to", which was true. What it should have ended with is *going to the
other source right then*. I had named the source myself, in writing, in the
previous session's roadmap, and then spent a whole turn doing more of what
had already stopped paying.

**Naming what would settle a question is not the same as trying it.** The
eliminations were not wasted — they are why I trusted the MAME sketch
immediately instead of doubting it — but they were the expensive way round.

Worth noting the sketch is self-contradictory: its condition tests `!m1l`
(*this access is an opcode fetch*) while its comment describes the opposite
(divert because the opcode *was* fetched from `0x7800`-`0x7FFF`). The
comment matches the hardware; implemented as written the condition does
nothing. Presumably that is why it is still a TODO — and implementing it
correctly is one of the places the scoping document hoped this project
would exceed its reference.

### A one-sided bounds check broke the disk

First version tested only `current_fetch_pc < 0x7800`, which admits all of
high RAM. Two reads by DOS code at `0xC178` and `0xC32A` got diverted into
the plane, and the DOS's sign-on came back as `Ver 6.00` with a truncated
date instead of `Ver 6.20`. A missing upper bound in the graphics path,
surfacing as text corruption in the disk operating system.

`dos-runs-lib` caught it — the check added yesterday, for an unrelated
milestone, against media the repository does not even ship.

### What the tests do and do not cover

Two new media-free checks: `graphics-fgline-draws` asserts **exactly 91**
bytes (90 Bresenham steps plus the start point, one byte each because the
line is diagonal at a 128-byte pitch — an exact count makes it a geometry
check), and `graphics-plane-clears` asserts the plane comes up zero, which
only holds if the memset's reads reach the plane.

Three sabotages. Narrowing the window's address range reds the first.
Widening the fetch-PC bound reds `dos-runs-lib` and nothing else. And
**changing the HRS bank shift is caught by nothing at all**, because every
test runs with `hrs = 0` — recorded as a gap rather than left to be
discovered by someone else.

## 2026-08-30 (4) — chasing the ABC806's memory switch, and eliminating most of it

The user's read of entry (3) was that the low address space must be
switched between ROM and framebuffer. That is right, and saying it out loud
sharpened the argument into something I could actually test:

> The ROM would not `memset` 30K into its own EPROM, so the low 32K has to
> be DRAM at that moment. BASIC's interpreter demonstrably reads its own
> data down there afterwards, so it has to be ROM later. Both cannot be
> true without a switch.

So I went looking for the switch. I did not find it, but I eliminated most
of the places it could be, which is worth as much.

- **Not the 74ALS259.** A graphics command issues no I/O that a bare `REM`
  does not — same ports, same values, same counts. And in all 32K of ROM
  there is exactly one immediate-form latch write: `LD A,80h / OUT (36h),A`
  at `0x00DC`. **EME goes on once at boot and is never turned off.**
- **Not the page map.** All 256 entries are written by the loop at
  `0x00D2` — which clears the map and `hrc` together — every one `0x00`,
  never touched again. And a uniformly-zero map is degenerate under every
  reading of the entry format: "zero diverts" aliases all sixteen pages
  onto one, "zero does not divert" disables it. There is no third reading
  that makes a constant map into a switch.
- **Not KEYDTR.** One WR5 write, `0x68`, ever.
- **Not symmetric reads**, and not symmetric reads with the executing
  instruction's own bytes excluded. Both kill the machine.

### The thing that made the first experiment mislead me

That second variant deserves its own note, because the reason it was worth
trying at all is a fact about *this project's core* rather than about the
ABC806.

Instrumenting `bus_read` shows **11.2 million "data reads below `0x8000`"
in a boot-plus-`REM` run**, essentially all at page `0x05`. They are not
data reads. They are **instruction operand fetches**: address `0x054D` read
with PC sitting at `0x054E`.

`fetch_byte()` indexes the flat array directly and deliberately bypasses
`bus_read_hook` — that is the documented arrangement every ABC target
relies on — but immediate operands go through `z80_read_byte()` and so
*do* reach the hook. A naive "divert data reads to the plane" therefore
diverts most of the instruction stream's operands too, and the machine dies
on a garbage jump thousands of instructions from the cause. Which is
exactly what happened, and I briefly read it as evidence about the ABC806
when it was evidence about the emulator.

Worth adding: real hardware could not use that split either. Only the
opcode fetch is an M1 cycle on a Z80; operand fetches are ordinary memory
reads. So the M1 idea is not merely awkward to implement here, it is
probably wrong about the machine.

### Two hardware facts fell out sideways

**Port `0x37`'s one write is a DIP switch being read.** At `0x0418` the ROM
reads DART channel B's RR0, tests bit 5 — CTS — and writes `09` or `0A` to
port `0x37` accordingly. That is the same channel, and one of the same two
pins, that the ABC802's reference already documents as carrying
configuration switches. Pleasing to have it turn up independently on the
other machine.

**The latch encoding is confirmed rather than assumed.** `LD A,08h / RRA`
at `0x7543` rotates the carry into bit 7 and leaves `0x04` beneath it, so
the index is `value & 7` and the state is bit 7. I had it right, but I had
it right by inheritance from the ABC802; now it is right by evidence, on a
family that does not use one convention throughout.

### Where this leaves it

No software-visible trigger exists in this ROM, so the switch is
structural. Settling it needs the schematic or MAME's own `abc806` memory
handler; the ROM alone has given up everything it is going to. That is a
better place to stop than another hypothesis — and the roadmap now lists
the eliminations, which is the part that saves the next attempt time.

## 2026-08-30 (3) — the ABC806 does draw, into a framebuffer I was throwing away

Picked up yesterday's open question and got most of the way through it.
Also had to correct yesterday's own conclusion, which was wrong in a way
worth recording.

### The correction first

Entry (2) said the graphics commands "write to no address a bare `REM`
does not". That was wrong, and the reason is instrument design rather than
reasoning: I compared the **set of distinct addresses** written, so every
address also touched during boot cancelled out. Comparing write **counts**
per address shows `FGPOINT 10,10` writing `0xFEFC`-`0xFEFF` exactly once
more than the baseline.

Which is correct behaviour. `FGPOINT` sets the graphics cursor; it does not
draw. **The command I had picked as my test could not have produced a pixel
even on perfect hardware.** A diff that came back empty felt like a strong
negative result, and it was an artifact twice over.

### What FGLINE actually does

`FGLINE 100,100` after `FGPOINT 10,10` writes four bytes at `0xF155` about
91 times each — a Bresenham loop, one iteration per step of a 90-pixel
line. Its plot is at `0x7E31` and is a masked read-modify-write:
`LD A,D / XOR (HL) / AND E / XOR (HL) / LD (HL),A`.

And **every one of those 91 writes was being discarded** by
`if (addr < 0x8000) return 1` in `memory.c`. Their targets are `0x4589`,
`0x4609`, `0x4689` — `0x80` apart.

### The geometry, read out of the ROM

That stride is the whole answer to a question I had expected to need a
datasheet for. 128 bytes per row; 240 pixels at 4bpp is 120, padded to 128.

The ROM confirms the rest itself. A single instruction at `0x7CB4` does
**30,719 discarded writes covering `0x0000`-`0x77FF`**, and the code is

```
7CAC: LD DE,0001h
7CAF: LD BC,77FFh
7CB2: LD (HL),00h
7CB4: LDIR
```

— the classic fill-with-a-constant idiom. So the framebuffer is at
`0x0000`-`0x77FF`: 30,720 bytes, 240 rows of 128, **ending exactly where
character RAM begins**. `FGPOINT`'s own range check (`LD HL,00EFh`, 239)
confirms the height independently.

The detail I liked most: **the clearing routine sits at `0x7CAC`, above the
framebuffer** — the only part of the low 32K that could still be ROM while
the plane covers the rest. That is not where you would put a fill routine
by accident.

### And where I stopped

I tried routing low-32K writes into the plane. They land, and it is almost
certainly half the answer. I did not commit it.

The reason is that **both the clear and the plot need their *reads* to come
from the plane too** — `LDIR` reads the region it fills, the plot reads
`(HL)` twice — and with reads still answering from ROM, the clear
propagates a ROM byte instead of zero and the plot writes ROM-derived
values. The trace shows exactly that: `4500 <- C9`, `4501 <- FD`.

Making reads symmetric breaks the machine outright: the interrupt vectors
and the ROM's data tables live down there too, and it dies on `ED 00` at
`0x0084`. So the question is not whether reads divert but **what
distinguishes a ROM data read from a plane data read at the same address**,
and none of the three mechanisms I have modelled answers it. KEYDTR is
written once and never changes. EME is on but the page map is uniformly
zero — 256 entries, all `0x00` — and no reading of a uniformly-zero map
gives a sensible per-page mapping. And there is **no bank-switching `OUT`
anywhere in the graphics path**.

Committing the write half alone would have left the source implementing a
model I already know produces wrong data, which is worse than an honest
gap: the next person would see writes working and assume the rest was
right. So the drop stays, with the evidence written at that exact line and
the full trail in the roadmap.

### Two things found sideways

**The option PROM's only latch writes** (`0x7546`, `0x754D`) bracket a read
of port `0x37` and set latch bits **2 and 4** from two bits of a value. So
the HRU II palette PROM's address needs both, where `ports.c` models bit 2
as `hru2_a8` and drops bit 4 as "TXOFF". Recorded, not fixed: nothing reads
the palette for real yet, so there is no way to tell a correct change from
a plausible one — which is the same discipline the rest of this entry is
about.

**`0x7502` is FGCTL's tokenizer, not its executor.** It calls a vector
dispatcher at `0x762F` that reads its target from `(0x001E)+2`, then writes
token bytes. Worth knowing before profiling one of these commands again and
concluding from the address list that it "ran".

## 2026-08-30 (2) — the ABC806's disk milestone was already done, and its graphics are not where I expected

Two milestones looked at today. One turned out to be finished; the other
turned out to be a different problem than the one I went looking for.

### Milestone 4 was free, exactly as the scoping document predicted

`BYE` leaves BASIC for the real DOS command shell, and the shell loads and
runs `LIB` off the disk, which gives a full directory listing. No
ABC806-specific work at all: the shared `abcbus/` card and yesterday's RTC
were between them the whole of it.

The listing is the assertion worth making, not the banner. `LIB` is a
*program on the disk*, not a shell built-in, so a listing means the bus,
the controller, the filesystem and the DOS's own loader all worked end to
end — where a banner only means a boot sector ran. Both are checks now, as
two separate runs, because `LIB`'s output scrolls the banner off the top
and I would otherwise have been asserting against a screen that no longer
held it. (It failed that way first.)

### The graphics commands exist, run, and draw nothing

Milestone 5 is high-resolution graphics, and the scoping gate is "a BASIC
program using the option PROM's commands draws something". So the first
question is what those commands are called.

Dumping the option PROM's keyword table with the high-bit token markers
made visible — the same technique the ABC802's BASIC reference was built
from — gives **`FGPOINT`, `FGLINE`, `FGFILL`, `FGCTL`, `FGPAINT`,
`FGPICTURE`**. And they are live: `FGCTL 1,1` answers `Error 221` and
`FGLINE TO 100,100` answers `"," saknas`, which is the machine telling me
its own argument syntax rather than me guessing it.

They are enabled, too. The dispatcher at `0x763B` gates the whole package
on a flag at `0xFEF4` and bails to `0x0012` when it is zero;
`PRINT PEEK(65268)` reads back 1.

And the plane stays at `0/131072 bytes nonzero`. Every command, every
`FGCTL` argument from 0 to 255.

### Three instruments, and what they said

The temptation here is to guess. Instead:

- **A differential profile.** `ABC806_PROFILE_ALL=1` dumps every executed
  address and its count; running once with the graphics line and once with
  a bare `REM` and diffing the sets gives **994 addresses executed only in
  the graphics run**, across both the BASIC ROM and the option PROM. So it
  is not silently skipping. Real code runs.
- **A write trace.** `ABC806_TRACE_WRITES=1` puts every CPU write on
  stderr along with EME, KEYDTR and HRS — the three bits that decide where
  a write lands. They go to ordinary high RAM, `0xF3xx` most heavily.
- **The state itself.** KEYDTR never changes for the life of the run, every
  page-map entry stays zero (which with the inverted polarity means "do not
  divert"), and the 74ALS259 is written three times during boot and never
  again.

So: **the CPU never opens a window onto the high-resolution plane at
all.** That is the finding. Not "the graphics do not work" — something
much more specific, and a much better starting point.

### What I did not do

I did not write down which of my three hypotheses is the answer, because I
did not test any of them. The leads are that port `0x37` is read during
these commands and not otherwise (HRU II, addressed here with register B
plus the latch's A8 line — possibly wrongly); that the code may be building
a display list for a blit these sequences never trigger; and that the
option board may need to announce itself in a way this emulator does not,
which would produce exactly the observed "runs fully, writes nowhere".

Each is plausible and each is cheap to write up as though it were the
cause. That is precisely the move that produced two wrong published
explanations yesterday — `BYE`'s `Abort 48` recorded as undiagnosed when it
was an interleave problem, and `LIB` blamed on an unbound `DR0:` that a
trace disproved. **A hypothesis written down without testing inherits the
credibility of the observation it sits next to.** So the roadmap section
says the path has not been found, lists the leads as leads, and stops.

The renderer is still unwritten either way — 240×240 at 4bpp, banked by
`hrs`, through the `hrc` lookup — and when it exists it will need its own
fixture, because there is no ROM output that exercises it. Which is the
third time that sentence has been true on this machine.

## 2026-08-30 (1) — the ABC806 gets a live session, and colour that can be tested

`bin/abc806 --interactive` is now a real session: 3 MHz pacing, a screen
redrawn thirty times a second **in colour**, a live keyboard, Ctrl-\ to
quit. Driven through a pty it takes `PRINT 6*7` to `42`, takes
`PRINT "ÅÄÖ"` back out as `ÅÄÖ`, and with `--disk` boots real UFD-DOS and
shows the date off the clock that went in yesterday.

That was milestone 3, and the gate was met early — `--type-at 40000000
--type $'PRINT 6*7\r'` already answered `42` before I wrote a line of it.
Most of the day went into the parts the gate does not cover.

### The screen was lying, readably

`--screen` printed `AABBCC880066` for a screen that reads `ABC806`. Not a
bug exactly: it dumped raw character cells, and this ROM writes its banner
double-width, so character RAM really does hold each letter twice. But it
meant the dump showed the machine's *memory* rather than the machine's
*screen*, and I had written a regression assertion against the doubled
form only the day before, complete with a confident comment about how
asserting on it was deliberate.

It now walks the attribute plane and prints one character per drawn cell.
The assertion moved to the plain form, and that is strictly stronger:
collapsing correctly *requires* decoding the attribute plane, so a broken
attribute walk now shows up in the text dump. The version I defended was
the weaker one.

### Two copies of one state machine, again

The text renderer needed the same attribute walk the pixel renderer had.
Writing a second copy is what caused the double-width bugs three entries
ago — a text dump and a PNG disagreeing about the same screen is what
exposed them. So `abc806_decode_row()` came out of `chargen.c` first and
both renderers now go through it. Behaviour-preserving, and the fixture
said so immediately.

### The part the boot screen cannot check, for the third time

The ABC806's eight colours are the reason it is not an ABC802. Its boot
screen is white on black and uses one attribute. Those two sentences
together are the whole argument: a live session would render that screen
perfectly with the colour mapping completely inverted, and I would have
shipped it feeling verified.

This is [the boot-screen postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md)
arriving a third time, on a third feature. It is starting to look less
like a lesson about one bug and more like a property of this whole
project: **the ROM's own output is a sample the hardware authors chose,
and it systematically under-exercises whatever the machine can do that the
sign-on does not need.** Every target here has now been caught by it once.

The fix has the shape the postmortem prescribes. The drawing came out of
`render.c` into a pure `text.c` — screen struct in, characters out, no
live machine anywhere — and `bin/abc806-chargen-dump` now emits the text
dump and the ANSI frame alongside its pixel art, ESC written as `\e` so
the fixture stays a diffable text file. The committed fixture holds the
real codes: `37;40` white on black, `31;44` red on blue, `[4m` underline,
`30;40` where flash has dropped the pen into the background. Swapping
foreground and background reds it, which I checked before believing it.

`render.c` kept only the half that asks the machine, and one consequence
was free: the `Abc806Screen` snapshot is now assembled in exactly one
place, so `--screen`, `--screenshot` and the live frame can no longer
disagree about what the screen currently is. They were assembling it
separately before, which is the same shape of mistake one layer up.

### A solved problem, reintroduced by starting from a blank page

`--type` fed its argument to the keyboard as raw bytes, so
`PRINT "ÅÄÖ"` reached BASIC as UTF-8 and errored while an interactive
session typing the same letters worked fine.

That is not a new bug. It is
[the one with its own postmortem](postmortems/2026-08-28-type-raw-utf8-bytes.md),
found and fixed on the ABC802 two days ago, in the file whose
job is exactly this. It came back because I wrote this target's `main.c`
from scratch instead of from the one that already had the fix in it — and
the same choice is why the terminal glue, the pacing constants and the
input state machine all had to be rewritten rather than reused.

Worth being precise about the tradeoff, because "just share the code"
is not obviously right here: each machine target owning its console glue
is this repository's deliberate standing choice, and it has been the
correct one twice. What it does not buy is *bug* isolation. A fix in one
target's glue does not reach another's, and nothing announces that when
you start the third copy. The charset table is now in its third copy too,
and `abc802/emu/src/render.c`'s own comment says the third consumer is the
moment to extract it. I left it, and wrote down why in the file: the
shared thing would be a table plus a decoder plus an encoder, and where it
should live depends on whether this target ends up sharing more with the
ABC802 than a character set — the same open question `ports.c`'s
duplication is already waiting on. One answer, or neither honestly.

### And the keyboard was ready before it was listening

Typing at T-state 0 arrived as `INT 6*7`, the first two characters gone.
The ABC802 has the same property and answers it with `--type-at`, a
caller-supplied delay. Here I could do better: the sign-on is drawn at the
very end of boot, immediately before the keyboard poll loop, so "there is
a non-space character on screen" is a real readiness signal rather than a
constant tuned against one machine's timing. `--type` waits for it and now
needs no help. `--type-at` stays for the different problem it was actually
built for — a program booting off disk that starts listening long after
the ROM's own sign-on, which no screen check can see.

### Two new checks, both broken on purpose first

`keyboard-basic-answers` and `keyboard-swedish-roundtrip`, neither needing
media. The first asserts on BASIC's *answer*, not only on the echoed line
— [that exact mistake](postmortems/2026-08-29-test-matched-the-echoed-input.md)
was found in these suites once already, passing with its subject entirely
broken. Bypassing the UTF-8 conversion reds the
round-trip; forcing the readiness gate open reds both.

## 2026-08-29 (17) — the ABC806 asks what day it is, and now gets an answer

Boot the ABC806 from a real 640K ABC832 UFD-DOS system disk and the DOS
prints a date line. Until today it printed this:

```
Datum och tid: 19é5-é5-é5  é5.é5.é5
```

Now it prints this, with the host's own clock beside it for comparison:

```
host time:     2026-08-29 20.25.32
Datum och tid: 2026-08-29 20.25.32
```

That was the whole job: the E050-16 real-time clock. It is the one device
the ROM visibly asks for and did not get, which is exactly why it was
worth doing next — small, and with a gate nobody can argue about.

### Why the gate is stronger than it looks

A date line is a weak-looking assertion. It is not. Every digit in it is a
BCD nibble that reached the CPU one bit at a time through a shift
register, so a date that reads correctly means the four-bit command
encoding, the register order in the continuous-read latch, the clock edge
each direction moves on, and the bit order within a byte are *all* right
simultaneously. Any one of them wrong and the line is garbage again,
usually differently-shaped garbage. There is no partial credit here, which
makes it the rare case where one string comparison genuinely covers a
whole protocol.

### The device has no bus

Worth stating plainly, because it shapes everything: the E050-16 is not
memory-mapped and has no data bus. Three bits of the 74ALS259 addressable
latch at port `0x36` drive chip select, clock and a bidirectional data
line, and that data line is read back as **bit 7 of port `0x37`**. One
register read is therefore thirty-odd `OUT`s and `IN`s of bit-banging by
the DOS, and modelling it means modelling a shift register and its edges,
not a register file.

CS low arms a four-bit command — address in bits 3:1, read/write in bit 0.
Address 7 is not a register but a mode, continuous read-out of all seven
registers as a single 56-bit transfer, and that is the one the DOS uses.

### Found the hard way

- **Port `0x37` had to become two devices at once.** It was returning a
  constant `0xFF`, which is not "unimplemented" — it is a data line stuck
  high. The clock appeared to answer every read with all bits set, which
  is where `é5` came from. The port now returns the HRU II palette PROM's
  low nibble *and* the clock's bit 7 from the same read, which is what the
  hardware does and what MAME's `cli_r` already said it does.
- **Reads move on the falling clock edge, writes on the rising one.** This
  looks like an asymmetry to tidy away and is not. Getting it wrong yields
  a value that is plausibly shaped and off by one bit position — the worst
  failure mode available, because it reads as a real date on some seconds
  and not others.
- **CS is inverted between the latch and the chip.** A *set* latch bit
  deselects the clock.
- **The ABC806 ties OUTSEL high**, which deletes the chip's
  high-impedance read state and its special case. That simplification is
  the board's, not the chip's — flagged in `rtc.c`'s own header, because
  anyone porting this to another ABC800-family machine needs to know it is
  not free.

### The test, and what it costs

`make test-abc806` gains `disk-boot-and-rtc`: boot real media, assert the
DOS agrees with the host about the date. It copies the image to a
temporary directory first, because the DOS writes to media and a test must
never mutate the user's archive — a lesson from [entry (7)](#2026-08-29-7--driving-the-real-thing-and-what-a-live-session-found)
earlier today, where a shared test disk made the user's own writes look
like an emulator bug.

It needs an image this repo deliberately does not commit, so it **skips
loudly** and is counted apart from the passes. That is the same bargain
the ABC80 and ABC802 suites already make, and it is the right one: a check
that silently passes when its subject is absent is worse than no check.

The assertion is on the *doubled* text — `--screen` dumps raw character
cells and this ROM writes its banner double-width, so `2026` is `22002266`
in character RAM. Asserting on the doubled form is deliberate rather than
sloppy: it is what the machine actually put in memory, and normalising it
away would quietly stop testing the double-width path that took two bugs
to get right this morning.

## 2026-08-29 (16) — the ABC806 draws: a delay loop, and two bugs the picture caught

`bin/abc806 --screenshot` now renders **ABC806**, the ROM's own sign-on.
That is milestone 2's gate *as originally written* — reached a few hours
after I replaced it for being unreachable.

### What it was waiting for

Disassembling the loop was the whole answer, and I should have done it
first instead of ruling out the disk and the DOS PROM by experiment:

```
7621: LD A,10h / OUT (C),A   ; reset external status on DART channel B
7625: IN A,(C) / XOR B       ; what changed since the baseline read?
7628: AND 10h / JR Z,7621    ; spin until bit 4 changes
```

It waits for **RR0 bit 4 to change** — the DART's RI input — as a delay.
Not for a keypress. Which is why sending one changed nothing even though
the keystroke demonstrably arrived: I had been testing the right stimulus
against the wrong hypothesis, and the loop's shape said so in nine
instructions.

Channel B's RI is now driven from a 50 Hz square wave. Written up in the
source as inference, because it is: MAME drives channel A's RI and channel
B's CTS and leaves this one alone. What is observed is that the loop waits
on a *change* and that a periodic source satisfies it; the rate is the
part that matters.

### Two rendering bugs, both found by looking

With the ROM finally drawing, `--screen` said `AABBCC880066` and
`--screenshot` said `A B C 8 0 6`. **Two views of the same screen
disagreeing is a bug by definition**, and that disagreement is the only
reason either was found.

The banner is written as alternating attributes `FF, 07`. `0xFF` has
foreground equal to background, so it is command 3 — double width — with
e5/e6 in its low bits and the colours taken from the `0x07` beside it. The
renderer got the column skip right and the width wrong twice: pixel
doubling was driven by the screen's 40-column flag rather than by e5/e6,
and x was computed as `column x width` rather than by advancing a pen
across what was actually drawn.

### My own fixture missed both

Its double-width row used attribute `0xC0` — command 3 with e5/e6
*clear*. So it exercised the attribute-inheritance branch and never
doubled a pixel. The synthetic screen that exists specifically because a
boot screen cannot validate attributes had itself picked the command
without the operand.

Fixed to use the ROM's own `FF`/`07` pattern, which renders visibly
doubled glyphs. The general lesson is sharper than "test the feature":
**a hand-written test input encodes the author's model of the feature, so
it fails in exactly the places the model is wrong.** The real ROM's
attribute pattern was available the whole time and would have been a
better source than my idea of one.

### And one thing I could not close

Reverting the x-position fix does not change the fixture output, and I
could not work out why within a reasonable time — the instrumented run
shows the pen at 0/12/24 where the broken form would give 0/24/48, so the
outputs should differ and do not. Recorded in the roadmap as unproven
rather than described as covered. That specific regression is currently
caught by looking at the real banner, which is weaker than a check and
should be said so.

---

## 2026-08-29 (15) — ABC806 milestone 2: a decode verified, and a gate replaced

The text renderer works: `--screen`, `--screenshot` in eight colours, and
`bin/abc806-chargen-dump` exercising every attribute path. Three checks in
a new `abc806/tests/`, now part of `make test`.

### The gate did not survive contact

`ABC806_SCOPING.md` set milestone 2's gate as "renders the ROM's own
sign-on banner". **This ROM draws no banner.** It boots, configures 80×25,
clears the screen and polls the keyboard forever, writing not one visible
character.

So the gate tested something the machine does not do. I replaced it rather
than quietly dropping it, and the replacement is stronger: verify the
decode against a *synthetic* screen exercising colours, underline, flash,
blank, keep-previous and double width — none of which a banner would have
touched.

Which is the boot-screen postmortem's own conclusion, arriving from a new
direction. On the ABC802 the banner rendered and still proved nothing
about attributes; here it does not render at all. **A gate written before
the work can be wrong about the machine, not just about the effort** — and
the fix is to ask what would actually constitute evidence, not to lower
the bar until the machine clears it.

### What the decode is

Nothing transfers from the ABC802. That machine hides attributes in the
character generator's output byte; this one has a parallel plane and a
PROM:

- An attribute byte whose fg and bg **match** is a *command*, not a
  colour — keep previous, reserved, blank, double width. Black on black is
  unreachable as an ordinary attribute, which is what makes it work.
- Underline, flash and double height are **never drawn**. They index the
  RAD PROM, which returns a *scanline address*, and the font is addressed
  with that instead of the real row. Same idea as the ABC802's attribute
  trick, different mechanism.
- Double width is described by the cell *before* it: command 3 takes its
  own e5/e6 and the colours from the next cell's attribute byte.
- The glyph is six bits from the top of the font byte after a two-place
  left shift; the low two bits are not pixels.

Changing that shift by one bit turns the fixture red, which is the only
evidence worth having that it is checking anything.

### The compiler found a bug I would not have

`(port & ~0x18) == 0x37` — flagged as always false. It is: `~0x18` clears
bits 3 and 4, and 0x37 has bit 4 set, so the test can never match. MAME's
`mirror(0x18)` on a base that already has bit 4 set produces the set
{0x27, 0x2F, 0x37, 0x3F}, and 0x27/0x2F collide with DART mirrors.

Nothing reads that port yet, so I took the narrow certainly-correct pair
and documented the collision instead of guessing which device wins. Worth
noting that `-Wtautological-bitwise-compare` caught a *masking* error, the
same class as milestone 1's two — three now, all in port and map decode.

### The banner, ruled down

Not solved, but the search space is much smaller, and every step was a
trace rather than a guess:

- The keyboard **does** reach the machine — a sent byte moves RR0 from
  `0x24` to `0x25`, receive-character-available.
- The loop at `0x7621` is a real poll: `LD A,0x10 / OUT (C),A / IN B,(C)`
  on port `0x23`, in the *option* PROM.
- It is **not** waiting for a disk: a real ABC832 UFD-DOS system image
  changes nothing.
- It is **not** the DOS PROM: v.19 and v.20 are identical.

So it wants something it has not been given, and the keyboard is not it.
The remaining candidates are the RTC and the protection device, both on
the 74ALS259 whose bits are decoded and dropped. That is milestone 3.

---

## 2026-08-29 (14) — ABC806 milestone 1: a fourth machine boots

`bin/abc806` runs the real Luxor ABC806 firmware and passes the gate
`ABC806_SCOPING.md` set for milestone 1 — executes past reset and programs
the CRTC. Three signs say it is a real boot rather than a survival: the
CRTC is set for **80×25**, which is this machine's geometry and not the
ABC802's 80×24; character RAM holds exactly 2000 bytes of `0x20`, so the
ROM cleared the screen it had just configured; and execution settles into
a six-address loop polling port `0x23`, DART channel B — waiting for a key.
Both committed DOS PROMs boot identically.

### Scoping earned its keep again

Written yesterday, checked today. It got the shape entirely right — the
firmware was complete and verified in one pass, half the machine
transferred, and the risk was correctly placed in the memory decode, where
every real difficulty turned out to be.

It was wrong in the reassuring direction on the biggest risk: it predicted
the MMU might force a change to the shared core's instruction-fetch path.
It did not, because the ABC806's *reset* state is ROM-low/RAM-high, exactly
the ABC802's shape, so the resident-32K arrangement carried over untouched.
Deferred rather than retired — firmware executing out of a mapped page
would still break it.

And the question it said to answer first is answered: `ABC-P4-1.bin` is a
well-formed JEDEC fuse map, 64 product lines of 32 fuses, exactly a
PAL16L8's array. It is committed and deliberately unused. A PAL evaluator
written before anything booted would have had nothing to check itself
against; now it would.

### The bug worth remembering

**The page map's entries are stored inverted.** MAME reads them as
`m_map[page] ^ 0xff` before testing ENL in bit 7, so an entry of zero —
what the map holds at reset — means "do not divert". I implemented the test
the other way round, so the moment the ROM enabled EME, *every* access went
to video RAM.

The failure appeared as an illegal `ED C3` at `0x05D1`, thousands of
instructions and one subsystem away from the cause. What found it was
bisection: disable one port handler at a time and see which one restores
the boot. Three tries, and the answer was unambiguous.

Second one of the same family: `0x34`-`0x36` have **no low-byte mirror**,
and decoding them as `port & 0x3F` also claims `0x74`, `0xB4` and `0xF4` —
CTC mirrors. Both bugs are "a mask that is nearly right", and both killed
the machine far from where they lived.

### A deliberate duplication

`abc806/emu/src/ports.c` is a near-copy of the ABC802's, because the CTC,
SIO, DART and CRTC are the same chips. That is not laziness and it is not
an accident — it is the order `abcbus/` was built in. Extract the shared
module once it is known what is genuinely common; find that out by having
a second consumer far enough along to say. At milestone 1 it is not.

The differences found so far are exactly the argument for waiting: DTR-B is
LRS on the ABC802 and KEYDTR on the ABC806 — same chip, same pin, different
wiring — and ports `0x06`/`0x07` are ABC-bus lines on one machine and video
registers on the other. A premature extraction would have unified two
things that are not the same.

### Where it stops

No banner. The ROM clears the screen and waits without writing visible
text, where the ABC802 shows its sign-on first. Whether that is a real
difference or something the machine wants before it will draw is the open
question milestone 2 starts from — and since it is polling the keyboard,
milestone 3 may simply answer it.

---

## 2026-08-29 (13) — the right disk, and a bug that halved every one I made

The user pointed at <https://www.abc80.net/archive/luxor/>. Its
`640k/index.txt` transcribes the physical disk labels, so `grep -i ufd`
found three ABC832 UFD-DOS system disks without downloading anything.
`disk039.img` closed two items that had been open for three sessions.

### LIB was never broken

```
-LIB
** Library list **
Drive MF0:
SYSDIR  .SYS BASICINI.SYS ADDOPT  .ABS ...
 1960 av  2528 sektorer lediga.
```

Full listing, first try. The `0x2C`-vs-`0x2D` diagnosis was right and is
now demonstrated rather than argued: give the DOS the controller it
addresses and its own utility works.

The same disk also settled `DR0:` by simply saying so at boot — `DOS är
UFD-DOS ver. 20 / DR_: motsvarar MF_:`. Two sessions of inference about
that mapping, answered by a sign-on message.

### The bug it exposed

`LIB` printed `1960 av 2528 sektorer lediga`. `bin/abcdisk` said the same
disk had 170 clusters free. Those disagree: 1960/4 = 490.

**`FORMAT_MF.usable_clusters` was 320 and should have been 640.** An
ABC832 has 2560 sectors, four to a cluster; minus the eight-cluster system
area that is 2528 sectors — exactly LIB's number. My value came from
`sana23.dsk`, the single 640K image I had, whose pristine bitmap marks the
upper half allocated. Atypical media, read as the format.

So every disk `abcdisk create --type mf` has made was **half-size**, and
nothing caught it: the round-trip test saves one small program, and the
suite never asks how much space there is. Fixed, and verified the right
way round — the real DOS's own `LIB`, on a disk the tool created, now
reports `2528 av 2528 sektorer lediga`.

**The test gap was closed too**, which matters more than the fix. The
suite now asserts both drives' free-cluster counts, at `create` time and
again through `list`, and the numbers are taken from the machines rather
than from arithmetic: 632 from a 640K disk's own `LIB` line, 616 from the
figure ABC80's suite already pins against real media. Re-introducing the
exact `320` and re-running turns `abcdisk-create-mf` red, which is the
only evidence worth having that a regression test works.

A formatter can produce a perfectly working disk of the wrong size. Every
functional check here passed throughout — save, load, list, round trip —
because none of them asked how much room there was.

That is the fourth time today that ABC830-vs-ABC832 hid a defect, and
the second where **one sample of 640K media was mistaken for the format**.
The rule that keeps earning itself: a constant derived from a single
artefact is a description of that artefact.

### Also worth having

`abcdisk list` on the three new disks agrees with the machine, which is
independent confirmation of the cluster fix from the previous session —
its start sectors are all multiples of four, and they resolve to real
descriptors.

### Still open

`DOSGEN` on a 640K system now starts and reaches its media-verify pass,
then walks cluster addresses far beyond the 2560 sectors an image has and
calls each one bad. `LIB` on the same disk is fine, so it is specific to
that pass rather than to the bus model. Recorded in the roadmap.

And the archive is worth remembering as a *documentation* source, not just
media — `doclist/`, `ABC80x/` and `Prom/` are all there, and the BASIC II
manual that anchors the language reference came from it.

---

## 2026-08-29 (12) — the DOS's file-extent semantics, and a converted disk that boots

Went back at the extent question. It is answered, and the proof is a
160K disk converted to 640K that autoboots the real ORD 800 word
processor.

### The rules

- **The allocation unit is the cluster, not the sector.** The descriptor's
  `last` field is `start + clusters - 1`. On the ABC830 a cluster *is* a
  sector, so it reads as `sectors - 1` and the distinction is invisible —
  which is exactly why the first conversion attempt used sectors and made
  the DOS read ten clusters where it should have read three. Confirmed
  against 23 of the 25 files on real 640K media.
- **Iteration is a flat logical index**: sector *i* is at cluster
  `start + i/spc`, offset `i mod spc`. The trace shows it directly — `MO`
  advances the cluster with offset always 0, `MF` runs the offset 0-3 then
  advances.
- **Per-sector framing is three bytes** — file id, sequence, zero — with
  253 bytes of payload, and the first sector is a descriptor rather than
  data.
- **The directory's byte-length field is optional.** Some disks record it,
  some leave it zero. When present the DOS honours it exactly: set it too
  large and the load fails with `Error 38`, "sector number outside the
  file".

That last one is a nice property to have found by *over*-shooting. Setting
a length that was correct-looking but too generous produced a different
error from setting none at all, and the pair of errors bracketed the real
semantics better than either alone would have.

### The demonstration

`ord800.dsk` converted to 640K boots ORD 800 Version 2.4. That is the
whole model — cluster allocation, descriptor rebasing, directory slots
preserved, free-list rebuilt — working end to end on real 1980s software.

### Four hypotheses, three wrong

Worth listing, because the wrong ones each looked convincing:

1. *The length field is required for MF.* **Wrong** — stripping every
   length from `ord800` and reconverting still boots. Tested by
   deliberately zeroing the field rather than by reasoning about it.
2. *Files with a partial last cluster over-read into the 0xE5 tail.*
   **Wrong** — `PRESTART.BAC` is 7 sectors in 2 clusters and loads fine.
3. *The system disks are special because they lack lengths.* **Wrong** —
   four of their five `.BAC` files load cleanly from the conversion.
4. *The allocation unit is the cluster.* **Right**, and it is the one that
   actually mattered.

The three wrong ones all had supporting evidence at the moment I formed
them. What killed each was a control: strip the field and retest, pick a
file with the opposite property, try every file instead of the first one.
**The cheapest way to kill a plausible hypothesis is to construct the case
it forbids** — considerably cheaper than reasoning about whether it is
true.

### Loose end

On a converted system disk, `DIRCOPY.BAC` still fails to load while the
other four `.BAC` files are fine. On the original it loads, and the trace
shows the DOS reading its ten sectors and then two more elsewhere on the
disk — sectors that the conversion has moved. Not isolated; noted in the
roadmap rather than guessed at.

---

## 2026-08-29 (11) — converting 160K to 640K: the diagnosis holds, the image does not

Converted `sys10sw.dsk` to a 640K ABC832 image to test whether `LIB` and
`DOSGEN` work once an `MF` controller is fitted. Partial result, one real
bug found in committed code, and the file format finally understood.

### The result

**On `MF` media, `LIB` stops bailing and renders a listing** — the "Drive
0" header and a full grid of rows, where on `MO` it printed a banner and
returned. The `0x2C`-vs-`0x2D` diagnosis is therefore right: the select
was the blocker.

**But the converted image is not valid.** The rows are filled with `e`
(0xE5 padding read as names), and BASIC gets `Error 37` loading files from
it: the DOS reads one sector *more* per file than the file has. So this is
evidence for the diagnosis, not proof of the claim that a genuine 640K
system disk would work.

### Three format facts, each found by a failure

The conversion failed twice before getting this far, and each failure
taught the format:

1. **`Abort 48`** — I had sorted the directory by start sector. Every data
   sector carries a file id of `0x10 × (slot + 1)`, so reordering the
   directory invalidates every sector of every file that moved. Preserve
   slots.
2. **`Abort 37`** — the first sector of a file is a *descriptor*
   (`id, 00, 00, FF, last(2), FF, FF`) whose `last` field is the start
   address plus the sector count. Copied verbatim, it still pointed into
   the old layout. Rebasing it fixed that error and produced a real
   listing.
3. **Still `Error 37`** — the remaining gap. `last - start` is exactly
   `len - 1` on every original file, and my rebased value matches, yet the
   DOS reads 11 sectors for a 10-sector file on `MF`. Its end-of-file test
   is not the simple count the descriptor suggests. Left there.

### The header is three bytes, not six

The most useful by-product. A file's sectors carry `id, seq, 00` and 253
bytes of payload — not the six-byte header I had assumed when
reconstructing `LIB.ABS` from the disk. **That is exactly the three-byte
framing error** that made the earlier disassembly disagree with itself and
sent me to a RAM dump. The RAM dump was still the right call, but the file
could have been read correctly with this known.

### A real bug in bin/abcdisk

Directory start fields are **cluster** addresses, not sector numbers: the
real sector is `cluster × sectors-per-cluster`. On the ABC830 a cluster is
one sector, so the two coincide — and `abcdisk list` reported the raw
field as a sector, which was silently a factor of four out on every
ABC832 image. Fixed and verified against the machine: a file BASIC just
saved to a fresh `MF` disk now reports sector 32, which is where the bus
trace shows the DOS actually wrote it.

This is the third time today that ABC830-vs-ABC832 has hidden a defect
by making a wrong formula look right. The interleave was the first, the
directory-on-a-track-boundary the second, and `cluster == sector` the
third. **On this machine, anything verified only against 160K media is
half-verified.**

### Scope

Stopped here. Making the converted image fully valid means reverse-
engineering the DOS's file-extent semantics, which is a different project
from "convert and test" and would be reverse-engineering someone else's
1981 filesystem rather than the machine.

---

## 2026-08-29 (10) — LIB disassembled: one cause, not two, and a third correction

Disassembled `LIB.ABS` and found where it gets its directory. The answer
folds `LIB` and `DOSGEN` into a single root cause, and corrects a claim
committed a few hours earlier.

### Getting the code

The file on disk is not the program. Sectors carry a 6-byte header, and
reconstructing the image from them left the disassembly 3 bytes out of
frame — visible as long `DB` runs and as `CALL` targets landing mid-
instruction. Two independent anchors (a string pointer and a known code
pattern) gave contradictory bases, which was the signal to stop guessing.

**The fix was to stop reading the file and read the machine.** A throwaway
`--dump-mem` flag on `bin/abc802`, used once and reverted, produced the
real 64K image with `LIB` resident. Everything after that was
straightforward: load address `0xC701`, entry `0xC764`, and message
strings that are *length-prefixed* — which is why `LD HL,0C716h` pointed
at `0x13` and not at `A` of "ABC800 Library list", 19 characters long.

Worth keeping: when a reconstruction and a disassembly disagree, the
emulator is holding the ground truth and can simply be asked.

### What LIB actually does

| Address | Holds |
|---|---|
| `0xC700` | LIB's control block; `(IX+3)` = last drive to scan, plus one |
| `0xFD50` | drive parsed by `CMDINT` — `0xFF` none, `0xFE` invalid |
| `0xFD01` | current drive in LIB's scan loop |
| `0xFD12` | the DOS's sector-buffer pointer |
| `0xFD18` | the ROM's read-retry counter, set to 3 |

With no argument, `0xFD50` is `0xFF`, so `(IX+3)` = `(0xFF AND 7) + 1` =
8 while `(0xFD01)` is already 8 — and the loop's bounds check exits
before any I/O at all.

With `LIB DR0:` the loop runs once. Its first act is to read the
free-space bitmap and count free clusters for the `… av … sektorer
lediga.` line: `LD HL,(0FD12h)` then `CALL 6066h`, a genuine ROM vector
into the read-sector path. That read **selects `0x2C`** — the ABC832/834 —
gets nothing from the fitted ABC830 at `0x2D`, retries three times and
returns carry. `LIB` takes the `JP C` and skips the whole listing section,
which is where the directory read (`CALL 600Fh`) lives.

Confirmed on the bus: exactly three `0x2C` selects after the program
loads, matching the retry counter, and `DR0:`, `DR1:` and `DR2:` all
identical.

### One cause, not two

That is precisely `DOSGEN`'s problem. **The DOS's logical drives map to
the ABC832, every system disk here is an ABC830, and one controller is
fitted at a time.** Two "separate" known gaps collapsed into one roadmap
entry.

### The third correction in three sessions

The committed claim was "once loaded, LIB issues no ABC-bus commands
whatsoever". That is true — of the no-argument case, which is what I
traced. With a drive argument it *does* issue commands, on the wrong
controller. I measured one case and wrote the sentence as though it
described the feature.

Three sessions, three corrections, and they rhyme:

1. `BYE` "undiagnosed" — never tested the why.
2. `LIB` "`DR0:` unbound" — hypothesis written as finding.
3. `LIB` "issues no bus commands" — one case generalised to all.

The first two were untested guesses. This one was worse in an interesting
way: it *was* measured. The failure was scope — a measurement of the
default invocation, stated as a property of the program. **A trace proves
what the traced run did.** Varying the input before generalising costs one
more run.

### Not chased

Whether a 640K system disk would make both work. There is no MF image here
carrying DOS utilities, and converting a 160K one is not a copy — the
geometry differs at four sectors per cluster.

---

## 2026-08-29 (9) — why LIB lists nothing: not the reason I published

Chased the `LIB` gap. It is still open, but three plausible explanations
are now dead and one published claim was wrong.

### The claim I got wrong

The reference said `LIB` lists nothing because it defaults to `DR0:`, a
logical name the system binds at boot, which never happens when the DOS
is entered from a ROM-booted BASIC. That was a hypothesis, and it read as
a finding.

**A bus trace disproves it in one line.** `LOAD "DR0:LIB"` selects `0x2D`,
addresses unit 0, and reads the directory at sector 16 — exactly like
`MO0:`. `DR0:` resolves perfectly well. Corrected in the reference and
recorded in the roadmap.

Two sessions ago the same thing happened with `BYE`: a hypothesis
("undiagnosed") hardened into documentation. The pattern is specific
enough to name — **when something does not work and I write down why
without testing the why, the guess inherits the credibility of the
observation.** The observation was real both times; the explanation was
invented both times.

### What the trace actually shows

Stranger than the guess. **Once loaded, `LIB` issues no ABC-bus commands
at all.** Every read in the trace is `CMDINT` loading `LIB` itself
(sectors 116-127 for `LIB.BAC`, 98-102 for `LIB.ABS`), and after the last
sector of the program the bus goes quiet. It is not failing an I/O; it
never attempts one.

So it takes its directory from somewhere other than the disk — a DOS
service call, or a resident copy the ROM populated when it loaded the
program — gets an empty result, and prints its headers over zero rows.
Option 3 even emits the `S = Skrivskyddad  R = Raderskyddad` legend that
would head a real listing.

### Ruled out

- **Wrong controller.** Unlike `DOSGEN`, which always selects `0x2C`,
  every `LIB` access is on `0x2D`, the fitted card.
- **DOS ROM version.** `ABC802-dos.32-21.bin` behaves identically to the
  default `32-31`.
- **The disk.** `sys10sw` and `sys10fi` both do it, in Swedish and
  Finnish respectively.
- **Argument syntax.** `LIB MO0:` is refused outright with `Felaktigt
  drivenummer`; `LIB DR0:`, `LIB *.*`, `LIB DR0:*.*` and the menu-driven
  `LIB.BAC` all run and list nothing. The menu's "Drive:" prompt wants a
  bare number — `0`, not `MO0:` — which the pty made it possible to
  discover at all.

### Where it stops

Going further means disassembling `LIB.ABS`, which is third-party software
on the media rather than anything in this repository's ROMs, and the
payoff would be understanding someone else's 1981 utility rather than the
machine. Left as a documented gap with the evidence attached, so the next
person starts from "it does no I/O" instead of from scratch.

Nothing is blocked by it: `bin/abcdisk list` reads the same directory
correctly and needs no machine at all.

---

## 2026-08-29 (8) — BYE was never broken, and there *is* a formatter

Chasing why `BYE` printed `Abort 48` took one command to answer and then
opened up the whole other half of the machine.

### The answer

`Abort 48` was the wrong interleave. The `BYE` test was written *before*
the logical-vs-physical dump discovery, so it ran without `--interleave 0`
and `CMDINT.SYS` was read from the wrong sectors. Error 48 is "failure in
system data", which was telling the exact truth. With the right mapping:

```
ABC800 DISC OPERATING SYSTEM
VERS 1.01  Feb '81
*   R E A D Y   *
```

and `$BAS` returns to BASIC. The reference had recorded `BYE` as broken
and undiagnosed; that entry has been replaced.

**The lesson is about blast radius.** One wrong constant did not just
break file reads — it made a whole subsystem look unimplemented, and it
got written into the documentation as a defect. Every observation made
before that fix deserved re-testing, not just the ones that obviously
touched sectors. Two more were wrong for the same reason: several disks
*do* autoboot (`red800.dsk` comes up as BOKFÖRING 800 v 2.0), which had
been recorded as "reaches a bare prompt".

### There is a FORMAT after all

The DOS's command set turns out to be simply the `.ABS` programs on the
disk — an unknown name answers `Förstår ej`, and `SYSTEM` prints the
inventory. On a Luxor system disk that includes **`DOSGEN — Formattering`**.

So six places in the docs saying "neither ROM has a FORMAT command" were
literally true and materially misleading. There is a real formatter; it
lives on a disk rather than in ROM. Corrected everywhere, and the honest
framing is better for `bin/abcdisk` than the old one was: on real hardware
**you need a working disk to make a disk**, and the tool breaks that
circularity — which is exactly the situation someone with a bare checkout
and a few downloaded images is in.

Worth noticing how the wrong claim survived: it was verified (no FORMAT
keyword is in either ROM - true), it was repeated across six files, and
none of the repetitions re-checked it. Verifying the *literal* statement
is not the same as verifying the impression it leaves.

### Trying to run DOSGEN, and two mistakes on the way

The prize would have been a byte-for-byte comparison of a disk formatted
by the real DOSGEN against one built by `bin/abcdisk` — the strongest
possible check on a format that was reverse-engineered rather than
documented. It did not happen, and the two dead ends are both worth
recording.

**First attempt: I answered the tool wrong.** DOSGEN asks "Enkel eller
dubbel densitet?" and I said D. Double density is the 640K ABC832, so it
went looking for a card that was not fitted. That was visible in one line
of bus trace — `[out] 01 <- 2C`, `[in ] 01 -> FF` — and invisible on
screen, where it printed `Skivan formatteras !` and appeared to work.

**Second attempt, with E for single density: identical.** So it was not
the density answer. DOSGEN selects `0x2C` — the `MF` controller —
*always*, whatever drive number or density it is given; answering
"Drivenhet? 0" produces the same select. Every disk here that carries
`DOSGEN.ABS` is a 160K ABC830, which fits an `MO` controller at `0x2D`,
and this emulator models one controller at a time. Its format commands
reach no card, and the image is left byte-for-byte untouched — verified by
md5 before and after.

So this is **the first time real software has hit the documented
"two drive types, one card" limitation**, which until now was a
theoretical note in the roadmap. A real ABC802 could carry an ABC830 and
an ABC832 at once. Recorded there and in the reference.

Two process notes. The screen said `Skivan formatteras !` in every failed
run — **the only thing that distinguished success from silent no-op was
hashing the file**, which is why the third run started from
`/dev/urandom` rather than a blank image: against a zero-filled target,
"nothing was written" and "it was formatted" can look alike. And the bus
trace answered in seconds what staring at the screen could not, which
argues for reaching for `ABC802_TRACE_IO=1` earlier than I did.

### Still open

`LIB` under the DOS runs but lists nothing, and `LIB MO0:` is refused with
`Felaktigt drivenummer`. It defaults to `DR0:`, a logical device name the
system binds at boot — which never happens when the DOS is reached from a
ROM-booted BASIC instead of by booting the disk. Recorded as a known gap.
The BASIC-side `RUN "MO0:LIB"` does not depend on it and works.

The DOSGEN comparison stays open too. It needs either two controllers
modelled at once, or a 640K system disk — and the one 640K image here
carries no DOS utilities.

---

## 2026-08-29 (7) — driving the real thing, and what a live session found

`bin/abcdisk` and `--interleave` were both verified through scripted runs.
This session put them under a genuine `--interactive` session and worked
through the whole disk command set, which turned a reference table of
syntax into a table of confirmed behaviour.

### Getting an interactive session at all

This runs as a background job with no TTY, and `--interactive` needs a
real terminal. The way through was a **pty with paced keystrokes** — the
same technique `cpm/`'s dBASE II and RED investigations used years of
commits ago to capture what a program really emits. A small driver forks a
pty, types a script a character at a time, and reads the framebuffer back.

That is worth recording as a general capability rather than a one-off:
anything in this repo that needs `--interactive` can now be exercised
without a human at a keyboard, including the parts of `LIB` that scripted
`--type` cannot reach.

### The command set, now actually run

`SAVE`, `LOAD`, `RUN "file"`, `LIST "file"`, `MERGE`, `UNSAVE`, `KILL` and
`NAME … AS` were each run live against a disk `bin/abcdisk` had formatted,
with the directory read back afterwards to confirm what happened. All
work. `CHAIN` was not tested and is marked as such.

Two findings the syntax tables could not have given:

- **`MERGE` refuses a `.BAC` file with `Error 204`**, and the *same*
  program saved as text with `LIST` merges fine. That is the manual's own
  rule confirmed from both sides rather than transcribed.
- **`UNSAVE` and `KILL` genuinely release clusters** — deleting the only
  file on a fresh disk returns the free-list to exactly its as-formatted
  state — **but the next save does not reuse the hole.** A file deleted
  from clusters 24-25 leaves them free while the next one lands at 28.
  The allocator appends. Harmless, but it means a churned disk fragments.

That last one is also the strongest evidence yet that the reconstructed
free-list is right: real firmware is reading it, allocating from it,
freeing into it, and the result stays consistent across sessions.

Also noticed: `LIST` re-indents loop bodies, so a listing is the ROM's own
formatting, not the text that was typed.

### An anomaly with a mundane cause: two people, one disk

Midway through, a file deleted two sessions earlier reappeared in the
directory. That looked like a serious defect — either `UNSAVE` not really
deleting, or the directory being restored from a stale copy.

It did not reproduce. `SAVE` alone, then `UNSAVE` alone, on a fresh disk,
in separate processes: clean, and the free-list returns to its pristine
24 clusters. Replaying the entire original sequence on a fresh disk: also
clean.

**The cause was that the user had been driving the same disk.** They were
trying the interface out interactively and re-saved the file themselves.
`UNSAVE` had worked correctly all along, and so had everything else — the
directory was simply showing a write neither of my sessions made.

The lesson is about where a test disk lives. `work.dsk` was created in
`abc802/resources/disks/`, alongside the real media — a *shared* location,
open to whoever runs the emulator next. A disk being written by an
automated check has no business there; it belongs in scratch space, so
that "the directory changed" can only ever mean the software changed it.
The interleave and formatter work avoided this by copying media into a
temp directory per run, which is exactly the habit that should have
applied here too.

Kept in the log anyway, because the reasoning up to that point was right
even though the hypothesis was wrong. An observation that contradicts the
story has to be chased until it reproduces or is explained, and the two
minimal experiments were what established that no defect existed — the
explanation then came from asking rather than from more testing.

### Housekeeping

`scratch/` is now in `.gitignore`. The user added it as their own
workspace and asked that it not be maintained or published; it was
untracked but *not* ignored, and the last two commits used `git add -A`,
so an hour's difference in timing would have published it. Ignoring it is
the fix that survives whoever runs `git add` next — a note to myself would
not have.

---

## 2026-08-29 (6) — bin/abcdisk, and a filesystem read out of the media

A CLI tool that creates formatted, empty ABC-bus disk images, and lists
what is on one. `abcbus/mkdisk.c`, beside `disk.c` and for that file's own
reason: the bus is not a machine and both targets mount the same media.

### Why it had to exist

Two sessions ago the reference document recorded, honestly, that a blank
file is not a blank disk — an all-zero image of the right size attaches,
is recognized, and then fails every `SAVE` with `Error 41`, because it has
no free-list, and neither the BASIC nor the DOS ROM has a `FORMAT`
command. That left the emulator able to *read* real media and unable to
produce any. Every write test needed media the repository cannot ship.

### Deriving the format

Nothing documents it, so it came out of the media by inspection. The step
that made it tractable was noticing that **sector 7 is a pristine copy of
the free-list**: on real disks it reports exactly the system area
allocated and nothing else, while sector 6 tracks the actual files. That
is not a backup, it is a photograph of the disk as formatted — the thing
being reconstructed, sitting on every disk already.

The rest fell out from there:

- The free-list is one bit per **cluster**, MSB first, 1 = allocated.
  Both drives have 640 clusters — the ABC832's four-sectors-per-cluster
  geometry is exactly what keeps its bitmap the same 80 bytes as the
  ABC830's, which is why one constant serves both.
- A directory sector is sixteen zero bytes then fifteen 16-byte records,
  and a free record is sixteen `0xFF`. So an empty directory sector is a
  two-line `memset`.
- **The primary directory is at sector 16, not 8.** Sector 8 is a backup
  the DOS does not keep in sync — of three real disks, two matched and one
  had drifted. Established by saving to a disk built by hand and seeing
  which copy the DOS actually wrote.
- The ABC832 marks everything from cluster 320 up as permanently
  allocated, i.e. this DOS uses only the first half of a 640K disk. Odd,
  but it is what its own real media says, so it is what the tool writes.

Verification was the same shape the disk milestones have always used and
is the only kind that counts here: build an image with the C code, `SAVE`
a program to it under the real ROM, then `LOAD`, `LIST` and `RUN` it back
**in a separate process**. Both drive types pass.

### The listing side earned itself twice

`list` started as a convenience so a created image could be checked
without booting a machine. It immediately did two things worth more than
that.

It **replaced a wrong analysis**. The ad-hoc Python parser used earlier
earlier today to tabulate what was on each disk had the record framing off by
four bytes and reported three disks as undecodable. The tool reads all
ten, and those three turned out to be perfectly ordinary — `ord800.dsk`
holds the *ORD 800* word processor with an `INITIERA.*` file per printer
make. The disks README's contents table has been regenerated from the
tool rather than from that script.

And it **exposed a blind spot in its own test**. Sabotaging the
directory's sector number did not fail anything: the writer and the reader
share the constant, so they agree perfectly on a wrong value. Fixed by
adding a check that has `abcdisk` read *real* media it did not write,
which does catch it. Sabotaging the free-list was caught by the round trip
from the start.

Both sabotages were run and confirmed before trusting either check.

### What this buys the suite

Four of the abc802 suite's disk checks no longer skip on a bare checkout,
because the tool makes their media. The write path — `SAVE` to disk, and
the DOS's own directory and allocation updates — was previously
untestable without third-party dumps. It is now covered by default, and
the suite went from 10 passing to 14.

### Loose ends

- The eight bytes at `0xEF`-`0xF6` of a free-list sector are counters,
  1 on a pristine disk and incrementing with use. Reproduced because real
  media has them; their meaning was not chased.
- Sectors 1-5 of a 160K disk are filled with `@`. Purpose unknown, copied
  for the same reason.
- The size field in a directory record is bytes, confirmed against two
  programs of known length, but real Luxor media often carries 0 there —
  so the tool prints "(size not recorded)" rather than "0 bytes".
- `list` prints names as raw bytes, so a Swedish character shows as its
  ASCII position (`ORDLÄNK` reads as `ORDL[NK`). Mapping it would pull
  the ABC802 charset table into a bus-level tool; left alone.
- **The ABC80 path is not verified.** It mounts the same media through
  the same card and its DOS keeps its directory copies at the same
  sectors, so images from this tool are expected to work there — but
  `bin/abc80` has no scripted-keyboard option, so nothing has driven a
  `SAVE` on that machine. Said so in the tool's own header rather than
  letting "shared infrastructure" imply coverage it does not have.

---

## 2026-08-29 (5) — interleave belongs to the dump, not the drive

`--interleave N` on both `bin/abc802` and `bin/abc80`, plus a home and a
README for disk images. What forced it was a user dropping ten real `.dsk`
images into `abc802/resources/disks/` and finding that nine of them did
not work.

### The finding

Every 160K image read as `Error 37` — file *found*, data garbage — which
is the wrong-interleave signature this project already knew from ABC80's
Milestone 6. But the emulator was using the factor that milestone
established. The images were not the problem and neither was the code.

**Both are right, for different dumps.** abc80.net's `.img` archive stores
ABC830 sectors in *physical* order, so reading it needs the factor-7
permutation. These `.dsk` files store the same media in *logical* order,
so applying that permutation breaks them. Nothing inside an image says
which convention it follows.

Proved before touching any code, by permuting a copy of the image so the
emulator's existing factor-7 mapping would land correctly. `RUN "MO0:LIB"`
immediately brought up a real Luxor directory utility that had been
returning `Error 37` a minute earlier. Then repeated across three more
images, with a negative control: the one 640K image in the set autoboots
*untouched*, because `MF` already defaults to no interleave and that
happens to match this convention. Applying `--interleave 7` to it breaks
it. Opposite defaults, opposite dumps, consistent explanation.

This closes something `ABC802_FLOPPY_SCOPING.md` flagged as a risk before
Milestone 5 and got half right. It predicted "likely a difference in how
the image files themselves were dumped." Milestone 5 then resolved the
abc80sim contradiction *by experiment*, in favour of factor 7, and
recorded it as settled — which it was, for the media in hand. The scoping
document's guess was the better one, and the experiment could not have
distinguished them with only one archive's dumps available.

Worth stating plainly for next time: **a repeatable experiment on one
sample settles what that sample does, not what the parameter means.**

### The flag

`abcbus_disk_set_interleave(factor)` in the shared card, overriding the
drive type's own value; the mask is forced to 15 or 0 alongside it, since
both modeled drives put 16 sectors on a track. Exposed as `--interleave N`
on both machine CLIs — the card is shared and ABC80 has exactly the same
media problem, so wiring only one would have left the capability
half-connected. The startup line now reports the factor in force and
whether it was overridden, which makes any future bug report about disks
self-documenting.

### The symptom is the part worth remembering

Not "the disk does nothing," which is what ABC80's Milestone 6 saw when
interleave was *missing*. Here the card is found, the directory lists
correctly, filenames are all legible, and only real file reads fail. That
is because track-boundary sectors map to themselves under any factor, and
both formats keep a directory copy at such a sector — the same trap
Milestone 6 documented for hex dumps, met again from the other side. A
readable directory is not evidence of a correct mapping.

### Also

- `abc802/resources/disks/` is now a real location with a committed
  README and a `.gitignore` pair, on the terms already recorded for disk
  images: unlike the ROMs, they carry no license statement, so they stay
  out of git while the directory and its documentation do not. The README
  carries the interleave note, checksums, and each disk's contents.
- Those contents were read straight out of the image files. The directory
  is 16-byte records from offset 2068 — `name[8] ext[3] 0xFF` plus four
  bytes, 15 usable slots, `0xFF`-filled ones free. Two of the ten images
  keep their directory somewhere else and were left undecoded rather than
  guessed at.
- `RUN "MO0:LIB"` is now the *verified* answer to "how do I list a disk",
  replacing the reference document's earlier honest hedge. `BYE` — the
  other documented route, into the disk's own `CMDINT.SYS` — prints
  `Abort 48` and is recorded as undiagnosed rather than written up as
  working.

### Not done

The regression suite does not cover the override; the flag is verified by
hand only. Covering it needs a logical-order image the suite can rely on,
and the suite's existing media is all physical-order, so this is a media
problem rather than a test-writing one. Noted in the roadmap.

---

## 2026-08-29 (4) — a BASIC II reference, read out of the ROM

`abc802/docs/ABC802_BASIC_REFERENCE.md`: how to actually *use* the
machine, alongside the disk drives and how a disk is stored. The ABC80
target has had an equivalent since its own Milestone 3; the ABC802 had a
hardware reference and three roadmap-shaped documents but nothing that
answered "what can I type at this prompt."

### Reading the language out of the ROM rather than a manual

The ABC80 document was built from a vendor quick-reference card and
spot-checked against the ROM. This one went the other way round, and the
result was better than expected: **BASIC II's keyword tables are stored in
the ROM in a format that decodes cleanly**, as a token byte with bit 7 set
followed by the keyword's ASCII characters, `0xFF` closing a group. Eight
tables came out of it — operators, functions, attributes, statements,
commands, an extension table, and in the DOS ROM the device names and the
four DOS commands.

Two things fell out for free that a manual could not have given as
reliably:

- **The operator table is stored in precedence order**, with the `0xFF`
  separators marking the group boundaries. So the precedence table in the
  document is read out of the ROM, not inferred by experiment.
- **Synonyms are identifiable, not guessable.** `NEW`/`SCR`,
  `RENUMBER`/`REN`, `LEFT`/`LEFT$` and `ASC`/`ASCII` each appear as two
  entries carrying the *same* token. That settled a contradiction: the
  manual's ABC 802 appendix lists `SCR` among the omitted keywords, but
  the ROM has it sharing `NEW`'s token and the machine accepts it. The
  appendix is removing the ABC 806's high-resolution `SCR`, a different
  keyword with the same spelling.

The first parse was wrong in an instructive way — I read the token as
*following* the keyword, which produced plausible-looking garbage
(`'COS#'`, `'NUM$\''`) for about half the entries and clean names for the
rest. Dumping forty bytes with the high bit marked per byte settled it in
one look. Plausible-looking output from a wrong assumption is the failure
mode to expect when decoding a table format.

### The manual was still needed, and it is a good one

Luxor's own *ABC 800 BASIC II* (English, © 1984) is on abc80.net with an
OCR text layer, 155 pages, and — the part that makes it usable here at all
— **an Appendix 5 devoted to the ABC 802's differences**. It supplied the
error-message table, which the ROM cannot: this BASIC reports errors as
bare numbers and contains exactly one relevant string, `Error`, at
`0x3D6E`. There is no message text in the machine to extract.

Sixteen of its error codes were then reproduced against the real ROM and
marked as such, which was worth doing — it confirmed the OCR's two-column
table had not slipped a row, which was the obvious risk. `CON` with
nothing to continue really is 207, `VAL("ABC")` really is 210, `DOUBLE`
after an assignment really is 211.

### What direct execution found that neither source had

- **`CHR$()` cannot substitute for an attribute word.** `PRINT GWHT;`
  stores byte 23 in the screen cell; `PRINT CHR$(23);` stores 32, a space
  — the print routine filters control codes. Found by peeking the cell
  after each. This matters because turning on graphics mode for a row is
  the *only* way to make `SET DOT` visible, and the obvious workaround
  silently does nothing.
- **The attribute words' tokens are their character codes plus `0x80`.**
  Which means the whole 25-entry table decodes to the character-generator
  attribute codes Milestone 3 already established, and a `PEEK` check on
  seven of them confirmed it.
- **`SET DOT` and `TXPOINT` do not cover the same area, and their origins
  are at opposite corners.** Bisected: in 80 columns `SET DOT` accepts
  position 0-159 and `TXPOINT` 0-157; in 40 columns, 0-79 and 0-77. The
  manual gives 0-77 for both and 2-79 for `CLR DOT` in the same chapter,
  so its numbers are not reliable at this granularity.
- **BASIC II ignores spaces inside keywords.** `P RINT 6*7` prints 42.
  This started as a check that ABC80's one-word `SETDOT` and `INPUTLINE`
  were *absent* — they are not; they are accepted verbatim and re-listed
  as `SET DOT` and `INPUT LINE`. A compatibility feature that would have
  been documented backwards had it not been tested.
- **A blank file is not a blank disk.** An all-zero 163,840-byte image is
  accepted as an ABC830, but `SAVE` to it gives `Error 41` ("disk space
  full") — an unformatted image has no free-list and there is no `FORMAT`
  anywhere in the BASIC or DOS ROM. Worth stating in the document, since
  `dd if=/dev/zero` is the obvious first thing to try.
- **`MEM:`, the 32 KB RAM-floppy, does not work here.** `SAVE "MEM:1"`
  reports no error; `LOAD "MEM:1"` gives `Error 37`, for every number
  tried. Recorded as an observed limitation rather than diagnosed — it
  lives in the RAM the ROM overlays, so a bank-selection path is the
  likely cause.

### Not done

No disk images were available locally this session, so the disk section
rests on the ROM's own device table, the manual, and the verified
transcripts already in `ABC802_COMPLETED.md` from Milestones 5-7 rather
than on a fresh round trip. The per-byte directory layout is described as
ABC80-derived, because that is what it is — the ABC802's UFD-DOS has not
been decoded to the same depth here, and the document says so rather than
borrowing the confidence.

---

## 2026-08-29 (3) — regression suites for the machine targets

Three machine targets, one covered by `make test`. Everything the ABC
targets had ever been verified by lived in a hand-run matrix retyped each
session — which is how the previous entry's change got signed off, and it
would not have survived to the next one.

`abc80/tests/run_tests.sh` (17 checks) and `abc802/tests/run_tests.sh`
(14) are that matrix, kept, with shared reporting in
`scripts/testlib.sh`. `make test` now runs all three suites: 47 checks,
under two minutes including a clean build.

### The stance

No unit tests of internal functions. Almost every bug these targets have
had was a wrong belief about the hardware rather than a wrong line of C,
and a unit test written from the same wrong belief passes — the
`*_COMPLETED.md` files are substantially a catalogue of such beliefs.
Everything drives the real ROMs and asserts on what the machine produced.

Where a check can be written as BASIC, it is. `PRINT INP(6)` exercises the
real port decode, the ROM's own `INP`, and the CPU's I/O path; calling the
same C function exercises none of them. That came from the ABC802 SIO work
and transfers to the ABC80 unchanged.

### Validated by breaking things on purpose

A suite nobody has seen fail is not known to work, so five real
regressions went in one at a time and each had to be caught: the status
bit from the previous entry reverted, the bus decode widened to swallow
the sound port, an ABC802 DIP-switch default flipped, the ABC830
interleave disabled, the mosaic-font address bit changed.

Four were caught. **The port-decode check passed with its subject entirely
broken** — it was asserting on its own input. Full write-up in
[a postmortem](postmortems/2026-08-29-test-matched-the-echoed-input.md),
because the lesson generalizes to every check on these targets: the
machine is driven by typing at it and read by looking at video RAM, and
video RAM contains the echoed input, so input and output share one
buffer.

Rerunning the sweep found two more checks that were weak rather than
wrong — `disk-boot` asserted only the absence of an error, and the
UFD-DOS check asserted a "file not found" that a completely dead card
also produces. Both now assert on the card's own `ABCBUS_TRACE=1` output,
so they fail when the bus stops carrying anything.

Of seventeen checks written that afternoon, three were not testing what
their names claimed, and the suite reported green throughout. A check is
finished when it has been seen to fail for the right reason.

### An aside on measuring

The hand-run matrix used instruction caps up to 120,000,000 where
3,000,000 does. They had been copied forward from an earlier session and
never questioned. At roughly 1.7M instructions/sec that was most of a
minute per check spent on nothing, and measuring the minimum took two
commands. Worth remembering next time a number gets carried across
sessions because it worked once.

### A false alarm worth recording

Twenty minutes went into suspecting the SN76477 was broken, because
`OUT 6,255` from BASIC rendered silence and so did `OUT 6,0`. Both are
correct. `0xFF` disables the chip and inhibits the mixer; `0x00` selects
an envelope mode that produces nothing without a trigger. `0x40` — the
value the demo tool itself uses — gives a clean tone. The bit layout was
in `sound.c`'s own header comment the whole time, and the near-miss was
writing the phantom up as a known gap in the roadmap.

It turned into the seventeenth check. `sound-register-from-basic` drives
the register through the CPU rather than calling the model directly, which
covers `step.c`'s decoding of BASIC's compiled `ED`-prefixed `OUT` — a
path nothing else touched. Breaking that decode now fails it.

### Also found

The ABC80 emulator runs at about 1.7M instructions/sec, several times
slower than the ABC802 on the same shared core. Nothing depends on it, but
it is most of why one suite takes 20 seconds and the other 3; recorded as
a Performance note in the ABC80 roadmap rather than chased. And
`bin/abc80-video-timing-dump` already spoke PASS/FAIL and set an exit code
of its own — it had simply never been run automatically. It is now.

---

## 2026-08-29 (2) — ABC80 Milestone 12: retiring the PC-address trap

The ABC802 roadmap's top candidate, taken up directly: replace the ABC80
target's floppy trap with the real ABC-bus card the ABC802 already had.

### The move

`abc802/emu/src/disk.c` went to `abcbus/disk.c` at the repo root, beside
`z80core/`. The reasoning is the same one that put `z80core/` there — the
ABC bus is a bus, not a machine — and it had to happen *before* the ABC80
could use the card, or `abc802/` would have quietly become a shared
library for `abc80/`, which is the exact arrangement moving `z80core/` out
of `cpm/` was meant to end. `abc80/emu/src/disk.c` became `abcbus.c` and
kept only what is genuinely machine-specific: loading the DOS ROM into the
`0x6000` expansion window, and this machine's own `0x17`-masked port
decode.

Before writing any of it, the first question was whether the ABC80's DOS
ROM even speaks the same protocol, since assuming so would have been the
whole risk. Scanning `ABCDOS80.bin` for `IN`/`OUT` opcodes answered it in
one command: ports 0, 1, 2 and 4, and nothing else. Same registers, same
C1/C3 pulses. Disassembling `0x612B` then showed the same four-byte
`B`/`C`/`D`/`E` header and the same `0x03`/`0x0C` command constants the
ABC802's ROM uses. Two unrelated ROMs, five years apart, agreeing on a
bitmask — which is what makes it a bitmask and not a coincidence.

### The bit that was invented

Then it did not work, twice, in two ways that looked unrelated. `ERR 21`
on every read; after fixing that, `ERR 48` at boot on every write. Both
were one bit: the card modeled status bit 3 as an error flag, and it is
the exact complement — "this command has not failed". Full write-up in
[a postmortem](postmortems/2026-08-28-status-bit-invented-from-one-rom.md),
because the interesting part is not the bit but *why nothing could have
caught it*: the ABC802's ROM never reads bit 3 at all, so no test on that
target — however thorough — could have distinguished right from wrong. A
second consumer was the only thing that could.

The tell was there to be seen, though. The file's header comment derives
four status bits from specific ROM addresses; bit 3 sat in the `#define`
block with no citation and no mention in that comment. In a file otherwise
scrupulous about grounding every claim, **the one value nobody could point
at an address for was the one value that was wrong.**

What made it quick was adding `ABCBUS_TRACE=1` to the card, and then
reading the trace for what it *lacked*: correct command headers for
exactly the right sectors, and no transfers at all. The absence of an
expected line was the entire diagnosis. Kept the facility.

### Evidence it was worth doing

Three results, in ascending order of how much they justify the change:

1. **Byte-identical output.** The same `SAVE`/`LOAD` keystrokes against
   the same image produce a disk file identical to the trap's, down to the
   five physical sectors touched. The real protocol and the carefully
   derived shortcut agree exactly — which is the reassuring result, not
   the interesting one.
2. **A case the trap could not do.** `RUN LIB` + Enter, the real directory
   listing utility, reported `Diskfel` under the trap. It now prints the
   volume label, all fourteen files and the free-sector count. Nobody was
   working on that; it came free with modeling the device instead of the
   routine. Worth noting that this was a *pre-existing, unnoticed*
   failure — the trap had been signed off as working.
3. **A different DOS ROM.** `UFD80V20.bin` was examined during Milestone 6
   and left unwired, for the good reason that a trap derived against
   ABC-DOS's routines cannot serve a different DOS. It drives the same
   card correctly — 40 real bus commands, reading bitmap, directory and
   file sectors — and then correctly reports `HITTAR EJ FILEN`, because an
   ABC-DOS-formatted disk has no UFD-DOS startup file on it. That is the
   whole argument for a device model in one line, so `--dos-rom` was added
   to make it reproducible instead of a throwaway experiment.

### Carried across

The same lesson as the ABC802 sessions, pointed the other way this time:
the ABC802 target was a good source of *structure* — the whole controller
transferred unchanged — and one of its hardware facts was wrong. Reuse the
shape, re-derive the facts. The difference is that here the wrong fact was
one this project had written itself, and the second machine is what
audited it.

---

## 2026-08-29 (1) — ABC802 Milestone 9: a real SIO, and testing from inside

Documented the line editor's keys in `ABC802_REFERENCE.md` (and refreshed
its ABC-bus section, which still claimed no card was modeled), then took
the SIO — the last stub left in the machine.

The thing that made this worth doing properly, rather than leaving a
constant that "works", is that **two of the machine's configuration DIP
switches arrive through the SIO**. S1 and S2 are not memory-mapped; they
are channel B's DCD and CTS inputs. A stub returning `0x04` was therefore
not a neutral placeholder — it was asserting that both switches were off.
That is the kind of stub worth being suspicious of: one that answers a
question it was never asked, in a way nobody notices because the answer is
plausible.

### Testing from inside the machine

ABC802 BASIC has `INP()` and `OUT`, which means the chip could be verified
by the emulated machine itself:

```
PRINT INP(67)   ->  36   RR0 = Tx empty + CTS from S2
OUT 67,1
PRINT INP(67)   ->   1   the register pointer works
OUT 67,2 : OUT 67,88 : OUT 67,2
PRINT INP(67)   ->  88   the interrupt vector round-trips
```

Every one of those returned `4` before. This is a better class of evidence
than a C-level assertion: it goes through the real port decoder, the real
mirror masks, and the ROM's own `INP` implementation, so it tests the path
the machine actually uses rather than the function I happened to write. It
is also the same trick `asm/examples/*.asm` play on the CP/M side — let
the emulated machine check its own emulator — reached here through BASIC
instead of assembly.

Worth remembering for the remaining devices: if the guest has a way to
poke at hardware, the guest is the best test harness available.

### One deliberate wrong-looking answer

RR2 is valid on channel B only. Channel A returns 0 rather than the
vector, even though returning the vector would have been easy and would
have looked fine. A caller reading the wrong channel should get an
obviously wrong answer instead of a subtly right one — the same instinct
as the negative control in Milestone 7, applied to an API rather than a
test.

### Scope held

Cassette lives on SIO channel B, and it stays unattached. The real
interface is bit-level — modulated through the SIO's synchronous clocks,
demodulated by frequency detection — which is a milestone in itself, and
the machine already has working disk storage, so it would be fidelity
rather than capability. Recorded as its own roadmap item rather than
half-done here.

---

## 2026-08-28 (last) — ABC802 Milestone 8: the line editor, and a gap that wasn't

Closed the arrow-key gap Milestone 2 left open — by discovering there was
nothing to close.

Milestone 2 had recorded it as "probing found no non-destructive
cursor-right; closing this properly means disassembling the ROM's own line
editor the way the ABC80 target's was." That framing quietly assumed the
mapping existed and simply had not been found. It doesn't.

I swept the editor with every byte `0x00`-`0x1F` plus a sample across
`0x80`-`0xFF`, typing `ABCDE<code>X` each time and reading back what
happened to the line. Its entire vocabulary is six codes: backspace
(`0x08`), discard-line (`0x18`), clear-screen (`0x0C`), and three line
terminators (`0x03`/`0x0A`/`0x0D`). **No cursor movement of any kind**, in
either byte range. This is a genuinely simpler editor than the ABC80's,
which does have a non-destructive cursor-right at `0x09`.

So Left maps to `0x08` — the only leftward motion that exists, and the key
that literally *is* backspace on the ABC80's own keyboard — and Right is
dropped, because the machine has nothing for it to do. The known-gap entry
becomes a documented hardware fact instead, phrased so nobody re-opens it
as a missing feature.

### On the method

The planned approach was a disassembly, and it would have been the harder
road for a worse answer. The editor sits in a region `bin/z80dasm` cannot
reach — it is only entered indirectly, so a reachability-based
disassembler renders it as `DB` bytes, which is exactly what happened when
I looked. I could have carved the region out and disassembled it linearly,
but then I would be reasoning about code paths I might have misread.

A 47-run behavioral sweep answered the question completely, and answered
it about *behavior* rather than about my reading of assembly. It also
turned up something the disassembly plan wasn't even aiming at: Ctrl-X
discards the whole line, has always worked, and had never been documented.

Worth remembering as a general preference. Disassembly tells you what the
code appears to do; a sweep tells you what the machine does. When the
question is "what does this accept?", and the input space is 256 values
wide, just try all of them.

---

## 2026-08-28 (end of day) — ABC802 Milestone 7: a second drive

Short one. `--disk` is now repeatable, so images take drives 0, 1, … in
order, with a `N:` prefix to pin one. The controller had tracked eight
units since Milestone 5 and the ROM has always addressed them as
`MO1:`/`MF1:`; the only missing piece was a way to attach one. No new flag
— repeating the existing one is what two-drive software expects and leaves
every single-`--disk` invocation working untouched.

The part worth recording is the **negative control**. The obvious test is
to save to drive 1 and load it back, and that passes — but it passes just
as happily if the unit number is being ignored entirely and everything
lands on drive 0. A round trip cannot tell those apart. What distinguishes
them is the read that must *fail*: `LOAD "MF0:D1TEST"` returning
`Error 21`, plus the two images' checksums showing only drive 1 changed.

That is the same lesson as [the boot-screen
postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md) wearing
different clothes. There the question was "would this output change if the
code were broken?" Here it is "would this test still pass if the feature
did nothing?" — and for a plain round trip on a two-drive system, the
answer is yes. Both are the same discipline: a passing check is only worth
what its counterfactual is.

Three sessions running now, that habit has caught something. It seems to
be the highest-value thing this project's write-ups have produced.

---

## 2026-08-28 (later still) — ABC802 Milestone 6: the 640K drive

Small follow-on to Milestone 5, and it produced one fact worth more than
the feature.

`--disk` now takes 640KB ABC832/834 images as well as 160KB ABC830 ones —
the ABC802's own native drive rather than the ABC80-era one it inherited.
Which controller is fitted comes from the image's size rather than a flag:
the formats differ by a factor of four, a real dump is always exactly one
of the two, and making the user restate what the file already says is a
good way to collect bug reports about the wrong geometry.

### The two drives interleave differently, in opposite directions

| Drive | Identity | Interleave 7 |
|---|---|---|
| ABC830 (160K) | does not boot | **boots** |
| ABC832/834 (640K) | **boots** | does not boot |

Both settled by booting real media both ways. Had either been assumed from
the other, that drive would have been silently broken, and the symptom is
"the disk does nothing" with no error to trace.

I nearly shipped this one badly. I wrote the code comment asserting the
identity result *before running the test* — reasoning from abc80sim's
table, which gives `MF` no interleave parameters. The reasoning happened
to be right, which is exactly what makes it a bad habit: a comment that
says "this was tested" when it has not been is worse than no comment,
because the next person has no way to tell the two apart. Caught it on
reread, tested both directions, and only then let the claim stand.

The trap underneath is the same one ABC80's Milestone 6 recorded: both
images have their directory at sector 16, and **sector 16 reads correctly
under either mapping**, because track-boundary sectors map to themselves.
So being able to see filenames in a hex dump proves nothing about
interleave. Only booting does. Worth knowing before anyone adds the `SF`
or `HD` types.

### A postmortem paying off twice

The `ADMINISTRATION 800` screen this milestone boots is the first time
*real* software has exercised Milestone 3's row-attribute decode — its
title bar is inverse-video and its rules are Row Graphic mosaics. That
path is precisely the one [the boot-screen
postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md) pointed
out the ROM's own boot screen could never test, and which had until now
only ever been driven by a synthetic screen built for the purpose. It
renders correctly, on 1983 output that knew nothing about this emulator.

Nothing was done to make that happen; it fell out of a milestone about
disk geometry. But it is the check the synthetic test was standing in for,
and it is worth recording that the stand-in turned out to be honest.

---

## 2026-08-28 (later) — ABC802 Milestone 5: the ABC-bus floppy, scoped then built

Scoped the work first, on the suspicion it would be intricate. It was —
but not where expected, and the scoping is what made the build quick. The
prediction-versus-outcome comparison lives at the top of
`abc802/docs/ABC802_FLOPPY_SCOPING.md`; the short version is that the
document got the *shape* entirely right and the *estimate* wrong in the
cautious direction.

### What the scoping found

Three things, none visible without looking:

- **The controller is a complete second computer** — its own Z80 at
  4&nbsp;MHz, a Z80 DMA controller, an FD1793 FDC, five firmware variants,
  and a PAL that was never dumped.
- **Porting MAME is not a shortcut.** MAME's card-side ABC-bus handlers
  are a byte latch, a busy flag and an NMI pulse. It does not *know* the
  disk protocol; it runs the firmware that implements it. So the choice is
  binary: run real firmware, or implement the protocol yourself.
- **The ABC80 target's bypass does not transfer.** That one traps two PC
  addresses because that ROM has a single sector-level routine. This DOS
  ROM's bus driver is generic and its callers issue *sequences* of bus
  commands, so there is no equivalent place to cut.

But abc80sim documents the protocol, and three details of it matched this
ROM exactly — the two command constants, the select mask, the select
values. That is what turned "an implementation exists for a related
machine" into "this is the same protocol", and it is why the build needed
no reverse-engineering at all.

### What the build found

- **`0x00` and `0xFF` both mean "no device."** The scoping flagged the
  status byte as the likeliest source of trouble and guessed the problem
  would be bit polarity. The actual constraint was stricter and was missed
  entirely: the ROM's poll loop does `INC A / JR Z` *and* `DEC A / JR Z`,
  so either value aborts it. Which is exactly why the old "every ABC-bus
  read returns `0xFF`" behavior read as *no card fitted* — correct, and by
  accident. An idle controller is `0x81`.
- **The interleave contradiction, settled by experiment.** The scoping
  recorded that this project verified interleave factor 7 empirically
  while abc80sim ships with interleave compiled out, and that both could
  not be right. Rebuilding with it disabled and changing nothing else,
  real media stops booting entirely. Resolved in favour of this
  repository's own earlier finding, now confirmed on a second machine and
  a different DOS ROM.
- **`LIB` does not exist, and the error was right.** Three guesses at
  directory-listing syntax all returned `Error 220`, which looked like a
  controller bug. It was not: the DOS ROM's own command table holds
  exactly four entries — `BYE`, `KILL`, `NAME`, `AS`. Reading the ROM
  settled in minutes what guessing had not in three attempts. The general
  lesson is one this project keeps relearning: when the machine disagrees,
  read its ROM rather than try another guess.
- **A gap no scoping would have predicted.** `--type` was unusable for
  disk work, because the ROM reports the keyboard ready long before a
  booting program is listening; `LIB` arrived as `B`. `--type-at` exists
  now because of it, and without it no scripted disk test is reproducible.

### On verifying it

The temptation with disk support is to see software appear on screen and
call it done. `ORD 800 Version 2.4` booting off a real image is a
wonderful thing to see, and proves nothing on its own — so the check was
that the string `ORD 800` appears at sector 57 of the image and in **none
of the six committed ROMs**, and that with no `--disk` the machine still
boots to a bare BASIC prompt.

That habit came directly from [the boot-screen
postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md) written
earlier the same day, which is the first time one of these write-ups has
visibly changed how the next piece of work was checked.

The first round-trip test was also thrown away and redone: it typed a
program into a session where the disk's own autoboot program was still
resident, so `SAVE` stored a merge of both and `LIST` came back showing
someone else's Luxor game menu with one line of mine in it. The round trip
was genuinely correct; the output simply did not demonstrate it. Redone
with `NEW` first, it does.

---

## 2026-08-28 — ABC802 Milestones 2-4: interactive, pixels, GTK

Picked the project back up after a gap. Starting state: `make test` green
(16/16), every ABC80 and CP/M roadmap item closed, and ABC802 one
milestone deep — it booted the real BASIC II ROM to a prompt and could be
typed at only through a fixed `--type` string. Three candidate next steps
were listed and none committed.

Took them in the order the roadmap suggested, which turned out to be the
right order for a reason worth recording: each one made the next one
verifiable.

### Milestone 2 — live interactive keyboard and screen

`bin/abc802 --interactive`: raw-terminal input, execution paced to the
real 3 MHz clock, a screen redrawn at 30fps, and UTF-8 input for the
Swedish letters the display could already show.

Four things were established by **probing the real ROM rather than porting
the ABC80 target's assumptions**, and all four would have been wrong if
assumed:

- **The cursor blink belongs to the ROM, not the emulator.** Tracing the
  ROM's CRTC writes showed it toggling MC6845 R10 between `0x09`
  (non-blink) and `0x29` (non-display) continuously, driven by its own
  93.75 Hz clock interrupt — it blinks in *software*. So this target needs
  no blink-rate constant at all, unlike `bin/abc80`, which must supply
  `ABC80_BLINK_HZ` because that machine blinks in hardware. Honoring R10
  and pacing execution correctly is the entire implementation; the
  measured ~2.7 Hz result is the firmware's decision, not this code's.
  This is the happiest kind of finding: the feature was already there, and
  the work was to stop getting in its way.
- **The host Backspace key sends DEL, and this ROM does not want DEL.**
  `0x08` is a real destructive delete (`PRINT 12` + two `0x08` + `3`
  evaluates to `3`); `0x7F` is treated as an ordinary printable character
  and echoes a blank into the line. Untranslated, Backspace would have
  silently corrupted what the user typed instead of erasing it.
- **This machine's line editor is not the ABC80's.** That target maps host
  arrow keys onto `0x08`/`0x09`, grounded in a disassembly of *its* ROM.
  Probing this one found no non-destructive cursor-right at all: `0x09`
  and `0x1F` are ignored, `0x0C` clears the screen. Rather than invent a
  mapping, arrow keys are dropped and the gap is recorded. Left as a known
  gap on purpose — the honest fix is to disassemble this ROM's editor,
  which is a piece of work in its own right.
- **Live input needs the same pacing `--type` already used.** See
  [the DART postmortem](postmortems/2026-08-28-dart-single-byte-overwrite.md).

### Milestone 3 — pixel rendering from the character ROM

`--screenshot FILE` writes a real 480x240 PNG in the machine's own amber
phosphor. Grounded in MAME's `abc802_update_row()` — which carries the
video board's actual PAL16R4 equations — then cross-checked byte-for-byte
against the committed ROM, which is what turned a port into knowledge.

The scheme is genuinely strange and worth knowing before touching anything
video-related: **the character generator ROM's own output byte decides
whether a cell is a character or an attribute command.** Bit 7 set means
it is not pixel data but an instruction. So the *font* defines which
character codes act as attribute codes — here exactly 17 of them. And all
three attributes work by substituting the scanline address rather than
post-processing pixels, which is why the real cursor is a solid bar
*replacing* the glyph rather than an inversion of it.

Two decisions worth keeping:

- `abc802_render_pixels()` is a **pure function** over an `Abc802Screen`
  struct. That was not architectural taste for its own sake — it is what
  let the decode be verified with no CPU core at all, and it is why the
  GTK app a milestone later could not drift from `--screenshot`.
- `png.c` is hand-written, using DEFLATE *stored* blocks so no compressor
  is needed. Same reasoning as `abc80`'s own WAV writer: the default build
  of this project has no third-party libraries, and the uncompressed path
  of both formats is small and completely specified.

The important lesson here was about testing, not rendering, and it has its
own write-up: [the boot screen cannot validate the
feature](postmortems/2026-08-28-boot-screen-cannot-validate.md).

### Milestone 4 — a GTK window

`bin/abc802-gtk`, a Cairo pixel framebuffer running the core in-process.
Much shorter than ABC80's equivalent, because Milestone 3's decode was
already shared and verified — the app only turns its output into a Cairo
surface. `abc802_step()` was extracted into `emu/src/step.h` at the same
time so the CLI's `--interactive` loop and the window share the
per-instruction logic, the same move ABC80's Milestone 11 made.

The interesting problem was **verification, not implementation**.
`abc80/gtk/README.md` records that automating `screencapture` against the
user's real desktop is disruptive — it steals focus and switches Spaces
while they are working — so that avenue was closed on purpose. The answer
was to build the app so it can verify itself: `--screenshot` opens no
window and never creates a `GtkApplication`, but renders one frame through
the *identical* `draw_screen()` the live window uses, against an offscreen
Cairo surface. Real evidence about the real renderer, with no desktop
involved.

That flag justified itself within minutes of existing by catching a real
bug — and a pre-existing one at that: [`--type` fed raw UTF-8
bytes](postmortems/2026-08-28-type-raw-utf8-bytes.md).

### What carried across all three

The pattern that kept repeating: **the ABC80 target is a good source of
architecture and a bad source of facts.** Its shapes transferred perfectly
— the raw-terminal setup, the pacing loop, the shared-step extraction, the
GTK app's structure. Every one of its *hardware* assumptions that got
tested against the ABC802 ROM turned out to be wrong: the blink, the
arrow keys, where the cursor comes from. Reuse the structure, re-derive
the facts.

Also: three of the four bugs found this session were found by *looking at
output*, not by a test failing. The screenshots and ASCII-art dumps earned
their keep.

### Housekeeping

- Deleted two untracked scratch files left over from an earlier session
  (`abc80/examples/hello.bac`, `test.bas`).
- Split completed work out of all three roadmaps into per-target
  `*_COMPLETED.md` files; the roadmaps went from 4,605 lines to 439 and
  now answer "what works, what doesn't, what's next" without scrolling
  through an archive. Two stale headings surfaced in the process and were
  corrected: **ABC80's Milestone 6 never said "done"** and **CP/M's Phase
  2 still said "in progress"**, though every item under both is checked
  off and has been for some time.
- Established this journal and `docs/postmortems/`.
