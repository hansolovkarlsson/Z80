# Colossal Cave Adventure (CP/M)

`Adventur.com` + `Phrogz.din` (the game's text/data file - both must sit
in the same directory to run) are a prebuilt 350-point CP/M port of
Willie Crowther and Don Woods' *Colossal Cave Adventure*, downloaded from
the [Interactive Fiction Archive](https://www.ifarchive.org/if-archive/games/cpm/Advent_CPM.zip)
(`Advent_CPM.zip`). No source is available for this particular port
(unlike SARGON in `resources/sargon/`) - only the compiled binary and its
data file, so real-world validation here is "run the actual program and
see what breaks" rather than "assemble it with our own toolchain and see
what breaks".

**License note**: like `resources/sargon/`, this is old commercial/
freeware software (this specific port descends from a Software Toolworks
commercial release) mirrored by a long-running enthusiast archive with no
explicit open-source license attached. Kept here anyway per the same
private-repo policy as SARGON - see the project's own memory notes on
this, or ask before assuming it extends to a public repo.

## Running it

Run from a directory whose `cpm_disk/` contains both files (see
`CLAUDE.md`'s File I/O section for how drive/user numbers map onto that
one host directory):

```
mkdir -p cpm_disk
cp resources/adventure/Adventur.com resources/adventure/Phrogz.din cpm_disk/
bin/z80 cpm_disk/Adventur.com
```

Answer `NO` to "WOULD YOU LIKE INSTRUCTIONS?" to skip straight to the
opening scene, or `YES` for the full instructions text.
