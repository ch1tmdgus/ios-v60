#!/usr/bin/env python3
"""Plan, or explicitly install, a private APFS sidecar outside V60's HFS root.

Default is read-only. --install writes only the reviewed image extent, never
formats, mounts, changes GPT or touches a boot partition. Keep plans private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

RELATIVE = 18 << 30
BLOCK = 4096
PART = '/dev/block/sda28'


def digest(path):
    sha, crc = hashlib.sha256(), 0
    with path.open('rb') as src:
        while buf := src.read(8 << 20):
            sha.update(buf)
            crc = zlib.crc32(buf, crc)
    return sha.hexdigest(), crc


def connect(serial):
    rows = [x.split() for x in subprocess.check_output(['adb', 'devices'], text=True).splitlines()[1:] if x.strip()]
    rows = [x for x in rows if x[1] in ('device', 'recovery') and (not serial or x[0] == serial)]
    if len(rows) != 1:
        raise SystemExit('Connect exactly one recovery device, or specify --serial')
    return ['adb', '-s', rows[0][0], 'shell', '-T']


def read(adb, path, skip, count, bs=BLOCK):
    # All paths and integer offsets originate inside this script.
    data = subprocess.check_output(adb + [f'dd if={path} bs={bs} skip={skip} count={count} 2>/dev/null'])
    if len(data) != count * bs:
        raise SystemExit('Short recovery read; nothing was written')
    return data


def geometry(adb):
    if not subprocess.check_output(adb + ['id'], text=True).startswith('uid=0'):
        raise SystemExit('Root recovery required')
    mounts = subprocess.check_output(adb + ['cat /proc/mounts'], text=True)
    if any(x in mounts for x in ('/dev/block/sda28 ', '/by-name/userdata ', '/data ')):
        raise SystemExit('userdata is mounted; refusing to proceed')
    path = subprocess.check_output(adb + ['readlink -f /dev/block/bootdevice/by-name/userdata'], text=True).strip()
    if path != PART:
        raise SystemExit('Unexpected userdata mapping')
    size = int(subprocess.check_output(adb + [f'blockdev --getsize64 {PART}'], text=True))
    gpt = read(adb, '/dev/block/sda', 0, 6)
    header = bytearray(gpt[BLOCK:2 * BLOCK])
    if header[:8] != b'EFI PART':
        raise SystemExit('GPT signature mismatch')
    hsize, crc = struct.unpack_from('<II', header, 12)
    header[16:20] = bytes(4)
    if not 92 <= hsize <= BLOCK or zlib.crc32(header[:hsize]) != crc:
        raise SystemExit('GPT header CRC/bounds mismatch')
    elba, count, esize, ecrc = struct.unpack_from('<QIII', header, 72)
    if elba != 2 or not 0 < count <= 128 or esize != 128:
        raise SystemExit('Unsupported GPT layout')
    entries = gpt[2 * BLOCK:2 * BLOCK + count * esize]
    if len(entries) != count * esize or zlib.crc32(entries) != ecrc:
        raise SystemExit('GPT entries CRC mismatch')
    matches = [entries[i*esize:(i+1)*esize] for i in range(count)
               if entries[i*esize+56:(i+1)*esize].decode('utf-16le').split('\0')[0] == 'userdata']
    if len(matches) != 1:
        raise SystemExit('userdata is not unique')
    first, last = struct.unpack_from('<QQ', matches[0], 32)
    if (last - first + 1) * BLOCK != size:
        raise SystemExit('GPT and recovery partition sizes disagree')
    primary = read(adb, PART, 2, 1, 512)
    bsize, total = struct.unpack_from('>II', primary, 40)
    if primary[:2] != b'HX' or bsize != BLOCK or total * bsize != 16 << 30:
        raise SystemExit('Expected existing 16GiB HFSX root; refusing changed geometry')
    backup = read(adb, PART, (total * bsize - 1024) // 512, 1, 512)
    if backup[:2] != b'HX' or struct.unpack_from('>II', backup, 40) != (bsize, total):
        raise SystemExit('HFS backup header geometry mismatch')
    return {'partition': PART, 'partition_bytes': size, 'first_lba': first, 'last_lba': last,
            'gpt_sha256': hashlib.sha256(gpt).hexdigest(), 'hfs_bytes': total * bsize,
            'hfs_header_sha256': hashlib.sha256(primary).hexdigest(),
            'hfs_backup_sha256': hashlib.sha256(backup).hexdigest()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image', type=Path, required=True)
    ap.add_argument('--plan', type=Path, required=True)
    ap.add_argument('--serial')
    ap.add_argument('--backup', action='store_true', help='read-only backup of the proposed sidecar extent')
    ap.add_argument('--install', action='store_true', help='explicit persistent sidecar write; requires existing matching plan')
    a = ap.parse_args()
    with a.image.open('rb') as src:
        first = src.read(BLOCK)
    size = a.image.stat().st_size
    if len(first) != BLOCK or first[32:36] != b'NXSB' or struct.unpack_from('<I', first, 36)[0] != BLOCK or size % BLOCK:
        ap.error('expected a complete 4K APFS image')
    if struct.unpack_from('<Q', first, 40)[0] * BLOCK != size:
        ap.error('APFS container length differs from image length')
    sha, crc = digest(a.image)
    adb = connect(a.serial)
    g = geometry(adb)
    if RELATIVE < g['hfs_bytes'] + (1 << 30) or RELATIVE + size + (1 << 20) > g['partition_bytes']:
        ap.error('image does not fit outside the existing filesystem and footer')
    plan = dict(g, version=1, relative=RELATIVE, image_bytes=size, image_sha256=sha, image_crc32=crc,
                image=str(a.image.resolve()), read_only_plan=True)
    backup_path = a.plan.with_suffix('.tail-backup.bin')
    backup_record = a.plan.with_suffix('.backup.json')
    if a.backup:
        if a.install or not a.plan.exists() or json.loads(a.plan.read_text()) != plan:
            ap.error('--backup requires a matching read-only plan, without --install')
        if backup_path.exists() or backup_record.exists():
            ap.error('preserving existing extent backup')
        if shutil.disk_usage(a.plan.parent).free < size + (512 << 20):
            ap.error('not enough local disk space for backup')
        proc = subprocess.Popen(adb + [f'dd if={PART} bs={BLOCK} skip={RELATIVE // BLOCK} count={size // BLOCK} 2>/dev/null'], stdout=subprocess.PIPE)
        total, saved = 0, hashlib.sha256()
        with backup_path.open('xb') as dst:
            while buf := proc.stdout.read(8 << 20):
                dst.write(buf)
                saved.update(buf)
                total += len(buf)
        if proc.wait() or total != size or geometry(adb) != g:
            raise SystemExit('Incomplete backup; no device write performed')
        backup_record.write_text(json.dumps(dict(plan=plan, backup_sha256=saved.hexdigest()), indent=2) + '\n')
        print('Read-only extent backup completed:', backup_path)
        return
    if not a.install:
        if a.plan.exists():
            ap.error('choose a fresh plan path; previous reviewed plans are preserved')
        a.plan.write_text(json.dumps(plan, indent=2) + '\n')
        print(f'Read-only plan saved: {a.plan}; image {size} bytes at userdata +18GiB; HFS unchanged')
        return
    if not a.plan.exists() or json.loads(a.plan.read_text()) != plan:
        ap.error('reviewed plan no longer matches image/device; no write performed')
    if not backup_path.exists() or not backup_record.exists():
        ap.error('make the read-only --backup before installing')
    backup_info = json.loads(backup_record.read_text())
    if backup_info['plan'] != plan or backup_path.stat().st_size != size or digest(backup_path)[0] != backup_info['backup_sha256']:
        ap.error('extent backup differs from reviewed plan; no write performed')
    with a.image.open('rb') as src:
        subprocess.run(adb + [f'dd of={PART} bs=1048576 seek={RELATIVE // (1 << 20)} conv=notrunc,fsync'],
                       stdin=src, check=True)
    # Read back the exact extent, streaming locally so the recovery needs no
    # hash utility and no temporary copy. Check old volume metadata as well.
    proc = subprocess.Popen(adb + [f'dd if={PART} bs={BLOCK} skip={RELATIVE // BLOCK} count={size // BLOCK} 2>/dev/null'],
                            stdout=subprocess.PIPE)
    verified, received = hashlib.sha256(), 0
    while buf := proc.stdout.read(8 << 20):
        verified.update(buf)
        received += len(buf)
    if proc.wait() or received != size or verified.hexdigest() != sha or geometry(adb) != g:
        raise SystemExit('Sidecar verification failed; do not boot it. Existing filesystem metadata was checked.')
    receipt = a.plan.with_suffix('.installed.json')
    receipt.write_text(json.dumps(dict(plan, read_only_plan=False, readback_verified=True), indent=2) + '\n')
    print('Exact readback SHA256 passed; GPT and both existing HFS headers unchanged:', receipt)


if __name__ == '__main__':
    main()
