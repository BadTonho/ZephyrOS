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
    "qemu:shell5:system-update",
    "qemu:tst5:update-recovery",
    "qemu:tst6:fault:update",
    "qemu:tst6:fault:recovery",
    "qemu:tst5:reboot",
}

REQUIRED_COMMANDS = {
    "update status",
    "update system status",
    "update system slots",
    "update system check --tag stable",
    "update system verify --cached",
    "update system fetch --tag stable",
    "update system stage ZSC0.ZSY",
    "update system apply",
    "update system cancel",
    "updater",
    "echo shell5-system-update",
}


class Shell5MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_shell5_selection_is_exact_and_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "shell5" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_shell5_cases(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["shell5"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_dedicated_case_declares_update_contract(self):
        case = self.cases["qemu:shell5:system-update"]
        self.assertEqual(case.get("guest_case"),
                         "qemu:tst5:shell5-system-update")
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
            if "qemu:shell5:system-update" in entry.get("case_ids", [])
        ]
        self.assertEqual(len(linked), 1)
        self.assertEqual(linked[0]["id"], "shell5-system-update-qemu")

    def test_orchestrator_declares_defaults(self):
        orchestrator = self.registry["orchestrators"]["shell5_system_update"]
        self.assertEqual(set(orchestrator["case_ids"]), EXPECTED_CASES)
        self.assertEqual(orchestrator["workers_default"], 4)
        self.assertEqual(orchestrator["seed_default"], 2205)
        self.assertEqual(orchestrator["artifact_directory"],
                         "build/test-results/shell5")
        self.assertEqual(orchestrator["required_commands"],
                         sorted(REQUIRED_COMMANDS))


if __name__ == "__main__":
    unittest.main()
