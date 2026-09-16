"""Executa a regressao host/QEMU da matriz de hardware suportada."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
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
    parse_machine_records,
    sample_process,
    summarize_process_samples,
)


RLS4_SCHEMA = "zephyros-rls4-supported-matrix-v1"
RLS4_MANIFEST_SCHEMA = "zephyros-rls4-supported-matrix-manifest-v1"
RLS4_CHILD_MANIFEST_SCHEMA = "zephyros-rls4-child-manifest-v1"
RLS4_PROFILES = (
    "baseline", "no-acpi", "network", "no-nic", "usb-hid",
    "no-usb", "usb-storage", "no-storage", "no-vesa", "no-audio",
)
RLS4_MODES = ("simple", "classic")
RLS4_NA_LANES = ({
    "profile": "no-vesa",
    "mode": "classic",
    "status": "NOT_APPLICABLE",
    "reason": "fallback suportado em Simple/serial",
},)
RLS4_ITERATIONS = 3
RLS4_HOST_SAMPLE_SECONDS = 0.25
RLS4_DEFAULT_WORKERS = 4
RLS4_BOOT_TIMEOUT_DEFAULT = 60.0
RLS4_INPUT_KEY_GAP_SECONDS = 0.05
RLS4_PHASES = ("boot", "baseline", "audit", "recovery", "repeat", "final")
RLS4_UI_CASE = "qemu:tst5:rls2-shell-liveness"
RLS4_METRICS_CASE = "qemu:tst5:rls3-invariants"
RLS4_GUEST_BLACKBOX_CASES = frozenset({RLS4_UI_CASE, RLS4_METRICS_CASE})
RLS4_HARDWARE_CASES = {
    "baseline": "qemu:tst6:matrix:baseline",
    "no-acpi": "qemu:hw1:no-acpi",
    "network": "qemu:tst6:matrix:network",
    "no-nic": "qemu:hw1:no-nic",
    "usb-hid": "qemu:tst6:matrix:usb-hid",
    "no-usb": "qemu:hw1:no-usb",
    "usb-storage": "qemu:tst6:matrix:usb-storage",
    "no-storage": "qemu:hw1:no-storage",
    "no-vesa": "qemu:tst6:sec6:no-vesa",
    "no-audio": "qemu:hw1:no-audio",
}
RLS4_PROFILE_CASES = (
    ("update-online-fixture", "qemu:shell5:system-update"),
    ("update-recovery", "qemu:tst5:update-recovery"),
)
RLS4_SIMPLE_FALLBACK_CLOSE_CASE = "qemu:shell5:system-update"
RLS4_BASELINE_FAILURE_CASES = (
    ("fault-update", "qemu:tst6:fault:update"),
    ("fault-recovery", "qemu:tst6:fault:recovery"),
    ("fault-block", "qemu:tst6:fault:block"),
    ("fault-block-cache", "qemu:tst6:fault:block-cache"),
    ("storage-no-space", "qemu:tst5:storage"),
)
RLS4_NETWORK_FAILURE_CASES = (("fault-network", "qemu:tst6:fault:network"),)
RLS4_REQUIRED_CASES = {
    RLS4_UI_CASE,
    RLS4_METRICS_CASE,
    *RLS4_HARDWARE_CASES.values(),
    *(case_id for _, case_id in RLS4_PROFILE_CASES),
    *(case_id for _, case_id in RLS4_BASELINE_FAILURE_CASES),
    *(case_id for _, case_id in RLS4_NETWORK_FAILURE_CASES),
}


class Rls4Error(ValueError):
    """Erro de selecao, protocolo ou agregacao da matriz RLS4."""

    def __init__(self, cause: str, blocked: bool = False):
        super().__init__(cause)
        self.cause = cause
        self.blocked = blocked


def _relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.name


def _safe_command(command: list[str]) -> list[str]:
    return [_relative_path(Path(item)) if Path(item).is_absolute() else item
            for item in command]


def _image_metadata(image: Path) -> dict[str, Any]:
    if not image.is_file():
        return {"path": _relative_path(image), "size_bytes": "ND", "sha256": "ND"}
    result = {
        "path": _relative_path(image),
        "size_bytes": image.stat().st_size,
        "sha256": qemu.sha256_file(image),
    }
    return result


def _lane_plan() -> list[tuple[str, str, int]]:
    return [
        (profile, mode, iteration)
        for profile in RLS4_PROFILES
        for mode in RLS4_MODES
        if (profile, mode) != ("no-vesa", "classic")
        for iteration in range(1, RLS4_ITERATIONS + 1)
    ]


def _profile_plan() -> list[tuple[str, int]]:
    return [
        (profile, iteration)
        for profile in RLS4_PROFILES
        for iteration in range(1, RLS4_ITERATIONS + 1)
    ]


def matrix_plan() -> list[dict[str, Any]]:
    jobs: list[dict[str, Any]] = []
    for profile, mode, iteration in _lane_plan():
        jobs.append({
            "kind": "lane",
            "profile": profile,
            "mode": mode,
            "iteration": iteration,
        })
    for profile, iteration in _profile_plan():
        jobs.append({
            "kind": "profile",
            "profile": profile,
            "mode": "profile",
            "iteration": iteration,
        })
    return jobs


def worker_count(requested: int, total_jobs: int | None = None) -> int:
    total = total_jobs or len(matrix_plan())
    if isinstance(requested, bool) or not isinstance(requested, int):
        raise Rls4Error("workers_rls4_invalidos", True)
    if requested < 1 or requested > total:
        raise Rls4Error("workers_rls4_invalidos", True)
    return min(requested, total)


def _network_for_profile(profile: str) -> str:
    return "user,model=e1000,restrict=on" \
        if profile in {"baseline", "network"} else "none"


def _case_for_catalog(catalog: dict[str, Any], case_id: str) -> dict[str, Any]:
    try:
        case = qemu.select_case(catalog, case_id)
    except qemu.RunnerError as error:
        raise Rls4Error(f"caso_rls4_ausente:{case_id}", True) from error
    qemu.validate_case_for_runner(case)
    if case.get("isolation") not in {"snapshot", "fixture"}:
        raise Rls4Error(f"isolamento_rls4_invalido:{case_id}", True)
    return case


def _mode_case(case: dict[str, Any], mode: str) -> dict[str, Any]:
    selected = dict(case)
    interaction = case.get("interaction")
    if not isinstance(interaction, dict):
        return selected
    steps = interaction.get("steps")
    if not isinstance(steps, list):
        raise Rls4Error(f"interacao_rls4_invalida:{case.get('id')}", True)
    copied_steps: list[dict[str, Any]] = []
    current_phase = ""
    scene_command = ""
    for step in steps:
        if not isinstance(step, dict):
            raise Rls4Error(f"passo_rls4_invalido:{case.get('id')}", True)
        copied = dict(step)
        if copied.get("op") == "phase":
            current_phase = str(copied.get("phase", ""))
        if current_phase == "scenes" and copied.get("op") == "text":
            scene_command = str(copied.get("text", ""))
        if current_phase == "scenes" and mode == "classic" and \
                copied.get("op") == "key" and copied.get("key") == "esc" and \
                scene_command != "desktop":
            copied = {"op": "keys", "keys": ["alt", "f4"]}
        if copied.get("op") == "text" and copied.get("text") == "guimode simple":
            copied["text"] = f"guimode {mode}"
        copied_steps.append(copied)
    selected_interaction = dict(interaction)
    selected_interaction["steps"] = copied_steps
    selected["interaction"] = selected_interaction
    return selected


def _fallback_case(case: dict[str, Any], case_id: str,
                   profile: str) -> dict[str, Any]:
    if profile != "no-vesa" or case_id != RLS4_SIMPLE_FALLBACK_CLOSE_CASE:
        return case
    interaction = case.get("interaction")
    if not isinstance(interaction, dict):
        return case
    steps = interaction.get("steps")
    if not isinstance(steps, list):
        raise Rls4Error(f"interacao_rls4_invalida:{case_id}", True)
    selected = dict(case)
    selected_interaction = dict(interaction)
    selected_steps: list[dict[str, Any]] = []
    for step in steps:
        copied = dict(step)
        if (copied.get("op") == "keys" and
                copied.get("keys") == ["alt", "f4"]):
            copied = {"op": "key", "key": "esc"}
        selected_steps.append(copied)
    selected_interaction["steps"] = selected_steps
    selected["interaction"] = selected_interaction
    return selected


def select_cases(catalog_path: Path) -> dict[str, dict[str, Any]]:
    catalog = qemu.load_catalog(catalog_path)
    cases = {
        case_id: _case_for_catalog(catalog, case_id)
        for case_id in sorted(RLS4_REQUIRED_CASES)
    }
    return cases


def prepare_case(cases: dict[str, dict[str, Any]], case_id: str,
                 profile: str, mode: str) -> dict[str, Any]:
    qemu.validate_qemu_profile(profile)
    if case_id not in cases:
        raise Rls4Error(f"caso_rls4_ausente:{case_id}", True)
    selected = _mode_case(cases[case_id], mode)
    selected = _fallback_case(selected, case_id, profile)
    selected["qemu_profile"] = profile
    required = set(selected.get("required_capabilities", []))
    available = set(qemu.qemu_profile_capabilities(profile))
    if not required.issubset(available):
        missing = ",".join(sorted(required - available))
        raise Rls4Error(f"capacidade_rls4_ausente:{case_id}:{missing}", True)
    if isinstance(selected.get("interaction"), dict):
        parameters = selected.get("parameters")
        parameters = dict(parameters) if isinstance(parameters, dict) else {}
        configured_gap = parameters.get("input_key_gap_seconds")
        if (not isinstance(configured_gap, (int, float)) or
                isinstance(configured_gap, bool) or
                configured_gap < RLS4_INPUT_KEY_GAP_SECONDS):
            parameters["input_key_gap_seconds"] = RLS4_INPUT_KEY_GAP_SECONDS
        selected["parameters"] = parameters
    qemu.validate_case_for_runner(selected)
    return selected


def _storage_args(path: Path | None, snapshot: bool = True) -> list[str]:
    if path is None:
        return []
    drive = f"file={path},format=raw,if=ide,index=1"
    if not snapshot:
        drive += ",readonly=on"
    return [
        "-drive", drive,
    ]


def _arguments(options: argparse.Namespace, profile: str,
               qemu_args: list[str], fixture: str | None,
               storage_image: Path | None,
               input_transport: str = qemu.QEMU_INPUT_TRANSPORT_DEFAULT
               ) -> argparse.Namespace:
    return SimpleNamespace(
        image=str(options.image), catalog=str(options.catalog), results=str(options.results),
        run_id=None, qemu=options.qemu, qemu_arg=qemu_args, cpu=options.cpu,
        qemu_profile=profile, network=_network_for_profile(profile),
        storage_image=str(storage_image) if storage_image else None,
        input_transport=input_transport,
        snapshot=options.snapshot, fixture=fixture, profile="smoke",
        boot_timeout=options.boot_timeout, case_timeout=options.case_timeout,
        suite_timeout=options.suite_timeout,
        heartbeat_timeout=options.heartbeat_timeout, coverage_symbols=None,
    )


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
    if force or timestamp - last_sample[0] >= RLS4_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def _read_serial(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _parse_records(serial_text: str) -> tuple[list[dict[str, Any]], str | None]:
    if "@@ZMETRIC/1 " not in serial_text:
        return [], None
    try:
        return parse_machine_records(serial_text), None
    except MetricsError as error:
        return [], str(error)


def _prompt_observed(event: dict[str, Any] | None, serial_text: str,
                     interactive: bool,
                     expected_guest_case: str | None = None) -> bool:
    if "zephyr>" in serial_text:
        return True
    return bool(
        interactive
        and event
        and event.get("event") == "PASS"
        and (event.get("case") in RLS4_GUEST_BLACKBOX_CASES or
             (expected_guest_case is not None and
              event.get("case") == expected_guest_case))
    )


def _child_status(event: dict[str, Any] | None, protocol_errors: list[str],
                 serial_text: str, records: list[dict[str, Any]],
                 parse_error: str | None, required_envelope: bool,
                 interactive: bool,
                 expected_guest_case: str | None = None) -> tuple[str, str | None]:
    if protocol_errors:
        return "FAIL", "protocolo_guest:" + ",".join(protocol_errors)
    if parse_error:
        return "FAIL", "envelope_guest:" + parse_error
    event_name = event.get("event") if event else None
    if event_name == "BLOCKED":
        return "BLOCKED", "caso_guest_blocked"
    if event_name != "PASS":
        return "FAIL", f"guest_status:{event_name or 'ND'}"
    if required_envelope and not records:
        return "FAIL", "envelope_guest_ausente"
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial"
    if interactive and not _prompt_observed(event, serial_text, interactive,
                                            expected_guest_case):
        return "FAIL", "prompt_ausente"
    return "PASS", None


def _run_child(options: argparse.Namespace, cases: dict[str, dict[str, Any]],
               child_dir: Path, scenario: str, case_id: str,
               profile: str, mode: str, iteration: int, seed: int,
               required_envelope: bool = False,
               storage_image: Path | None = None,
               fixture: str | None = None) -> dict[str, Any]:
    child_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(child_dir)
    selected_case = prepare_case(cases, case_id, profile, mode)
    qemu_args = _storage_args(storage_image, options.snapshot)
    input_transport = qemu.QEMU_INPUT_TRANSPORT_DEFAULT
    if (selected_case.get("interaction") and
            profile in {"usb-hid", "usb-storage"}):
        input_transport = qemu.QEMU_INPUT_TRANSPORT_PS2_FALLBACK
    arguments = _arguments(options, profile, qemu_args, fixture,
                           storage_image, input_transport)
    run_id = f"r4-{profile[:2]}-{mode[:2]}-{iteration}-{scenario[:8]}"[:47]
    session = qemu.QemuSession(arguments, child_dir)
    host_samples: list[dict[str, Any]] = []
    last_sample = [float("-inf")]
    process_pid: int | None = None
    event: dict[str, Any] | None = None
    error: str | None = None
    status: str | None = None
    started = time.monotonic()
    try:
        qemu.write_json(child_dir / "manifest.json", {
            "schema": RLS4_CHILD_MANIFEST_SCHEMA,
            "run_id": run_id,
            "scenario": scenario,
            "case": case_id,
            "guest_case": selected_case.get("guest_case", case_id),
            "profile": profile,
            "mode": mode,
            "iteration": iteration,
            "image": _relative_path(Path(options.image)),
            "network": _network_for_profile(profile),
            "input_transport": input_transport,
            "fixture": fixture,
            "snapshot": bool(options.snapshot),
            "command": _safe_command(session.command()),
        })
        session.start()
        if session.process is not None:
            process_pid = session.process.pid
            _sample_host(process_pid, host_samples, last_sample, True)
        session.host_sample_hook = lambda: _sample_host(
            process_pid, host_samples, last_sample)
        qemu.wait_for_ready(session, run_id)
        event = qemu.wait_for_case(
            session, str(selected_case.get("guest_case", case_id)), 0, seed,
            qemu.case_timeout(selected_case, options.case_timeout),
            qemu.heartbeat_timeout(selected_case, options.heartbeat_timeout),
            selected_case,
        )
    except qemu.RunnerError as failure:
        status = "BLOCKED" if failure.blocked else "FAIL"
        error = failure.cause
    except (OSError, ValueError) as failure:
        status = "FAIL"
        error = str(failure)
    finally:
        _sample_host(process_pid, host_samples, last_sample, True)
        exit_code = session.stop()
    wall_time = round(time.monotonic() - started, 6)
    serial_text = _read_serial(child_dir / "serial.log")
    records, parse_error = _parse_records(serial_text)
    derived_status, derived_error = _child_status(
        event, session.protocol_errors, serial_text, records, parse_error,
        required_envelope, bool(selected_case.get("interaction")),
        str(selected_case.get("guest_case", case_id)),
    )
    prompt_observed = _prompt_observed(
        event, serial_text, bool(selected_case.get("interaction")),
        str(selected_case.get("guest_case", case_id)))
    final_status = status or derived_status
    final_error = error or derived_error
    try:
        aggregate = aggregate_records(records) if records else {
            "schema": "zephyros-perf1-metrics-aggregate-v1",
            "record_count": 0,
            "metrics": {},
        }
    except MetricsError as failure:
        aggregate = {"schema": "zephyros-perf1-metrics-aggregate-v1",
                     "record_count": len(records), "metrics": {}}
        final_status = "FAIL"
        final_error = final_error or str(failure)
    result = {
        "scenario": scenario,
        "case": case_id,
        "guest_case": selected_case.get("guest_case", case_id),
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "status": final_status,
        "error": final_error,
        "event": event or {"event": "ND"},
        "protocol_errors": list(session.protocol_errors),
        "guest": {
            "records": records,
            "aggregate": aggregate,
            "required_envelope": required_envelope,
            "prompt_observed": prompt_observed,
            "serial_bytes": len(serial_text.encode("utf-8")),
        },
        "host": {
            "qemu_process": summarize_process_samples(host_samples, wall_time),
            "samples": host_samples,
            "sample_count": len(host_samples),
            "sample_interval_seconds": _sample_interval(host_samples),
            "wall_time_seconds": wall_time,
            "qmp_status": session.qmp_status or "ND",
        },
        "input": session.input_trace,
        "qmp_events": session.qmp_events,
        "diagnostics": session.diagnostics,
        "artifacts": {
            "directory": _relative_path(child_dir),
            "manifest": "manifest.json",
            "serial": "serial.log",
            "input": "input.log",
            "qmp_events": "qmp-events.log",
        },
        "qemu_exit_code": exit_code,
    }
    qemu.write_json(child_dir / "result.json", result)
    return result


def _failure_status(children: list[dict[str, Any]]) -> tuple[str, str | None]:
    if any(item.get("status") == "BLOCKED" for item in children):
        return "BLOCKED", "caso_qemu_bloqueado"
    if any(item.get("status") != "PASS" for item in children):
        errors = [str(item.get("error")) for item in children
                  if item.get("status") != "PASS" and item.get("error")]
        return "FAIL", ";".join(errors) if errors else "caso_reprovado"
    return "PASS", None


def _run_job(options: argparse.Namespace, cases: dict[str, dict[str, Any]],
             root: Path, job: dict[str, Any], seed: int) -> dict[str, Any]:
    profile = str(job["profile"])
    mode = str(job["mode"])
    iteration = int(job["iteration"])
    job_id = f"{job['kind']}-{profile}-{mode}-{iteration}"
    job_dir = root / job_id
    job_dir.mkdir(parents=True, exist_ok=False)
    children: list[dict[str, Any]] = []
    if job["kind"] == "lane":
        specs = [
            ("shell", RLS4_UI_CASE, False, None, None),
            ("diagnostics", RLS4_METRICS_CASE, True, None, "readonly-update"),
            ("hardware", RLS4_HARDWARE_CASES[profile], False, None, None),
        ]
    else:
        specs = [
            (name, case_id, False, None, "readonly-update")
            for name, case_id in RLS4_PROFILE_CASES
        ]
        if profile == "baseline":
            specs.extend((name, case_id, False, None, None)
                         for name, case_id in RLS4_BASELINE_FAILURE_CASES[:-1])
            specs.append(("storage-no-space", "qemu:tst5:storage", False,
                          options.no_space_image, "readonly"))
        if profile == "network":
            specs.extend((name, case_id, False, None, None)
                         for name, case_id in RLS4_NETWORK_FAILURE_CASES)
    for index, (scenario, case_id, required_envelope, storage_image, fixture) in enumerate(specs):
        children.append(_run_child(
            options, cases, job_dir / f"child-{index:02d}-{scenario}",
            scenario, case_id, profile, mode if job["kind"] == "lane" else "simple",
            iteration, (seed + index * 2654435761) & 0xFFFFFFFF,
            required_envelope, storage_image, fixture,
        ))
    status, error = _failure_status(children)
    records = [record for child in children for record in
               child["guest"].get("records", [])]
    return {
        "id": job_id,
        "kind": job["kind"],
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "status": status,
        "error": error,
        "phases": list(RLS4_PHASES),
        "children": children,
        "guest": {
            "records": records,
            "aggregate": aggregate_records(records) if records else {
                "schema": "zephyros-perf1-metrics-aggregate-v1",
                "record_count": 0,
                "metrics": {},
            },
        },
        "observations": {
            "prompt_children": sum(
                1 for child in children if child["guest"].get("prompt_observed")),
            "interactive_children": sum(
                1 for child in children
                if cases[child["case"]].get("interaction")),
            "fallback_profile": profile in {"no-acpi", "no-nic", "no-usb",
                                            "no-storage", "no-vesa", "no-audio"},
            "persistent_image_write": False,
        },
        "artifacts": {"directory": _relative_path(job_dir)},
    }


def _job_failure(job: dict[str, Any], error: Exception) -> dict[str, Any]:
    return {
        "id": f"{job['kind']}-{job['profile']}-{job['mode']}-{job['iteration']}",
        "kind": job["kind"], "profile": job["profile"],
        "mode": job["mode"], "iteration": job["iteration"],
        "status": "FAIL", "error": f"worker_exception:{error}",
        "phases": list(RLS4_PHASES), "children": [],
        "guest": {"records": [], "aggregate": {
            "schema": "zephyros-perf1-metrics-aggregate-v1",
            "record_count": 0, "metrics": {},
        }},
        "observations": {"prompt_children": 0, "interactive_children": 0,
                         "persistent_image_write": False},
        "artifacts": {"directory": "ND"},
    }


def build_report(image: Path, catalog: Path, jobs: list[dict[str, Any]],
                 workers: int, seed: int) -> dict[str, Any]:
    primary = [job for job in jobs if job.get("kind") == "lane"]
    complementary = [job for job in jobs if job.get("kind") == "profile"]
    statuses = {str(job.get("status")) for job in jobs}
    status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    return {
        "schema": RLS4_SCHEMA,
        "version": 1,
        "status": status,
        "image": _image_metadata(image),
        "catalog": {"path": _relative_path(catalog),
                    "sha256": qemu.sha256_file(catalog) if catalog.is_file() else "ND"},
        "profiles": list(RLS4_PROFILES),
        "modes": list(RLS4_MODES),
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode, _ in _lane_plan()],
        "not_applicable_lanes": list(RLS4_NA_LANES),
        "iterations_per_lane": RLS4_ITERATIONS,
        "phases": list(RLS4_PHASES),
        "host_sampling": {"interval_seconds": RLS4_HOST_SAMPLE_SECONDS},
        "execution": {"workers": workers, "parallel": workers > 1,
                       "seed": seed, "primary_jobs": len(primary),
                       "complementary_jobs": len(complementary)},
        "coverage": {
            "primary": ["shell", "diagnostics", "hardware"],
            "complementary": ["update-online-fixture", "update-recovery",
                              "fault-update", "fault-recovery", "fault-block",
                              "fault-block-cache", "storage-no-space",
                              "fault-network"],
            "persistent_image_write": False,
        },
        "sessions": jobs,
        "passed_sessions": sum(job.get("kind") == "lane" and
                                job.get("status") == "PASS" for job in jobs),
        "total_sessions": len(primary),
        "passed_complementary": sum(job.get("kind") == "profile" and
                                     job.get("status") == "PASS" for job in jobs),
        "total_complementary": len(complementary),
        "artifacts": {
            "manifest": "manifest.json",
            "sessions": "sessions/",
            "report": "rls4-supported-matrix.json",
        },
    }


def run(options: argparse.Namespace) -> int:
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog_path = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results_root = qemu.resolve_path(
        options.results, Path("build/test-results/rls4-supported-matrix"))
    report_path = qemu.resolve_path(
        options.report, results_root / "rls4-supported-matrix.json")
    options.image = image
    options.catalog = catalog_path
    options.results = results_root
    if not image.is_file():
        raise Rls4Error(f"imagem_ausente:{image}", True)
    if not catalog_path.is_file():
        raise Rls4Error(f"catalogo_ausente:{catalog_path}", True)
    cases = select_cases(catalog_path)
    if options.no_space_image and not Path(options.no_space_image).is_file():
        raise Rls4Error(f"fixture_no_space_ausente:{options.no_space_image}", True)
    results_root.mkdir(parents=True, exist_ok=True)
    run_root = results_root / f"run-{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}-{time.time_ns() % 1000000:06d}"
    sessions_root = run_root / "sessions"
    sessions_root.mkdir(parents=True, exist_ok=False)
    jobs = matrix_plan()
    workers = worker_count(options.workers, len(jobs))
    qemu.write_json(run_root / "manifest.json", {
        "schema": RLS4_MANIFEST_SCHEMA,
        "image": _image_metadata(image),
        "catalog": _relative_path(catalog_path),
        "profiles": list(RLS4_PROFILES),
        "modes": list(RLS4_MODES),
        "iterations_per_lane": RLS4_ITERATIONS,
        "not_applicable_lanes": list(RLS4_NA_LANES),
        "workers": workers,
        "seed": options.seed,
        "snapshot": bool(options.snapshot),
        "network_policy": "restricted-user-e1000-or-none",
        "fixture_policy": "isolated",
        "persistent_image_write": False,
    })
    runs: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=workers,
                            thread_name_prefix="rls4-qemu") as executor:
        futures = {
            executor.submit(_run_job, options, cases, sessions_root, job,
                            (options.seed + index * 2654435761) & 0xFFFFFFFF): job
            for index, job in enumerate(jobs)
        }
        for future in as_completed(futures):
            job = futures[future]
            try:
                runs.append(future.result())
            except Exception as error:
                runs.append(_job_failure(job, error))
    runs.sort(key=lambda item: str(item.get("id", "")))
    report = build_report(image, catalog_path, runs, workers, options.seed)
    report["run_id"] = run_root.name
    report["run_directory"] = _relative_path(run_root)
    qemu.write_json(run_root / "result.json", report)
    qemu.write_json(report_path, report)
    print(f"RLS4 supported matrix: {report['status']}")
    print(f"Sessoes: {report['passed_sessions']}/{report['total_sessions']}")
    print(f"Relatorio: {report_path}")
    return 0 if report["status"] == "PASS" else \
        2 if report["status"] == "BLOCKED" else 1


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--image")
    command_parser.add_argument("--catalog")
    command_parser.add_argument("--results")
    command_parser.add_argument("--report")
    command_parser.add_argument("--qemu")
    command_parser.add_argument("--qemu-arg", action="append")
    command_parser.add_argument("--cpu", default="max")
    command_parser.add_argument("--storage-image", default="build/storage-valid.img")
    command_parser.add_argument("--no-space-image", default="build/storage-fat32-no-space.img")
    command_parser.add_argument("--no-snapshot", dest="snapshot", action="store_false")
    command_parser.set_defaults(snapshot=True)
    command_parser.add_argument("--boot-timeout", type=float,
                                default=RLS4_BOOT_TIMEOUT_DEFAULT)
    command_parser.add_argument("--case-timeout", type=float,
                                default=qemu.CASE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--suite-timeout", type=float,
                                default=qemu.SUITE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--heartbeat-timeout", type=float,
                                default=qemu.PROTOCOL_HEARTBEAT_DEFAULT)
    command_parser.add_argument("--workers", type=int, default=RLS4_DEFAULT_WORKERS)
    command_parser.add_argument("--seed", type=int, default=2404)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    options.storage_image = str(qemu.resolve_path(options.storage_image, Path("build/storage-valid.img")))
    options.no_space_image = str(qemu.resolve_path(options.no_space_image, Path("build/storage-fat32-no-space.img")))
    try:
        return run(options)
    except Rls4Error as error:
        print(f"RLS4: {'BLOCKED' if error.blocked else 'FAIL'} {error.cause}",
              file=sys.stderr)
        return 2 if error.blocked else 1
    except (MetricsError, OSError, ValueError) as error:
        print(f"RLS4: FAIL {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
