#!/usr/bin/env python3
"""Executa a primeira bateria host-only com evidência dinâmica de cobertura."""

from __future__ import annotations

import argparse
import json
import os
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

from tools import coverage_collector


RESULT_DIR = ROOT / "build" / "test-results" / "core-host"
DEFAULT_BINARY = ROOT / "build" / "tests" / "test_core_contracts_host.exe"
NETWORK_RESULT_DIR = ROOT / "build" / "test-results" / "network-host"
NETWORK_BINARY = ROOT / "build" / "tests" / "test_network_host.exe"
NETWORK_MANAGER_RESULT_DIR = ROOT / "build" / "test-results" / "network-manager-host"
NETWORK_MANAGER_BINARY = ROOT / "build" / "tests" / "test_network_manager_host.exe"
ROUTE_RESULT_DIR = ROOT / "build" / "test-results" / "route-host"
ROUTE_BINARY = ROOT / "build" / "tests" / "test_route_host.exe"
IPV4_RESULT_DIR = ROOT / "build" / "test-results" / "ipv4-host"
IPV4_BINARY = ROOT / "build" / "tests" / "test_ipv4_host.exe"
CRYPTO_RESULT_DIR = ROOT / "build" / "test-results" / "crypto-host"
CRYPTO_BINARY = ROOT / "build" / "tests" / "test_crypto_host.exe"
SCHEDULING_RESULT_DIR = ROOT / "build" / "test-results" / "scheduling-host"
SCHEDULING_BINARY = ROOT / "build" / "tests" / "test_core_scheduling_host.exe"
PACKAGE_RESULT_DIR = ROOT / "build" / "test-results" / "package-host"
PACKAGE_BINARY = ROOT / "build" / "tests" / "test_package_host.exe"
STATE_RESULT_DIR = ROOT / "build" / "test-results" / "state-host"
STATE_BINARY = ROOT / "build" / "tests" / "test_core_state_host.exe"
DEVICE_RESULT_DIR = ROOT / "build" / "test-results" / "device-manager-host"
DEVICE_BINARY = ROOT / "build" / "tests" / "test_device_manager_host.exe"
APP_API_RESULT_DIR = ROOT / "build" / "test-results" / "app-api-host"
APP_API_BINARY = ROOT / "build" / "tests" / "test_app_api_host.exe"
APP_CATALOG_RESULT_DIR = ROOT / "build" / "test-results" / "app-catalog-host"
APP_CATALOG_BINARY = ROOT / "build" / "tests" / "test_app_catalog_host.exe"
INPUT_RESULT_DIR = ROOT / "build" / "test-results" / "input-host"
INPUT_BINARY = ROOT / "build" / "tests" / "test_input_host.exe"
POWER_RESULT_DIR = ROOT / "build" / "test-results" / "power-host"
POWER_BINARY = ROOT / "build" / "tests" / "test_power_host.exe"
VFS_PATH_RESULT_DIR = ROOT / "build" / "test-results" / "vfs-path-host"
VFS_PATH_BINARY = ROOT / "build" / "tests" / "test_vfs_path_host.exe"
FILE_INDEX_RESULT_DIR = ROOT / "build" / "test-results" / "file-index-host"
FILE_INDEX_BINARY = ROOT / "build" / "tests" / "test_file_index_host.exe"
FS_RESULT_DIR = ROOT / "build" / "test-results" / "fs-host"
FS_BINARY = ROOT / "build" / "tests" / "test_fs_host.exe"
STORAGE_RESULT_DIR = ROOT / "build" / "test-results" / "storage-host"
STORAGE_BINARY = ROOT / "build" / "tests" / "test_storage_host.exe"
BLOCK_RESULT_DIR = ROOT / "build" / "test-results" / "block-host"
BLOCK_BINARY = ROOT / "build" / "tests" / "test_block_host.exe"
FAT12_RESULT_DIR = ROOT / "build" / "test-results" / "fat12-host"
FAT12_BINARY = ROOT / "build" / "tests" / "test_fat12_host.exe"
FAT32_RESULT_DIR = ROOT / "build" / "test-results" / "fat32-host"
FAT32_BINARY = ROOT / "build" / "tests" / "test_fat32_host.exe"
VFS_RESULT_DIR = ROOT / "build" / "test-results" / "vfs-host"
VFS_BINARY = ROOT / "build" / "tests" / "test_vfs_host.exe"
SLAB_RESULT_DIR = ROOT / "build" / "test-results" / "slab-host"
SLAB_BINARY = ROOT / "build" / "tests" / "test_slab_metadata_host.exe"
TIMER_RESULT_DIR = ROOT / "build" / "test-results" / "timer-host"
TIMER_BINARY = ROOT / "build" / "tests" / "test_timer_host.exe"
UDP_RESULT_DIR = ROOT / "build" / "test-results" / "udp-host"
UDP_BINARY = ROOT / "build" / "tests" / "test_udp_host.exe"
ARP_RESULT_DIR = ROOT / "build" / "test-results" / "arp-host"
ARP_BINARY = ROOT / "build" / "tests" / "test_arp_host.exe"
ICMP_RESULT_DIR = ROOT / "build" / "test-results" / "icmp-host"
ICMP_BINARY = ROOT / "build" / "tests" / "test_icmp_host.exe"
DNS_RESULT_DIR = ROOT / "build" / "test-results" / "dns-host"
DNS_BINARY = ROOT / "build" / "tests" / "test_dns_host.exe"
DEFAULT_TIMEOUT = 120.0
CORE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_core_contracts.c",
    ROOT / "src" / "core" / "string.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "clock.c",
    ROOT / "src" / "core" / "test_protocol_core.c",
)
NETWORK_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_network_host.c",
    ROOT / "src" / "core" / "net_buffer.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
