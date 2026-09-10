import tempfile
import unittest
from pathlib import Path

from tools.rank_unknown_functions import (
    cataloged_addresses,
    generated_call_counts,
    seed_addresses,
)


class RankUnknownFunctionsTests(unittest.TestCase):
    def test_parses_catalog_seeds_and_generated_calls(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            symbols = root / "symbols.toml"
            symbols.write_text(
                "[[func]]\npc = 0x80100000\nname = 'Known'\n"
                "[[block]]\npc = 0x80100004\nname = 'KnownBlock'\n",
                encoding="utf-8",
            )
            seeds = root / "seeds.txt"
            seeds.write_text("0x80100008\nnoise\n", encoding="utf-8")
            generated = root / "generated"
            generated.mkdir()
            (generated / "a.c").write_text(
                "/* CPS jal -> 0x80100008 */\n"
                "/* CPS jal -> 0x80100008 */\n"
                "/* CPS jal -> 0x8010000C */\n",
                encoding="ascii",
            )

            self.assertEqual(cataloged_addresses(symbols), {0x80100000, 0x80100004})
            self.assertEqual(seed_addresses(seeds), {0x80100008})
            self.assertEqual(
                generated_call_counts(generated),
                {0x80100008: 2, 0x8010000C: 1},
            )


if __name__ == "__main__":
    unittest.main()
