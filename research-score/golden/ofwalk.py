#!/usr/bin/env python3
"""Walk the OpenBIOS device tree over a serial unix socket.
python3 ofwalk.py /tmp/qemu-golden/of.sock out.txt"""
import re
import socket
import sys
import time


class OF:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.s.settimeout(0.3)
        self.buf = b""

    def read_until_idle(self, idle=0.6, maxwait=8.0):
        out = b""
        last = time.time()
        start = last
        while True:
            try:
                d = self.s.recv(65536)
                if d:
                    out += d
                    last = time.time()
                    continue
            except socket.timeout:
                pass
            now = time.time()
            if now - last > idle or now - start > maxwait:
                return out.decode("latin-1")

    def cmd(self, line, idle=0.6, maxwait=8.0):
        self.s.sendall((line + "\n").encode())
        return self.read_until_idle(idle, maxwait)


def main():
    of = OF(sys.argv[1])
    of.read_until_idle(1.0)
    of.cmd("")  # wake prompt
    ls = of.cmd("show-devs", idle=1.5, maxwait=30)
    paths = []
    for line in ls.splitlines():
        m = re.match(r"^\s*[0-9a-fA-F]{6,8}\s+(/\S*)", line)
        if m:
            paths.append(m.group(1))
    paths = ["/"] + [p for p in paths if p != "/"]
    with open(sys.argv[2], "w") as f:
        f.write("### show-devs\n" + ls + "\n")
        for p in paths:
            of.cmd("dev " + p, idle=0.3, maxwait=3)
            props = of.cmd(".properties", idle=0.8, maxwait=10)
            f.write(f"\n### {p}\n{props}\n")
    print(f"{len(paths)} nodes -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
