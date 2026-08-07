# Z80 CPU Reference

A reference for the Z80 instruction set as implemented by this project's
emulator (`cpm/emu/src/`), assembler (`cpm/asm/src/`), and disassembler
(`cpm/disasm/src/`), including the undocumented instructions and flag
behavior that real Z80 hardware exhibits and that `zexall`/`zexdoc`
specifically test for. `bin/z80dasm` is a hands-on way to explore this
instruction set against real machine code — see `cpm/docs/ROADMAP.md`'s
Phase 2 section for what it covers.

Where this project's own implementation status matters (an instruction
the assembler can encode but the emulator can't yet execute, for
example), it's called out explicitly — see [Implementation status](#implementation-status)
at the end. Everything else describes real Z80 behavior, independent of
this codebase.

## Registers

**Main set**: `A` (accumulator) + `F` (flags), paired as `AF`; `B`,`C`
paired as `BC`; `D`,`E` paired as `DE`; `H`,`L` paired as `HL`.

**Alternate set**: `A'F'`, `B'C'`, `D'E'`, `H'L'` — swapped in via `EX
AF,AF'` (exchanges just the AF pair) and `EXX` (exchanges BC/DE/HL as a
group; AF is not affected by `EXX`).

**Index registers**: `IX`, `IY` — 16-bit, usable as `(IX+d)`/`(IY+d)`
base addresses. Each also has undocumented 8-bit halves: `IXH`/`IXL`
(high/low byte of IX) and `IYH`/`IYL` (of IY) — see
[Undocumented registers](#undocumented-registers-ixh-ixl-iyh-iyl).

**Special purpose**: `SP` (stack pointer), `PC` (program counter), `I`
(interrupt vector base), `R` (memory refresh counter, increments once per
instruction fetch, bit 7 preserved).

## Flags (the F register)

| Bit | Name  | Meaning                                    |
|-----|-------|---------------------------------------------|
| 7   | S     | Sign (copy of result bit 7)                 |
| 6   | Z     | Zero (result == 0)                          |
| 5   | Y     | **Undocumented** — see below                |
| 4   | H     | Half-carry (carry/borrow between bits 3/4)  |
| 3   | X     | **Undocumented** — see below                |
| 2   | P/V   | Parity (logical ops) or Overflow (arithmetic) |
| 1   | N     | Add/Subtract (set for subtract-family ops)  |
| 0   | C     | Carry                                       |

`X`/`Y` (bits 3 and 5) are undocumented but deterministic, and
`zexall.com` specifically checks them (unlike `zexdoc.com`, which only
checks the other six). The rule is **not** uniform across instructions:

- Most arithmetic/logic/rotate/shift results: copied from bits 3 and 5 of
  the actual result byte.
- `BIT b,(HL)`: copied from bits 3/5 of the high byte of `HL+1` (an
  internal "MEMPTR" register), **not** the tested byte. `BIT b,(IX+d)`/
  `BIT b,(IY+d)` similarly use the high byte of the effective address
  (`IX+d`), not the tested byte.
- `LDI`/`LDD`/`LDIR`/`LDDR`: derived from `(transferred byte + A)` — bit 3
  of that sum feeds `X`, bit 1 feeds `Y` (yes, bit *1*, not bit 5 — an
  original hardware quirk, not a typo).
- `CPI`/`CPD`/`CPIR`/`CPDR`: derived similarly, from the comparison
  result adjusted by the half-carry flag.
- `CP n`/`CP r`: `X`/`Y` come from the **operand**, not the (discarded)
  result — the one arithmetic instruction where this differs from `ADD`/
  `SUB`/etc.

## Addressing modes

| Mode              | Example      | Notes |
|--------------------|--------------|-------|
| Implied            | `NOP`        | no operand |
| Register           | `LD A,B`     | |
| Register indirect  | `LD A,(HL)`  | also `(BC)`, `(DE)`, `(SP)` (EX only), `(C)` (IN/OUT only) |
| Indexed            | `LD A,(IX+5)`| 8-bit signed displacement, `-128..+127` |
| Immediate          | `LD A,42`    | 8-bit; 16-bit form is `LD HL,1234h` |
| Extended (direct)  | `LD A,(1234h)` | absolute memory address |
| Relative           | `JR label`   | 8-bit signed displacement from the byte *after* the instruction |

## Instruction set by category

Each category below is a link between "what the mnemonic table looks
like" and where it lives in this codebase: the emulator's *decoder*
(`cpm/emu/src/z80.c`, dispatch built in `z80_init_tables()`) and the
assembler's *encoder* (`cpm/asm/src/encode.c`, dispatched from
`encode_instruction()`) implement these symmetrically — if you're
checking an exact opcode byte, both files are the authoritative source
(this document rounds to the pattern level, not literal hex tables).

### 8-bit load

