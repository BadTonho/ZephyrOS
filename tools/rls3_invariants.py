"""Audita invariantes de recursos, segurança, supervisor e recovery no QEMU."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys
import time
from types import SimpleNamespace
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import qemu_test_runner as qemu
from tools.perf1_metrics import (
    MetricsError,
    aggregate_records,
    parse_machine_file,
    sample_process,
    summarize_process_samples,
    write_report,
)


RLS3_CASE = "qemu:tst5:rls3-invariants"
RLS3_SCHEMA = "zephyros-rls3-invariants-v1"
RLS3_MANIFEST_SCHEMA = "zephyros-rls3-invariants-manifest-v1"
RLS3_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
RLS3_NA_LANES = ({
    "profile": "no-vesa",
    "mode": "classic",
    "status": "NOT_APPLICABLE",
    "reason": "fallback fixado em Simple",
},)
RLS3_ITERATIONS = 3
RLS3_IDLE_SECONDS = 3.0
RLS3_HOST_SAMPLE_SECONDS = 0.25
RLS3_DEFAULT_WORKERS = 3
RLS3_MAX_WORKERS = len(RLS3_LANES) * RLS3_ITERATIONS
RLS3_PHASES = ("boot", "baseline", "audit", "recovery", "repeat", "final")
UINT32_MASK = 0xFFFFFFFF

RLS3_REQUIRED_METRICS = (
    "invariant_valid",
    "invariant_ownership_valid",
    "invariant_security_valid",
    "invariant_supervisor_valid",
    "invariant_update_valid",
    "invariant_recovery_valid",
    "invariant_domain_failures",
    "invariant_last_error",
    "resource_invalid",
    "permissions_valid",
    "credentials_valid",
    "workqueue_pending",
    "workqueue_running",
    "workqueue_invariant_errors",
    "vfs_failures",
    "block_failed",
    "cache_errors",
    "vfs_descriptors_open",
    "vfs_mounts_active",
    "vfs_pipes_active",
    "update_transaction_pending",
    "update_recovery_pending",
    "update_journal_pending",
    "update_recovery_pending_slots",
    "service_0_state",
    "service_0_target",
    "service_0_failures",
    "service_0_last_error",
    "recovery_0_state",
    "recovery_0_failures",
    "recovery_0_last_error",
)
RLS3_REQUIRED_RECOVERY_COMPONENTS = (2, 3, 5, 6, 16, 21, 22, 24)

RLS3_COUNTER_METRICS = (
    "workqueue_invariant_errors",
    "vfs_failures",
    "block_failed",
    "cache_errors",
    "net_buffer_invalid_transitions",
    "net_buffer_duplicate_completions",
    "net_socket_stale_handles",
    "service_0_failures",
    "recovery_0_failures",
)
RLS3_ERROR_COUNTER_METRICS = (
    "workqueue_invariant_errors",
    "vfs_failures",
    "block_failed",
    "cache_errors",
    "net_buffer_invalid_transitions",
    "net_buffer_duplicate_completions",
    "net_socket_stale_handles",
    "service_0_failures",
    "recovery_0_failures",
)
RLS3_EXPECTED_ERROR_DELTAS = {
    "vfs_failures": 6,
}
RLS3_REQUIRED_TRUE_METRICS = (
    "invariant_valid",
    "invariant_ownership_valid",
    "invariant_security_valid",
    "invariant_supervisor_valid",
    "invariant_update_valid",
    "invariant_recovery_valid",
)
RLS3_ZERO_METRICS = (
    "invariant_domain_failures",
    "invariant_last_error",
    "resource_invalid",
    "workqueue_invariant_errors",
    "update_transaction_pending",
    "update_recovery_pending",
    "update_journal_pending",
    "update_recovery_pending_slots",
)
RLS3_STABLE_RESIDUAL_METRICS = (
    "vfs_descriptors_open",
    "vfs_mounts_active",
    "vfs_pipes_active",
    "block_queue_depth",
    "block_in_flight",
    "cache_reading_entries",
    "cache_dirty_entries",
    "cache_writeback_entries",
    "workqueue_pending",
    "workqueue_running",
)


def _arguments(options: argparse.Namespace, profile: str) -> argparse.Namespace:
    return SimpleNamespace(
        image=str(options.image), catalog=str(options.catalog),
        results=str(options.results), run_id=None, qemu=options.qemu,
        qemu_arg=list(options.qemu_arg or []), cpu=options.cpu,
        qemu_profile=profile, network=options.network, storage_image=None,
        snapshot=options.snapshot, fixture="readonly-update", profile="smoke",
        boot_timeout=options.boot_timeout, case_timeout=options.case_timeout,
        suite_timeout=options.suite_timeout,
        heartbeat_timeout=options.heartbeat_timeout, coverage_symbols=None,
    )


def _relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.name


def select_case(catalog_path: Path) -> dict[str, Any]:
    catalog = qemu.load_catalog(catalog_path)
    case = qemu.select_case(catalog, RLS3_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_rls3_invalidos", "catalog_error", True)
    expected = {
        "iterations": RLS3_ITERATIONS,
        "idle_seconds": int(RLS3_IDLE_SECONDS),
        "host_sample_interval_seconds": RLS3_HOST_SAMPLE_SECONDS,
        "invariant_schema": RLS3_SCHEMA,
        "input_key_gap_seconds": 0.05,
    }
    for key, value in expected.items():
        if parameters.get(key) != value:
            raise qemu.RunnerError(f"parametro_rls3_invalido:{key}",
                                    "catalog_error", True)
    if parameters.get("phases") != ",".join(RLS3_PHASES):
        raise qemu.RunnerError("fases_rls3_invalidas", "catalog_error", True)
    if parameters.get("recovery_fixture") != "readonly-update":
        raise qemu.RunnerError("fixture_recovery_rls3_invalida",
                               "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_rls3_ausente", "catalog_error", True)
    return case


def case_for_lane(case: dict[str, Any], profile: str,
                  mode: str) -> dict[str, Any]:
    if (profile, mode) not in RLS3_LANES:
        raise qemu.RunnerError("faixa_rls3_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or not isinstance(interaction.get("steps"), list):
        raise qemu.RunnerError("passos_rls3_ausentes", "catalog_error", True)
    steps: list[dict[str, Any]] = []
    mode_seen = False
    for step in interaction["steps"]:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_rls3_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_rls3_ausente", "catalog_error", True)
    selected = dict(case)
    selected["qemu_profile"] = profile
    selected_interaction = dict(interaction)
    selected_interaction["steps"] = steps
    selected["interaction"] = selected_interaction
    qemu.validate_case_for_runner(selected)
    return selected


def _metric_value(record: dict[str, Any], name: str) -> int | None:
    metric = record.get("metrics", {}).get(name)
    if not isinstance(metric, dict) or metric.get("value") == "ND":
        return None
    try:
        value = int(metric.get("value"), 10)
    except (TypeError, ValueError):
        return None
    return value if 0 <= value <= UINT32_MASK else None


def _delta(record: dict[str, Any], baseline: dict[str, Any], name: str) -> int | None:
    current = _metric_value(record, name)
    previous = _metric_value(baseline, name)
    if current is None or previous is None:
        return None
    return (current - previous) & UINT32_MASK


def phase_sequence(trace: list[dict[str, Any]]) -> list[str]:
    return [str(entry.get("phase")) for entry in trace
            if entry.get("op") == "phase"]


def validate_phase_trace(trace: list[dict[str, Any]]) -> tuple[bool, str | None]:
    actual = phase_sequence(trace)
    if actual != list(RLS3_PHASES):
        return False, f"fases_invalidas:{actual}"
    return True, None


def _final_prompt_observation(serial_text: str) -> tuple[int, str]:
    marker_offset = serial_text.rfind("tst5-rls3-invariants")
    if marker_offset >= 0:
        return serial_text[marker_offset:].count("zephyr>"), "after_marker"
    record_offset = serial_text.rfind("@@ZMETRIC/1 record=end")
    if record_offset >= 0:
        return serial_text[record_offset:].count("zephyr>"), "after_final_record"
    return serial_text.count("zephyr>"), "serial"


def _nonzero_metrics(record: dict[str, Any], names: tuple[str, ...]) -> list[str]:
    return [name for name in names
            if _metric_value(record, name) not in (None, 0)]


def validate_guest_records(
    records: list[dict[str, Any]], trace: list[dict[str, Any]],
    event: dict[str, Any] | None, protocol_errors: list[str],
    serial_text: str = "",
) -> tuple[str, str | None, dict[str, Any]]:
    phase_valid, phase_error = validate_phase_trace(trace)
    prompt_count, prompt_source = _final_prompt_observation(serial_text)
    guest_event = bool(event and event.get("event") == "PASS" and
                       event.get("case") == RLS3_CASE)
    if guest_event and prompt_count == 0:
        prompt_count = 1
        prompt_source = "guest_blackbox"
    observations: dict[str, Any] = {
        "phases": phase_sequence(trace),
        "sample_count": len(records),
        "prompt_observations": {
            "expected": 1, "observed": prompt_count, "source": prompt_source,
            "total_serial": serial_text.count("zephyr>"),
            "guest_blackbox": guest_event,
        },
    }
    if protocol_errors:
        return "FAIL", "protocolo_guest:" + ",".join(protocol_errors), observations
    if not phase_valid:
        return "FAIL", phase_error, observations
    if event is None:
        return "FAIL", "evento_guest_ausente", observations
    if event.get("event") == "BLOCKED":
        return "BLOCKED", "guest_blocked", observations
    if event.get("event") != "PASS":
        return "FAIL", f"guest_status:{event.get('event', 'ND')}", observations
    if len(records) != len(RLS3_PHASES):
        return "FAIL", f"amostras_guest_incompletas:{len(records)}", observations
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial", observations
    if (not serial_text or "zephyr>" not in serial_text) and not guest_event:
        return "FAIL", "prompt_ausente", observations
    if prompt_count != 1:
        return "FAIL", "prompt_duplicado", observations

    final = records[-1]
    required_recovery_metrics = [
        f"recovery_{index}_{suffix}"
        for index in RLS3_REQUIRED_RECOVERY_COMPONENTS
        for suffix in ("state", "failures", "last_error")
    ]
    missing = [name for name in RLS3_REQUIRED_METRICS +
               tuple(required_recovery_metrics)
               if _metric_value(final, name) is None]
    if missing:
        return "FAIL", "metricas_guest_ND:" + ",".join(missing), observations

    invalid_true = [name for name in RLS3_REQUIRED_TRUE_METRICS
                    if _metric_value(final, name) != 1]
    if invalid_true:
        return "FAIL", "invariante_violada:" + ",".join(invalid_true), observations
    invalid = _nonzero_metrics(final, RLS3_ZERO_METRICS)
    if invalid:
        return "FAIL", "invariante_violada:" + ",".join(invalid), observations
    if _metric_value(final, "credentials_valid") != 1 or \
            _metric_value(final, "permissions_valid") != 1:
        return "FAIL", "seguranca_inconsistente", observations
    if _metric_value(final, "service_0_target") != 1:
        return "FAIL", "supervisor_target_invalido", observations
    if _metric_value(final, "service_0_state") != 1:
        return "FAIL", "supervisor_state_invalido", observations
    invalid_recovery = [
        f"recovery_{index}"
        for index in RLS3_REQUIRED_RECOVERY_COMPONENTS
        if _metric_value(final, f"recovery_{index}_state") != 1 or
        _metric_value(final, f"recovery_{index}_last_error") != 0
    ]
    if _metric_value(final, "service_0_last_error") != 0 or invalid_recovery:
        return "FAIL", "erro_supervisor_ou_recovery", observations

    baseline = records[1]
    deltas = {name: _delta(final, baseline, name)
              for name in RLS3_COUNTER_METRICS}
    unexpected_errors = [name for name in RLS3_ERROR_COUNTER_METRICS
                         if deltas.get(name) not in
                         (None, RLS3_EXPECTED_ERROR_DELTAS.get(name, 0))]
    if unexpected_errors:
        return "FAIL", "novos_erros:" + ",".join(unexpected_errors), observations
    residual_changes = [name for name in RLS3_STABLE_RESIDUAL_METRICS
                        if _metric_value(final, name) is None or
                        _metric_value(baseline, name) is None or
                        _metric_value(final, name) != _metric_value(baseline, name)]
    if residual_changes:
        return "FAIL", "recurso_residual:" + ",".join(residual_changes), observations
    observations.update({
        "invariants": {
            "valid": _metric_value(final, "invariant_valid"),
            "ownership": _metric_value(final, "invariant_ownership_valid"),
            "security": _metric_value(final, "invariant_security_valid"),
            "supervisor": _metric_value(final, "invariant_supervisor_valid"),
            "update": _metric_value(final, "invariant_update_valid"),
            "recovery": _metric_value(final, "invariant_recovery_valid"),
            "domain_failures": _metric_value(final, "invariant_domain_failures"),
            "last_error": _metric_value(final, "invariant_last_error"),
        },
        "deltas_from_baseline": deltas,
        "residuals": {
            name: {"baseline": _metric_value(baseline, name),
                   "final": _metric_value(final, name)}
            for name in RLS3_STABLE_RESIDUAL_METRICS
        } | {name: _metric_value(final, name) for name in RLS3_ZERO_METRICS},
        "security": {
            "credentials_valid": _metric_value(final, "credentials_valid"),
            "permissions_valid": _metric_value(final, "permissions_valid"),
        },
        "recovery": {
            "critical": {
                str(index): {
                    "state": _metric_value(final, f"recovery_{index}_state"),
                    "last_error": _metric_value(
                        final, f"recovery_{index}_last_error"),
                }
                for index in RLS3_REQUIRED_RECOVERY_COMPONENTS
            },
        },
        "prompt_after_final_record": True,
    })
    return "PASS", None, observations


def _sample_interval(samples: list[dict[str, Any]]) -> float | str:
    timestamps = [sample.get("monotonic_seconds") for sample in samples
                  if isinstance(sample.get("monotonic_seconds"), (int, float))]
    if len(timestamps) < 2:
        return "ND"
    return round(float(timestamps[-1] - timestamps[0]), 6)


def _sample_host(pid: int | None, samples: list[dict[str, Any]],
                 last_sample: list[float], force: bool = False) -> None:
    if pid is None:
        return
    sample = sample_process(pid)
    timestamp = sample.get("monotonic_seconds")
    if not isinstance(timestamp, (int, float)):
        return
    if force or timestamp - last_sample[0] >= RLS3_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def _image_metadata(image: Path) -> dict[str, Any]:
    return {
        "path": _relative_path(image),
        "size_bytes": image.stat().st_size if image.is_file() else "ND",
        "sha256": qemu.sha256_file(image) if image.is_file() else "ND",
    }


def protocol_run_id(profile: str, mode: str, iteration: int) -> str:
    return f"r3-{profile[0]}-{mode[0]}-{iteration}-{qemu.utc_run_id()}"


def build_run_report(
    image: Path, profile: str, mode: str, iteration: int,
    artifact_dir: Path, records: list[dict[str, Any]],
    trace: list[dict[str, Any]], host_samples: list[dict[str, Any]],
    event: dict[str, Any] | None, protocol_errors: list[str],
    wall_time: float | str, qemu_status: dict[str, Any] | None,
    error: str | None = None, status: str | None = None,
) -> dict[str, Any]:
    try:
        serial_text = (artifact_dir / "serial.log").read_text(
            encoding="utf-8", errors="replace")
    except OSError:
        serial_text = ""
    validation_status, validation_error, observations = validate_guest_records(
        records, trace, event, protocol_errors, serial_text)
    final_status = status or validation_status
    final_error = error or validation_error
    try:
        aggregate = aggregate_records(records)
    except MetricsError as failure:
        aggregate = {"schema": "zephyros-perf1-metrics-aggregate-v1",
                     "record_count": len(records), "metrics": {}}
        final_status = status or "FAIL"
        final_error = final_error or str(failure)
    host = summarize_process_samples(host_samples, wall_time)
    return {
        "schema": RLS3_SCHEMA,
        "version": 1,
        "status": final_status,
        "error": final_error,
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "image": _image_metadata(image),
        "guest": {
            "samples": records,
            "aggregate": aggregate,
            "result": event or {"event": "ND"},
            "phase_trace": phase_sequence(trace),
            "protocol_errors": protocol_errors,
        },
        "actions": trace,
        "observations": observations,
        "host": {
            "qemu_process": host,
            "samples": host_samples,
            "sample_count": len(host_samples),
            "sample_interval_seconds": _sample_interval(host_samples),
            "wall_time_seconds": wall_time,
            "qmp_status": qemu_status or "ND",
        },
        "artifacts": {
            "manifest": "manifest.json", "serial": "serial.log",
            "input": "input.log", "qmp_events": "qmp-events.log",
        },
    }


def _run_iteration(options: argparse.Namespace, case: dict[str, Any],
                   profile: str, mode: str, iteration: int,
                   run_id: str) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    selected_case = case_for_lane(case, profile, mode)
    parameters = selected_case.get("parameters", {})
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": RLS3_MANIFEST_SCHEMA, "run_id": run_id,
        "case": RLS3_CASE, "image": _relative_path(Path(options.image)),
        "profile": profile, "mode": mode, "iteration": iteration,
        "phases": list(RLS3_PHASES), "idle_seconds": RLS3_IDLE_SECONDS,
        "host_sample_interval_seconds": RLS3_HOST_SAMPLE_SECONDS,
        "input_key_gap_seconds": parameters.get("input_key_gap_seconds"),
        "snapshot": bool(options.snapshot), "fixture": "readonly-update",
        "diagnostics": "read-only", "persistent_image_write": False,
    })
    arguments = _arguments(options, profile)
    session = qemu.QemuSession(arguments, artifact_dir)
    host_samples: list[dict[str, Any]] = []
    last_sample = [float("-inf")]
    process_pid: int | None = None
    event: dict[str, Any] | None = None
    records: list[dict[str, Any]] = []
    error: str | None = None
    status: str | None = None
    started = time.monotonic()
    try:
        session.start()
        if session.process is not None:
            process_pid = session.process.pid
            _sample_host(process_pid, host_samples, last_sample, True)
        session.host_sample_hook = lambda: _sample_host(
            process_pid, host_samples, last_sample)
        qemu.wait_for_ready(session, run_id)
        event = qemu.wait_for_case(
            session, str(selected_case.get("guest_case", RLS3_CASE)), 0,
            (options.seed + iteration) & UINT32_MASK,
            qemu.case_timeout(selected_case, options.case_timeout),
            qemu.heartbeat_timeout(selected_case, options.heartbeat_timeout),
            selected_case,
        )
        if session.protocol_errors:
            status = "FAIL"
            error = "protocolo_guest:" + ",".join(session.protocol_errors)
    except qemu.RunnerError as failure:
        status = "BLOCKED" if failure.blocked else "FAIL"
        error = str(failure)
    except (OSError, ValueError) as failure:
        status = "FAIL"
        error = str(failure)
    finally:
        _sample_host(process_pid, host_samples, last_sample, True)
        exit_code = session.stop()
    wall_time = round(time.monotonic() - started, 6)
    try:
        records = parse_machine_file(artifact_dir / "serial.log")
    except MetricsError as failure:
        error = error or str(failure)
        status = status or "FAIL"
    result = build_run_report(
        Path(options.image), profile, mode, iteration, artifact_dir, records,
        session.input_trace, host_samples, event, session.protocol_errors,
        wall_time, session.qmp_status, error, status,
    )
    result["run_id"] = run_id
    result["qemu_exit_code"] = exit_code
    qemu.write_json(artifact_dir / "rls3.json", result)
    return result


def matrix_plan() -> list[tuple[str, str, int, str]]:
    plan: list[tuple[str, str, int, str]] = []
    for profile, mode in RLS3_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, RLS3_ITERATIONS + 1):
            plan.append((profile, mode, iteration,
                         protocol_run_id(profile, mode, iteration)))
    return plan


def worker_count(requested: int, total_jobs: int) -> int:
    if isinstance(requested, bool) or not isinstance(requested, int):
        raise qemu.RunnerError("workers_rls3_invalidos", "arguments", True)
    if requested < 1 or requested > RLS3_MAX_WORKERS:
        raise qemu.RunnerError("workers_rls3_invalidos", "arguments", True)
    return min(requested, total_jobs)


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/rls3-invariants"))
    report_path = qemu.resolve_path(
        options.report, results_root / "rls3-invariants.json")
    options.image, options.catalog, options.results = image, catalog_path, results_root
    case = select_case(catalog_path)
    plan = matrix_plan()
    workers = worker_count(getattr(options, "workers", RLS3_DEFAULT_WORKERS),
                           len(plan))
    with ThreadPoolExecutor(max_workers=workers,
                            thread_name_prefix="rls3-qemu") as executor:
        futures = [executor.submit(_run_iteration, options, case, profile,
                                   mode, iteration, run_id)
                   for profile, mode, iteration, run_id in plan]
        runs = [future.result() for future in futures]
    statuses = {item.get("status") for item in runs}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": RLS3_SCHEMA, "version": 1, "status": matrix_status,
        "image": _image_metadata(image),
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode in RLS3_LANES],
        "not_applicable_lanes": list(RLS3_NA_LANES),
        "iterations_per_lane": RLS3_ITERATIONS,
        "phases": list(RLS3_PHASES),
        "windows": {"idle_seconds": RLS3_IDLE_SECONDS},
        "host_sampling": {"interval_seconds": RLS3_HOST_SAMPLE_SECONDS},
        "execution": {"workers": workers, "parallel": workers > 1,
                       "order": "lane-iteration"},
        "recovery": {"fixture": "readonly-update", "persistent_write": False},
        "runs": runs,
        "passed_sessions": sum(item.get("status") == "PASS" for item in runs),
        "total_sessions": len(runs),
    }
    write_report(report_path, matrix)
    print(f"RLS3 invariants: {matrix_status}")
    print(f"Relatorio: {report_path}")
    return 0 if matrix_status == "PASS" else 2 if matrix_status == "BLOCKED" else 1


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--image")
    command_parser.add_argument("--catalog")
    command_parser.add_argument("--results")
    command_parser.add_argument("--report")
    command_parser.add_argument("--qemu")
    command_parser.add_argument("--qemu-arg", action="append")
    command_parser.add_argument("--cpu", default="max")
    command_parser.add_argument("--network", default="none")
    command_parser.add_argument("--no-snapshot", dest="snapshot", action="store_false")
    command_parser.set_defaults(snapshot=True)
    command_parser.add_argument("--boot-timeout", type=float,
                                default=qemu.BOOT_TIMEOUT_DEFAULT)
    command_parser.add_argument("--case-timeout", type=float,
                                default=qemu.CASE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--suite-timeout", type=float,
                                default=qemu.SUITE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--heartbeat-timeout", type=float,
                                default=qemu.PROTOCOL_HEARTBEAT_DEFAULT)
    command_parser.add_argument("--workers", type=int,
                                default=RLS3_DEFAULT_WORKERS)
    command_parser.add_argument("--seed", type=int, default=1)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    try:
        return run(options)
    except (qemu.RunnerError, MetricsError, OSError, ValueError) as error:
        print(f"ERRO RLS3: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
