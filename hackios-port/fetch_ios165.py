#!/usr/bin/env python3
"""Private local iOS16.5 extraction for the pinned HackiOS userland (macOS).
Download only three Apple IPSW members, verify ZIP CRCs and HTTP ranges,
extract one stage at a time, then delete this tool's reproducible DMGs.
Never uploads files or accesses a phone. Keeps >=1.5 GiB disk headroom.
"""
import argparse, importlib.util, json, os, shutil, ssl, struct, subprocess, sys, time
import urllib.request, zlib
from pathlib import Path

REV = '906425b75e02b4f99eb5d7bb1ef7c9583472d772'
URL = ('https://updates.cdn-apple.com/2023SpringFCS/fullrestores/032-85130/'
       '6AB0D10A-8AF9-4F6C-8CF5-A1B315CABE88/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw')
SIZE = 6165698198
CTX = ssl.create_default_context(cafile='/etc/ssl/cert.pem')

def remote(start, end):
    r = urllib.request.urlopen(urllib.request.Request(URL, headers={
        'Range': f'bytes={start}-{end}', 'Accept-Encoding': 'identity'}), context=CTX, timeout=45)
    if r.status != 206 or r.headers.get('Content-Range') != f'bytes {start}-{end}/{SIZE}':
        r.close()
        raise RuntimeError('Apple CDN did not honor the exact requested byte range')
    return r

