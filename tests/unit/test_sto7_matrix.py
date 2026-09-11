import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner as parallel
from tools import qemu_test_runner as qemu


ROOT = Path(__file__).resolve().parents[2]

STO7_CASE_IDS = {
    "qemu:tst4:storage-vfs",
    "qemu:tst5:apps",
    "qemu:tst5:poweroff",
    "qemu:tst5:processes",
    "qemu:tst5:reboot",
    "qemu:tst5:shell",
    "qemu:tst5:storage",
    "qemu:tst5:update-recovery",
    "qemu:tst6:fault:block",
    "qemu:tst6:fault:block-cache",
    "qemu:tst6:fault:memory",
    "qemu:tst6:fault:network",
    "qemu:tst6:fault:process",
    "qemu:tst6:fault:recovery",
    "qemu:tst6:fault:update",
    "qemu:tst6:matrix:audio",
    "qemu:tst6:matrix:baseline",
    "qemu:tst6:matrix:display",
    "qemu:tst6:matrix:minimal",
    "qemu:tst6:matrix:network",
    "qemu:tst6:matrix:pci",
    "qemu:tst6:matrix:usb-hid",
    "qemu:tst6:matrix:usb-storage",
    "qemu:tst6:sec6:no-vesa",
    "qemu:tst6:stress:apps",
    "qemu:tst6:stress:kernel",
    "qemu:tst6:stress:network",
    "qemu:tst6:stress:storage",
}


class Sto7MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(
            (ROOT / "tests" / "catalog.json").read_text(encoding="utf-8"))
        cls.cases = {
            case["id"]: case for case in cls.catalog["cases"]
            if isinstance(case, dict) and case.get("executor") == "qemu"
        }

    def test_catalog_has_complete_sto7_selection(self):
        selected = {
            case["id"] for case in self.cases.values()
            if "sto7" in case.get("tags", [])
        }
        self.assertEqual(selected, STO7_CASE_IDS)
        self.assertEqual(len(selected), len(STO7_CASE_IDS))

    def test_cases_are_snapshot_isolated_and_runner_valid(self):
        arguments = SimpleNamespace(
            case=[], profile=None, tag=["sto7"], all_cases=False,
            workers=4, seed=7007, run_id=None, boot_timeout=60.0,
            case_timeout=120.0, heartbeat_timeout=60.0,
            suite_timeout=600.0,
        )
        selected = parallel.select_cases(self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, STO7_CASE_IDS)
        for case in selected:
            self.assertEqual(case["status"], "AUTOMATED")
            self.assertEqual(case["isolation"], "snapshot")
            qemu.validate_case_for_runner(case)
            qemu_profile = case.get("qemu_profile", "baseline")
            qemu.validate_qemu_profile(qemu_profile)
            capabilities = set(case.get("required_capabilities", []))
            self.assertTrue(
                capabilities.issubset(
                    set(qemu.qemu_profile_capabilities(qemu_profile))
                )
            )

    def test_matrix_domains_have_representatives(self):
        selected = [self.cases[case_id] for case_id in STO7_CASE_IDS]
        ids = {case["id"] for case in selected}
        domains = {
            "storage": {"qemu:tst4:storage-vfs", "qemu:tst5:storage",
                         "qemu:tst6:stress:storage"},
            "fault": {"qemu:tst6:fault:block", "qemu:tst6:fault:memory",
                      "qemu:tst6:fault:recovery"},
            "hardware": {"qemu:tst6:matrix:baseline",
                         "qemu:tst6:matrix:minimal",
                         "qemu:tst6:matrix:usb-storage",
                         "qemu:tst6:sec6:no-vesa"},
            "lifecycle": {"qemu:tst5:reboot", "qemu:tst5:poweroff",
                          "qemu:tst5:update-recovery"},
        }
        for name, representatives in domains.items():
            with self.subTest(domain=name):
                self.assertTrue(ids.intersection(representatives))

    def test_seed_derivation_is_reproducible_and_case_specific(self):
        first = parallel.stable_seed(7007, "qemu:tst5:storage", 1)
        self.assertEqual(first, parallel.stable_seed(
            7007, "qemu:tst5:storage", 1))
        self.assertNotEqual(first, parallel.stable_seed(
            7007, "qemu:tst6:stress:storage", 1))
        self.assertNotEqual(first, parallel.stable_seed(
            7007, "qemu:tst5:storage", 2))


if __name__ == "__main__":
    unittest.main()
