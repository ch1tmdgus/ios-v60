# SPDX-License-Identifier: GPL-2.0-only
import importlib.util,struct,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('v60build',Path(__file__).with_name('build.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ReclaimAuditTest(unittest.TestCase):
 def payload(self,execute=True):
  p=bytearray(0x4200);va=m.START-0x400
  struct.pack_into('<I',p,0,0xfeedfacf);struct.pack_into('<I',p,16,1)
  struct.pack_into('<II16s4Q2I',p,32,0x19,72,b'__TEXT_EXEC',va,0x200,0,0x200,5,5 if execute else 1)
  return p,[(va,0x200,0,0x200),(m.START,0x4000,0x200,0x4000)]
 def test_clean(self):
  p,s=self.payload();r=m.audit_reclaimed_rpmh(p,s,m.END+0x100);self.assertEqual(r['incoming_immediate_branches'],0);self.assertEqual(r['incoming_raw_pointers'],0)
 def test_branches(self):
  for opcode,bits,shift in ((0x14000000,26,0),(0x94000000,26,0),(0x54000000,19,5),(0x34000000,19,5),(0x36000000,14,5)):
   p,s=self.payload();pc=s[0][0]+0x100;delta=(m.END+16-pc)//4
   struct.pack_into('<I',p,0x100,opcode|((delta&((1<<bits)-1))<<shift))
   with self.subTest(opcode=hex(opcode)):
    with self.assertRaisesRegex(ValueError,'Incoming branch'):m.audit_reclaimed_rpmh(p,s,m.END+0x100)
 def test_replaced_internal_call(self):
  p,s=self.payload();pc=m.START+0x100;delta=(m.END+16-pc)//4
  struct.pack_into('<I',p,0x300,0x94000000|(delta&0x3ffffff));m.audit_reclaimed_rpmh(p,s,m.END+0x100)
 def test_raw_pointer(self):
  p,s=self.payload(False);struct.pack_into('<Q',p,0x100,m.END)
  with self.assertRaisesRegex(ValueError,'Raw pointer'):m.audit_reclaimed_rpmh(p,s,m.END+0x100)
 def test_normal_entry_call_allowed(self):
  p,s=self.payload();pc=s[0][0]+0x100;struct.pack_into('<I',p,0x100,0x94000000|((m.START-pc)//4&0x3ffffff));m.audit_reclaimed_rpmh(p,s,m.END+0x100)
if __name__=='__main__':unittest.main()
