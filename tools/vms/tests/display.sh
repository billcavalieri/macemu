#!/bin/bash
# A VM started with --embedded has no window: its picture goes to shared memory and its keyboard and mouse come in over
# the display socket. A test viewer (tools/vms/displayclient.py) maps the frames, keeps the viewer heartbeat going and
# checks: the VM draws nothing while nobody looks and about 60 frames a second while someone does; the frame equals the
# control socket's screenshot; the display socket replays the guest's state and its input moves the pointer, opens the
# Apple menu under a held button and types into the Finder's Find window; frames stop when the heartbeat stops; and the
# shared memory and sockets are gone after a clean shutdown. Nothing on your Mac is clicked or typed.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test starts an embedded SheepShaver VM (no window) and drives it through its display socket."

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; [ -n "${SHEARS_KEEP_LOGS:-}" ] && cp "$WORK/log" "$SHEARS_KEEP_LOGS/display.log" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/VMs/dsp"
shears_prepare "$WORK/VMs/dsp" SheepShears.bin
NW_VERBOSE=1 "$SHEEPSHAVER_APP" --config "$WORK/VMs/dsp/prefs" --vm-id dsp --vm-dir "$WORK/VMs/dsp" --embedded --background > "$WORK/log" 2>&1 &
P=$!; disown

cat > "$WORK/drive.py" <<'PY'
import base64, os, struct, sys, time
sys.path.insert(0, sys.argv[1])
from vmctl import Control, decode_png
import displayclient as dc
ok = True
def check(name, cond, extra=""):
    global ok
    print(("PASS  " if cond else "FAIL  ") + name + (f"  ({extra})" if extra and not cond else ""))
    ok &= bool(cond)
def screenshot(c):
    r = c.call("screenshot")
    return decode_png(base64.b64decode(r["png_base64"]))[2]
def differing(a, b, x0, y0, x1, y1):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if a[y][x] != b[y][x])
def sampled_difference(shm_a, shm_b, x0, y0, x1, y1):
    return sum(1 for (x, y), px in shm_a.items() if x0 <= x < x1 and y0 <= y < y1 and shm_b.get((x, y)) != px)

deadline = time.time() + 300
c = None
while time.time() < deadline:
    try:
        c = Control("dsp"); st = c.call("status")
        if st["sheep_shears"]["running"]: break
        c.close(); c = None
    except Exception:
        c = None
    time.sleep(1)
check("the VM's control socket is up and the Sheep Shears tool is running", c is not None)
if c is None: sys.exit(1)
time.sleep(5)       # let the Finder finish drawing the desktop
shm = dc.Shm("dsp")
check("the shared memory is there, versioned, sized for 1024x768", shm.ready and shm.u32("version") == 1 and shm.u32("slots") == 3 and (shm.u32("maxWidth"), shm.u32("maxHeight")) == (1024, 768))
check("the VM process id is in the header", shm.u32("vmPid") > 0)

# nobody looking: nothing is drawn
before = shm.u64("frameCounter")
time.sleep(3)
check("with no viewer heartbeat the VM draws no frames", shm.u64("frameCounter") == before == 0, (before, shm.u64("frameCounter")))

# a viewer looks: about 60 frames a second
shm.start_heartbeat()
time.sleep(0.5)
n0, t0 = shm.u64("frameCounter"), time.time()
time.sleep(3)
n1, t1 = shm.u64("frameCounter"), time.time()
fps = (n1 - n0) / (t1 - t0)
check(f"with a viewer the VM publishes about 60 frames a second ({fps:.0f})", 40 <= fps <= 75, fps)
check("the picture size is in the header", (shm.u32("width"), shm.u32("height")) == (1024, 768) and shm.u32("bytesPerRow") >= 4096 and shm.u32("generation") >= 1)

