from __future__ import annotations

import unittest

from tools.product.release_lane import CARD_SIZE, _new_memory_card


class PersistenceAcceptanceTests(unittest.TestCase):
    def test_setup_card_is_canonical_ps1_format(self) -> None:
        image = _new_memory_card()
        self.assertEqual(len(image), CARD_SIZE)
        self.assertEqual(image[:2], b"MC")
        self.assertEqual(image[63 * 128:64 * 128], image[:128])
        for frame in range(36):
            block = image[frame * 128:(frame + 1) * 128]
            checksum = 0
            for byte in block: checksum ^= byte
            self.assertEqual(checksum, 0)


if __name__ == "__main__":
    unittest.main()
