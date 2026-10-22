#!/usr/bin/env python3
"""Scatter-load matched HackiOS GUI images in QEMU; never accesses a phone.

Avoid making multiple copies of the multi-gigabyte Apple system image. Apple
files remain private. This validates software only, not V60 hardware drivers.
"""
import argparse
import importlib.util
import json
import re
from pathlib import Path
import struct
import subprocess
import time
import build
from smoke import symbol


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--kernel', required=True, type=Path)
    ap.add_argument('--system', required=True, type=Path)
    ap.add_argument('--root', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--seconds', type=int, default=180)
    ap.add_argument('--bootarg', action='append', default=[], help='extra single-token software diagnostic boot argument')
    ap.add_argument('--cpus', type=int, choices=(1, 4), default=1)
    a = ap.parse_args()
    out = a.out.resolve()
    if out.exists() or len(str(out / 'mon.sock')) > 100:
        ap.error('choose a fresh short output path')
    out.mkdir()
    # Live V60 DT: contiguous RAM from 0xc0000000 to 0x280000000.
    # Restrict this candidate to >=4GiB: all dynamic reserved pools are <4GiB.
    build.PHYS, build.MEM = 0x100000000, 0x180000000
    k = bytearray(a.kernel.read_bytes())
    m = build.mkpayload.parse_macho(k)
    assert m['cputype'] == 0x100000c and m['cpusub'] == 0 and m['fixups'] is None
    delta = build.PHYS - build.VIRT
    slot = k.find(b'XNULDRST')
    assert slot >= 0 and k.find(b'XNULDRST', slot + 1) < 0
    assert k[slot + 8:slot + 32] == bytes(24)
    struct.pack_into('<3Q', k, slot + 8, 0, 0, delta & ((1 << 64) - 1))
    segs = [s for s in m['segs'] if s['vmsize']]
    end = build.ALIGN(max(s['vmaddr'] + s['vmsize'] for s in segs)) + delta
    system_pa = end
    small_pa = build.ALIGN(system_pa + a.system.stat().st_size)
    adt_pa = build.ALIGN(small_pa + a.root.stat().st_size)
    tree, _ = build.adt.parse(build.device_tree(system_pa, a.system.stat().st_size, True, a.cpus))
    chosen = next(c for c in tree.children if c.name == 'chosen')
    mm = next(c for c in chosen.children if c.name == 'memory-map')
    mm.props.append(('RAMDisk1', 0, build.U64(small_pa, a.root.stat().st_size)))
    dt = build.adt.serialize(tree)
    ba_pa = build.ALIGN(adt_pa + len(dt))
    top = ba_pa + build.PAGE
    assert top + (1 << 30) < build.PHYS + build.MEM
    cmd = (f'-v debug=0x100 keepsyms=1 cpus={a.cpus} rd=md0 serial=3 gpu=0 wifi=0 '
           'touchscreen=0 buttons=0 cpufreq=0 battery=0 adsp=0 cam=0 haptic=0 '
           'torch=0 ufs_write=0 v60=0 epoch=' + str(int(time.time())))
    for arg in a.bootarg:
        if not arg or any(c.isspace() or c == '\0' for c in arg):
            ap.error('--bootarg must be a single token')
        cmd += ' ' + arg
    if len(cmd.encode()) >= 608:
        ap.error('boot arguments exceed shim ABI limit')
    ba = struct.pack('<HH4x4Q6QI4xQI', 2, 2, build.VIRT, build.PHYS, build.MEM, top,
                     0x9c000000, 0, 4320, 1080, 2460, 32, 0, adt_pa - delta, len(dt))
    ba += cmd.encode().ljust(608, b'\0') + bytes(4) + struct.pack('<2Q', 0, build.MEM)
    assert len(ba) == 736
    spec = importlib.util.spec_from_file_location('qemu', build.HERE.parent / 'tools/qemu-sweet.py')
    q = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(q)
    q.FB_H = 2460
    args = ['qemu-system-aarch64', '-M', 'virt,gic-version=3', '-cpu', 'cortex-a76',
            '-smp', str(a.cpus), '-m', '9G', '-nographic', '-serial', f'file:{out}/serial.txt',
            '-qmp', f'unix:{out}/mon.sock,server,nowait', '-no-reboot']

    def load(path, pa):
        args.extend(['-device', f'loader,file={path.resolve()},addr={pa:#x},force-raw=on'])

    for i, s in enumerate(segs):
        if s['filesize']:
            path = out / f'segment{i}.bin'
            path.write_bytes(k[s['fileoff']:s['fileoff'] + s['filesize']])
            load(path, s['vmaddr'] + delta)
    for name, data, pa in [('adt.bin', dt, adt_pa), ('bootargs.bin', ba, ba_pa),
                           ('stub.bin', q.stub(ba_pa, m['entry'] + delta), q.STUB_PA)]:
        path = out / name
        path.write_bytes(data)
        load(path, pa)
    # QEMU's ROM image loader uses a signed int for individual reads. Keep
    # each input below 2GiB, even on a 64-bit host. Remove temporary parts
    # after QEMU has loaded them; never alter the source image.
    parts = []
    if a.system.stat().st_size >= 0x80000000:
        with a.system.open('rb') as src:
            offset = 0
            while offset < a.system.stat().st_size:
                part = out / f'system-part{len(parts)}.bin'
                count = min(0x40000000, a.system.stat().st_size - offset)
                with part.open('wb') as dst:
                    remaining = count
                    while remaining:
                        buf = src.read(min(8 << 20, remaining))
                        assert buf
                        dst.write(buf)
                        remaining -= len(buf)
                parts.append(part)
                load(part, system_pa + offset)
                offset += count
    else:
        load(a.system, system_pa)
    load(a.root, small_pa)
    args.extend(['-device', f'loader,addr={q.STUB_PA:#x},cpu-num=0'])
    metadata = {'kernel_sha256': build.SHA(k), 'system_size': a.system.stat().st_size,
                'root_size': a.root.stat().st_size, 'phys_base': hex(build.PHYS),
                'mem_size': hex(build.MEM), 'system_pa': hex(system_pa),
                'small_pa': hex(small_pa), 'v60_hardware_tested': False}
    (out / 'layout.json').write_text(json.dumps(metadata, indent=2) + '\n')
    p = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + a.seconds
        while p.poll() is None and time.monotonic() < deadline:
            if parts and (out / 'mon.sock').exists():
                try:
                    q.qmp(str(out / 'mon.sock'), 'info status')
                except (OSError, ValueError):
                    pass
                else:
                    for part in parts:
                        part.unlink()
                    parts.clear()
            time.sleep(.2)
        if p.poll() is not None:
            raise RuntimeError(f'QEMU exited: {p.returncode}; inspect serial.txt')
        _, registers = q.qmp(str(out / 'mon.sock'), 'stop', 'info registers', pmemsave=(symbol(k, b'_loader_log') + delta,
              1536 * 1024, str(out / 'kernel-console.bin')))
        (out / 'regs.txt').write_text(registers)
        q.qmp(str(out / 'mon.sock'), pmemsave=(q.FB_BASE, 4320 * 2460, str(out / 'fb.raw')))
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        for part in parts:
            part.unlink(missing_ok=True)
    # OS log strings can themselves contain NUL bytes. Only trim unused tail
    # space, otherwise a valid startup log is silently cut at its first one.
    log = (out / 'kernel-console.bin').read_bytes().rstrip(b'\0').decode(errors='replace')
    (out / 'kernel-console.txt').write_text(log)
    q.write_png(str(out / 'fb.png'), (out / 'fb.raw').read_bytes())
    (out / 'fb.raw').unlink()
    if 'panic(cpu ' in log or 'pid 1 exited' in log:
        raise SystemExit('FAIL: kernel panic/init exit; inspect kernel-console.txt')
    startup = re.search(r'=== (\d+) checks, 0 failed ===', log)
    result = dict(metadata, startup_checks_passed=bool(startup),
                  startup_checks=int(startup[1]) if startup else 0,
                  gui_confirmed=False)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    if not startup:
        raise SystemExit('INCOMPLETE: startup checks not observed; inspect console and registers')
    print('Captured GUI software test; inspect console and screenshot:', out)


if __name__ == '__main__':
    main()
