#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Collect QCA6390 reference data via adb. Never flashes or controls a radio.
Optional recovery mounts are read-only (ext4 ro,noload), private /tmp, cleaned up.
Firmware is a PRIVATE input, not a GitHub release asset.
"""
import argparse,hashlib,json,re,shlex,subprocess,tempfile,zipfile
from pathlib import Path,PurePosixPath

FIRMWARE_ROOTS=('/vendor/firmware_mnt/image','/vendor/firmware','/firmware/image','/firmware','/odm/firmware','/system/etc/firmware','/lib/firmware')
PROPS=('ro.product.model','ro.product.device','ro.board.platform','ro.boot.slot_suffix','ro.boot.slot','ro.bootmode','ro.twrp.version','ro.orangefox.version','vendor.qcom.bluetooth.soc','ro.vendor.bluetooth.soc','qcom.bluetooth.soc','persist.vendor.qcom.bluetooth.soc','persist.vendor.bluetooth.soc')
DT_NODES={
 'vendor/bt_qca6390':('compatible','status','qca,bt-reset-gpio','pinctrl-0','qca,bt-vdd-aon-voltage-level','qca,bt-vdd-dig-voltage-level','qca,bt-vdd-rfa1-voltage-level','qca,bt-vdd-rfa2-voltage-level'),
 'soc/qcom,cnss-qca6390@b0000000':('compatible','status','reg','reg-names','wlan-en-gpio','qcom,bt-en-gpio','qcom,sw-ctrl-gpio','qcom,wlan-rc-num','qcom,vdd-wlan-aon-config','qcom,vdd-wlan-dig-config','qcom,vdd-wlan-io-config'),
 'soc/qcom,qup_uart@998000':('compatible','status','reg','clock-names','clocks','pinctrl-0','pinctrl-1','interrupts-extended'),
 'soc/qcom,pcie@1c00000':('compatible','status','reg','reg-names','perst-gpio','qcom,phy-status-offset'),
}
MAX_FILE=16*1024*1024;MAX_TOTAL=96*1024*1024

def safe_path(s):
 return bool(re.fullmatch(r'/[A-Za-z0-9_.,@+/-]+',s)) and '..' not in PurePosixPath(s).parts

def firmware_candidate(path):
 p=PurePosixPath(path);n=p.name.lower()
 if not safe_path(path) or any(x in n for x in ('mac','address','persist','calibration')):return False
 return bool(re.fullmatch(r'bdwlan\.[be][0-9a-f]{2,3}',n)) or bool(re.fullmatch(r'(?:amss(?:20)?|m3|bdwlan[^/]*|rampatch[^/]*|nvm[^/]*|crbtfw[^/]*|crnv[^/]*|qca6390[^/]*)\.(?:bin|mbn|elf|tlv)',n))

def inside(path,roots):
 return safe_path(path) and any(path==r or path.startswith(r.rstrip('/')+'/') for r in roots)

class Adb:
 def __init__(self,serial=None):
  self.prefix=['adb']+(['-s',serial] if serial else [])
 def run(self,command,timeout=20):
  p=subprocess.run([*self.prefix,'exec-out','sh -c '+shlex.quote(command)],capture_output=True,timeout=timeout)
  return p.returncode,p.stdout,p.stderr
 def command(self,*args):return self.run(shlex.join(args))
 def text(self,*args):
  rc,b,_=self.command(*args);return b.decode('utf-8','replace').strip() if rc==0 else ''
 def read(self,path,limit=65536):
  if not safe_path(path):raise ValueError('Unsafe remote path')
  rc,b,_=self.command('head','-c',str(limit+1),path)
  if rc or len(b)>limit:return None
  return b
 def exists(self,path,kind='d'):
  return safe_path(path) and self.command('test','-'+kind,path)[0]==0
 def realpath(self,path):
  v=self.text('readlink','-f',path);return v if safe_path(v) else ''

def slot_suffix(props):
 v=props.get('ro.boot.slot_suffix') or props.get('ro.boot.slot','')
 return '_'+v.lstrip('_') if v.lstrip('_') in ('a','b') else ''

def block_candidates(kind,slot):
 # Never guess an inactive A/B partition. Unsuffixed mapper/by-name is allowed.
 names=([kind+slot] if slot else [])+[kind]
 return [base+'/'+name for base in ['/dev/block/mapper','/dev/block/by-name','/dev/block/bootdevice/by-name'] for name in names]

def mount_options(fs):
 if fs=='ext4':return 'ro,noload'
 if fs in ('erofs','vfat'):return 'ro'
 raise ValueError('Unsupported read-only filesystem')

class RecoveryMounts:
 def __init__(self,adb,report):self.adb=adb;self.report=report;self.mounts=[];self.dirs=[]
 def add(self,kind,slot):
  a=self.adb
  for block in block_candidates(kind,slot):
   if not a.exists(block,'b'):continue
   result=a.text('blkid',block);m=re.search(r'\bTYPE="([a-z0-9]+)"',result)
   if not m:continue
   fs=m.group(1)
   try:options=mount_options(fs)
   except ValueError:continue
   if kind=='vendor' and fs not in ('ext4','erofs'):continue
   if kind=='modem' and fs!='vfat':continue
   dest=a.text('mktemp','-d','/tmp/v60-wireless-XXXXXX')
   if not re.fullmatch(r'/tmp/v60-wireless-[A-Za-z0-9]+',dest):raise RuntimeError('No private recovery tmp directory')
   self.dirs.append(dest)
   # Register before running mount so cleanup also covers an adb timeout.
   self.mounts.append(dest)
   rc,_,_=a.command('mount','-t',fs,'-o',options,block,dest)
   self.report.setdefault('temporary_mounts',[]).append({'kind':kind,'block':block,'filesystem':fs,'options':options,'ok':rc==0})
   if rc==0:return dest
   a.command('umount',dest)
   self.mounts.remove(dest)
  return ''
 def close(self):
  errors=[]
  for p in reversed(self.mounts):
   try:
    if self.adb.command('umount',p)[0]:errors.append('umount '+p)
   except (OSError,subprocess.TimeoutExpired):errors.append('umount '+p)
  for p in reversed(self.dirs):
   try:self.adb.command('rmdir',p)
   except (OSError,subprocess.TimeoutExpired):errors.append('rmdir '+p)
  self.report['cleanup_errors']=errors


def collect(adb,out,include_firmware=False,mount_ro=False):
 if out.exists():raise ValueError('Output already exists; choose a new --out filename')
 report={'format':'v60-wireless-reference-v1','properties':{},'firmware':[],'missing':[],'radio_commands_issued':False}
 for key in PROPS:report['properties'][key]=adb.text('getprop',key)
 report['kernel_release']=adb.text('uname','-r');report['uid']=adb.text('id','-u')
 mounts=RecoveryMounts(adb,report);blobs={};roots=[]
 try:
  for r in FIRMWARE_ROOTS:
   if adb.exists(r):
    real=adb.realpath(r)
    if real and inside(real,FIRMWARE_ROOTS):roots.append(real)
  if mount_ro:
   props=report['properties']
   recovery=props.get('ro.bootmode')=='recovery' or bool(props.get('ro.twrp.version') or props.get('ro.orangefox.version')) or adb.exists('/sbin/recovery','f')
   if report['uid']!='0' or not recovery:raise ValueError('--mount-vendor-ro requires adb-root recovery')
   slot=slot_suffix(props)
   if not adb.exists('/vendor/etc'):
    vendor=mounts.add('vendor',slot)
    if vendor:
     for rel in ('firmware','firmware_mnt/image'):
      r=vendor+'/'+rel
      if adb.exists(r):roots.append(r)
   # modem firmware filesystem may be absent from recovery even with vendor mounted.
   modem=mounts.add('modem',slot)
   if modem:roots.append(modem)
  roots=list(dict.fromkeys(roots));report['firmware_roots_available']=roots
  for base in ('/sys/firmware/devicetree/base','/proc/device-tree'):
   if not adb.exists(base):continue
   for node,keys in DT_NODES.items():
    for key in keys:
     path=base+'/'+node+'/'+key;b=adb.read(path)
     if b is not None:blobs['dt/'+node+'/'+key]=b
   break
  # PCI vendor/device IDs and driver names only; never MAC addresses or traffic.
  rc,listing,_=adb.run('for p in /sys/bus/pci/devices/*; do [ -d "$p" ] && printf "%s\\n" "$p"; done; :')
  pci=[]
  if rc==0:
   for line in listing.decode(errors='replace').splitlines()[:64]:
    if not re.fullmatch(r'/sys/bus/pci/devices/[0-9a-fA-F:.]+',line):continue
    ent={key:adb.text('cat',line+'/'+key) for key in ('vendor','device','subsystem_vendor','subsystem_device','class')}
    driver=adb.realpath(line+'/driver');ent['driver']=PurePosixPath(driver).name if driver else '';pci.append(ent)
  report['pci']=pci
  rc,ports,_=adb.run('for p in /dev/ttyHS* /dev/ttyMSM*; do [ -c "$p" ] && printf "%s\\n" "$p"; done; :')
  report['serial_ports']=[x for x in ports.decode(errors='replace').splitlines() if re.fullmatch(r'/dev/tty(?:HS|MSM)[0-9]+',x)] if rc==0 else []
  report['tty_bindings']=[{'port':p,'of_node':adb.realpath('/sys/class/tty/'+PurePosixPath(p).name+'/device/of_node'),'driver':adb.realpath('/sys/class/tty/'+PurePosixPath(p).name+'/device/driver')} for p in report['serial_ports']]
  seen=set();total=0
  for root in roots:
   rc,listing,_=adb.command('find',root,'-maxdepth','4','-type','f')
   if rc or len(listing)>1024*1024:report['missing'].append(root);continue
   for name in listing.decode(errors='replace').splitlines():
    if not firmware_candidate(name):continue
    real=adb.realpath(name)
    if not real or not inside(real,roots) or real in seen:continue
    seen.add(real)
    if len(seen)>256:raise ValueError('Unexpected number of firmware candidates')
    size_text=adb.text('stat','-c','%s',real)
    if not size_text.isdecimal():continue
    size=int(size_text);entry={'path':real,'size':size,'included':False};report['firmware'].append(entry)
    if not include_firmware:continue
    if size>MAX_FILE or total+size>MAX_TOTAL:entry['skipped']='size limit';continue
    data=adb.read(real,MAX_FILE)
    if data is None or len(data)!=size:entry['skipped']='short/unavailable read';continue
    key='firmware/'+str(len(report['firmware'])).zfill(3)+'-'+PurePosixPath(real).name
    blobs[key]=data;total+=size;entry.update({'included':True,'sha256':hashlib.sha256(data).hexdigest(),'archive_path':key})
 finally:mounts.close()
 out.parent.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
  z.writestr('report.json',json.dumps(report,ensure_ascii=False,indent=2)+'\n')
  for name,data in blobs.items():z.writestr(name,data)
 return report

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True,type=Path);ap.add_argument('--serial');ap.add_argument('--include-firmware',action='store_true');ap.add_argument('--mount-vendor-ro',action='store_true');a=ap.parse_args()
 if not a.serial:
  p=subprocess.run(['adb','devices'],capture_output=True,text=True,timeout=10)
  devices=[line.split() for line in p.stdout.splitlines()[1:] if line.strip()]
  if len(devices)!=1 or devices[0][1]!='device':raise SystemExit('Connect exactly one adb device in recovery, or specify --serial')
 report=collect(Adb(a.serial),a.out,a.include_firmware,a.mount_vendor_ro)
 print('Saved private wireless reference:',a.out)
 print('Firmware candidates:',len(report['firmware']),'included:',sum(x['included'] for x in report['firmware']))
 if not report['firmware']:print('No firmware files found; inspect report.json for missing mounts. No wireless fix image was generated.')
 if report.get('cleanup_errors'):print('Temporary recovery mount cleanup needs attention:',report['cleanup_errors'])
 print('No flash, PMIC write, UART command, Wi-Fi scan or network credentials collected.')
if __name__=='__main__':main()
