import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner
from tools import qemu_test_runner


ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = ROOT / "config" / "hardware-profiles.json"
CATALOG_PATH = ROOT / "tests" / "catalog.json"
REQUIRED_PROFILES = {
    "baseline", "no-acpi", "no-nic", "no-usb", "no-vesa", "no-audio",
    "no-storage",
}
STATES = {
    "ABSENT", "UNSUPPORTED", "FAILED", "POLICY_DISABLED", "INVENTORIED",
    "INITIALIZED", "VALIDATED", "DEGRADED",
}
DRIVER_STATES = {
    "INVENTORIED", "INITIALIZED", "VALIDATED", "FAILED", "NOT_APPLICABLE",
}
DIAGNOSTICS = {"health", "regcheck full", "devices", "device-scan"}


class HardwareProfileManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_schema_required_profiles_and_unique_case_ids(self):
        self.assertEqual(self.manifest["schema"], "zephyros-hardware-profile-v1")
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["scope"], "qemu")
        profiles = self.manifest["profiles"]
        self.assertEqual({profile["id"] for profile in profiles}, REQUIRED_PROFILES)
        case_ids = [case_id for profile in profiles for case_id in profile["case_ids"]]
        self.assertEqual(len(case_ids), len(set(case_ids)))
        self.assertEqual(self.manifest["physical_hardware_status"], "PENDING")

    def test_states_diagnostics_and_identity_policy(self):
        self.assertEqual(
            set(self.manifest["availability_states"]), STATES)
        self.assertEqual(
            set(self.manifest["state_semantics"]), STATES)
        for description in self.manifest["state_semantics"].values():
            self.assertTrue(description)
        self.assertEqual(
            set(self.manifest["driver_states"]), DRIVER_STATES)
        self.assertTrue(DIAGNOSTICS.issubset(self.manifest["diagnostics"]))
        policy = self.manifest["identity_policy"]
        self.assertEqual(
            set(policy["stable_fields"]),
            {"device_id", "bdf", "io_base", "mmio_base", "irq", "version"},
        )
        self.assertEqual(policy["unexpected_change"], "FAIL")
        for profile in self.manifest["profiles"]:
            self.assertTrue(DIAGNOSTICS.issubset(profile["diagnostics"]))
            for hardware in profile["hardware"]:
                self.assertIn(hardware["availability_state"], STATES)
                self.assertIn(hardware["driver_state"], DRIVER_STATES)

    def test_cases_are_automated_snapshot_cases_with_matching_profiles(self):
        for profile in self.manifest["profiles"]:
            runner_profile = profile["runner_profile"]
            qemu_test_runner.validate_qemu_profile(runner_profile)
            capabilities = set(qemu_test_runner.qemu_profile_capabilities(runner_profile))
            for case_id in profile["case_ids"]:
                case = self.cases[case_id]
                self.assertEqual(case["executor"], "qemu")
                self.assertEqual(case["status"], "AUTOMATED")
                self.assertEqual(case["isolation"], "snapshot")
                self.assertEqual(case["qemu_profile"], runner_profile)
                self.assertNotEqual(case.get("status"), "PENDING")
                self.assertTrue(
                    set(case.get("required_capabilities", [])) <= capabilities)

    def test_profile_commands_and_absence_contracts(self):
        base = {
            "image": str(ROOT / "build" / "zephyros.img"),
            "qemu": "qemu-system-i386", "cpu": "max", "snapshot": True,
            "network": "none", "qemu_arg": [], "storage_image": None,
        }
        commands = {}
        for name in REQUIRED_PROFILES:
            arguments = SimpleNamespace(**base, qemu_profile=name)
            commands[name] = qemu_test_runner.QemuSession(
                arguments, ROOT / "build" / "test-results" / "hw1").command()
        self.assertIn("pc,acpi=off", commands["no-acpi"])
        self.assertIn("-nic", commands["no-nic"])
        self.assertIn("none", commands["no-nic"])
        self.assertIn("pc,usb=off", commands["no-usb"])
        self.assertEqual(commands["no-vesa"][-2:], ["-vga", "none"])
        self.assertNotIn("AC97", " ".join(commands["no-audio"]))
        self.assertNotIn("usb-storage", " ".join(commands["no-storage"]))
        self.assertEqual(
            qemu_parallel_runner.qemu_network(
                {"id": "qemu:hw1:no-nic", "qemu_profile": "no-nic"}),
            "none",
        )


if __name__ == "__main__":
    unittest.main()
