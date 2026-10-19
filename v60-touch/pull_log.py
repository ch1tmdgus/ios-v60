#!/usr/bin/env python3
"""Read an existing 4 MiB XNU log ring via adb root/recovery; never writes the phone."""
import argparse,re,struct,subprocess
from pathlib import Path

def call(*args):return subprocess.check_output(['adb',*args])
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True,type=Path);a=ap.parse_args()
 dev=None
 for candidate in ['/dev/block/by-name/userdata','/dev/block/bootdevice/by-name/userdata']:
  try:
   p=call('shell','readlink','-f',candidate).decode().strip()
   if re.fullmatch(r'/dev/block/[A-Za-z0-9/_-]+',p):dev=p;break
  except subprocess.CalledProcessError:pass
 if not dev:raise SystemExit('userdata unavailable; use the existing recovery with adb root')
 size=int(call('shell','blockdev','--getsize64',dev).decode().strip())
 if size<=1025*4096 or size%4096:raise SystemExit('unexpected partition size')
 skip=size//4096-1025
 area=call('exec-out',f'dd if={dev} bs=4096 skip={skip} count=1025 2>/dev/null')
 if len(area)!=1025*4096:raise SystemExit('short read; no log written')
 magic,version,ring_size,total,seq=struct.unpack_from('<8sIIQQ',area)
 if magic!=b'SWEETLOG' or version!=1 or ring_size!=4*1024*1024:raise SystemExit('expected kernel log header not found; no log written')
 ring=area[4096:4096+ring_size]
 data=ring[:total] if total<=ring_size else ring[total%ring_size:]+ring[:total%ring_size]
 a.out.write_text(data.decode('utf-8','replace').replace('\r',''))
 print(f'Read-only log saved: {a.out}; flush={seq}; V60 input / sampler lines:')
 for line in a.out.read_text().splitlines():
  if any(tag in line for tag in ['V60 TOUCH','SWEET KEYS','SMP ---','SMP Preferences']):print(line)
if __name__=='__main__':main()
