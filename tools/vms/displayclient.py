#!/usr/bin/env python3
"""A viewer of a VM that runs with --embedded, for tests: maps its shared-memory frames (display_shm.h), keeps the viewer
heartbeat going, and speaks the display socket (DisplayProtocol.swift) to send input and read events.

    from displayclient import Shm, DisplayLink
    shm = Shm("disp"); shm.start_heartbeat()           # the VM draws only while this runs
    link = DisplayLink("disp"); link.mouse_abs(512, 384); link.key(0x37, True)
"""
import mmap, os, socket, struct, threading, time
import _posixshmem

HEADER_BYTES = 16384
MAGIC = 0x5348454550444953
OFF = dict(magic=0, version=8, slots=12, slotBytes=16, headerBytes=24, maxWidth=28, maxHeight=32, pixelFormat=36, generation=40,
           width=44, height=48, bytesPerRow=52, latestSlot=56, readingSlot=60, frameCounter=64, publishNs=72,
           viewerHeartbeatNs=80, viewerIntervalNs=88, vmPid=96)
NO_SLOT = 0xffffffff

def uptime_ns():
    return time.clock_gettime_ns(time.CLOCK_UPTIME_RAW)

def shm_name(vm_id, uid=None):
    uid = os.getuid() if uid is None else uid
    safe = "".join(c for c in vm_id if c.isalnum() or c == "-")[:12] or "vm"
    return f"/sheep.{uid}.{safe}"

def display_socket_path(vm_id, uid=None):
    uid = os.getuid() if uid is None else uid
    safe = "".join(c for c in vm_id if c.isalnum() or c == "-")[:12] or "vm"
    return f"/tmp/sheepshaver-{uid}/{safe}.disp"

class Shm:
    def __init__(self, vm_id):
        fd = _posixshmem.shm_open(shm_name(vm_id), os.O_RDWR, 0o600)
        self.mm = mmap.mmap(fd, os.fstat(fd).st_size)
        os.close(fd)
        self._beat = None
        self._stop = threading.Event()

    def u32(self, name):
        return struct.unpack_from("<I", self.mm, OFF[name])[0]
    def u64(self, name):
        return struct.unpack_from("<Q", self.mm, OFF[name])[0]
    def put64(self, name, value):
        struct.pack_into("<Q", self.mm, OFF[name], value)
    def put32(self, name, value):
        struct.pack_into("<I", self.mm, OFF[name], value)

    @property
    def ready(self):
        return self.u64("magic") == MAGIC

    def start_heartbeat(self, interval_ns=16_666_667, period=0.008):
        """The viewer is looking: the VM may draw. Stops with stop_heartbeat()."""
        self._stop.clear()
        def beat():
            while not self._stop.is_set():
                self.put64("viewerIntervalNs", interval_ns)
                self.put64("viewerHeartbeatNs", uptime_ns())
                time.sleep(period)
        self._beat = threading.Thread(target=beat, daemon=True)
        self._beat.start()

    def stop_heartbeat(self):
        self._stop.set()
        if self._beat:
            self._beat.join()

    def newest(self):
        """(frame counter, slot, width, height, bytesPerRow) of the latest published frame, held against reuse while read."""
        for _ in range(8):
            counter = self.u64("frameCounter")
            slot = self.u32("latestSlot")
            if slot == NO_SLOT:
                return counter, None, 0, 0, 0
            self.put32("readingSlot", slot)
            if self.u32("latestSlot") == slot:
                return counter, slot, self.u32("width"), self.u32("height"), self.u32("bytesPerRow")
        self.put32("readingSlot", NO_SLOT)
        return counter, None, 0, 0, 0

    def release(self):
        self.put32("readingSlot", NO_SLOT)

    def pixel(self, slot, bpr, x, y):
        off = HEADER_BYTES + slot * self.u64("slotBytes") + y * bpr + x * 4
        b, g, r, a = self.mm[off:off + 4]
        return r, g, b

    def sample(self, step=4):
        """Every step-th pixel of the newest frame as {(x, y): (r, g, b)}, or None before the first frame."""
        counter, slot, w, h, bpr = self.newest()
        if slot is None:
            return None
        base = HEADER_BYTES + slot * self.u64("slotBytes")
        out = {}
        for y in range(0, h, step):
            row = self.mm[base + y * bpr: base + y * bpr + w * 4]
            for x in range(0, w, step):
                out[(x, y)] = (row[x * 4 + 2], row[x * 4 + 1], row[x * 4])
        self.release()
        return out

def frame(kind, payload=b""):
    return bytes([len(payload) + 1, kind]) + payload

class DisplayLink:
    KINDS = {0x81: "guestMode", 0x82: "cursor", 0x83: "cursorHidesHost", 0x84: "arrow", 0x85: "shearsPointer", 0x86: "shearsStatus", 0x87: "problem"}

    def __init__(self, vm_id):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(display_socket_path(vm_id))
        self.events = []                      # (monotonic time, name, payload bytes)
        self.cond = threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        while True:
            try:
                chunk = self.sock.recv(4096)
            except OSError:
                return
            if not chunk:
                return
            buf += chunk
            while buf and len(buf) >= 1 + buf[0]:
                n = buf[0]
                kind, payload = buf[1], buf[2:1 + n]
                buf = buf[1 + n:]
                with self.cond:
                    self.events.append((time.monotonic(), self.KINDS.get(kind, hex(kind)), payload))
                    self.cond.notify_all()

    def wait_event(self, name, timeout=5, since=0):
        end = time.monotonic() + timeout
        with self.cond:
            while True:
                for i in range(since, len(self.events)):
                    if self.events[i][1] == name:
                        return i, self.events[i]
                left = end - time.monotonic()
                if left <= 0:
                    return None, None
                self.cond.wait(left)

    def send(self, data):
        self.sock.sendall(data)

    def key(self, code, down):            self.send(frame(1, bytes([code, 1 if down else 0])))
    def mouse_move(self, dx, dy):         self.send(frame(2, struct.pack("<hh", dx, dy)))
    def mouse_abs(self, x, y):            self.send(frame(3, struct.pack("<hh", x, y)))
    def button(self, number, down):       self.send(frame(4, bytes([number, 1 if down else 0])))
    def set_relative(self, on):           self.send(frame(5, bytes([1 if on else 0])))
    def pointer_wanted(self, on):         self.send(frame(6, bytes([1 if on else 0])))
    def close(self):                      self.sock.close()
