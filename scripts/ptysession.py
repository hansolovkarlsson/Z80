#!/usr/bin/env python3
"""Run a command on a pseudo-terminal and play a timed script of keys at it.

Usage: python3 scripts/ptysession.py [--raw] STEPS -- COMMAND [ARG...]

STEPS is a comma-separated list, each step one of:
  wait:SECONDS   read the command's output for that long
  key:HEX        send the bytes in one write, as a terminal sends a key
                 (1d is Ctrl-], 1c is Ctrl-\\, 1b5b32347e is F12)
  text:WORDS     send WORDS followed by a newline, in one write
  mark:NAME      note the time, for the report

Everything the command wrote is printed, with ANSI escapes stripped, then
one line: `ptysession: wall=S NAME=S ...`, where wall is the time from the
start to the last step and each NAME is its mark's time since the start.

--raw strips nothing, for checks on what the terminal itself was told
(whether its cursor is hidden, say), which is decided by exactly the
escapes the default strips. Control bytes are made visible instead: ESC
as \\e, CR as \\r, any other but newline and tab as \\xHH. Each mark is
written into the output as {mark:NAME} at the point the command's output
had reached when the step ran, so a check can ask what state the terminal
was in at that moment (scripts/testlib.sh's tl_cursor_states does).

It exists for what a pipe cannot test: a program that puts the terminal in
raw mode and takes its interrupt character from it, the way --interactive
does with Ctrl-] under the debugger. No third-party modules, the same rule
the rest of this repository's tooling follows.
"""

import os
import pty
import re
import select
import sys
import time


def visible(text):
    """Show every control byte a terminal would act on, keeping lines."""
    named = {"\x1b": "\\e", "\r": "\\r", "\n": "\n", "\t": "\t"}
    return "".join(named.get(c, f"\\x{ord(c):02x}" if ord(c) < 0x20 or c == "\x7f" else c)
                   for c in text)


def main():
    args = sys.argv[1:]
    raw = bool(args) and args[0] == "--raw"
    if raw:
        args = args[1:]
    if len(args) < 3 or args[1] != "--":
        sys.exit(__doc__)
    steps = args[0].split(",")
    command = args[2:]

    pid, fd = pty.fork()
    if pid == 0:
        try:
            os.execvp(command[0], command)
        finally:
            os._exit(127)

    out = bytearray()

    def pump(seconds):
        end = time.monotonic() + seconds
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return
            ready, _, _ = select.select([fd], [], [], min(left, 0.05))
            if ready:
                try:
                    out.extend(os.read(fd, 65536))
                except OSError:   # the command has exited
                    time.sleep(left)
                    return

    start = time.monotonic()
    marks = []
    for step in steps:
        kind, _, arg = step.partition(":")
        if kind == "wait":
            pump(float(arg))
        elif kind == "key":
            os.write(fd, bytes.fromhex(arg))
        elif kind == "text":
            os.write(fd, arg.encode() + b"\n")
        elif kind == "mark":
            marks.append((arg, time.monotonic() - start, len(out)))
        else:
            sys.exit(f"ptysession: unknown step '{step}'")
    wall = time.monotonic() - start
    pump(2.0)   # let the command print its summary and exit
    try:
        os.kill(pid, 9)
    except ProcessLookupError:
        pass
    os.waitpid(pid, 0)

    text = out.decode("latin-1")
    if raw:
        pieces, at = [], 0
        for name, _, offset in marks:
            pieces += [visible(text[at:offset]), "{mark:" + name + "}"]
            at = offset
        pieces.append(visible(text[at:]))
        sys.stdout.write("".join(pieces))
    else:
        sys.stdout.write(re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text).replace("\r", ""))
    report = " ".join(f"{name}={t:.2f}" for name, t, _ in marks)
    print(f"\nptysession: wall={wall:.2f} {report}")


if __name__ == "__main__":
    main()
