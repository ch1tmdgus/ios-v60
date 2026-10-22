#!/usr/bin/env python3
"""Build an untested V60 GUI candidate with a source-built shim and kernel.

The private APFS sidecar is described by a read-only recovery plan. This
builder never connects to or writes a phone. Keep the resulting plan private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time
import zlib
import build
from smoke import symbol
from recovery_system import digest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'kernel', 'root', 'plan', 'out'):
        ap.add_argument('--' + name, required=True, type=Path)
    ap.add_argument('--source-revision', required=True)
    ap.add_argument('--run-id', required=True)
    a = ap.parse_args()
    if len(a.source_revision) != 40 or any(c not in '0123456789abcdef' for c in a.source_revision):
        ap.error('provide the full source build Git revision')
    out = a.out.resolve()
    if out.exists():
        ap.error('choose a fresh output folder')
    out.mkdir(parents=True)
    plan = json.loads(a.plan.read_text())
    sha, crc = digest(Path(plan['image']))
    assert sha == plan['image_sha256'] and crc == plan['image_crc32']
    assert plan['image_bytes'] == Path(plan['image']).stat().st_size
    kernel = a.kernel.read_bytes()
    for s in (b'_v60_input_init', b'_v60_keys_init', b'_redwood_touch_post'):
        symbol(kernel, s)  # Require the actual native input build, not old prebuilt.
    build.PHYS, build.MEM = 0x100000000, 0x180000000
    small, info = build.payload(kernel, a.root.read_bytes(), False)
    n = struct.unpack_from('<I', small, 12)[0]
    virt, entry, dt_off, dt_len, old_end, phys, memory = struct.unpack_from('<7Q', small, 0x10)
    tree, _ = build.adt.parse(small[dt_off:dt_off+dt_len])
    chosen = next(c for c in tree.children if c.name == 'chosen')
    mm = next(c for c in chosen.children if c.name == 'memory-map')
    small_pa = int(info['ramdisk_pa'], 16)
    system_pa = old_end - virt + phys
    image_bytes = plan['image_bytes']
    end = build.ALIGN(old_end + image_bytes)
    assert end - virt + 0x20000 + (1 << 30) < memory
    mm.props = [(name, flags, data) for name, flags, data in mm.props if name != 'RAMDisk']
    mm.props.extend([('RAMDisk', 0, build.U64(system_pa, image_bytes)),
                     ('RAMDisk1', 0, build.U64(small_pa, a.root.stat().st_size))])
    dt = build.adt.serialize(tree)
    d = struct.pack('<8sII6QIIQ', b'V60APFS2', 1, 80, plan['first_lba'], plan['last_lba'],
                    plan['hfs_bytes'], plan['relative'], image_bytes, system_pa, crc, 0, 0)
    d = d[:68] + struct.pack('<I', zlib.crc32(d[:68])) + d[72:]
    pieces = []
    for i in range(n):
        va, size, off, length = struct.unpack_from('<4Q', small, 0x2a8 + i*32)
        pieces.append((va, size, small[off:off+length]))
    pieces.append((old_end, image_bytes, b''))
    p = bytearray(0x2a8 + len(pieces)*32)
    p += d
    for i, (va, size, data) in enumerate(pieces):
        off = len(p)
        p += data
        p += bytes(build.ALIGN(len(p), 16) - len(p))
        struct.pack_into('<4Q', p, 0x2a8 + i*32, va, size, off, len(data))
    dt_off = len(p)
    p += dt
    p += bytes(build.ALIGN(len(p), 8) - len(p))
    struct.pack_into('<8sII7Q', p, 0, b'SWEETXNU', 2, len(pieces), virt, entry, dt_off, len(dt), end, phys, memory)
    cmd = ('debug=0x100 keepsyms=1 cpus=1 rd=md0 v60=1 v60touch=1 v60touchdelay=10 v60keys=1 '
           'gpu=0 wifi=0 touchscreen=0 buttons=0 cpufreq=0 battery=0 adsp=0 cam=0 haptic=0 torch=0 ufs_write=0 epoch=' + str(int(time.time())))
    assert len(cmd) < 608
    p[0x48:0x2a8] = cmd.encode().ljust(608, b'\0')
    (out / 'payload.bin').write_bytes(p)
    src = build.HERE.parent / 'shim'
    sources = ['start.S', 'shim.c', 'console.c', 'mmu.c', 'fdt.c', 'loader.c', 'ufs.c']
    work = out / 'shim'
    work.mkdir()
    source_hashes = {}
    for path in src.iterdir():
        if path.suffix in ('.c', '.h', '.S', '.ld'):
            shutil.copyfile(path, work / path.name)
            source_hashes[path.name] = build.SHA((work / path.name).read_bytes())
    flags = ['-O2', '-ffreestanding', '-nostdlib', '-fno-pic', '-mgeneral-regs-only',
             '-mstrict-align', '-fno-stack-protector', '-Wall', '-Wextra', '-DV60_EXTERNAL_ROOT']
    objects = []
    for name in sources:
        obj = Path(name).stem + '.o'
        subprocess.run(['aarch64-elf-gcc', *flags, '-c', name, '-o', obj], cwd=work, check=True)
        objects.append(obj)
    reloc = subprocess.check_output(['aarch64-elf-readelf', '-r', *objects[1:]], cwd=work, text=True)
    assert 'R_AARCH64_ABS64' not in reloc, 'C objects contain non-relocatable pointers'
    subprocess.run(['aarch64-elf-gcc', *flags, '-static', '-T', 'shim.ld', '-Wl,--build-id=none',
                    '-Wl,--no-warn-rwx-segments', '-Wl,-N', *objects, '-o', 'shim.elf'], cwd=work, check=True)
    subprocess.run(['aarch64-elf-objcopy', '-O', 'binary', 'shim.elf', 'Image'], cwd=work, check=True)
    symbols = subprocess.check_output(['aarch64-elf-nm', 'shim.elf'], cwd=work, text=True)
    image_size = int(next(line.split()[0] for line in symbols.splitlines() if line.split()[-1] == '_end'), 16)
    with (work / 'Image').open('r+b') as image:
        image.truncate(image_size)
    shim = (work / 'Image').read_bytes()
    assert struct.unpack_from('<Q', shim, 16)[0] == len(shim)
    baseline = a.baseline.read_bytes()
    assert build.SHA(baseline) == build.BASE_SHA
    ks, _, rs, _, ss, _, _, ps, version, _ = struct.unpack_from('<10I', baseline, 8)
    assert baseline[:8] == b'ANDROID!' and ps == 4096 and version == 2 and ss == 0
    ds = struct.unpack_from('<I', baseline, 1648)[0]
    off = ps + build.ALIGN(ks, ps) + build.ALIGN(rs, ps)
    dtb = baseline[off:off+ds]
    assert build.SHA(dtb) == '2b7d6d2cf7f7a9b7344e73a5550a4a4fdbf016ce1cf540eef55bf7f21aa25a63'
    header = bytearray(baseline[:ps])
    struct.pack_into('<I', header, 8, len(shim))
    struct.pack_into('<I', header, 16, len(p))
    check = hashlib.sha1()
    for data in (shim, p, b'', b'', dtb):
        check.update(data)
        check.update(build.U32(len(data)))
    header[576:608] = check.digest() + bytes(12)
    pad = lambda data: bytes(data) + bytes(build.ALIGN(len(data), ps)-len(data))
    image = bytes(header) + pad(shim) + pad(p) + pad(dtb)
    target = out / 'v60-hackios-gui1.img'
    target.write_bytes(image)
    info.update(stage='GUI candidate; hardware untested', device_tested=False, payload_version=2,
                cmdline=cmd, kernel_sha256=build.SHA(kernel), source_revision=a.source_revision,
                github_run=a.run_id, shim_sources=source_hashes, shim_sha256=build.SHA(shim),
                system_sha256=sha, system_bytes=image_bytes, system_relative=plan['relative'],
                root_apfs_sha256=build.SHA(a.root.read_bytes()),
                userland_commit=build.USERLAND_COMMIT,
                ramdisk_pa=hex(system_pa), small_ramdisk_pa=hex(small_pa),
                image_sha256=build.SHA(image), image_size=len(image), image=target.name)
    (out / 'manifest.json').write_text(json.dumps(info, indent=2) + '\n')
    print(target)


if __name__ == '__main__':
    main()
