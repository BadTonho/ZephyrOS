"""Executa a matriz PERF6 do kworker como thread e audita o release 0.1.0."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import sys
import time
from types import SimpleNamespace
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import perf3_scheduler_idle as perf3
from tools import qemu_test_runner as qemu
from tools.perf1_metrics import (
    MetricsError,
    aggregate_records,
    parse_machine_file,
    sample_process,
    summarize_process_samples,
    write_report,
)


PERF6_CASE = "qemu:tst5:perf6-kworker-thread"
PERF6_SCHEMA = "zephyros-perf6-kworker-release-v1"
PERF6_MANIFEST_SCHEMA = "zephyros-perf6-kworker-release-manifest-v1"
PERF6_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
PERF6_ITERATIONS = 3
PERF6_IDLE_SECONDS = 3.0
PERF6_LOAD_SECONDS = 10.0
PERF6_HOST_SAMPLE_SECONDS = 0.25
PERF6_FINAL_DRAIN_SECONDS = 3.0
PERF6_DEFAULT_WORKERS = 2
PERF6_MAX_WORKERS = len(PERF6_LANES) * PERF6_ITERATIONS
UINT32_MASK = 0xFFFFFFFF
PERF6_REQUIRED_METRICS = tuple(dict.fromkeys(
    perf3.PERF3_REQUIRED_METRICS + (
        "workqueue_fallback_active",
        "workqueue_worker_pid",
        "workqueue_worker_tid",
        "workqueue_worker_thread_generation",
        "workqueue_worker_process_generation",
        "service_0_state",
        "service_0_target",
        "service_0_pid",
        "service_0_tid",
        "service_0_thread_generation",
        "service_0_failures",
        "service_0_fallback_active",
        "scheduler_current_tid",
        "scheduler_current_thread_generation",
        "scheduler_current_thread_kernel_service",
    )))
PERF6_ZERO_METRICS = tuple(dict.fromkeys(
    perf3.PERF3_ZERO_METRICS + ("service_0_failures", "service_0_fallback_active")))
PERF6_PHASES = ("boot", "baseline", "pressure", "cancel", "diagnostics", "final")
PERF6_COUNTER_METRICS = (
    "scheduler_wakeups", "scheduler_wake_latency_samples",
    "scheduler_wake_latency_ticks", "workqueue_scheduled",
    "workqueue_executed", "workqueue_dispatch_latency_samples",
    "workqueue_dispatch_latency_ticks", "service_0_failures",
)


def _arguments(options: argparse.Namespace, profile: str) -> argparse.Namespace:
    return SimpleNamespace(
        image=str(options.image), catalog=str(options.catalog),
        results=str(options.results), run_id=None, qemu=options.qemu,
        qemu_arg=list(options.qemu_arg or []), cpu=options.cpu,
        qemu_profile=profile, network=options.network, storage_image=None,
        snapshot=options.snapshot, fixture=None, profile="smoke",
        boot_timeout=options.boot_timeout, case_timeout=options.case_timeout,
        suite_timeout=options.suite_timeout,
        heartbeat_timeout=options.heartbeat_timeout, coverage_symbols=None,
    )


def select_case(catalog_path: Path) -> dict[str, Any]:
    catalog = qemu.load_catalog(catalog_path)
    case = qemu.select_case(catalog, PERF6_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_perf6_invalidos", "catalog_error", True)
    expected = {
        "iterations": PERF6_ITERATIONS,
        "idle_seconds": int(PERF6_IDLE_SECONDS),
        "load_seconds": int(PERF6_LOAD_SECONDS),
        "host_sample_interval_seconds": PERF6_HOST_SAMPLE_SECONDS,
    }
    for key, value in expected.items():
        if parameters.get(key) != value:
            raise qemu.RunnerError(f"parametro_perf6_invalido:{key}",
                                    "catalog_error", True)
    if parameters.get("release_version") != "0.1.0":
        raise qemu.RunnerError("release_perf6_invalido", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_perf6_ausente", "catalog_error", True)
    return case


def case_for_lane(case: dict[str, Any], profile: str,
                  mode: str) -> dict[str, Any]:
    if (profile, mode) not in PERF6_LANES:
        raise qemu.RunnerError("faixa_perf6_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or not isinstance(interaction.get("steps"), list):
        raise qemu.RunnerError("passos_perf6_ausentes", "catalog_error", True)
    steps: list[dict[str, Any]] = []
    mode_seen = False
    for step in interaction["steps"]:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_perf6_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_perf6_ausente", "catalog_error", True)
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
    if actual != list(PERF6_PHASES):
        return False, f"fases_invalidas:{actual}"
    return True, None


def _validate_identity(final: dict[str, Any]) -> str | None:
    if _metric_value(final, "workqueue_worker_bound") != 1 or \
            _metric_value(final, "workqueue_worker_active") != 1:
        return "kworker_nao_vinculada"
    if _metric_value(final, "workqueue_worker_target") != 2:
        return "kworker_alvo_invalido"
    if _metric_value(final, "workqueue_execution_context") != 1:
        return "contexto_kworker_invalido"
    if _metric_value(final, "service_0_state") != 1:
        return "supervisor_kworker_nao_ready"
    tid = _metric_value(final, "workqueue_worker_tid")
    generation = _metric_value(final, "workqueue_worker_thread_generation")
    if tid is None or tid == 0 or generation is None or generation == 0:
        return "identidade_thread_invalida"
    if _metric_value(final, "workqueue_worker_pid") != 0 or \
            _metric_value(final, "workqueue_worker_process_generation") != 0:
        return "kworker_consumiu_slot_de_processo"
    if _metric_value(final, "service_0_target") != 1 or \
            _metric_value(final, "service_0_pid") != 0 or \
            _metric_value(final, "service_0_tid") != tid or \
            _metric_value(final, "service_0_thread_generation") != generation:
        return "identidade_supervisor_inconsistente"
    if _metric_value(final, "workqueue_fallback_active") != 0 or \
            _metric_value(final, "service_0_fallback_active") != 0:
        return "fallback_kworker_ativo"
    return None


def validate_guest_records(
    records: list[dict[str, Any]], trace: list[dict[str, Any]],
    event: dict[str, Any] | None, protocol_errors: list[str],
) -> tuple[str, str | None, dict[str, Any]]:
    phase_valid, phase_error = validate_phase_trace(trace)
    observations: dict[str, Any] = {"phases": phase_sequence(trace)}
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
    if len(records) != 3:
        return "FAIL", f"amostras_guest_incompletas:{len(records)}", observations
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial", observations
    missing = [name for name in PERF6_REQUIRED_METRICS
               if _metric_value(records[-1], name) is None]
    if missing:
        return "FAIL", "metricas_guest_ND:" + ",".join(missing), observations

    baseline = records[1]
    final = records[-1]
    pit = _metric_value(final, "pit_ticks")
    idle = _metric_value(final, "scheduler_idle_ticks")
    active = _metric_value(final, "scheduler_active_ticks")
    if pit is None or idle is None or active is None or \
            (idle + active) & UINT32_MASK != pit:
        return "FAIL", "contabilidade_idle_inconsistente", observations
    identity_error = _validate_identity(final)
    if identity_error:
        return "FAIL", identity_error, observations
    for name in PERF6_ZERO_METRICS:
        if _metric_value(final, name) != 0:
            return "FAIL", f"erro_runtime:{name}", observations
    final_queue = {name: _metric_value(final, name) for name in (
        "workqueue_pending", "workqueue_ready_high",
        "workqueue_ready_normal", "workqueue_delayed", "workqueue_running")}
    baseline_delayed = _metric_value(baseline, "workqueue_delayed")
    if final_queue["workqueue_ready_high"] != 0 or \
            final_queue["workqueue_ready_normal"] != 0 or \
            final_queue["workqueue_running"] != 0 or \
            final_queue["workqueue_pending"] != final_queue["workqueue_delayed"] or \
            final_queue["workqueue_delayed"] != baseline_delayed:
        return "FAIL", "fila_workqueue_residual", observations
    observations.update({
        "idle_ticks": idle,
        "active_ticks": active,
        "worker_tid": _metric_value(final, "workqueue_worker_tid"),
        "worker_thread_generation": _metric_value(
            final, "workqueue_worker_thread_generation"),
        "wakeups": _metric_value(final, "scheduler_wakeups"),
        "wake_latency_ticks": _metric_value(final, "scheduler_wake_latency_ticks"),
        "wake_latency_max_ticks": _metric_value(
            final, "scheduler_wake_latency_max_ticks"),
        "dispatch_latency_ticks": _metric_value(
            final, "workqueue_dispatch_latency_ticks"),
        "dispatch_latency_max_ticks": _metric_value(
            final, "workqueue_max_dispatch_latency_ticks"),
        "deltas": {name: _delta(final, baseline, name)
                   for name in PERF6_COUNTER_METRICS},
        "queue": {name: final_queue[name] for name in (
            "workqueue_ready_high", "workqueue_ready_normal",
            "workqueue_delayed", "workqueue_running")},
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
    if force or timestamp - last_sample[0] >= PERF6_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def build_run_report(
    image: Path, profile: str, mode: str, iteration: int,
    artifact_dir: Path, records: list[dict[str, Any]], trace: list[dict[str, Any]],
    host_samples: list[dict[str, Any]], event: dict[str, Any] | None,
    protocol_errors: list[str], wall_time: float | str,
    qemu_status: dict[str, Any] | None, error: str | None = None,
    status: str | None = None, lifecycle: dict[str, Any] | None = None,
) -> dict[str, Any]:
    validation_status, validation_error, observations = validate_guest_records(
        records, trace, event, protocol_errors)
    final_status = status or validation_status
    final_error = error or validation_error
    try:
        aggregate = aggregate_records(records)
    except MetricsError as failure:
        aggregate = {"schema": "zephyros-perf1-metrics-aggregate-v1",
                     "record_count": len(records), "metrics": {}}
        final_status = status or "FAIL"
        final_error = final_error or str(failure)
    return {
        "schema": PERF6_SCHEMA,
        "version": 1,
        "status": final_status,
        "error": final_error,
        "release": {"version": "0.1.0", "candidate_tag": "v0.1.0-rc1",
                     "signed_verification": "PENDING"},
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "image": {"path": str(image),
                  "size_bytes": image.stat().st_size if image.is_file() else "ND",
                  "sha256": qemu.sha256_file(image) if image.is_file() else "ND"},
        "guest": {"samples": records, "aggregate": aggregate,
                  "result": event or {"status": "ND"},
                  "phase_trace": trace, "protocol_errors": protocol_errors},
        "observations": observations,
        "identity": {name: aggregate["metrics"].get(name, {"median": "ND"})
                     for name in PERF6_REQUIRED_METRICS
                     if name.startswith("workqueue_worker_") or
                     name.startswith("service_0_")},
        "host": {"qemu_process": summarize_process_samples(host_samples, wall_time),
                 "samples": host_samples, "sample_count": len(host_samples),
                 "sample_interval_seconds": _sample_interval(host_samples),
                 "wall_time_seconds": wall_time, "qmp_status": qemu_status or "ND"},
        "reboot": lifecycle or {"status": "ND"},
        "artifacts": {
            "manifest": "manifest.json", "serial": "serial.log",
            "input": "input.log", "qmp_events": "qmp-events.log",
        },
    }


def protocol_run_id(profile: str, mode: str, iteration: int) -> str:
    return f"p6-{profile[0]}-{mode[0]}-{iteration}-{qemu.utc_run_id()}"


def matrix_plan(case: dict[str, Any]) -> list[tuple[str, str, int, str]]:
    plan: list[tuple[str, str, int, str]] = []
    for profile, mode in PERF6_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, PERF6_ITERATIONS + 1):
            plan.append((profile, mode, iteration,
                         protocol_run_id(profile, mode, iteration)))
    return plan


def worker_count(requested: int, total_jobs: int) -> int:
    if isinstance(requested, bool) or not isinstance(requested, int):
        raise qemu.RunnerError("workers_perf6_invalidos", "arguments", True)
    if requested < 1 or requested > PERF6_MAX_WORKERS:
        raise qemu.RunnerError("workers_perf6_invalidos", "arguments", True)
    return min(requested, total_jobs)


def validate_kworker_source(path: Path = ROOT / "src/kernel/kernel.c") -> bool:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError:
        return False
    return bool(re.search(r"thread_create_kernel\s*\(", source) and
                re.search(r"workqueue_bind_thread\s*\(", source) and
                "process_create_with_stack_size(\"Zephyr kworker\"" not in source)


def _run_iteration(options: argparse.Namespace, case: dict[str, Any],
                   profile: str, mode: str, iteration: int,
                   run_id: str) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": PERF6_MANIFEST_SCHEMA, "run_id": run_id, "case": PERF6_CASE,
        "image": str(options.image), "profile": profile, "mode": mode,
        "iteration": iteration, "idle_seconds": PERF6_IDLE_SECONDS,
        "load_seconds": PERF6_LOAD_SECONDS,
        "host_sample_interval_seconds": PERF6_HOST_SAMPLE_SECONDS,
        "release_version": "0.1.0", "candidate_tag": "v0.1.0-rc1",
        "snapshot": bool(options.snapshot), "target": "SYSTEM/thread",
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
    lifecycle: dict[str, Any] | None = None
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
            session, str(selected_case.get("guest_case", PERF6_CASE)), 0,
            (options.seed + iteration) & UINT32_MASK,
            qemu.case_timeout(selected_case, options.case_timeout),
            qemu.heartbeat_timeout(selected_case, options.heartbeat_timeout),
            selected_case,
        )
        if event.get("event") == "PASS":
            lifecycle = qemu.run_post_action(session, selected_case, run_id)
            if not lifecycle or lifecycle.get("status") != "PASS":
                status = "FAIL"
                error = "reboot_guest_invalido"
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
        wall_time, session.qmp_status, error, status, lifecycle=lifecycle,
    )
    result["run_id"] = run_id
    result["qemu_exit_code"] = exit_code
    qemu.write_json(artifact_dir / "perf6.json", result)
    return result


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/perf6-kworker-release"))
    report_path = qemu.resolve_path(
        options.report, results_root / "perf6-kworker-release.json")
    options.image, options.catalog, options.results = image, catalog_path, results_root
    case = select_case(catalog_path)
    if not validate_kworker_source():
        raise qemu.RunnerError("migracao_kworker_ausente", "catalog_error", True)
    plan = matrix_plan(case)
    workers = worker_count(
        getattr(options, "workers", PERF6_DEFAULT_WORKERS), len(plan))
    with ThreadPoolExecutor(max_workers=workers,
                            thread_name_prefix="perf6-qemu") as executor:
        futures = [executor.submit(_run_iteration, options, case, profile,
                                   mode, iteration, run_id)
                   for profile, mode, iteration, run_id in plan]
        runs = [future.result() for future in futures]
    statuses = {item.get("status") for item in runs}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": PERF6_SCHEMA, "version": 1, "status": matrix_status,
        "release": {"version": "0.1.0", "candidate_tag": "v0.1.0-rc1",
                     "signed_verification": "PENDING"},
        "image": {"path": str(image),
                  "size_bytes": image.stat().st_size if image.is_file() else "ND",
                  "sha256": qemu.sha256_file(image) if image.is_file() else "ND"},
        "lanes": [{"profile": profile, "mode": mode} for profile, mode in PERF6_LANES],
        "iterations_per_lane": PERF6_ITERATIONS,
        "windows": {"idle_seconds": PERF6_IDLE_SECONDS,
                     "load_seconds": PERF6_LOAD_SECONDS,
                     "final_drain_seconds": PERF6_FINAL_DRAIN_SECONDS},
        "host_sampling": {"interval_seconds": PERF6_HOST_SAMPLE_SECONDS},
        "execution": {"workers": workers, "parallel": workers > 1,
                       "order": "lane-iteration"},
        "static_validation": {"kworker_thread_binding": validate_kworker_source(),
                               "process_scheduler_changed": False,
                               "thread_scheduler": "isolated"},
        "runs": runs,
        "passed_sessions": sum(item.get("status") == "PASS" for item in runs),
        "total_sessions": len(runs),
    }
    write_report(report_path, matrix)
    print(f"PERF6 kworker/release: {matrix_status}")
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
                                default=PERF6_DEFAULT_WORKERS)
    command_parser.add_argument("--seed", type=int, default=1)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    try:
        return run(options)
    except (qemu.RunnerError, MetricsError, OSError, ValueError) as error:
        print(f"ERRO PERF6: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
