#!/usr/bin/env python3
"""Build diagnostic shim; preserve DTB and boot-v2 settings from a proven image.
No device commands. The template is private device data, never committed.
"""
import argparse
import hashlib
from pathlib import Path
import os
import shutil
import struct
import subprocess


def run(argv, **kwargs):
    return subprocess.check_output(argv, **kwargs)


def pack_boot(template, kernel):
    if template[:8] != b'ANDROID!':
        raise ValueError('not an Android boot image')
    ks, _, rs, _, ss, _, _, page, version, _ = struct.unpack_from('<10I', template, 8)
    if version != 2 or page != 4096 or ss:
        raise ValueError('expected proven v2 / 4096 / no second-stage template')
    rds, rdo, hs = struct.unpack_from('<IQI', template, 1632)
    ds, _ = struct.unpack_from('<IQ', template, 1648)
    if rds or rdo or hs != 1660 or not ds:
        raise ValueError('unexpected recovery DTBO or header size / missing DTB')
    align = lambda n: (n + page - 1) // page * page
    rdpos = page + align(ks)
    dtpos = rdpos + align(rs)
    ramdisk = template[rdpos:rdpos + rs]
    dtb = template[dtpos:dtpos + ds]
    if len(ramdisk) != rs or len(dtb) != ds or dtb[:4] != b'\xd0\x0d\xfe\xed':
        raise ValueError('truncated template or invalid DTB')
    # Keep the known empty compressed cpio. Reject XNU or other payloads.
    import gzip
    archive = gzip.decompress(ramdisk)
    if not archive.startswith(b'070701') or b'TRAILER!!!' not in archive or len(archive) > 512:
        raise ValueError('template must have the test1 empty cpio ramdisk')
    header = bytearray(template[:page])
    struct.pack_into('<I', header, 8, len(kernel))
    digest = hashlib.sha1()
    # AOSP mkbootimg v2 ID: kernel, ramdisk, second, recovery_dtbo, dtb.
    for part in [kernel, ramdisk, b'', b'', dtb]:
        digest.update(part)
        digest.update(struct.pack('<I', len(part)))
    header[576:608] = digest.digest() + bytes(12)
    padded = lambda b: b + bytes(align(len(b)) - len(b))
    return bytes(header) + padded(kernel) + padded(ramdisk) + padded(dtb)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--template', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--mode', choices=['cpu', 'smp', 'gic-read', 'mmu-hold'], default='cpu')
    a = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    prefix = os.environ.get('CROSS_COMPILE')
    if not prefix:
        prefix = next((p for p in ['aarch64-elf-', 'aarch64-linux-gnu-'] if shutil.which(p + 'gcc')), None)
    if not prefix:
        raise SystemExit('aarch64 GCC/binutils required; set CROSS_COMPILE')
    flags = ['-O2', '-ffreestanding', '-nostdlib', '-fno-pic', '-mgeneral-regs-only',
             '-mstrict-align', '-fno-stack-protector', '-fno-builtin', '-Wall', '-Wextra',
             '-DV60_DIAG=' + {'cpu': '1', 'smp': '2', 'gic-read': '3', 'mmu-hold': '4'}[a.mode]]
    objects = []
    for name in ['start.S', 'shim.c', 'console.c', 'mmu.c', 'fdt.c', 'loader.c', 'ufs.c']:
        obj = out / (Path(name).stem + '.o')
        subprocess.run([prefix + 'gcc', *flags, '-c', str(root / 'shim' / name), '-o', str(obj)], check=True)
        objects.append(obj)
    relocs = run([prefix + 'readelf', '-r', *map(str, objects[1:])]).decode()
    if 'ABS64' in relocs:
        raise SystemExit('absolute relocation in position-independent C objects')
    elf = out / 'shim.elf'
    subprocess.run([prefix + 'gcc', *flags, '-static', '-T', str(root / 'shim/shim.ld'),
                    '-Wl,--build-id=none', '-Wl,--no-warn-rwx-segments', '-Wl,-N',
                    *map(str, objects), '-o', str(elf)], check=True)
    raw = out / 'Image'
    subprocess.run([prefix + 'objcopy', '-O', 'binary', str(elf), str(raw)], check=True)
    nm = run([prefix + 'nm', str(elf)]).decode()
    end = next(int(line.split()[0], 16) for line in nm.splitlines() if line.split()[-1] == '_end')
    kernel = raw.read_bytes()
    if len(kernel) > end:
        raise ValueError('binary exceeds _end')
    kernel += bytes(end - len(kernel))
    if kernel[56:60] != b'ARM\x64' or struct.unpack_from('<Q', kernel, 16)[0] != end:
        raise ValueError('invalid Linux Image header')
    raw.write_bytes(kernel)
    name = {'cpu': 'v60-shim-test2.img', 'smp': 'v60-shim-test2-smp.img',
            'gic-read': 'v60-shim-test2b.img', 'mmu-hold': 'v60-shim-test2b-mmu.img'}[a.mode]
    boot = pack_boot(a.template.read_bytes(), kernel)
    (out / name).write_bytes(boot)
    (out / 'relocations.txt').write_text(relocs)
    (out / 'symbols.txt').write_text(nm)
    print(f'{out / name}: {len(boot)} bytes, SHA256 {hashlib.sha256(boot).hexdigest()}')


if __name__ == '__main__':
    main()
