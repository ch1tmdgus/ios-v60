#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import importlib.util,json,subprocess,tempfile,unittest,zipfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('collector',Path(__file__).parents[1]/'v60-wireless-collect.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class FakeAdb:
 def __init__(self):self.calls=[];self.timeout_mount=False;self.fail_unmount=False;self.oversize=False
 def text(self,*args):
  self.calls.append(args)
  if args[0]=='getprop':return {'ro.boot.slot_suffix':'_b','ro.bootmode':'recovery','ro.product.device':'timelm'}.get(args[1],'')
  if args==('id','-u'):return '0'
  if args==('uname','-r'):return '4.19-test'
  if args[0]=='blkid':return 'TYPE="ext4"' if 'vendor_b' in args[1] else 'TYPE="vfat"'
  if args[0]=='mktemp':return '/tmp/v60-wireless-'+('VENDOR1' if sum(x[0]=='mktemp' for x in self.calls)==1 else 'MODEM1')
  if args[0]=='stat':return str(m.MAX_FILE+1 if self.oversize else 4)
  return ''
 def exists(self,path,kind='d'):
  if kind=='b':return path in ('/dev/block/by-name/vendor_b','/dev/block/by-name/modem_b')
  return path in ('/sys/firmware/devicetree/base','/tmp/v60-wireless-VENDOR1/firmware')
 def realpath(self,path):return path
 def read(self,path,limit=65536):
  self.calls.append(('read',path))
  if path.endswith('compatible'):return b'qcom,cnss-qca6390\0'
  if path.endswith(('.bin','.tlv')):return b'abcd'
  return None
 def run(self,command,timeout=20):self.calls.append(('run',command));return 1,b'',b''
 def command(self,*args):
  self.calls.append(args)
  if args[0]=='mount' and self.timeout_mount:raise subprocess.TimeoutExpired('adb',20)
  if args[0]=='umount' and self.fail_unmount:return 1,b'',b''
  if args[0]=='find':
   if args[1].endswith('MODEM1'):return 0,b'/tmp/v60-wireless-MODEM1/qca6390/amss20.bin\n/tmp/v60-wireless-MODEM1/crbtfw32.tlv\n/tmp/v60-wireless-MODEM1/wlan_mac.bin\n/tmp/v60-wireless-MODEM1/evil;name.bin\n',b''
   return 0,b'',b''
  return 0,b'',b''

class CollectorTest(unittest.TestCase):
 def test_paths(self):
  for path in ('/persist/nvm.bin','/vendor/firmware/../nvm.bin','/vendor/firmware/evil;name.bin'):
   self.assertFalse(m.inside(path,m.FIRMWARE_ROOTS))
  self.assertFalse(m.firmware_candidate('/vendor/firmware/wlan_mac.bin'))
  self.assertFalse(m.firmware_candidate('/vendor/etc/wifi/wpa_supplicant.conf'))
  self.assertTrue(m.firmware_candidate('/vendor/firmware/qca6390/amss20.bin'))
  self.assertTrue(m.firmware_candidate('/vendor/firmware/qca6390/bdwlan.b30'))
 def test_mount_options(self):
  self.assertEqual(m.mount_options('ext4'),'ro,noload')
  self.assertEqual(m.mount_options('vfat'),'ro')
  with self.assertRaises(ValueError):m.mount_options('f2fs')
 def test_slot(self):
  self.assertEqual(m.slot_suffix({'ro.boot.slot_suffix':'_b'}),'_b')
  self.assertTrue(all(not x.endswith('_a') for x in m.block_candidates('vendor','_b')))
  self.assertTrue(all(not x.endswith(('_a','_b')) for x in m.block_candidates('vendor','')))
 def test_private_collection(self):
  adb=FakeAdb()
  with tempfile.TemporaryDirectory() as d:
   out=Path(d)/'dump.zip';r=m.collect(adb,out,True,True)
   self.assertEqual(len(r['firmware']),2);self.assertTrue(all(x['included'] for x in r['firmware']))
   self.assertFalse(r['cleanup_errors'])
   with zipfile.ZipFile(out) as z:
    self.assertIsNone(z.testzip());self.assertEqual(len([n for n in z.namelist() if n.startswith('firmware/')]),2)
    self.assertFalse(json.loads(z.read('report.json'))['radio_commands_issued'])
   self.assertEqual(len([x for x in adb.calls if x[0]=='umount']),2)
   opts=[x[4] for x in adb.calls if x[0]=='mount'];self.assertEqual(opts,['ro,noload','ro'])
   self.assertFalse(any('wlan_mac.bin' in x[-1] for x in adb.calls if x[0]=='read'))
 def test_timeout_cleanup(self):
  adb=FakeAdb();adb.timeout_mount=True
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(subprocess.TimeoutExpired):m.collect(adb,Path(d)/'dump.zip',True,True)
  self.assertIn(('umount','/tmp/v60-wireless-VENDOR1'),adb.calls)
 def test_no_firmware_bytes_without_flag(self):
  adb=FakeAdb()
  with tempfile.TemporaryDirectory() as d:
   r=m.collect(adb,Path(d)/'dump.zip',False,True)
   self.assertEqual(len(r['firmware']),2);self.assertFalse(any(x['included'] for x in r['firmware']))
   self.assertFalse(any(x[0]=='read' and x[-1].endswith(('.bin','.tlv')) for x in adb.calls))
 def test_size_limit(self):
  adb=FakeAdb();adb.oversize=True
  with tempfile.TemporaryDirectory() as d:
   r=m.collect(adb,Path(d)/'dump.zip',True,True)
   self.assertTrue(all(x['skipped']=='size limit' for x in r['firmware']))
 def test_existing_output(self):
  adb=FakeAdb()
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'dump.zip';p.write_bytes(b'keep')
   with self.assertRaises(ValueError):m.collect(adb,p,True,True)
   self.assertEqual(p.read_bytes(),b'keep');self.assertFalse(adb.calls)
 def test_reject_nonrecovery(self):
  adb=FakeAdb();original=adb.text
  adb.text=lambda *args: 'normal' if args==('getprop','ro.bootmode') else original(*args)
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.collect(adb,Path(d)/'dump.zip',True,True)
  self.assertFalse(any(x[0]=='mount' for x in adb.calls))
 def test_symlink_escape(self):
  adb=FakeAdb();adb.realpath=lambda p:'/persist/nvm.bin' if p.endswith(('.bin','.tlv')) else p
  with tempfile.TemporaryDirectory() as d:
   r=m.collect(adb,Path(d)/'dump.zip',True,True)
   self.assertFalse(r['firmware'])
   self.assertFalse(any(x[0]=='read' and x[-1].endswith(('.bin','.tlv')) for x in adb.calls))
 def test_cleanup_error_visible(self):
  adb=FakeAdb();adb.fail_unmount=True
  with tempfile.TemporaryDirectory() as d:
   r=m.collect(adb,Path(d)/'dump.zip',True,True)
   self.assertEqual(len(r['cleanup_errors']),2)
if __name__=='__main__':unittest.main()
