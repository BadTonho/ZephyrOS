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
    "qemu:hw6:diagnostics-repeat",
    "qemu:shell2:commands-diagnostics",
    "qemu:tst5:apps",
    "qemu:tst5:input",
    "qemu:tst5:krn6-diagnostics",
    "qemu:tst5:sec6-classic",
    "qemu:tst5:sec6-diagnostics",
    "qemu:tst5:sec6-simple",
    "qemu:tst5:shell",
    "qemu:tst6:sec6:no-vesa",
}

REQUIRED_COMMANDS = {
    "help",
    "clear",
    "echo shell2-success",
    "mem",
    "procs",
    "threads",
    "uptime",
    "ls",
    "cat README.TXT",
    "mount",
    "devices",
    "device-info pci-00:03.0",
    "device-scan",
    "health check",
    "regcheck full",
    "memcheck",
    "schedcheck",
    "proccheck",
}


class Shell2MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_shell2_selection_is_exact_and_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "shell2" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_shell2_cases(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["shell2"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_dedicated_case_declares_command_contract(self):
        case = self.cases["qemu:shell2:commands-diagnostics"]
        self.assertEqual(case.get("guest_case"),
                         "qemu:tst5:shell2-commands-diagnostics")
        self.assertEqual(qemu_test_runner.qemu_case_profile(case), "baseline")
        self.assertEqual(case.get("parameters", {}).get("postcondition"),
                         "prompt-recovered")
        steps = case.get("interaction", {}).get("steps", [])
        texts = {step.get("text") for step in steps if step.get("op") == "text"}
        self.assertTrue(REQUIRED_COMMANDS <= texts)
        self.assertIn("shell2-comando-invalido", texts)
        self.assertIn("echo shell2-commands-diagnostics", texts)
        self.assertTrue(any(step.get("op") == "keys" and
                            step.get("keys") == ["ctrl", "c"]
                            for step in steps))

    def test_registry_links_dedicated_case(self):
        entries = self.registry.get("entries", [])
        linked = [
            entry for entry in entries
            if "qemu:shell2:commands-diagnostics" in entry.get("case_ids", [])
        ]
        self.assertEqual(len(linked), 1)
        case = self.cases["qemu:shell2:commands-diagnostics"]
        surface_ids = set(case.get("surface_ids", []))
        registered = {
            surface_id for entry in linked
            for surface_id in entry.get("surface_ids", [])
        }
        self.assertTrue(surface_ids <= registered)

    def test_orchestrator_declares_defaults(self):
        orchestrator = self.registry["orchestrators"]["shell2_commands_diagnostics"]
        self.assertEqual(set(orchestrator["case_ids"]), EXPECTED_CASES)
        self.assertEqual(orchestrator["workers_default"], 4)
        self.assertEqual(orchestrator["seed_default"], 2202)
        self.assertEqual(orchestrator["artifact_directory"],
                         "build/test-results/shell2")
        self.assertEqual(orchestrator["required_commands"],
                         sorted(REQUIRED_COMMANDS))


if __name__ == "__main__":
    unittest.main()
