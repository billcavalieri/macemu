#!/usr/bin/env python3
"""Host test of the qcow shim (the same code the app links): writes through the shim into qcow v1, qcow2 (compat 0.10)
and qcow3 (compat 1.1) images, plain and compressed (zlib; zstd for qcow3), then checks the result with QEMU's own
tools: `qemu-img check` must be clean and an independent `qemu-img convert` to raw must hold the bytes the test wrote.
Needs build/qemu-blocklib (tools/build_qemu_blocklib.sh) and qemu-img from Homebrew."""
import os, shutil, subprocess, sys, tempfile, random
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / 'build/qemu-blocklib'
subprocess.run([str(root / 'tools/build_qemu_blocklib.sh')], check=True)
libs = [l.strip() for l in (out / 'ssqcow.rsp').read_text().splitlines() if l.strip()]
tmp = Path(tempfile.mkdtemp(prefix='qcow-test-'))
exe = tmp / 'qcow_shim_test'
subprocess.run(['cc', '-O1', '-I', str(root / 'BasiliskII/src/Unix'), str(root / 'tools/tests/qcow_shim_test.c'), '-o', str(exe)] + libs, check=True)

MIB16 = 16 * 1024 * 1024
random.seed(1)
src = tmp / 'src.raw'
src.write_bytes(bytes(random.choice([0, 0, 0, 1, 2, 3]) for _ in range(MIB16)))

def q(*a): return subprocess.run(['qemu-img', *a], check=True, capture_output=True, text=True).stdout
def fill(n, seed):
    s = (seed * 2654435761 + 1) & 0xffffffff; b = bytearray(n)
    for i in range(n):
        s = (s * 1664525 + 1013904223) & 0xffffffff; b[i] = s >> 24
    return bytes(b)

def mkc1(f):
    # qemu-img exits 1 without a message after writing a compressed qcow v1 file; the file is fine, so verify it instead
    subprocess.run(['qemu-img', 'convert', '-f', 'raw', '-O', 'qcow', '-c', str(src), f], capture_output=True)
    subprocess.run(['qemu-img', 'compare', '-f', 'raw', '-F', 'qcow', str(src), f], check=True, capture_output=True)

OFFS = lambda size: [0, 512, 65536 - 512, 65536, 1048576 + 1024, 3 * 65536 + 4096, size - 4096, size - 512]
LENS = [512, 512, 512, 4096, 512, 65536 + 512, 3584, 512]

cases = [
    ('qcow v1', 'v1.qcow', 1, lambda f: q('create', '-f', 'qcow', f, '16M')),
    ('qcow2 compat=0.10', 'v2.qcow2', 2, lambda f: q('create', '-f', 'qcow2', '-o', 'compat=0.10', f, '16M')),
    ('qcow3 compat=1.1', 'v3.qcow2', 3, lambda f: q('create', '-f', 'qcow2', '-o', 'compat=1.1', f, '16M')),
    ('qcow v1 compressed', 'c1.qcow', 1, lambda f: mkc1(f)),
    ('qcow2 compressed zlib', 'c2.qcow2', 2, lambda f: q('convert', '-f', 'raw', '-O', 'qcow2', '-c', '-o', 'compression_type=zlib', str(src), f)),
    ('qcow3 compressed zstd', 'c3.qcow2', 3, lambda f: q('convert', '-f', 'raw', '-O', 'qcow2', '-c', '-o', 'compat=1.1,compression_type=zstd', str(src), f)),
]
failed = 0
for name, fn, ver, make in cases:
    f = str(tmp / fn); make(f)
    r = subprocess.run([str(exe), f, str(ver), 'write', '7'], capture_output=True, text=True)
    ok = r.returncode == 0 and r.stdout.strip().endswith('OK')
    fmt = 'qcow' if ver == 1 else 'qcow2'
    check = 'n/a (qcow v1 has no checker)'
    if ver != 1:
        c = subprocess.run(['qemu-img', 'check', f], capture_output=True, text=True)
        check = 'clean' if c.returncode == 0 else 'ERRORS'
        ok &= c.returncode == 0
    raw = tmp / (fn + '.raw'); q('convert', '-f', fmt, '-O', 'raw', f, str(raw))
    data = raw.read_bytes()
    base = src.read_bytes() if name.find('compressed') >= 0 else bytes(MIB16)
    exp = bytearray(base)
    for i, (o, l) in enumerate(zip(OFFS(MIB16), LENS)):
        exp[o:o + l] = fill(l, 7 + i)
    same = data == bytes(exp)
    r2 = subprocess.run([str(exe), f, str(ver), 'read', '7'], capture_output=True, text=True)
    ok &= same and r2.stdout.strip().endswith('OK')
    print(f'{name:26s} shim write {"ok" if r.returncode == 0 else "FAIL"}, qemu-img check {check}, independent raw compare {"same" if same else "DIFFERENT"}, reopen {"ok" if r2.returncode == 0 else "FAIL"}')
    failed += not ok
