#!/usr/bin/env python3
"""Boot the --qemu payload with the phone's 1080x2460 video geometry.

--framebuffer removes the emulated UART and serial=3, exercising the phone's
console route. Capture upstream's in-kernel console ring over QMP as well as
the framebuffer. Require our real EL0 PID 1 marker, and reject panics.
QEMU cannot prove any V60 hardware works.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

import build


def symbol(buf, wanted):
    off = 32
    for _ in range(struct.unpack_from('<I', buf, 16)[0]):
        cmd, size = struct.unpack_from('<II', buf, off)
        if cmd == 2:
            symoff, count, stroff, strsize = struct.unpack_from('<4I', buf, off + 8)
            strings = buf[stroff:stroff + strsize]
            for i in range(count):
                ix, _, _, _, va = struct.unpack_from('<IBBHQ', buf, symoff + 16 * i)
                end = strings.find(b'\0', ix)
                if strings[ix:end] == wanted:
                    return va
        off += size
    raise ValueError('kernel symbol absent: ' + repr(wanted))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('payload', type=Path)
    ap.add_argument('kernel', type=Path)
    ap.add_argument('out', type=Path, help='short, fresh /tmp directory (QMP socket path limit)')
    ap.add_argument('--framebuffer', action='store_true')
    a = ap.parse_args()
    out = a.out.resolve()
    if out.exists() or len(str(out / 'mon.sock').encode()) >= 104:
        ap.error('use a short fresh output directory')
    kernel = a.kernel.read_bytes()
    assert build.SHA(kernel) == build.KERNEL_SHA
    p = bytearray(a.payload.read_bytes())
    dt_off, dt_size = struct.unpack_from('<2Q', p, 0x20)
    tree, _ = build.adt.parse(p[dt_off:dt_off + dt_size])
    arm_io = next(c for c in tree.children if c.name == 'arm-io')
    gic = next(c for c in arm_io.children if c.name == 'gic')
    assert gic.prop('reg') == build.U64(0, 0x10000, 0xa0000, 0x100000), 'not a QEMU payload'
    if a.framebuffer:
        arm_io.children = [c for c in arm_io.children if c.name != 'uart0']
        dt = build.adt.serialize(tree)
        assert len(dt) <= dt_size
        p[dt_off:dt_off + len(dt)] = dt
        struct.pack_into('<Q', p, 0x28, len(dt))
        cmd = p[0x48:0x2a8].split(b'\0')[0].replace(b' serial=3', b'')
        p[0x48:0x2a8] = cmd.ljust(608, b'\0')
    out.mkdir(parents=True)
    image = out / 'payload.bin'
    image.write_bytes(p)
    virt, _, _, _, _, phys, _ = struct.unpack_from('<7Q', p, 0x10)
    log_pa = symbol(kernel, b'_loader_log') - virt + phys
    spec = importlib.util.spec_from_file_location('qemu_runner', build.HERE.parent / 'tools/qemu-sweet.py')
    qemu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qemu)
    qemu.FB_H = 2460
    original_image = qemu.build_image

    def text_mode(payload):
        phys, ram, ba, entry = original_image(payload)
        ram = bytearray(ram)
        struct.pack_into('<Q', ram, ba - phys + 48, 0)  # diagnostic V60 shim's v_display
        return phys, bytes(ram), ba, entry

    qemu.build_image = text_mode
    original = qemu.qmp

    def capture(sock, *cmds, **kw):
        original(sock, 'stop', pmemsave=(log_pa, 1536 * 1024, str(out / 'kernel-console.bin')))
        return original(sock, *cmds, **kw)

    qemu.qmp = capture
    sys.argv = [str(spec.origin), str(image), str(out), '--secs', '15']
    code = qemu.main()
    assert code is None, 'QEMU did not complete normally'
    log = (out / 'kernel-console.bin').read_bytes().split(b'\0')[0].decode(errors='replace')
    (out / 'kernel-console.txt').write_text(log)
    assert 'V60 HACKIOS: PID1 PASS' in log, 'PID 1 did not execute successfully'
    assert 'panic(cpu ' not in log, 'kernel panic'
    result = {'status': 'PASS', 'pid1': True, 'apfs_root': True,
        'video_geometry': [1080, 2460, 4320], 'uart_disabled': a.framebuffer,
        'v60_hardware_tested': False}
    (out / 'smoke-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
