#!/usr/bin/env python3
"""Build and run the SheepForce Metal accelerator harnesses (headless, needs a Metal device).

  python3 tools/run_sheepforce_tests.py            # QuickDraw harness
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "SheepShaver/src"
MAC = SRC / "MacOSX"

INCLUDES = [MAC / "config", SRC / "Unix", MAC / "Launcher", SRC / "kpx_cpu/src",
            SRC / "kpx_cpu/include", SRC / "include", SRC / "CrossPlatform", SRC]
FLAGS = ["-std=gnu++17", "-O1", "-g", "-DHAVE_CONFIG_H", "-D_GNU_SOURCE=1", "-DNW_BOOT_LOG=0",
         "-Wno-deprecated-declarations"] + ["-I%s" % p for p in INCLUDES]
FRAMEWORKS = ["-framework", "Metal", "-framework", "Cocoa", "-framework", "QuartzCore"]


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    return subprocess.run([str(c) for c in cmd], **kw)


def main():
    work = Path(tempfile.mkdtemp(prefix="sheepforce-tests-"))
    common = []
    for name, extra in (("MacOSX/sheepforce_metal.mm", ["-fobjc-arc"]),
                        ("MacOSX/sheepforce_rave.mm", ["-fobjc-arc"]),
                        ("sheepforce.cpp", [])):
        src = SRC / name
        obj = work / (src.stem + ".o")
        if run(["clang++", *FLAGS, *extra, "-c", src, "-o", obj]).returncode:
            return 1
        common.append(obj)
    status = 0
    for harness, extra_srcs in (("MacOSX/tests/sheepforce_qd_harness.mm", []),
                                ("MacOSX/tests/sheepforce_rave_harness.mm", ["rave.cpp"])):
        objs = list(common)
        for name in [harness] + extra_srcs:
            src = SRC / name
            obj = work / (src.stem + "_t.o")
            flags = ["-DSHEEPFORCE_RAVE_HARNESS"] if "rave" in name else []
            if run(["clang++", *FLAGS, *flags, "-c", src, "-o", obj]).returncode:
                return 1
            objs.append(obj)
        exe = work / Path(harness).stem
        if run(["clang++", *objs, *FRAMEWORKS, "-o", exe]).returncode:
            return 1
        if run([exe]).returncode:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
