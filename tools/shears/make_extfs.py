#!/usr/bin/env python3
"""Builds the Sheep Shears download for people who share a folder from the Mac (SheepShaver's extfs): a zip of a folder
whose files carry real macOS resource forks and Finder info (type and creator), which is what SheepShaver's extfs on
macOS reads. Unzip it (double-click) into the shared folder; in the guest the shared volume then shows "Sheep Shears"
with "Install Sheep Shears" in it.

  make_extfs.py OUT.zip BUILD_DIR      BUILD_DIR is build/sheepshears (the MacBinary .bin files Retro68 wrote)

macOS only: it sets the forks with xattr and zips with `ditto`, which stores them as AppleDouble (__MACOSX/._name)
the way Archive Utility does."""
import shutil, struct, subprocess, sys, tempfile
from pathlib import Path

if len(sys.argv) != 3:
    sys.exit(__doc__)
out, build = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
TOP = 'Sheep Shears'
FILES = 'Sheep Shears Files'

def macbinary(path):
    b = path.read_bytes()
    if b[0] != 0 or b[74] != 0 or b[82] != 0:
        sys.exit(f'{path} is not MacBinary')
    dlen, rlen = struct.unpack('>II', b[83:91])
    flags = (b[73] << 8) | b[101]
    data = b[128:128 + dlen]
    roff = 128 + ((dlen + 127) // 128) * 128
    return dict(type=b[65:69], creator=b[69:73], flags=flags, data=data, rsrc=b[roff:roff + rlen])

def finder_info(type_, creator, flags):
    # FInfo: type, creator, flags (not "has been inited": the Finder places the icon itself), location, folder; then FXInfo
    return type_ + creator + struct.pack('>HHHH', flags & ~0x0100, 0, 0, 0) + bytes(16)

def put(directory, name, data, type_, creator, flags=0, rsrc=b''):
    f = directory / name
    f.write_bytes(data)
    if rsrc:
        (f.parent / (name + '/..namedfork/rsrc')).write_bytes(rsrc)
    subprocess.run(['xattr', '-wx', 'com.apple.FinderInfo', finder_info(type_, creator, flags).hex(), str(f)], check=True)

stage = Path(tempfile.mkdtemp(prefix='shears-extfs-'))
try:
    top = stage / TOP
    (top / FILES).mkdir(parents=True)
    for src, name, dest, type_ in [('SheepShearsInstaller.bin', 'Install Sheep Shears', top, None),
                                   ('SheepShears.bin', 'Sheep Shears Tool', top / FILES, None),
                                   ('SheepShearsPanel.bin', 'Sheep Shears', top / FILES, b'APPC')]:    # a control panel application
        m = macbinary(build / src)
        put(dest, name, m['data'], type_ or m['type'], m['creator'], m['flags'], m['rsrc'])
    readme = build / 'ReadMe.txt'
    if readme.exists():
        put(top, 'ReadMe', readme.read_bytes().replace(b'\n', b'\r'), b'TEXT', b'ttxt')   # Mac line ends
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    subprocess.run(['ditto', '-c', '-k', '--keepParent', '--rsrc', '--sequesterRsrc', str(top), str(out)], check=True)
finally:
    shutil.rmtree(stage, ignore_errors=True)
print(out, out.stat().st_size, 'bytes')
