#!/usr/bin/env python3
"""Test the real KPX interpreter from a completed isolated Xcode app build.

Pass --build-log from xcodebuild -scheme SheepShaver (Debug or Release).
Reuses the build's flags and objects, replacing only the CPU clock reference.
The alternate entry point never launches AppKit or initializes guest media.
"""
import argparse
import importlib.util
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-log', type=Path, required=True)
parser.add_argument('--replay', type=Path, help='Replay a bounded live PPC verification capture instead of the core suite')
parser.add_argument('--scalar-p6', action='store_true', help='Run the focused independent scalar conversion/comparison fixtures')
parser.add_argument('--frsp-p6', action='store_true', help='Run independent round-to-single and production exception fixtures')
parser.add_argument('--basic-special-p6', action='store_true', help='Run basic scalar special-result and production exception fixtures')
parser.add_argument('--io-publication', action='store_true', help='Run raw file-read code-publication fixtures')
args = parser.parse_args()
if sum(bool(x) for x in (args.replay,args.scalar_p6,args.frsp_p6,args.basic_special_p6,args.io_publication)) > 1: parser.error('Focused modes are mutually exclusive')
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
    subprocess.run([str(binary)] + (['--replay', str(args.replay.resolve())] if args.replay else ['--scalar-p6'] if args.scalar_p6 else ['--frsp-p6'] if args.frsp_p6 else ['--basic-special-p6'] if args.basic_special_p6 else ['--io-publication'] if args.io_publication else []), check=True, cwd=root / 'SheepShaver/src/MacOSX')
    if not args.replay and not args.scalar_p6 and not args.frsp_p6 and not args.basic_special_p6 and not args.io_publication:
        for name in ['ppc_verify_fctiwz.capture', 'ppc_verify_atomic_alias.capture', 'ppc_verify_vector_sat.capture', 'ppc_verify_system_trap.capture', 'ppc_verify_stale_source.capture']:
            capture = root / 'SheepShaver/src/kpx_cpu/tests' / name
            subprocess.run([str(binary), '--replay', str(capture)], check=True, cwd=root / 'SheepShaver/src/MacOSX')
        # Compare the generated checklist's Python policy to the actual C
        # app policy across opcode-4/31 slots and each register-field value.
        spec = importlib.util.spec_from_file_location('jit_ops_policy', root / 'tools/gen_jit_ops_md.py')
        policy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(policy)
        rows = subprocess.check_output([str(binary), '--vmx-policy'], text=True).splitlines()
        probes = 0
        for row in rows:
            if not row.startswith('VMX '): continue
            _, opcode, accepted = row.split()
            op = int(opcode, 16)
            if int(accepted) != policy.nw_jit_op_supported(op):
                raise RuntimeError('VMX C/Python policy mismatch for ' + opcode)
            probes += 1
        if probes != 2 * 2048 * 3 * 32: raise RuntimeError('Incomplete VMX policy probe')
        print(f'VMX C/Python policy: {probes} probes, zero mismatches', flush=True)
        # Exercise the complete portable kernel under both sanitizers; app
        # objects and generated ARM64 instructions are otherwise uninstrumented.
        sanitizer_binary = path / 'ppc-vmx-sanitizers'
        platform_flags = []
        for option in ('-target', '-isysroot'):
            if option in link_command:
                i = link_command.index(option)
                platform_flags += link_command[i:i+2]
        subprocess.run([link_command[0]] + platform_flags + ['-std=c++17', '-O1', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I', str(root / 'SheepShaver/src/include'),
                        '-I', str(root / 'SheepShaver/src/kpx_cpu/src'),
                        str(root / 'SheepShaver/src/kpx_cpu/tests/ppc_vmx_integer_sanitize.cpp'),
                        '-o', str(sanitizer_binary)], check=True)
        subprocess.run([str(sanitizer_binary)], check=True)
