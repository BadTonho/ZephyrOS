import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import tst7_continuous_runner as runner


class ContinuousRunnerTests(unittest.TestCase):
    def test_modes_use_allowlisted_tst7_targets(self):
        arguments = runner.parse_arguments([
            "start", "--mode", "soak", "--max-cycles", "1"])
        self.assertEqual(runner.MODE_TARGETS[arguments.mode], "soak")
        command = runner.build_command(arguments, "tst7c-20260901T000000Z-1-1")
        self.assertIn("soak", command)
        self.assertIn("--strict-coverage", command)
        self.assertNotIn("--shell", command)

    def test_permanent_mode_requires_explicit_forever(self):
        arguments = runner.parse_arguments([
            "start", "--forever", "--interval", "0"])
        self.assertIsNone(runner.validate_arguments(arguments))
        arguments = runner.parse_arguments(["start", "--max-cycles", "2"])
        self.assertIsNone(runner.validate_arguments(arguments))

    def test_unbounded_configuration_is_rejected_without_forever(self):
        arguments = runner.parse_arguments(["start", "--max-cycles", "0"])
        self.assertEqual(runner.validate_arguments(arguments), "max_cycles_invalido")

    def test_return_code_classification(self):
        self.assertEqual(runner.classify_returncode(0, False), "PASS")
        self.assertEqual(runner.classify_returncode(1, False), "FAIL")
        self.assertEqual(runner.classify_returncode(2, False), "BLOCKED")
        self.assertEqual(runner.classify_returncode(None, True), "TIMEOUT")

    def test_failure_groups_keep_all_original_runs(self):
        cycles = [{
            "cycle": 1, "run_id": "run-1", "mode": "full",
            "status": "FAIL", "artifact_dir": "a",
            "result": {"cause": "catalogo_incompleto"},
        }, {
            "cycle": 2, "run_id": "run-2", "mode": "full",
            "status": "FAIL", "artifact_dir": "b",
            "result": {"cause": "catalogo_incompleto"},
        }]
        groups = runner.group_failures(cycles)
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0]["count"], 2)
        self.assertEqual(groups[0]["run_ids"], ["run-1", "run-2"])

    def test_stop_file_is_observed(self):
        with tempfile.TemporaryDirectory() as directory:
            arguments = runner.parse_arguments([
                "start", "--max-cycles", "1",
                "--stop-file", str(Path(directory) / "STOP")])
            Path(directory, "STOP").touch()
            arguments.stop_event = False
            self.assertTrue(runner.stop_requested(arguments))

    def test_cycle_result_is_written_and_status_preserved(self):
        arguments = runner.parse_arguments([
            "start", "--max-cycles", "1", "--interval", "0"])
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            session.mkdir(exist_ok=True)
            (session / "stdout.log").touch()
            (session / "stderr.log").touch()
            item = {
                "cycle": 1,
                "run_id": "tst7c-20260901T000000Z-1-1",
                "mode": "full",
                "status": "FAIL",
                "returncode": 1,
                "timed_out": False,
                "duration_seconds": 1.0,
                "command": [],
                "artifact_dir": "artifacts",
                "result": {"status": "FAIL"},
            }
            runner.write_json_atomic(session / "latest.json", item)
            self.assertEqual(
                runner.read_cycle_result("missing-run"), None)
            self.assertEqual(
                json.loads(
                    (session / "latest.json").read_text())["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
