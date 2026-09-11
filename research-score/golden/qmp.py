#!/usr/bin/env python3
"""Minimal QMP client: python3 qmp.py <sock> <command> [json-args]"""
import json
import socket
import sys


def qmp(sock_path, command, args=None):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    f = s.makefile("rwb", buffering=0)
    f.readline()  # greeting
    f.write(b'{"execute":"qmp_capabilities"}\n')
    f.readline()
    msg = {"execute": command}
    if args:
        msg["arguments"] = args
    f.write((json.dumps(msg) + "\n").encode())
    while True:
        line = f.readline()
        if not line:
            return None
        obj = json.loads(line)
        if "return" in obj or "error" in obj:
            return obj


if __name__ == "__main__":
    sock, cmd = sys.argv[1], sys.argv[2]
    args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else None
    print(json.dumps(qmp(sock, cmd, args)))
