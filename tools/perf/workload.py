#!/usr/bin/env python3
"""A repeatable guest workload for measuring the JIT, driven only through the VM's own control socket (nothing is typed or
clicked on your Mac).

    tools/perf/workload.py OUTDIR [--app PATH] [--disk PATH] [--env K=V ...] [--sample] [--no-profile] [--label NAME]

Boots a clone of the Mac OS 9 test disk and measures three phases, each reported as host CPU seconds (from `ps`) and, with
the profiler on, as a profile dump of its own:

  boot   power-on to the QuickDraw hooks line (Mac OS 9 starting up: mostly 68k code and Open Transport)
  gui    the same scripted Finder activity every time: menus, new windows, dragging, text entry, window close (Toolbox, QuickDraw)
  idle   the desktop doing nothing for 20 s (timers, interrupts, the idle loop)

Outputs in OUTDIR: <label>.boot.prof, .gui.prof, .idle.prof (when profiling), <label>.sample.<phase>.txt (with --sample),
<label>.json (the CPU numbers) and <label>.log (the emulator's log). Compare runs with `compare.py`.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/vms"))
from vmctl import Control  # noqa: E402


def cpu_seconds(pid):
    out = subprocess.run(["ps", "-o", "cputime=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    if not out:
        return None
    # [dd-]hh:mm:ss.cc or mm:ss.cc
    days = 0
    if "-" in out:
        d, out = out.split("-", 1)
        days = int(d)
    parts = [float(x) for x in out.split(":")]
    while len(parts) < 3:
        parts.insert(0, 0.0)
    return days * 86400 + parts[0] * 3600 + parts[1] * 60 + parts[2]


def default_app():
    return str(next(iter(sorted(Path.home().glob("Library/Developer/Xcode/DerivedData/SheepShaver-*/Build/Products/Release/SheepShaver.app/Contents/MacOS/SheepShaver"),
                                key=lambda p: -p.stat().st_mtime)), ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--app", default=None)
    ap.add_argument("--disk", default=str(Path.home() / "Library/Application Support/SheepShaver/os921/macos921-blank.hfv"))
    ap.add_argument("--cd", default=str(Path.home() / "Downloads/Mac OS 9.2.1.toast"))
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--sample", action="store_true", help="also run /usr/bin/sample on the VM during each phase")
    ap.add_argument("--no-profile", action="store_true", help="leave the profiler off (pure speed measurement)")
    ap.add_argument("--label", default="run")
    ap.add_argument("--idle", type=int, default=20)
    ap.add_argument("--gui-rounds", type=int, default=6)
    args = ap.parse_args()
    app = args.app or default_app()
    args.outdir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="perfwl-"))
    vm = work / "VMs" / "perf"
    vm.mkdir(parents=True)
    shutil.copy2  # (cp -c below keeps the clone cheap)
    subprocess.run(["cp", "-c", args.disk, str(vm / "disk.hfv")], check=True)
    share = vm / "share"
    share.mkdir()
    prefs = f"""disk {vm}/disk.hfv
