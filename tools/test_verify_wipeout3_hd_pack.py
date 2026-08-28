import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "verify_wipeout3_hd_pack.py"
SPEC = importlib.util.spec_from_file_location("verify_wipeout3_hd_pack", MODULE_PATH)
VERIFY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VERIFY)


class HdFontActivationTests(unittest.TestCase):
    def test_complete_inventory_with_no_exclusions_activates_every_font(self):
        missing, excluded, active = VERIFY.classify_font_activation(
            set(VERIFY.FONT_KEYS), set()
        )
        self.assertEqual(missing, [])
        self.assertEqual(excluded, [])
        self.assertEqual(set(active), VERIFY.FONT_KEYS)
        self.assertEqual(len(active), 20)
        self.assertTrue(VERIFY.HELVETICA_KEYS <= set(active))

    def test_old_helvetica_exclusion_reproduces_the_four_key_gap(self):
        _, excluded, active = VERIFY.classify_font_activation(
            set(VERIFY.FONT_KEYS), {"861ad9f1", "d20dee94"}
        )
        self.assertEqual(set(excluded), VERIFY.HELVETICA_KEYS)
        self.assertEqual(len(active), 16)

    def test_helvetica_hashes_map_to_exact_16x_atlas_dimensions(self):
        self.assertEqual(VERIFY.HELVETICA_HD_DIMENSIONS["861ad9f1"], (4096, 512))
        self.assertEqual(VERIFY.HELVETICA_HD_DIMENSIONS["d20dee94"], (4096, 576))


if __name__ == "__main__":
    unittest.main()
