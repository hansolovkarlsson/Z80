#!/usr/bin/env python3
"""Translates 8080-mnemonic Z80/CP/M-era assembly into Z80 mnemonics.

z80asm (this project's assembler) speaks Z80 mnemonics only - real CP/M
source predates the Z80 and is written against the 8080 instruction set.
This is a general-purpose line-level translator (not specific to the
CCP), built because we've now hit this same 8080-vs-Z80 gap three times
(a hand-translated resources/user_prompt.txt, 3 stray lines in SARGON,
and now this entire ~1300-line CCP) - worth doing properly once.

Approach: for each line, find the first whitespace-separated word that's
a *known 8080 mnemonic* (scanning left to right so a leading label -
colon-terminated or not - is naturally skipped, since none of DRI's own
label/constant names collide with an 8080 mnemonic). Everything before
that word (the label, if any) passes through unchanged; the word itself
is replaced per MNEMONIC_MAP, and the transform function rewrites its
operands (register-pair renaming for LXI/DAD/INX/DCX/LDAX/STAX, M -> (HL)
for 8-bit ALU/MOV forms, PSW -> AF for PUSH/POP, condition-code jump/
call/return forms). Everything else (comments, directives this project's
assembler already understands like DB/DW/DS/ORG/EQU/IF/ELSE/ENDIF) is
left untouched - only real 8080 opcodes are ever touched.

Deliberately NOT general enough to handle every 8080 assembler's syntax
quirks (this is a derive.sh-style one-off tool, not a shipped feature) -
see docs/ROADMAP.md for what specifically needed hand-fixing beyond this
script for ccp.asm itself.
"""
import re
import sys

REG8 = {"a": "a", "b": "b", "c": "c", "d": "d", "e": "e", "h": "h", "l": "l", "m": "(hl)"}
REGPAIR = {"b": "bc", "d": "de", "h": "hl", "sp": "sp", "psw": "af"}
COND = {"c", "nc", "z", "nz", "p", "m", "pe", "po"}


def split_ops(text):
    ops, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            ops.append(text[start:i].strip())
            start = i + 1
    tail = text[start:].strip()
    if tail:
        ops.append(tail)
    return ops


def reg8(tok):
    return REG8.get(tok.lower(), tok)


def regpair(tok):
    return REGPAIR.get(tok.lower(), tok)


# Each handler takes the raw operand string (everything after the
# mnemonic on the line, comment already stripped off by the caller) and
# returns (new_mnemonic, new_operand_string).
def h_mov(ops_text):
    d, s = split_ops(ops_text)
    return "ld", f"{reg8(d)},{reg8(s)}"


def h_mvi(ops_text):
    r, imm = split_ops(ops_text)
    return "ld", f"{reg8(r)},{imm}"


def h_lxi(ops_text):
    rp, imm = split_ops(ops_text)
    return "ld", f"{regpair(rp)},{imm}"


def h_lda(ops_text):
    return "ld", f"a,({ops_text})"


def h_sta(ops_text):
    return "ld", f"({ops_text}),a"


def h_lhld(ops_text):
    return "ld", f"hl,({ops_text})"


def h_shld(ops_text):
    return "ld", f"({ops_text}),hl"


def h_ldax(ops_text):
    return "ld", f"a,({regpair(ops_text)})"


def h_stax(ops_text):
    return "ld", f"({regpair(ops_text)}),a"


def h_inx(ops_text):
    return "inc", regpair(ops_text)


def h_dcx(ops_text):
    return "dec", regpair(ops_text)


def h_inr(ops_text):
    return "inc", reg8(ops_text)


def h_dcr(ops_text):
    return "dec", reg8(ops_text)


def h_dad(ops_text):
    return "add", f"hl,{regpair(ops_text)}"


def h_alu_r(z80_mnem, needs_a_prefix):
    def handler(ops_text):
        r = reg8(ops_text)
        return z80_mnem, (f"a,{r}" if needs_a_prefix else r)
    return handler


