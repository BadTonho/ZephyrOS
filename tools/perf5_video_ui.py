"""Executa a matriz PERF5 de video e interfaces."""

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


PERF5_CASE = "qemu:tst5:perf5-video-ui"
PERF5_SCHEMA = "zephyros-perf5-video-ui-v1"
PERF5_MANIFEST_SCHEMA = "zephyros-perf5-video-ui-manifest-v1"
PERF5_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
PERF5_ITERATIONS = 3
PERF5_IDLE_SECONDS = 3.0
PERF5_HOST_SAMPLE_SECONDS = 0.25
UINT32_MASK = 0xFFFFFFFF

PERF5_REQUIRED_METRICS = (
    "vesa_available", "vesa_backbuffer", "vesa_presentations",
    "vesa_partial_presentations", "vesa_bytes_copied",
    "vesa_last_region_width", "vesa_last_region_height",
    "vesa_backbuffer_width", "vesa_backbuffer_height",
    "video_full_redraws", "video_partial_redraws", "video_dirty_regions",
    "cursor_invalidations", "cursor_draws", "cursor_presentations",
    "taskbar_redraws", "taskbar_clock_updates", "taskbar_menu_draws",
    "taskbar_menu_open", "desktop_redraws", "desktop_workspace_redraws",
    "desktop_icon_redraws", "desktop_icon_count", "desktop_active",
    "desktop_mode", "wm_redraws", "wm_window_redraws", "wm_focus_changes",
    "wm_minimize_operations", "wm_maximize_operations",
    "wm_move_operations", "wm_resize_operations", "wm_visible_windows",
    "wm_window_count", "wm_focused_id", "wm_active",
)
PERF5_COUNTER_METRICS = (
    "vesa_presentations", "vesa_full_presentations",
    "vesa_partial_presentations", "vesa_bytes_copied", "vesa_partial_pixels",
    "video_full_redraws", "video_partial_redraws", "video_dirty_regions",
    "cursor_invalidations", "cursor_draws", "cursor_presentations",
    "taskbar_redraws", "taskbar_clock_updates", "taskbar_menu_draws",
    "desktop_redraws", "desktop_workspace_redraws", "desktop_icon_redraws",
    "wm_redraws", "wm_window_redraws", "wm_focus_changes",
    "wm_minimize_operations", "wm_maximize_operations",
    "wm_move_operations", "wm_resize_operations",
)
PERF5_ERROR_METRICS = (
    "mouse_raw_dropped", "mouse_packets_dropped", "mouse_queue_rejected",
    "input_key_dropped", "input_pointer_dropped", "deferred_rejected",
)
PERF5_SCREENSHOTS = ("boot", "baseline", "desktop", "ui", "diagnostics", "final")


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
    case = qemu.select_case(catalog, PERF5_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_perf5_invalidos", "catalog_error", True)
    expected = {
        "iterations": PERF5_ITERATIONS,
        "idle_seconds": int(PERF5_IDLE_SECONDS),
        "host_sample_interval_seconds": PERF5_HOST_SAMPLE_SECONDS,
    }
    for key, value in expected.items():
        if parameters.get(key) != value:
            raise qemu.RunnerError(f"parametro_perf5_invalido:{key}",
                                    "catalog_error", True)
    if parameters.get("network") != "user,model=e1000,restrict=on":
        raise qemu.RunnerError("rede_perf5_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_perf5_ausente", "catalog_error", True)
    return case


def _normalize_classic_ui_steps(steps: list[dict[str, Any]]) -> list[dict[str, Any]]:
    normalized = [dict(step) for step in steps]
    result: list[dict[str, Any]] = []
    machine_count = 0
    menu_count = 0
    for step in normalized:
        if step.get("op") == "key" and step.get("key") == "meta_l":
            menu_count += 1
            if menu_count == 5:
                result.extend((
                    {"op": "key", "key": "meta_l"},
                    {"op": "key", "key": "down"},
                    {"op": "key", "key": "down"},
                    {"op": "key", "key": "enter"},
                    {"op": "wait", "seconds": 1},
                ))
        if step.get("op") == "text" and step.get("text") == "kmetrics machine":
            machine_count += 1
            if machine_count == 3:
                result.extend((
                    {"op": "keys", "keys": ["alt", "f4"]},
                    {"op": "wait", "seconds": 1},
                    {"op": "keys", "keys": ["alt", "f4"]},
                    {"op": "wait", "seconds": 1},
                ))
        result.append(step)
    return result


def case_for_lane(case: dict[str, Any], profile: str,
                  mode: str) -> dict[str, Any]:
    if (profile, mode) not in PERF5_LANES:
        raise qemu.RunnerError("faixa_perf5_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or not isinstance(interaction.get("steps"), list):
        raise qemu.RunnerError("passos_perf5_ausentes", "catalog_error", True)
    steps: list[dict[str, Any]] = []
    mode_seen = False
    for step in interaction["steps"]:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_perf5_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_perf5_ausente", "catalog_error", True)
    selected = dict(case)
    selected["qemu_profile"] = profile
    selected_interaction = dict(interaction)
    if mode == "classic":
        steps = _normalize_classic_ui_steps(steps)
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
    expected = ["boot", "baseline", "ui", "diagnostics", "cleanup", "final"]
    actual = phase_sequence(trace)
    if actual != expected:
        return False, f"fases_invalidas:{actual}"
    return True, None


def _validate_screenshots(artifact_dir: Path, profile: str,
                          trace: list[dict[str, Any]]) -> tuple[bool, str | None,
                                                                  dict[str, Any]]:
    entries = {str(entry.get("label")): entry for entry in trace
               if entry.get("op") == "screenshot"}
    expected_nd = profile == "no-vesa"
    result: dict[str, Any] = {"expected_nd": expected_nd, "labels": {}}
    for label in PERF5_SCREENSHOTS:
        entry = entries.get(label)
        path = artifact_dir / f"screenshot-{label}.ppm"
        present = path.is_file()
        status = entry.get("status") if entry else "ND"
        result["labels"][label] = {
            "status": status, "path": path.name, "present": present,
        }
        if expected_nd:
            continue
        if not entry or status != "ok" or not present:
            return False, f"screenshot_ausente:{label}", result
    return True, None, result


def _validate_no_regression(records: list[dict[str, Any]]) -> str | None:
    baseline = records[1]
    final = records[-1]
    for name in PERF5_ERROR_METRICS:
        before = _metric_value(baseline, name)
        after = _metric_value(final, name)
        if before is None or after is None:
            return f"erro_ND:{name}"
        if ((after - before) & UINT32_MASK) != 0:
            return f"erro_runtime:{name}"
    return None


def _validate_ui_state(records: list[dict[str, Any]], profile: str) -> str | None:
    final = records[-1]
    if _metric_value(final, "taskbar_menu_open") != 0:
        return "taskbar_menu_residual"
    expected_windows = 1 if profile != "no-vesa" and \
        _metric_value(final, "wm_active") == 1 else 0
    if _metric_value(final, "wm_visible_windows") != expected_windows:
        return "janelas_visiveis_residuais"
    if profile == "no-vesa":
        if _metric_value(final, "vesa_available") != 0 or \
                _metric_value(final, "vesa_backbuffer") != 0 or \
                _metric_value(final, "desktop_mode") != 0:
            return "fallback_vesa_inconsistente"
    else:
        if _metric_value(final, "vesa_available") != 1 or \
                _metric_value(final, "vesa_backbuffer") != 1:
            return "vesa_obrigatorio_ND"
    return None


def validate_guest_records(
    records: list[dict[str, Any]], trace: list[dict[str, Any]],
    event: dict[str, Any] | None, protocol_errors: list[str],
    profile: str, artifact_dir: Path,
) -> tuple[str, str | None, dict[str, Any]]:
    phase_valid, phase_error = validate_phase_trace(trace)
    observations: dict[str, Any] = {
        "phases": phase_sequence(trace),
        "input_events": sum(entry.get("op") in {"key", "keys", "pointer"}
                             for entry in trace),
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
    if len(records) != 5:
        return "FAIL", f"amostras_guest_incompletas:{len(records)}", observations
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial", observations
    for record in records[1:]:
        missing = [name for name in PERF5_REQUIRED_METRICS
                   if _metric_value(record, name) is None]
        if profile == "no-vesa":
            missing = [name for name in missing
                       if not name.startswith("vesa_")]
        if missing:
            return "FAIL", "metricas_guest_ND:" + ",".join(missing), observations
    state_error = _validate_ui_state(records, profile)
    if state_error:
        return "FAIL", state_error, observations
    error = _validate_no_regression(records)
    if error:
        return "FAIL", error, observations
    screenshot_valid, screenshot_error, screenshots = _validate_screenshots(
        artifact_dir, profile, trace)
    observations["screenshots"] = screenshots
    if not screenshot_valid:
        return "FAIL", screenshot_error, observations
    baseline = records[1]
    final = records[-1]
    observations.update({
        "presentation_cost": {
            "bytes_copied": _metric_value(final, "vesa_bytes_copied"),
            "partial_pixels": _metric_value(final, "vesa_partial_pixels"),
            "last_copy_ticks": _metric_value(final, "vesa_last_copy_ticks"),
        },
        "regions": {
            "vesa_last": {
                field: _metric_value(final, f"vesa_last_region_{field}")
                for field in ("x", "y", "width", "height", "pixels")
            },
            "video_last_dirty": {
                field: _metric_value(final, f"video_last_dirty_{field}")
                for field in ("x", "y", "width", "height")
            },
        },
        "deltas": {name: _delta(final, baseline, name)
                   for name in PERF5_COUNTER_METRICS},
        "cursor": {name: _metric_value(final, name)
                   for name in PERF5_REQUIRED_METRICS if name.startswith("cursor_")},
        "taskbar": {name: _metric_value(final, name)
                    for name in PERF5_REQUIRED_METRICS if name.startswith("taskbar_")},
        "desktop": {name: _metric_value(final, name)
                    for name in PERF5_REQUIRED_METRICS if name.startswith("desktop_")},
        "wm": {name: _metric_value(final, name)
               for name in PERF5_REQUIRED_METRICS if name.startswith("wm_")},
    })
    return "PASS", None, observations


def _sample_interval(samples: list[dict[str, Any]]) -> float | str:
    timestamps = [sample.get("monotonic_seconds") for sample in samples
                  if isinstance(sample.get("monotonic_seconds"), (int, float))]
    if len(timestamps) < 2:
        return "ND"
    return round(float(timestamps[-1] - timestamps[0]), 6)


def _observed_latency(trace: list[dict[str, Any]]) -> dict[str, Any]:
    inputs = [entry for entry in trace
              if entry.get("op") in {"key", "keys", "pointer"}]
    screenshots = [entry for entry in trace if entry.get("op") == "screenshot" and
                  entry.get("status") == "ok"]
    if not inputs or not screenshots:
        return {"samples": 0, "min_seconds": "ND", "max_seconds": "ND"}
    input_time = float(inputs[0].get("time", 0.0))
    values = [float(item.get("time", 0.0)) - input_time for item in screenshots]
    return {"samples": len(values), "min_seconds": round(min(values), 6),
            "max_seconds": round(max(values), 6), "resolution": "ND"}


def build_run_report(
    image: Path, profile: str, mode: str, iteration: int,
    artifact_dir: Path, records: list[dict[str, Any]], trace: list[dict[str, Any]],
    host_samples: list[dict[str, Any]], event: dict[str, Any] | None,
    protocol_errors: list[str], wall_time: float | str,
    qemu_status: dict[str, Any] | None, error: str | None = None,
    status: str | None = None,
) -> dict[str, Any]:
    validation_status, validation_error, observations = validate_guest_records(
        records, trace, event, protocol_errors, profile, artifact_dir)
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
        "schema": PERF5_SCHEMA, "version": 1, "status": final_status,
        "error": final_error, "profile": profile, "mode": mode,
        "iteration": iteration,
        "image": {"path": str(image),
                  "size_bytes": image.stat().st_size if image.is_file() else "ND",
                  "sha256": qemu.sha256_file(image) if image.is_file() else "ND"},
        "guest": {"samples": records, "aggregate": aggregate,
                  "result": event or {"status": "ND"},
                  "phase_trace": trace, "protocol_errors": protocol_errors},
        "observations": observations,
        "input": {"events": trace, "observed_latency": _observed_latency(trace)},
        "host": {"qemu_process": host, "samples": host_samples,
                 "sample_count": len(host_samples),
                 "sample_interval_seconds": _sample_interval(host_samples),
                 "wall_time_seconds": wall_time,
                 "qmp_status": qemu_status or "ND"},
        "artifacts": {
            "manifest": "manifest.json", "serial": "serial.log",
            "input": "input.log", "qmp_events": "qmp-events.log",
            "screenshots": [path.name for path in artifact_dir.glob("screenshot-*.ppm")],
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
    if force or timestamp - last_sample[0] >= PERF5_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def protocol_run_id(profile: str, mode: str, iteration: int,
                    role: str = "candidate") -> str:
    return f"p5-{role[0]}-{profile[0]}-{mode[0]}-{iteration}-{qemu.utc_run_id()}"


def _run_iteration(options: argparse.Namespace, case: dict[str, Any],
                   profile: str, mode: str, iteration: int,
                   run_id: str) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": PERF5_MANIFEST_SCHEMA, "run_id": run_id, "case": PERF5_CASE,
        "image": str(options.image), "profile": profile, "mode": mode,
        "iteration": iteration, "idle_seconds": PERF5_IDLE_SECONDS,
        "host_sample_interval_seconds": PERF5_HOST_SAMPLE_SECONDS,
        "screenshots": list(PERF5_SCREENSHOTS), "snapshot": bool(options.snapshot),
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
        qemu.wait_for_ready(session, protocol_run_id(profile, mode, iteration))
        event = qemu.wait_for_case(
            session, str(selected_case.get("guest_case", PERF5_CASE)), 0,
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
    qemu.write_json(artifact_dir / "perf5.json", result)
    return result


def _run_matrix(options: argparse.Namespace, case: dict[str, Any],
                role: str = "candidate") -> list[dict[str, Any]]:
    runs: list[dict[str, Any]] = []
    for profile, mode in PERF5_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, PERF5_ITERATIONS + 1):
            runs.append(_run_iteration(
                options, case, profile, mode, iteration,
                protocol_run_id(profile, mode, iteration, role)))
    return runs


def _comparison(candidate: list[dict[str, Any]],
                reference: list[dict[str, Any]] | None) -> dict[str, Any]:
    if reference is None:
        return {"status": "not_requested", "metric": "vesa_bytes_copied"}
    def medians(runs: list[dict[str, Any]]) -> list[int]:
        values = []
        for item in runs:
            value = item.get("observations", {}).get("presentation_cost", {}).get(
                "bytes_copied")
            if isinstance(value, int):
                values.append(value)
        return sorted(values)
    candidate_values = medians(candidate)
    reference_values = medians(reference)
    if not candidate_values or not reference_values:
        return {"status": "ND", "metric": "vesa_bytes_copied",
                "candidate_median": "ND", "reference_median": "ND"}
    candidate_median = candidate_values[(len(candidate_values) - 1) // 2]
    reference_median = reference_values[(len(reference_values) - 1) // 2]
    return {
        "status": "improved" if candidate_median < reference_median else "tie_or_regression",
        "metric": "vesa_bytes_copied", "candidate_median": candidate_median,
        "reference_median": reference_median,
        "candidate_sessions": len(candidate_values),
        "reference_sessions": len(reference_values),
    }


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/perf5-video-ui"))
    report_path = qemu.resolve_path(
        options.report, results_root / "perf5-video-ui.json")
    options.image, options.catalog, options.results = image, catalog_path, results_root
    case = select_case(catalog_path)
    candidate = _run_matrix(options, case)
    reference: list[dict[str, Any]] | None = None
    if options.reference_image:
        reference_options = argparse.Namespace(**vars(options))
        reference_options.image = qemu.resolve_path(options.reference_image, image)
        reference_options.results = results_root / "reference"
        reference = _run_matrix(reference_options, case, "reference")
    statuses = {item.get("status") for item in candidate}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": PERF5_SCHEMA, "version": 1, "status": matrix_status,
        "image": {"path": str(image),
                  "size_bytes": image.stat().st_size if image.is_file() else "ND",
                  "sha256": qemu.sha256_file(image) if image.is_file() else "ND"},
        "lanes": [{"profile": profile, "mode": mode} for profile, mode in PERF5_LANES],
        "iterations_per_lane": PERF5_ITERATIONS,
        "windows": {"idle_seconds": PERF5_IDLE_SECONDS},
        "host_sampling": {"interval_seconds": PERF5_HOST_SAMPLE_SECONDS},
        "screenshots": list(PERF5_SCREENSHOTS), "runs": candidate,
        "reference_runs": reference or [],
        "comparison": _comparison(candidate, reference),
        "passed_sessions": sum(item.get("status") == "PASS" for item in candidate),
        "total_sessions": len(candidate),
    }
    write_report(report_path, matrix)
    print(f"PERF5 video/ui: {matrix_status}")
    print(f"Relatorio: {report_path}")
    return 0 if matrix_status == "PASS" else 2 if matrix_status == "BLOCKED" else 1


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--image")
    command_parser.add_argument("--reference-image")
    command_parser.add_argument("--catalog")
    command_parser.add_argument("--results")
    command_parser.add_argument("--report")
    command_parser.add_argument("--qemu")
    command_parser.add_argument("--qemu-arg", action="append")
    command_parser.add_argument("--cpu", default="max")
    command_parser.add_argument("--network", default="user,model=e1000,restrict=on")
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
        print(f"ERRO PERF5: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
