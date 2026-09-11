#!/usr/bin/env python3
"""Golden reference capture: boot Mac OS 9.2.1 in QEMU mac99 with the nwgolden
plugin, drive the Finder to launch `Mac OS Install`, and record phase markers.

    python3 capture.py --out /path/to/out [--toast ...] [--until welcome|desktop]

Writes into --out:
    events.txt      nwgolden event stream (X exceptions, A A-line dispatches, T ticks)
    serial.log      OpenBIOS / guest serial
    shot-NNNN.png   periodic screenshots
    markers.tsv     elapsed_s  shot  events_bytes  phase
    phases.json     phase -> events.txt byte offset (boot, desktop, launch, welcome)

Phase detection is by screenshot stability: the desktop is reached when the
framebuffer stops changing after the boot animation; the installer Welcome
window is reached when it stops changing after Cmd+O.
"""
import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qmp import qmp  # noqa: E402

DEFAULT_TOAST = os.path.expanduser("~/Downloads/Mac OS 9.2.1.toast")
QCODE = {" ": "spc", ".": "dot", "-": "minus", "/": "slash"}


def key(sock, names, hold=0.08):
    ev = [{"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": n}}} for n in names]
    qmp(sock, "input-send-event", {"events": ev})
    time.sleep(hold)
    ev = [{"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": n}}} for n in reversed(names)]
    qmp(sock, "input-send-event", {"events": ev})
    time.sleep(0.08)


def type_text(sock, text):
    for ch in text:
        if ch.isupper():
            key(sock, ["shift", ch.lower()])
        else:
            key(sock, [QCODE.get(ch, ch)])


def screendump(sock, path_ppm):
    qmp(sock, "screendump", {"filename": path_ppm})
    for _ in range(50):
        if os.path.exists(path_ppm) and os.path.getsize(path_ppm) > 0:
            break
        time.sleep(0.1)
    with open(path_ppm, "rb") as f:
        return f.read()


def to_png(ppm, png):
    subprocess.run(["sips", "-s", "format", "png", ppm, "--out", png],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if os.path.exists(png):
        os.remove(ppm)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--toast", default=DEFAULT_TOAST)
    ap.add_argument("--plugin", default=os.path.join(HERE, "libnwgolden.dylib"))
    ap.add_argument("--until", choices=["desktop", "welcome"], default="welcome")
    ap.add_argument("--interval", type=float, default=5.0, help="screenshot period (s)")
    ap.add_argument("--stable", type=int, default=4, help="identical shots to call a phase stable")
    ap.add_argument("--min-boot", type=float, default=60.0, help="ignore stability before this (s)")
    ap.add_argument("--timeout", type=float, default=1200.0)
    ap.add_argument("--no-int", action="store_true", help="plugin int=0 (drop interrupt discontinuities)")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    sock = os.path.join(out, "qmp.sock")
    events = os.path.join(out, "events.txt")
    for f in (sock, events):
        if os.path.exists(f):
            os.remove(f)

    plugin_arg = f"{args.plugin},out={events}" + (",int=0" if args.no_int else "")
    cmd = [
        "qemu-system-ppc", "-M", "mac99,via=pmu", "-m", "512", "-cpu", "g4",
        "-drive", f"file={args.toast},format=raw,media=cdrom,if=ide,index=1",
        "-boot", "d", "-display", "none", "-rtc", "base=localtime",
        "-serial", f"file:{os.path.join(out, 'serial.log')}",
        "-qmp", f"unix:{sock},server,nowait",
        "-plugin", plugin_arg,
    ]
    with open(os.path.join(out, "qemu-cmd.txt"), "w") as f:
        f.write(" ".join(repr(c) if " " in c else c for c in cmd) + "\n")
    qemu = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                            stdout=open(os.path.join(out, "qemu-stderr.log"), "w"),
                            stderr=subprocess.STDOUT)
    t0 = time.time()
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)

    markers = open(os.path.join(out, "markers.tsv"), "w")
    markers.write("elapsed_s\tshot\tevents_bytes\tphase\n")
    phases = {}
    phase = "boot"
    shot_n = 0
    prev = None
    same = 0
    launched_at = None

    def mark(name):
        nonlocal phase
        phase = name
        phases[name] = {"elapsed_s": round(time.time() - t0, 1),
                        "events_bytes": os.path.getsize(events) if os.path.exists(events) else 0,
                        "shot": shot_n}
        print(f"[{phases[name]['elapsed_s']:7.1f}s] phase {name} @ events+{phases[name]['events_bytes']}", flush=True)

    try:
        while time.time() - t0 < args.timeout and qemu.poll() is None:
            time.sleep(args.interval)
            shot_n += 1
            ppm = os.path.join(out, f"shot-{shot_n:04d}.ppm")
            png = os.path.join(out, f"shot-{shot_n:04d}.png")
            try:
                data = screendump(sock, ppm)
            except Exception as e:  # noqa: BLE001
                print(f"screendump failed: {e}", file=sys.stderr)
                continue
            ebytes = os.path.getsize(events) if os.path.exists(events) else 0
            elapsed = time.time() - t0
            markers.write(f"{elapsed:.1f}\t{shot_n:04d}\t{ebytes}\t{phase}\n")
            markers.flush()
            if data == prev:
                same += 1
            else:
                same = 0
            prev = data
            to_png(ppm, png)

            if phase == "boot" and elapsed >= args.min_boot and same >= args.stable:
                mark("desktop")
                if args.until == "desktop":
                    break
                # Finder: CD window is frontmost; select "Mac OS Install" by name, Cmd+O.
                type_text(sock, "Mac OS Install")
                time.sleep(0.5)
                key(sock, ["meta_l", "o"], hold=0.15)
                launched_at = time.time()
                same = 0
                mark("launch")
            elif phase == "launch" and launched_at and time.time() - launched_at > 15 and same >= args.stable:
                mark("welcome")
                break
    finally:
        markers.close()
        try:
            qmp(sock, "quit")
        except Exception:  # noqa: BLE001
            qemu.terminate()
        qemu.wait(timeout=30)
        phases["end"] = {"elapsed_s": round(time.time() - t0, 1),
                         "events_bytes": os.path.getsize(events) if os.path.exists(events) else 0,
                         "shot": shot_n}
        with open(os.path.join(out, "phases.json"), "w") as f:
            json.dump(phases, f, indent=2)
        print(json.dumps(phases, indent=2))


if __name__ == "__main__":
    main()
