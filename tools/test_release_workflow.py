import re
import unittest
from pathlib import Path

WORKFLOW = Path(__file__).parents[1] / ".github/workflows/release.yml"


class ReleaseWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW.read_text(encoding="utf-8")

    def test_concurrency_coalesces_by_ref_across_events(self):
        self.assertIn(
            "group: wse-release-${{ github.ref }}",
            self.text,
        )
        self.assertNotIn("github.run_id", self.text)
        concurrency = self.text.split("concurrency:", 1)[1].split("permissions:", 1)[0]
        self.assertNotIn("github.event_name", concurrency)

    def test_workflow_inputs_are_not_interpolated_in_shell(self):
        run_blocks = re.findall(
            r"(?m)^\s+run: \|\n((?:^\s{10,}.*(?:\n|$))*)", self.text
        )
        self.assertTrue(run_blocks)
        self.assertNotIn("github.event.inputs", "".join(run_blocks))

    def test_freshness_checks_guard_upload_and_publish(self):
        package = self.text.index("- name: Package setup zip")
        packaged_check = self.text.index("- name: Verify packaged release freshness")
        upload = self.text.index("- name: Upload artifact")
        flatten = self.text.index("- name: Flatten zips")
        downloaded_check = self.text.index("- name: Verify downloaded release freshness")
        publish = self.text.index("- name: Publish release")
        self.assertLess(package, packaged_check)
        self.assertLess(packaged_check, upload)
        self.assertLess(flatten, downloaded_check)
        self.assertLess(downloaded_check, publish)
        self.assertEqual(
            self.text.count("python tools/verify_release_freshness.py"), 2
        )

    def test_publish_checkout_includes_recursive_submodules(self):
        release_job = self.text.split("\n  release:\n", 1)[1]
        checkout_step = release_job.split(
            "\n      - name: Download all artifacts", 1
        )[0]
        self.assertIn("uses: actions/checkout@v4", checkout_step)
        self.assertRegex(checkout_step, r"(?m)^          submodules: recursive$")


if __name__ == "__main__":
    unittest.main()
