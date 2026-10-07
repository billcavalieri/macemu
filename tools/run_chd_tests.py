#!/usr/bin/env python3
"""Host test of the CHD shim (the code the app links): creates hard-disk CHDs with chdman (built from the same pinned
MAME tree), writes through the shim and checks the result with `chdman verify` and an independent `chdman extracthd`.
Covered: uncompressed v5 written in place; compressed v5 written into <path>.ssdiff.chd with the original untouched;
the diff reused on a later open; read-only (`*`) open of a compressed file; refusal of files with a parent and of CD CHDs."""
import hashlib, random, shutil, subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for t in ('build_chdlib.sh', 'build_chdman.sh'):
    subprocess.run([str(root / 'tools' / t)], check=True, stdout=subprocess.DEVNULL)
out = root / 'build/chdlib'
chdman = str(out / 'chdman')
tmp = Path(tempfile.mkdtemp(prefix='chd-test-'))
exe = tmp / 'chd_shim_test'
subprocess.run(['cc', '-O1', '-I', str(root / 'BasiliskII/src/Unix'), str(root / 'tools/tests/chd_shim_test.c'), '-o', str(exe)]
               + [l.strip() for l in (out / 'sschd.rsp').read_text().splitlines() if l.strip()], check=True)

random.seed(3)
raw = tmp / 'src.raw'
raw.write_bytes(bytes(random.choice([0, 0, 0, 1, 2, 3, 4]) for _ in range(8 * 1024 * 1024)))
def run(*a, check=True): return subprocess.run(list(a), capture_output=True, encoding='utf-8', errors='replace', check=check)
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def fill(n, seed, zero=False):
    s = (seed * 2654435761 + 1) & 0xffffffff; b = bytearray(n)
    for i in range(n):
        s = (s * 1664525 + 1013904223) & 0xffffffff; b[i] = 0 if zero else s >> 24
    return bytes(b)
OFFS = lambda size: [512, 1024, 4096 - 512, 8192 + 512, 40000 - 512, 100000, size - 1024, size - 512]
LENS = [512, 1024, 1024, 512, 4096, 70000, 512, 512]
ZERO = [0, 0, 0, 1, 0, 0, 0, 1]
def expected(base, size, seed):
    e = bytearray(base)
    for i, (o, l, z) in enumerate(zip(OFFS(size), LENS, ZERO)):
        if o >= 0 and o + l <= size: e[o:o + l] = fill(l, seed + i, bool(z))
    return bytes(e)

failed = 0
def report(name, ok, detail=''):
    global failed
    print(f'{name:58s} {"ok" if ok else "FAIL"} {detail}'); failed += not ok

# 1. uncompressed v5, in place
f = tmp / 'plain.chd'
run(chdman, 'createraw', '-i', str(raw), '-o', str(f), '-hs', '4096', '-us', '512', '-c', 'none')
r = run(str(exe), str(f), 'write', '5', check=False)
v = run(chdman, 'verify', '-i', str(f), check=False)
x = tmp / 'plain.out'; run(chdman, 'extractraw', '-i', str(f), '-o', str(x))
size = len(raw.read_bytes())
report('uncompressed v5: shim write in place', r.returncode == 0 and 'read_only 0' in r.stdout)
report('uncompressed v5: chdman verify', v.returncode == 0, v.stdout.strip().splitlines()[-1] if v.stdout.strip() else v.stderr[-80:])
report('uncompressed v5: independent extract holds the bytes', x.read_bytes() == expected(raw.read_bytes(), size, 5))

