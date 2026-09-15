import tempfile
import unittest
from pathlib import Path

from tools import qemu_test_runner as runner
from tools import rls2_shell_liveness as rls2


def _metric(name: str, value: int | str) -> dict[str, str]:
    state_names = {
        "shell_lifecycle_generation", "shell_lifecycle_last_error",
        "shell_lifecycle_last_layer", "shell_lifecycle_prompt_state",
        "shell_lifecycle_operation_active", "shell_lifecycle_input_blocked",
        "shell_lifecycle_terminal_active", "shell_lifecycle_hosted_visible",
        "shell_lifecycle_focus_shell", "shell_lifecycle_scene_active",
        "shell_lifecycle_job_active", "shell_lifecycle_loader_active",
    }
    kind = "state" if name in state_names else "counter"
    unit = "enum" if kind == "state" else "count"
    return {
        "metric": name,
        "value": str(value),
        "unit": unit,
        "kind": kind,
        "source": "fixture",
        "context": "test",
        "status": "unavailable" if value == "ND" else "ok",
        "resolution": "1",
        "overflow": "none" if kind == "state" else "wrap_u32",
    }


def _records() -> list[dict[str, object]]:
    values = {name: 0 for name in rls2.RLS2_REQUIRED_METRICS}
    values.update({
        "shell_lifecycle_generation": 1,
        "shell_lifecycle_last_layer": 1,
        "shell_lifecycle_prompt_state": 2,
        "shell_lifecycle_terminal_active": 1,
        "shell_lifecycle_focus_shell": 1,
    })
    records = []
    for sequence in range(1, len(rls2.RLS2_PHASES) + 1):
        current = dict(values)
        current.update({
            "shell_lifecycle_finalization_requests": sequence,
            "shell_lifecycle_finalizations": sequence,
            "shell_lifecycle_prompt_requests": sequence,
            "shell_lifecycle_prompt_reconciliations": sequence,
            "shell_lifecycle_prompt_rendered": sequence,
        })
        records.append({
            "seq": sequence,
            "baseline": "reset",
            "source": "fixture",
            "metrics": {name: _metric(name, value)
                        for name, value in current.items()},
            "status": "ok",
        })
    return records


def _trace() -> list[dict[str, object]]:
    return [{"op": "phase", "phase": phase} for phase in rls2.RLS2_PHASES]


