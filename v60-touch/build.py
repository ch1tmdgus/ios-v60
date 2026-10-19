#!/usr/bin/env python3
"""Replace only the disabled touch module in one exact, symbol-bearing XNU boot image.
Never flashes a device. No support for other kernels or slid kernel images.
"""
import argparse,hashlib,json,os,struct,subprocess
from pathlib import Path
BASE_SHA='6c916223cf86aca1caf13d4e490a20c519dfc7f315671bb494e643a2d31ab816'
START=0xfffffe00074e9168
END=0xfffffe00074eb4d0
IMPORTS={
'kernel_printf':0xfffffe000734984c,'kernel_io_map':0xfffffe0007490300,
'kernel_thread_start':0xfffffe00073794d0,'kernel_thread_deallocate':0xfffffe0007377824,
'kernel_thread_terminate':0xfffffe000737ba94,'kernel_current_thread':0xfffffe000748f4d0,
'kernel_sleep':0xfffffe00079aaed4,'kernel_delay':0xfffffe000731f264,
'kernel_bootarg':0xfffffe0007a7742c,'kernel_hid_touch':0xfffffe0007a6d14c,
}
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('image',type=Path);ap.add_argument('--out',required=True,type=Path);a=ap.parse_args()
 image=a.image.read_bytes()
 if sha(image)!=BASE_SHA:raise SystemExit('REFUSED: input SHA256 does not match the inspected v60-xnu-boot2-gui.img')
 root=Path(__file__).resolve().parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
 ks,_,rs,_,ss,_,_,ps,ver,_=struct.unpack_from('<10I',image,8)
 assert image[:8]==b'ANDROID!' and ps==4096 and ver==2 and ss==0
 assert struct.unpack_from('<I',image,1632)[0]==0
 align=lambda n:(n+ps-1)//ps*ps
 kp=ps;rp=ps+align(ks);dp=rp+align(rs);ds=struct.unpack_from('<I',image,1648)[0]
 kernel=image[kp:kp+ks];ramdisk=image[rp:rp+rs];dtb=image[dp:dp+ds]
 assert ramdisk[:8]==b'SWEETXNU'
 version,nsegs=struct.unpack_from('<II',ramdisk,8);assert version==1 and nsegs==10
 segments=[struct.unpack_from('<4Q',ramdisk,0x2a8+i*32) for i in range(nsegs)]
 seg=next(s for s in segments if s[0]<=START and END<=s[0]+s[3]);patchoff=seg[2]+START-seg[0]
 # Exact baseline hash protects entry, import addresses, module boundary and existing fixups.
 prefix=os.environ.get('CROSS_COMPILE','aarch64-elf-')
 flags=['-Os','-ffreestanding','-nostdlib','-fno-builtin','-fno-pic','-fno-stack-protector',
        '-mgeneral-regs-only','-mstrict-align','-ffixed-x18','-Wall','-Wextra','-Werror']
 for name in ['v60_touch.c','printf_bridge.S']:
  subprocess.run([prefix+'gcc',*flags,'-c',str(root/name),'-o',str(out/(Path(name).stem+'.o'))],check=True)
 script='ENTRY(v60_touch_init)\n'+''.join(f'{k} = 0x{v:x};\n' for k,v in IMPORTS.items())+f'''
SECTIONS {{
 . = 0x{START:x};
 .text : {{ KEEP(*(.text.entry)) *(.text*) }}
 .rodata : {{ *(.rodata*) }}
 .data : {{ *(.data*) }}
 .bss : {{ *(.bss*) *(COMMON) }}
 ASSERT(SIZEOF(.data) == 0, "unexpected global data")
 ASSERT(SIZEOF(.bss) == 0, "unexpected BSS")
 ASSERT(. <= 0x{END:x}, "touch replacement exceeds original module")
 /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }}
}}
'''
 (out/'module.ld').write_text(script)
 subprocess.run([prefix+'ld','-T',str(out/'module.ld'),'--build-id=none','-N',str(out/'v60_touch.o'),str(out/'printf_bridge.o'),'-o',str(out/'touch.elf')],check=True)
 subprocess.run([prefix+'objcopy','-O','binary',str(out/'touch.elf'),str(out/'touch.bin')],check=True)
 code=(out/'touch.bin').read_bytes();assert 0<len(code)<=END-START
 nm=subprocess.check_output([prefix+'nm','-n',str(out/'touch.elf')]).decode();(out/'symbols.txt').write_text(nm)
 assert any(line.split()==[f'{START:016x}','T','v60_touch_init'] for line in nm.splitlines())
 # Ensure no unresolved imports and no dynamic relocations remain.
 assert not subprocess.check_output([prefix+'nm','-u',str(out/'touch.elf')]).strip()
 dis=subprocess.check_output([prefix+'objdump','-d',str(out/'touch.elf')]).decode();(out/'disassembly.txt').write_text(dis)
 payload=bytearray(ramdisk);payload[patchoff:patchoff+len(code)]=code
 # This kernel's HID start() already uses 2460, but touch() retained 2400
 # clamp/edge literals. Correct only the three inspected instructions.
 hid_changes=[]
 for va,old,new in [
  (0xfffffe0007a6cbd8,0x52812be8,0x52800000|(2459<<5)|8),
  (0xfffffe0007a6cbdc,0x71257edf,0x7100001f|(2459<<10)|(22<<5)),
  (0xfffffe0007a6cc9c,0x528125e8,0x52800000|(2411<<5)|8),
 ]:
  sg=next(v for v in segments if v[0]<=va< v[0]+v[3]);off=sg[2]+va-sg[0]
  assert struct.unpack_from('<I',payload,off)[0]==old
  struct.pack_into('<I',payload,off,new)
  hid_changes.append({'va':hex(va),'payload_offset':off,'old':hex(old),'new':hex(new)})
 original_cmd=ramdisk[0x48:0x2a8].split(b'\0')[0].decode()
 manifests=[]
 for name,mode,console in [('gui',1,1),('diag',1,0),('probe',2,0)]:
  p=bytearray(payload)
  cmd=original_cmd.replace('sweetfb_noconsole=1',f'sweetfb_noconsole={console}')+f' v60touch={mode} v60touchdelay=60'
  encoded=cmd.encode();assert len(encoded)<608
  p[0x48:0x2a8]=encoded+bytes(608-len(encoded))
  # No Mach-O layout, ADT, segment descriptor, framebuffer or shim modifications.
  assert p[:0x48]==ramdisk[:0x48] and p[0x2a8:patchoff]==ramdisk[0x2a8:patchoff]
  expected=bytearray(ramdisk)
  expected[0x48:0x2a8]=p[0x48:0x2a8]
  expected[patchoff:patchoff+len(code)]=code
  for change in hid_changes:struct.pack_into('<I',expected,change['payload_offset'],int(change['new'],16))
  assert p==expected
  h=bytearray(image[:ps]);digest=hashlib.sha1()
  for part in [kernel,p,b'',b'',dtb]:digest.update(part);digest.update(struct.pack('<I',len(part)))
  h[576:608]=digest.digest()+bytes(12)
  pad=lambda b:b+bytes(align(len(b))-len(b))
  boot=bytes(h)+pad(kernel)+pad(p)+pad(dtb)
  path=out/f'v60-xnu-touch4-{name}.img';path.write_bytes(boot)
  manifests.append({'file':path.name,'sha256':sha(boot),'cmdline':cmd,'mode':mode})
 report={'baseline_sha256':BASE_SHA,'method':'in-place replacement of disabled touch module; same fixed kernel VA layout',
         'module_va':hex(START),'module_limit':hex(END),'module_size':len(code),'payload_patch_offset':hex(patchoff),
         'hid_literal_changes':hid_changes,
         'original_code_sha256':sha(ramdisk[patchoff:patchoff+len(code)]),'module_sha256':sha(code),
         'preserved':{'shim_sha256':sha(kernel),'dtb_sha256':sha(dtb)},'outputs':manifests}
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
