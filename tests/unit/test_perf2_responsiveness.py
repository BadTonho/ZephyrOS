import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools import perf2_responsiveness as perf2
from tools import perf1_metrics
from tools import qemu_test_runner as runner


def metric(value, kind="counter"):
    return {
        "metric": "fixture",
        "value": str(value),
        "unit": "count",
        "kind": kind,
        "source": "fixture",
        "context": "test",
        "status": "ok" if value != "ND" else "unavailable",
        "resolution": "1",
        "overflow": "wrap_u32" if kind == "counter" else "none",
    }


def records():
    values = {name: 0 for name in perf2.PERF2_REQUIRED_METRICS}
    values.update({
        "mouse_press_events": 1,
        "mouse_release_events": 1,
        "mouse_wheel_events": 1,
        "mouse_wheel_supported": 1,
    })
    output = []
    for sequence in range(3):
        output.append({
            "seq": sequence,
            "baseline": "yes",
            "source": "fixture",
            "metrics": {
                name: metric(value, "state" if "state" in name or
                             name.endswith("supported") else "counter")
                for name, value in values.items()
            },
            "status": "ok",
        })
    return output


def trace():
    return [
        {"op": "stress_begin", "seconds": 10, "cycle_ms": 100},
        {"op": "key", "key": "c"},
        {"op": "pointer", "events": [
            {"type": "rel", "axis": "x", "value": 4},
            {"type": "rel", "axis": "y", "value": 2},
        ]},
        {"op": "pointer", "events": [
            {"type": "rel", "axis": "z", "value": 1},
        ]},
        {"op": "pointer", "events": [
            {"type": "btn", "button": "left", "down": True},
        ]},
        {"op": "pointer", "events": [
            {"type": "btn", "button": "left", "down": False},
        ]},
        {"op": "stress_end", "cycles": 1},
    ]


class Perf2ValidationTests(unittest.TestCase):
    def test_stimulus_counts_discrete_events_and_motion(self):
        self.assertEqual(
            perf2.count_stimulus_events(trace()),
            {"cycles": 1, "key_events": 1, "move_events": 2,
             "button_down": 1, "button_up": 1, "wheel_events": 1})

    def test_guest_validation_accepts_coalescible_motion(self):
        status, error, stimulus = perf2.validate_guest_records(
            records(), trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(stimulus["move_events"], 2)

    def test_guest_validation_rejects_nd_and_drops(self):
        incomplete = records()
        incomplete[-1]["metrics"]["mouse_wheel_events"] = metric("ND")
        status, error, _ = perf2.validate_guest_records(
            incomplete, trace(), {"event": "PASS"}, [])
        self.assertEqual(status, "FAIL")
        self.assertIn("metricas_guest_ND", error)

        dropped = records()
        dropped[-1]["metrics"]["mouse_raw_dropped"] = metric(1)
        status, error, _ = perf2.validate_guest_records(
            dropped, trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "contador_descartes:mouse_raw_dropped"))

    def test_guest_validation_distinguishes_qmp_blocked(self):
        status, error, _ = perf2.validate_guest_records(
            [], [], {"event": "BLOCKED"}, [])
        self.assertEqual((status, error), ("BLOCKED", "guest_blocked"))

    def test_report_preserves_nd_host_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            report = perf2.build_run_report(
                Path(directory) / "missing.img", "baseline", "simple", 1,
                records(), trace(), [{
                    "monotonic_seconds": 1.0,
                    "user_seconds": "ND",
                    "system_seconds": "ND",
                    "rss_bytes": "ND",
                    "peak_rss_bytes": "ND",
                }], {"event": "PASS"}, [], 10.0, None)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["host"]["sample_count"], 1)
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")
        self.assertEqual(report["schema"], perf2.PERF2_SCHEMA)

    def test_machine_parser_rejects_incomplete_duplicate_and_overflow(self):
        begin = "@@ZMETRIC/1 record=begin seq=1 baseline=ND source=fixture\n"
        end = "@@ZMETRIC/1 record=end seq=1 status=ok\n"
        with self.assertRaises(perf1_metrics.MetricsError):
            perf1_metrics.parse_machine_records(begin)
        duplicate = begin + (
            "@@ZMETRIC/1 record=metric metric=x value=1 unit=count kind=counter "
            "source=fixture context=test status=ok resolution=1 overflow=wrap_u32\n"
            "@@ZMETRIC/1 record=metric metric=x value=2 unit=count kind=counter "
            "source=fixture context=test status=ok resolution=1 overflow=wrap_u32\n" + end)
        with self.assertRaisesRegex(perf1_metrics.MetricsError, "metric_duplicada"):
            perf1_metrics.parse_machine_records(duplicate)
        overflow = begin.replace("seq=1", "seq=4294967296") + end.replace("seq=1", "seq=4294967296")
        with self.assertRaisesRegex(perf1_metrics.MetricsError, "seq_overflow"):
            perf1_metrics.parse_machine_records(overflow)

    def test_lane_selection_changes_profile_and_mode(self):
        case = {
            "id": perf2.PERF2_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": perf2.PERF2_CASE, "qemu_profile": "baseline",
            "timeout_seconds": 10, "heartbeat_timeout_seconds": 2,
            "isolation": "snapshot", "required_capabilities": [],
            "parameters": {}, "preconditions": "x", "action": "x",
            "expected": "x", "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "surface_ids": [],
            "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "stress", "seconds": 1, "cycle_ms": 100},
            ], "post_action": {"type": "none", "steps": []}},
        }
        selected = perf2.case_for_lane(case, "no-vesa", "simple")
        self.assertEqual(selected["qemu_profile"], "no-vesa")
        self.assertEqual(selected["interaction"]["steps"][0]["text"],
                         "guimode simple")

    def test_protocol_run_id_respects_ztest_capacity(self):
        run_id = perf2.protocol_run_id("baseline", "classic", 3)
        self.assertLess(len(run_id), 48)
        self.assertTrue(run_id.startswith("p2-b-c-3-qemu-"))


