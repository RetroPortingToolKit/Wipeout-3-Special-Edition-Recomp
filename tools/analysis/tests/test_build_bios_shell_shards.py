import importlib.util,hashlib,tempfile,unittest
from pathlib import Path
P=Path(__file__).parents[1]/"build_bios_shell_shards.py"; S=importlib.util.spec_from_file_location("shell",P); m=importlib.util.module_from_spec(S); S.loader.exec_module(m)
class Tests(unittest.TestCase):
 def fixture(self):
  # jal 0x1010; nop; jr ra; nop; jr t0 (unresolved callback); nop
  words=[0x0c000402,0,0x03e00008,0,0x01000008,0]; image=b''.join(x.to_bytes(4,'little') for x in words)
  td=tempfile.TemporaryDirectory(); path=Path(td.name)/"f.manifest"; path.write_text(f"schema=clean-aot-compiler-manifest-v1\nimage_kind=raw-payload\nvariant_sha256={hashlib.sha256(image).hexdigest()}\nimage_sha256={hashlib.sha256(image).hexdigest()}\nload_start=0x1000\nload_end_exclusive=0x1018\nentry=0x1000\nentry=0x1010\ncode=0x1000-0x1018\n")
  return td,image,[(path,m.parse_manifest(path))]
 def test_finite_returns_and_fail_closed_callback(self):
  td,image,items=self.fixture()
  try:
   out=m.build(image,0x1000,items); self.assertEqual(out["finite_return_cfi"][0]["finite_targets"],[0x1008]); self.assertIn(0x1010,[x["pc"] for x in out["unresolved_indirect_sites"]]); self.assertFalse(out["policy"]["runtime_decode_or_compile"])
  finally: td.cleanup()
 def test_tampered_slice_rejected(self):
  td,image,items=self.fixture()
  try:
   with self.assertRaises(m.ShellShardError): m.build(image[:-1]+b'x',0x1000,items)
  finally: td.cleanup()
 def test_epoch_variants_deduplicate_exact_identity(self):
  td,image,items=self.fixture()
  try:
   path,data=items[0]; blob=Path(td.name)/"slice.bin"; blob.write_bytes(image)
   out=m.epoch_variants([(path,blob),(path,blob)]); self.assertEqual(len(out),1); self.assertEqual(out[0]["unknown_identity_action"],"reject")
  finally: td.cleanup()
if __name__=="__main__": unittest.main()
