"""Testes host-only do contrato da auditoria RLS3."""

from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools import rls3_invariants as rls3
from tools.perf1_metrics import MetricsError, parse_machine_records, summarize_process_samples


class Rls3InvariantsTests(unittest.TestCase):
    def _record(self, sequence: int, overrides: dict[str, int | str] | None = None) -> dict:
        values = {name: 0 for name in rls3.RLS3_REQUIRED_METRICS}
        for name in (
            "invariant_valid", "invariant_ownership_valid",
            "invariant_security_valid", "invariant_supervisor_valid",
            "invariant_update_valid", "invariant_recovery_valid",
            "credentials_valid", "permissions_valid",
        ):
            values[name] = 1
        values["service_0_state"] = 1
        values["service_0_target"] = 1
        values["recovery_0_state"] = 1
        for name in rls3.RLS3_STABLE_RESIDUAL_METRICS:
            values[name] = 0
        if sequence == 6:
            values["vfs_failures"] = 6
        for index in rls3.RLS3_REQUIRED_RECOVERY_COMPONENTS:
            values[f"recovery_{index}_state"] = 1
            values[f"recovery_{index}_failures"] = 0
            values[f"recovery_{index}_last_error"] = 0
        if overrides:
            values.update(overrides)
        metrics = {
            name: {
                "metric": name,
                "value": str(value),
                "unit": "count",
                "kind": "gauge",
                "source": "test",
                "context": "test",
                "status": "ok",
                "resolution": "1",
                "overflow": "none",
            }
            for name, value in values.items()
        }
        return {"seq": sequence, "baseline": "1", "source": "test",
                "metrics": metrics, "status": "ok"}

    def test_delta_wraps_uint32(self) -> None:
        current = self._record(2, {"service_0_failures": 1})
        baseline = self._record(1, {"service_0_failures": 0xFFFFFFFF})
        self.assertEqual(rls3._delta(current, baseline, "service_0_failures"), 2)

    def test_phase_trace_requires_exact_sequence(self) -> None:
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        self.assertEqual(rls3.validate_phase_trace(trace), (True, None))
        self.assertFalse(rls3.validate_phase_trace(trace[:-1])[0])

    def test_valid_guest_records_pass(self) -> None:
        records = [self._record(index) for index in range(1, 7)]
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, observations = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "boot output\ntst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["invariants"]["valid"], 1)

    def test_invariant_error_fails(self) -> None:
        records = [self._record(index) for index in range(1, 6)]
        records.append(self._record(6, {"resource_invalid": 1}))
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "tst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual(status, "FAIL")
        self.assertIn("resource_invalid", error or "")

    def test_resource_residual_fails_against_baseline(self) -> None:
        records = [self._record(index) for index in range(1, 6)]
        records.append(self._record(6, {"vfs_descriptors_open": 1}))
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "tst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual(status, "FAIL")
        self.assertIn("vfs_descriptors_open", error or "")

    def test_new_error_counter_fails_even_when_baseline_had_no_error(self) -> None:
        records = [self._record(index) for index in range(1, 6)]
        records.append(self._record(6, {"service_0_failures": 1}))
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "tst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual(status, "FAIL")
        self.assertIn("service_0_failures", error or "")

    def test_required_nd_fails(self) -> None:
        records = [self._record(index) for index in range(1, 6)]
        records.append(self._record(6, {"permissions_valid": "ND"}))
        records[-1]["metrics"]["permissions_valid"]["value"] = "ND"
        records[-1]["metrics"]["permissions_valid"]["status"] = "unavailable"
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "tst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual(status, "FAIL")
        self.assertIn("permissions_valid", error or "")

    def test_optional_recovery_degradation_is_allowed(self) -> None:
        records = [self._record(index) for index in range(1, 7)]
        records[-1] = self._record(6, {
            "recovery_4_state": 3,
            "recovery_4_last_error": 4,
        })
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "tst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual((status, error), ("PASS", None))

    def test_critical_recovery_error_fails(self) -> None:
        records = [self._record(index) for index in range(1, 7)]
        records[-1] = self._record(6, {"recovery_2_last_error": 4})
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            records, trace, {"event": "PASS", "case": rls3.RLS3_CASE}, [],
            "tst5-rls3-invariants\nzephyr> ",
        )
        self.assertEqual(status, "FAIL")
        self.assertEqual(error, "erro_supervisor_ou_recovery")

    def test_protocol_duplicate_and_incomplete_envelopes_fail(self) -> None:
        line = (
            "@@ZMETRIC/1 record=begin seq=1 baseline=0 "
            "source=shell\n"
            "@@ZMETRIC/1 record=metric metric=a value=1 unit=count kind=gauge "
            "source=test context=test status=ok resolution=1 overflow=none\n"
            "@@ZMETRIC/1 record=end seq=1 status=ok\n"
        )
        self.assertEqual(len(parse_machine_records(line)), 1)
        with self.assertRaises(MetricsError):
            parse_machine_records(line.replace(
                "metric=a value=1", "metric=a value=1 metric=a"))
        with self.assertRaises(MetricsError):
            parse_machine_records(line.replace("record=end seq=1 status=ok\n", ""))

    def test_blocked_guest_is_not_pass(self) -> None:
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        status, error, _ = rls3.validate_guest_records(
            [], trace, {"event": "BLOCKED", "case": rls3.RLS3_CASE}, [], "",
        )
        self.assertEqual((status, error), ("BLOCKED", "guest_blocked"))

    def test_host_unavailable_is_nd(self) -> None:
        summary = summarize_process_samples([], "ND")
        self.assertEqual(summary["user_seconds"], "ND")
        self.assertEqual(summary["peak_rss_bytes"], "ND")

    def test_report_contains_schema_and_nd_host_summary(self) -> None:
        records = [self._record(index) for index in range(1, 7)]
        trace = [{"op": "phase", "phase": phase} for phase in rls3.RLS3_PHASES]
        with TemporaryDirectory() as temporary:
            artifact_dir = Path(temporary)
            (artifact_dir / "serial.log").write_text(
                "tst5-rls3-invariants\nzephyr> ", encoding="utf-8")
            report = rls3.build_run_report(
                Path("missing.img"), "baseline", "simple", 1, artifact_dir,
                records, trace, [], {"event": "PASS", "case": rls3.RLS3_CASE},
                [], "ND", None,
            )
        self.assertEqual(report["schema"], rls3.RLS3_SCHEMA)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["host"]["qemu_process"]["peak_rss_bytes"], "ND")

    def test_worker_count_is_bounded(self) -> None:
        self.assertEqual(rls3.worker_count(rls3.RLS3_MAX_WORKERS, 9), 9)
        with self.assertRaises(rls3.qemu.RunnerError):
            rls3.worker_count(0, 9)
        with self.assertRaises(rls3.qemu.RunnerError):
            rls3.worker_count(rls3.RLS3_MAX_WORKERS + 1, 9)

    def test_runner_accepts_rls3_phases(self) -> None:
        for phase in rls3.RLS3_PHASES:
            qemu_step = {"op": "phase", "phase": phase}
            rls3.qemu.validate_input_step(qemu_step, "qemu:tst5:rls3-invariants")


if __name__ == "__main__":
    unittest.main()