NETWORK_MANAGER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_network_manager_host.c",
    ROOT / "src" / "core" / "network_manager.c",
    ROOT / "src" / "core" / "recovery.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ROUTE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_route_host.c",
    ROOT / "src" / "core" / "route.c",
    ROOT / "src" / "core" / "ipv4.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
IPV4_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_ipv4_host.c",
    ROOT / "src" / "core" / "ipv4.c",
    ROOT / "src" / "core" / "route.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
CRYPTO_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_crypto_host.c",
    ROOT / "src" / "core" / "crypto.c",
    ROOT / "src" / "core" / "crypto_ed25519.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
SCHEDULING_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_core_scheduling_host.c",
    ROOT / "src" / "core" / "wait.c",
    ROOT / "src" / "core" / "workqueue.c",
    ROOT / "src" / "core" / "irq_deferred.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
PACKAGE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_package_host.c",
    ROOT / "src" / "core" / "app_package.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
STATE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_core_state_host.c",
    ROOT / "src" / "core" / "recovery.c",
    ROOT / "src" / "core" / "power_notifier.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
DEVICE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_device_manager_host.c",
    ROOT / "src" / "core" / "device_manager.c",
    ROOT / "src" / "core" / "recovery.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
APP_API_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_app_api_host.c",
    ROOT / "src" / "core" / "app_api.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
APP_CATALOG_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_app_catalog_host.c",
    ROOT / "src" / "core" / "app_catalog.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
INPUT_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_input_host.c",
    ROOT / "src" / "core" / "input.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
POWER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_power_host.c",
    ROOT / "src" / "core" / "power.c",
    ROOT / "src" / "core" / "power_notifier.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
VFS_PATH_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_vfs_path_host.c",
    ROOT / "src" / "fs" / "vfs_path.c",
    ROOT / "src" / "core" / "string.c",
)
FILE_INDEX_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_file_index_host.c",
    ROOT / "src" / "fs" / "file_index.c",
    ROOT / "src" / "core" / "string.c",
)
FS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_fs_host.c",
    ROOT / "src" / "fs" / "fs.c",
    ROOT / "src" / "core" / "string.c",
)
STORAGE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_storage_host.c",
    ROOT / "src" / "fs" / "storage.c",
    ROOT / "src" / "core" / "string.c",
)
BLOCK_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_block_host.c",
    ROOT / "src" / "fs" / "block.c",
    ROOT / "src" / "fs" / "block_cache.c",
    ROOT / "src" / "core" / "string.c",
)
FAT12_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_fat12_host.c",
    ROOT / "src" / "fs" / "fat12.c",
    ROOT / "src" / "core" / "string.c",
)
FAT32_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_fat32_host.c",
    ROOT / "src" / "fs" / "fat32.c",
    ROOT / "src" / "core" / "string.c",
)
VFS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_vfs_host.c",
    ROOT / "src" / "fs" / "vfs.c",
    ROOT / "src" / "core" / "string.c",
)
SLAB_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_slab_metadata_host.c",
    ROOT / "src" / "memory" / "slab.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
TIMER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_timer_host.c",
    ROOT / "src" / "drivers" / "timer.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
UDP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_udp_host.c",
    ROOT / "src" / "core" / "udp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ARP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_arp_host.c",
    ROOT / "src" / "core" / "arp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ICMP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_icmp_host.c",
    ROOT / "src" / "core" / "icmp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
