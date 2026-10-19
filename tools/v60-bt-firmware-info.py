#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Inspect Qualcomm TLV metadata offline; never print NVM tag values or control hardware.
Layout reference: Rofik kernel drivers/bluetooth/btqca.h at 7eab1a7.
The patch file describes a candidate revision, not the connected controller ROM.
"""
import argparse,json,struct
from pathlib import Path
MAX_SIZE=16*1024*1024

def inspect(data):
 if not 4<=len(data)<=MAX_SIZE:raise ValueError('Invalid TLV file size')
 kind=data[0];size=int.from_bytes(data[1:4],'little')
 if size!=len(data)-4:raise ValueError('TLV payload length mismatch')
 result={'type':kind,'payload_size':size,'controller_revision_verified':False}
 if kind==1:
  if size<24:raise ValueError('Truncated patch header')
  fields=struct.unpack_from('<IIBBBBHHHHI',data,4)
  names=('total_size','data_length','format_version','signature','download_mode','reserved1','product_id','rom_build','patch_version','reserved2','entry')
  result['patch']=dict(zip(names,fields))
  if fields[0]>size or fields[1]>size-24:raise ValueError('Patch internal size exceeds payload')
 elif kind==2:
  pos=4;tags=[]
  while pos<len(data):
   if pos+12>len(data):raise ValueError('Truncated NVM tag header')
   tag,length=struct.unpack_from('<HH',data,pos);pos+=12
   if pos+length>len(data):raise ValueError('Truncated NVM tag payload')
   tags.append({'id':tag,'length':length});pos+=length
  result['tags']=tags
 else:raise ValueError('Unsupported TLV type')
 return result

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('file',type=Path);args=ap.parse_args()
 with args.file.open('rb') as f:data=f.read(MAX_SIZE+1)
 print(json.dumps(inspect(data),indent=2))
if __name__=='__main__':main()
