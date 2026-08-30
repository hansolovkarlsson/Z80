# ABC806 BASIC reference

The language `bin/abc806` actually runs — **BASIC II**, the ABC800 family's
dialect, plus the ABC806's own colour and high-resolution graphics.

## Start here: this is a supplement, not a whole reference

The ABC806 and ABC802 run the *same* BASIC II. Extracting both machines'
keyword tables from their BASIC PROMs and diffing them gives an almost
identical set — the ABC806 adds attribute keywords and drops a couple the
ABC802 needed, and nothing in the core language differs.

So **[`../../abc802/docs/ABC802_BASIC_REFERENCE.md`](../../abc802/docs/ABC802_BASIC_REFERENCE.md)
is the reference for the language itself** — statements, functions,
operators, file handling, the disk devices and error codes. Everything
there applies here. Duplicating 1,200 lines to change a handful of entries
would guarantee the two drift apart.

This file covers only what is genuinely ABC806: **colour, text attributes,
and the high-resolution graphics commands.**

Everything below was verified by running it, and each example's stated
result is what the emulator actually produced. Where something is inferred
rather than observed it says so.

---

## Colour and text attributes

The ABC806 is the colour machine of the family. Attributes are keywords
used inside `PRINT`, and they take effect from that point on the line.

| Keyword | Effect |
|---|---|
| `GBLK` | black |
| `GRED` | red |
| `GGRN` | green |
| `GYEL` | yellow |
| `GBLU` | blue |
| `GMAG` | magenta |
| `GCYA` | cyan |
| `GWHT` | white |
| `ULN` | underline on |
| `NULN` | underline off |
| `HIDE` | conceal |
| `BLBG` / `NWBG` | black background / new background |
| `GCON` / `GSEP` | contiguous / separated graphics |
| `GHOL` / `GREL` | hold / release graphics |
| `FLSH` / `STDY` | flashing / steady |

```basic
PRINT GRED;"RED";GGRN;"GREEN";GYEL;"YELLOW"
PRINT ULN;"UNDERLINE";NULN;" PLAIN"
```

Verified: the three words render in real red, green and yellow, and the
underline is a real underline rather than a substituted glyph. (Underline
is drawn by the `RAD` PROM substituting a scanline address — see
[`ABC806_REFERENCE.md`](ABC806_REFERENCE.md).)

The colour names are `G`-prefixed for *graphic*, which is a teletext
inheritance; `GCON`/`GSEP`/`GHOL`/`GREL` are teletext concepts too. Only
the colours and `ULN`/`NULN` have been exercised here.

---

## High-resolution graphics

### The model, in five facts

1. **The plane is 240×240**, both axes numbered `0`–`239`, with the
   **origin at bottom left** — y increases upward, unlike the text screen.
2. **There are four pens.** A pen argument is masked to two bits, so pen 4
   is pen 0 again and pen 7 is pen 3.
3. **`FGCTL` must run first.** It programs the colour lookup. Until it
   does, every pen is transparent and a perfectly drawn picture is
   invisible. This is the single most likely reason for "nothing happens".
4. **A pen argument is what makes a command draw.** `FGPOINT x,y` moves the
   graphics cursor and plots nothing; `FGPOINT x,y,pen` plots a dot.
5. **Text wins where it is lit.** The graphics layer shows through wherever
   the text screen is black, so anything drawn behind text is hidden. With
   25 rows of text at the top, drawing above about y=140 may be covered.

### The commands

| Command | Effect |
|---|---|
| `FGCTL n` | select the palette / enable the layer. Any non-zero `n` but 128 programs it |
| `FGPOINT x,y` | move the graphics cursor; draws nothing |
| `FGPOINT x,y,pen` | plot a dot |
| `FGLINE x,y,pen` | draw from the cursor to `x,y`, and leave the cursor there |
| `FGFILL x,y,pen` | fill the rectangle between the cursor and `x,y` |
| `FGPAINT x,y,pen` | flood fill outward from `x,y`, bounded by drawn pixels |
| `FGPICTURE` | present in the ROM's keyword table; **not exercised here** |