def h_alu_i(z80_mnem, needs_a_prefix):
    def handler(ops_text):
        return z80_mnem, (f"a,{ops_text}" if needs_a_prefix else ops_text)
    return handler


def h_push_pop(mnem):
    def handler(ops_text):
        return mnem, regpair(ops_text)
    return handler


def h_jmp(ops_text):
    return "jp", ops_text


def h_jcond(cond):
    def handler(ops_text):
        return "jp", f"{cond},{ops_text}"
    return handler


def h_ccond(cond):
    def handler(ops_text):
        return "call", f"{cond},{ops_text}"
    return handler


def h_rcond(cond):
    def handler(_ops_text):
        return "ret", cond
    return handler


def h_noop(mnem):
    def handler(ops_text):
        return mnem, ops_text
    return handler


def h_pchl(_ops_text):
    return "jp", "(hl)"


def h_xchg(_ops_text):
    return "ex", "de,hl"


MNEMONIC_MAP = {
    "mov": h_mov, "mvi": h_mvi, "lxi": h_lxi,
    "lda": h_lda, "sta": h_sta, "lhld": h_lhld, "shld": h_shld,
    "ldax": h_ldax, "stax": h_stax,
    "inx": h_inx, "dcx": h_dcx, "inr": h_inr, "dcr": h_dcr, "dad": h_dad,
    "add": h_alu_r("add", True), "adc": h_alu_r("adc", True),
    "sub": h_alu_r("sub", False), "sbb": h_alu_r("sbc", True),
    "ana": h_alu_r("and", False), "xra": h_alu_r("xor", False), "ora": h_alu_r("or", False),
    "cmp": h_alu_r("cp", False),
    "adi": h_alu_i("add", True), "aci": h_alu_i("adc", True),
    "sui": h_alu_i("sub", False), "sbi": h_alu_i("sbc", True),
    "ani": h_alu_i("and", False), "xri": h_alu_i("xor", False), "ori": h_alu_i("or", False),
    "cpi": h_alu_i("cp", False),
    "push": h_push_pop("push"), "pop": h_push_pop("pop"),
    "jmp": h_jmp, "pchl": h_pchl, "xchg": h_xchg,
    "rlc": h_noop("rlca"), "rrc": h_noop("rrca"), "ral": h_noop("rla"), "rar": h_noop("rra"),
}
for _c in COND:
    MNEMONIC_MAP[f"j{_c}"] = h_jcond(_c)
    MNEMONIC_MAP[f"c{_c}"] = h_ccond(_c)
    MNEMONIC_MAP[f"r{_c}"] = h_rcond(_c)

WORD_RE = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")


def split_comment(line):
    in_s = in_d = False
    for i, ch in enumerate(line):
        if ch == "'" and not in_d:
            in_s = not in_s
        elif ch == '"' and not in_s:
            in_d = not in_d
        elif ch == ";" and not in_s and not in_d:
            return line[:i], line[i:]
    return line, ""


def translate_line(line):
    code, comment = split_comment(line.rstrip("\r\n"))
    m = None
    for m in WORD_RE.finditer(code):
        if m.group(0).lower() in MNEMONIC_MAP:
            break
    else:
        return code + comment
    mnem = m.group(0).lower()
    prefix = code[: m.start()]
    ops_text = code[m.end():].strip()
    new_mnem, new_ops = MNEMONIC_MAP[mnem](ops_text)
    rebuilt = f"{prefix}{new_mnem}" + (f"\t{new_ops}" if new_ops else "")
    return rebuilt + ("\t" + comment if comment else "")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "r", errors="replace") as f:
        lines = f.readlines()
    out = [translate_line(l) for l in lines]
    with open(dst, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"Wrote {dst} ({len(out)} lines)", file=sys.stderr)


if __name__ == "__main__":
    main()
