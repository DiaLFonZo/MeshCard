"""Copy local files to the Cardputer Zero over SFTP (CRLF -> LF for text files).
Usage: python zero_put.py local_path remote_path [local_path remote_path ...]
(env ZERO_HOST, ZERO_USER, ZERO_PASS; defaults 192.168.0.44 pi pi)"""
import os
import sys

import paramiko

host = os.environ.get("ZERO_HOST", "192.168.0.44")
c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(host, username=os.environ.get("ZERO_USER", "pi"), password=os.environ.get("ZERO_PASS", "pi"),
          timeout=10, allow_agent=False, look_for_keys=False)
s = c.open_sftp()
args = sys.argv[1:]
for local, remote in zip(args[::2], args[1::2]):
    data = open(local, "rb").read()
    if local.endswith((".py", ".sh", ".desktop", ".yaml", ".service", ".txt")):
        data = data.replace(b"\r\n", b"\n")
    with s.open(remote, "wb") as f:
        f.write(data)
    print(f"{local} -> {remote} ({len(data)} bytes)")
s.close()
c.close()
