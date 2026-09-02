#!/usr/bin/env python3
"""Executa e compara a regressao continua do ZephyrOS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
CATALOG_PATH = ROOT / "tests" / "catalog.json"
BASELINE_PATH = ROOT / "tests" / "baselines" / "tst7-approved.json"
REGRESSION_MANIFEST_PATH = ROOT / "tests" / "regressions" / "manifest.json"
RESULTS_ROOT = ROOT / ".tst7-results"
QUICK_COMMANDS = (
    ("test-core-host", "core-host"),
    ("test-network-host", "network-host"),
    ("test-network-manager-host", "network-manager-host"),
    ("test-route-host", "route-host"),
    ("test-ipv4-host", "ipv4-host"),
    ("test-crypto-host", "crypto-host"),
    ("test-scheduling-host", "scheduling-host"),
    ("test-package-host", "package-host"),
    ("test-state-host", "state-host"),
    ("test-device-manager-host", "device-manager-host"),
    ("test-app-api-host", "app-api-host"),
    ("test-app-catalog-host", "app-catalog-host"),
    ("test-input-host", "input-host"),
    ("test-power-host", "power-host"),
    ("test-vfs-path-host", "vfs-path-host"),
    ("test-file-index-host", "file-index-host"),
    ("test-fs-host", "fs-host"),
    ("test-storage-host", "storage-host"),
    ("test-block-host", "block-host"),
    ("test-fat12-host", "fat12-host"),
    ("test-fat32-host", "fat32-host"),
    ("test-vfs-host", "vfs-host"),
    ("test-slab-host", "slab-host"),
    ("test-timer-host", "timer-host"),
    ("test-udp-host", "udp-host"),
    ("test-arp-host", "arp-host"),
    ("test-icmp-host", "icmp-host"),
    ("test-dns-host", "dns-host"),
    ("test-dhcp-host", "dhcp-host"),
    ("test-ethernet-host", "ethernet-host"),
    ("test-qemu-selftest", "qemu-selftest"),
    ("test-tst2-host", "tst2-host"),
    ("test-tst3-host", "tst3-host"),
    ("test-tst3-sanitize", "tst3-sanitize"),
    ("test-tst5-host", "tst5-host"),
    ("test-tst6-host", "tst6-host"),
    ("q3check", "q3check"),
)
HOST_CASE_TARGETS = {
    "host:core:contracts": "test-core-host",
    "host:core:net-buffer": "test-network-host",
    "host:core:network-manager": "test-network-manager-host",
    "host:network:route": "test-route-host",
    "host:network:ipv4": "test-ipv4-host",
    "host:core:crypto": "test-crypto-host",
    "host:core:scheduling": "test-scheduling-host",
    "host:core:app-package": "test-package-host",
    "host:core:state": "test-state-host",
    "host:core:device-manager": "test-device-manager-host",
    "host:core:app-api": "test-app-api-host",
    "host:core:app-catalog": "test-app-catalog-host",
    "host:core:input": "test-input-host",
    "host:core:power": "test-power-host",
    "host:storage:vfs-path": "test-vfs-path-host",
    "host:storage:file-index": "test-file-index-host",
    "host:storage:fs": "test-fs-host",
    "host:storage:storage": "test-storage-host",
    "host:storage:block": "test-block-host",
    "host:storage:fat12": "test-fat12-host",
    "host:storage:fat32": "test-fat32-host",
    "host:storage:vfs": "test-vfs-host",
    "host:memory:slab-metadata": "test-slab-host",
    "host:core:timer": "test-timer-host",
    "host:network:udp": "test-udp-host",
    "host:network:arp": "test-arp-host",
    "host:network:icmp": "test-icmp-host",
    "host:network:dns": "test-dns-host",
    "host:network:dhcp": "test-dhcp-host",
    "host:network:ethernet": "test-ethernet-host",
    "host:tst2:protocol-core": "test-tst2-host",
    "host:tst3:string-compress": "test-tst3-host",
}
QEMU_FIXTURE_ALLOWLIST = {"readonly", "readonly-update"}
SCHEMA = "zephyros-tst7-result-v1"
BASELINE_SCHEMA = "zephyros-tst7-baseline-v1"
REGRESSION_SCHEMA = "zephyros-tst7-regressions-v1"
MAX_CASE_TIMEOUT = 600.0
MAX_SUITE_TIMEOUT = 7200.0
DEFAULT_COMMAND_TIMEOUT = 300.0
DEFAULT_QUICK_TIMEOUT = 1800.0
DEFAULT_FULL_TIMEOUT = 7200.0
QEMU_CASE_SETTLE_SECONDS = 1.0
MAX_OUTPUT_BYTES = 4 * 1024 * 1024
VOLATILE_WARNING_PATTERNS = (
    (re.compile(r"tst7-[A-Za-z0-9_.:-]+"), "<run>"),
    (re.compile(r"qemu-[0-9TZ-]+"), "<qemu-run>"),
    (re.compile(r"0x[0-9A-Fa-f]+"), "<hex>"),
    (re.compile(r"\b[0-9]+(?:\.[0-9]+)?\b"), "<number>"),
)


class Tst7Error(Exception):
    """Erro de configuracao ou de infraestrutura da TST7."""

    def __init__(self, cause: str, blocked: bool = False):
        super().__init__(cause)
        self.cause = cause
        self.blocked = blocked


def utc_run_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"tst7-{stamp}-{os.getpid()}"


def safe_identifier(value: str) -> bool:
    return bool(value) and len(value) < 64 and bool(
        re.fullmatch(r"[A-Za-z0-9_.:-]+", value))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                return digest.hexdigest()
            digest.update(block)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def write_json_atomic(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    write_json(temporary, value)
    os.replace(temporary, path)


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Tst7Error(f"json_invalido:{path}:{error}", True) from error


def output_text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    return value.decode(errors="replace") if isinstance(value, bytes) else value


def result_status(returncode: int | None, missing: bool = False,
                  timed_out: bool = False, blocked: bool = False) -> str:
    if timed_out:
        return "TIMEOUT"
    if missing or blocked:
        return "BLOCKED"
    return "PASS" if returncode == 0 else "FAIL"


def aggregate_status(statuses: list[str]) -> str:
    if any(status in {"FAIL", "TIMEOUT"} for status in statuses):
        return "FAIL"
    if any(status == "BLOCKED" for status in statuses):
        return "BLOCKED"
    return "PASS"


def run_command(command: list[str], label: str, timeout: float) -> dict[str, Any]:
    started = time.monotonic()
    stdout = ""
    stderr = ""
    returncode: int | None = None
    missing = False
    timed_out = False
    try:
        process = subprocess.Popen(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
        )
        try:
            raw_stdout, raw_stderr = process.communicate(timeout=timeout)
            stdout = output_text(raw_stdout)
            stderr = output_text(raw_stderr)
            returncode = process.returncode
        except subprocess.TimeoutExpired as error:
            timed_out = True
            stdout = output_text(error.stdout)
            stderr = output_text(error.stderr)
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               check=False)
            else:
                process.kill()
            tail_stdout, tail_stderr = process.communicate()
            stdout += output_text(tail_stdout)
            stderr += output_text(tail_stderr)
    except FileNotFoundError as error:
        missing = True
        stderr = str(error)
    except OSError as error:
        stderr = str(error)
    blocked = returncode == 2 and "BLOCKED" in f"{stdout}\n{stderr}".upper()
    status = result_status(returncode, missing, timed_out, blocked)
    cause = "dependencia_bloqueada" if status == "BLOCKED" and (blocked or missing) else (
        "dependencia_ausente" if missing else ("timeout" if timed_out else None))
    return {
        "label": label,
        "command": command,
        "status": status,
        "returncode": returncode,
        "duration_seconds": round(time.monotonic() - started, 6),
        "timeout_seconds": timeout,
        "stdout": stdout[-MAX_OUTPUT_BYTES:],
        "stderr": stderr[-MAX_OUTPUT_BYTES:],
        "attempts": 1,
        "cause": cause,
    }


def load_catalog(path: Path) -> dict[str, Any]:
    catalog = read_json(path)
    if not isinstance(catalog, dict) or \
            catalog.get("schema") != "zephyros-test-catalog-v1":
        raise Tst7Error("schema_catalogo_invalido", True)
    if not isinstance(catalog.get("cases"), list) or \
            not isinstance(catalog.get("surfaces"), list):
        raise Tst7Error("catalogo_incompleto", True)
    return catalog


def validate_catalog_for_regression(catalog: dict[str, Any],
                                    strict_coverage: bool = False) -> list[str]:
    try:
        from tools import test_catalog
        errors = test_catalog.validate_catalog(catalog, ROOT,
                                               strict=strict_coverage)
        if strict_coverage:
            registry_path = ROOT / "tests" / "coverage" / "registry.json"
            if not registry_path.is_file():
                errors.append(f"registro de cobertura ausente: {registry_path}")
            else:
                registry = test_catalog.load_json(registry_path)
                errors.extend(test_catalog.validate_coverage_registry(
                    registry, catalog, strict=True))
    except (ImportError, AttributeError, OSError) as error:
        return [f"validador_catalogo_indisponivel:{error}"]
    for case in catalog.get("cases", []):
        if not isinstance(case, dict) or case.get("status") != "AUTOMATED":
            continue
        case_id = str(case.get("id", "<sem-id>"))
        executor = case.get("executor")
        if executor == "host" and case_id not in HOST_CASE_TARGETS:
            errors.append(f"executor_host_ausente:{case_id}")
        elif executor not in {"host", "qemu"}:
            errors.append(f"executor_tst7_ausente:{case_id}:{executor}")
        if executor == "qemu":
            parameters = case.get("parameters")
            fixture = parameters.get("fixture") if isinstance(parameters, dict) else None
            if fixture is not None and fixture not in QEMU_FIXTURE_ALLOWLIST:
                errors.append(f"fixture_qemu_ausente:{case_id}:{fixture}")
    return list(errors)


def qemu_cases(catalog: dict[str, Any]) -> list[dict[str, Any]]:
    return sorted(
        [case for case in catalog["cases"]
         if isinstance(case, dict) and case.get("status") == "AUTOMATED"
         and case.get("executor") == "qemu"],
        key=lambda case: str(case.get("id", "")),
    )


def soak_cases(catalog: dict[str, Any]) -> list[dict[str, Any]]:
    return [case for case in qemu_cases(catalog)
            if str(case.get("id", "")).startswith("qemu:tst6:stress:")]


def host_cases(catalog: dict[str, Any]) -> list[dict[str, Any]]:
    return sorted(
        [case for case in catalog["cases"]
         if isinstance(case, dict) and case.get("status") == "AUTOMATED"
         and case.get("executor") == "host"],
        key=lambda case: str(case.get("id", "")),
    )


def validate_regression_manifest(path: Path, catalog: dict[str, Any]) -> list[str]:
    if not path.is_file():
        return [f"manifesto_regressoes_ausente:{path}"]
    try:
        manifest = read_json(path)
    except Tst7Error as error:
        return [error.cause]
    errors: list[str] = []
    if not isinstance(manifest, dict) or manifest.get("schema") != REGRESSION_SCHEMA:
        return ["schema_manifesto_regressoes_invalido"]
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        return ["entries_manifesto_regressoes_invalido"]
    known = {str(case.get("id")) for case in qemu_cases(catalog)}
    known.update(str(case.get("id")) for case in catalog["cases"]
                 if isinstance(case, dict) and case.get("status") == "AUTOMATED")
    identifiers: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("entrada_regressao_invalida")
            continue
        identifier = entry.get("id")
        case_id = entry.get("case_id")
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            errors.append(f"id_regressao_invalido:{identifier}")
        identifiers.add(str(identifier))
        if not isinstance(case_id, str) or case_id not in known:
            errors.append(f"caso_regressao_inexistente:{case_id}")
        for field in ("condition", "origin"):
            if not isinstance(entry.get(field), str) or not entry[field].strip():
                errors.append(f"{field}_regressao_ausente:{identifier}")
    return errors


def catalog_coverage(catalog: dict[str, Any], catalog_hash: str) -> dict[str, Any]:
    surfaces = [item for item in catalog["surfaces"] if isinstance(item, dict)]
    cases = [item for item in catalog["cases"] if isinstance(item, dict)]
    counts: dict[str, int] = {}
    status_by_id: dict[str, str] = {}
    for surface in surfaces:
        status = str(surface.get("status", "PENDING"))
        counts[status] = counts.get(status, 0) + 1
        if isinstance(surface.get("id"), str):
            status_by_id[surface["id"]] = status
    automated = sorted(str(case["id"]) for case in cases
                       if case.get("status") == "AUTOMATED")
    return {
        "schema": "zephyros-tst7-coverage-v1",
        "catalog_sha256": catalog_hash,
        "surface_counts": dict(sorted(counts.items())),
        "surface_statuses": dict(sorted(status_by_id.items())),
        "covered_surface_ids": sorted(
            identifier for identifier, status in status_by_id.items()
            if status == "COVERED"),
        "pending_surface_ids": sorted(
            identifier for identifier, status in status_by_id.items()
            if status == "PENDING"),
        "automated_case_ids": automated,
    }


def git_revision() -> str | None:
    try:
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
            text=True, timeout=5, check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def environment_identity(make_command: str, qemu_command: str) -> dict[str, Any]:
    make_path = shutil.which(make_command) or make_command
    qemu_path = shutil.which(qemu_command) or qemu_command
    return {
        "os": platform.system(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "make": str(make_path),
        "qemu": str(qemu_path),
        "host_cc": os.environ.get("HOST_CC", "cc"),
        "host_sanitize_cc": os.environ.get("HOST_SANITIZE_CC", "clang"),
    }


def stable_seed(case_id: str) -> int:
    digest = hashlib.sha256(f"zephyros-tst7:{case_id}:v1".encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)[:64]


def normalize_warning(line: str) -> str:
    normalized = line.strip().lower()
    for pattern, replacement in VOLATILE_WARNING_PATTERNS:
        normalized = pattern.sub(replacement, normalized)
    return re.sub(r"\s+", " ", normalized)


def warning_fingerprints(run_dir: Path, steps: list[dict[str, Any]],
                         cases: list[dict[str, Any]]) -> dict[str, int]:
    fingerprints: dict[str, int] = {}

    def consume(source: str, text: str) -> None:
        for line in text.splitlines():
            if re.search(r"\bwarn(?:ing)?\b", line, re.IGNORECASE):
                key = f"{source}:{normalize_warning(line)}"
                fingerprints[key] = fingerprints.get(key, 0) + 1

    for step in steps:
        consume(str(step.get("label", "step")), str(step.get("stdout", "")))
        consume(str(step.get("label", "step")), str(step.get("stderr", "")))
    for case in cases:
        case_id = str(case.get("id", case.get("catalog_case", "case")))
        for file_name in ("serial.log", "qemu.stdout.log", "qemu.stderr.log"):
            path = run_dir / str(case.get("artifact_dir", "")) / file_name
            if path.is_file():
                try:
                    consume(case_id, path.read_text(encoding="utf-8", errors="replace"))
                except OSError:
                    continue
    return dict(sorted(fingerprints.items()))


def qemu_network(case: dict[str, Any]) -> str:
    identifier = str(case.get("id", ""))
    parameters = case.get("parameters")
    if not isinstance(parameters, dict):
        parameters = {}
    declared = parameters.get("network")
    if declared == "none":
        return "none"
    if case.get("qemu_profile") == "network" or \
            declared in {"isolated", "user-isolated"} or \
            identifier.startswith("qemu:tst6:") or \
            identifier == "qemu:tst4:network":
        return "user,model=e1000,restrict=on"
    return "none"


def qemu_iterations(case: dict[str, Any]) -> int:
    parameters = case.get("parameters")
    if isinstance(parameters, dict) and isinstance(parameters.get("iterations"), int):
        value = parameters["iterations"]
        if 0 < value <= 1000:
            return value
    return 1


def qemu_command(case: dict[str, Any], arguments: argparse.Namespace,
                 run_dir: Path, index: int) -> tuple[list[str], Path, int]:
    case_id = str(case["id"])
    profile = str(case.get("qemu_profile") or "baseline")
    iterations = qemu_iterations(case)
    seed = stable_seed(case_id)
    qemu_run_id = f"tst7-{index:02d}-{slug(case_id)}"[:47]
    artifact_root = run_dir / "qemu"
    artifact_root.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable, str(ROOT / "tools" / "qemu_test_runner.py"), "stress",
        "--case", case_id, "--iterations", str(iterations), "--seed", str(seed),
        "--run-id", qemu_run_id, "--image", str(arguments.image),
        "--catalog", str(arguments.catalog), "--results", str(artifact_root),
        "--qemu", arguments.qemu, "--cpu", arguments.cpu,
        "--qemu-profile", profile, "--network", qemu_network(case),
        "--boot-timeout", str(arguments.boot_timeout),
        "--case-timeout", str(case.get("timeout_seconds", arguments.case_timeout)),
        "--heartbeat-timeout", str(case.get(
            "heartbeat_timeout_seconds", arguments.heartbeat_timeout)),
        "--suite-timeout", str(min(
            MAX_CASE_TIMEOUT, arguments.boot_timeout +
            float(case.get("timeout_seconds", arguments.case_timeout)) * iterations + 30)),
    ]
    parameters = case.get("parameters")
    if isinstance(parameters, dict) and isinstance(parameters.get("fixture"), str):
        fixture = parameters["fixture"]
        if fixture not in QEMU_FIXTURE_ALLOWLIST:
            raise Tst7Error(f"fixture_qemu_invalida:{case_id}:{fixture}")
        command.extend(["--fixture", fixture])
    if profile == "usb-storage":
        command.extend(["--storage-image", str(arguments.storage_image)])
    timeout = min(
        MAX_CASE_TIMEOUT,
        arguments.boot_timeout + float(case.get(
            "timeout_seconds", arguments.case_timeout)) * iterations + 45,
    )
    return command, artifact_root / qemu_run_id, timeout


def contract_entries(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    entries: dict[str, dict[str, Any]] = {}
    for step in report.get("steps", []):
        label = str(step.get("label", "step"))
        entries[f"host:{label}"] = {
            "status": step.get("status"),
            "termination": "timeout" if step.get("status") == "TIMEOUT" else "completed",
            "phase": label,
            "first_error": step.get("cause"),
            "events": [],
            "duration_seconds": step.get("duration_seconds"),
        }
    for case in report.get("cases", []):
        identifier = str(case.get("id", case.get("catalog_case", "case")))
        entries[f"qemu:{identifier}"] = {
            "status": case.get("status"),
            "termination": case.get("termination", "completed"),
            "phase": case.get("last_state"),
            "first_error": case.get("cause"),
            "events": case.get("events", []),
            "duration_seconds": case.get("duration_seconds"),
            "seed": case.get("seed"),
        }
    return dict(sorted(entries.items()))


def compare_contract(current: dict[str, Any], baseline: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    current_entries = current.get("contract", {}).get("entries", {})
    baseline_entries = baseline.get("contract", {}).get("entries", {})
    if not isinstance(current_entries, dict) or not isinstance(baseline_entries, dict):
        return ["contrato_ausente"]
    identifiers = baseline_entries.keys()
    if current.get("mode") in {"quick", "soak"}:
        identifiers = (identifier for identifier in current_entries
                       if identifier in baseline_entries)
    for identifier in identifiers:
        expected = baseline_entries[identifier]
        actual = current_entries.get(identifier)
        if actual is None:
            errors.append(f"caso_ausente:{identifier}")
            continue
        expected_status = expected.get("status")
        actual_status = actual.get("status")
        if expected_status == "PASS" and actual_status in {"FAIL", "TIMEOUT"}:
            errors.append(f"pass_para_falha:{identifier}:{actual_status}")
        elif expected_status != actual_status:
            errors.append(f"status_alterado:{identifier}:{expected_status}->{actual_status}")
    if current.get("mode") not in {"quick", "soak"}:
        for identifier in current_entries:
            if identifier not in baseline_entries:
                errors.append(f"caso_novo_sem_baseline:{identifier}")
    return errors


def compare_warnings(current: dict[str, int], baseline: dict[str, int]) -> list[str]:
    errors: list[str] = []
    for fingerprint, count in current.items():
        old_count = baseline.get(fingerprint, 0)
        if count > old_count:
            errors.append(f"warning_nova_ou_aumentada:{fingerprint}:{old_count}->{count}")
    return errors


def compare_durations(current: dict[str, Any], baseline: dict[str, Any],
                      comparable: bool) -> tuple[list[str], list[str]]:
    if not comparable:
        return [], ["NOT_COMPARABLE:environment"]
    errors: list[str] = []
    for identifier, expected in baseline.get("contract", {}).get("entries", {}).items():
        if not identifier.startswith("qemu:"):
            continue
        actual = current.get("contract", {}).get("entries", {}).get(identifier)
        if not isinstance(actual, dict) or not isinstance(expected, dict):
            continue
        old = expected.get("duration_seconds")
        new = actual.get("duration_seconds")
        if not isinstance(old, (int, float)) or not isinstance(new, (int, float)):
            continue
        if new > old * 1.2 and new - old > 5:
            errors.append(f"duracao_regressao:{identifier}:{old}->{new}")
    return errors, []


def compare_coverage(current: dict[str, Any], baseline: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    current_statuses = current.get("coverage", {}).get("surface_statuses", {})
    baseline_covered = set(baseline.get("coverage", {}).get(
        "covered_surface_ids", []))
    if not isinstance(current_statuses, dict):
        return ["cobertura_atual_ausente"]
    for identifier in sorted(baseline_covered):
        if current_statuses.get(identifier) != "COVERED":
            errors.append(f"cobertura_perdida:{identifier}")
    baseline_cases = set(baseline.get("coverage", {}).get("automated_case_ids", []))
    current_cases = set(current.get("coverage", {}).get("automated_case_ids", []))
    for identifier in sorted(baseline_cases - current_cases):
        errors.append(f"caso_automatizado_perdido:{identifier}")
    for error in current.get("catalog_errors", []):
        errors.append(f"catalogo_inconsistente:{error}")
    return errors


def compare_runs(current: dict[str, Any], baseline: dict[str, Any] | None) -> dict[str, Any]:
    if baseline is None:
        return {
            "status": "BLOCKED",
            "cause": "baseline_ausente",
            "reasons": ["baseline_ausente"],
            "duration": {"status": "NOT_COMPARABLE"},
            "warnings": {"status": "NOT_COMPARABLE"},
            "coverage": {"status": "NOT_COMPARABLE"},
        }
    contract_errors = compare_contract(current, baseline)
    warning_errors = compare_warnings(
        current.get("warnings", {}), baseline.get("warnings", {}))
    coverage_errors = compare_coverage(current, baseline)
    environment_same = current.get("environment") == baseline.get("environment")
    duration_errors, duration_notes = compare_durations(
        current, baseline, environment_same)
    reasons = contract_errors + warning_errors + coverage_errors + duration_errors
    current_status = current.get("execution_status", current.get("status"))
    has_real_failure = current_status in {"FAIL", "TIMEOUT"} or any(
        isinstance(entry, dict) and
        entry.get("status") in {"FAIL", "TIMEOUT"}
        for entry in current.get("contract", {}).get("entries", {}).values()
    )
    non_blocked_reasons = [reason for reason in reasons if not (
        reason.startswith("status_alterado:") and "->BLOCKED" in reason)]
    if has_real_failure or non_blocked_reasons:
        status = "FAIL"
    elif current_status == "BLOCKED":
        status = "BLOCKED"
    elif reasons:
        status = "FAIL"
    else:
        status = "PASS"
    return {
        "status": status,
        "cause": reasons[0] if reasons else (
            "execucao_bloqueada" if status == "BLOCKED" else None),
        "reasons": reasons,
        "duration": {"status": "REGRESSION" if duration_errors else (
            duration_notes[0] if duration_notes else "PASS"),
                      "errors": duration_errors, "notes": duration_notes},
        "warnings": {"status": "FAIL" if warning_errors else "PASS",
                      "errors": warning_errors},
        "coverage": {"status": "FAIL" if coverage_errors else "PASS",
                      "errors": coverage_errors},
        "contract": {"status": "FAIL" if contract_errors else "PASS",
                      "errors": contract_errors},
    }


def artifact_index(run_dir: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for path in sorted(run_dir.rglob("*")):
        if not path.is_file() or path.name == "artifact-index.json":
            continue
        try:
            relative = path.relative_to(run_dir).as_posix()
            entries.append({"path": relative, "bytes": path.stat().st_size,
                            "sha256": sha256_file(path)})
        except OSError:
            continue
    return entries


def write_summary(run_dir: Path, report: dict[str, Any]) -> None:
    lines = [
        "# TST7 regression run",
        "",
        f"- Run: `{report.get('run_id')}`",
        f"- Mode: `{report.get('mode')}`",
        f"- Status: **{report.get('status')}**",
        f"- Execution: `{report.get('execution_status')}`",
        f"- Comparison: `{report.get('comparison', {}).get('status')}`",
        f"- Cases: {len(report.get('cases', []))}",
        "",
        "## Cases",
        "",
        "| Caso | Status | Seed | Duracao | Artefatos |",
        "|---|---|---:|---:|---|",
    ]
    for case in report.get("cases", []):
        lines.append("| `{}` | `{}` | {} | {} | `{}` |".format(
            case.get("id", case.get("catalog_case", "")),
            case.get("status"), case.get("seed", ""),
            case.get("duration_seconds", ""), case.get("artifact_dir", "")))
    lines.extend(["", "## Limitacoes", ""])
    limitations = report.get("limitations", [])
    lines.extend(f"- {item}" for item in limitations) if limitations else lines.append("- Nenhuma registrada.")
    (run_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def base_report(mode: str, run_id: str, arguments: argparse.Namespace,
                catalog_hash: str, coverage: dict[str, Any],
                catalog_errors: list[str]) -> dict[str, Any]:
    image = Path(arguments.image)
    return {
        "schema": SCHEMA,
        "run_id": run_id,
        "mode": mode,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "status": "BLOCKED",
        "execution_status": "BLOCKED",
        "cause": None,
        "catalog": str(arguments.catalog),
        "catalog_sha256": catalog_hash,
        "image": str(image),
        "image_sha256": sha256_file(image) if image.is_file() else None,
        "git_revision": git_revision(),
        "environment": environment_identity(arguments.make, arguments.qemu),
        "coverage": coverage,
        "catalog_errors": catalog_errors,
        "steps": [],
        "cases": [],
        "warnings": {},
        "contract": {"entries": {}},
        "limitations": [],
        "comparison": {},
        "limits": {
            "command_timeout_seconds": arguments.command_timeout,
            "suite_timeout_seconds": arguments.suite_timeout,
            "max_case_timeout_seconds": MAX_CASE_TIMEOUT,
            "max_suite_timeout_seconds": MAX_SUITE_TIMEOUT,
        },
    }


def persist_report(run_dir: Path, report: dict[str, Any]) -> None:
    report["contract"] = {"entries": contract_entries(report)}
    report["warnings"] = warning_fingerprints(
        run_dir, report.get("steps", []), report.get("cases", []))
    write_json(run_dir / "result.json", report)
    write_json(run_dir / "coverage.json", report.get("coverage", {}))
    write_summary(run_dir, report)
    write_json(run_dir / "artifact-index.json", artifact_index(run_dir))


def append_output(run_dir: Path, step: dict[str, Any]) -> None:
    with (run_dir / "stdout.log").open("a", encoding="utf-8") as output:
        output.write(f"\n===== {step.get('label')} =====\n")
        output.write(str(step.get("stdout", "")))
    with (run_dir / "stderr.log").open("a", encoding="utf-8") as output:
        output.write(f"\n===== {step.get('label')} =====\n")
        output.write(str(step.get("stderr", "")))


def add_internal_step(report: dict[str, Any], label: str, status: str,
                      cause: str | None = None) -> None:
    report["steps"].append({
        "label": label, "command": [], "status": status, "returncode": None,
        "duration_seconds": 0, "timeout_seconds": 0, "stdout": "", "stderr": "",
        "attempts": 1, "cause": cause,
    })


def suite_remaining(started: float, limit: float) -> float:
    return max(0.0, limit - (time.monotonic() - started))


def execute_make(report: dict[str, Any], run_dir: Path, arguments: argparse.Namespace,
                 target: str | None, label: str, started: float) -> bool:
    remaining = suite_remaining(started, arguments.suite_timeout)
    if remaining <= 0:
        add_internal_step(report, f"{label}-suite-timeout", "TIMEOUT",
                          "suite_timeout")
        persist_report(run_dir, report)
        return False
    command = [arguments.make]
    if target:
        command.append(target)
    step = run_command(command, label,
                       min(arguments.command_timeout, remaining))
    report["steps"].append(step)
    append_output(run_dir, step)
    persist_report(run_dir, report)
    print(f"TST7 {label}: {step['status']}")
    return True


def execute_qemu_case(report: dict[str, Any], run_dir: Path,
                      arguments: argparse.Namespace, case: dict[str, Any],
                      index: int, started: float) -> bool:
    label = f"qemu:{case['id']}"
    try:
        command, artifact_dir, timeout = qemu_command(
            case, arguments, run_dir, index)
    except Tst7Error as error:
        add_internal_step(report, label, "FAIL", error.cause)
        persist_report(run_dir, report)
        return True
    remaining = suite_remaining(started, arguments.suite_timeout)
    if remaining <= 0:
        add_internal_step(report, f"qemu:{case['id']}-suite-timeout", "TIMEOUT",
                          "suite_timeout")
        persist_report(run_dir, report)
        return False
    step = run_command(command, label, min(timeout, remaining))
    result_path = artifact_dir / "result.json"
    qemu_result: dict[str, Any] = {}
    if result_path.is_file():
        try:
            loaded = read_json(result_path)
            if isinstance(loaded, dict):
                qemu_result = loaded
        except Tst7Error:
            qemu_result = {}
    status = qemu_result.get("status") if qemu_result else step["status"]
    if not qemu_result and step["status"] == "PASS":
        status = "BLOCKED"
        step["cause"] = "resultado_qemu_ausente"
    elif step["status"] in {"FAIL", "TIMEOUT", "BLOCKED"}:
        status = step["status"]
    elif status not in {"PASS", "FAIL", "BLOCKED", "TIMEOUT"}:
        status = step["status"]
    case_result = {
        "id": str(case["id"]),
        "status": status,
        "returncode": step.get("returncode"),
        "duration_seconds": qemu_result.get("duration_seconds",
                                            step.get("duration_seconds")),
        "seed": stable_seed(str(case["id"])),
        "iterations": qemu_iterations(case),
        "termination": qemu_result.get("termination", "timeout" if status == "TIMEOUT" else "completed"),
        "cause": qemu_result.get("cause", step.get("cause")),
        "last_state": qemu_result.get("last_state"),
        "events": [event.get("event") for event in qemu_result.get(
            "progress_history", []) if isinstance(event, dict) and event.get("event")],
        "artifact_dir": artifact_dir.relative_to(run_dir).as_posix()
        if artifact_dir.is_relative_to(run_dir) else str(artifact_dir),
        "command": command,
        "attempts": 1,
        "qemu_result": qemu_result,
    }
    report["steps"].append(step)
    report["cases"].append(case_result)
    append_output(run_dir, step)
    persist_report(run_dir, report)
    print(f"TST7 {label}: {status} seed={case_result['seed']} artifacts={case_result['artifact_dir']}")
    return True


def execute_host_case(report: dict[str, Any], run_dir: Path,
                      arguments: argparse.Namespace, case: dict[str, Any],
                      started: float) -> bool:
    case_id = str(case["id"])
    target = HOST_CASE_TARGETS.get(case_id)
    remaining = suite_remaining(started, arguments.suite_timeout)
    if remaining <= 0:
        add_internal_step(report, f"host:{case_id}-suite-timeout", "TIMEOUT",
                          "suite_timeout")
        persist_report(run_dir, report)
        return False
    label = f"host:{case_id}"
    if target is None:
        step = {
            "label": label,
            "command": [],
            "status": "FAIL",
            "returncode": None,
            "duration_seconds": 0,
            "timeout_seconds": 0,
            "stdout": "",
            "stderr": "",
            "attempts": 1,
            "cause": f"executor_host_ausente:{case_id}",
        }
    else:
        step = run_command(
            [arguments.make, target], label,
            min(float(case.get("timeout_seconds", arguments.command_timeout)),
                arguments.command_timeout, remaining),
        )
    artifact_dir = run_dir / "host" / slug(case_id)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    (artifact_dir / "stdout.log").write_text(
        str(step.get("stdout", "")), encoding="utf-8")
    (artifact_dir / "stderr.log").write_text(
        str(step.get("stderr", "")), encoding="utf-8")
    write_json(artifact_dir / "manifest.json", {
        "case_id": case_id,
        "executor": "host",
        "command": step.get("command", []),
        "timeout_seconds": step.get("timeout_seconds"),
    })
    case_result = {
        "id": case_id,
        "status": step.get("status"),
        "returncode": step.get("returncode"),
        "duration_seconds": step.get("duration_seconds"),
        "seed": None,
        "iterations": 1,
        "termination": "timeout" if step.get("status") == "TIMEOUT" else "completed",
        "cause": step.get("cause"),
        "last_state": "terminal" if step.get("status") in
        {"PASS", "FAIL", "BLOCKED"} else "timeout",
        "events": [],
        "artifact_dir": artifact_dir.relative_to(run_dir).as_posix(),
        "command": step.get("command", []),
        "attempts": 1,
    }
    report["steps"].append(step)
    report["cases"].append(case_result)
    append_output(run_dir, step)
    persist_report(run_dir, report)
    print(f"TST7 {label}: {case_result['status']} artifacts="
          f"{case_result['artifact_dir']}")
    return True


def run_execution(arguments: argparse.Namespace, mode: str) -> int:
    if arguments.suite_timeout <= 0 or arguments.suite_timeout > MAX_SUITE_TIMEOUT:
        print("TST7: BLOCKED suite_timeout_invalido", file=sys.stderr)
        return 2
    if arguments.command_timeout <= 0 or arguments.command_timeout > MAX_CASE_TIMEOUT:
        print("TST7: BLOCKED command_timeout_invalido", file=sys.stderr)
        return 2
    catalog_path = Path(arguments.catalog).resolve()
    image_path = Path(arguments.image).resolve()
    arguments.catalog = str(catalog_path)
    arguments.image = str(image_path)
    try:
        catalog = load_catalog(catalog_path)
        catalog_hash = sha256_file(catalog_path)
    except Tst7Error as error:
        print(f"TST7: BLOCKED {error.cause}", file=sys.stderr)
        return 2
    run_id = arguments.run_id or utc_run_id()
    if not safe_identifier(run_id):
        print("TST7: BLOCKED run_id_invalido", file=sys.stderr)
        return 2
    run_dir = RESULTS_ROOT / run_id
    if run_dir.exists():
        print(f"TST7: BLOCKED run_id_ja_existente:{run_id}", file=sys.stderr)
        return 2
    run_dir.mkdir(parents=True)
    (run_dir / "qemu").mkdir()
    (run_dir / "stdout.log").touch()
    (run_dir / "stderr.log").touch()
    catalog_errors = validate_catalog_for_regression(
        catalog, strict_coverage=arguments.strict_coverage)
    catalog_errors.extend(
        validate_regression_manifest(REGRESSION_MANIFEST_PATH, catalog))
    coverage = catalog_coverage(catalog, catalog_hash)
    report = base_report(mode, run_id, arguments, catalog_hash, coverage, catalog_errors)
    write_json(run_dir / "manifest.json", {
        "schema": "zephyros-tst7-manifest-v1",
        "run_id": run_id,
        "mode": mode,
        "catalog": str(catalog_path),
        "catalog_sha256": catalog_hash,
        "image": str(image_path),
        "image_sha256": sha256_file(image_path) if image_path.is_file() else None,
        "git_revision": report["git_revision"],
        "environment": report["environment"],
        "quick_commands": [target for target, _ in QUICK_COMMANDS],
        "automated_case_count": len(qemu_cases(catalog)) + len(host_cases(catalog)),
        "qemu_case_count": len(qemu_cases(catalog)),
        "host_case_count": len(host_cases(catalog)),
        "strict_coverage": arguments.strict_coverage,
        "limits": report["limits"],
    })
    started = time.monotonic()
    if catalog_errors:
        add_internal_step(report, "catalog-coverage", "FAIL",
                          catalog_errors[0])
        persist_report(run_dir, report)
        if arguments.strict_coverage:
            return finalize_execution(report, run_dir, started)
    if mode in {"full", "soak"}:
        if not execute_make(report, run_dir, arguments, "clean", "clean", started):
            return finalize_execution(report, run_dir, started)
        if not execute_make(report, run_dir, arguments, None, "build", started):
            return finalize_execution(report, run_dir, started)
    quick_targets = QUICK_COMMANDS
    if mode == "soak":
        soak_target_names = {"test-qemu-selftest", "test-tst6-host", "q3check"}
        quick_targets = tuple((target, label) for target, label in QUICK_COMMANDS
                              if target in soak_target_names)
    elif mode == "full":
        host_targets = set(HOST_CASE_TARGETS.values())
        quick_targets = tuple((target, label) for target, label in QUICK_COMMANDS
                              if target not in host_targets)
    for target, label in quick_targets:
        if not execute_make(report, run_dir, arguments, target, label, started):
            return finalize_execution(report, run_dir, started)
    if mode in {"full", "soak"}:
        selected_host_cases = host_cases(catalog)
        if mode == "full":
            for case in selected_host_cases:
                if not execute_host_case(report, run_dir, arguments, case, started):
                    break
        if not execute_make(report, run_dir, arguments, "catalog-test", "catalog-test", started):
            return finalize_execution(report, run_dir, started)
        if any(case.get("qemu_profile") == "usb-storage" for case in qemu_cases(catalog)):
            if not execute_make(report, run_dir, arguments, "storage-fixtures", "storage-fixtures", started):
                return finalize_execution(report, run_dir, started)
        selected_qemu_cases = qemu_cases(catalog) if mode == "full" else soak_cases(catalog)
        for index, case in enumerate(selected_qemu_cases, start=1):
            if not execute_qemu_case(report, run_dir, arguments, case, index, started):
                break
            if index < len(selected_qemu_cases):
                time.sleep(QEMU_CASE_SETTLE_SECONDS)
    return finalize_execution(report, run_dir, started)


def finalize_execution(report: dict[str, Any], run_dir: Path,
                       started: float | None = None) -> int:
    if started is not None:
        report["duration_seconds"] = round(time.monotonic() - started, 6)
    report["execution_status"] = aggregate_status(
        [str(step.get("status")) for step in report["steps"]] +
        [str(case.get("status")) for case in report["cases"]])
    report["comparison"] = compare_runs(report, read_baseline(BASELINE_PATH))
    report["limitations"] = [
        f"superficies_pending:{len(report.get('coverage', {}).get('pending_surface_ids', []))}",
        "hardware_fisico_BLOCKED",
    ]
    if report["execution_status"] == "FAIL":
        report["status"] = "FAIL"
        report["cause"] = next((step.get("cause") for step in report["steps"]
                                if step.get("status") in {"FAIL", "TIMEOUT"}
                                and step.get("cause")), "falha_na_execucao")
    elif report["execution_status"] == "BLOCKED":
        report["status"] = "BLOCKED"
        report["cause"] = next((step.get("cause") for step in report["steps"]
                                if step.get("status") == "BLOCKED" and step.get("cause")),
                               "dependencia_bloqueada")
    else:
        report["status"] = report["comparison"].get("status", "BLOCKED")
        report["cause"] = report["comparison"].get("cause")
    persist_report(run_dir, report)
    print(f"TST7 {report.get('mode')}: {report['status']} run={report.get('run_id')}")
    print(f"Artefatos: {run_dir}")
    return 0 if report["status"] == "PASS" else 2 if report["status"] == "BLOCKED" else 1


def read_baseline(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        value = read_json(path)
    except Tst7Error:
        return None
    return value if isinstance(value, dict) and value.get("schema") == BASELINE_SCHEMA else None


def approve_run(run_id: str) -> int:
    if not safe_identifier(run_id):
        print("TST7 approve: BLOCKED run_id_invalido", file=sys.stderr)
        return 2
    run_dir = RESULTS_ROOT / run_id
    result_path = run_dir / "result.json"
    if not result_path.is_file():
        print("TST7 approve: BLOCKED resultado_ausente", file=sys.stderr)
        return 2
    try:
        report = read_json(result_path)
    except Tst7Error as error:
        print(f"TST7 approve: BLOCKED {error.cause}", file=sys.stderr)
        return 2
    if not isinstance(report, dict) or report.get("schema") != SCHEMA:
        print("TST7 approve: BLOCKED schema_resultado_invalido", file=sys.stderr)
        return 2
    reasons: list[str] = []
    if report.get("mode") != "full":
        reasons.append("baseline_requer_full")
    if report.get("execution_status") != "PASS":
        reasons.append(f"execucao_{report.get('execution_status')}")
    if any(case.get("status") != "PASS" for case in report.get("cases", [])):
        reasons.append("caso_nao_passou")
    if any(step.get("status") in {"FAIL", "TIMEOUT", "BLOCKED"}
           for step in report.get("steps", [])):
        reasons.append("etapa_nao_passou")
    if report.get("catalog_errors"):
        reasons.append("catalogo_ou_manifesto_invalido")
    comparison = report.get("comparison", {})
    if isinstance(comparison, dict) and comparison.get("status") == "FAIL":
        reasons.append("comparacao_reprovada")
    if reasons:
        print(f"TST7 approve: FAIL {';'.join(reasons)}", file=sys.stderr)
        return 1
    baseline = {
        "schema": BASELINE_SCHEMA,
        "version": 1,
        "approved_at": datetime.now(timezone.utc).isoformat(),
        "approved_run_id": run_id,
        "catalog_sha256": report.get("catalog_sha256"),
        "image": report.get("image"),
        "image_sha256": report.get("image_sha256"),
        "git_revision": report.get("git_revision"),
        "environment": report.get("environment"),
        "coverage": report.get("coverage", {}),
        "contract": report.get("contract", {}),
        "warnings": report.get("warnings", {}),
        "cases": report.get("cases", []),
        "limitations": report.get("limitations", []),
    }
    write_json_atomic(BASELINE_PATH, baseline)
    print(f"TST7 approve: PASS baseline={BASELINE_PATH}")
    return 0


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    subparsers = command_parser.add_subparsers(dest="command", required=True)
    for name in ("quick", "full", "soak"):
        subparser = subparsers.add_parser(name)
        subparser.add_argument("--run-id")
        subparser.add_argument("--make", default=os.environ.get("MAKE", "make"))
        subparser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
        subparser.add_argument("--cpu", default=os.environ.get("QEMU_TEST_CPU", "max"))
        subparser.add_argument("--image", default=str(ROOT / "build" / "zephyros.img"))
        subparser.add_argument("--storage-image", default=str(ROOT / "build" / "storage-valid.img"))
        subparser.add_argument("--catalog", default=str(CATALOG_PATH))
        subparser.add_argument("--command-timeout", type=float, default=DEFAULT_COMMAND_TIMEOUT)
        subparser.add_argument("--suite-timeout", type=float,
                               default=DEFAULT_QUICK_TIMEOUT if name == "quick" else DEFAULT_FULL_TIMEOUT)
        subparser.add_argument("--boot-timeout", type=float, default=60)
        subparser.add_argument("--case-timeout", type=float, default=120)
        subparser.add_argument("--heartbeat-timeout", type=float, default=60)
        subparser.add_argument("--strict-coverage", action="store_true")
    approve_parser = subparsers.add_parser("approve")
    approve_parser.add_argument("--run-id", required=True)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    if arguments.command == "approve":
        return approve_run(arguments.run_id)
    return run_execution(arguments, arguments.command)


if __name__ == "__main__":
    raise SystemExit(main())
