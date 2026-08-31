# ABC80 floppy images

Real ABC-bus disk images for `bin/abc80 --disk`. **The images themselves
are not committed**; this README is.

## Why they are not in the repo

The reasoning was first written down for this target
([`ABC80_COMPLETED.md`](../../docs/ABC80_COMPLETED.md), Milestone 6) and
the ABC802's own disk directory now points back at it: unlike the ROM
images — which got an explicit provenance and CRC32/SHA1 table before
being committed — and unlike ZEXALL/ZEXDOC, which are explicitly GPLv2,
these dumps carry no license statement anywhere. They are
third-party-authored software on third-party-dumped media. So the
directory and this file are tracked; `*.img` and `*.dsk` in it are not
(see the repo's `.gitignore`).

Drop images here and git ignores them automatically.

## What the regression suite wants

`abc80/tests/run_tests.sh` looks in `$ABC80_TEST_DISKS`, and falls back to
this directory when that variable is unset. Eight of its checks need media
and skip loudly without it.

| File | What it is |
|---|---|
| `disk003.img` | 160K, "System.diskett ABC80 Ver. 2.1". Holds the real `LIB` utility; its directory listing ends `453 av 616 sektorer kvar`, which the suite asserts. Used by five checks. |
| `disk001.img` | 160K, volume `SYSTEMSKIVA VER. 1.0`. Needed by the three two-drive checks, and needed *because its volume label differs* — that difference is what proves two drives rather than one image mounted twice. |

**`disk002.img` is byte-identical to `disk001.img`**, so the archive
yields only two distinct ABC80 disks, which is exactly the two above.

Both come from
<https://www.abc80.net/archive/luxor/sw/disk_images/ABC80/160k/>, whose
own `index.txt` carries scanned labels and descriptions.

For reference, the copies these checks were validated against:

```
dcf847ce4ab3184ff79644e64cbc3d10  disk001.img
459b5db6150ca1ceb7da70ca76736664  disk003.img
```

## Attaching one

```
bin/abc80 abc80/resources/rom 60000000 --disk abc80/resources/disks/disk003.img
```

Repeat `--disk` for a second drive — it becomes the ROM's own `DR1:` — or
pin one with `1:FILE`. This machine's DOS names drives `DR0:` through
`DR6:` (a real device table at `0x6EB5` in `ABCDOS80.bin`), and unlike the
ABC800 family's DOS it does *not* scan them at boot: a drive is touched
only when something asks for it.

**Copy an image before anything that writes to it.** The DOS writes to
media, and the suite's own checks copy first for exactly this reason.

Interleave: these `.img` dumps store ABC830 sectors in physical order and
need the default factor of 7. `.dsk` dumps of the same media are usually
in logical order and need `--interleave 0`. Nothing inside a file says
which, and a directory listing reads correctly under either — only reading
a real file settles it (see
[`ABC80_COMPLETED.md`](../../docs/ABC80_COMPLETED.md), Milestone 6).
