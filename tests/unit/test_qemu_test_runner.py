import io
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

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

    def test_fixture_allowlist(self):
        runner.validate_fixture(None)
        runner.validate_fixture("readonly")
        runner.validate_fixture("readonly-update")
        with self.assertRaisesRegex(runner.RunnerError, "fixture_invalida"):
            runner.validate_fixture("arbitrary")

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

    def test_explicit_heartbeat_limit_can_raise_catalog_limit(self):
        self.assertEqual(
            runner.heartbeat_timeout({"heartbeat_timeout_seconds": 10}, 120),
            120)
        self.assertEqual(
            runner.heartbeat_timeout({"heartbeat_timeout_seconds": 120}, 60),
            120)

    def test_explicit_case_limit_can_raise_catalog_limit(self):
        self.assertEqual(
            runner.case_timeout({"timeout_seconds": 60}, 120),
            120)
        self.assertEqual(
            runner.case_timeout({"timeout_seconds": 120}, 60),
            120)

    def test_qemu_uses_deterministic_single_thread_tcg(self):
        self.assertEqual(
            runner.QEMU_COMMON_ARGS,
            ["-accel", "tcg,thread=single"])

    def test_no_vesa_profile_keeps_serial_capabilities_without_framebuffer(self):
        runner.validate_qemu_profile("no-vesa")
        self.assertEqual(
            runner.qemu_profile_capabilities("no-vesa"), ["acpi", "pci"])
        arguments = SimpleNamespace(
            image="build/zephyros.img", qemu="qemu-system-i386", cpu="max",
            snapshot=True, network="none", qemu_profile="no-vesa",
            qemu_arg=[], storage_image=None,
        )
        session = runner.QemuSession(arguments, Path("build/results"))
        command = session.command()
        self.assertIn("-serial", command)
        self.assertIn("-qmp", command)
        self.assertEqual(command[-2:], ["-vga", "none"])

    def test_ps2_fallback_removes_only_usb_keyboard(self):
        args = runner.qemu_profile_args(
            "usb-hid", runner.QEMU_INPUT_TRANSPORT_PS2_FALLBACK)
        self.assertNotIn("usb-kbd,bus=tst6usb.0", args)
        self.assertIn("usb-mouse,bus=tst6usb.0", args)

    def test_default_input_transport_keeps_usb_keyboard(self):
        args = runner.qemu_profile_args("usb-hid")
        self.assertIn("usb-kbd,bus=tst6usb.0", args)

    def test_unknown_input_transport_is_rejected(self):
        with self.assertRaisesRegex(runner.RunnerError,
                                    "transporte_input_invalido"):
            runner.qemu_profile_args("usb-hid", "invalid")


