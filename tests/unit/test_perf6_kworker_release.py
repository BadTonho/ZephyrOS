import tempfile
import unittest
from pathlib import Path

from tools import perf6_kworker_release as perf6
from tools import qemu_test_runner as runner


def _metric(name: str, value: int | str) -> dict[str, str]:
    state_names = {
        "scheduler_current_pid", "scheduler_current_tid",
        "scheduler_current_thread_generation",
        "scheduler_current_thread_kernel_service",
        "workqueue_worker_bound", "workqueue_worker_active",
        "workqueue_fallback_active", "workqueue_worker_target",
        "workqueue_worker_pid", "workqueue_worker_tid",
        "workqueue_worker_thread_generation",
        "workqueue_worker_process_generation", "workqueue_execution_context",
        "service_0_state", "service_0_target", "service_0_pid",
        "service_0_tid", "service_0_thread_generation",
        "service_0_fallback_active",
    }
    kind = "state" if name in state_names else "counter"
    unit = "enum" if kind == "state" else "count"
    return {
        "metric": name, "value": str(value), "unit": unit, "kind": kind,
        "source": "fixture", "context": "test",
        "status": "unavailable" if value == "ND" else "ok",
        "resolution": "1", "overflow": "none" if kind == "state" else "wrap_u32",
    }


def _records() -> list[dict[str, object]]:
    names = set(perf6.PERF6_REQUIRED_METRICS) | set(perf6.PERF6_ZERO_METRICS)
    values = {name: 0 for name in names}
    values.update({
        "pit_ticks": 1000, "scheduler_idle_ticks": 900,
        "scheduler_active_ticks": 100, "scheduler_idle_entries": 20,
        "scheduler_idle_hlt_returns": 20, "scheduler_current_pid": 1,
        "workqueue_worker_bound": 1, "workqueue_worker_active": 1,
        "workqueue_execution_context": 1, "workqueue_worker_target": 2,
        "workqueue_worker_tid": 7, "workqueue_worker_thread_generation": 33,
        "workqueue_worker_pid": 0, "workqueue_worker_process_generation": 0,
        "service_0_state": 1, "service_0_target": 1, "service_0_pid": 0,
        "service_0_tid": 7, "service_0_thread_generation": 33,
    })
    output = []
    for sequence, ticks in enumerate((0, 1000, 1400), 1):
        current = dict(values)
        current.update({
            "pit_ticks": ticks,
            "scheduler_idle_ticks": (ticks * 9) // 10,
            "scheduler_active_ticks": ticks // 10,
            "scheduler_idle_entries": ticks // 10,
            "scheduler_idle_hlt_returns": ticks // 10,
            "scheduler_wakeups": sequence * 2,
            "scheduler_wake_latency_samples": sequence,
            "scheduler_wake_latency_ticks": sequence * 3,
            "scheduler_wake_latency_max_ticks": 2,
            "workqueue_dispatch_latency_samples": sequence,
            "workqueue_dispatch_latency_ticks": sequence * 4,
            "workqueue_max_dispatch_latency_ticks": 2,
        })
        output.append({
            "seq": sequence, "baseline": "reset", "source": "fixture",
            "metrics": {name: _metric(name, value)
                        for name, value in current.items()},
            "status": "ok",
        })
    return output


def _trace() -> list[dict[str, object]]:
    return [{"op": "phase", "phase": phase} for phase in perf6.PERF6_PHASES]


