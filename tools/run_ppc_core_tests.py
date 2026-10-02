#!/usr/bin/env python3
"""Test the real KPX interpreter from a completed isolated Xcode app build.

Pass --build-log from xcodebuild -scheme SheepShaver (Debug or Release).
Reuses the build's flags and objects, replacing only the CPU clock reference.
The alternate entry point never launches AppKit or initializes guest media.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-log', type=Path, required=True)
parser.add_argument('--replay', type=Path, help='Replay a bounded live PPC verification capture instead of the core suite')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
lines = args.build_log.read_text().splitlines()
compile_command = next((shlex.split(line.strip()) for line in lines
                        if '/usr/bin/clang ' in line and ' -c ' in line and '/ppc-cpu.cpp ' in line), None)
link_command = next((shlex.split(line.strip()) for line in reversed(lines)
                     if '/usr/bin/clang++ ' in line and ' -filelist ' in line and ' -lkpx_cpu ' in line), None)
if not compile_command or not link_command:
    parser.error('Build log must contain CPU compilation and the final app link command')

# Remove output-only options so the private test build cannot alter app objects.
def remove_options(command, options):
    result = []; i = 0
    while i < len(command):
        if command[i] in options: i += options[command[i]] + 1
        else: result.append(command[i]); i += 1
    return result

with tempfile.TemporaryDirectory(prefix='macemu-ppc-core-') as directory:
    path = Path(directory)
    flags = remove_options(compile_command, {'-MMD':0, '-MT':1, '-MF':1, '--serialize-diagnostics':1, '-c':1, '-o':1})
    cpu_object = path / 'ppc-cpu.o'
    subprocess.run(flags + ['-DGetTicks_usec=ppc_test_ticks_usec', '-c', str(root / 'SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp'), '-o', str(cpu_object)], check=True, cwd=root / 'SheepShaver/src/MacOSX')
    test_object = path / 'ppc-tests.o'
    subprocess.run(flags + ['-c', str(root / 'SheepShaver/src/kpx_cpu/tests/ppc_core_harness.cpp'), '-o', str(test_object)], check=True, cwd=root / 'SheepShaver/src/MacOSX')
    command = remove_options(link_command, {'-o':1})
    # No app build outputs (LTO/dependency artifacts), nor app-only signing option.
    for option, count in [('-object_path_lto', 4), ('-dependency_info', 4), ('-no_adhoc_codesign', 2)]:
        i = 0
        while i < len(command):
            if command[i:i+2] == ['-Xlinker', option]: del command[i:i+count]
            else: i += 1
    index = command.index('-lkpx_cpu')
    command[index:index] = [str(cpu_object), str(test_object)]
    binary = path / 'ppc-core-tests'
    subprocess.run(command + ['-Wl,-e,_ppc_test_main', '-o', str(binary)], check=True, cwd=root / 'SheepShaver/src/MacOSX')
    subprocess.run([str(binary)] + (['--replay', str(args.replay.resolve())] if args.replay else []), check=True, cwd=root / 'SheepShaver/src/MacOSX')
    if not args.replay:
        capture = root / 'SheepShaver/src/kpx_cpu/tests/ppc_verify_fctiwz.capture'
        subprocess.run([str(binary), '--replay', str(capture)], check=True, cwd=root / 'SheepShaver/src/MacOSX')