# 2. compressed v5: writes go to the diff, the original stays byte-identical
for codec in ('zlib', 'lzma', 'zstd', 'huff'):
    f = tmp / f'comp_{codec}.chd'
    run(chdman, 'createraw', '-i', str(raw), '-o', str(f), '-hs', '4096', '-us', '512', '-c', codec)
    before = sha(f)
    r = run(str(exe), str(f), 'write', '9', check=False)
    diff = Path(str(f) + '.ssdiff.chd')
    report(f'compressed v5 ({codec}): shim write, diff created', r.returncode == 0 and diff.exists() and 'read_only 0' in r.stdout)
    report(f'compressed v5 ({codec}): original unchanged', sha(f) == before)
    v = run(chdman, 'verify', '-i', str(f), check=False)
    report(f'compressed v5 ({codec}): chdman verify original', v.returncode == 0)
    x = tmp / f'comp_{codec}.out'
    ex = run(chdman, 'extractraw', '-i', str(diff), '-ip', str(f), '-o', str(x), check=False)
    report(f'compressed v5 ({codec}): extract diff+parent holds the bytes', ex.returncode == 0 and x.exists() and x.read_bytes() == expected(raw.read_bytes(), size, 9))
    r2 = run(str(exe), str(f), 'check', '9', check=False)
    report(f'compressed v5 ({codec}): reopen reuses the diff', r2.returncode == 0 and r2.stdout.strip().endswith('OK') and 'read_only 0' in r2.stdout)
    ro = run(str(exe), str(f), 'read', '9', check=False)
    report(f'compressed v5 ({codec}): read-only open skips the diff', 'read_only 1' in ro.stdout and 'verify' in ro.stdout)

# 3. a version 4 hard-disk CHD (built by hand: chdman only writes version 5) opens read-only and reads correctly
import struct, zlib
hunk, nh = 4096, 64
payload = [bytes(random.getrandbits(8) for _ in range(hunk)) for _ in range(nh)]
meta_at = 108 + 16 * nh + nh * hunk
geom = b'CYLS:1,HEADS:8,SECS:64,BPS:512\0'
v4 = bytearray(b'MComprHD' + struct.pack('>IIII', 108, 4, 0, 0) + struct.pack('>IQQI', nh, nh * hunk, meta_at, hunk) + bytes(60))
v4 += b''.join(struct.pack('>QIHBB', 108 + 16 * nh + i * hunk, zlib.crc32(payload[i]), hunk & 0xffff, hunk >> 16, 2) for i in range(nh))
v4 += b''.join(payload)
v4 += b'GDDD' + bytes([1]) + len(geom).to_bytes(3, 'big') + bytes(8) + geom
(tmp / 'v4.chd').write_bytes(v4)
r = run(str(exe), str(tmp / 'v4.chd'), 'write', '1', check=False)   # asked for read/write: must come back read-only
report('version 4 CHD opens read-only (asked for write)', 'read_only 1' in r.stdout and 'size %d' % (nh * hunk) in r.stdout, r.stdout.splitlines()[0] if r.stdout else r.stderr[-80:])
r = run(str(exe), str(tmp / 'v4.chd'), 'read', '1', check=False)
report('version 4 CHD reads (content check by the host test is size/mode only)', 'size' in r.stdout)

# 4. refusals
cdr = tmp / 'cd.chd'
cue = tmp / 'cd.cue'; (tmp / 'cd.bin').write_bytes(bytes(2352 * 300))
cue.write_text('FILE "cd.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n')
run(chdman, 'createcd', '-i', str(cue), '-o', str(cdr))
r = run(str(exe), str(cdr), 'write', '1', check=False)
report('CD CHD is refused', r.returncode != 0 and 'open failed (status 2)' in r.stdout, r.stdout.strip()[:100])
# a file that has a parent: the diff the shim itself made above
child = tmp / 'comp_zlib.chd.ssdiff.chd'
r = run(str(exe), str(child), 'read', '1', check=False)
report('CHD with a parent is refused', r.returncode != 0 and 'status 2' in r.stdout, r.stdout.strip()[:100])
junk = tmp / 'junk.chd'; junk.write_bytes(b'MComprHD' + bytes(100))
r = run(str(exe), str(junk), 'read', '1', check=False)
report('damaged CHD (magic only) is refused, not "not a CHD"', r.returncode != 0 and 'status 2' in r.stdout)
plain = tmp / 'plain.bin'; plain.write_bytes(bytes(4096))
r = run(str(exe), str(plain), 'read', '1', check=False)
report('non-CHD file is reported as not a CHD', 'status 0' in r.stdout)

shutil.rmtree(tmp)
print('chd tests:', 'FAILED' if failed else 'passed'); sys.exit(1 if failed else 0)