DNS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_dns_host.c",
    ROOT / "src" / "core" / "dns.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
SOURCE_FILES = CORE_SOURCE_FILES


def case_configuration(case_id: str) -> tuple[Path, Path, tuple[Path, ...], str]:
    if case_id == "host:core:contracts":
        return RESULT_DIR, DEFAULT_BINARY, CORE_SOURCE_FILES, "core-host"
    if case_id == "host:core:net-buffer":
        return NETWORK_RESULT_DIR, NETWORK_BINARY, NETWORK_SOURCE_FILES, "network-host"
    if case_id == "host:core:network-manager":
        return (NETWORK_MANAGER_RESULT_DIR, NETWORK_MANAGER_BINARY,
                NETWORK_MANAGER_SOURCE_FILES, "network-manager-host")
    if case_id == "host:network:route":
        return ROUTE_RESULT_DIR, ROUTE_BINARY, ROUTE_SOURCE_FILES, "route-host"
    if case_id == "host:network:ipv4":
        return IPV4_RESULT_DIR, IPV4_BINARY, IPV4_SOURCE_FILES, "ipv4-host"
    if case_id == "host:core:crypto":
        return CRYPTO_RESULT_DIR, CRYPTO_BINARY, CRYPTO_SOURCE_FILES, "crypto-host"
    if case_id == "host:core:scheduling":
        return (SCHEDULING_RESULT_DIR, SCHEDULING_BINARY,
                SCHEDULING_SOURCE_FILES, "scheduling-host")
    if case_id == "host:core:app-package":
        return PACKAGE_RESULT_DIR, PACKAGE_BINARY, PACKAGE_SOURCE_FILES, "package-host"
    if case_id == "host:core:state":
        return STATE_RESULT_DIR, STATE_BINARY, STATE_SOURCE_FILES, "state-host"
    if case_id == "host:core:device-manager":
        return (DEVICE_RESULT_DIR, DEVICE_BINARY,
                DEVICE_SOURCE_FILES, "device-manager-host")
    if case_id == "host:core:app-api":
        return APP_API_RESULT_DIR, APP_API_BINARY, APP_API_SOURCE_FILES, "app-api-host"
    if case_id == "host:core:app-catalog":
        return (APP_CATALOG_RESULT_DIR, APP_CATALOG_BINARY,
                APP_CATALOG_SOURCE_FILES, "app-catalog-host")
    if case_id == "host:core:input":
        return INPUT_RESULT_DIR, INPUT_BINARY, INPUT_SOURCE_FILES, "input-host"
    if case_id == "host:core:power":
        return POWER_RESULT_DIR, POWER_BINARY, POWER_SOURCE_FILES, "power-host"
    if case_id == "host:storage:vfs-path":
        return (VFS_PATH_RESULT_DIR, VFS_PATH_BINARY,
                VFS_PATH_SOURCE_FILES, "vfs-path-host")
    if case_id == "host:storage:file-index":
        return (FILE_INDEX_RESULT_DIR, FILE_INDEX_BINARY,
                FILE_INDEX_SOURCE_FILES, "file-index-host")
    if case_id == "host:storage:fs":
        return FS_RESULT_DIR, FS_BINARY, FS_SOURCE_FILES, "fs-host"
    if case_id == "host:storage:storage":
        return STORAGE_RESULT_DIR, STORAGE_BINARY, STORAGE_SOURCE_FILES, "storage-host"
    if case_id == "host:storage:block":
        return BLOCK_RESULT_DIR, BLOCK_BINARY, BLOCK_SOURCE_FILES, "block-host"
    if case_id == "host:storage:fat12":
        return FAT12_RESULT_DIR, FAT12_BINARY, FAT12_SOURCE_FILES, "fat12-host"
    if case_id == "host:storage:fat32":
        return FAT32_RESULT_DIR, FAT32_BINARY, FAT32_SOURCE_FILES, "fat32-host"
    if case_id == "host:storage:vfs":
        return VFS_RESULT_DIR, VFS_BINARY, VFS_SOURCE_FILES, "vfs-host"
    if case_id == "host:memory:slab-metadata":
        return SLAB_RESULT_DIR, SLAB_BINARY, SLAB_SOURCE_FILES, "slab-host"
    if case_id == "host:core:timer":
        return TIMER_RESULT_DIR, TIMER_BINARY, TIMER_SOURCE_FILES, "timer-host"
    if case_id == "host:network:udp":
        return UDP_RESULT_DIR, UDP_BINARY, UDP_SOURCE_FILES, "udp-host"
    if case_id == "host:network:arp":
        return ARP_RESULT_DIR, ARP_BINARY, ARP_SOURCE_FILES, "arp-host"
    if case_id == "host:network:icmp":
        return ICMP_RESULT_DIR, ICMP_BINARY, ICMP_SOURCE_FILES, "icmp-host"
    if case_id == "host:network:dns":
        return DNS_RESULT_DIR, DNS_BINARY, DNS_SOURCE_FILES, "dns-host"
    raise ValueError(f"caso_host_invalido:{case_id}")


