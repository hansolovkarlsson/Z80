#!/usr/bin/env python3
"""Run a command on a pseudo-terminal and play a timed script of keys at it.

Usage: python3 scripts/ptysession.py STEPS -- COMMAND [ARG...]

STEPS is a comma-separated list, each step one of:
  wait:SECONDS   read the command's output for that long
  key:HEX        send one byte (1d is Ctrl-], 1c is Ctrl-\\)
  text:WORDS     send WORDS followed by a newline, in one write
  mark:NAME      note the time, for the report

Everything the command wrote is printed, with ANSI escapes stripped, then
one line: `ptysession: wall=S NAME=S ...`, where wall is the time from the
start to the last step and each NAME is its mark's time since the start.

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


def main():
    if len(sys.argv) < 4 or sys.argv[2] != "--":
        sys.exit(__doc__)
    steps = sys.argv[1].split(",")
    command = sys.argv[3:]

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
            os.write(fd, bytes([int(arg, 16)]))
        elif kind == "text":
            os.write(fd, arg.encode() + b"\n")
        elif kind == "mark":
            marks.append((arg, time.monotonic() - start))
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
    sys.stdout.write(re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text).replace("\r", ""))
    report = " ".join(f"{name}={t:.2f}" for name, t in marks)
    print(f"\nptysession: wall={wall:.2f} {report}")


if __name__ == "__main__":
    main()
