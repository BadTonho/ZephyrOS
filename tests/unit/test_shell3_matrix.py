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
    "qemu:hw1:no-storage",
    "qemu:hw1:no-usb",
    "qemu:shell3:files-admin",
    "qemu:tst4:storage-vfs",
    "qemu:tst5:apps",
    "qemu:tst5:input",
    "qemu:tst5:sec6-classic",
    "qemu:tst5:sec6-simple",
    "qemu:tst5:shell",
    "qemu:tst5:storage",
    "qemu:tst6:matrix:minimal",
    "qemu:tst6:matrix:usb-storage",
    "qemu:tst6:sec6:no-vesa",
    "qemu:tst6:stress:apps",
    "qemu:tst6:stress:storage",
}

REQUIRED_COMMANDS = {
    "cat README.TXT",
    "cd /",
    "cat README.TXT | grep Zephyr",
    "echo shell3-pipeline-source | grep shell3",
    "echo shell3-redirect > /tmp/SHELL3.TXT",
    "index status",
    "ls",
    "ls | grep README",
    "mount",
    "pipetest",
    "pwd",
    "search README",
    "storage mount missing",
    "storage unmount missing",
    "cat /tmp/SHELL3.TXT",
}


class Shell3MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        cls.cases = {case["id"]: case for case in cls.catalog["cases"]}

    def test_shell3_selection_is_exact_and_isolated(self):
        selected = [
            case for case in self.catalog["cases"]
            if case.get("executor") == "qemu"
            and case.get("status") == "AUTOMATED"
            and "shell3" in case.get("tags", [])
        ]
        ids = [case["id"] for case in selected]
        self.assertEqual(set(ids), EXPECTED_CASES)
        self.assertEqual(len(ids), len(set(ids)))
        for case in selected:
            self.assertEqual(case.get("isolation"), "snapshot")
            self.assertNotIn(case.get("status"), {"PENDING", "BLOCKED"})
            qemu_test_runner.validate_case_for_runner(case)

    def test_parallel_runner_selects_shell3_cases(self):
        arguments = SimpleNamespace(
            case=None,
            profile=None,
            tag=["shell3"],
            all_cases=False,
        )
        selected = qemu_parallel_runner.select_cases(
            self.catalog, arguments, "parallel")
        self.assertEqual({case["id"] for case in selected}, EXPECTED_CASES)

    def test_dedicated_case_declares_file_and_scene_contract(self):
        case = self.cases["qemu:shell3:files-admin"]
        self.assertEqual(case.get("guest_case"),
                         "qemu:tst5:shell3-files-admin")
        self.assertEqual(qemu_test_runner.qemu_case_profile(case), "baseline")
        self.assertEqual(case.get("parameters", {}).get("postcondition"),
                         "prompt-recovered")
        self.assertEqual(case.get("parameters", {}).get("mutation"),
                         "snapshot-discarded")
        steps = case.get("interaction", {}).get("steps", [])
        texts = {step.get("text") for step in steps if step.get("op") == "text"}
        self.assertTrue(REQUIRED_COMMANDS <= texts)
        self.assertIn("explorer", texts)
        self.assertIn("taskmgr", texts)
        self.assertIn("settings", texts)
        self.assertIn("cd /missing", texts)
        self.assertTrue(any(step.get("op") == "keys" and
                            step.get("keys") == ["ctrl", "c"]
                            for step in steps))
        self.assertTrue(any(step.get("op") == "text" and
                            step.get("text") == "echo shell3-files-admin"
                            for step in steps))

    def test_registry_links_dedicated_case(self):
        entries = self.registry.get("entries", [])
        linked = [
            entry for entry in entries
            if "qemu:shell3:files-admin" in entry.get("case_ids", [])
        ]
        self.assertEqual(len(linked), 1)
        case = self.cases["qemu:shell3:files-admin"]
        surface_ids = set(case.get("surface_ids", []))
        registered = {
            surface_id for entry in linked
            for surface_id in entry.get("surface_ids", [])
        }
        self.assertTrue(surface_ids <= registered)

    def test_orchestrator_declares_defaults(self):
        orchestrator = self.registry["orchestrators"]["shell3_files_admin"]
        self.assertEqual(set(orchestrator["case_ids"]), EXPECTED_CASES)
        self.assertEqual(orchestrator["workers_default"], 4)
        self.assertEqual(orchestrator["seed_default"], 2203)
        self.assertEqual(orchestrator["artifact_directory"],
                         "build/test-results/shell3")
        self.assertEqual(orchestrator["required_commands"],
                         sorted(REQUIRED_COMMANDS))


if __name__ == "__main__":
    unittest.main()
