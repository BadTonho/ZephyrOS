import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner
from tools import qemu_test_runner


ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "tests" / "catalog.json"

EXPECTED_CASES = {
    "qemu:tst4:storage-vfs",
    "qemu:tst5:storage",
    "qemu:tst6:stress:storage",
    "qemu:tst6:matrix:usb-storage",
    "qemu:hw1:no-storage",
    "qemu:hw1:no-usb",
    "qemu:hw4:usb-storage-ehci",
}
EXPECTED_PROFILES = {
    "baseline",
    "usb-storage",
    "usb-storage-ehci",
    "no-storage",
    "no-usb",
}


class HW4MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_storage_cases_are_unique_automated_snapshots(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "hw4" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotEqual(case.get("status"), "PENDING")
            qemu_test_runner.validate_case_for_runner(case)

    def test_runner_selects_hw4_without_duplicates(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["hw4"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_profiles_and_capabilities_are_compatible(self):
        profiles = {
            qemu_test_runner.qemu_case_profile(self.cases[case_id])
            for case_id in EXPECTED_CASES
        }
        self.assertTrue(EXPECTED_PROFILES.issubset(profiles))
        for case_id in EXPECTED_CASES:
            case = self.cases[case_id]
            profile = qemu_test_runner.qemu_case_profile(case)
            qemu_test_runner.validate_qemu_profile(profile)
            capabilities = set(qemu_test_runner.qemu_profile_capabilities(profile))
            self.assertTrue(
                set(case.get("required_capabilities", [])) <= capabilities)

    def test_usb_storage_commands_are_read_only_and_require_fixture(self):
        base = {
            "image": str(ROOT / "build" / "zephyros.img"),
            "qemu": "qemu-system-i386",
            "cpu": "max",
            "snapshot": True,
            "network": "none",
            "qemu_arg": [],
            "storage_image": str(ROOT / "build" / "storage-valid.img"),
        }

        def command(profile):
            arguments = SimpleNamespace(**base, qemu_profile=profile)
            return qemu_test_runner.QemuSession(
                arguments, ROOT / "build" / "test-results" / "hw4").command()

        uhci = command("usb-storage")
        ehci = command("usb-storage-ehci")
        no_storage = command("no-storage")
        no_usb = command("no-usb")
        self.assertIn("usb-storage,bus=tst6usb.0,drive=tst6stick", uhci)
        self.assertIn("readonly=on", " ".join(uhci))
        self.assertIn("usb-ehci,id=tst6ehci", ehci)
        self.assertIn("pc,usb=off", ehci)
        self.assertIn("usb-storage,bus=tst6ehci.0,drive=tst6ehcistick", ehci)
        self.assertIn("readonly=on", " ".join(ehci))
        self.assertNotIn("usb-storage", " ".join(no_storage))
        self.assertIn("pc,usb=off", no_usb)

    def test_parallel_runner_forwards_ehci_storage_fixture(self):
        case = self.cases["qemu:hw4:usb-storage-ehci"]
        arguments = SimpleNamespace(
            image="build/zephyros.img",
            catalog="tests/catalog.json",
            qemu="qemu-system-i386",
            cpu="max",
            boot_timeout=60,
            case_timeout=120,
            heartbeat_timeout=60,
            suite_timeout=600,
            storage_image="build/storage-valid.img",
        )
        command = qemu_parallel_runner.case_command(
            case, arguments, "hw4-child", Path("build/results"), 2104)
        self.assertIn("--qemu-profile", command)
        self.assertIn("usb-storage-ehci", command)
        self.assertIn("--storage-image", command)
        self.assertIn("build/storage-valid.img", command)


if __name__ == "__main__":
    unittest.main()
