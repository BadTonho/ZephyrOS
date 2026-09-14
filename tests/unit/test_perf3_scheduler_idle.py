import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools import perf1_metrics
from tools import perf3_scheduler_idle as perf3
from tools import qemu_test_runner as runner


def _metric(name: str, value: int | str) -> dict[str, str]:
    state = name.endswith("_error") or name in {
        "scheduler_current_pid", "workqueue_worker_bound",
        "workqueue_worker_active", "workqueue_execution_context",
    }
    duration = name.endswith("_max_ticks")
    kind = "state" if state else "duration" if duration else "counter"
    unit = "code" if name.endswith("_error") else "enum" if state else "tick"
    if kind == "counter":
        overflow = "wrap_u32"
    else:
        overflow = "none"
    return {
        "metric": name,
        "value": str(value),
        "unit": unit,
        "kind": kind,
        "source": "fixture",
        "context": "test",
        "status": "unavailable" if value == "ND" else "ok",
        "resolution": "1",
        "overflow": overflow,
    }


def _records() -> list[dict[str, object]]:
    values = {name: 0 for name in perf3.PERF3_REQUIRED_METRICS}
    values.update({
        "pit_ticks": 0,
        "scheduler_idle_ticks": 0,
        "scheduler_active_ticks": 0,
        "scheduler_idle_entries": 0,
        "scheduler_idle_hlt_returns": 0,
        "scheduler_current_pid": 1,
        "workqueue_worker_bound": 1,
        "workqueue_worker_active": 1,
        "workqueue_execution_context": 1,
    })
    output = []
    for sequence, (pit, idle, active, entries, returns) in enumerate((
            (0, 0, 0, 0, 0), (3000, 2990, 10, 3000, 2999),
            (4000, 3500, 500, 3500, 3499))):
        current = dict(values)
        current.update({
            "pit_ticks": pit,
            "scheduler_idle_ticks": idle,
            "scheduler_active_ticks": active,
            "scheduler_idle_entries": entries,
            "scheduler_idle_hlt_returns": returns,
            "scheduler_wakeups": sequence * 2,
            "scheduler_wake_latency_samples": sequence,
            "scheduler_wake_latency_ticks": sequence * 4,
            "scheduler_wake_latency_max_ticks": 2,
            "workqueue_dispatch_latency_samples": sequence,
            "workqueue_dispatch_latency_ticks": sequence * 3,
            "workqueue_max_dispatch_latency_ticks": 1,
        })
        output.append({
            "seq": sequence + 1,
            "baseline": "reset",
            "source": "fixture",
            "metrics": {name: _metric(name, value)
                        for name, value in current.items()},
            "status": "ok",
        })
    return output


def _trace() -> list[dict[str, object]]:
    return [{"op": "phase", "phase": phase}
            for phase in ("boot", "idle", "load", "cleanup", "final")]


