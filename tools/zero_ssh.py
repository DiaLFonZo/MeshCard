"""Run a shell command on the Cardputer Zero over SSH and print the output.
Usage: python zero_ssh.py "command"   (env ZERO_HOST, ZERO_USER, ZERO_PASS; defaults 192.168.0.44 pi pi)"""
import os
import sys

import paramiko

host = os.environ.get("ZERO_HOST", "192.168.0.44")
user = os.environ.get("ZERO_USER", "pi")
pw = os.environ.get("ZERO_PASS", "pi")
c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(host, username=user, password=pw, timeout=10, allow_agent=False, look_for_keys=False)
_, out, err = c.exec_command(sys.argv[1], timeout=int(os.environ.get("ZERO_TIMEOUT", "600")))
for line in out:
    print(line, end="")
e = err.read().decode(errors="replace")
if e:
    print(e, end="", file=sys.stderr)
code = out.channel.recv_exit_status()
c.close()
sys.exit(code)
