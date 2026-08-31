import unittest

from tools import qemu_test_runner as runner


class FrameTests(unittest.TestCase):
    def test_round_trip(self):
        frame = runner.build_frame([
            ("cmd", "HELLO"), ("run", "unit"), ("seq", "1")])
        self.assertEqual(runner.parse_frame(frame), {
            "cmd": "HELLO", "run": "unit", "seq": "1"})

    def test_invalid_frames_are_rejected(self):
        frame = runner.build_frame([
            ("cmd", "HELLO"), ("run", "unit"), ("seq", "1")])
        invalid = [
            frame.replace(b"run=unit", b"run=unix"),
            frame.replace(b"HELLO", b"HEL\x80O"),
            frame.replace(b"\n", b"\r\n"),
            b"x" * (runner.PROTOCOL_MAX_FRAME + 1),
        ]
        for candidate in invalid:
            with self.subTest(candidate=candidate[:20]):
                with self.assertRaises(ValueError):
                    runner.parse_frame(candidate)

    def test_build_rejects_duplicate_and_invalid_tokens(self):
        with self.assertRaises(runner.RunnerError):
            runner.build_frame([("cmd", "HELLO"), ("cmd", "PING")])
        with self.assertRaises(runner.RunnerError):
            runner.build_frame([("cmd", "HELLO"), ("run", "bad value")])


class CatalogAndStatusTests(unittest.TestCase):
    def valid_case(self):
        return {
            "id": "qemu:test", "scenario": "scenario", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": "qemu:test", "timeout_seconds": 5,
            "heartbeat_timeout_seconds": 2, "isolation": "snapshot",
            "parameters": {}, "preconditions": "ready", "action": "run",
            "expected": "pass", "errors": "failure", "effects": "none",
            "cleanup": "stop", "status": "AUTOMATED", "surface_ids": [],
        }

    def test_case_validation(self):
        case = self.valid_case()
        runner.validate_case_for_runner(case)
        case["heartbeat_timeout_seconds"] = 0
        with self.assertRaises(runner.RunnerError):
            runner.validate_case_for_runner(case)

    def test_status_classification(self):
        self.assertEqual(
            runner.result_status([{"status": "PASS"}], None),
            ("PASS", "completed", "suite_concluida"))
        self.assertEqual(
            runner.result_status([{"status": "FAIL"}], None),
            ("FAIL", "completed", "caso_reprovado"))
        self.assertEqual(
            runner.result_status([], runner.RunnerError("timeout", "timeout")),
            ("FAIL", "timeout", "timeout"))
        self.assertEqual(
            runner.result_status([], runner.RunnerError("missing", "precondition", True)),
            ("BLOCKED", "precondition", "missing"))


class ProgressTests(unittest.TestCase):
    def test_progress_states(self):
        progress = runner.ProgressTracker()
        self.assertEqual(progress.state, runner.PROGRESS_BOOT)
        progress.mark_state(runner.PROGRESS_HELLO)
        progress.record({"event": "READY", "seq": "1"})
        progress.mark_state(runner.PROGRESS_RUN_SENT)
        progress.record({"event": "BEGIN", "seq": "2"})
        progress.record({"event": "HEARTBEAT", "seq": "3", "ticks": "25"})
        progress.record({"event": "PASS", "seq": "4"})
        self.assertEqual(progress.state, runner.PROGRESS_PASS)
        self.assertEqual(progress.last_event, "PASS")
        self.assertEqual([item["event"] for item in progress.history],
                         ["READY", "BEGIN", "HEARTBEAT", "PASS"])

    def test_history_is_bounded(self):
        progress = runner.ProgressTracker()
        for sequence in range(runner.PROGRESS_HISTORY_LIMIT + 5):
            progress.record({"event": "HEARTBEAT", "seq": str(sequence)})
        self.assertEqual(len(progress.history), runner.PROGRESS_HISTORY_LIMIT)
        self.assertEqual(progress.history[0]["seq"], "5")


class FakeSession:
    def __init__(self, events):
        self.events = list(events)
        self.progress = runner.ProgressTracker()
        self.protocol_errors = []
        self.sent = []
        self.host_sequence = 0
        self.arguments = type("Arguments", (), {"boot_timeout": 1})()

    def send(self, fields):
        fields = list(fields)
        if not any(key == "seq" for key, _ in fields):
            self.host_sequence += 1
            fields.append(("seq", str(self.host_sequence)))
        else:
            self.host_sequence = int(next(value for key, value in fields
                                          if key == "seq"))
        self.sent.append(fields)

    def pump(self):
        if not self.events:
            return []
        event = self.events.pop(0)
        self.progress.record(event)
        return [event]


class WaitForCaseTests(unittest.TestCase):
    def test_blocked_can_finish_without_begin(self):
        session = FakeSession([{
            "event": "BLOCKED", "case": "qemu:test", "seq": "1"}])
        result = runner.wait_for_case(session, "qemu:test", 0, 1, 1, 0.001)
        self.assertEqual(result["event"], "BLOCKED")
        self.assertEqual(session.progress.state, runner.PROGRESS_BLOCKED)

    def test_heartbeat_watchdog_starts_after_begin(self):
        session = FakeSession([{
            "event": "BEGIN", "case": "qemu:test", "iteration": "0",
            "seed": "1", "seq": "1"}])
        with self.assertRaisesRegex(runner.RunnerError, "state=RUNNING"):
            runner.wait_for_case(session, "qemu:test", 0, 1, 1, 0.001)

    def test_terminal_event_publishes_terminal_state(self):
        session = FakeSession([
            {"event": "BEGIN", "case": "qemu:test", "iteration": "0",
             "seed": "1", "seq": "1"},
            {"event": "PASS", "case": "qemu:test", "iteration": "0",
             "seed": "1", "seq": "2"},
        ])
        result = runner.wait_for_case(session, "qemu:test", 0, 1, 1, 1)
        self.assertEqual(result["event"], "PASS")
        self.assertEqual(session.progress.state, runner.PROGRESS_PASS)


class WaitForReadyTests(unittest.TestCase):
    def test_ready_is_followed_by_ping_heartbeat(self):
        session = FakeSession([
            {"event": "READY", "run": "qemu:test", "seq": "1"},
            {"event": "HEARTBEAT", "run": "qemu:test", "seq": "2",
             "ticks": "25"},
        ])
        runner.wait_for_ready(session, "qemu:test")
        self.assertEqual([fields[0][1] for fields in session.sent],
                         ["HELLO", "PING"])
        self.assertEqual(session.progress.last_event, "HEARTBEAT")


if __name__ == "__main__":
    unittest.main()
