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
    "qemu:tst5:input",
    "qemu:tst5:shell",
    "qemu:tst5:apps",
    "qemu:tst5:sec6-simple",
    "qemu:tst5:sec6-classic",
    "qemu:shell1:prompt-lifecycle",
}


class Shell1MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_shell1_selection_is_exact_and_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "shell1" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_shell1_cases(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["shell1"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_dedicated_case_declares_prompt_contract(self):
        case = self.cases["qemu:shell1:prompt-lifecycle"]
        self.assertEqual(case.get("guest_case"),
                         "qemu:tst5:shell1-prompt-lifecycle")
        self.assertEqual(qemu_test_runner.qemu_case_profile(case), "baseline")
        self.assertEqual(case.get("parameters", {}).get("postcondition"),
                         "prompt-recovered")
        steps = case.get("interaction", {}).get("steps", [])
        texts = {step.get("text") for step in steps if step.get("op") == "text"}
        self.assertIn("shell1-comando-invalido", texts)
        self.assertIn("health check", texts)
        self.assertIn("echo shell1-prompt-lifecycle", texts)
        self.assertTrue(any(step.get("op") == "keys" and
                            step.get("keys") == ["ctrl", "c"]
                            for step in steps))

    def test_registry_links_prompt_contract(self):
        entries = self.registry.get("entries", [])
        linked = [
            entry for entry in entries
            if "qemu:shell1:prompt-lifecycle" in entry.get("case_ids", [])
        ]
        self.assertEqual(len(linked), 1)
        case = self.cases["qemu:shell1:prompt-lifecycle"]
        surface_ids = set(case.get("surface_ids", []))
        registered = {
            surface_id for entry in linked
            for surface_id in entry.get("surface_ids", [])
        }
        self.assertTrue(surface_ids <= registered)


if __name__ == "__main__":
    unittest.main()