def entries():
    with remote(SIZE-65536, SIZE-1) as r: tail=r.read()
    at=tail.rfind(b'PK\x06\x06')
    assert at >= 0
    size, off=struct.unpack_from('<QQ',tail,at+40)
    assert 0 < size < 8*1024*1024
    with remote(off, off+size-1) as r: data=r.read()
    result={}; p=0
    while data[p:p+4]==b'PK\x01\x02':
        method=struct.unpack_from('<H',data,p+10)[0]
        crc,cs,us=struct.unpack_from('<III',data,p+16)
        nl,el,cl=struct.unpack_from('<HHH',data,p+28)
        offset=struct.unpack_from('<I',data,p+42)[0]
        name=data[p+46:p+46+nl].decode()
        extra=data[p+46+nl:p+46+nl+el]; q=0
        while q+4<=len(extra):
            tag,n=struct.unpack_from('<HH',extra,q)
            if tag==1:
                vals=list(struct.unpack('<'+'Q'*(n//8),extra[q+4:q+4+(n//8)*8]))
                if us==0xffffffff:us=vals.pop(0)
                if cs==0xffffffff:cs=vals.pop(0)
                if offset==0xffffffff:offset=vals.pop(0)
            q+=4+n
        result[name]={'method':method,'crc':crc,'compressed_size':cs,'size':us,'offset':offset}
        p+=46+nl+el+cl
    return result

def checksum(path):
    crc=0
    with path.open('rb') as f:
        while b:=f.read(4<<20):crc=zlib.crc32(b,crc)
    return crc

def download(meta, target):
    if target.exists() and target.stat().st_size==meta['size'] and checksum(target)==meta['crc']:return
    offset=meta['offset']
    with remote(offset,offset+29) as r: h=r.read()
    assert h[:4]==b'PK\x03\x04'
    nl,el=struct.unpack_from('<HH',h,26); start=offset+30+nl+el
    partial=target.with_suffix(target.suffix+'.part')
    for attempt in range(5):
        have=partial.stat().st_size if partial.exists() and meta['method']==0 else 0
        if have>meta['size']:raise RuntimeError('partial file is oversized')
        try:
            if have<meta['size']:
                first=start+have if meta['method']==0 else start
                inflater=zlib.decompressobj(-15) if meta['method']==8 else None
                if meta['method'] not in (0,8):raise RuntimeError('unsupported ZIP method')
                print(f"download {target.name}: resume {have}/{meta['size']}",flush=True)
                with remote(first,start+meta['compressed_size']-1) as r, partial.open('ab' if have else 'wb') as f:
                    progress=have
                    while b:=r.read(1<<20):
                        if shutil.disk_usage(target.parent).free < 1536*1024*1024:
                            raise RuntimeError('disk headroom below 1.5 GiB; partial download retained')
                        f.write(inflater.decompress(b) if inflater else b)
                        if f.tell()-progress >= 256*1024*1024:
                            progress=f.tell();print(f'{target.name}: {progress//(1<<20)} MiB',flush=True)
                    if inflater:f.write(inflater.flush())
            if partial.stat().st_size!=meta['size'] or checksum(partial)!=meta['crc']:
                raise RuntimeError('IPSW member length/CRC verification failed')
            partial.replace(target);print('verified ZIP CRC:',target.name,flush=True);return
        except (OSError, urllib.error.URLError) as e:
            if attempt==4:raise
            print('retry after network error:',str(e),flush=True);time.sleep(2)
    raise RuntimeError('download incomplete')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--workspace',required=True,type=Path)
    ap.add_argument('--kernel-repo',required=True,type=Path)
    a=ap.parse_args(); w=a.workspace.resolve(); ios=w/'ios165'; k=a.kernel_repo.resolve()
    assert subprocess.check_output(['git','-C',str(w),'rev-parse','HEAD'],text=True).strip()==REV
    seven=shutil.which('7zz') or shutil.which('7z')
    if not seven:raise SystemExit('7-Zip 23+ is required')
    unz=w/'v60-host/unzbm';unz.parent.mkdir(exist_ok=True)
    c=k/'xnu-deps/build/lzbitmap'
    subprocess.run(['cc','-O2','-w','-o',str(unz),str(c/'unzbm.c'),str(c/'libzbitmap.c')],check=True)
    meta=entries(); (ios/'v60-ipsw-members.json').write_text(json.dumps({n:meta[n] for n in
        ['096-04088-077.dmg','096-04666-078.dmg','096-03536-083.dmg']},indent=2)+'\n')
    def run(*args,log=None,allow=False):
        print('run:',str(args[0]),' '.join(map(str,args[1:4])),flush=True)
        with (ios/log).open('w') if log else open(os.devnull,'w') as f:
            r=subprocess.run(list(map(str,args)),cwd=ios,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode and not allow:raise RuntimeError(f'{args[0]} failed: {r.returncode}; {log}')
    def expand(root):
        attrs=list(root.rglob('*:com.apple.decmpfs'))
        for i in range(0,len(attrs),100):
            subprocess.run([str(unz),*[str(x)[:-len(':com.apple.decmpfs')] for x in attrs[i:i+100]]],check=True)
        if list(root.rglob('*:com.apple.decmpfs')):raise RuntimeError('compressed attributes remain')
    if not (ios/'.done-rootfs').exists():
        dmg=ios/'rootfs.dmg';download(meta['096-04088-077.dmg'],dmg)
        run(seven,'l','-slt',dmg,log='rootfs.slt')
        run(sys.executable,'select-system.py','rootfs.slt','system.files','system.manifest',log='select-system.log')
        run(sys.executable,'mkvarskel.py','rootfs.slt',log='var.skel')
        run(seven,'x','-y','-orootfs',dmg,'@system.files',log='extract-system.log',allow=True)
        expand(ios/'rootfs')
        for mode,file,arg in [('emoji','CoreAddition/AppleColorEmoji-160px.ttc','40,64,96'),
            ('faces','LanguageSupport/PingFang.ttc',r'^\.?PingFang(SC|TC|HK)-(Regular|Semibold)$')]:
            p=ios/'rootfs/System/Library/Fonts'/file
            run(sys.executable,w/'tools/font_trim.py',mode,p,p,arg,log='font-'+mode+'.log')
        assert (ios/'rootfs/sbin/launchd').stat().st_size > 0
        (ios/'.done-rootfs').touch();dmg.unlink()
    if not (ios/'.done-cryptex').exists():
        dmg=ios/'cryptex-os.dmg';download(meta['096-04666-078.dmg'],dmg)
        run(seven,'x','-y','-ocryptex',dmg,'-xr!com.apple.dyld','-x!System/DriverKit',log='extract-cryptex.log',allow=True)
        expand(ios/'cryptex')
        run(seven,'x','-y','-ocryptex.cache',dmg,'System/Library/Caches/com.apple.dyld','-xr!*.symbols*',log='extract-dyld.log',allow=True)
        expand(ios/'cryptex.cache')
        cache=ios/'20F66__iPhone10,3_6'
        (ios/'cryptex.cache/System/Library/Caches/com.apple.dyld').rename(cache)
        assert len(list(cache.iterdir()))==43
        (ios/'.done-cryptex').touch();dmg.unlink();shutil.rmtree(ios/'cryptex.cache')
    if not (ios/'.done-cryptex-app').exists():
        dmg=ios/'cryptex-app.dmg';download(meta['096-03536-083.dmg'],dmg)
        run(seven,'x','-y','-ocryptex-app',dmg,log='extract-cryptex-app.log',allow=True)
        expand(ios/'cryptex-app')
        assert (ios/'cryptex-app/usr/libexec/webbookmarksd').stat().st_size>0
        (ios/'.done-cryptex-app').touch()
    print('Private iOS16.5 extraction complete:',ios,flush=True)

if __name__=='__main__':main()
