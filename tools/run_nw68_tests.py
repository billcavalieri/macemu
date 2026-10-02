#!/usr/bin/env python3
"""Build/run isolated native 68k tests; optionally compare a decoded 4 MiB ROM."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import platform

parser = argparse.ArgumentParser()
parser.add_argument('--rom', type=Path, help='user-provided decoded New World ROM')
parser.add_argument('--release', action='store_true')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--thread-sanitize', action='store_true', help='run only the CPU special-flag concurrency regression under ThreadSanitizer')
parser.add_argument('--benchmark', action='store_true', help='isolated register workload versus PPC-JIT NanoKernel (requires ROM)')
parser.add_argument('--policy-csv', type=Path, help='write the complete opcode candidate/exit policy')
args = parser.parse_args()
if platform.machine().lower() not in ('arm64', 'aarch64'):
    parser.error('Native code tests require an ARM64 host')
if args.benchmark and not args.rom:
    parser.error('--benchmark requires --rom')
root = Path(__file__).resolve().parents[1]
src = root / 'SheepShaver/src'
names = ['nw_68k_state.cpp', 'nw_68k_decode.cpp', 'nw_68k_memory.cpp',
         'nw_68k_emit_a64.cpp', 'nw_68k_profile.cpp', 'nw_jit.cpp',
         'nw_boot_contract.cpp', 'nw_bootinfo.cpp', 'nw_io.cpp', 'nw_devices.cpp',
         'nw_nvram.cpp', 'kpx_cpu/src/cpu/ppc/ppc-mmu.cpp',
         'kpx_cpu/tests/nw_test_platform.cpp', 'kpx_cpu/tests/nw_68k_harness.cpp']
with tempfile.TemporaryDirectory(prefix='macemu-nw68-tests-') as directory:
    flags_binary = Path(directory) / 'spcflags-tests'
    flags_command = ['clang++', '-std=c++11', '-O1', '-g', '-DHAVE_CONFIG_H',
                     '-o', str(flags_binary)]
    if args.thread_sanitize:
        flags_command += ['-fsanitize=thread']
    for path in ['MacOSX/config', 'include', 'Unix', 'kpx_cpu/src']:
        flags_command += ['-I', str(src / path)]
    flags_command += [str(src / 'kpx_cpu/tests/spcflags_harness.cpp')]
    subprocess.run(flags_command, check=True, cwd=root)
    subprocess.run([str(flags_binary)], check=True)
    if args.thread_sanitize:
        raise SystemExit(0)
    binary = Path(directory) / 'nw68-tests'
    command = ['clang++', '-std=c++11', '-O2' if args.release else '-O0', '-g',
               '-Wl,-dead_strip', '-o', str(binary)]
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    for path in ['MacOSX/config', 'include', 'Unix', 'kpx_cpu/src', 'kpx_cpu/include']:
        command += ['-I', str(src / path)]
    command += [str(src / name) for name in names]
    subprocess.run(command, check=True, cwd=root)
    arguments = [str(binary)] + ([str(args.rom.resolve())] if args.rom else [])
    if args.benchmark:
        arguments += ['--benchmark']
    if args.policy_csv:
        arguments += ['--policy', str(args.policy_csv.resolve())]
    subprocess.run(arguments, check=True)
