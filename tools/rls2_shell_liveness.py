"""Audita a liveness do Shell, jobs e cenas em uma matriz QEMU fixa."""

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


RLS2_CASE = "qemu:tst5:rls2-shell-liveness"
RLS2_SCHEMA = "zephyros-rls2-shell-liveness-v1"
RLS2_MANIFEST_SCHEMA = "zephyros-rls2-shell-liveness-manifest-v1"
RLS2_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
RLS2_ITERATIONS = 3
RLS2_IDLE_SECONDS = 3.0
RLS2_HOST_SAMPLE_SECONDS = 0.25
RLS2_DEFAULT_WORKERS = 3
RLS2_MAX_WORKERS = len(RLS2_LANES) * RLS2_ITERATIONS
RLS2_PHASES = ("boot", "baseline", "commands", "jobs", "scenes",
               "cancel", "final")
RLS2_NA_LANES = ({"profile": "no-vesa", "mode": "classic",
                  "status": "NOT_APPLICABLE",
                  "reason": "fallback fixado em Simple"},)
RLS2_REQUIRED_METRICS = (
    "shell_lifecycle_generation",
    "shell_lifecycle_finalization_requests",
    "shell_lifecycle_finalizations",
    "shell_lifecycle_duplicate_finalizations",
    "shell_lifecycle_prompt_requests",
    "shell_lifecycle_prompt_reconciliations",
    "shell_lifecycle_prompt_rendered",
    "shell_lifecycle_prompt_blocked",
    "shell_lifecycle_prompt_missing",
    "shell_lifecycle_prompt_duplicates",
    "shell_lifecycle_input_blocked_events",
    "shell_lifecycle_last_error",
    "shell_lifecycle_last_layer",
    "shell_lifecycle_prompt_state",
    "shell_lifecycle_operation_active",
    "shell_lifecycle_input_blocked",
    "shell_lifecycle_terminal_active",
    "shell_lifecycle_hosted_visible",
    "shell_lifecycle_focus_shell",
    "shell_lifecycle_scene_active",
    "shell_lifecycle_job_active",
    "shell_lifecycle_loader_active",
)
RLS2_COUNTER_METRICS = (
    "shell_lifecycle_finalization_requests",
    "shell_lifecycle_finalizations",
    "shell_lifecycle_duplicate_finalizations",
    "shell_lifecycle_prompt_requests",
    "shell_lifecycle_prompt_reconciliations",
    "shell_lifecycle_prompt_rendered",
    "shell_lifecycle_prompt_blocked",
    "shell_lifecycle_prompt_missing",
    "shell_lifecycle_prompt_duplicates",
    "shell_lifecycle_input_blocked_events",
)
RLS2_ALLOWED_LAYERS = set(range(0, 8))
UINT32_MASK = 0xFFFFFFFF


def _arguments(options: argparse.Namespace, profile: str) -> argparse.Namespace:
    return SimpleNamespace(
        image=str(options.image),
        catalog=str(options.catalog),
        results=str(options.results),
        run_id=None,
        qemu=options.qemu,
        qemu_arg=list(options.qemu_arg or []),
        cpu=options.cpu,
        qemu_profile=profile,
        network=options.network,
        storage_image=None,
        snapshot=options.snapshot,
        fixture=None,
        profile="smoke",
        boot_timeout=options.boot_timeout,
        case_timeout=options.case_timeout,
        suite_timeout=options.suite_timeout,
        heartbeat_timeout=options.heartbeat_timeout,
        coverage_symbols=None,
    )


def _relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.name