class HostSamplerTests(unittest.TestCase):
    def test_sampler_obeys_quarter_second_interval(self):
        session = SimpleNamespace()
        samples = []
        last = [0.0]
        with patch.object(perf2, "sample_process", side_effect=[
                {"monotonic_seconds": 0.1},
                {"monotonic_seconds": 0.2},
                {"monotonic_seconds": 0.6}]) as sampler:
            perf2._sample_host(session, 1, samples, last)
            perf2._sample_host(session, 1, samples, last)
            perf2._sample_host(session, 1, samples, last)
        self.assertEqual(len(samples), 1)
        self.assertEqual(sampler.call_count, 3)


class QmpInputTests(unittest.TestCase):
    def test_pointer_operations_emit_declarative_qmp_events(self):
        class FakeQmp:
            def __init__(self):
                self.commands = []

            def command(self, name, arguments):
                self.commands.append((name, arguments))
                return {}

        with tempfile.TemporaryDirectory() as directory:
            session = runner.QemuSession.__new__(runner.QemuSession)
            session.artifact_dir = Path(directory)
            session.input_trace = []
            session.qmp = FakeQmp()
            with patch.object(runner.time, "sleep"):
                session.send_pointer_move(3, -2)
                session.send_pointer_button("left", True)
                session.send_pointer_wheel(-1)
                session.send_pointer_drag("right", 1, 1, 2)
        self.assertEqual(len(session.qmp.commands), 7)
        self.assertEqual(session.qmp.commands[0][0], "input-send-event")
        self.assertEqual(session.qmp.commands[0][1], {"events": [
            {"type": "rel", "data": {"axis": "x", "value": 3}},
            {"type": "rel", "data": {"axis": "y", "value": -2}},
        ]})
        self.assertEqual(session.qmp.commands[2][1], {"events": [
            {"type": "btn", "data": {
                "button": "wheel-down", "down": True,
            }},
        ]})
        self.assertEqual(sum(entry["op"] == "pointer"
                             for entry in session.input_trace), 7)

    def test_input_step_validation_rejects_invalid_pointer_and_accepts_stress(self):
        runner.validate_input_step(
            {"op": "pointer_move", "dx": 1, "dy": -1}, "fixture")
        runner.validate_input_step(
            {"op": "pointer_drag", "button": "left", "dx": 1,
             "dy": 1, "steps": 4}, "fixture")
        runner.validate_input_step(
            {"op": "stress", "seconds": 10, "cycle_ms": 100}, "fixture")
        for step in (
                {"op": "pointer_move", "dx": 0, "dy": 0},
                {"op": "pointer_button", "button": "x", "down": True},
                {"op": "pointer_wheel", "delta": 0},
                {"op": "stress", "seconds": 31, "cycle_ms": 100}):
            with self.subTest(step=step):
                with self.assertRaises(runner.RunnerError):
                    runner.validate_input_step(step, "fixture")


if __name__ == "__main__":
    unittest.main()
