"""Run the isolated PERF1 guest and QEMU process baseline matrix."""

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
    build_matrix_report,
    build_report,
    parse_machine_file,
    sample_process,
    summarize_process_samples,
    write_report,
)


PERF1_CASE = "qemu:tst5:perf1-baseline"
PERF1_MODES = ("simple", "classic")
PERF1_ITERATIONS = 3
PERF1_IDLE_SECONDS = 3.0


def _arguments(options: argparse.Namespace) -> argparse.Namespace:
    return SimpleNamespace(
        image=options.image,
        catalog=options.catalog,
        results=options.results,
        run_id=None,
        qemu=options.qemu,
        qemu_arg=list(options.qemu_arg or []),
        cpu=options.cpu,
        qemu_profile=options.qemu_profile,
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


def _select_case(catalog_path: Path) -> dict[str, Any]:
    catalog = qemu.load_catalog(catalog_path)
    case = qemu.select_case(catalog, PERF1_CASE)
    qemu.validate_case_for_runner(case)
    parameters = case.get("parameters", {})
    if parameters.get("idle_window_seconds") != int(PERF1_IDLE_SECONDS):
        raise qemu.RunnerError("janela_idle_perf1_invalida", "catalog_error", True)
    return case


def _case_for_mode(case: dict[str, Any], mode: str) -> dict[str, Any]:
    interaction = case.get("interaction")
    if not isinstance(interaction, dict):
        raise qemu.RunnerError("interacao_perf1_ausente", "catalog_error", True)
    steps = interaction.get("steps")
    if not isinstance(steps, list):
        raise qemu.RunnerError("passos_perf1_invalidos", "catalog_error", True)
    mode_seen = False
    mode_steps: list[dict[str, Any]] = []
    for step in steps:
        if not isinstance(step, dict):
            raise qemu.RunnerError("passo_perf1_invalido", "catalog_error", True)
        copied = dict(step)
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
            mode_seen = True
        mode_steps.append(copied)
    if not mode_seen:
        raise qemu.RunnerError("selecao_modo_perf1_ausente", "catalog_error", True)
    selected = dict(case)
    selected_interaction = dict(interaction)
    selected_interaction["steps"] = mode_steps
    selected["interaction"] = selected_interaction
    return selected


def _run_iteration(
    options: argparse.Namespace,
    case: dict[str, Any],
    mode: str,
    iteration: int,
    run_id: str,
) -> dict[str, Any]:
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": "zephyros-perf1-baseline-manifest-v1",
        "run_id": run_id,
        "case": PERF1_CASE,
        "image": str(options.image),
        "profile": options.qemu_profile,
        "mode": mode,
        "iteration": iteration,
        "network": options.network,
        "snapshot": bool(options.snapshot),
        "diagnostics": "read-only",
    })
    arguments = _arguments(options)
    arguments.image = str(options.image)
    session = qemu.QemuSession(arguments, artifact_dir)
    started = time.monotonic()
    process_samples: list[dict[str, Any]] = []
    process_pid: int | None = None
    event: dict[str, Any] | None = None
    error: str | None = None
    exit_code: int | None = None
    guest_case = str(case.get("guest_case", PERF1_CASE))
    try:
        session.start()
        if session.process is not None:
            process_pid = session.process.pid
            process_samples.append(sample_process(process_pid))
        qemu.wait_for_ready(session, run_id)
        case_for_mode = _case_for_mode(case, mode)
        event = qemu.wait_for_case(
            session,
            guest_case,
            0,
            (options.seed + iteration) & 0xFFFFFFFF,
            qemu.case_timeout(case, options.case_timeout),
            qemu.heartbeat_timeout(case, options.heartbeat_timeout),
            case_for_mode,
        )
        if event.get("event") != "PASS":
            error = f"guest_status:{event.get('event', 'ND')}"
        if session.protocol_errors:
            error = "protocolo_guest:" + ",".join(session.protocol_errors)
    except (qemu.RunnerError, MetricsError, OSError, ValueError) as failure:
        error = str(failure)
    finally:
        if process_pid is not None:
            process_samples.append(sample_process(process_pid))
        exit_code = session.stop()

    wall_time = round(time.monotonic() - started, 6)
    host = summarize_process_samples(process_samples, wall_time)
    try:
        records = parse_machine_file(artifact_dir / "serial.log")
    except MetricsError as failure:
        records = []
        error = error or str(failure)
    timestamps = [
        sample["monotonic_seconds"] for sample in process_samples
        if isinstance(sample.get("monotonic_seconds"), (int, float))
    ]
    sample_interval = (
        round(timestamps[-1] - timestamps[0], 6)
        if len(timestamps) >= 2 else "ND"
    )
    result: dict[str, Any]
    if error or not records:
        result = build_report(
            Path(options.image), options.qemu_profile, mode, iteration, records,
            host, event, wall_time, sample_interval,
        )
        result.update({
            "status": "FAIL",
            "run_id": run_id,
            "error": error or "envelope_ausente",
            "qemu_exit_code": exit_code,
            "artifacts": {"serial": "serial.log", "manifest": "manifest.json"},
        })
    else:
        result = build_report(
            Path(options.image), options.qemu_profile, mode, iteration, records,
            host, event, wall_time, sample_interval,
        )
        result.update({
            "status": "PASS",
            "run_id": run_id,
            "qemu_exit_code": exit_code,
            "artifacts": {"serial": "serial.log", "manifest": "manifest.json"},
        })
    qemu.write_json(artifact_dir / "perf1.json", result)
    return result


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(options.results, Path("build/test-results/perf1-baseline"))
    report_path = qemu.resolve_path(
        options.report, results_root / "perf1-baseline.json")
    options.image = image
    options.results = results_root
    case = _select_case(catalog_path)
    qemu.validate_qemu_profile(options.qemu_profile)
    if qemu.qemu_case_profile(case) != options.qemu_profile:
        raise qemu.RunnerError("perfil_qemu_divergente", "catalog_error", True)

    runs: list[dict[str, Any]] = []
    for mode in PERF1_MODES:
        for iteration in range(1, PERF1_ITERATIONS + 1):
            run_id = f"perf1-{mode}-{iteration}-{qemu.utc_run_id()}"
            runs.append(_run_iteration(options, case, mode, iteration, run_id))
    matrix = build_matrix_report(image, options.qemu_profile, runs)
    matrix["status"] = "PASS" if all(
        run.get("status") == "PASS" for run in runs) else "FAIL"
    write_report(report_path, matrix)
    print(f"PERF1 baseline: {matrix['status']}")
    print(f"Relatorio: {report_path}")
    return 0 if matrix["status"] == "PASS" else 1


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--image")
    command_parser.add_argument("--catalog")
    command_parser.add_argument("--results")
    command_parser.add_argument("--report")
    command_parser.add_argument("--qemu")
    command_parser.add_argument("--qemu-arg", action="append")
    command_parser.add_argument("--cpu", default="max")
    command_parser.add_argument("--qemu-profile", default="baseline")
    command_parser.add_argument("--network", default="none")
    command_parser.add_argument("--no-snapshot", dest="snapshot", action="store_false")
    command_parser.set_defaults(snapshot=True)
    command_parser.add_argument("--boot-timeout", type=float, default=qemu.BOOT_TIMEOUT_DEFAULT)
    command_parser.add_argument("--case-timeout", type=float, default=qemu.CASE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--suite-timeout", type=float, default=qemu.SUITE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--heartbeat-timeout", type=float, default=qemu.PROTOCOL_HEARTBEAT_DEFAULT)
    command_parser.add_argument("--seed", type=int, default=1)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    try:
        return run(options)
    except (qemu.RunnerError, MetricsError, OSError, ValueError) as error:
        print(f"ERRO PERF1: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
