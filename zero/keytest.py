#!/usr/bin/env python3
"""Show and record the raw bytes each key sends in the Zero's terminal. q quits."""
import os
import sys
import termios
import tty

LOG = "/tmp/keytest.txt"
fd = sys.stdin.fileno()
old = termios.tcgetattr(fd)
with open(LOG, "w") as log:
    log.write(f"TERM={os.environ.get('TERM')}\n")
    print("Press keys. q = quit\r")
    tty.setraw(fd)
    try:
        while True:
            data = os.read(fd, 32)
            line = repr(data)
            log.write(line + "\n")
            log.flush()
            sys.stdout.write(line + "\r\n")
            sys.stdout.flush()
            if data == b"q":
                break
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
