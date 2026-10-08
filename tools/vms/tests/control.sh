#!/bin/bash
# The control socket of a running VM, end to end: status, screenshot (a valid PNG of the guest picture), pointer
# movement and clicks (the Apple menu opens while the button is held), the keyboard (Cmd-F opens the Finder's Find
# window, typed text appears in it), error handling, and finally a clean shutdown through the socket.
# Boots a copy of your Mac OS 9 disk and opens a SheepShaver window; nothing on your Mac is clicked or typed.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test starts a SheepShaver VM and drives it through its control socket."

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; [ -n "${SHEARS_KEEP_LOGS:-}" ] && cp "$WORK/log" "$SHEARS_KEEP_LOGS/control.log" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/VMs/ctl"
shears_prepare "$WORK/VMs/ctl" SheepShears.bin
NW_VERBOSE=1 "$SHEEPSHAVER_APP" --config "$WORK/VMs/ctl/prefs" --vm-id ctl --vm-dir "$WORK/VMs/ctl" > "$WORK/log" 2>&1 &
P=$!; disown

cat > "$WORK/drive.py" <<'PY'
import base64, sys, time
sys.path.insert(0, sys.argv[1])
from vmctl import Control, decode_png
ok = True
def check(name, cond, extra=""):
    global ok
    print(("PASS  " if cond else "FAIL  ") + name + (f"  ({extra})" if extra and not cond else ""))
    ok &= bool(cond)
def shot(c, **a):
    r = c.call("screenshot", **a)
    return decode_png(base64.b64decode(r["png_base64"])), r
def differing(a, b, x0, y0, x1, y1):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if a[y][x] != b[y][x])

deadline = time.time() + 300
c = None
while time.time() < deadline:
    try:
        c = Control("ctl"); st = c.call("status")
        if st["sheep_shears"]["running"]: break
        c.close(); c = None
    except Exception:
        c = None
    time.sleep(1)
check("the VM's control socket is up and the Sheep Shears tool is running", c is not None)
if c is None: sys.exit(1)
time.sleep(5)       # let the Finder finish drawing the desktop
st = c.call("status")
check("status reports the guest screen", (st["width"], st["height"], st["depth"]) == (1024, 768, 32), st)
check("status has id, state and cursor", st["id"] == "ctl" and st["state"] == "running" and "x" in st["cursor"], st)
check("ping", c.call("ping") == {"pong": True})

(w, h, base), r = shot(c)
check("screenshot is a PNG of the whole guest screen", (w, h) == (1024, 768) and (r["guest_width"], r["guest_height"]) == (1024, 768), (w, h))
colors = {px for row in base[::16] for px in row[::16]}
check("the screenshot is a real picture (many colours)", len(colors) > 20, len(colors))
check("the menu bar is light and the desktop is not (guest pixels, not the window)", base[8][400] != base[400][400])
(w2, h2, _), r2 = shot(c, max_width=256)
check("max_width scales the screenshot, aspect kept", (w2, h2) == (256, 192), (w2, h2))

for (x, y) in [(300, 200), (800, 600), (5, 5), (1000, 700), (512, 384)]:
    t = time.time(); c.call("mouse_move", x=x, y=y); dt = time.time() - t
    cur = c.call("status")["cursor"]
    check(f"mouse_move to {x},{y} lands on it ({dt:.1f} s)", abs(cur["x"] - x) <= 1 and abs(cur["y"] - y) <= 1, cur)

# hold the button on the Apple menu: the menu opens; release away from it: it closes
c.call("mouse_move", x=14, y=8)
c.call("mouse_down")
time.sleep(0.6)
(_, _, menu), _ = shot(c)
check("holding the button on the Apple menu opens it", differing(base, menu, 0, 20, 220, 200) > 500, differing(base, menu, 0, 20, 220, 200))
c.call("mouse_move", x=600, y=500)
c.call("mouse_up")
time.sleep(0.6)
(_, _, closed), _ = shot(c)
check("releasing elsewhere closes it again", differing(base, closed, 0, 20, 220, 200) < 50, differing(base, closed, 0, 20, 220, 200))

# keyboard: Cmd-F opens Find; typing shows up in it
c.call("press_key", key="f", modifiers=["command"])
time.sleep(2.5)
(_, _, find), _ = shot(c)
changed = differing(closed, find, 100, 100, 900, 600)
check("Command-F opens the Finder's Find window", changed > 3000, changed)
c.call("type_text", text="Sheep Shears")
time.sleep(1)
(_, _, typed), _ = shot(c)
check("typed text appears in the window", differing(find, typed, 100, 100, 900, 600) > 100, differing(find, typed, 100, 100, 900, 600))
c.call("press_key", key="w", modifiers=["command"])
time.sleep(1)

# errors
for bad, why in [(("mouse_move", dict(x=-5, y=1)), "range"), (("type_text", dict(text="café")), "untypeable"), (("format_disk", {}), "unknown op")]:
    try:
        c.call(bad[0], **bad[1]); check(f"bad request is refused ({why})", False)
    except RuntimeError as e:
        check(f"bad request is refused ({why})", True)
raw = c.raw("this is not json")
check("garbage gets an error reply and the connection stays usable", raw["ok"] is False and c.call("ping")["pong"])

# clean shutdown through the socket
r = c.call("shutdown")
check("shutdown asks the guest to shut down cleanly", r["method"] == "clean", r)
print("DONE" if ok else "FAILED")
sys.exit(0 if ok else 1)
PY
python3 "$WORK/drive.py" "$ROOT/tools/vms"; rc=$?
i=0; while [ $i -lt 360 ] && kill -0 $P 2>/dev/null; do sleep 0.5; i=$((i+1)); done
! kill -0 $P 2>/dev/null; shut=$?
if [ $shut -eq 0 ]; then echo "PASS  the VM process ended by itself after the clean shutdown"; else echo "FAIL  the VM process ended by itself after the clean shutdown"; rc=1; fi
[ ! -e "/tmp/sheepshaver-$(id -u)/ctl.sock" ]; if [ $? -eq 0 ]; then echo "PASS  the control socket was removed"; else echo "FAIL  the control socket was removed"; rc=1; fi
[ $rc -eq 0 ] && echo "control test: passed" || echo "control test: FAILED"
exit $rc
