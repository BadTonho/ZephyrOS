"""Executa a matriz PERF3 de scheduler, Idle e kworker."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
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


PERF3_CASE = "qemu:tst5:perf3-scheduler-idle"
PERF3_SCHEMA = "zephyros-perf3-scheduler-idle-v1"
PERF3_MANIFEST_SCHEMA = "zephyros-perf3-scheduler-idle-manifest-v1"
PERF3_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
PERF3_ITERATIONS = 3
PERF3_IDLE_SECONDS = 3.0
PERF3_LOAD_SECONDS = 10.0
PERF3_FINAL_QUIESCENCE_SECONDS = 3.0
PERF3_HOST_SAMPLE_SECONDS = 0.25
UINT32_MASK = 0xFFFFFFFF
PERF3_REQUIRED_METRICS = (
    "pit_ticks",
    "scheduler_idle_ticks",
    "scheduler_active_ticks",
    "scheduler_idle_entries",
    "scheduler_idle_hlt_returns",
    "scheduler_wakeups",
    "scheduler_wake_latency_samples",
    "scheduler_wake_latency_ticks",
    "scheduler_wake_latency_max_ticks",
    "scheduler_ready_peak",
    "scheduler_blocked_peak",
    "scheduler_current_pid",
    "scheduler_last_error",
    "workqueue_worker_bound",
    "workqueue_worker_active",
    "workqueue_execution_context",
    "workqueue_pending",
    "workqueue_ready_high",
    "workqueue_ready_normal",
    "workqueue_delayed",
    "workqueue_running",
    "workqueue_rejected",
    "workqueue_callback_errors",
    "workqueue_context_errors",
    "workqueue_invariant_errors",
    "workqueue_wakeups",
    "workqueue_wake_errors",
    "workqueue_sleeps",
    "workqueue_dispatch_latency_samples",
    "workqueue_dispatch_latency_ticks",
    "workqueue_max_dispatch_latency_ticks",
    "workqueue_last_error",
)
PERF3_ZERO_METRICS = (
    "scheduler_last_error",
    "workqueue_rejected",
    "workqueue_callback_errors",
    "workqueue_context_errors",
    "workqueue_invariant_errors",
    "workqueue_wake_errors",
    "workqueue_last_error",
)


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


def select_case(catalog_path: Path) -> dict[str, Any]:
    catalog = qemu.load_catalog(catalog_path)
    case = qemu.select_case(catalog, PERF3_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_perf3_invalidos", "catalog_error", True)
    if parameters.get("iterations") != PERF3_ITERATIONS:
        raise qemu.RunnerError("iteracoes_perf3_invalidas", "catalog_error", True)
    if parameters.get("idle_seconds") != int(PERF3_IDLE_SECONDS):
        raise qemu.RunnerError("janela_idle_perf3_invalida", "catalog_error", True)
    if parameters.get("load_seconds") != int(PERF3_LOAD_SECONDS):
        raise qemu.RunnerError("janela_carga_perf3_invalida", "catalog_error", True)
    if parameters.get("host_sample_interval_seconds") != PERF3_HOST_SAMPLE_SECONDS:
        raise qemu.RunnerError("amostragem_host_perf3_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_perf3_ausente", "catalog_error", True)
    return case


def case_for_lane(case: dict[str, Any], profile: str, mode: str) -> dict[str, Any]:
    if (profile, mode) not in PERF3_LANES:
        raise qemu.RunnerError("faixa_perf3_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or not isinstance(interaction.get("steps"), list):
        raise qemu.RunnerError("passos_perf3_ausentes", "catalog_error", True)
    selected_steps: list[dict[str, Any]] = []
    mode_seen = False
    for step in interaction["steps"]:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_perf3_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        if copied.get("op") == "phase" and copied.get("phase") == "final" and \
                selected_steps and selected_steps[-1].get("op") == "wait":
            selected_steps[-1] = dict(selected_steps[-1])
            selected_steps[-1]["seconds"] = max(
                float(selected_steps[-1].get("seconds", 0.0)),
                PERF3_FINAL_QUIESCENCE_SECONDS)
            selected_steps.extend([
                {"op": "text", "text": "workq check"},
                {"op": "key", "key": "enter"},
                {"op": "wait", "seconds": 1},
            ])
        selected_steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_perf3_ausente", "catalog_error", True)
    selected = dict(case)
    selected["qemu_profile"] = profile
    selected_interaction = dict(interaction)
    selected_interaction["steps"] = selected_steps
    selected["interaction"] = selected_interaction
    qemu.validate_case_for_runner(selected)
    return selected


def _metric_value(record: dict[str, Any], name: str) -> int | None:
    metric = record.get("metrics", {}).get(name)
    if not isinstance(metric, dict):
        return None
    value = metric.get("value")
    if value == "ND":
        return None
    try:
        parsed = int(value, 10)
    except (TypeError, ValueError):
        return None
    return parsed if 0 <= parsed <= UINT32_MASK else None


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
    expected = ["boot", "idle", "load", "cleanup", "final"]
    actual = phase_sequence(trace)
    if actual != expected:
        return False, f"fases_invalidas:{actual}"
    return True, None


def validate_guest_records(
    records: list[dict[str, Any]],
    trace: list[dict[str, Any]],
    event: dict[str, Any] | None,
    protocol_errors: list[str],
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
    missing = [name for name in PERF3_REQUIRED_METRICS
               if _metric_value(records[-1], name) is None]
    if missing:
        return "FAIL", "metricas_guest_ND:" + ",".join(missing), observations

    idle = records[1]
    final = records[2]
    idle_ticks = _metric_value(idle, "scheduler_idle_ticks")
    idle_entries = _metric_value(idle, "scheduler_idle_entries")
    idle_returns = _metric_value(idle, "scheduler_idle_hlt_returns")
    if not idle_ticks or not idle_entries or not idle_returns:
        return "FAIL", "idle_sem_residencia_observavel", observations
    for record in (idle, final):
        pit = _metric_value(record, "pit_ticks")
        idle_value = _metric_value(record, "scheduler_idle_ticks")
        active_value = _metric_value(record, "scheduler_active_ticks")
        if pit is None or idle_value is None or active_value is None or \
                (idle_value + active_value) & UINT32_MASK != pit:
            return "FAIL", "contabilidade_idle_inconsistente", observations

    for name in PERF3_ZERO_METRICS:
        if _metric_value(final, name) != 0:
            return "FAIL", f"erro_runtime:{name}", observations
    if _metric_value(final, "workqueue_worker_bound") != 1 or \
            _metric_value(final, "workqueue_worker_active") != 1 or \
            _metric_value(final, "workqueue_execution_context") != 1:
        return "FAIL", "kworker_nao_vinculada", observations
    ready_high = _metric_value(final, "workqueue_ready_high")
    ready_normal = _metric_value(final, "workqueue_ready_normal")
    running = _metric_value(final, "workqueue_running")
    delayed = _metric_value(final, "workqueue_delayed")
    if ready_high is None or ready_normal is None or running is None or \
            delayed is None:
        return "FAIL", "estado_workqueue_incompleto", observations
    if ready_high or ready_normal or running:
        return "FAIL", "fila_workqueue_residual", observations

    observations.update({
        "idle_ticks": idle_ticks,
        "idle_entries": idle_entries,
        "idle_hlt_returns": idle_returns,
        "final_pit_ticks": _metric_value(final, "pit_ticks"),
        "final_scheduler_idle_ticks": _metric_value(final, "scheduler_idle_ticks"),
        "final_scheduler_active_ticks": _metric_value(final, "scheduler_active_ticks"),
        "wakeups": _metric_value(final, "scheduler_wakeups"),
        "wake_latency_samples": _metric_value(final, "scheduler_wake_latency_samples"),
        "wake_latency_ticks": _metric_value(final, "scheduler_wake_latency_ticks"),
        "wake_latency_max_ticks": _metric_value(final, "scheduler_wake_latency_max_ticks"),
        "workqueue_dispatch_latency_samples": _metric_value(
            final, "workqueue_dispatch_latency_samples"),
        "workqueue_dispatch_latency_ticks": _metric_value(
            final, "workqueue_dispatch_latency_ticks"),
        "workqueue_max_dispatch_latency_ticks": _metric_value(
            final, "workqueue_max_dispatch_latency_ticks"),
        "workqueue_pending": _metric_value(final, "workqueue_pending"),
        "workqueue_ready_high": ready_high,
        "workqueue_ready_normal": ready_normal,
        "workqueue_delayed": delayed,
        "workqueue_running": running,
    })
    return "PASS", None, observations


def _sample_interval(samples: list[dict[str, Any]]) -> float | str:
    timestamps = [sample.get("monotonic_seconds") for sample in samples
                  if isinstance(sample.get("monotonic_seconds"), (int, float))]
    if len(timestamps) < 2:
        return "ND"
    return round(float(timestamps[-1] - timestamps[0]), 6)


def build_run_report(
    image: Path,
    profile: str,
    mode: str,
    iteration: int,
    records: list[dict[str, Any]],
    trace: list[dict[str, Any]],
    host_samples: list[dict[str, Any]],
    event: dict[str, Any] | None,
    protocol_errors: list[str],
    wall_time: float | str,
    qemu_status: dict[str, Any] | None,
    error: str | None = None,
    status: str | None = None,
) -> dict[str, Any]:
    validation_status, validation_error, observations = validate_guest_records(
        records, trace, event, protocol_errors)
    final_status = status or validation_status
    final_error = error or validation_error
    try:
        aggregate = aggregate_records(records)
    except MetricsError as failure:
        aggregate = {
            "schema": "zephyros-perf1-metrics-aggregate-v1",
            "record_count": len(records),
            "metrics": {},
        }
        final_status = status or "FAIL"
        final_error = final_error or str(failure)
    host = summarize_process_samples(host_samples, wall_time)
    return {
        "schema": PERF3_SCHEMA,
        "version": 1,
        "status": final_status,
        "error": final_error,
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "image": {
            "path": str(image),
            "size_bytes": image.stat().st_size if image.is_file() else "ND",
            "sha256": qemu.sha256_file(image) if image.is_file() else "ND",
        },
        "guest": {
            "samples": records,
            "aggregate": aggregate,
            "result": event or {"status": "ND"},
            "phase_trace": trace,
            "protocol_errors": protocol_errors,
        },
        "observations": observations,
        "workqueue": {
            name: aggregate["metrics"][name]
            for name in aggregate["metrics"]
            if name.startswith("workqueue_")
        },
        "host": {
            "qemu_process": host,
            "samples": host_samples,
            "sample_count": len(host_samples),
            "sample_interval_seconds": _sample_interval(host_samples),
            "wall_time_seconds": wall_time,
            "qmp_status": qemu_status or "ND",
        },
        "scheduler_model": {
            "thread_scheduler": "isolated",
            "selection_algorithm_changed": False,
            "quantum_changed": False,
        },
    }


def _sample_host(
    pid: int | None,
    samples: list[dict[str, Any]],
    last_sample: list[float],
    force: bool = False,
) -> None:
    if pid is None:
        return
    sample = sample_process(pid)
    timestamp = sample.get("monotonic_seconds")
    if not isinstance(timestamp, (int, float)):
        return
    if force or timestamp - last_sample[0] >= PERF3_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def protocol_run_id(profile: str, mode: str, iteration: int) -> str:
    return f"p3-{profile[0]}-{mode[0]}-{iteration}-{qemu.utc_run_id()}"


def validate_idle_source(path: Path = ROOT / "src/process/process.c") -> bool:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError:
        return False
    match = re.search(
        r"static void process_idle_main\(void\) \{(?P<body>.*?)"
        r"static int scheduler_idle_context_valid",
        source,
        re.DOTALL,
    )
    return bool(match and re.search(r'asm volatile\("sti\\n\\thlt"', match.group("body")))


def validate_protocol_idle_path(path: Path = ROOT / "src/kernel/kernel.c") -> bool:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError:
        return False
    match = re.search(
        r"void system_process_main\(void\) \{(?P<body>.*?)"
        r"void shell_process_main",
        source,
        re.DOTALL,
    )
    return bool(match and re.search(
        r"if \(test_protocol_is_active\(\)\) \{.*?"
        r"process_block\(1U\);",
        match.group("body"),
        re.DOTALL,
    ))


def _run_iteration(
    options: argparse.Namespace,
    case: dict[str, Any],
    profile: str,
    mode: str,
    iteration: int,
    run_id: str,
) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": PERF3_MANIFEST_SCHEMA,
        "run_id": run_id,
        "case": PERF3_CASE,
        "image": str(options.image),
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "idle_seconds": PERF3_IDLE_SECONDS,
        "load_seconds": PERF3_LOAD_SECONDS,
        "host_sample_interval_seconds": PERF3_HOST_SAMPLE_SECONDS,
        "snapshot": bool(options.snapshot),
        "diagnostics": "read-only",
        "thread_scheduler": "isolated",
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
            session, str(selected_case.get("guest_case", PERF3_CASE)), 0,
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
        Path(options.image), profile, mode, iteration, records,
        session.input_trace, host_samples, event, session.protocol_errors,
        wall_time, session.qmp_status, error, status,
    )
    result["run_id"] = run_id
    result["qemu_exit_code"] = exit_code
    result["artifacts"] = {
        "serial": "serial.log", "manifest": "manifest.json",
        "input": "input.log", "qmp_events": "qmp-events.log",
    }
    qemu.write_json(artifact_dir / "perf3.json", result)
    return result


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/perf3-scheduler-idle"))
    report_path = qemu.resolve_path(
        options.report, results_root / "perf3-scheduler-idle.json")
    options.image = image
    options.catalog = catalog_path
    options.results = results_root
    case = select_case(catalog_path)
    if not validate_idle_source():
        raise qemu.RunnerError("sti_hlt_idle_ausente", "catalog_error", True)
    if not validate_protocol_idle_path():
        raise qemu.RunnerError("espera_protocolo_idle_ausente", "catalog_error", True)
    runs: list[dict[str, Any]] = []
    for profile, mode in PERF3_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, PERF3_ITERATIONS + 1):
            runs.append(_run_iteration(
                options, case, profile, mode, iteration,
                protocol_run_id(profile, mode, iteration)))
    statuses = {run_item.get("status") for run_item in runs}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": PERF3_SCHEMA,
        "version": 1,
        "status": matrix_status,
        "image": {
            "path": str(image),
            "size_bytes": image.stat().st_size if image.is_file() else "ND",
            "sha256": qemu.sha256_file(image) if image.is_file() else "ND",
        },
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode in PERF3_LANES],
        "iterations_per_lane": PERF3_ITERATIONS,
        "windows": {"idle_seconds": PERF3_IDLE_SECONDS,
                    "load_seconds": PERF3_LOAD_SECONDS},
        "host_sampling": {"interval_seconds": PERF3_HOST_SAMPLE_SECONDS},
        "static_validation": {"idle_sti_hlt": validate_idle_source(),
                               "thread_scheduler": "isolated"},
        "runs": runs,
        "passed_sessions": sum(run_item.get("status") == "PASS"
                                for run_item in runs),
        "total_sessions": len(runs),
    }
    write_report(report_path, matrix)
    print(f"PERF3 scheduler/idle: {matrix_status}")
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
    command_parser.add_argument("--seed", type=int, default=1)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    try:
        return run(options)
    except (qemu.RunnerError, MetricsError, OSError, ValueError) as error:
        print(f"ERRO PERF3: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