class Perf3ValidationTests(unittest.TestCase):
    def test_phase_order_and_guest_validation(self):
        self.assertEqual(perf3.phase_sequence(_trace()),
                         ["boot", "idle", "load", "cleanup", "final"])
        self.assertEqual(perf3.validate_phase_trace(_trace()), (True, None))
        status, error, observations = perf3.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["idle_ticks"], 2990)
        self.assertEqual(observations["workqueue_dispatch_latency_samples"], 2)

    def test_phase_order_rejects_missing_or_duplicate_markers(self):
        trace = _trace()[:-1] + [{"op": "phase", "phase": "cleanup"},
                                 {"op": "phase", "phase": "final"}]
        valid, error = perf3.validate_phase_trace(trace)
        self.assertFalse(valid)
        self.assertIn("fases_invalidas", error)

    def test_guest_validation_rejects_nd_partial_and_accounting(self):
        incomplete = _records()
        incomplete[-1]["status"] = "partial"
        status, error, _ = perf3.validate_guest_records(
            incomplete, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "envelope_guest_partial"))

        unavailable = _records()
        unavailable[-1]["metrics"]["scheduler_wakeups"] = _metric(
            "scheduler_wakeups", "ND")
        status, error, _ = perf3.validate_guest_records(
            unavailable, _trace(), {"event": "PASS"}, [])
        self.assertIn("metricas_guest_ND", error)

        inconsistent = _records()
        inconsistent[1]["metrics"]["scheduler_active_ticks"] = _metric(
            "scheduler_active_ticks", 11)
        status, error, _ = perf3.validate_guest_records(
            inconsistent, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error),
                         ("FAIL", "contabilidade_idle_inconsistente"))

    def test_guest_validation_rejects_runtime_error_queue_and_blocked(self):
        errored = _records()
        errored[-1]["metrics"]["workqueue_wake_errors"] = _metric(
            "workqueue_wake_errors", 1)
        status, error, _ = perf3.validate_guest_records(
            errored, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "erro_runtime:workqueue_wake_errors"))

        pending = _records()
        pending[-1]["metrics"]["workqueue_pending"] = _metric(
            "workqueue_pending", 1)
        pending[-1]["metrics"]["workqueue_ready_normal"] = _metric(
            "workqueue_ready_normal", 1)
        status, error, _ = perf3.validate_guest_records(
            pending, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "fila_workqueue_residual"))

        status, error, _ = perf3.validate_guest_records(
            [], _trace(), {"event": "BLOCKED"}, [])
        self.assertEqual((status, error), ("BLOCKED", "guest_blocked"))

    def test_guest_validation_rejects_protocol_and_kworker(self):
        status, error, _ = perf3.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, ["duplicate"])
        self.assertEqual(status, "FAIL")
        self.assertIn("protocolo_guest", error)

        no_worker = _records()
        no_worker[-1]["metrics"]["workqueue_worker_bound"] = _metric(
            "workqueue_worker_bound", 0)
        status, error, _ = perf3.validate_guest_records(
            no_worker, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "kworker_nao_vinculada"))

    def test_wraparound_delta_is_uint32(self):
        current = {"metrics": {"counter": _metric("counter", 2)}}
        previous = {"metrics": {"counter": _metric("counter", 0xFFFFFFFE)}}
        self.assertEqual(perf3._delta(current, previous, "counter"), 4)

    def test_report_preserves_host_nd_and_model_decision(self):
        with tempfile.TemporaryDirectory() as directory:
            report = perf3.build_run_report(
                Path(directory) / "missing.img", "baseline", "simple", 1,
                _records(), _trace(), [{
                    "monotonic_seconds": 1.0,
                    "user_seconds": "ND", "system_seconds": "ND",
                    "rss_bytes": "ND", "peak_rss_bytes": "ND",
                }], {"event": "PASS"}, [], 10.0, None)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")
        self.assertEqual(report["scheduler_model"]["thread_scheduler"], "isolated")
        self.assertEqual(report["schema"], perf3.PERF3_SCHEMA)

    def test_static_idle_sequence_is_present(self):
        self.assertTrue(perf3.validate_idle_source())
        self.assertTrue(perf3.validate_protocol_idle_path())

    def test_lane_selection_replaces_mode(self):
        case = {
            "id": perf3.PERF3_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": perf3.PERF3_CASE, "qemu_profile": "baseline",
            "required_capabilities": [], "timeout_seconds": 180,
            "heartbeat_timeout_seconds": 20, "isolation": "snapshot",
            "parameters": {"iterations": 3, "idle_seconds": 3,
                            "load_seconds": 10,
                            "host_sample_interval_seconds": 0.25},
            "preconditions": "x", "action": "x", "expected": "x",
            "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "surface_ids": [],
            "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "phase", "phase": "boot"},
            ], "post_action": {"type": "none", "steps": []}},
        }
        selected = perf3.case_for_lane(case, "baseline", "classic")
        self.assertEqual(selected["interaction"]["steps"][0]["text"],
                         "guimode classic")

    def test_lane_selection_adds_final_quiescence(self):
        case = {
            "id": perf3.PERF3_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": perf3.PERF3_CASE, "qemu_profile": "baseline",
            "required_capabilities": [], "timeout_seconds": 180,
            "heartbeat_timeout_seconds": 20, "isolation": "snapshot",
            "parameters": {"iterations": 3, "idle_seconds": 3,
                            "load_seconds": 10,
                            "host_sample_interval_seconds": 0.25},
            "preconditions": "x", "action": "x", "expected": "x",
            "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "surface_ids": [],
            "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "wait", "seconds": 1},
                {"op": "phase", "phase": "final"},
            ], "post_action": {"type": "none", "steps": []}},
        }
        selected = perf3.case_for_lane(case, "baseline", "simple")
        self.assertEqual(selected["interaction"]["steps"][1]["seconds"],
                         perf3.PERF3_FINAL_QUIESCENCE_SECONDS)

        final_index = next(index for index, step in enumerate(
            selected["interaction"]["steps"])
            if step.get("op") == "phase" and step.get("phase") == "final")
        self.assertEqual(selected["interaction"]["steps"][final_index - 3],
                         {"op": "text", "text": "workq check"})
        self.assertEqual(selected["interaction"]["steps"][final_index - 1],
                         {"op": "wait",
                          "seconds": perf3.PERF3_FINAL_DRAIN_SECONDS})


class Perf3RunnerTests(unittest.TestCase):
    def test_phase_operation_is_validated_and_recorded(self):
        runner.validate_input_step({"op": "phase", "phase": "idle"}, "fixture")
        with self.assertRaises(runner.RunnerError):
            runner.validate_input_step({"op": "phase", "phase": "unknown"}, "fixture")
        with tempfile.TemporaryDirectory() as directory:
            session = runner.QemuSession.__new__(runner.QemuSession)
            session.artifact_dir = Path(directory)
            session.input_trace = []
            session.mark_phase("idle")
            self.assertEqual(session.input_trace[0]["phase"], "idle")

    def test_machine_parser_rejects_incomplete_and_duplicate_keys(self):
        begin = "@@ZMETRIC/1 record=begin seq=1 baseline=ND source=fixture\n"
        end = "@@ZMETRIC/1 record=end seq=1 status=ok\n"
        with self.assertRaises(perf1_metrics.MetricsError):
            perf1_metrics.parse_machine_records(begin)
        duplicate = begin + (
            "@@ZMETRIC/1 record=metric metric=x value=1 unit=count kind=counter "
            "source=fixture context=test status=ok resolution=1 overflow=wrap_u32 "
            "metric=x\n" + end)
        with self.assertRaises(perf1_metrics.MetricsError):
            perf1_metrics.parse_machine_records(duplicate)


if __name__ == "__main__":
    unittest.main()
