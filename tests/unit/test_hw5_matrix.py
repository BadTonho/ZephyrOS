import json
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import qemu_parallel_runner
from tools import qemu_test_runner


ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "tests" / "catalog.json"

EXPECTED_CASES = {
    "qemu:hw1:no-acpi",
    "qemu:hw1:no-nic",
    "qemu:tst4:network",
    "qemu:tst5:krn6-diagnostics",
    "qemu:tst5:network",
    "qemu:tst5:poweroff",
    "qemu:tst5:reboot",
    "qemu:tst6:fault:network",
    "qemu:tst6:matrix:baseline",
    "qemu:tst6:matrix:network",
    "qemu:tst6:stress:network",
    "qemu:hw5:network-dual",
}
EXPECTED_PROFILES = {
    "baseline", "network", "network-dual", "no-nic", "no-acpi",
}


class HW5MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_network_power_cases_are_unique_snapshots(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "hw5" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            qemu_test_runner.validate_case_for_runner(case)

    def test_runner_selects_hw5_without_duplicates(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["hw5"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_profiles_capabilities_and_network_are_compatible(self):
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
        self.assertEqual(
            qemu_parallel_runner.qemu_network(
                self.cases["qemu:hw5:network-dual"]),
            "none",
        )

    def test_dual_nic_command_is_private_and_stable(self):
        base = {
            "image": str(ROOT / "build" / "zephyros.img"),
            "qemu": "qemu-system-i386",
            "cpu": "max",
            "snapshot": True,
            "network": "none",
            "qemu_arg": [],
            "storage_image": None,
        }
        arguments = SimpleNamespace(**base, qemu_profile="network-dual")
        command = qemu_test_runner.QemuSession(
            arguments, ROOT / "build" / "test-results" / "hw5").command()
        text = " ".join(command)
        self.assertIn("user,id=hw5net0,restrict=on", text)
        self.assertIn("user,id=hw5net1,restrict=on", text)
        self.assertIn("e1000,netdev=hw5net0,id=hw5nic0", text)
        self.assertIn("e1000,netdev=hw5net1,id=hw5nic1", text)
        self.assertNotIn("-nic", command)

    def test_parallel_runner_forwards_dual_profile(self):
        case = self.cases["qemu:hw5:network-dual"]
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
        command = qemu_parallel_runner.case_command(
            case, arguments, "hw5-child", Path("build/results"), 2105)
        self.assertIn("--qemu-profile", command)
        self.assertIn("network-dual", command)
        self.assertIn("--network", command)
        self.assertIn("none", command)


if __name__ == "__main__":
    unittest.main()