`FGLINE` leaves the cursor at its endpoint, so lines chain:
`FGPOINT a,b,p : FGLINE c,d,p : FGLINE e,f,p` draws a connected path.

### Palettes

`FGCTL`'s argument selects which colours the four pens get. Two that were
measured:

- **`FGCTL 1`** — every pen white. Useful, and a common cause of confusion:
  drawing in pens 1, 2 and 3 all comes out white, which looks like colour
  being broken.
- **`FGCTL 2`** — pen 1 red, pen 2 green, pen 3 yellow. This is the one the
  examples below use.

Other arguments program the lookup differently and have not been mapped.

---

## Worked examples

All four were run; the described result is what appeared.

### A dot and a line

```basic
FGCTL 2
FGPOINT 100,100,3
FGPOINT 20,20,1:FGLINE 200,20,1
```

A yellow dot, and a red horizontal line near the bottom of the screen.

### A fan, as a real program

```basic
10 FGCTL 2
20 FOR I=0 TO 200 STEP 20
30 FGPOINT 120,10,1
40 FGLINE I,220,3
50 NEXT I
RUN
```

Eleven yellow rays from a common point near the bottom centre, spreading
upward. Note line 30: `FGLINE` moves the cursor to its endpoint, so the
origin has to be re-set each time round the loop.

### A filled rectangle

```basic
FGCTL 2
FGPOINT 50,50
FGFILL 150,150,2
```

A green rectangle. `FGPOINT` sets one corner — no pen argument needed,
since here it is only moving the cursor — and `FGFILL` sets the other.

### An outlined box, flood filled

```basic
FGCTL 2
FGPOINT 40,40,3:FGLINE 160,40,3:FGLINE 160,140,3:FGLINE 40,140,3:FGLINE 40,40,3
FGPAINT 100,90,1
```

A yellow box filled with red. This is the clearest demonstration that
`FGPAINT` is a genuine flood fill: with **no** box drawn first, the same
command fills all 30,720 bytes of the plane.

---

## Things that will waste your time

- **Forgetting `FGCTL`.** The commands run, the pixels are written, and
  nothing appears. Every pen is transparent until the palette is
  programmed.
- **Expecting `FGPOINT x,y` to plot.** Two arguments move the cursor;
  three plot.
- **Pens above 3.** `FGLINE x,y,7` draws in pen 3, not a fifth colour.
- **Drawing behind text.** The layer only shows through black text cells.
  `FGPAINT` in particular will appear to do nothing if you flood a region
  the text screen covers.
- **Four coordinates to `FGFILL`.** `FGFILL x1,y1,x2,y2,pen` is accepted by
  the parser and writes nothing. The corner comes from the cursor.

---

## How this was established, and what it is not

The keyword tables were read out of the committed ROM images — the BASIC
PROMs for the language, and `ABC806-option.76-11` for the `FG` commands —
rather than transcribed from a manual, the same method
[`ABC802_BASIC_REFERENCE.md`](../../abc802/docs/ABC802_BASIC_REFERENCE.md)
used. Behaviour was then established by running each command and looking at
what the machine drew, including reading the rendered PNG's pixels rather
than judging colour by eye, after doing exactly that and getting it wrong
(see [`../../docs/JOURNAL.md`](../../docs/JOURNAL.md)).

**Not covered**: `FGPICTURE`, most `FGCTL` arguments, and the teletext-style
attribute keywords beyond the colours and underline. They are listed above
because they are in the ROM's own table; that is evidence they exist, not
evidence of what they do.

For the hardware underneath — the plane's memory layout, the palette's
two-pixels-per-entry encoding, and why the text layer composites the way it
does — see [`ABC806_REFERENCE.md`](ABC806_REFERENCE.md).
