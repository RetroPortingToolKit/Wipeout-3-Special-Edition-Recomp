import importlib.util
from pathlib import Path
import unittest
P=Path(__file__).parents[1]/"build_100hz_patch_contract.py"; S=importlib.util.spec_from_file_location("patch_contract",P); m=importlib.util.module_from_spec(S); S.loader.exec_module(m)
class Tests(unittest.TestCase):
 def fixture(self):
  s=bytes(range(16)); t=bytearray(s); t[1]^=1; t[12]^=1; t=bytes(t)
  a={"schema":"wo3se.generated-game-shards.v1","images":[{"name":"main","identity_sha256":m.sha(t),"load_start":0x1000,"load_end_exclusive":0x1010,"ranges":[{"start":0x1000,"end_exclusive":0x1008}]}]}
  c={"schema":"wo3se.static-100hz-patch-input.v1","cpu_retirement_multiplier":{"numerator":9,"denominator":1},"mutations":[{"name":"pal","generated_image":"main","source_key":"s","target_key":"t","source_sha256":m.sha(s),"target_sha256":m.sha(t)}]}; return c,a,{"s":s,"t":t}
 def test_binding_and_native_clocks(self):
  c,a,b=self.fixture(); o=m.build(c,a,b); self.assertEqual((o["mutations"][0]["generated_instruction_word_count"],o["mutations"][0]["not_authorized_as_instruction_word_count"]),(1,1)); self.assertEqual(o["native_device_clocks"]["spu_samples_per_tick"],441); self.assertEqual(o["native_device_clocks"]["cd_sector_schedule_first_four_ticks"],[0,1,1,1]); self.assertFalse(o["claims"]["unique_gameplay_updates_proven"])
 def test_identity_tamper(self):
  c,a,b=self.fixture(); a["images"][0]["identity_sha256"]="0"*64
  with self.assertRaises(m.ContractError): m.build(c,a,b)
 def test_overlap(self):
  c,a,b=self.fixture(); a["images"][0]["ranges"].append({"start":0x1004,"end_exclusive":0x100c})
  with self.assertRaises(m.ContractError): m.build(c,a,b)
 def test_bad_multipliers(self):
  for n,d in ((18,2),(11,1),(0,1)):
   c,a,b=self.fixture(); c["cpu_retirement_multiplier"]={"numerator":n,"denominator":d}
   with self.assertRaises(m.ContractError): m.build(c,a,b)
 def test_unselected_is_incomplete(self):
  c,a,b=self.fixture(); c["cpu_retirement_multiplier"]=None; self.assertEqual(m.build(c,a,b)["status"],"incomplete-fail-closed")
if __name__=="__main__": unittest.main()
