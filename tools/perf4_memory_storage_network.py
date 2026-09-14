"""Executa a matriz PERF4 de memoria, VFS, armazenamento e rede."""

from __future__ import annotations

import argparse
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


PERF4_CASE = "qemu:tst5:perf4-memory-storage-network"
PERF4_SCHEMA = "zephyros-perf4-memory-storage-network-v1"
PERF4_MANIFEST_SCHEMA = "zephyros-perf4-memory-storage-network-manifest-v1"
PERF4_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
PERF4_ITERATIONS = 3
PERF4_IDLE_SECONDS = 3.0
PERF4_HOST_SAMPLE_SECONDS = 0.25
UINT32_MASK = 0xFFFFFFFF

PERF4_REQUIRED_METRICS = (
    "pit_ticks",
    "capture_ticks",
    "memory_heap_used_bytes",
    "memory_heap_free_bytes",
    "memory_heap_fragmentation_percent",
    "memory_pmm_owned_pages",
    "memory_detailed_total_pages",
    "memory_zone_5_pages",
    "memory_detailed_fragmentation_percent",
    "slab_active_objects",
    "vfs_descriptors_open",
    "vfs_mounts_active",
    "vfs_pipes_active",
    "block_queue_depth",
    "block_in_flight",
    "block_peak_depth",
    "cache_entries",
    "cache_dirty_bytes",
    "cache_writeback_entries",
    "net_buffer_active_buffers",
    "net_buffer_peak_buffers",
    "net_buffer_copies",
    "net_buffer_copied_bytes",
    "sk_buff_active_buffers",
    "socket_active_count",
    "net_socket_active_count",
    "route_entry_count",
    "network_interfaces",
    "ethernet_rx_frames",
)
PERF4_ERROR_METRICS = (
    "memory_heap_allocation_failures",
    "memory_heap_invalid_frees",
    "memory_heap_double_frees",
    "memory_pmm_allocation_failures",
    "memory_pmm_invalid_frees",
    "slab_allocation_failures",
    "slab_invalid_frees",
    "slab_double_frees",
    "vfs_failures",
    "block_failed",
    "block_cancelled",
    "cache_errors",
    "cache_writeback_failures",
    "net_buffer_dropped",
    "net_buffer_invalid_transitions",
    "net_buffer_duplicate_completions",
    "sk_buff_drops",
    "sk_buff_invalid_operations",
    "socket_queue_drops",
    "socket_failures",
    "net_socket_rx_overflows",
    "net_socket_wait_failures",
)
PERF4_EXPECTED_ERROR_DELTAS = {
    "vfs_failures": 3,
    "net_buffer_dropped": 2,
    "sk_buff_drops": 2,
}
PERF4_RESIDUAL_METRICS = (
    "vfs_descriptors_open",
    "vfs_pipes_active",
    "block_queue_depth",
    "block_in_flight",
    "cache_reading_entries",
    "cache_writeback_entries",
    "net_buffer_active_buffers",
    "sk_buff_active_buffers",
    "socket_active_count",
    "net_socket_active_count",
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
    case = qemu.select_case(catalog, PERF4_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_perf4_invalidos", "catalog_error", True)
    expected = {
        "iterations": PERF4_ITERATIONS,
        "idle_seconds": int(PERF4_IDLE_SECONDS),
        "host_sample_interval_seconds": PERF4_HOST_SAMPLE_SECONDS,
    }
    for key, value in expected.items():
        if parameters.get(key) != value:
            raise qemu.RunnerError(f"parametro_perf4_invalido:{key}",
                                    "catalog_error", True)
    if parameters.get("network") != "user,model=e1000,restrict=on":
        raise qemu.RunnerError("rede_perf4_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_perf4_ausente", "catalog_error", True)
    return case


def case_for_lane(case: dict[str, Any], profile: str,
                  mode: str) -> dict[str, Any]:
    if (profile, mode) not in PERF4_LANES:
        raise qemu.RunnerError("faixa_perf4_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or not isinstance(interaction.get("steps"), list):
        raise qemu.RunnerError("passos_perf4_ausentes", "catalog_error", True)
    selected_steps: list[dict[str, Any]] = []
    mode_seen = False
    for step in interaction["steps"]:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_perf4_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        selected_steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_perf4_ausente", "catalog_error", True)
    selected = dict(case)
    selected["qemu_profile"] = profile
    selected_interaction = dict(interaction)
    selected_interaction["steps"] = selected_steps
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
    expected = ["boot", "idle", "load", "cleanup", "final"]
    actual = phase_sequence(trace)
    if actual != expected:
        return False, f"fases_invalidas:{actual}"
    return True, None


def _validate_memory(record: dict[str, Any]) -> str | None:
    total = _metric_value(record, "memory_detailed_total_pages")
    zones = [_metric_value(record, f"memory_zone_{index}_pages")
             for index in range(6)]
    if total is None or any(value is None for value in zones):
        return "zonas_memoria_ND"
    if sum(value for value in zones if value is not None) != total:
        return "zonas_memoria_inconsistentes"
    return None


def _validate_no_growth(records: list[dict[str, Any]],
                        names: tuple[str, ...]) -> str | None:
    baseline = records[1]
    final = records[-1]
    for name in names:
        before = _metric_value(baseline, name)
        after = _metric_value(final, name)
        if before is None or after is None:
            return f"residual_ND:{name}"
        if after != before:
            return f"residual:{name}:{before}->{after}"
    return None


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
    for record in records[1:]:
        missing = [name for name in PERF4_REQUIRED_METRICS
                   if _metric_value(record, name) is None]
        if missing:
            return "FAIL", "metricas_guest_ND:" + ",".join(missing), observations

    for record in records:
        memory_error = _validate_memory(record)
        if memory_error:
            return "FAIL", memory_error, observations

    baseline = records[1]
    final = records[-1]
    for name in PERF4_ERROR_METRICS:
        value = _metric_value(final, name)
        delta = _delta(final, baseline, name)
        if value is None or delta is None:
            return "FAIL", f"erro_ND:{name}", observations
        expected = PERF4_EXPECTED_ERROR_DELTAS.get(name, 0)
        if delta != expected:
            return "FAIL", f"erro_runtime:{name}={delta}", observations
    residual_error = _validate_no_growth(records, PERF4_RESIDUAL_METRICS)
    if residual_error:
        return "FAIL", residual_error, observations

    observations.update({
        "baseline_memory_heap_used_bytes": _metric_value(
            baseline, "memory_heap_used_bytes"),
        "final_memory_heap_used_bytes": _metric_value(
            final, "memory_heap_used_bytes"),
        "baseline_free_pages": _metric_value(baseline, "memory_zone_5_pages"),
        "final_free_pages": _metric_value(final, "memory_zone_5_pages"),
        "heap_fragmentation_percent": _metric_value(
            final, "memory_heap_fragmentation_percent"),
        "memory_fragmentation_percent": _metric_value(
            final, "memory_detailed_fragmentation_percent"),
        "block_queue_depth": _metric_value(final, "block_queue_depth"),
        "cache_entries": _metric_value(final, "cache_entries"),
        "cache_dirty_bytes": _metric_value(final, "cache_dirty_bytes"),
        "net_buffer_copies": _metric_value(final, "net_buffer_copies"),
        "net_buffer_copied_bytes": _metric_value(final, "net_buffer_copied_bytes"),
        "socket_active_count": _metric_value(final, "socket_active_count"),
        "route_entry_count": _metric_value(final, "route_entry_count"),
        "diagnostic_deltas": {
            name: _delta(final, baseline, name)
            for name in ("vfs_reads", "block_completed", "cache_hits",
                         "ethernet_rx_frames", "net_buffer_copies",
                         "socket_receives")
        },
        "expected_diagnostic_error_deltas": {
            name: _delta(final, baseline, name)
            for name in PERF4_EXPECTED_ERROR_DELTAS
        },
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
        "schema": PERF4_SCHEMA,
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
        "memory": {
            name: aggregate["metrics"][name]
            for name in aggregate["metrics"] if name.startswith(("memory_", "slab_"))
        },
        "storage": {
            name: aggregate["metrics"][name]
            for name in aggregate["metrics"]
            if name.startswith(("vfs_", "block_", "cache_", "durability_"))
        },
        "network": {
            name: aggregate["metrics"][name]
            for name in aggregate["metrics"]
            if name.startswith(("net_", "sk_buff_", "socket_", "route_",
                                "network_", "ethernet_"))
        },
        "host": {
            "qemu_process": host,
            "samples": host_samples,
            "sample_count": len(host_samples),
            "sample_interval_seconds": _sample_interval(host_samples),
            "wall_time_seconds": wall_time,
            "qmp_status": qemu_status or "ND",
        },
    }


def _sample_host(pid: int | None, samples: list[dict[str, Any]],
                 last_sample: list[float], force: bool = False) -> None:
    if pid is None:
        return
    sample = sample_process(pid)
    timestamp = sample.get("monotonic_seconds")
    if not isinstance(timestamp, (int, float)):
        return
    if force or timestamp - last_sample[0] >= PERF4_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def protocol_run_id(profile: str, mode: str, iteration: int) -> str:
    return f"p4-{profile[0]}-{mode[0]}-{iteration}-{qemu.utc_run_id()}"


def _run_iteration(options: argparse.Namespace, case: dict[str, Any],
                   profile: str, mode: str, iteration: int,
                   run_id: str) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": PERF4_MANIFEST_SCHEMA,
        "run_id": run_id,
        "case": PERF4_CASE,
        "image": str(options.image),
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "idle_seconds": PERF4_IDLE_SECONDS,
        "host_sample_interval_seconds": PERF4_HOST_SAMPLE_SECONDS,
        "network": "user,model=e1000,restrict=on",
        "snapshot": bool(options.snapshot),
        "diagnostics": "deterministic-read-only",
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
            session, str(selected_case.get("guest_case", PERF4_CASE)), 0,
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
    qemu.write_json(artifact_dir / "perf4.json", result)
    return result


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/perf4-memory-storage-network"))
    report_path = qemu.resolve_path(
        options.report, results_root / "perf4-memory-storage-network.json")
    options.image = image
    options.catalog = catalog_path
    options.results = results_root
    case = select_case(catalog_path)
    runs: list[dict[str, Any]] = []
    for profile, mode in PERF4_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, PERF4_ITERATIONS + 1):
            runs.append(_run_iteration(
                options, case, profile, mode, iteration,
                protocol_run_id(profile, mode, iteration)))
    statuses = {run_item.get("status") for run_item in runs}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": PERF4_SCHEMA,
        "version": 1,
        "status": matrix_status,
        "image": {
            "path": str(image),
            "size_bytes": image.stat().st_size if image.is_file() else "ND",
            "sha256": qemu.sha256_file(image) if image.is_file() else "ND",
        },
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode in PERF4_LANES],
        "iterations_per_lane": PERF4_ITERATIONS,
        "windows": {"idle_seconds": PERF4_IDLE_SECONDS},
        "network": "user,model=e1000,restrict=on",
        "host_sampling": {"interval_seconds": PERF4_HOST_SAMPLE_SECONDS},
        "runs": runs,
        "passed_sessions": sum(run_item.get("status") == "PASS"
                                for run_item in runs),
        "total_sessions": len(runs),
    }
    write_report(report_path, matrix)
    print(f"PERF4 memory/storage/network: {matrix_status}")
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
    command_parser.add_argument("--network",
                                default="user,model=e1000,restrict=on")
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
        print(f"ERRO PERF4: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
