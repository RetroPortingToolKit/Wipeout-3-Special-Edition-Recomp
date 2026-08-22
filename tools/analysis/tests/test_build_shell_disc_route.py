import copy
import json
import struct
import unittest

from tools.analysis.build_shell_disc_route import RouteError, build, sha256


class ShellDiscRouteTests(unittest.TestCase):
    def fixture(self):
        base = 0x80010000
        image = bytearray(0x80)
        struct.pack_into("<I", image, 0x00, 0x0C004010)  # jal 0x80010040
        struct.pack_into("<I", image, 0x04, 0)
        struct.pack_into("<I", image, 0x10, 0x10000003)  # b 0x80010020
        struct.pack_into("<I", image, 0x14, 0)
        struct.pack_into("<I", image, 0x40, 0x03E00008)
        struct.pack_into("<I", image, 0x44, 0)
        raw = b'{"frontier":"fixture"}\n'
        frontier = {"schema": "clean-aot-bios-shell-exact-frontiers-v1",
                    "last_frontier": 239, "terminal_pc": "0x80010010",
                    "terminal_ram_epoch_sha256": "a" * 64}
        spec = {"schema": "clean-aot-shell-disc-route-spec-v1",
                "resident_image_sha256": sha256(image), "load_start": hex(base),
                "frontier": {"number": 239, "terminal_pc": "0x80010010",
                             "ram_epoch_sha256": "a" * 64},
                "shell_init_entry": "0x80010000", "goal_call_site": "0x80010000",
                "direct_calls": [{"pc": "0x80010000", "target": "0x80010040", "role": "fixture"}],
                "branches": [{"pc": "0x80010010", "target": "0x80010020", "prerequisite": "fixture"}],
                "finite_returns": [{"pc": "0x80010040", "callee": "0x80010040",
                                    "call_sites": ["0x80010000"]}],
                "loops": [{"name": "fixture", "start": "0x80010010",
                           "end_exclusive": "0x80010018", "bound": 1,
                           "exit_prerequisite": "fixture"}],
                "ordered_top_level_calls": [], "unresolved_prerequisites": []}
        return bytes(image), spec, frontier, raw

    def test_build_is_deterministic_and_binds_delay_slots(self):
        image, spec, frontier, raw = self.fixture()
        self.assertEqual(build(image, spec, frontier, raw), build(image, spec, frontier, raw))
        receipt = build(image, spec, frontier, raw)
        self.assertEqual(receipt["direct_calls"][0]["return_pc"], "0x80010008")
        self.assertEqual(receipt["finite_returns"][0]["finite_targets"], ["0x80010008"])

    def test_rejects_identity_edge_and_frontier_mutation(self):
        image, spec, frontier, raw = self.fixture()
        mutated = bytearray(image); mutated[0] = 0
        with self.assertRaises(RouteError): build(bytes(mutated), spec, frontier, raw)
        bad = copy.deepcopy(spec); bad["resident_image_sha256"] = sha256(mutated)
        with self.assertRaises(RouteError): build(bytes(mutated), bad, frontier, raw)
        bad_frontier = dict(frontier); bad_frontier["last_frontier"] = 240
        with self.assertRaises(RouteError): build(image, spec, bad_frontier, raw)


if __name__ == "__main__":
    unittest.main()
