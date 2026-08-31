import tempfile
import unittest
from argparse import Namespace
from pathlib import Path

from tools import qemu_test_runner as runner


class Tst5CatalogTests(unittest.TestCase):
    def base_case(self):
        return {
            "id": "qemu:tst5:test",
            "executor": "qemu",
            "profile": "smoke",
            "guest_case": "qemu:tst5:test",
            "isolation": "snapshot",
            "timeout_seconds": 20,
            "heartbeat_timeout_seconds": 5,
            "interaction": {
                "mode": "qmp-keyboard",
                "steps": [
                    {"op": "key", "key": "enter"},
                    {"op": "text", "text": "echo marker"},
                    {"op": "key", "key": "enter"},
                ],
                "post_action": {"type": "none", "steps": []},
            },
        }

    def test_allowlisted_interaction_is_valid(self):
        runner.validate_case_for_runner(self.base_case())

    def test_script_rejects_shell_control_characters(self):
        case = self.base_case()
        case["interaction"]["steps"][1]["text"] = "echo;bad"
        with self.assertRaises(runner.RunnerError):
            runner.validate_case_for_runner(case)

    def test_script_accepts_bounded_wait_only(self):
        case = self.base_case()
        case["interaction"]["steps"].insert(1, {
            "op": "wait", "seconds": 1
        })
        runner.validate_case_for_runner(case)
        case["interaction"]["steps"][1]["seconds"] = 11
        with self.assertRaises(runner.RunnerError) as context:
            runner.validate_case_for_runner(case)
        self.assertEqual(context.exception.cause,
                         "espera_entrada_invalida:qemu:tst5:test")

    def test_lifecycle_requires_bounded_post_action(self):
        case = self.base_case()
        case["interaction"]["post_action"] = {
            "type": "reboot",
            "steps": [{"op": "text", "text": "reboot"},
                       {"op": "key", "key": "enter"}],
            "timeout_seconds": 5,
        }
        runner.validate_case_for_runner(case)
        case["interaction"]["post_action"]["timeout_seconds"] = 0
        with self.assertRaises(runner.RunnerError):
            runner.validate_case_for_runner(case)


class Tst5ProgressTests(unittest.TestCase):
    def test_blackbox_states_are_public_to_the_report(self):
        progress = runner.ProgressTracker()
        for state in (runner.PROGRESS_INPUT_SENT, runner.PROGRESS_OBSERVING,
                      runner.PROGRESS_RESTART_WAIT,
                      runner.PROGRESS_SHUTDOWN_WAIT):
            progress.mark_state(state)
            self.assertEqual(progress.state, state)


class Tst5InputTests(unittest.TestCase):
    def test_qmp_payloads_and_input_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            arguments = Namespace(
                image="build/zephyros.img", cpu="max", snapshot=True,
                network="none", qemu="qemu-system-i386", qemu_arg=[],
            )
            session = runner.QemuSession(arguments, Path(directory))

            class FakeQmp:
                def __init__(self):
                    self.calls = []

                def command(self, name, arguments=None, deadline=None):
                    self.calls.append((name, arguments))
                    return {"return": {}}

            session.qmp = FakeQmp()
            session.execute_interaction({
                "steps": [
                    {"op": "key", "key": "enter"},
                    {"op": "text", "text": "a b-"},
                ]
            })
            self.assertEqual(session.qmp.calls[0],
                             ("send-key", {"keys": [
                                 {"type": "qcode", "data": "ret"}
                             ], "hold-time": 20}))
            self.assertIn(("send-key", {"keys": [
                {"type": "qcode", "data": "spc"}
            ], "hold-time": 20}), session.qmp.calls)
            self.assertEqual(session.input_trace[0]["key"], "enter")
            self.assertEqual(session.input_trace[1]["text"], "a b-")
            self.assertTrue((Path(directory) / "input.log").is_file())


class Tst5ProtocolTests(unittest.TestCase):
    def test_restart_and_shutdown_events_are_distinct(self):
        self.assertEqual(runner.PROGRESS_RESTART_WAIT, "RESTART_WAIT")
        self.assertEqual(runner.PROGRESS_SHUTDOWN_WAIT, "SHUTDOWN_WAIT")
        self.assertEqual(runner.result_status(
            [{"status": "PASS", "lifecycle": {"status": "PASS"}}], None)[0],
            "PASS")


if __name__ == "__main__":
    unittest.main()