class Rls2ValidationTests(unittest.TestCase):
    def test_validates_phases_layers_and_final_prompt(self):
        status, error, observations = rls2.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, [],
            "@@ZMETRIC/1 record=end seq=7 status=ok\nzephyr>")
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["final_prompt_state"], 2)
        self.assertEqual(observations["layers_observed"], [1])

    def test_accepts_snapshot_taken_inside_dispatcher(self):
        records = _records()
        records[-1]["metrics"]["shell_lifecycle_operation_active"] = _metric(
            "shell_lifecycle_operation_active", 1)
        records[-1]["metrics"]["shell_lifecycle_prompt_state"] = _metric(
            "shell_lifecycle_prompt_state", 0)
        status, error, observations = rls2.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [],
            "@@ZMETRIC/1 record=end seq=7 status=ok\nzephyr>")
        self.assertEqual((status, error), ("PASS", None))
        self.assertTrue(observations["snapshot_in_dispatcher"])

    def test_accepts_guest_blackbox_prompt_without_serial_terminal(self):
        status, error, observations = rls2.validate_guest_records(
            _records(), _trace(),
            {"event": "PASS", "case": rls2.RLS2_CASE}, [],
            "@@ZMETRIC/1 record=end seq=7 status=ok\n")
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["prompt_observations"]["source"],
                         "guest_blackbox")

    def test_rejects_incomplete_phase_prompt_and_residual(self):
        trace = _trace()[:-1]
        status, error, _ = rls2.validate_guest_records(
            _records(), trace, {"event": "PASS"}, [], "zephyr>")
        self.assertEqual((status, error), ("FAIL", "fases_invalidas:" +
                         str([item["phase"] for item in trace])))

        status, error, _ = rls2.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, [], "")
        self.assertEqual((status, error), ("FAIL", "prompt_ausente"))

        records = _records()
        records[-1]["metrics"]["shell_lifecycle_scene_active"] = _metric(
            "shell_lifecycle_scene_active", 1)
        status, error, _ = rls2.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [], "zephyr>")
        self.assertEqual((status, error), ("FAIL", "recursos_lifecycle_residuais"))

    def test_rejects_duplicate_finalization_nd_protocol_and_blocked(self):
        records = _records()
        records[-1]["metrics"]["shell_lifecycle_duplicate_finalizations"] = _metric(
            "shell_lifecycle_duplicate_finalizations", 1)
        status, error, _ = rls2.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [], "zephyr>")
        self.assertEqual((status, error),
                         ("FAIL", "prompt_ou_finalizacao_duplicada"))

        records = _records()
        records[-1]["metrics"]["shell_lifecycle_prompt_state"] = _metric(
            "shell_lifecycle_prompt_state", "ND")
        status, error, _ = rls2.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [], "zephyr>")
        self.assertIn("metricas_guest_ND", error)

        status, error, _ = rls2.validate_guest_records(
            [], _trace(), {"event": "BLOCKED"}, [])
        self.assertEqual((status, error), ("BLOCKED", "guest_blocked"))
        status, error, _ = rls2.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, ["duplicate_key"], "zephyr>")
        self.assertIn("protocolo_guest", error)

        status, error, _ = rls2.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, [],
            "@@ZMETRIC/1 record=end seq=7 status=ok\nzephyr>zephyr>")
        self.assertEqual((status, error), ("FAIL", "prompt_duplicado"))

    def test_wraparound_report_and_host_nd(self):
        current = {"metrics": {"counter": _metric("counter", 2)}}
        previous = {"metrics": {"counter": _metric("counter", 0xFFFFFFFE)}}
        self.assertEqual(rls2._delta(current, previous, "counter"), 4)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "serial.log").write_text("zephyr>\n", encoding="utf-8")
            report = rls2.build_run_report(
                root / "missing.img", "baseline", "simple", 1, root,
                _records(), _trace(), [], {"event": "PASS"}, [], 1.0, None)
        self.assertEqual(report["schema"], rls2.RLS2_SCHEMA)
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")

    def test_lane_matrix_and_declared_phase_operations(self):
        case = {
            "id": rls2.RLS2_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": rls2.RLS2_CASE, "qemu_profile": "baseline",
            "timeout_seconds": 300, "heartbeat_timeout_seconds": 20,
            "isolation": "snapshot", "required_capabilities": [],
            "parameters": {"iterations": 3, "idle_seconds": 3,
                            "host_sample_interval_seconds": 0.25,
                            "liveness_schema": rls2.RLS2_SCHEMA,
                            "phases": ",".join(rls2.RLS2_PHASES)},
            "preconditions": "x", "action": "x", "expected": "x",
            "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "surface_ids": [],
            "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "phase", "phase": "commands"}],
                "post_action": {"type": "none", "steps": []}},
        }
        selected = rls2.case_for_lane(case, "baseline", "classic")
        self.assertEqual(selected["interaction"]["steps"][0]["text"],
                         "guimode classic")
        self.assertEqual(len(rls2.matrix_plan()), 9)
        self.assertEqual(rls2.worker_count(3, 9), 3)
        with self.assertRaises(runner.RunnerError):
            rls2.worker_count(0, 9)
        for phase in ("commands", "jobs", "scenes"):
            runner.validate_input_step({"op": "phase", "phase": phase},
                                       "rls2-fixture")

    def test_catalog_separates_generic_f12_and_regcheck_f11_cancellation(self):
        case = rls2.select_case(
            Path(__file__).resolve().parents[2] / "tests" / "catalog.json")
        steps = case["interaction"]["steps"]
        job_index = next(index for index, step in enumerate(steps)
                         if step.get("op") == "phase" and
                         step.get("phase") == "jobs")
        self.assertEqual(steps[job_index + 1]["text"], "q2check")
        self.assertEqual(steps[job_index + 4]["key"], "f12")
        cancel_index = next(index for index, step in enumerate(steps)
                            if step.get("op") == "phase" and
                            step.get("phase") == "cancel")
        self.assertEqual(steps[cancel_index + 1]["text"], "regcheck full")
        self.assertEqual(steps[cancel_index + 4]["key"], "f11")

        classic = rls2.case_for_lane(case, "baseline", "classic")
        classic_steps = classic["interaction"]["steps"]
        self.assertFalse(any(step.get("op") == "key" and
                             step.get("key") == "esc"
                             for step in classic_steps))
        self.assertEqual(sum(step.get("op") == "keys" and
                             step.get("keys") == ["alt", "f4"]
                             for step in classic_steps), 2)


if __name__ == "__main__":
    unittest.main()