class QemuSessionTests(unittest.TestCase):
    def test_reset_protocol_discards_stale_frames_until_ready(self):
        session = runner.QemuSession.__new__(runner.QemuSession)
        session.serial_buffer = bytearray()
        session.protocol_errors = []
        session.events = []
        session.deferred_events = []
        session.host_sequence = 4
        session.guest_sequence = 42
        session.last_heartbeat = 123.0
        session.awaiting_ready_after_reset = False
        session.restart_waiting = False
        session.restart_detected = False
        session.run_id = "unit"
        session.observed_capabilities = []
        session.progress = runner.ProgressTracker()
        session.serial = None

        session.reset_protocol()

        self.assertTrue(session.awaiting_ready_after_reset)
        self.assertEqual(session.guest_sequence, 0)
        self.assertEqual(session.host_sequence, 0)

    def test_serial_ready_detects_reboot_without_qmp_reset(self):
        session = runner.QemuSession.__new__(runner.QemuSession)
        session.serial_buffer = bytearray()
        session.protocol_errors = []
        session.events = []
        session.deferred_events = []
        session.host_sequence = 2
        session.guest_sequence = 8
        session.last_heartbeat = None
        session.awaiting_ready_after_reset = False
        session.restart_waiting = True
        session.restart_detected = False
        session.run_id = "unit"
        session.observed_capabilities = []
        session.progress = runner.ProgressTracker()
        frame = runner.build_frame([
            ("event", "READY"), ("run", "unit"), ("seq", "1")])

        class FakeSerial:
            def recv(self, size):
                return frame

        with tempfile.TemporaryDirectory() as directory:
            session.artifact_dir = Path(directory)
            session.serial = FakeSerial()
            session._read_serial()

        self.assertTrue(session.restart_detected)
        self.assertEqual(session.events, [])
        self.assertEqual(session.protocol_errors, [])

    def test_send_text_supports_colon_in_device_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            session = runner.QemuSession.__new__(runner.QemuSession)
            session.artifact_dir = Path(directory)
            session.input_trace = []
            sent = []
            session._send_qmp_keys = sent.append
            with patch.object(runner.time, "sleep"):
                session.send_text("pci-00:03.0")
            self.assertIn(["shift", "semicolon"], sent)
            self.assertEqual(session.input_trace[0]["text"], "pci-00:03.0")

    def test_send_text_uses_abnt2_slash_usage(self):
        with tempfile.TemporaryDirectory() as directory:
            session = runner.QemuSession.__new__(runner.QemuSession)
            session.artifact_dir = Path(directory)
            session.input_trace = []
            sent = []
            scancodes = []
            session._send_qmp_keys = sent.append
            session._send_qmp_scancode = scancodes.append
            with patch.object(runner.time, "sleep"):
                session.send_text("http://example.com/")
            self.assertEqual(scancodes, [0x73, 0x73, 0x73])
            self.assertNotIn(["slash"], sent)

    def test_send_text_supports_pipeline_and_redirect_operators(self):
        with tempfile.TemporaryDirectory() as directory:
            session = runner.QemuSession.__new__(runner.QemuSession)
            session.artifact_dir = Path(directory)
            session.input_trace = []
            sent = []
            scancodes = []
            session._send_qmp_keys = sent.append
            session._send_qmp_scancode = scancodes.append
            with patch.object(runner.time, "sleep"):
                session.send_text("echo x | grep x > /tmp/X")
            self.assertIn(["shift", "backslash"], sent)
            self.assertIn(["shift", "dot"], sent)
            self.assertEqual(scancodes, [0x73, 0x73])
            self.assertEqual(session.input_trace[0]["text"],
                             "echo x | grep x > /tmp/X")

    def test_configure_input_timing_accepts_bounded_slow_gap(self):
        session = runner.QemuSession.__new__(runner.QemuSession)
        session.input_key_gap_seconds = runner.QMP_KEY_GAP_SECONDS

        session.configure_input_timing(0.05)

        self.assertEqual(session.input_key_gap_seconds, 0.05)

    def test_configure_input_timing_rejects_unsafe_gap(self):
        session = runner.QemuSession.__new__(runner.QemuSession)
        session.input_key_gap_seconds = runner.QMP_KEY_GAP_SECONDS

        for value in (0.0, 1.1, True, float("nan")):
            with self.subTest(value=value):
                with self.assertRaises(runner.RunnerError):
                    session.configure_input_timing(value)

    def test_start_retries_after_serial_port_collision(self):
        class FakeProcess:
            def __init__(self):
                self.pid = 4321
                self.returncode = None
                self.stdout = io.BytesIO()
                self.stderr = io.BytesIO()

            def poll(self):
                return self.returncode

            def wait(self, timeout=None):
                self.returncode = 0
                return self.returncode

            def terminate(self):
                self.returncode = 143

            def kill(self):
                self.returncode = 137

        class FakeQmp:
            def __init__(self, port):
                self.port = port

            def connect(self, deadline):
                return None

            def command(self, name, arguments=None):
                return {"return": {"status": "running"}}

            def close(self):
                return None

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "zephyros.img"
            image.write_bytes(b"image")
            artifact_dir = root / "artifacts"
            artifact_dir.mkdir()
            arguments = SimpleNamespace(
                image=str(image), qemu="qemu-system-i386", cpu="max",
                snapshot=True, network="none", qemu_profile="baseline",
                qemu_arg=[], boot_timeout=1, storage_image=None,
            )
            session = runner.QemuSession(arguments, artifact_dir)
            with patch.object(runner.shutil, "which", return_value="qemu"), \
                    patch.object(runner.subprocess, "Popen",
                                 side_effect=[FakeProcess(), FakeProcess()]), \
                    patch.object(runner, "QmpClient", FakeQmp), \
                    patch.object(runner, "free_port",
                                 side_effect=[1001, 2001, 1002, 2002]), \
                    patch.object(
                        session, "_connect_serial",
                        side_effect=[
                            runner.RunnerError("serial_timeout", "serial_timeout"),
                            None,
                        ]):
                session.start()
            self.assertEqual(session.serial_port, 1002)
            self.assertEqual(session.qmp_port, 2002)
            session.stop()


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
        self.deferred_events = []
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

    def pump(self, preserve_events=False):
        pending = [] if preserve_events else self.deferred_events
        if not preserve_events:
            self.deferred_events = []
        if not self.events:
            return pending
        event = self.events.pop(0)
        self.progress.record(event)
        if preserve_events:
            self.deferred_events.append(event)
        return pending + [event]

    def execute_interaction(self, interaction):
        self.pump(preserve_events=True)

    def capture_screenshot(self, label):
        return None


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

    def test_terminal_event_during_interaction_is_preserved(self):
        session = FakeSession([{
            "event": "BEGIN", "case": "qemu:test", "iteration": "0",
            "seed": "1", "seq": "1"}, {
            "event": "PASS", "case": "qemu:test", "iteration": "0",
            "seed": "1", "seq": "2"}])
        result = runner.wait_for_case(
            session, "qemu:test", 0, 1, 1, 1,
            {"interaction": {"steps": [{"op": "wait", "seconds": 0}]}})
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
