from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]

class SdlColocationTests(unittest.TestCase):
    def test_every_directly_runnable_sdl_target_uses_shared_colocation(self) -> None:
        app = (ROOT / "clean-aot/app/CMakeLists.txt").read_text(encoding="utf-8")
        host = (ROOT / "clean-aot/host/CMakeLists.txt").read_text(encoding="utf-8")
        for target, cmake in {
            "Wipeout3SE": app,
            "clean_aot_app_sdl_tests": app,
            "clean_aot_host_sdl_vulkan_tests": host,
        }.items():
            self.assertIn(f"add_executable({target}", cmake)
            self.assertIn(f"clean_aot_colocate_sdl3({target})", cmake)

    def test_helper_is_post_build_and_target_relative(self) -> None:
        helper = (ROOT / "clean-aot/host/cmake/colocate_sdl3.cmake").read_text(encoding="utf-8")
        self.assertIn("POST_BUILD", helper)
        self.assertIn("$<TARGET_FILE:SDL3::SDL3>", helper)
        self.assertIn("$<TARGET_FILE_DIR:${target}>", helper)
        self.assertIn("copy_if_different", helper)

if __name__ == "__main__":
    unittest.main()
