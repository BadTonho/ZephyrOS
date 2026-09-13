"""Executa a matriz PERF2 e valida a evidencia de responsividade de entrada."""

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


PERF2_CASE = "qemu:tst5:perf2-input-responsiveness"
PERF2_SCHEMA = "zephyros-perf2-responsiveness-v1"
PERF2_MANIFEST_SCHEMA = "zephyros-perf2-responsiveness-manifest-v1"
PERF2_LANES = (
    ("baseline", "simple"),
    ("baseline", "classic"),
    ("no-vesa", "simple"),
)
PERF2_ITERATIONS = 3
PERF2_STRESS_SECONDS = 10.0
PERF2_CYCLE_MS = 100
PERF2_HOST_SAMPLE_SECONDS = 0.25
PERF2_REQUIRED_METRICS = (
    "input_key_dropped",
    "input_pointer_dropped",
    "input_key_rejected",
    "input_pointer_rejected",
    "keyboard_raw_dropped",
    "mouse_raw_dropped",
    "mouse_packets_dropped",
    "mouse_queue_rejected",
    "mouse_press_events",
    "mouse_release_events",
    "mouse_wheel_events",
    "mouse_button_state",
    "mouse_raw_button_state",
    "mouse_wheel_supported",
    "deferred_irq_1_rejected",
    "deferred_irq_12_rejected",
)
PERF2_ZERO_METRICS = (
    "input_key_dropped",
    "input_pointer_dropped",
    "input_key_rejected",
    "input_pointer_rejected",
    "keyboard_raw_dropped",
    "mouse_raw_dropped",
    "mouse_packets_dropped",
    "mouse_queue_rejected",
    "deferred_irq_1_rejected",
    "deferred_irq_12_rejected",
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
    case = qemu.select_case(catalog, PERF2_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if not isinstance(parameters, dict):
        raise qemu.RunnerError("parametros_perf2_invalidos", "catalog_error", True)
    if parameters.get("iterations") != PERF2_ITERATIONS:
        raise qemu.RunnerError("iteracoes_perf2_invalidas", "catalog_error", True)
    if parameters.get("stress_seconds") != int(PERF2_STRESS_SECONDS):
        raise qemu.RunnerError("duracao_estresse_perf2_invalida", "catalog_error", True)
    if parameters.get("cycle_ms") != PERF2_CYCLE_MS:
        raise qemu.RunnerError("ciclo_perf2_invalido", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-input":
        raise qemu.RunnerError("qmp_input_perf2_ausente", "catalog_error", True)
    return case


def case_for_lane(case: dict[str, Any], profile: str, mode: str) -> dict[str, Any]:
    if (profile, mode) not in PERF2_LANES:
        raise qemu.RunnerError("faixa_perf2_invalida", "catalog_error", True)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict):
        raise qemu.RunnerError("interacao_perf2_ausente", "catalog_error", True)
    steps = interaction.get("steps")
    if not isinstance(steps, list):
        raise qemu.RunnerError("passos_perf2_invalidos", "catalog_error", True)
    mode_seen = False
    selected_steps: list[dict[str, Any]] = []
    for step in steps:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_perf2_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        selected_steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_perf2_ausente", "catalog_error", True)
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
        return int(value, 10)
    except (TypeError, ValueError):
        return None


def count_stimulus_events(trace: list[dict[str, Any]]) -> dict[str, int]:
    in_stress = False
    result = {"cycles": 0, "key_events": 0, "move_events": 0,
              "button_down": 0, "button_up": 0, "wheel_events": 0}
    for entry in trace:
        operation = entry.get("op")
        if operation == "stress_begin":
            in_stress = True
            continue
        if operation == "stress_end":
            result["cycles"] = int(entry.get("cycles", result["cycles"]))
            in_stress = False
            continue
        if not in_stress:
            continue
        if operation == "key":
            result["key_events"] += 1
        if operation != "pointer":
            continue
        for event in entry.get("events", []):
            event_data = event.get("data", event)
            if not isinstance(event_data, dict):
                continue
            if event.get("type") == "rel" and event_data.get("axis") in {"x", "y"}:
                result["move_events"] += 1
            elif event.get("type") == "rel" and event_data.get("axis") == "z":
                result["wheel_events"] += 1
            elif event.get("type") == "btn":
                if event_data.get("button") in {"wheel-up", "wheel-down"}:
                    result["wheel_events"] += 1
                elif event_data.get("down"):
                    result["button_down"] += 1
                else:
                    result["button_up"] += 1
    return result


def validate_guest_records(
    records: list[dict[str, Any]],
    trace: list[dict[str, Any]],
    event: dict[str, Any] | None,
    protocol_errors: list[str],
) -> tuple[str, str | None, dict[str, Any]]:
    stimulus = count_stimulus_events(trace)
    if protocol_errors:
        return "FAIL", "protocolo_guest:" + ",".join(protocol_errors), stimulus
    if event is None:
        return "FAIL", "evento_guest_ausente", stimulus
    if event.get("event") == "BLOCKED":
        return "BLOCKED", "guest_blocked", stimulus
    if event.get("event") != "PASS":
        return "FAIL", f"guest_status:{event.get('event', 'ND')}", stimulus
    if len(records) != 3:
        return "FAIL", f"amostras_guest_incompletas:{len(records)}", stimulus
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial", stimulus
    final = records[-1]
    missing = [name for name in PERF2_REQUIRED_METRICS
               if _metric_value(final, name) is None]
    if missing:
        return "FAIL", "metricas_guest_ND:" + ",".join(missing), stimulus
    for name in PERF2_ZERO_METRICS:
        if _metric_value(final, name) != 0:
            return "FAIL", f"contador_descartes:{name}", stimulus
    if _metric_value(final, "mouse_button_state") != 0 or \
            _metric_value(final, "mouse_raw_button_state") != 0:
        return "FAIL", "botoes_mouse_pendentes", stimulus
    if _metric_value(final, "mouse_wheel_supported") != 1:
        return "FAIL", "roda_mouse_indisponivel", stimulus
    expected = (stimulus["button_down"], stimulus["button_up"],
                stimulus["wheel_events"])
    observed = (_metric_value(final, "mouse_press_events"),
                _metric_value(final, "mouse_release_events"),
                _metric_value(final, "mouse_wheel_events"))
    if observed != expected:
        return "FAIL", f"eventos_discretos:{observed}!={expected}", stimulus
    return "PASS", None, stimulus


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
    validation_status, validation_error, stimulus = validate_guest_records(
        records, trace, event, protocol_errors)
    final_status = status or validation_status
    final_error = error or validation_error
    aggregate = aggregate_records(records) if records else {
        "schema": "zephyros-perf1-metrics-aggregate-v1",
        "record_count": 0,
        "metrics": {},
    }
    host = summarize_process_samples(host_samples, wall_time)
    return {
        "schema": PERF2_SCHEMA,
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
            "input_trace": trace,
            "stimulus": stimulus,
            "protocol_errors": protocol_errors,
        },
        "observations": {
            "input": {
                name: aggregate["metrics"][name]
                for name in aggregate["metrics"]
                if name.startswith(("input_", "keyboard_", "mouse_", "deferred_irq_"))
            },
            "latencies": {
                name: metric for name, metric in aggregate["metrics"].items()
                if metric.get("kind") == "duration" or "latency" in name
            },
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


def _sample_host(
    session: qemu.QemuSession,
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
    if force or timestamp - last_sample[0] >= PERF2_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def protocol_run_id(profile: str, mode: str, iteration: int) -> str:
    profile_token = profile[0] if profile else "x"
    mode_token = mode[0] if mode else "x"
    return f"p2-{profile_token}-{mode_token}-{iteration}-{qemu.utc_run_id()}"


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
        "schema": PERF2_MANIFEST_SCHEMA,
        "run_id": run_id,
        "case": PERF2_CASE,
        "image": str(options.image),
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "network": options.network,
        "stress_seconds": PERF2_STRESS_SECONDS,
        "cycle_ms": PERF2_CYCLE_MS,
        "host_sample_interval_seconds": PERF2_HOST_SAMPLE_SECONDS,
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
            _sample_host(session, process_pid, host_samples, last_sample, True)
        session.input_stress_hook = lambda: _sample_host(
            session, process_pid, host_samples, last_sample)
        qemu.wait_for_ready(session, run_id)
        event = qemu.wait_for_case(
            session, str(selected_case.get("guest_case", PERF2_CASE)), 0,
            (options.seed + iteration) & 0xFFFFFFFF,
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
        _sample_host(session, process_pid, host_samples, last_sample, True)
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
    qemu.write_json(artifact_dir / "perf2.json", result)
    return result


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/perf2-responsiveness"))
    report_path = qemu.resolve_path(
        options.report, results_root / "perf2-responsiveness.json")
    options.image = image
    options.catalog = catalog_path
    options.results = results_root
    case = select_case(catalog_path)
    runs: list[dict[str, Any]] = []
    for profile, mode in PERF2_LANES:
        qemu.validate_qemu_profile(profile)
        for iteration in range(1, PERF2_ITERATIONS + 1):
            run_id = protocol_run_id(profile, mode, iteration)
            runs.append(_run_iteration(options, case, profile, mode,
                                       iteration, run_id))
    statuses = {run.get("status") for run in runs}
    matrix_status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    matrix = {
        "schema": PERF2_SCHEMA,
        "version": 1,
        "status": matrix_status,
        "image": {
            "path": str(image),
            "size_bytes": image.stat().st_size if image.is_file() else "ND",
            "sha256": qemu.sha256_file(image) if image.is_file() else "ND",
        },
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode in PERF2_LANES],
        "iterations_per_lane": PERF2_ITERATIONS,
        "stress": {"seconds": PERF2_STRESS_SECONDS, "cycle_ms": PERF2_CYCLE_MS},
        "host_sampling": {"interval_seconds": PERF2_HOST_SAMPLE_SECONDS},
        "runs": runs,
    }
    write_report(report_path, matrix)
    print(f"PERF2 responsiveness: {matrix_status}")
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
        print(f"ERRO PERF2: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
