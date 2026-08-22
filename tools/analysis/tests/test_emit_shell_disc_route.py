import unittest
from tools.analysis.emit_shell_disc_route import build,sha,EmitError
class Tests(unittest.TestCase):
 def fixture(self):
  base=0x80010000; image=bytes(0x100); route={"schema":"clean-aot-shell-disc-route-receipt-v1","resident_image":{"load_start":hex(base),"length":len(image),"sha256":sha(image)},"shell_init_entry":hex(base),"goal_call_site":hex(base+8),"direct_calls":[{"pc":hex(base),"target":hex(base+0x40)},{"pc":hex(base+8),"target":hex(base+0x44)}],"branches":[{"pc":hex(base+0x20),"target":hex(base+0x30),"prerequisite":"taken"}],"finite_returns":[{"pc":hex(base+0x30),"callee":hex(base+0x40),"finite_targets":[hex(base+8),hex(base+0x18)]}],"loops":[{"name":"loop","start":hex(base+0x50),"end_exclusive":hex(base+0x58),"bound":2,"sha256":sha(image[0x50:0x58])}]};return image,route
 def test_deterministic_bounded_cfi_and_no_duplicate_pcs(self):
  image,route=self.fixture();a=build(image,route,4);b=build(image,route,4);self.assertEqual(a,b);self.assertEqual(a["finite_return_cfi"][0]["finite_targets"],[0x80010008,0x80010018]);self.assertTrue(all(v["instruction_count"]<=4 for v in a["variants"]));self.assertEqual(a["policy"]["post_goal_status"],"excluded")
 def test_rejects_image_loop_and_duplicate_cfi_tamper(self):
  image,route=self.fixture()
  for mutate in (lambda r:r["resident_image"].update(sha256="0"*64),lambda r:r["loops"][0].update(sha256="0"*64),lambda r:r["finite_returns"][0].update(finite_targets=[hex(0x80010008)]*2)):
   import copy;bad=copy.deepcopy(route);mutate(bad)
   with self.assertRaises(EmitError):build(image,bad)
if __name__=="__main__":unittest.main()
