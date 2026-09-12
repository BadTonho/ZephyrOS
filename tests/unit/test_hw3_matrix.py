import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner
from tools import qemu_test_runner


ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "tests" / "catalog.json"
PROFILE_PATH = ROOT / "config" / "hardware-profiles.json"

EXPECTED_CASES = {
    "qemu:tst5:input",
    "qemu:tst5:shell",
    "qemu:tst5:apps",
    "qemu:tst5:sec6-simple",
    "qemu:tst5:sec6-classic",
    "qemu:tst5:krn6-diagnostics",
    "qemu:tst6:matrix:baseline",
    "qemu:tst6:matrix:minimal",
    "qemu:tst6:matrix:usb-hid",
    "qemu:tst6:matrix:audio",
    "qemu:tst6:matrix:display",
    "qemu:tst6:sec6:no-vesa",
    "qemu:hw1:no-usb",
    "qemu:hw1:no-audio",
}
EXPECTED_PROFILES = {
    "baseline",
    "minimal",
    "usb-hid",
    "audio",
    "display",
    "no-vesa",
    "no-usb",
    "no-audio",
}
DIAGNOSTICS = {"health", "regcheck full", "devices", "device-scan"}


class HW3MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.hardware = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_selected_cases_are_unique_automated_snapshots(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "hw3" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotEqual(case.get("status"), "PENDING")
            qemu_test_runner.validate_case_for_runner(case)

    def test_runner_selects_the_complete_hw3_tag(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["hw3"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")

        self.assertEqual(
            {case["id"] for case in selected}, EXPECTED_CASES)

    def test_profiles_and_capabilities_are_compatible(self):
        selected = [
            self.cases[case_id]
            for case_id in EXPECTED_CASES
        ]
        profiles = {
            qemu_test_runner.qemu_case_profile(case)
            for case in selected
        }
        self.assertTrue(EXPECTED_PROFILES.issubset(profiles))
        for case in selected:
            profile = qemu_test_runner.qemu_case_profile(case)
            qemu_test_runner.validate_qemu_profile(profile)
            capabilities = set(
                qemu_test_runner.qemu_profile_capabilities(profile))
            self.assertTrue(
                set(case.get("required_capabilities", [])) <= capabilities)

    def test_required_diagnostics_and_hardware_scope(self):
        self.assertEqual(self.hardware["scope"], "qemu")
        self.assertEqual(self.hardware["physical_hardware_status"], "PENDING")
        self.assertTrue(DIAGNOSTICS.issubset(self.hardware["diagnostics"]))
        for profile in self.hardware["profiles"]:
            self.assertTrue(DIAGNOSTICS.issubset(set(profile["diagnostics"])))

    def test_profile_commands_keep_input_and_fallback_paths(self):
        base = {
            "image": str(ROOT / "build" / "zephyros.img"),
            "qemu": "qemu-system-i386",
            "cpu": "max",
            "snapshot": True,
            "network": "none",
            "qemu_arg": [],
            "storage_image": None,
        }

        def command(profile):
            arguments = SimpleNamespace(**base, qemu_profile=profile)
            return qemu_test_runner.QemuSession(
                arguments, ROOT / "build" / "test-results" / "hw3").command()

        self.assertIn("-serial", command("baseline"))
        self.assertIn("usb-kbd,bus=tst6usb.0", command("usb-hid"))
        self.assertIn("usb-mouse,bus=tst6usb.0", command("usb-hid"))
        self.assertIn("AC97,audiodev=tst6audio", command("audio"))
        self.assertIn("cirrus", command("display"))
        self.assertEqual(command("no-vesa")[-2:], ["-vga", "none"])
        self.assertIn("pc,usb=off", command("no-usb"))
        self.assertNotIn("AC97", " ".join(command("no-audio")))

    def test_missing_catalog_profile_uses_baseline(self):
        case = dict(self.cases["qemu:tst5:input"])
        self.assertIsNone(case.get("qemu_profile"))
        self.assertEqual(qemu_test_runner.qemu_case_profile(case), "baseline")
        qemu_test_runner.validate_case_for_runner(case)


if __name__ == "__main__":
    unittest.main()
