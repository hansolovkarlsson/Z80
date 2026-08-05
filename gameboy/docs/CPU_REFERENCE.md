# Game Boy CPU (Sharp SM83) Reference

A reference for the Sharp SM83 (commonly called "LR35902" or "GBZ80")
instruction set as implemented by this project's Game Boy emulator core
(`gameboy/src/cpu.c`/`alu.c`), grounded against [Pan
Docs](https://gbdev.io/pandocs/) and the official [gbdev.io opcode
table](https://gbdev.io/gb-opcodes/optables/) (data at
`https://gbdev.io/gb-opcodes/Opcodes.json`) — the same primary sources
`gameboy/docs/GAMEBOY_ROADMAP.md`'s Phase 1 status cites. Where this
project's own implementation status matters, it's called out explicitly
— see [Implementation status](#implementation-status) at the end.
Everything else describes real SM83 behavior, independent of this
codebase.

This is a companion to `cpm/docs/Z80_REFERENCE.md`, not a duplicate of
it: the SM83 is Z80-*derived*, sharing the bulk of its instruction
encoding and dispatch shape, but is a genuinely different, simpler CPU
— no alternate register set, no index registers, no `ED`-prefixed block
instructions, and no `IN`/`OUT` (all I/O is memory-mapped). See
[Differences from the Z80](#differences-from-the-z80) for the full,
confirmed list.

## Registers

**Main set only** — `A` (accumulator) + `F` (flags), paired as `AF`;
`B`,`C` paired as `BC`; `D`,`E` paired as `DE`; `H`,`L` paired as `HL`.
No alternate register set exists (no `EXX`, no `EX AF,AF'` — both are
Z80-only), and no index registers (no `IX`/`IY`, and therefore none of
the `(IX+d)`/`(IY+d)` addressing or the Z80's undocumented `IXH`/`IXL`/
`IYH`/`IYL` half-index forms).

**Special purpose**: `SP` (stack pointer), `PC` (program counter). No
`I` (interrupt vector base) or `R` (memory refresh counter) — both are
Z80-only registers with no SM83 equivalent.

## Flags (the F register)

| Bit | Name | Meaning                                   |
|-----|------|--------------------------------------------|
| 7   | Z    | Zero (result == 0)                          |
| 6   | N    | Add/Subtract (set for subtract-family ops)  |
| 5   | H    | Half-carry (carry/borrow between bits 3/4)  |
| 4   | C    | Carry                                       |
| 3-0 | —    | Unused — always read as 0                   |

Only the top 4 bits are implemented; the bottom 4 are always 0, both on
a direct read and when `POP AF` restores `F` from the stack (real
hardware masks the low nibble to 0 regardless of what was actually
pushed — `gb_alu.h`'s `set_rr2()` in `cpu.c` enforces this). Unlike the
Z80, there are **no undocumented `X`/`Y` flag bits** here (bits 3 and
5 on a Z80's `F` register) — a genuine hardware difference, not an
oversight in this emulator.

Two flag-behavior distinctions worth calling out precisely, both
implemented and grounded against pandocs:

- The accumulator rotate forms (`RLCA`/`RRCA`/`RLA`/`RRA`) **always
  clear Z** regardless of the result — unlike the `CB`-prefixed forms
  (`RLC`/`RRC`/`RL`/`RR` r/`(HL)`), which set Z from the result. A real
  hardware distinction inherited from the Z80, not an inconsistency.
- `DAA`'s behavior (BCD-correct `A` after an 8-bit add/subtract) needs
  its own grounded reference rather than assumed-identical-to-Z80
  semantics — verified against pandocs' own `DAA` table during Phase 1
  and by Blargg's `cpu_instrs` `01-special.gb` sub-test.

## Addressing modes

| Mode              | Example        | Notes |
|--------------------|----------------|-------|
| Implied            | `NOP`          | no operand |
| Register           | `LD A,B`       | |
| Register indirect  | `LD A,(HL)`    | also `(BC)`, `(DE)` — no `(SP)`/`(C)` forms (Z80-only) |
| Auto-inc/dec indirect | `LD A,(HL+)` | also `(HL-)`, and the reverse `LD (HL+),A`/`LD (HL-),A` — **SM83-only**, no Z80 equivalent |
| High-page indirect | `LDH A,(C)`    | fixed `0xFF00+C`; also `LDH (C),A` — **SM83-only** |
| High-page immediate | `LDH A,(a8)`  | fixed `0xFF00+a8`; also `LDH (a8),A` — **SM83-only** |
| Immediate          | `LD A,42`      | 8-bit; 16-bit form is `LD HL,1234h` |
| Signed immediate   | `ADD SP,e8`    | 8-bit signed, also `LD HL,SP+e8` — **SM83-only** |
| Extended (direct)  | `LD A,(a16)`   | absolute memory address |
| Relative           | `JR label`     | 8-bit signed displacement from the byte *after* the instruction |

No indexed mode exists at all (no `IX`/`IY` to index with) — the SM83
compensates for the loss of `(IX+d)`-style structure access with the
auto-increment/decrement and high-page forms above, which real Game Boy
code uses constantly (`LD (HL+),A` is the idiomatic way to fill a
buffer; `LDH` is how every I/O register at `0xFF00`-`0xFFFF` gets
touched in 2 bytes instead of `LD (a16),A`'s 3).

## Instruction set by category

Each category below maps to where it lives in this codebase: the
emulator's *decoder* (`gameboy/src/cpu.c`, dispatch built in
`gb_cpu_init_tables()`) is the authoritative source for exact opcode
bytes — this document rounds to the pattern level, not a literal hex
table (see the official gbdev.io opcode table linked above for that).

### 8-bit load

- `LD r,r'` — any of `B C D E H L A` to any other (`0x40`-`0x7F`; `0x76`
  is `HALT`, not `LD (HL),(HL)`, which isn't representable in this
  encoding — the same real hardware special case the Z80 shares)
- `LD r,n`; `LD r,(HL)`; `LD (HL),r`; `LD (HL),n`
- `LD A,(BC)` / `LD A,(DE)` / `LD A,(a16)`; and the reverse (`LD
  (BC),A` etc.)
- `LD A,(HL+)` / `LD A,(HL-)` / `LD (HL+),A` / `LD (HL-),A` — the
  auto-increment/decrement forms real code uses to walk a buffer
  without a separate `INC`/`DEC HL`
- `LDH A,(a8)` / `LDH (a8),A` — fixed `0xFF00`+8-bit offset (reaches
  every I/O register in 2 bytes instead of 3)
- `LDH A,(C)` / `LDH (C),A` — fixed `0xFF00+C`

### 16-bit load

- `LD rr,d16` (`rr` = `BC DE HL SP`)
- `LD (a16),SP` — stores `SP` (not `HL`) to an absolute address, the
  SM83's equivalent of the Z80's `LD (nn),HL`/`LD (nn),dd`
- `LD SP,HL`
- `LD HL,SP+e8` — `HL` = `SP` + signed 8-bit immediate, with flags set
  from the addition (shares its flag logic with `ADD SP,e8` below —
  see `gb_alu_add_sp_e8()` in `alu.c`)
- `PUSH rr` / `POP rr` (`rr` = `BC DE HL AF` — `AF` replaces `SP` in
  this encoding slot, a real, deliberate difference from the 16-bit
  load group above, not an inconsistency)

### 8-bit arithmetic / logic

`ADD` `ADC` `SUB` `SBC` `AND` `XOR` `OR` `CP`, each against: a
register, an 8-bit immediate, or `(HL)`. No `(IX+d)`/`(IY+d)` forms and
no undocumented `IXH`/`IXL`/`IYH`/`IYL` operand forms — both entirely
Z80-only, since the SM83 has no index registers at all.

### General-purpose accumulator/flag ops

`DAA` `CPL` `CCF` `SCF` `NOP` `HALT` `STOP` `DI` `EI` — no `NEG`
(Z80/`ED`-only; the SM83 has no `ED`-prefixed instruction space at all,
see [Differences from the Z80](#differences-from-the-z80)).

### 16-bit arithmetic

- `INC rr` / `DEC rr` (`rr` = `BC DE HL SP`)
- `ADD HL,rr` (`rr` = `BC DE HL SP`)
- `ADD SP,e8` — `SP` += signed 8-bit immediate (shares flag logic with
  `LD HL,SP+e8` above)

No `ADC HL,rr`/`SBC HL,rr` — both are Z80-only (`ED`-prefixed on real
Z80 hardware).

### Rotate / shift

- `RLCA` `RRCA` `RLA` `RRA` — accumulator-only, 1 byte, always clear Z
- `RLC` `RRC` `RL` `RR` `SLA` `SRA` `SWAP` `SRL` r/`(HL)` (`CB`-prefixed)
  — set Z from the result. `SWAP` (exchange the high/low nibbles of a
  byte) is the SM83's own addition to this group — no Z80 equivalent
  (the Z80 has the undocumented `SLL`/`SL1` in this opcode slot
  instead; the SM83 uses it for a real, documented instruction).

No `RLD`/`RRD` (Z80/`ED`-only BCD-digit rotate).

### Bit test / set / reset

`BIT` `SET` `RES` *b*,r/`(HL)` (`CB`-prefixed), *b* = 0-7 — identical in
shape to the Z80's version, minus the `(IX+d)`/`(IY+d)` forms.

### Jump

- `JP a16`; `JP cc,a16` (*cc* = `NZ Z NC C` — no parity/sign-flag forms
  at all, since the SM83 has no `P/V` flag)
- `JP HL` — jumps to the address *in* `HL` (not indirect through it,
  despite the parenthesized `(HL)` some references write for it — real
  hardware, and this emulator's own `gb_op_jp_hl()`, confirm this
  against the official opcode table's per-operand `immediate` flag,
  same reasoning `cpu.c`'s own comment gives)
- `JR e`; `JR cc,e` (*cc* restricted to the same `NZ Z NC C` set)

### Call / return

- `CALL a16`; `CALL cc,a16`
- `RET`; `RET cc`; `RETI` — sets `IME` immediately on return, unlike
  `EI`'s one-instruction delay (see [Interrupt
  handling](#interrupt-handling)); no `RETN` (Z80/`ED`-only, and
  meaningless here anyway since the SM83 has no NMI line)
- `RST p` (*p* = `00h 08h 10h 18h 20h 28h 30h 38h`)

### CPU control

- `NOP` `HALT` `DI` `EI` — see [The HALT bug](#the-halt-bug) and
  [Interrupt handling](#interrupt-handling)
- `STOP` — a real 2-byte instruction (opcode `0x10` + a padding byte,
  conventionally `0x00`) per the official opcode table, not the 1-byte
  form some older references list. Resets the timer's system counter
  exactly like a `DIV` write does (pandocs'
  `Timer_and_Divider_Registers.md`) — see `gameboy/docs/HARDWARE_REFERENCE.md`'s
  Timer section. Real hardware's full low-power STOP mode, and exiting
  it via a joypad press, needs an actual input source to ever trigger;
  see [Implementation status](#implementation-status).

No `IM 0`/`1`/`2` (Z80/`ED`-only — the SM83 has exactly one interrupt
mode, always active) and no `IN`/`OUT` at all — see [Differences from
the Z80](#differences-from-the-z80).

## Differences from the Z80

The real, confirmed differences from the Z80 core this project's
CP/M-side emulator implements (`cpm/emu/src/z80.c`) — see
`gameboy/docs/GAMEBOY_ROADMAP.md`'s "Architecture decision" section for why
this is a standalone, independently-implemented core rather than a
parameterized variant of `z80.c`, and `cpm/docs/Z80_REFERENCE.md` for the
Z80-side detail every line below is contrasted against:

- **No `IX`/`IY` index registers** — and therefore none of the
  `DD`/`FD`-prefixed instructions, `(IX+d)`/`(IY+d)` addressing, or the
  Z80's undocumented `IXH`/`IXL`/`IYH`/`IYL` 8-bit halves.
- **No alternate register set** — no `EXX`, no `EX AF,AF'`.
- **No `ED`-prefixed instructions at all** — the Z80's entire `ED`
  opcode space (block transfer/search `LDIR`/`CPIR`/etc., 16-bit
  `ADC`/`SBC HL,rr`, `NEG`, `RLD`/`RRD`, `IM 0`/`1`/`2`, `RETI`/`RETN`,
  `LD A,I`/`LD A,R`/`LD I,A`/`LD R,A`, `IN r,(C)`/`OUT (C),r`) simply
  doesn't exist on the SM83. (`RETI` still exists — it's just a plain,
  unprefixed opcode here, `0xD9`, not `ED 4D`.)
- **No `IN`/`OUT`** — I/O is entirely memory-mapped
  (`0xFF00`-`0xFF7F`), not a separate address space. See
  `gameboy/docs/HARDWARE_REFERENCE.md`'s Memory map.
- **Adds its own instructions the Z80 doesn't have**: `STOP`, the
  `LD (HL+),A`/`LD (HL-),A`/`LD A,(HL+)`/`LD A,(HL-)` auto-increment/
  decrement forms, `LDH`, `ADD SP,e8`/`LD HL,SP+e8`, and `SWAP`
  (occupying the Z80's undocumented-`SLL` `CB`-opcode slot with a real,
  documented instruction instead).
- **No `P/V` (parity/overflow) flag** — only `Z N H C`, so conditional
  jump/call/return forms are restricted to `NZ Z NC C` (no `PO PE P M`).
- **No `X`/`Y` undocumented flag bits** — the SM83's `F` register only
  implements its top 4 bits at all; see [Flags](#flags-the-f-register).
- **`DAA`'s exact behavior** was independently verified against a real
  reference rather than assumed identical to the Z80's.
- **Runs at ~4.194304 MHz** (identical nominal clock to a 4 MHz-ish Z80,
  though a real Game Boy's crystal is exact where "4 MHz Z80" usually
  isn't) — the T-state cycle-counting approach carries over, but real
  timing values come from a grounded Game Boy reference (the official
  opcode table's own per-instruction cycle counts), not carried over
  from Z80 timings.

## Timing model

`gb_cpu_step()` returns the number of T-cycles (4.194304 MHz ticks —
*not* the "M-cycles" = T-cycles/4 some references count in instead) the
executed instruction took, mirroring `z80_step()`'s own convention in
`cpm/emu/src/z80.h`. Every opcode's cycle count in this codebase was
checked against the official gbdev.io opcode table rather than trusted
from memory — this caught one real erratum in passing: a
commonly-mirrored community JSON dataset
(`lmmendes/game-boy-opcodes`) lists `BIT b,(HL)` as 16 cycles; the
official table (and this emulator) has it at 12, since `BIT` never
writes anything back the way the other `CB` read-modify-write ops do.

## The HALT bug

A genuine hardware quirk (pandocs' `halt.md`), not an emulator bug:
executing `HALT` while `IME=0` **and** an interrupt is already pending
(`IE & IF & 0x1F != 0`) fails to actually halt at all. Instead, the
byte immediately after `HALT` gets fetched and executed *twice* — once
right away (with its real side effects, not just a wasted fetch), and
once more on the next `gb_cpu_step()` call as if `HALT` had advanced
`PC` normally. `cpu.c`'s `gb_op_ld_r_r()` (the `0x76` special case)
detects the triggering condition and sets `cpu->halt_bug`;
`gb_cpu_step()` executes the following opcode once immediately and then
rewinds `PC` before returning, so the very next step re-executes it
"for real."

When `HALT` executes with no interrupt pending (regardless of `IME`),
it halts normally: `gb_cpu_step()` just burns 4 cycles per call without
advancing `PC`, until `IE & IF & 0x1F` becomes nonzero — at which point
`cpu->halted` clears and normal execution resumes, whether or not
`IME` is actually set to service that interrupt (pandocs' `halt.md`:
"if no interrupt is pending, `HALT` executes as normal, and the CPU
resumes regular execution as soon as an interrupt becomes pending" —
`IME` only gates whether the handler actually *runs*, not whether
`HALT` itself ends).

## Interrupt handling

Five interrupt sources exist: V-Blank, LCD STAT, Timer, Serial, and
Joypad — see `gameboy/docs/HARDWARE_REFERENCE.md`'s Interrupts section for
`IE`/`IF`'s exact bit layout and how each source requests one.
CPU-side, per pandocs' `Interrupts.md`:

- **`IME`** (Interrupt Master Enable) gates whether a pending, enabled
  interrupt actually gets *dispatched* — it doesn't affect whether `IF`
  bits get set by a hardware event, only whether the CPU acts on them.
- **`EI`**'s enable is delayed by one instruction — `cpu->ime_pending`
  is set when `EI` executes, and `gb_cpu_step()` only promotes it to
  `cpu->ime = 1` *after* the instruction immediately following `EI` has
  fully executed. This means a real, deliberate `EI` / `RET` idiom at
  the end of an interrupt handler can't be interrupted again before the
  `RET` itself runs.
- **`DI`** clears `IME` immediately, and also cancels a still-pending
  `EI` if one was mid-delay.
- **`RETI`** sets `IME` immediately (no delay) — safe, since by
  definition it's returning from a handler that already finished.
- **Priority** follows bit order: bit 0 (V-Blank) highest, bit 4
  (Joypad) lowest. `gb_cpu_step()` scans `IE & IF & 0x1F` from bit 0
  upward and dispatches the first set bit, which is simultaneously
  "find a pending interrupt" and "find the highest-priority one" in the
  same pass.
- **Dispatch cost**: pushes `PC`, jumps to the source's fixed vector
  (`0x40`/`0x48`/`0x50`/`0x58`/`0x60` for V-Blank/STAT/Timer/Serial/
  Joypad respectively), and costs 20 T-states (5 M-cycles) — clearing
  both `IME` and the corresponding `IF` bit as part of the same step,
  exactly like a `CALL` to that address.
- **Waking from `HALT`**: any pending, enabled interrupt wakes the CPU
  even with `IME=0` — see [The HALT bug](#the-halt-bug) for the
  distinction between "wakes" and "actually dispatches."

## Implementation status

The full SM83 instruction set — every opcode in both the unprefixed and
`CB`-prefixed tables — is implemented in `gameboy/src/cpu.c`, verified
against Blargg's `cpu_instrs`/`instr_timing` test ROMs (12 of 12
sub-tests passing; see `gameboy/docs/GAMEBOY_ROADMAP.md`'s Phase 1/4
status). This includes:

- All 8-bit/16-bit load, arithmetic, logic, rotate/shift, and bit-test
  forms above.
- `STOP`'s real 2-byte encoding and its system-counter-reset side
  effect.
- The HALT bug, exactly as described above.
- `EI`'s one-instruction enable delay.
- Full interrupt dispatch (priority, vector jump, cost) — not just
  `IE`/`IF` bookkeeping with nothing reading them.

**Not modeled**: real hardware's low-power STOP mode and exiting it via
a joypad press — `cpu->stopped` is set, but nothing currently checks it
to actually suspend execution, since there's no real input source yet
to ever trigger the exit condition (deferred to Phase 7's real front
end — see `gameboy/docs/GAMEBOY_ROADMAP.md`).

There are 11 genuinely illegal opcodes in the unprefixed table —
`0xD3`/`0xDB`/`0xDD`/`0xE3`/`0xE4`/`0xEB`/`0xEC`/`0xED`/`0xF4`/`0xFC`/
`0xFD` — confirmed against the official opcode table (each is labeled
`ILLEGAL_xx` there, distinct from just being absent the way an
incomplete table would leave them). Real hardware locks up executing
one of these; `gb_cpu_step()` returns `-1` for them, matching
`z80_step()`'s own convention for "this is a genuine bug in whatever's
running," not a gap in this emulator.
