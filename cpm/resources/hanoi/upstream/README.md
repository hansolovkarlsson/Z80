# Towers of Hanoi (CP/M, Turbo Pascal 3.01A) - upstream source

`hanoi-p.pas` is the real, unmodified "portable" variant (uses only
KayPro/ADM-3A escape sequences, no machine-specific graphics) from
Francesco Sblendorio's [hanoi-cpm](https://github.com/sblendorio/hanoi-cpm)
(GPLv2, see `LICENSE`) - a genuine Turbo Pascal 3.01A CP/M-80 program, not
written for or modified by this project. The repository also contains a
Commodore 128-specific variant (`hanoi128.pas` + `graph.inc`) not used
here.

Fetched from `https://raw.githubusercontent.com/sblendorio/hanoi-cpm/master/source/hanoi-p.pas`.

The file's own `{I GRAPH.INC}` line at the top is a harmless leftover
comment (missing the `$` an actual `{$I ...}` include directive needs) -
this portable variant doesn't reference anything `graph.inc` defines, so
it isn't fetched here.
