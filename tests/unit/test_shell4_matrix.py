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
    "qemu:shell4:apps-packages",
    "qemu:tst5:apps",
    "qemu:tst6:fault:package",
    "qemu:tst6:stress:apps",
}

REQUIRED_COMMANDS = {
    "pkg verify VALID.ZPK",
    "pkg info VALID.ZPK",
    "pkg install VALID.ZPK",
    "pkg list",
    "pkg remove VALID",
    "store list",
    "store status",
    "store info VALID",
    "store install VALID",
    "store update VALID",
    "store remove VALID",
    "store rollback VALID",
    "store history VALID",
    "store run VALID shell4",
    "app run APPS/VALID/APP.ZAP shell4",
    "store remote status",
    "store remote list",
    "store remote check",
    "store",
    "pkg verify BADCRC.ZPK",
    "store install NEEDSDEP",
    "app outputtest fail",
    "echo shell4-apps-packages",
}


class Shell4MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_shell4_selection_is_exact_and_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "shell4" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_shell4_cases(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["shell4"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_dedicated_case_declares_package_contract(self):
        case = self.cases["qemu:shell4:apps-packages"]
        self.assertEqual(case.get("guest_case"),
                         "qemu:tst5:shell4-apps-packages")
        self.assertEqual(qemu_test_runner.qemu_case_profile(case), "baseline")
        self.assertEqual(case.get("parameters", {}).get("postcondition"),
                         "prompt-recovered")
        self.assertEqual(case.get("parameters", {}).get("mutation"),
                         "snapshot-discarded")
        steps = case.get("interaction", {}).get("steps", [])
        texts = {step.get("text") for step in steps if step.get("op") == "text"}
        self.assertTrue(REQUIRED_COMMANDS <= texts)
        self.assertTrue(any(step.get("op") == "keys" and
                            step.get("keys") == ["f12"]
                            for step in steps))

    def test_registry_links_dedicated_case(self):
        entries = self.registry.get("entries", [])
        linked = [
            entry for entry in entries
            if "qemu:shell4:apps-packages" in entry.get("case_ids", [])
        ]
        self.assertEqual(len(linked), 1)
        case = self.cases["qemu:shell4:apps-packages"]
        surface_ids = set(case.get("surface_ids", []))
        registered = {
            surface_id for entry in linked
            for surface_id in entry.get("surface_ids", [])
        }
        self.assertTrue(surface_ids <= registered)

    def test_orchestrator_declares_defaults(self):
        orchestrator = self.registry["orchestrators"]["shell4_apps_packages"]
        self.assertEqual(set(orchestrator["case_ids"]), EXPECTED_CASES)
        self.assertEqual(orchestrator["workers_default"], 4)
        self.assertEqual(orchestrator["seed_default"], 2204)
        self.assertEqual(orchestrator["artifact_directory"],
                         "build/test-results/shell4")
        self.assertEqual(orchestrator["required_commands"],
                         sorted(REQUIRED_COMMANDS))


if __name__ == "__main__":
    unittest.main()