# the frame equals the control socket's screenshot
shot = screenshot(c)
view = shm.sample(step=4)
mismatch = sum(1 for (x, y), px in view.items() if shot[y][x] != px)
check(f"the shared frame equals the control socket's screenshot ({mismatch} of {len(view)} sampled pixels differ)", mismatch <= len(view) // 200, mismatch)
colors = {px for px in list(view.values())[::17]}
check("the frame is a real picture (many colours)", len(colors) > 20, len(colors))

# the display socket: state replay, then input
link = dc.DisplayLink("dsp")
_, mode = link.wait_event("guestMode")
check("a viewer that connects is told the guest mode", mode is not None and struct.unpack("<HHBI", mode[2][:9])[:3] == (1024, 768, 32), mode)
_, cur = link.wait_event("cursor")
check("…and the guest cursor (68 bytes)", cur is not None and len(cur[2]) == 68, cur)
_, hides = link.wait_event("cursorHidesHost")
check("…and whether the guest draws its own cursor", hides is not None and hides[2] in (b"\x00", b"\x01"), hides)
_, shears = link.wait_event("shearsStatus")
check("…and that the Sheep Shears tool is running", shears is not None and shears[2][0] == 1, shears)

base = screenshot(c)
for (x, y) in [(300, 200), (800, 600), (5, 5), (1000, 700), (512, 384)]:
    link.mouse_abs(x, y)
    time.sleep(0.25)
    cur = c.call("status")["cursor"]
    check(f"mouse_abs to {x},{y} lands on it", abs(cur["x"] - x) <= 1 and abs(cur["y"] - y) <= 1, cur)
link.mouse_abs(14, 8); time.sleep(0.3)
link.button(0, True); time.sleep(0.8)
menu = screenshot(c)
check("holding the button on the Apple menu opens it", differing(base, menu, 0, 20, 220, 200) > 500, differing(base, menu, 0, 20, 220, 200))
link.mouse_move(400, 300); link.mouse_move(200, 200); time.sleep(0.3)
link.button(0, False); time.sleep(0.8)
closed = screenshot(c)
check("releasing away from it closes it again", differing(base, closed, 0, 20, 220, 200) < 50, differing(base, closed, 0, 20, 220, 200))
cursor_after = c.call("status")["cursor"]
check("relative moves move the pointer", (cursor_after["x"], cursor_after["y"]) != (14, 8), cursor_after)

ADB = {"command": 0x37, "f": 0x03, "w": 0x0d, "h": 0x04, "e": 0x0e, "l": 0x25, "o": 0x1f}
link.key(ADB["command"], True); link.key(ADB["f"], True); link.key(ADB["f"], False); link.key(ADB["command"], False)
time.sleep(2.5)
find = screenshot(c)
check("Command-F, sent as key messages, opens the Finder's Find window", differing(closed, find, 100, 100, 900, 600) > 3000, differing(closed, find, 100, 100, 900, 600))
for ch in "hello":
    link.key(ADB[ch], True); link.key(ADB[ch], False)
time.sleep(1)
typed = screenshot(c)
check("typed key messages appear in the window", differing(find, typed, 100, 100, 900, 600) > 100, differing(find, typed, 100, 100, 900, 600))
link.key(ADB["command"], True); link.key(ADB["w"], True); link.key(ADB["w"], False); link.key(ADB["command"], False)
time.sleep(1)

# damaged and unknown messages are ignored; the link stays usable
link.send(bytes([2, 99, 1]))
link.mouse_abs(100, 100); time.sleep(0.3)
cur = c.call("status")["cursor"]
check("an unknown message is ignored and the link keeps working", abs(cur["x"] - 100) <= 1 and abs(cur["y"] - 100) <= 1, cur)

# frames stop when the viewer stops looking
shm.stop_heartbeat()
time.sleep(0.6)
a = shm.u64("frameCounter"); time.sleep(1.5); b = shm.u64("frameCounter")
check("without the heartbeat the frames stop", b - a <= 1, (a, b))
shm.start_heartbeat(); time.sleep(0.5)
a = shm.u64("frameCounter"); time.sleep(1); b = shm.u64("frameCounter")
check("…and resume with it", b - a > 30, (a, b))

# shutdown: process, sockets and shared memory all go
link.close()
r = c.call("shutdown")
check("shutdown asks the guest to shut down cleanly", r["method"] == "clean", r)
shm.stop_heartbeat()
print("DONE" if ok else "FAILED")
sys.exit(0 if ok else 1)
PY
python3 "$WORK/drive.py" "$ROOT/tools/vms"; rc=$?
grep -aq "no window, scanout into shared memory" "$WORK/log"; if [ $? -eq 0 ]; then echo "PASS  the VM started without a window (scanout into shared memory)"; else echo "FAIL  the VM started without a window"; rc=1; fi
i=0; while [ $i -lt 360 ] && kill -0 $P 2>/dev/null; do sleep 0.5; i=$((i+1)); done
! kill -0 $P 2>/dev/null; if [ $? -eq 0 ]; then echo "PASS  the VM process ended by itself after the clean shutdown"; else echo "FAIL  the VM process ended by itself after the clean shutdown"; rc=1; fi
U="$(id -u)"
[ ! -e "/tmp/sheepshaver-$U/dsp.disp" ] && [ ! -e "/tmp/sheepshaver-$U/dsp.sock" ]; if [ $? -eq 0 ]; then echo "PASS  the display and control sockets were removed"; else echo "FAIL  the sockets were removed"; rc=1; fi
python3 -I - <<PY
import _posixshmem, os, sys
try:
    _posixshmem.shm_open("/sheep.$U.dsp", os.O_RDWR, 0o600)
    print("FAIL  the shared memory was removed"); sys.exit(1)
except FileNotFoundError:
    print("PASS  the shared memory was removed")
PY
[ $? -eq 0 ] || rc=1
[ $rc -eq 0 ] && echo "display test: passed" || echo "display test: FAILED"
exit $rc
