#!/usr/bin/env python3
"""Build a V60 HackiOS first-boot image; never accesses a phone.

Use the proven V60 shim (one guarded text-mode store change)/DTB, upstream's pinned plain-arm64 kernel,
and a tiny APFS root containing our own static PID 1. No iOS root is mixed
with the older kernel's root. The --qemu variant changes GIC/UART/CPU only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
import adt
import mkpayload

BASE_SHA = '6c916223cf86aca1caf13d4e490a20c519dfc7f315671bb494e643a2d31ab816'
KERNEL_SHA = '9239d4adb7a2c433d7ed5729b47b63acc5281af73d8a81640ef3c678c8afd26c'
APFS_SHA = 'c14a81ae9286c69a77673552fcf240de84d37e8fde16f617b2d2efc84d758bdc'
KERNEL_COMMIT = '8e61187098f2c26c5daee8f9f72b6416aa64b9ab'
USERLAND_COMMIT = '906425b75e02b4f99eb5d7bb1ef7c9583472d772'
VIRT = 0xfffffff004000000
PHYS, MEM = 0xb0000000, 0x4d000000
PAGE = 0x4000                 # shim's allocation alignment; XNU itself uses 4K
U32 = lambda *x: struct.pack('<%dI' % len(x), *x)
U64 = lambda *x: struct.pack('<%dQ' % len(x), *x)
CSTR = lambda x: x.encode() + b'\0'
SHA = lambda b: hashlib.sha256(b).hexdigest()
ALIGN = lambda n, a=PAGE: (n + a - 1) // a * a


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, CSTR(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def nvram():
    # Same CHRP format as the upstream loader, in RAM only.
    b = bytearray(0x2000)
    for off, sig, size, name in [(0, 0x5a, 0x20, b'nvram'), (0x20, 0x70, 0x1fe0, b'common')]:
        b[off] = sig
        struct.pack_into('<H', b, off + 2, size // 16)
        b[off + 4:off + 4 + len(name)] = name
        s = sum(b[off:off + 16])
        while s > 255:
            s = (s & 255) + (s >> 8)
        b[off + 1] = s
    struct.pack_into('<I', b, 20, 1)
    struct.pack_into('<I', b, 16, zlib.adler32(b[20:]))
    return bytes(b)


def device_tree(rd_pa, rd_size, qemu=False):
    io = 0x08000000 if qemu else 0x00400000
    gd, gr = (0x08000000, 0x080a0000) if qemu else (0x17a00000, 0x17a60000)
    freq = 62500000 if qemu else 19200000
    chosen = node('chosen', [
        ('dram-base', U64(PHYS)), ('dram-size', U64(MEM)),
        ('random-seed', hashlib.sha512(b'V60 HackiOS first boot diagnostic seed').digest()),
        ('debug-enabled', U32(1)), ('psci-method', CSTR('hvc' if qemu else 'smc')),
        ('firmware-version', CSTR('v60-hackios-shim2')),
        ('display-scale', U32(3)), ('display-rotation', U32(0)),
        ('nvram-bank-size', U32(0x2000)), ('nvram-bank-count', U32(1)),
        ('nvram-current-bank', U32(0)), ('nvram-proxy-data', nvram()),
    ], [node('memory-map', [('RAMDisk', U64(rd_pa, rd_size))])])
    cpu = node('cpu0', [
        ('device_type', CSTR('cpu')), ('state', CSTR('running')), ('reg', U32(0)),
        ('cpu-id', U32(0)), ('timebase-frequency', U64(freq)), ('fixed-frequency', U64(freq)),
        ('clock-frequency', U64(1804800000)), ('bus-frequency', U64(100000000)),
        ('memory-frequency', U64(1866000000)), ('peripheral-frequency', U64(freq)),
    ])
    children = [node('gic', [
        ('device_type', CSTR('interrupt-controller')), ('compatible', CSTR('arm,gic-v3')),
        ('reg', U64(gd - io, 0x10000, gr - io, 0x100000)),
    ])]
    if qemu:
        children.append(node('uart0', [('compatible', CSTR('arm,pl011')),
            ('device_type', CSTR('uart')), ('reg', U64(0x09000000 - io, 0x1000)),
            ('boot-console', U32(1))]))
    return adt.serialize(node('device-tree', [
        ('model', CSTR('LG-V60-HackiOS-bringup')), ('compatible', CSTR('lge,timelm') + CSTR('AppleARM')),
        ('#address-cells', U32(2)), ('#size-cells', U32(2)),
    ], [chosen, node('defaults'), node('options'), node('cpus', children=[cpu]),
        node('arm-io', [('device_type', CSTR('v60-io')), ('ranges', U64(0, io, 0x40000000 - io)),
            ('#address-cells', U32(2)), ('#size-cells', U32(2))], children)]))


def payload(kernel, root, qemu):
    m = mkpayload.parse_macho(kernel)
    assert (m['cputype'], m['cpusub'], m['fixups']) == (0x100000c, 0, None)
    segs = [s for s in m['segs'] if s['vmsize']]
    assert segs[0]['vmaddr'] == VIRT + 0x3004000
    delta = (PHYS - VIRT) & ((1 << 64) - 1)
    kernel = bytearray(kernel)
    # Upstream loader patches this slot: callbacks absent on the direct shim
    # route, but its physical/virtual delta must still be supplied.
    slot = kernel.find(b'XNULDRST')
    assert slot >= 0 and slot % 8 == 0 and kernel.find(b'XNULDRST', slot + 1) == -1
    assert kernel[slot + 8:slot + 32] == bytes(24)
    struct.pack_into('<3Q', kernel, slot + 8, 0, 0, delta)
    end = ALIGN(max(s['vmaddr'] + s['vmsize'] for s in segs))
    rd_va, rd_size = end, ALIGN(len(root))
    tree = device_tree(rd_va - VIRT + PHYS, len(root), qemu)
    end += rd_size
    assert end - VIRT + ALIGN(len(tree)) + PAGE + 0x4000000 < MEM
    pieces = []
    previous = VIRT
    for s in segs:
        assert s['vmaddr'] >= previous and s['filesize'] <= s['vmsize']
        assert s['fileoff'] + s['filesize'] <= len(kernel)
        previous = s['vmaddr'] + s['vmsize']
        pieces.append((s['vmaddr'], s['vmsize'], kernel[s['fileoff']:s['fileoff'] + s['filesize']]))
    assert any(s['vmaddr'] <= m['entry'] < s['vmaddr'] + s['filesize'] for s in segs)
    pieces.append((rd_va, rd_size, root))
    b = bytearray(0x2a8 + len(pieces) * 32)
    for i, (va, size, data) in enumerate(pieces):
        off = len(b)
        b += data
        b += bytes(ALIGN(len(b), 16) - len(b))
        struct.pack_into('<4Q', b, 0x2a8 + i * 32, va, size, off, len(data))
    dt_off = len(b)
    b += tree
    b += bytes(ALIGN(len(b), 8) - len(b))  # shim copy64 may read the last full word
    struct.pack_into('<8sII7Q', b, 0, b'SWEETXNU', 1, len(pieces), VIRT, m['entry'],
        dt_off, len(tree), end, PHYS, MEM)
    cmd = '-v debug=0x100 keepsyms=1 systrace=1 cpus=1 rd=md0 gpu=0 wifi=0 touchscreen=0 buttons=0 cpufreq=0 battery=0 adsp=0 cam=0 haptic=0 torch=0 ufs_write=0'
    if qemu:
        cmd += ' serial=3'
    assert len(cmd.encode()) < 608
    b[0x48:0x48 + len(cmd)] = cmd.encode()
    return bytes(b), {'cmdline': cmd, 'return_slot_file_offset': slot, 'ramdisk_pa': hex(rd_va - VIRT + PHYS),
        'virt_base': hex(VIRT), 'phys_base': hex(PHYS), 'mem_size': hex(MEM), 'entry_va': hex(m['entry'])}


def repack(base, p):
    assert SHA(base) == BASE_SHA, 'baseline differs from the proven V60 image'
    ks, _, rs, _, ss, _, _, ps, version, _ = struct.unpack_from('<10I', base, 8)
    assert base[:8] == b'ANDROID!' and ps == 4096 and version == 2 and ss == 0
    assert struct.unpack_from('<I', base, 1632)[0] == 0
    ds = struct.unpack_from('<I', base, 1648)[0]
    kernel = bytearray(base[ps:ps + ks])
    old_shim_sha = SHA(kernel)
    # boot_args.Video.v_display at +48: str x0,[x20,#48] -> str xzr,[x20,#48].
    # x0 is 1 here; leave the register and every other store unchanged. New XNU's
    # PE_create_console selects text only with v_display=0 (upstream -v route).
    assert struct.unpack_from('<I', kernel, 0x26a8)[0] == 0xf9001a80
    struct.pack_into('<I', kernel, 0x26a8, 0xf9001a9f)
    off = ps + ALIGN(ks, ps) + ALIGN(rs, ps)
    dtb = base[off:off + ds]
    assert len(dtb) == ds and dtb[:4] == b'\xd0\x0d\xfe\xed'
    h = bytearray(base[:ps])
    struct.pack_into('<I', h, 16, len(p))
    digest = hashlib.sha1()
    for part in [kernel, p, b'', b'', dtb]:
        digest.update(part)
        digest.update(U32(len(part)))
    h[576:608] = digest.digest() + bytes(12)
    pad = lambda b: b + bytes(ALIGN(len(b), ps) - len(b))
    return bytes(h) + pad(kernel) + pad(p) + pad(dtb), {
        'original_shim_sha256': old_shim_sha, 'shim_sha256': SHA(kernel), 'dtb_sha256': SHA(dtb),
        'shim_change': {'offset': '0x26a8', 'old': '0xf9001a80', 'new': '0xf9001a9f',
                        'effect': 'boot_args.Video.v_display=0 (text console)'}}


def main():
    if not __debug__:
        raise SystemExit('Do not run this guarded builder with python -O')
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline', required=True, type=Path)
    ap.add_argument('--kernel-repo', required=True, type=Path)
    ap.add_argument('--userland-repo', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--qemu', action='store_true')
    a = ap.parse_args()
    for repo, commit in [(a.kernel_repo, KERNEL_COMMIT), (a.userland_repo, USERLAND_COMMIT)]:
        assert subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() == commit
    k = (a.kernel_repo / 'prebuilt/mach.development.bcm2837').read_bytes()
    assert SHA(k) == KERNEL_SHA, 'upstream prebuilt kernel differs'
    assert SHA((a.userland_repo / 'tools/apfs/mkapfs.py').read_bytes()) == APFS_SHA
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    root = out / 'root'
    if root.exists():
        raise SystemExit('Choose a fresh output directory; refusing to include stale root files')
    (root / 'sbin').mkdir(parents=True, exist_ok=True)
    (root / 'dev').mkdir(exist_ok=True)
    subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios16.5', '-isysroot', '/', '-c', str(HERE / 'init.S'), '-o', str(out / 'init.o')], check=True)
    subprocess.run(['xcrun', 'ld', '-arch', 'arm64', '-static', '-e', '_start', '-platform_version', 'ios', '16.5', '16.5',
        '-o', str(root / 'sbin/launchd'), str(out / 'init.o')], check=True)
    subprocess.run([sys.executable, str(a.userland_repo / 'tools/apfs/mkapfs.py'), '-o', str(out / 'root.apfs'),
        'V60Bringup=' + str(root)], check=True)
    p, info = payload(k, (out / 'root.apfs').read_bytes(), a.qemu)
    (out / 'payload.bin').write_bytes(p)
    info.update({'kernel_commit': KERNEL_COMMIT, 'userland_commit': USERLAND_COMMIT,
        'kernel_method': 'pinned upstream prebuilt + loader delta slot; no kernel source rebuild',
        'builder_source_sha256': SHA(Path(__file__).read_bytes()),
        'init_source_sha256': SHA((HERE / 'init.S').read_bytes()),
        'apfs_builder_sha256': APFS_SHA, 'root_apfs_sha256': SHA((out / 'root.apfs').read_bytes()),
        'kernel_sha256': SHA(k), 'payload_sha256': SHA(p), 'qemu': a.qemu,
        'stage': 'diagnostic PID1; not iOS GUI', 'device_tested': False})
    if not a.qemu:
        image, preserved = repack(a.baseline.read_bytes(), p)
        path = out / 'v60-hackios-boot2-diag.img'
        path.write_bytes(image)
        info.update(preserved)
        info.update({'image': path.name, 'image_sha256': SHA(image), 'image_size': len(image)})
    (out / 'manifest.json').write_text(json.dumps(info, indent=2) + '\n')
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
