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
    "qemu:shell6:interface-compatibility",
    "qemu:tst5:shell",
    "qemu:tst5:input",
    "qemu:tst5:apps",
    "qemu:tst5:network",
    "qemu:tst5:sec6-simple",
    "qemu:tst5:sec6-classic",
    "qemu:tst6:matrix:baseline",
    "qemu:tst6:matrix:minimal",
    "qemu:tst6:matrix:network",
    "qemu:tst6:matrix:usb-hid",
    "qemu:tst6:matrix:audio",
    "qemu:tst6:matrix:display",
    "qemu:tst6:sec6:no-vesa",
    "qemu:hw1:no-acpi",
    "qemu:hw1:no-audio",
    "qemu:hw1:no-nic",
    "qemu:hw1:no-storage",
    "qemu:hw1:no-usb",
    "qemu:hw6:diagnostics-repeat",
}

REQUIRED_COMMANDS = {
    "guimode simple",
    "display status",
    "display scale normal",
    "desktop",
    "explorer",
    "taskmgr",
    "settings",
    "updater",
    "guimode classic",
    "guitest",
    "echo shell6-interface-compatibility",
}


class Shell6MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_shell6_selection_is_exact_and_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "shell6" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_shell6_cases(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["shell6"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_dedicated_case_declares_interface_contract(self):
        case = self.cases["qemu:shell6:interface-compatibility"]
        self.assertEqual(case.get("guest_case"),
                         "qemu:tst5:shell6-interface-compatibility")
        self.assertEqual(qemu_test_runner.qemu_case_profile(case), "baseline")
        self.assertEqual(case.get("parameters", {}).get("postcondition"),
                         "prompt-recovered")
        self.assertEqual(case.get("parameters", {}).get("mutation"),
                         "snapshot-discarded")
        steps = case.get("interaction", {}).get("steps", [])
        texts = {step.get("text") for step in steps if step.get("op") == "text"}
        self.assertTrue(REQUIRED_COMMANDS <= texts)
        self.assertTrue(any(step.get("op") == "key" and
                            step.get("key") == "esc"
                            for step in steps))

    def test_registry_links_dedicated_case(self):
        entries = self.registry.get("entries", [])
        linked = [
            entry for entry in entries
            if "qemu:shell6:interface-compatibility" in
            entry.get("case_ids", [])
        ]
        self.assertEqual(len(linked), 1)
        self.assertEqual(linked[0]["id"], "shell6-interface-compatibility-qemu")

    def test_orchestrator_declares_defaults(self):
        orchestrator = self.registry["orchestrators"]["shell6_interface_compatibility"]
        self.assertEqual(set(orchestrator["case_ids"]), EXPECTED_CASES)
        self.assertEqual(orchestrator["workers_default"], 4)
        self.assertEqual(orchestrator["seed_default"], 2206)
        self.assertEqual(orchestrator["artifact_directory"],
                         "build/test-results/shell6")
        self.assertEqual(orchestrator["required_commands"],
                         sorted(REQUIRED_COMMANDS))


if __name__ == "__main__":
    unittest.main()