class Perf6ValidationTests(unittest.TestCase):
    def test_phase_identity_and_queue_validation(self):
        status, error, observations = perf6.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["worker_tid"], 7)
        self.assertEqual(observations["worker_thread_generation"], 33)

    def test_rejects_process_worker_or_fallback(self):
        records = _records()
        records[-1]["metrics"]["workqueue_worker_pid"] = _metric(
            "workqueue_worker_pid", 42)
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error),
                         ("FAIL", "kworker_consumiu_slot_de_processo"))

        records = _records()
        records[-1]["metrics"]["workqueue_fallback_active"] = _metric(
            "workqueue_fallback_active", 1)
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "fallback_kworker_ativo"))

        records = _records()
        records[-1]["metrics"]["workqueue_execution_context"] = _metric(
            "workqueue_execution_context", 0)
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "contexto_kworker_invalido"))

    def test_rejects_new_or_runnable_workqueue_residue(self):
        records = _records()
        records[1]["metrics"]["workqueue_delayed"] = _metric(
            "workqueue_delayed", 2)
        records[1]["metrics"]["workqueue_pending"] = _metric(
            "workqueue_pending", 2)
        records[-1]["metrics"]["workqueue_delayed"] = _metric(
            "workqueue_delayed", 3)
        records[-1]["metrics"]["workqueue_pending"] = _metric(
            "workqueue_pending", 3)
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "fila_workqueue_residual"))

        records = _records()
        records[-1]["metrics"]["workqueue_pending"] = _metric(
            "workqueue_pending", 1)
        records[-1]["metrics"]["workqueue_ready_normal"] = _metric(
            "workqueue_ready_normal", 1)
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "fila_workqueue_residual"))

    def test_nd_partial_protocol_and_blocked(self):
        records = _records()
        records[-1]["status"] = "partial"
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "envelope_guest_partial"))

        records = _records()
        records[-1]["metrics"]["service_0_tid"] = _metric("service_0_tid", "ND")
        status, error, _ = perf6.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertIn("metricas_guest_ND", error)

        status, error, _ = perf6.validate_guest_records(
            [], _trace(), {"event": "BLOCKED"}, [])
        self.assertEqual((status, error), ("BLOCKED", "guest_blocked"))
        status, error, _ = perf6.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, ["duplicate_key"])
        self.assertIn("protocolo_guest", error)

    def test_wraparound_report_and_host_nd(self):
        current = {"metrics": {"counter": _metric("counter", 2)}}
        previous = {"metrics": {"counter": _metric("counter", 0xFFFFFFFE)}}
        self.assertEqual(perf6._delta(current, previous, "counter"), 4)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = perf6.build_run_report(
                root / "missing.img", "baseline", "simple", 1, root,
                _records(), _trace(), [], {"event": "PASS"}, [], 1.0, None,
                lifecycle={"status": "PASS", "event": "RESET"})
        self.assertEqual(report["schema"], perf6.PERF6_SCHEMA)
        self.assertEqual(report["release"]["version"], "0.1.0")
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")
        self.assertEqual(report["reboot"]["event"], "RESET")

    def test_lane_selection_and_phase_operation(self):
        case = {
            "id": perf6.PERF6_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": perf6.PERF6_CASE, "qemu_profile": "baseline",
            "timeout_seconds": 240, "heartbeat_timeout_seconds": 20,
            "isolation": "snapshot", "parameters": {
                "iterations": 3, "idle_seconds": 3, "load_seconds": 10,
                "host_sample_interval_seconds": 0.25,
                "release_version": "0.1.0"},
            "preconditions": "x", "action": "x", "expected": "x",
            "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "required_capabilities": [],
            "surface_ids": [], "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "phase", "phase": "pressure"}],
                "post_action": {"type": "none", "steps": []}},
        }
        selected = perf6.case_for_lane(case, "baseline", "classic")
        self.assertEqual(selected["interaction"]["steps"][0]["text"],
                         "guimode classic")
        runner.validate_input_step({"op": "phase", "phase": "pressure"},
                                   "fixture")
        runner.validate_input_step({"op": "phase", "phase": "cancel"},
                                   "fixture")

    def test_parallel_matrix_plan_and_worker_limit(self):
        plan = perf6.matrix_plan({})
        self.assertEqual(len(plan), 9)
        self.assertEqual(plan[0][:3], ("baseline", "simple", 1))
        self.assertEqual(plan[-1][:3], ("no-vesa", "simple", 3))
        self.assertEqual(perf6.worker_count(3, len(plan)), 3)
        self.assertEqual(perf6.worker_count(9, len(plan)), 9)
        with self.assertRaises(runner.RunnerError):
            perf6.worker_count(0, len(plan))
        with self.assertRaises(runner.RunnerError):
            perf6.worker_count(perf6.PERF6_MAX_WORKERS + 1, len(plan))


if __name__ == "__main__":
    unittest.main()