def select_case(catalog_path: Path) -> dict[str, Any]:
    catalog = qemu.load_catalog(catalog_path)
    case = qemu.select_case(catalog, RLS2_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_rls2_invalidos", "catalog_error", True)
    expected = {
        "iterations": RLS2_ITERATIONS,
        "idle_seconds": int(RLS2_IDLE_SECONDS),
        "host_sample_interval_seconds": RLS2_HOST_SAMPLE_SECONDS,
        "liveness_schema": RLS2_SCHEMA,
    }
    for key, value in expected.items():
        if parameters.get(key) != value:
            raise qemu.RunnerError(f"parametro_rls2_invalido:{key}",
                                    "catalog_error", True)
    if parameters.get("phases") != ",".join(RLS2_PHASES):
        raise qemu.RunnerError("fases_rls2_invalidas", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_rls2_ausente", "catalog_error", True)
    return case


def case_for_lane(case: dict[str, Any], profile: str,
                  mode: str) -> dict[str, Any]:
    if (profile, mode) not in RLS2_LANES:
        raise qemu.RunnerError("faixa_rls2_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or not isinstance(interaction.get("steps"), list):
        raise qemu.RunnerError("passos_rls2_ausentes", "catalog_error", True)
    steps: list[dict[str, Any]] = []
    mode_seen = False
    current_phase = ""
    scene_command = ""
    for step in interaction["steps"]:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_rls2_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "phase":
            current_phase = str(copied.get("phase", ""))
        if current_phase == "scenes" and copied.get("op") == "text":
            scene_command = str(copied.get("text", ""))
        if current_phase == "scenes" and mode == "classic":
            if copied.get("op") == "key" and copied.get("key") == "esc":
                if scene_command == "desktop":
                    continue
                copied = {"op": "keys", "keys": ["alt", "f4"]}
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_rls2_ausente", "catalog_error", True)
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
    if actual != list(RLS2_PHASES):
        return False, f"fases_invalidas:{actual}"
    return True, None


def _final_prompt_observation(serial_text: str) -> tuple[int, str]:
    marker = "tst5-rls2-shell-liveness"
    marker_offset = serial_text.rfind(marker)
    if marker_offset >= 0:
        return serial_text[marker_offset:].count("zephyr>"), "after_marker"
    record_offset = serial_text.rfind("@@ZMETRIC/1 record=end")
    if record_offset >= 0:
        return serial_text[record_offset:].count("zephyr>"), "after_final_record"
    return serial_text.count("zephyr>"), "serial"


def validate_guest_records(
    records: list[dict[str, Any]], trace: list[dict[str, Any]],
    event: dict[str, Any] | None, protocol_errors: list[str],
    serial_text: str = "",
) -> tuple[str, str | None, dict[str, Any]]:
    phase_valid, phase_error = validate_phase_trace(trace)
    final_prompt_count, prompt_source = _final_prompt_observation(serial_text)
    guest_blackbox_prompt = bool(
        event and event.get("event") == "PASS" and
        event.get("case") == RLS2_CASE)
    if guest_blackbox_prompt and final_prompt_count == 0:
        final_prompt_count = 1
        prompt_source = "guest_blackbox"
    observations: dict[str, Any] = {
        "phases": phase_sequence(trace),
        "sample_count": len(records),
        "prompt_observations": {
            "expected": 1,
            "observed": final_prompt_count,
            "source": prompt_source,
            "total_serial": serial_text.count("zephyr>"),
            "guest_blackbox": guest_blackbox_prompt,
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
    if len(records) != len(RLS2_PHASES):
        return "FAIL", f"amostras_guest_incompletas:{len(records)}", observations
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial", observations
    if (not serial_text or "zephyr>" not in serial_text) and \
            not guest_blackbox_prompt:
        return "FAIL", "prompt_ausente", observations
    if final_prompt_count != 1:
        return "FAIL", "prompt_duplicado", observations

    missing = [name for name in RLS2_REQUIRED_METRICS
               if _metric_value(records[-1], name) is None]
    if missing:
        return "FAIL", "metricas_guest_ND:" + ",".join(missing), observations

    final = records[-1]
    final_layer = _metric_value(final, "shell_lifecycle_last_layer")
    final_error = _metric_value(final, "shell_lifecycle_last_error")
    if final_layer not in RLS2_ALLOWED_LAYERS:
        return "FAIL", "camada_lifecycle_invalida", observations
    if final_error != 0:
        return "FAIL", "erro_lifecycle_final", observations
    if _metric_value(final, "shell_lifecycle_finalizations") == 0:
        return "FAIL", "nenhuma_finalizacao_observada", observations
    if _metric_value(final, "shell_lifecycle_duplicate_finalizations") != 0 or \
            _metric_value(final, "shell_lifecycle_prompt_duplicates") != 0:
        return "FAIL", "prompt_ou_finalizacao_duplicada", observations
    if _metric_value(final, "shell_lifecycle_prompt_missing") != 0:
        return "FAIL", "prompt_ausente_no_ciclo", observations
    snapshot_in_dispatcher = (
        _metric_value(final, "shell_lifecycle_operation_active") == 1 and
        final_layer == 1)
    if (not snapshot_in_dispatcher and
            (_metric_value(final, "shell_lifecycle_prompt_state") != 2 or
             _metric_value(final, "shell_lifecycle_operation_active") != 0)) or \
            _metric_value(final, "shell_lifecycle_terminal_active") != 1 or \
            _metric_value(final, "shell_lifecycle_focus_shell") != 1:
        return "FAIL", "prompt_nao_restaurado", observations
    last_record = serial_text.rfind("@@ZMETRIC/1 record=end")
    prompt_after_record = guest_blackbox_prompt or last_record < 0 or \
        serial_text.find("zephyr>", last_record) >= 0
    if not prompt_after_record:
        return "FAIL", "prompt_ausente_apos_amostra_final", observations
    residual_names = (
        "shell_lifecycle_input_blocked", "shell_lifecycle_scene_active",
        "shell_lifecycle_job_active", "shell_lifecycle_loader_active",
    )
    residuals = {name: _metric_value(final, name) for name in residual_names}
    if any(value != 0 for value in residuals.values()):
        return "FAIL", "recursos_lifecycle_residuais", observations

    baseline = records[1]
    deltas = {name: _delta(final, baseline, name)
              for name in RLS2_COUNTER_METRICS}
    observations.update({
        "final_generation": _metric_value(final, "shell_lifecycle_generation"),
        "final_layer": final_layer,
        "final_prompt_state": _metric_value(final, "shell_lifecycle_prompt_state"),
        "snapshot_in_dispatcher": snapshot_in_dispatcher,
        "prompt_after_final_record": prompt_after_record,
        "finalizations": _metric_value(final, "shell_lifecycle_finalizations"),
        "prompt_rendered": _metric_value(final, "shell_lifecycle_prompt_rendered"),
        "deltas_from_baseline": deltas,
        "residuals": residuals,
        "layers_observed": sorted({
            value for record in records
            for value in [_metric_value(record, "shell_lifecycle_last_layer")]
            if value is not None
        }),
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
    if force or timestamp - last_sample[0] >= RLS2_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def _image_metadata(image: Path) -> dict[str, Any]:
    return {
        "path": _relative_path(image),
        "size_bytes": image.stat().st_size if image.is_file() else "ND",
        "sha256": qemu.sha256_file(image) if image.is_file() else "ND",
    }


def protocol_run_id(profile: str, mode: str, iteration: int) -> str:
    return f"r2-{profile[0]}-{mode[0]}-{iteration}-{qemu.utc_run_id()}"


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
        "schema": RLS2_SCHEMA,
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
            "manifest": "manifest.json",
            "serial": "serial.log",
            "input": "input.log",
            "qmp_events": "qmp-events.log",
        },
    }


def _run_iteration(options: argparse.Namespace, case: dict[str, Any],
                   profile: str, mode: str, iteration: int,
                   run_id: str) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": RLS2_MANIFEST_SCHEMA,
        "run_id": run_id,
        "case": RLS2_CASE,
        "image": _relative_path(Path(options.image)),
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "phases": list(RLS2_PHASES),
        "idle_seconds": RLS2_IDLE_SECONDS,
        "host_sample_interval_seconds": RLS2_HOST_SAMPLE_SECONDS,
        "snapshot": bool(options.snapshot),
        "diagnostics": "read-only",
    })
    arguments = _arguments(options, profile)
    session = qemu.QemuSession(arguments, artifact_dir)
    selected_case = case_for_lane(case, profile, mode)
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
            session, str(selected_case.get("guest_case", RLS2_CASE)), 0,
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
    qemu.write_json(artifact_dir / "rls2.json", result)
    return result


def matrix_plan() -> list[tuple[str, str, int, str]]:
    plan: list[tuple[str, str, int, str]] = []
    for profile, mode in RLS2_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, RLS2_ITERATIONS + 1):
            plan.append((profile, mode, iteration,
                         protocol_run_id(profile, mode, iteration)))
    return plan


def worker_count(requested: int, total_jobs: int) -> int:
    if isinstance(requested, bool) or not isinstance(requested, int):
        raise qemu.RunnerError("workers_rls2_invalidos", "arguments", True)
    if requested < 1 or requested > RLS2_MAX_WORKERS:
        raise qemu.RunnerError("workers_rls2_invalidos", "arguments", True)
    return min(requested, total_jobs)


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/rls2-shell-liveness"))
    report_path = qemu.resolve_path(
        options.report, results_root / "rls2-shell-liveness.json")
    options.image, options.catalog, options.results = image, catalog_path, results_root
    case = select_case(catalog_path)
    plan = matrix_plan()
    workers = worker_count(getattr(options, "workers", RLS2_DEFAULT_WORKERS),
                           len(plan))
    with ThreadPoolExecutor(max_workers=workers,
                            thread_name_prefix="rls2-qemu") as executor:
        futures = [executor.submit(_run_iteration, options, case, profile,
                                   mode, iteration, run_id)
                   for profile, mode, iteration, run_id in plan]
        runs = [future.result() for future in futures]
    statuses = {item.get("status") for item in runs}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": RLS2_SCHEMA,
        "version": 1,
        "status": matrix_status,
        "image": _image_metadata(image),
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode in RLS2_LANES],
        "not_applicable_lanes": list(RLS2_NA_LANES),
        "iterations_per_lane": RLS2_ITERATIONS,
        "phases": list(RLS2_PHASES),
        "windows": {"idle_seconds": RLS2_IDLE_SECONDS},
        "host_sampling": {"interval_seconds": RLS2_HOST_SAMPLE_SECONDS},
        "execution": {"workers": workers, "parallel": workers > 1,
                       "order": "lane-iteration"},
        "runs": runs,
        "passed_sessions": sum(item.get("status") == "PASS" for item in runs),
        "total_sessions": len(runs),
    }
    write_report(report_path, matrix)
    print(f"RLS2 Shell liveness: {matrix_status}")
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
                                default=RLS2_DEFAULT_WORKERS)
    command_parser.add_argument("--seed", type=int, default=1)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    try:
        return run(options)
    except (qemu.RunnerError, MetricsError, OSError, ValueError) as error:
        print(f"ERRO RLS2: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
