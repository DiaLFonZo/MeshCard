"""Run a command on the Zero in a 45x10 terminal and print what the screen shows.
Usage: python zero_screen_test.py "command" [seconds] [keys...]"""
import sys
import time

import paramiko
import pyte

COLS, ROWS = 45, 10
cmd = sys.argv[1]
wait = float(sys.argv[2]) if len(sys.argv) > 2 else 8
keys = sys.argv[3:]

screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)
c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect("192.168.0.44", username="pi", password="pi", allow_agent=False, look_for_keys=False)
ch = c.get_transport().open_session()
ch.get_pty(term="st-256color", width=COLS, height=ROWS)
ch.exec_command("export COLORTERM=truecolor; " + cmd)


def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        if ch.recv_ready():
            stream.feed(ch.recv(65536))
        elif ch.exit_status_ready():
            break
        else:
            time.sleep(0.05)


def show(label):
    print(f"--- {label} ---")
    for y in range(ROWS):
        row = screen.buffer[y]
        first = next((row[x] for x in range(COLS) if row[x].data.isalpha()), None)
        attr = f"  [fg={first.fg} bg={first.bg} rev={first.reverse}]" if first else ""
        print("|" + "".join(row[x].data for x in range(COLS)) + "|" + attr)


pump(wait)
show("start")
for k in keys:
    ch.send({"ENTER": "\r", "ESC": "\x1b", "TAB": "\t", "DOWN": "\x1b[B", "UP": "\x1b[A",
             "LEFT": "\x1b[D", "RIGHT": "\x1b[C"}.get(k, k))
    pump(2)
    show(f"after {k}")
if ch.exit_status_ready():
    print("EXIT", ch.recv_exit_status())
ch.close()
c.close()
