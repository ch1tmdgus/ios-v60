# SPDX-License-Identifier: GPL-2.0-only
import importlib.util,struct,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('btinfo',Path(__file__).parents[1]/'v60-bt-firmware-info.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def tlv(kind,data):return bytes([kind])+len(data).to_bytes(3,'little')+data
class FirmwareTest(unittest.TestCase):
 def test_patch(self):
  data=struct.pack('<IIBBBBHHHHI',24,0,1,0,3,0,16,512,123,0,0)
  r=m.inspect(tlv(1,data));self.assertEqual(r['patch']['rom_build'],512);self.assertFalse(r['controller_revision_verified'])
 def test_nvm_does_not_expose_values(self):
  data=struct.pack('<HHII',2,6,0,0)+b'secret'
  r=m.inspect(tlv(2,data));self.assertEqual(r['tags'],[{'id':2,'length':6}]);self.assertNotIn('secret',str(r))
 def test_malformed(self):
  for data in (b'',b'\x01\xff\x00\x00',tlv(3,b''),tlv(1,b'1234'),tlv(2,b'123'),tlv(2,struct.pack('<HHII',2,6,0,0)),tlv(1,struct.pack('<IIBBBBHHHHI',100,0,1,0,3,0,16,512,0,0,0))):
   with self.subTest(data=data):
    with self.assertRaises(ValueError):m.inspect(data)
