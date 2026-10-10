#!/bin/bash
# A VM shown INSIDE the library window. The library starts a VM embedded (no window of its own), connects to its display
# socket and draws its picture from shared memory. Checked, with nothing clicked or typed:
#   - the library window really shows the guest desktop (a window-only screenshot of it, never the rest of the screen);
#   - input from the library window reaches the guest (a diagnostics hook moves the guest pointer through the window's link);
#   - the library notices when the VM ends;
#   - a VM that is already running (embedded) when the library opens is picked up and shown;
#   - a guest restart (which re-executes the VM process) is followed: the library connects again and shows the new boot.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test starts the SheepShaver library window and embedded VMs in it."

WORK="$(mktemp -d)"
M=""; V=""
cleanup() { [ -n "$M" ] && kill "$M" 2>/dev/null; [ -n "$V" ] && kill "$V" 2>/dev/null; sleep 1; [ -n "$M" ] && kill -9 "$M" 2>/dev/null; [ -n "$V" ] && kill -9 "$V" 2>/dev/null; pkill -f "$WORK/" 2>/dev/null; [ -n "${SHEARS_KEEP_LOGS:-}" ] && cp "$WORK"/*.log "$WORK"/logs/* "$SHEARS_KEEP_LOGS"/ 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/lib" "$WORK/logs" "$WORK/src" "$WORK/adopt"
swiftc -O -o "$WORK/winid" "$(dirname "$0")/winid.swift" || exit 1
shears_prepare "$WORK/src" SheepShears.bin
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }

# ---- A: the library starts a VM, embedded, and shows it
NW_VERBOSE=1 NW_VM_LIBRARY="$WORK/lib" NW_MANAGER_ADD="$WORK/src/prefs" NW_MANAGER_START=1 NW_MANAGER_POKE=300,200 NW_MANAGER_LOGDIR="$WORK/logs" \
    "$SHEEPSHAVER_APP" > "$WORK/manager.log" 2>&1 &
M=$!; disown
i=0; while [ $i -lt 60 ] && [ -z "$(ls "$WORK/lib" 2>/dev/null)" ]; do sleep 0.5; i=$((i+1)); done
ID="$(ls "$WORK/lib" | head -1)"
cat > "$WORK/driveA.py" <<'PY'
import subprocess, sys, time
sys.path.insert(0, sys.argv[1])
from vmctl import Control, decode_png
vm_id, winid, mgr_pid, out = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
ok = True
def check(name, cond, extra=""):
    global ok
    print(("PASS  " if cond else "FAIL  ") + name + (f"  ({extra})" if extra and not cond else "")); ok &= bool(cond)
deadline = time.time() + 300; c = None
while time.time() < deadline:
    try:
        c = Control(vm_id); st = c.call("status")
        if st["sheep_shears"]["running"]: break
        c.close(); c = None
    except Exception:
        c = None
    time.sleep(1)
check("the embedded VM started by the library is running with the Sheep Shears tool", c is not None)
if c is None: sys.exit(1)
time.sleep(8)
# the library window's picture: a window-only capture, shrunk, then decoded
wid = subprocess.run([winid, mgr_pid, "SheepShaver"], capture_output=True, text=True).stdout.strip()
check("the library window exists", wid != "")
subprocess.run(["screencapture", "-l", wid, "-o", "-t", "png", out + ".full.png"], check=True)
subprocess.run(["sips", "-Z", "700", out + ".full.png", "--out", out + ".png"], capture_output=True, check=True)
w, h, rows = decode_png(open(out + ".png", "rb").read())
# the guest desktop is a purple-blue; sample the middle of the window right of the sidebar
purple = total = 0
for y in range(int(h * 0.35), int(h * 0.9), 3):
    for x in range(int(w * 0.40), int(w * 0.95), 3):
        r, g, b = rows[y][x]; total += 1
        if abs(r - 102) < 40 and abs(g - 102) < 40 and abs(b - 153) < 40: purple += 1
check(f"the library window shows the guest desktop ({100 * purple // max(total, 1)}% of the area is the desktop colour)", purple > total * 0.30, (purple, total))
# input from the window's link reached the guest (the hook moves the pointer 20 s after the display connects)
deadline = time.time() + 40
cur = c.call("status")["cursor"]
while time.time() < deadline and not (abs(cur["x"] - 300) <= 1 and abs(cur["y"] - 200) <= 1):
    time.sleep(1); cur = c.call("status")["cursor"]
check("input sent through the library window's link moved the guest pointer", abs(cur["x"] - 300) <= 1 and abs(cur["y"] - 200) <= 1, cur)
# Detach: the VM opens its own window (a window-only capture of the VM's process); Attach: back into the library window
def desktop_share(window, shrunk):
    subprocess.run(["screencapture", "-l", window, "-o", "-t", "png", shrunk + ".full.png"], check=True)
    subprocess.run(["sips", "-Z", "700", shrunk + ".full.png", "--out", shrunk + ".png"], capture_output=True, check=True)
    w, h, rows = decode_png(open(shrunk + ".png", "rb").read())
    purple = total = 0
    for y in range(int(h * 0.30), int(h * 0.9), 3):
        for x in range(int(w * 0.30), int(w * 0.95), 3):
            r, g, b = rows[y][x]; total += 1
            if abs(r - 102) < 40 and abs(g - 102) < 40 and abs(b - 153) < 40: purple += 1
    return purple / max(total, 1)
check("a VM started embedded reports its display as embedded", c.call("status")["display"] == "embedded")
vm_pid = subprocess.run(["pgrep", "-f", "--", "--vm-id " + vm_id], capture_output=True, text=True).stdout.split()[0]
check("…and has no window of its own", subprocess.run([winid, vm_pid, ""], capture_output=True, text=True).stdout.strip() == "")
r = c.call("display", mode="window")
time.sleep(4)
check("the display op detaches it", r["mode"] == "window" and c.call("status")["display"] == "window", r)
own = subprocess.run([winid, vm_pid, ""], capture_output=True, text=True).stdout.strip()
check("a detached VM has its own window", own != "", subprocess.run(["pgrep", "-fl", "--", "--vm-id " + vm_id], capture_output=True, text=True).stdout[:300])
if own:
    share = desktop_share(own, out + ".own")
    check(f"…that shows the guest desktop ({100 * share:.0f}% of it)", share > 0.30, share)
c.call("mouse_move", x=400, y=300)                       # the guest is alive and answering while detached
check("a detached VM still takes input", abs(c.call("status")["cursor"]["x"] - 400) <= 1)
r = c.call("display", mode="embedded")
time.sleep(4)
check("the display op attaches it again", c.call("status")["display"] == "embedded")
check("…and its own window is gone", subprocess.run([winid, vm_pid, ""], capture_output=True, text=True).stdout.strip() == "")
share = desktop_share(wid, out + ".lib")
check(f"…and the library window shows the guest again ({100 * share:.0f}% of it)", share > 0.30, share)
try:
    c.call("display", mode="fullscreen"); check("a bad display mode is refused", False)
except RuntimeError:
    check("a bad display mode is refused", True)

# the Guest menu's switches: read, change, read back, and put back (they are kept in the app's preferences)
f0 = c.call("features")
check("the features op reports the host and guest switches", set(f0["host"]) == {"edge_release", "clipboard"} and f0["tool_running"] is True, f0)
original = f0["host"]["clipboard"]
f1 = c.call("features", clipboard=not original)
check("…changes one", f1["host"]["clipboard"] == (not original) and f1["host"]["edge_release"] == f0["host"]["edge_release"], f1)
f2 = c.call("features", clipboard=original)
check("…and puts it back", f2["host"] == f0["host"], f2)
try:
    c.call("features", clipboard="yes"); check("a non-boolean switch is refused", False)
except RuntimeError:
    check("a non-boolean switch is refused", True)
c.call("shutdown")
print("DONE" if ok else "FAILED"); sys.exit(0 if ok else 1)
PY
python3 "$WORK/driveA.py" "$ROOT/tools/vms" "$ID" "$WORK/winid" "$M" "$WORK/library"; [ $? -eq 0 ] || fail=1
grep -aq "NW-MANAGER display connected $ID" "$WORK/manager.log"; check "the library connected to the VM's display socket" $?
i=0; while [ $i -lt 120 ] && ! grep -aq "NW-MANAGER display closed $ID" "$WORK/manager.log"; do sleep 1; i=$((i+1)); done
grep -aq "NW-MANAGER display closed $ID" "$WORK/manager.log"; check "the library noticed when the VM ended" $?
kill -0 $M 2>/dev/null; check "the library is still running after its VM ended" $?
kill $M 2>/dev/null; sleep 2; M=""

# ---- B: a VM that is already running is picked up when the library opens
mkdir -p "$WORK/adopt/lib/adoptvm"
cp "$WORK/src/prefs" "$WORK/adopt/lib/adoptvm/prefs"
printf '{"name":"Adopted"}' > "$WORK/adopt/lib/adoptvm/vm.json"
NW_VERBOSE=1 "$SHEEPSHAVER_APP" --config "$WORK/adopt/lib/adoptvm/prefs" --vm-id adoptvm --vm-dir "$WORK/adopt/lib/adoptvm" --embedded --background > "$WORK/adopt.log" 2>&1 &
V=$!; disown
i=0; while [ $i -lt 300 ] && [ ! -S "/tmp/sheepshaver-$(id -u)/adoptvm.disp" ]; do sleep 1; i=$((i+1)); done
NW_VERBOSE=1 NW_VM_LIBRARY="$WORK/adopt/lib" "$SHEEPSHAVER_APP" > "$WORK/manager2.log" 2>&1 &
M=$!; disown
i=0; while [ $i -lt 30 ] && ! grep -aq "NW-MANAGER display connected adoptvm" "$WORK/manager2.log"; do sleep 1; i=$((i+1)); done
grep -aq "NW-MANAGER display connected adoptvm" "$WORK/manager2.log"; check "a library that opens while an embedded VM is running picks it up" $?
python3 - "$ROOT/tools/vms" <<'PY'
import sys; sys.path.insert(0, sys.argv[1])
from vmctl import Control
try: Control("adoptvm").call("shutdown", force=True)
except Exception: pass
PY
i=0; while [ $i -lt 60 ] && kill -0 $V 2>/dev/null; do sleep 0.5; i=$((i+1)); done
kill $M 2>/dev/null; sleep 2; M=""

# ---- C: a guest restart (Special > Restart) re-executes the VM process. The library must connect again and show it
# (diagnostics hook NW_RESTART_AFTER restarts the guest 40 s after its first start, the way the PMU restart does)
mkdir -p "$WORK/restart/lib"
NW_VERBOSE=1 NW_VM_LIBRARY="$WORK/restart/lib" NW_MANAGER_ADD="$WORK/src/prefs" NW_MANAGER_START=1 NW_RESTART_AFTER=40 NW_MANAGER_LOGDIR="$WORK/restart" \
    "$SHEEPSHAVER_APP" > "$WORK/manager3.log" 2>&1 &
M=$!; disown
i=0; while [ $i -lt 60 ] && [ -z "$(ls "$WORK/restart/lib" 2>/dev/null)" ]; do sleep 0.5; i=$((i+1)); done
RID="$(ls "$WORK/restart/lib" | head -1)"
cat > "$WORK/driveC.py" <<'PY'
import subprocess, sys, time
sys.path.insert(0, sys.argv[1])
from vmctl import Control, decode_png
vm_id, winid, mgr_pid, out, log = sys.argv[2:7]
ok = True
def check(name, cond, extra=""):
    global ok
    print(("PASS  " if cond else "FAIL  ") + name + (f"  ({extra})" if extra and not cond else "")); ok &= bool(cond)
def count(text): return open(log, errors="replace").read().count(text)
def wait(cond, secs):
    end = time.time() + secs
    while time.time() < end:
        if cond(): return True
        time.sleep(1)
    return False
check("the library connected to the VM", wait(lambda: count(f"display connected {vm_id}") >= 1, 120))
check("the guest restarted (the library lost its connection)", wait(lambda: count(f"display closed {vm_id}") >= 1, 120))
check("the library connected again to the restarted VM", wait(lambda: count(f"display connected {vm_id}") >= 2, 60), count(f"display connected {vm_id}"))
c = None
def tool_up():
    global c
    try:
        c = Control(vm_id); return c.call("status")["sheep_shears"]["running"]
    except Exception:
        return False
check("the restarted VM's control socket answers and the Sheep Shears tool is running again", wait(tool_up, 200))
time.sleep(10)
wid = subprocess.run([winid, mgr_pid, "SheepShaver"], capture_output=True, text=True).stdout.strip()
subprocess.run(["screencapture", "-l", wid, "-o", "-t", "png", out + ".full.png"], check=True)
subprocess.run(["sips", "-Z", "700", out + ".full.png", "--out", out + ".png"], capture_output=True, check=True)
w, h, rows = decode_png(open(out + ".png", "rb").read())
purple = total = 0
for y in range(int(h * 0.35), int(h * 0.9), 3):
    for x in range(int(w * 0.40), int(w * 0.95), 3):
        r, g, b = rows[y][x]; total += 1
        if abs(r - 102) < 40 and abs(g - 102) < 40 and abs(b - 153) < 40: purple += 1
check(f"the library window shows the restarted guest's desktop ({100 * purple // max(total, 1)}%)", purple > total * 0.30, (purple, total))
c.call("shutdown", force=True)
print("DONE" if ok else "FAILED"); sys.exit(0 if ok else 1)
PY
python3 "$WORK/driveC.py" "$ROOT/tools/vms" "$RID" "$WORK/winid" "$M" "$WORK/restart/library" "$WORK/manager3.log"; [ $? -eq 0 ] || fail=1

[ $fail -eq 0 ] && echo "embedded test: passed" || echo "embedded test: FAILED"
exit $fail
