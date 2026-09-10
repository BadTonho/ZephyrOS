import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner as parallel
from tools import qemu_test_runner as qemu


ROOT = Path(__file__).resolve().parents[2]


class Sec6RunnerTests(unittest.TestCase):
    def load_catalog(self):
        return json.loads((ROOT / "tests" / "catalog.json").read_text(
            encoding="utf-8"))

    def test_sec6_cases_are_snapshot_isolated_and_tagged(self):
        catalog = self.load_catalog()
        cases = [case for case in catalog["cases"]
                 if "sec6" in case.get("tags", [])]
        self.assertGreaterEqual(len(cases), 4)
        self.assertTrue(all(case["isolation"] == "snapshot" for case in cases))
        self.assertTrue(all("adversarial" in case["tags"] for case in cases))
        self.assertEqual(len({case["id"] for case in cases}), len(cases))

    def test_sec6_tag_selection_is_explicit(self):
        arguments = parallel.parser().parse_args([
            "parallel", "--tag", "sec6", "--workers", "4",
        ])
        selected = parallel.select_cases(self.load_catalog(), arguments,
                                         "parallel")
        self.assertTrue(selected)
        self.assertTrue(all("sec6" in case.get("tags", [])
                            for case in selected))

    def test_no_vesa_case_uses_serial_qmp_and_no_framebuffer(self):
        case = next(case for case in self.load_catalog()["cases"]
                    if case["id"] == "qemu:tst6:sec6:no-vesa")
        self.assertEqual(case["qemu_profile"], "no-vesa")
        arguments = SimpleNamespace(
            image="build/zephyros.img", qemu="qemu-system-i386", cpu="max",
            snapshot=True, network="none", qemu_profile="no-vesa",
            qemu_arg=[], storage_image=None,
        )
        command = qemu.QemuSession(arguments, ROOT / "build").command()
        self.assertIn("-serial", command)
        self.assertIn("-qmp", command)
        self.assertIn("-vga", command)
        self.assertIn("none", command)

    def test_invalid_fixture_and_profile_remain_blocked(self):
        with self.assertRaises(qemu.RunnerError):
            qemu.validate_qemu_profile("sec6-unknown")
        with self.assertRaises(qemu.RunnerError):
            qemu.validate_fixture("sec6-corrupt")

    def test_seed_derivation_is_reproducible_and_case_specific(self):
        first = parallel.stable_seed(606, "qemu:tst5:sec6-simple", 1)
        self.assertEqual(first, parallel.stable_seed(
            606, "qemu:tst5:sec6-simple", 1))
        self.assertNotEqual(first, parallel.stable_seed(
            606, "qemu:tst5:sec6-classic", 1))
        self.assertNotEqual(first, parallel.stable_seed(
            606, "qemu:tst5:sec6-simple", 2))


if __name__ == "__main__":
    unittest.main()