- `LD r,r'` — any of `B C D E H L A` to any other (0x40-0x7F; 0x76 is
  `HALT`, not `LD (HL),(HL)`, which isn't representable in this encoding)
- `LD r,n`; `LD r,(HL)`; `LD (HL),r`; `LD (HL),n`
- `LD A,(BC)` / `LD A,(DE)` / `LD A,(nn)`; and the reverse (`LD
  (BC),A` etc.)
- `LD A,I` / `LD A,R` / `LD I,A` / `LD R,A` (`ED`-prefixed)
- `LD r,(IX+d)` / `LD r,(IY+d)`; `LD (IX+d),r` / `LD (IY+d),r`; `LD
  (IX+d),n` / `LD (IY+d),n`

### 16-bit load

- `LD dd,nn` (`dd` = `BC DE HL SP`)
- `LD HL,(nn)`; `LD dd,(nn)` (`ED`-prefixed, `dd` = `BC DE SP`); `LD
  (nn),HL`; `LD (nn),dd` (`ED`-prefixed)
- `LD IX,nn` / `LD IY,nn`; `LD IX,(nn)` / `LD IY,(nn)`; `LD (nn),IX` /
  `LD (nn),IY`
- `LD SP,HL` / `LD SP,IX` / `LD SP,IY`

### Exchange

- `EX DE,HL`; `EX AF,AF'`; `EX (SP),HL` / `EX (SP),IX` / `EX (SP),IY`;
  `EXX`

### Block transfer / search

- `LDI` `LDIR` `LDD` `LDDR` — copy `(HL)`→`(DE)`, `HL`/`DE` +1/-1, `BC`
  decrements; the `R` (repeat) forms loop while `BC != 0`
- `CPI` `CPIR` `CPD` `CPDR` — compare `A` against `(HL)`, `HL` +1/-1,
  `BC` decrements; the `R` forms also stop early if a match is found

### 8-bit arithmetic / logic

`ADD` `ADC` `SUB` `SBC` `AND` `XOR` `OR` `CP`, each against: a register,
an 8-bit immediate, `(HL)`, `(IX+d)`, `(IY+d)`, or (undocumented) `IXH`/
`IXL`/`IYH`/`IYL`.

### General-purpose accumulator/flag ops

`DAA` `CPL` `NEG` (`ED`-prefixed) `CCF` `SCF` `NOP` `HALT` `DI` `EI`

### 16-bit arithmetic

- `INC ss` / `DEC ss` (`ss` = `BC DE HL SP`); `INC IX`/`IY`, `DEC
  IX`/`IY`
- `ADD HL,ss`; `ADC HL,ss` / `SBC HL,ss` (`ED`-prefixed, unlike `ADD
  HL,ss` which isn't); `ADD IX,pp` (`pp` = `BC DE IX SP`); `ADD IY,rr`
  (`rr` = `BC DE IY SP`)

### Rotate / shift

- `RLCA` `RRCA` `RLA` `RRA` — accumulator-only, 1 byte
- `RLC` `RRC` `RL` `RR` `SLA` `SRA` `SRL` r/`(HL)`/`(IX+d)`/`(IY+d)`
  (`CB`-prefixed) — plus undocumented `SLL`/`SL1`, see below
- `RLD` `RRD` (`ED`-prefixed) — BCD digit rotate through `(HL)` and the
  low nibble of `A`

### Bit test / set / reset

`BIT` `SET` `RES` *b*,r/`(HL)`/`(IX+d)`/`(IY+d)` (`CB`-prefixed), *b* =
0-7

### Jump

- `JP nn`; `JP cc,nn` (*cc* = `NZ Z NC C PO PE P M`); `JP (HL)` / `JP
  (IX)` / `JP (IY)` (jumps to the address *in* HL/IX/IY — not indirect
  through it)
- `JR e`; `JR cc,e` (*cc* restricted to `NZ Z NC C` — no parity/sign
  forms); `DJNZ e` (decrements `B`, jumps if nonzero)

### Call / return

- `CALL nn`; `CALL cc,nn`
- `RET`; `RET cc`; `RETI` / `RETN` (`ED`-prefixed — see
  [Implementation status](#implementation-status))
- `RST p` (*p* = `00h 08h 10h 18h 20h 28h 30h 38h`)

### Input / output

`IN A,(n)`; `IN r,(C)` (`ED`-prefixed); `OUT (n),A`; `OUT (C),r`
(`ED`-prefixed) — see [Implementation status](#implementation-status)

### CPU control

`NOP` `HALT` `DI` `EI` `IM 0` / `IM 1` / `IM 2` (`ED`-prefixed) — see
[Implementation status](#implementation-status)

## Undocumented opcodes and behavior

### Undocumented registers: IXH, IXL, IYH, IYL

When a `DD`/`FD` prefix precedes an opcode in the `LD r,r'` (`0x40-0x7F`)
or ALU (`0x80-0xBF`) range, and the register-field bits select the `H`
position (`100`) or `L` position (`101`), the CPU substitutes the
high/low byte of `IX`/`IY` instead of the real `H`/`L` — **unless** the
*other* operand of that same instruction is `(HL)`-turned-`(IX+d)`/
`(IY+d)` memory, in which case `H`/`L` still mean the real registers.
For example: `LD H,(IX+d)` loads the real `H` register, not `IXH` —
because the instruction already involves `(IX+d)` memory as the other
operand. But `LD IXH,B` really does target `IXH`.

Also applies to the 8-bit inc/dec/immediate-load forms: `INC IXH` / `DEC
IXH` / `LD IXH,n` (and the `IXL`/`IYH`/`IYL` equivalents) —
`DD`/`FD`-prefixed versions of the normal `0x24`/`0x25`/`0x26` (H-position)
and `0x2C`/`0x2D`/`0x2E` (L-position) opcodes.

Mixing an `IX` half with an `IY` half in one instruction (e.g. `LD
IXH,IYL`) is undefined on real hardware; this project's assembler
(`encode.c`) rejects it as an error rather than guessing.

### Undocumented shift: SLL (also written SL1)

`CB`-prefixed, in the same 3-bit "operation" field as `RLC`/`RRC`/`RL`/
`RR`/`SLA`/`SRA`/`SRL` — `SLL` occupies the otherwise-unused value between
`SRA` (5) and `SRL` (7). It shifts left like `SLA`, but sets bit 0 to
**1** instead of 0 (a real hardware quirk — the name suggests "shift
left logical" but it doesn't zero-fill like you'd expect).

### Duplicate NEG encodings

`ED 0x44`, `0x4C`, `0x54`, `0x5C`, `0x64`, `0x6C`, `0x74`, `0x7C` — eight
different byte values, all behaving as plain `NEG`. Only `0x44` is the
"canonical" documented one; the rest are undocumented duplicates.

### Undocumented `(HL)`/`(IX+d)`/`(IY+d)` copy-back on `CB` rotate/shift/SET/RES

When a `DD`/`FD CB d op` instruction targets `(IX+d)`/`(IY+d)` with a
rotate/shift/`SET`/`RES` operation (not `BIT`), the result is written
back to memory as normal — but if the low 3 bits of the `CB` opcode
select a register other than `(HL)`'s slot (`110`), the result is
*also* copied into that register. E.g. `DD CB d 06` is `RLC (IX+d)`
(no register copy, since `110` = the memory slot itself), but `DD CB d
00` is `RLC (IX+d)` with the result *also* stored into `B` — an
undocumented side effect of how the real hardware's internal register
selection works for this double-prefixed form.

## Implementation status

Nearly every instruction real Z80 hardware supports is executable by
`cpm/emu/src/z80.c` (verified by `zexall`/`zexdoc`, plus `cpm/asm/examples/
gaps_test.asm` for the instructions those exercisers don't touch).
Interrupt delivery (`INT`/`NMI` from the host side, not an instruction at
all but closely tied to `IM 0`/`1`/`2`/`DI`/`EI`/`RETI`/`RETN`) is
implemented too — `z80_request_int()`/`z80_request_nmi()`/
`z80_clear_int()` in `z80.h`, grounded against the Zilog Z80 CPU User
Manual's "Interrupt Response" section, with `cpm/tests/test_interrupts.c`
as its regression coverage (no real interrupt-raising device is attached
yet — see `cpm/docs/ROADMAP.md`'s Status section). The one remaining gap:

| Instructions | Emulator status |
|---|---|
| `IM 0` with a multi-byte/prefixed device-supplied instruction | Only a single-byte `RST` device vector is supported (the real-world norm — the Zilog manual itself calls this "often" the case) — a non-`RST` byte returns the same `-1` "unimplemented" signal a genuinely unrecognized opcode gives, a documented, deliberate scope decision rather than a silent wrong answer. See `cpm/docs/ROADMAP.md`'s Status section for why a fully general "any instruction on the bus" isn't implemented. |

`IN A,(n)`/`IN r,(C)`/`OUT (n),A`/`OUT (C),r`, `IM 0`/`1`/`2`, `RETI`/
`RETN`, and `LD A,I`/`LD A,R`/`LD I,A`/`LD R,A` are all implemented,
including their undocumented duplicate `ED` encodings. `IN`/`OUT` are
backed by a real 256-entry port array (`cpu->io_ports`) rather than being a
no-op — no actual devices are attached, but a port read now returns
whatever was last written to it.

Everything else in this document — including the undocumented
`IXH`/`IXL`/`IYH`/`IYL` forms, `SLL`, and all the `X`/`Y` flag quirks — is
implemented and verified: both `zexall.com` and `zexdoc.com` run to
completion with 67/67 tests OK, 0 errors.
