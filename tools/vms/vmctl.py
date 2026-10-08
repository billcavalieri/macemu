#!/usr/bin/env python3
"""Talks to a running SheepShaver VM over its control socket (the same socket the MCP server uses).

  vmctl.py ID OP [key=value ...]        ID is the VM id (its folder name under .../SheepShaver/VMs) or a socket path
  vmctl.py a1b2c3 status
  vmctl.py a1b2c3 mouse_click x=100 y=40 count=2
  vmctl.py a1b2c3 screenshot out=/tmp/shot.png max_width=800     (out= saves the picture instead of printing it)

Also importable: `Control(path).call("status")`, and `decode_png(bytes)` for tests."""
import base64, json, os, re, socket, struct, sys, zlib

def socket_path(vm_id):
    if "/" in vm_id:
        return vm_id
    safe = "".join(c for c in vm_id if c.isalnum() or c == "-")[:12] or "vm"
    return f"/tmp/sheepshaver-{os.getuid()}/{safe}.sock"

class Control:
    def __init__(self, path_or_id, timeout=120):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(timeout)
        self.sock.connect(socket_path(path_or_id))
        self.file = self.sock.makefile("rwb")
        self.n = 0
    def call(self, op, **args):
        self.n += 1
        self.file.write((json.dumps({"id": self.n, "op": op, **args}) + "\n").encode())
        self.file.flush()
        reply = json.loads(self.file.readline())
        if not reply.get("ok"):
            raise RuntimeError(reply.get("error", "failed"))
        return reply["result"]
    def raw(self, line):
        self.file.write(line.encode() + b"\n"); self.file.flush()
        return json.loads(self.file.readline())
    def close(self):
        self.sock.close()

def decode_png(data):
    """(width, height, rows) of an 8-bit RGB or RGBA non-interlaced PNG; rows are lists of (r, g, b)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and interlace == 0, f"unsupported PNG {depth}/{ctype}/{interlace}"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return w, h, rows

def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    args = {}
    out = None
    for item in argv[3:]:
        k, v = item.split("=", 1)
        if k == "out": out = v; continue
        args[k] = int(v) if re.fullmatch(r"-?\d+", v) else (True if v == "true" else False if v == "false" else v)
    c = Control(argv[1])
    result = c.call(argv[2], **args)
    if "png_base64" in result:
        png = base64.b64decode(result.pop("png_base64"))
        if out:
            open(out, "wb").write(png); result["saved"] = out
        else:
            result["png_bytes"] = len(png)
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main(sys.argv)