# backing files, resolved as QEMU does: overlay -> base reads fall through, writes (also partial-cluster ones) go to the
# overlay only, the base stays byte-identical, relative paths resolve against the overlay's directory
def chain_case(name, base_make, base_fmt, depth=1, ver=2):
    d = tmp / ('chain-' + name.replace(' ', '_')); d.mkdir()
    base = d / ('base.' + base_fmt); base_make(str(base))
    base_sum = subprocess.run(['shasum', str(base)], capture_output=True, text=True).stdout.split()[0]
    prev, pfmt = 'base.' + base_fmt, base_fmt
    for i in range(depth):
        cur = f'ovl{i}.qcow2'
        subprocess.run(['qemu-img', 'create', '-f', 'qcow2', '-b', prev, '-F', pfmt, str(d / cur)], check=True, capture_output=True, cwd=d)
        prev, pfmt = cur, 'qcow2'
    top = str(d / prev)
    r = subprocess.run([str(exe), top, str(ver), 'write', '7'], capture_output=True, text=True, cwd='/')   # cwd elsewhere: the base path is relative to the overlay
    ok = r.returncode == 0 and r.stdout.strip().endswith('OK') and 'chain ' in r.stdout and '->' in r.stdout
    c = subprocess.run(['qemu-img', 'check', top], capture_output=True, text=True); ok &= c.returncode == 0
    raw = d / 'top.raw'; q('convert', '-f', 'qcow2', '-O', 'raw', top, str(raw))
    exp = bytearray(src.read_bytes())
    for i, (o, l) in enumerate(zip(OFFS(MIB16), LENS)):
        exp[o:o + l] = fill(l, 7 + i)
    same = raw.read_bytes() == bytes(exp)
    base_same = subprocess.run(['shasum', str(base)], capture_output=True, text=True).stdout.split()[0] == base_sum
    print(f'backing {name:34s} shim {"ok" if ok else "FAIL"}, view = base + writes {"same" if same else "DIFFERENT"}, base untouched {"yes" if base_same else "NO"}')
    return ok and same and base_same
failed += not chain_case('qcow2 over qcow2', lambda f: q('convert', '-f', 'raw', '-O', 'qcow2', str(src), f), 'qcow2')
failed += not chain_case('qcow2 over raw', lambda f: shutil.copy(str(src), f), 'raw')
failed += not chain_case('qcow2 over compressed qcow2', lambda f: q('convert', '-f', 'raw', '-O', 'qcow2', '-c', str(src), f), 'qcow2')
failed += not chain_case('3-deep qcow2 chain', lambda f: q('convert', '-f', 'raw', '-O', 'qcow2', str(src), f), 'qcow2', depth=3)
# a qcow2 whose backing file is missing must fail with the backing-chain code (the app then refuses to start)
b = str(tmp / 'base.qcow2'); q('create', '-f', 'qcow2', b, '1M'); o = str(tmp / 'ovl.qcow2'); q('create', '-f', 'qcow2', '-b', b, '-F', 'qcow2', o)
os.rename(b, b + '.moved')
r = subprocess.run([str(exe), o, '2', 'read', '1'], capture_output=True, text=True)
print('missing backing file fails (code 3, message names it):', 'yes' if r.returncode == 3 and 'base.qcow2' in r.stderr else f'NO ({r.returncode}: {r.stderr.strip()})')
failed += not (r.returncode == 3 and 'base.qcow2' in r.stderr)
# a backing file that is not a usable image of the declared format fails the same way
open(b, 'wb').write(b'not a qcow2 image' * 100)
r = subprocess.run([str(exe), o, '2', 'read', '1'], capture_output=True, text=True)
print('unusable backing file fails (code 3):', 'yes' if r.returncode == 3 else f'NO ({r.returncode}: {r.stderr.strip()})')
failed += r.returncode != 3
# a backing loop (overlay naming itself) fails the same way
l = tmp / 'loop.qcow2'; q('create', '-f', 'qcow2', str(l), '1M')
subprocess.run(['qemu-img', 'rebase', '-u', '-f', 'qcow2', '-F', 'qcow2', '-b', str(l), str(l)], capture_output=True)
r = subprocess.run([str(exe), str(l), '2', 'read', '1'], capture_output=True, text=True)
print('backing loop fails (code 3):', 'yes' if r.returncode == 3 else f'NO ({r.returncode}: {r.stderr.strip()})')
failed += r.returncode != 3
# an AES-encrypted qcow v1 (crypt_method = 1 at header offset 36) must not open: this QEMU build has no crypto
e = tmp / 'aes.qcow'; q('create', '-f', 'qcow', str(e), '1M'); data = bytearray(e.read_bytes()); data[36:40] = (1).to_bytes(4, 'big'); e.write_bytes(data)
r = subprocess.run([str(exe), str(e), '1', 'read', '1'], capture_output=True, text=True)
print('encrypted image refused:', 'yes' if r.returncode != 0 else 'NO'); failed += r.returncode == 0
shutil.rmtree(tmp)
print('qcow tests:', 'FAILED' if failed else 'passed'); sys.exit(1 if failed else 0)
