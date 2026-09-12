import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner
from tools import qemu_test_runner


ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "tests" / "catalog.json"
REGISTRY_PATH = ROOT / "tests" / "coverage" / "registry.json"

EXPECTED_CASES = {
    "qemu:hw1:no-acpi",
    "qemu:hw1:no-audio",
    "qemu:hw1:no-nic",
    "qemu:hw1:no-storage",
    "qemu:hw1:no-usb",
    "qemu:tst6:sec6:no-vesa",
    "qemu:tst5:krn6-diagnostics",
    "qemu:tst5:poweroff",
    "qemu:tst5:reboot",
    "qemu:tst6:matrix:baseline",
    "qemu:tst6:matrix:minimal",
    "qemu:tst6:matrix:pci",
    "qemu:tst6:matrix:network",
    "qemu:tst6:matrix:usb-hid",
    "qemu:tst6:matrix:usb-storage",
    "qemu:tst6:matrix:audio",
    "qemu:tst6:matrix:display",
    "qemu:hw4:usb-storage-ehci",
    "qemu:hw5:network-dual",
    "qemu:hw6:diagnostics-repeat",
}
EXPECTED_PROFILES = {
    "baseline", "minimal", "pci", "network", "network-dual", "no-acpi",
    "no-audio", "no-nic", "no-storage", "no-usb", "no-vesa", "usb-hid",
    "usb-storage", "usb-storage-ehci", "audio", "display",
}
REQUIRED_DIAGNOSTICS = {
    "health check",
    "regcheck full",
    "devices",
    "devices -v",
    "device-info",
    "device-scan",
    "acpi tables",
    "net status",
    "usb status",
    "power status",
}


class HW6MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_hw6_selection_is_exact_and_snapshot_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "hw6" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_only_hw6(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["hw6"],
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
        self.assertEqual(profiles, EXPECTED_PROFILES)
        for case_id in EXPECTED_CASES:
            case = self.cases[case_id]
            profile = qemu_test_runner.qemu_case_profile(case)
            qemu_test_runner.validate_qemu_profile(profile)
            capabilities = set(qemu_test_runner.qemu_profile_capabilities(profile))
            required = set(case.get("required_capabilities", []))
            self.assertTrue(required <= capabilities, case_id)
        self.assertEqual(
            qemu_parallel_runner.qemu_network(
                self.cases["qemu:hw5:network-dual"]),
            "none",
        )
        self.assertEqual(
            qemu_parallel_runner.qemu_network(
                self.cases["qemu:hw6:diagnostics-repeat"]),
            "user,model=e1000,restrict=on",
        )

    def test_diagnostics_case_declares_repetition_and_reverse_links(self):
        case = self.cases["qemu:hw6:diagnostics-repeat"]
        parameters = case.get("parameters", {})
        self.assertEqual(case.get("qemu_profile"), "baseline")
        self.assertEqual(parameters.get("repeat"), 2)
        self.assertEqual(set(parameters.get("diagnostics", [])),
                         REQUIRED_DIAGNOSTICS)
        self.assertIn("prompt-recovered-no-reinit", parameters.values())
        self.assertTrue(case.get("surface_ids"))

        entries = self.registry.get("entries", [])
        links = {
            surface_id
            for entry in entries
            if "qemu:hw6:diagnostics-repeat" in entry.get("case_ids", [])
            for surface_id in entry.get("surface_ids", [])
        }
        self.assertTrue(set(case["surface_ids"]) <= links)

    def test_case_command_uses_baseline_profile_and_unique_run(self):
        case = self.cases["qemu:hw6:diagnostics-repeat"]
        arguments = SimpleNamespace(
            image="build/zephyros.img",
            catalog="tests/catalog.json",
            qemu="qemu-system-i386",
            cpu="max",
            boot_timeout=60,
            case_timeout=120,
            heartbeat_timeout=60,
            suite_timeout=600,
            storage_image=None,
        )
        first = qemu_parallel_runner.case_command(
            case, arguments, "hw6-child-a", Path("build/results/a"), 2106)
        second = qemu_parallel_runner.case_command(
            case, arguments, "hw6-child-b", Path("build/results/b"), 2106)
        self.assertIn("--qemu-profile", first)
        self.assertIn("baseline", first)
        self.assertIn("hw6-child-a", first)
        self.assertIn("hw6-child-b", second)
        self.assertNotEqual(first, second)


if __name__ == "__main__":
    unittest.main()