cdrom {args.cd}
extfs {share}
screen win/1024/768
windowmodes 0
screenmodes 0
seriala /dev/null
serialb /dev/null
rom
bootdrive 0
bootdriver 0
ramsize 536870912
frameskip 1
gfxaccel true
sheepforce true
qtcodec true
nocdrom false
nonet false
nosound false
nogui true
ignoresegv true
ignoreillegal true
jit true
keyboardtype 5
idlewait true
"""
    (vm / "prefs").write_text(prefs)
    env = dict(os.environ, NW_VERBOSE="1")
    prof = args.outdir / f"{args.label}.prof"
    if not args.no_profile:
        env["NW_JIT_PROFILE"] = str(prof)
    for kv in args.env:
        k, v = kv.split("=", 1)
        env[k] = v
    log = open(args.outdir / f"{args.label}.log", "wb")
    p = subprocess.Popen([app, "--config", str(vm / "prefs"), "--vm-id", "perf", "--vm-dir", str(vm)], env=env, stdout=log, stderr=subprocess.STDOUT)
    results = {"label": args.label, "app": app, "env": args.env}

    def dump(phase):
        if args.no_profile:
            return
        now = Path(str(prof) + ".now")
        now.write_text("")
        for _ in range(60):
            if not now.exists():
                break
            time.sleep(0.25)
        time.sleep(0.5)
        if prof.exists():
            shutil.copy(prof, args.outdir / f"{args.label}.{phase}.prof")

    def reset():
        if not args.no_profile:
            Path(str(prof) + ".reset").write_text("")
            time.sleep(2.5)

    def sampled(phase, seconds):
        if not args.sample:
            return None
        return subprocess.Popen(["/usr/bin/sample", str(p.pid), str(seconds), "-file", str(args.outdir / f"{args.label}.sample.{phase}.txt")],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        t0 = time.time()
        logtxt = args.outdir / f"{args.label}.log"
        boot_sample = None
        if args.sample:
            time.sleep(3)
            boot_sample = sampled("boot", 25)
        while time.time() - t0 < 300:
            if p.poll() is not None:
                raise SystemExit("the emulator quit during boot")
            if b"QuickDraw hooks installed" in logtxt.read_bytes():
                break
            time.sleep(0.25)
        else:
            raise SystemExit("boot did not finish in 300 s")
        results["boot_wall"] = time.time() - t0
        results["boot_cpu"] = cpu_seconds(p.pid)
        dump("boot")
        if boot_sample:
            boot_sample.wait()

        c = None
        for _ in range(120):
            try:
                c = Control("perf")
                c.call("ping")
                break
            except Exception:
                c = None
                time.sleep(0.5)
        if c is None:
            raise SystemExit("no control socket")
        time.sleep(15)   # let the Finder finish drawing and the disk activity settle
        reset()

        # ---- gui phase
        g0, c0 = time.time(), cpu_seconds(p.pid)
        s = sampled("gui", 30)
        for r in range(args.gui_rounds):
            # Apple menu held open, then released over the desktop
            c.call("mouse_move", x=12, y=8)
            c.call("mouse_down")
            for y in (30, 60, 90, 60, 20):
                c.call("mouse_move", x=40, y=y)
            c.call("mouse_up")
            time.sleep(0.4)
            # a new Finder window, dragged around, text typed into the Find window, windows closed
            c.call("press_key", key="n", modifiers=["command"])
            time.sleep(0.8)
            c.call("mouse_drag", from_x=300, from_y=90, to_x=520, to_y=300)
            c.call("mouse_drag", from_x=520, from_y=300, to_x=260, to_y=200)
            c.call("press_key", key="f", modifiers=["command"])
            time.sleep(1.0)
            c.call("type_text", text="performance workload text " * 3)
            time.sleep(0.5)
            c.call("press_key", key="w", modifiers=["command"])
            c.call("press_key", key="w", modifiers=["command"])
            # rubber-band over the desktop, then an icon double-click and its window closed again
            c.call("mouse_drag", from_x=600, from_y=500, to_x=980, to_y=120)
            c.call("mouse_click", x=980, y=60, count=2)
            time.sleep(1.2)
            c.call("press_key", key="w", modifiers=["command"])
            time.sleep(0.4)
            c.call("mouse_click", x=700, y=600)
        results["gui_wall"] = time.time() - g0
        results["gui_cpu"] = cpu_seconds(p.pid) - c0
        dump("gui")
        if s:
            s.wait()
        # screenshot so a human can see the end state
        try:
            import base64
            (args.outdir / f"{args.label}.gui.png").write_bytes(base64.b64decode(c.call("screenshot", max_width=640)["png_base64"]))
        except Exception:
            pass

        # ---- idle phase
        time.sleep(2)
        reset()
        i0, ic0 = time.time(), cpu_seconds(p.pid)
        s = sampled("idle", args.idle)
        time.sleep(args.idle)
        results["idle_wall"] = time.time() - i0
        results["idle_cpu"] = cpu_seconds(p.pid) - ic0
        dump("idle")
        if s:
            s.wait()
    finally:
        p.terminate()
        try:
            p.wait(10)
        except Exception:
            p.kill()
        shutil.rmtree(work, ignore_errors=True)
    (args.outdir / f"{args.label}.json").write_text(json.dumps(results, indent=1))
    print(json.dumps(results))


if __name__ == "__main__":
    main()