def executable(value: str) -> str | None:
    candidate = value.strip().strip('"')
    if not candidate:
        return None
    path = Path(candidate)
    if path.is_file():
        return str(path)
    return shutil.which(candidate)


def command_environment(compiler: str) -> dict[str, str]:
    environment = os.environ.copy()
    parent = Path(compiler).parent
    if parent != Path('.'):
        environment["PATH"] = str(parent) + os.pathsep + environment.get("PATH", "")
    return environment


def run_process(command: list[str], environment: dict[str, str],
                timeout: float) -> dict[str, Any]:
    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=ROOT, env=environment,
                                   capture_output=True, text=True,
                                   timeout=timeout, check=False)
        result = {
            "status": "PASS" if completed.returncode == 0 else "FAIL",
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
    except subprocess.TimeoutExpired as error:
        result = {
            "status": "TIMEOUT",
            "returncode": None,
            "stdout": error.stdout or "",
            "stderr": error.stderr or "",
        }
    result["command"] = command
    result["duration_seconds"] = round(time.monotonic() - started, 6)
    return result


def compiler_command(compiler: str, binary: Path,
                     sources: tuple[Path, ...] | None = None) -> list[str]:
    selected_sources = sources or SOURCE_FILES
    compatibility_flags = []
    if any(source.name == "crypto_ed25519.c" for source in selected_sources):
        compatibility_flags.append("-Wno-unused-const-variable")
    return [
        compiler, "-std=c11", "-O0", "-fno-inline", "-ffunction-sections",
        "-fdata-sections", "-Wall", "-Wextra",
        "-Werror", "-DZEPHYROS_HOST_TEST=1", "-finstrument-functions",
        *compatibility_flags,
        "-I", str(ROOT / "tests" / "unit" / "host_include"),
        "-I", str(ROOT / "src" / "include"),
        "-I", str(ROOT / "src" / "core"),
        *(str(source) for source in selected_sources), "-Wl,--gc-sections",
        "-o", str(binary),
    ]


def find_nm(compiler: str, environment: dict[str, str]) -> str | None:
    compiler_path = Path(compiler)
    candidates = [compiler_path.with_name("nm.exe"), compiler_path.with_name("nm")]
    candidates.extend([Path("nm.exe"), Path("nm")])
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
        resolved = shutil.which(str(candidate), path=environment.get("PATH"))
        if resolved:
            return resolved
    return None


def symbol_map(binary: Path, nm: str, environment: dict[str, str],
               timeout: float) -> tuple[list[dict[str, Any]] | None, dict[str, Any]]:
    result = run_process([nm, "-n", str(binary)], environment, timeout)
    if result["status"] != "PASS":
        return None, result
    return coverage_collector.parse_nm(result["stdout"]), result


def attach_surface_ids(symbols: list[dict[str, Any]],
                       catalog: dict[str, Any],
                       sources: tuple[Path, ...] | None = None) -> list[dict[str, Any]]:
    selected_sources = sources or SOURCE_FILES
    source_names = {path.relative_to(ROOT).as_posix() for path in selected_sources
                    if path.as_posix().startswith((ROOT / "src").as_posix())}
    candidates: dict[str, list[str]] = {}
    for surface in catalog.get("surfaces", []):
        if not isinstance(surface, dict) or surface.get("kind") != "c_function":
            continue
        if surface.get("source") not in source_names:
            continue
        symbol = surface.get("symbol")
        identifier = surface.get("id")
        if isinstance(symbol, str) and isinstance(identifier, str):
            candidates.setdefault(symbol, []).append(identifier)
    enriched = []
    for symbol in symbols:
        item = dict(symbol)
        options = candidates.get(symbol.get("symbol"), [])
        if len(options) == 1:
            item["surface_id"] = options[0]
        enriched.append(item)
    return enriched


def write_artifacts(result_dir: Path, manifest: dict[str, Any], result: dict[str, Any],
                    coverage: dict[str, Any] | None,
                    symbols: list[dict[str, Any]] | None) -> None:
    result_dir.mkdir(parents=True, exist_ok=True)
    (result_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (result_dir / "result.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (result_dir / "coverage.json").write_text(
        json.dumps(coverage or {}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (result_dir / "coverage-symbols.json").write_text(
        json.dumps({"schema": coverage_collector.SYMBOL_SCHEMA,
                    "symbols": symbols or []}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    stdout = "\n".join(step.get("stdout", "") for step in result["steps"])
    stderr = "\n".join(step.get("stderr", "") for step in result["steps"])
    (result_dir / "stdout.log").write_text(stdout, encoding="utf-8")
    (result_dir / "stderr.log").write_text(stderr, encoding="utf-8")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--case", default="host:core:contracts",
                        choices=("host:core:contracts", "host:core:net-buffer",
                                 "host:core:network-manager",
                                 "host:network:route", "host:network:ipv4",
                                 "host:core:crypto", "host:core:scheduling",
                                 "host:core:app-package", "host:core:state",
                                 "host:core:device-manager", "host:core:app-api",
                                 "host:core:app-catalog", "host:core:input",
                                 "host:core:power", "host:storage:vfs-path",
                                 "host:storage:file-index", "host:storage:fs",
                                 "host:storage:storage", "host:storage:block",
                                 "host:storage:fat12", "host:storage:fat32",
                                 "host:storage:vfs",
                                 "host:memory:slab-metadata",
                                 "host:core:timer", "host:network:udp",
                                 "host:network:arp", "host:network:icmp",
                                 "host:network:dns"))
    parser.add_argument("--catalog", default=str(ROOT / "tests" / "catalog.json"))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    result_dir, default_binary, source_files, suite = case_configuration(arguments.case)
    compiler = executable(arguments.cc)
    binary_value = arguments.binary
    if arguments.case != "host:core:contracts" and binary_value == str(DEFAULT_BINARY):
        binary_value = str(default_binary)
    binary = Path(binary_value)
    if not binary.is_absolute():
        binary = ROOT / binary
    manifest = {
        "suite": suite,
        "case_id": arguments.case,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "compiler_requested": arguments.cc,
        "compiler": compiler,
        "timeout_seconds": arguments.timeout,
        "instrumentation": "-finstrument-functions",
        "sources": [str(path.relative_to(ROOT)) for path in source_files],
    }
    result: dict[str, Any] = {"suite": suite, "status": "PASS",
                              "steps": [], "cause": None}
    if arguments.timeout <= 0:
        result["status"] = "FAIL"
        result["cause"] = "timeout_invalido"
        write_artifacts(result_dir, manifest, result, None, None)
        return 1
    if not compiler:
        result["status"] = "BLOCKED"
        result["cause"] = f"host_compiler_unavailable:{arguments.cc}"
        write_artifacts(result_dir, manifest, result, None, None)
        print(f"Core host: BLOCKED {result['cause']}", file=sys.stderr)
        return 2

    environment = command_environment(compiler)
    catalog = coverage_collector.read_json(Path(arguments.catalog))
    binary.parent.mkdir(parents=True, exist_ok=True)
    compile_result = run_process(compiler_command(compiler, binary, source_files), environment,
                                 arguments.timeout)
    result["steps"].append(compile_result)
    if compile_result["status"] != "PASS":
        result["status"] = compile_result["status"]
        result["cause"] = "core_host_compile"
        write_artifacts(result_dir, manifest, result, None, None)
        return 1

    run_result = run_process([str(binary)], environment, arguments.timeout)
    result["steps"].append(run_result)
    nm = find_nm(compiler, environment)
    if not nm:
        result["status"] = "BLOCKED"
        result["cause"] = "nm_unavailable_for_coverage"
        write_artifacts(result_dir, manifest, result, None, None)
        return 2
    symbols, nm_result = symbol_map(binary, nm, environment, arguments.timeout)
    result["steps"].append(nm_result)
    coverage = None
    if symbols is not None and run_result["stdout"]:
        try:
            symbols = attach_surface_ids(symbols, catalog, source_files)
            coverage = coverage_collector.collect_report(
                run_result["stdout"], symbols)
        except (coverage_collector.CoverageError, OSError) as error:
            result["cause"] = f"coverage_collection:{error}"
    if run_result["status"] != "PASS":
        result["status"] = run_result["status"]
        result["cause"] = result["cause"] or "core_host_test"
    elif coverage is None or coverage["status"] != "PASS":
        result["status"] = "FAIL"
        result["cause"] = result["cause"] or "coverage_incomplete"
    write_artifacts(result_dir, manifest, result, coverage, symbols)
    print(f"Core host: {result['status']}")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
