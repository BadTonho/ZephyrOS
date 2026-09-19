"""Audita DHCP automatico, DNS e HTTP externo em QEMU."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import copy
import json
import os
from pathlib import Path
import re
import sys
import time
from types import SimpleNamespace
from typing import Any
from urllib.parse import urlsplit

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


NET1_CASE = "qemu:net1:external-connectivity"
NET1_GUEST_CASE = "qemu:tst5:network"
NET1_HOST_CASE = "host:net1:external-connectivity"
NET1_SCHEMA = "zephyros-net1-external-connectivity-v1"
NET1_MANIFEST_SCHEMA = "zephyros-net1-external-connectivity-manifest-v1"
NET1_EXTERNAL_HOST = "example.com"
NET1_EXTERNAL_URL = "http://example.com/"
NET1_QEMU_NETWORK = "user,model=e1000"
NET1_ITERATIONS = 3
NET1_HOST_SAMPLE_SECONDS = 0.25
NET1_DHCP_WAIT_SECONDS = 10.0
NET1_PHASES = ("boot", "baseline", "dhcp", "dns", "http", "final")
NET1_LANES = (
    ("external", "simple"),
    ("external", "classic"),
    ("restricted", "simple"),
    ("no-nic", "simple"),
)
UINT32_MASK = 0xFFFFFFFF
HOST_PATTERN = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$")


class Net1Error(ValueError):
    """Erro de configuracao, protocolo ou validacao do NET1."""

    def __init__(self, cause: str, blocked: bool = False):
        super().__init__(cause)
        self.cause = cause
        self.blocked = blocked


def relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.name


def validate_external_host(host: str) -> str:
    value = str(host).strip()
    if not value or len(value) > 253 or not HOST_PATTERN.fullmatch(value):
        raise Net1Error("host_externo_invalido", True)
    if ".." in value or any(label.startswith("-") or label.endswith("-")
                             for label in value.split(".")):
        raise Net1Error("host_externo_invalido", True)
    return value


def validate_external_url(url: str) -> str:
    value = str(url).strip()
    parsed = urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise Net1Error("url_externa_invalida", True)
    if parsed.username or parsed.password or "@" in parsed.netloc:
        raise Net1Error("credencial_na_url_externa", True)
    if any(ord(char) < 0x20 or ord(char) == 0x7F for char in value):
        raise Net1Error("controle_na_url_externa", True)
    if len(value) > 511 or not parsed.path.startswith("/"):
        raise Net1Error("url_externa_invalida", True)
    return value


def validate_workers(value: int, total: int = NET1_ITERATIONS * len(NET1_LANES)) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise Net1Error("workers_net1_invalidos", True)
    return min(value, total)


def matrix_plan() -> list[dict[str, Any]]:
    return [
        {"profile": profile, "mode": mode, "iteration": iteration}
        for profile, mode in NET1_LANES
        for iteration in range(1, NET1_ITERATIONS + 1)
    ]


def _command_steps(command: str, wait: float = 1.0) -> list[dict[str, Any]]:
    return [
        {"op": "text", "text": command},
        {"op": "key", "key": "enter"},
        {"op": "wait", "seconds": wait},
    ]


def build_interaction(mode: str, lane: str, host: str,
                      url: str) -> dict[str, Any]:
    if mode not in {"simple", "classic"} or lane not in {
            "external", "restricted", "no-nic"}:
        raise Net1Error("lane_net1_invalida", True)
    steps: list[dict[str, Any]] = [
        {"op": "phase", "phase": "boot"},
        {"op": "key", "key": "meta_l"},
        {"op": "key", "key": "down"},
        {"op": "key", "key": "enter"},
        {"op": "wait", "seconds": 2.0},
        *_command_steps(f"guimode {mode}"),
        {"op": "phase", "phase": "baseline"},
        *_command_steps("kmetrics machine"),
        *_command_steps("net status"),
    ]
    if lane == "no-nic":
        steps.extend([
            {"op": "phase", "phase": "final"},
            *_command_steps("kmetrics machine"),
            *_command_steps("net status"),
            *_command_steps("echo tst5-network"),
            *_command_steps("echo net1-external-connectivity"),
        ])
    else:
        steps.extend([
            *_command_steps("kmetrics reset", 0.5),
            {"op": "phase", "phase": "dhcp"},
            {"op": "wait", "seconds": NET1_DHCP_WAIT_SECONDS},
            *_command_steps("net status", 0.5),
            {"op": "phase", "phase": "dns"},
            *_command_steps(f"nslookup {host}", 4.0),
            {"op": "phase", "phase": "http"},
            *_command_steps(f"http get {url}", 8.0),
        ])
        if lane == "restricted":
            steps.extend([
                {"op": "key", "key": "f11"},
                {"op": "wait", "seconds": 2.0},
            ])
        steps.extend([
            {"op": "phase", "phase": "final"},
            *_command_steps("kmetrics machine"),
            *_command_steps("net status"),
            *_command_steps("echo tst5-network"),
            *_command_steps("echo net1-external-connectivity"),
        ])
    return {"mode": "qmp-input", "steps": steps,
            "post_action": {"type": "none", "steps": []}}


def _arguments(options: argparse.Namespace, profile: str,
               lane: str, mode: str) -> argparse.Namespace:
    if lane == "restricted":
        qemu_profile = "network-restricted"
        network = ""
    elif lane == "no-nic":
        qemu_profile = "no-nic"
        network = "none"
    else:
        qemu_profile = "baseline"
        network = options.network
    qemu_arg = list(options.qemu_arg or [])
    if "-k" not in qemu_arg:
        qemu_arg.extend(["-k", "pt-br"])
    return SimpleNamespace(
        image=str(options.image), catalog=str(options.catalog),
        results=str(options.results), run_id=None, qemu=options.qemu,
        qemu_arg=qemu_arg, cpu=options.cpu,
        qemu_profile=qemu_profile, network=network, storage_image=None,
        snapshot=options.snapshot, fixture=None, profile=profile,
        boot_timeout=options.boot_timeout, case_timeout=options.case_timeout,
        suite_timeout=options.suite_timeout,
        heartbeat_timeout=options.heartbeat_timeout,
        coverage_symbols=None,
        input_transport=qemu.QEMU_INPUT_TRANSPORT_DEFAULT,
    )


def _metric_value(metric: dict[str, Any] | None) -> int | None:
    if not metric or metric.get("value") == "ND":
        return None
    try:
        return int(metric["value"])
    except (KeyError, TypeError, ValueError):
        return None


def _latest_values(records: list[dict[str, Any]]) -> dict[str, int | None]:
    if not records:
        return {}
    return {name: _metric_value(metric)
            for name, metric in records[-1].get("metrics", {}).items()}


def validate_phases(input_trace: list[dict[str, Any]], lane: str) -> tuple[bool, str | None]:
    phases = [item.get("phase") for item in input_trace
              if item.get("op") == "phase"]
    expected = ("boot", "baseline", "final") if lane == "no-nic" \
        else NET1_PHASES
    if phases != list(expected):
        return False, "fases_net1_invalidas"
    return True, None


def _prompt_observed(serial_text: str, values: dict[str, int | None]) -> bool:
    if "zephyr>" in serial_text:
        return True
    if values.get("shell_lifecycle_prompt_state") != 0 or \
            values.get("shell_lifecycle_input_blocked") != 0:
        return False
    rendered = values.get("shell_lifecycle_prompt_rendered")
    if rendered is None:
        return values.get("shell_lifecycle_terminal_active") == 1 and \
            values.get("shell_lifecycle_focus_shell") == 1 and \
            values.get("shell_lifecycle_scene_active") == 0 and \
            values.get("shell_lifecycle_job_active") == 0 and \
            values.get("shell_lifecycle_loader_active") == 0
    return rendered > 0 and \
        values.get("shell_lifecycle_prompt_missing") == 0 and \
        values.get("shell_lifecycle_prompt_duplicates") == 0


def _required_common(values: dict[str, int | None]) -> str | None:
    required = ("network_interfaces", "network_ipv4_configured",
                "dhcp_initialized", "dns_initialized", "http_initialized",
                "socket_active_count", "net_socket_active_count",
                "net_buffer_active_buffers", "sk_buff_active_buffers")
    for name in required:
        if name not in values:
            return f"metrica_ausente:{name}"
    return None


def validate_run(lane: str, records: list[dict[str, Any]],
                 input_trace: list[dict[str, Any]], event: dict[str, Any] | None,
                 protocol_errors: list[str], serial_text: str,
                 error: str | None = None,
                 blocked: bool = False) -> tuple[str, str | None, dict[str, Any]]:
    if blocked:
        return "BLOCKED", error or "qemu_indisponivel", {}
    if error:
        return "FAIL", error, {}
    if protocol_errors:
        return "FAIL", "protocolo_guest:" + ",".join(protocol_errors), {}
    if not event or event.get("event") != "PASS":
        return "FAIL", "caso_guest_nao_passou", {}
    if not records:
        return "FAIL", "envelope_guest_ausente", {}
    if any(record.get("status") != "ok" for record in records):
        return "FAIL", "envelope_guest_partial", {}
    values = _latest_values(records)
    valid, phase_error = validate_phases(input_trace, lane)
    if not valid:
        return "FAIL", phase_error, {}
    if not _prompt_observed(serial_text, values):
        return "FAIL", "prompt_ausente", {}
    missing = _required_common(values)
    if missing:
        return "FAIL", missing, {}
    observations: dict[str, Any] = {
        "dhcp_automatic": values.get("dhcp_bound") == 1,
        "ipv4_configured": values.get("network_ipv4_configured") == 1,
        "dns_configured": values.get("dns_configured") == 1,
        "http_status_code": values.get("http_status_code"),
        "latest_metrics": values,
    }
    if lane == "external":
        if values.get("dhcp_bound") != 1:
            return "FAIL", "dhcp_automatico_nao_comprovado", observations
        if values.get("network_ipv4_configured") != 1 or \
                values.get("ipv4_configured") != 1:
            return "FAIL", "ipv4_nao_configurado", observations
        if values.get("dns_configured") != 1 or \
                not (values.get("dns_replies") or 0) > 0:
            return "FAIL", "dns_nao_comprovado", observations
        status_code = values.get("http_status_code")
        if not (values.get("http_responses") or 0) > 0 or \
                status_code is None or status_code < 200:
            return "FAIL", "http_externo_nao_comprovado", observations
    elif lane == "restricted":
        observations["network_restricted"] = True
        if (values.get("http_responses") or 0) > 0:
            return "FAIL", "rede_restrita_respondeu_http_externo", observations
    else:
        observations["nic_absent"] = values.get("network_interfaces") == 0
        if not observations["nic_absent"]:
            return "FAIL", "nic_nao_ausente", observations
        if values.get("network_ipv4_configured") == 1:
            return "FAIL", "ipv4_configurado_sem_nic", observations
    for name in ("socket_active_count", "net_socket_active_count",
                 "net_buffer_active_buffers", "sk_buff_active_buffers"):
        if values.get(name) not in {0, None}:
            return "FAIL", f"residual:{name}", observations
    return "PASS", None, observations


def _sample_host(pid: int | None, samples: list[dict[str, Any]],
                 last_sample: list[float], force: bool = False) -> None:
    if pid is None:
        return
    sample = sample_process(pid)
    timestamp = sample.get("monotonic_seconds")
    if not isinstance(timestamp, (int, float)):
        return
    if force or timestamp - last_sample[0] >= NET1_HOST_SAMPLE_SECONDS:
        samples.append(sample)
        last_sample[0] = float(timestamp)


def _run_id(profile: str, mode: str, iteration: int) -> str:
    suffix = int(time.time() * 1000) & 0xFFFFF
    return f"n1-{profile[:6]}-{mode[0]}-{iteration}-{suffix:x}"


def build_report(image: Path, runs: list[dict[str, Any]],
                 external_host: str = NET1_EXTERNAL_HOST,
                 external_url: str = NET1_EXTERNAL_URL,
                 network: str = NET1_QEMU_NETWORK,
                 workers: int = 4) -> dict[str, Any]:
    statuses = {item.get("status") for item in runs}
    status = "BLOCKED" if "BLOCKED" in statuses else \
        "FAIL" if "FAIL" in statuses else "PASS"
    return {
        "schema": NET1_SCHEMA, "version": 1, "status": status,
        "image": {"path": relative_path(image),
                  "size_bytes": image.stat().st_size if image.is_file() else "ND",
                  "sha256": qemu.sha256_file(image) if image.is_file() else "ND"},
        "configuration": {"external_host": external_host,
                          "external_url": external_url,
                          "qemu_network": network, "workers": workers},
        "lanes": [{"profile": profile, "mode": mode}
                  for profile, mode in NET1_LANES],
        "iterations_per_lane": NET1_ITERATIONS,
        "host_sampling": {"interval_seconds": NET1_HOST_SAMPLE_SECONDS},
        "runs": sorted(runs, key=lambda item: (
            item.get("profile", ""), item.get("mode", ""),
            item.get("iteration", 0))),
        "passed_sessions": sum(item.get("status") == "PASS" for item in runs),
        "total_sessions": len(runs), "not_applicable": [],
        "credentials_stored": False,
    }


def _run_session(options: argparse.Namespace, catalog_case: dict[str, Any],
                 lane: str, mode: str, iteration: int) -> dict[str, Any]:
    run_id = _run_id(lane, mode, iteration)
    artifact_dir = Path(options.results) / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    qemu.initialize_artifacts(artifact_dir)
    qemu.write_json(artifact_dir / "manifest.json", {
        "schema": NET1_MANIFEST_SCHEMA, "run_id": run_id,
        "case": NET1_CASE, "guest_case": NET1_GUEST_CASE,
        "image": relative_path(Path(options.image)), "lane": lane,
        "mode": mode, "iteration": iteration,
        "external_host": options.external_host if lane == "external" else "ND",
        "external_url": options.external_url if lane == "external" else "ND",
        "network": options.network if lane == "external" else lane,
        "host_sample_interval_seconds": NET1_HOST_SAMPLE_SECONDS,
        "credentials": "none",
    })
    case = copy.deepcopy(catalog_case)
    case["interaction"] = build_interaction(
        mode, lane, options.external_host, options.external_url)
    arguments = _arguments(options, "smoke", lane, mode)
    session = qemu.QemuSession(arguments, artifact_dir)
    host_samples: list[dict[str, Any]] = []
    last_sample = [float("-inf")]
    process_pid: int | None = None
    event: dict[str, Any] | None = None
    error: str | None = None
    blocked = False
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
            session, NET1_GUEST_CASE, 0,
            (options.seed + iteration) & UINT32_MASK,
            qemu.case_timeout(case, options.case_timeout),
            qemu.heartbeat_timeout(case, options.heartbeat_timeout), case)
    except qemu.RunnerError as failure:
        error = str(failure)
        blocked = failure.blocked
    except (OSError, ValueError) as failure:
        error = str(failure)
    finally:
        _sample_host(process_pid, host_samples, last_sample, True)
        exit_code = session.stop()
    wall_time = round(time.monotonic() - started, 6)
    try:
        records = parse_machine_file(artifact_dir / "serial.log")
    except MetricsError as failure:
        records = []
        if not error:
            error = str(failure)
    serial_text = (artifact_dir / "serial.log").read_text(
        encoding="utf-8", errors="replace")
    status, validation_error, observations = validate_run(
        lane, records, session.input_trace, event, session.protocol_errors,
        serial_text, error, blocked)
    aggregate = aggregate_records(records) if records else {
        "schema": "zephyros-perf1-metrics-aggregate-v1",
        "record_count": 0, "metrics": {},
    }
    result = {
        "schema": NET1_SCHEMA, "version": 1, "status": status,
        "error": validation_error, "run_id": run_id, "lane": lane,
        "profile": lane, "mode": mode, "iteration": iteration,
        "image": {"path": relative_path(Path(options.image)),
                  "size_bytes": Path(options.image).stat().st_size
                  if Path(options.image).is_file() else "ND",
                  "sha256": qemu.sha256_file(Path(options.image))
                  if Path(options.image).is_file() else "ND"},
        "guest": {"samples": records, "aggregate": aggregate,
                  "result": event or {"status": "ND"}},
        "observations": observations,
        "phases": [item.get("phase") for item in session.input_trace
                    if item.get("op") == "phase"],
        "events": session.input_trace,
        "host": summarize_process_samples(host_samples, wall_time),
        "host_samples": host_samples,
        "qmp_status": session.qmp_status or "ND",
        "qemu_exit_code": exit_code,
        "artifacts": {"manifest": "manifest.json", "serial": "serial.log",
                       "input": "input.log", "qmp_events": "qmp-events.log"},
    }
    qemu.write_json(artifact_dir / "net1.json", result)
    return result


def _case(catalog_path: Path) -> dict[str, Any]:
    catalog = qemu.load_catalog(catalog_path)
    case = qemu.select_case(catalog, NET1_CASE)
    qemu.validate_case_for_runner(case)
    return case


def run(options: argparse.Namespace) -> int:
    options.external_host = validate_external_host(options.external_host)
    options.external_url = validate_external_url(options.external_url)
    options.workers = validate_workers(options.workers)
    image = qemu.resolve_path(options.image, qemu.DEFAULT_IMAGE)
    catalog = qemu.resolve_path(options.catalog, qemu.DEFAULT_CATALOG)
    results = qemu.resolve_path(
        options.results, Path("build/test-results/net1-external-connectivity"))
    report = qemu.resolve_path(
        options.report, results / "net1-external-connectivity.json")
    options.image, options.catalog, options.results = image, catalog, results
    catalog_case = _case(catalog)
    runs: list[dict[str, Any]] = []
    futures = {}
    with ThreadPoolExecutor(max_workers=options.workers) as executor:
        for item in matrix_plan():
            future = executor.submit(_run_session, options, catalog_case,
                                     item["profile"], item["mode"],
                                     item["iteration"])
            futures[future] = item
        for future in as_completed(futures):
            runs.append(future.result())
    runs.sort(key=lambda item: (item["profile"], item["mode"], item["iteration"]))
    matrix = build_report(Path(image), runs, options.external_host,
                          options.external_url, options.network,
                          options.workers)
    status = matrix["status"]
    write_report(report, matrix)
    print(f"NET1 external connectivity: {status}")
    print(f"Relatorio: {report}")
    return 0 if status == "PASS" else 2 if status == "BLOCKED" else 1


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--image")
    command_parser.add_argument("--catalog")
    command_parser.add_argument("--results")
    command_parser.add_argument("--report")
    command_parser.add_argument("--qemu")
    command_parser.add_argument("--qemu-arg", action="append")
    command_parser.add_argument("--cpu", default="max")
    command_parser.add_argument("--network", default=os.environ.get(
        "NET1_QEMU_NETWORK", NET1_QEMU_NETWORK))
    command_parser.add_argument("--external-host", default=os.environ.get(
        "NET1_EXTERNAL_HOST", NET1_EXTERNAL_HOST))
    command_parser.add_argument("--external-url", default=os.environ.get(
        "NET1_EXTERNAL_URL", NET1_EXTERNAL_URL))
    command_parser.add_argument("--workers", type=int, default=int(os.environ.get(
        "NET1_QEMU_WORKERS", "4")))
    command_parser.add_argument("--no-snapshot", dest="snapshot",
                                action="store_false")
    command_parser.set_defaults(snapshot=True)
    command_parser.add_argument("--boot-timeout", type=float,
                                default=qemu.BOOT_TIMEOUT_DEFAULT)
    command_parser.add_argument("--case-timeout", type=float,
                                default=180.0)
    command_parser.add_argument("--suite-timeout", type=float,
                                default=qemu.SUITE_TIMEOUT_DEFAULT)
    command_parser.add_argument("--heartbeat-timeout", type=float,
                                default=20.0)
    command_parser.add_argument("--seed", type=int, default=1)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    try:
        return run(options)
    except (Net1Error, qemu.RunnerError, MetricsError, OSError, ValueError) as error:
        print(f"ERRO NET1: {error}")
        return 2 if isinstance(error, Net1Error) and error.blocked else 1


if __name__ == "__main__":
    raise SystemExit(main())
