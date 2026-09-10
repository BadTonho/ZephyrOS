#!/usr/bin/env python3
"""Executa casos QEMU independentes em paralelo e preserva evidencias."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import platform
import random
import re
import secrets
import shutil
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import qemu_test_runner

DEFAULT_IMAGE = Path("build/zephyros.img")
DEFAULT_CATALOG = Path("tests/catalog.json")
DEFAULT_RESULTS = Path("build/test-results/parallel")
DEFAULT_WORKERS = 4
MAX_WORKERS = 64
DEFAULT_SOAK_TAGS = ("stress", "fault", "apps", "storage")
DEFAULT_BOOT_TIMEOUT = 60.0
DEFAULT_CASE_TIMEOUT = 120.0
DEFAULT_HEARTBEAT_TIMEOUT = 60.0
DEFAULT_SUITE_TIMEOUT = 600.0
MAX_TIMEOUT = 600.0
MAX_SUITE_TIMEOUT = 7200.0
RUN_ID_RE = re.compile(r"^[A-Za-z0-9_.:-]+$")


class ParallelError(Exception):
    """Falha controlada na selecao ou coordenacao de casos."""

    def __init__(self, cause: str, blocked: bool = True):
        super().__init__(cause)
        self.cause = cause
        self.blocked = blocked


def resolve_path(value: str | None, default: Path) -> Path:
    path = Path(value) if value else default
    return path if path.is_absolute() else ROOT / path


def write_json_atomic(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
    except OSError:
        return None
    return digest.hexdigest()


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def default_run_id() -> str:
    return f"qpp-{utc_stamp()}-{os.getpid()}"


def safe_run_id(value: str) -> bool:
    return bool(value) and len(value) < 48 and bool(RUN_ID_RE.fullmatch(value))


def slug(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", value)
    return result[:48] or "case"


def git_revision() -> str | None:
    try:
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
            text=True, timeout=5, check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def load_catalog(path: Path) -> dict[str, Any]:
    try:
        catalog = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ParallelError(f"catalogo_invalido:{error}") from error
    if not isinstance(catalog, dict) or \
            catalog.get("schema") != "zephyros-test-catalog-v1":
        raise ParallelError("schema_catalogo_invalido")
    if not isinstance(catalog.get("cases"), list):
        raise ParallelError("casos_ausentes_no_catalogo")
    return catalog


def qemu_cases(catalog: dict[str, Any]) -> list[dict[str, Any]]:
    return sorted(
        [case for case in catalog["cases"]
         if isinstance(case, dict)
         and case.get("status") == "AUTOMATED"
         and case.get("executor") == "qemu"],
        key=lambda case: str(case.get("id", "")),
    )


def validate_selection_arguments(arguments: argparse.Namespace,
                                 mode: str) -> str | None:
    if mode == "parallel":
        if not (arguments.case or arguments.profile or arguments.tag or
                arguments.all_cases):
            return "selecao_deve_conter_case_profile_tag_ou_all"
        if arguments.case and (arguments.profile or arguments.tag or
                               arguments.all_cases):
            return "case_nao_pode_ser_combinado"
        if arguments.all_cases and (arguments.profile or arguments.tag):
            return "all_nao_pode_ser_combinado"
    if mode == "soak" and arguments.case:
        return "soak_nao_aceita_case_exato"
    if mode == "soak" and arguments.all_cases:
        return "soak_usa_tags"
    if not 1 <= arguments.workers <= MAX_WORKERS:
        return "workers_fora_do_limite"
    for value in (arguments.boot_timeout, arguments.case_timeout,
                  arguments.heartbeat_timeout):
        if value <= 0 or value > MAX_TIMEOUT:
            return "timeout_fora_do_limite"
    if arguments.suite_timeout <= 0 or \
            arguments.suite_timeout > MAX_SUITE_TIMEOUT:
        return "suite_timeout_fora_do_limite"
    if arguments.seed is not None and not 0 <= arguments.seed <= 0xFFFFFFFF:
        return "seed_invalida"
    if arguments.run_id and not safe_run_id(arguments.run_id):
        return "run_id_invalido"
    return None


def select_cases(catalog: dict[str, Any], arguments: argparse.Namespace,
                 mode: str) -> list[dict[str, Any]]:
    available = qemu_cases(catalog)
    by_id = {str(case["id"]): case for case in available}
    if arguments.case:
        if len(arguments.case) != len(set(arguments.case)):
            raise ParallelError("caso_duplicado")
        missing = sorted(set(arguments.case) - set(by_id))
        if missing:
            raise ParallelError(f"caso_inexistente:{','.join(missing)}")
        selected = [by_id[case_id] for case_id in arguments.case]
    elif arguments.all_cases:
        selected = list(available)
    else:
        profiles = {arguments.profile} if arguments.profile else None
        tags = set(arguments.tag)
        selected = [
            case for case in available
            if (profiles is None or case.get("profile") in profiles)
            and (not tags or tags.intersection(case.get("tags", [])))
        ]
    if mode == "soak" and not arguments.tag:
        selected = [
            case for case in available
            if set(DEFAULT_SOAK_TAGS).intersection(case.get("tags", []))
        ]
    if not selected:
        raise ParallelError("selecao_vazia")
    for case in selected:
        try:
            qemu_test_runner.validate_case_for_runner(case)
            qemu_test_runner.validate_qemu_profile(
                str(case.get("qemu_profile", "baseline")))
        except qemu_test_runner.RunnerError as error:
            raise ParallelError(f"caso_invalido:{case.get('id')}:{error.cause}") \
                from error
        if case.get("isolation") not in {"snapshot", "fixture"}:
            raise ParallelError(f"isolamento_invalido:{case.get('id')}")
    return sorted(selected, key=lambda case: str(case["id"]))


def stable_seed(master_seed: int, case_id: str, occurrence: int) -> int:
    material = f"zephyros-qemu-parallel:v1:{master_seed}:{occurrence}:{case_id}"
    digest = hashlib.sha256(material.encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big")


def qemu_network(case: dict[str, Any]) -> str:
    identifier = str(case.get("id", ""))
    parameters = case.get("parameters")
    if not isinstance(parameters, dict):
        parameters = {}
    declared = parameters.get("network")
    if identifier == "qemu:tst4:network":
        return "user,model=e1000,restrict=on"
    if declared in {"none", "offline", False}:
        return "none"
    required = case.get("required_capabilities")
    if not isinstance(required, list):
        required = []
    if case.get("qemu_profile") == "network" or \
            "network-e1000" in required or \
            declared in {"isolated", "user-isolated"}:
        return "user,model=e1000,restrict=on"
    return "none"


def qemu_iterations(case: dict[str, Any]) -> int:
    parameters = case.get("parameters")
    if isinstance(parameters, dict):
        value = parameters.get("iterations")
        if isinstance(value, int) and 0 < value <= 1000:
            return value
    return 1


def case_command(case: dict[str, Any], arguments: argparse.Namespace,
                 child_run_id: str, case_results: Path,
                 case_seed: int) -> list[str]:
    profile = str(case.get("qemu_profile", "baseline"))
    iterations = qemu_iterations(case)
    case_timeout = max(arguments.case_timeout,
                       float(case.get("timeout_seconds", arguments.case_timeout)))
    heartbeat_timeout = max(
        arguments.heartbeat_timeout,
        float(case.get("heartbeat_timeout_seconds", arguments.heartbeat_timeout)),
    )
    suite_timeout = min(
        arguments.suite_timeout,
        max(30.0, arguments.boot_timeout + case_timeout * iterations + 30.0),
        qemu_test_runner.TST6_MAX_DURATION_SECONDS,
    )
    command = [
        sys.executable, str(ROOT / "tools" / "qemu_test_runner.py"),
        "stress", "--case", str(case["id"]),
        "--iterations", str(iterations), "--seed", str(case_seed),
        "--run-id", child_run_id, "--image", arguments.image,
        "--catalog", arguments.catalog, "--results", str(case_results),
        "--qemu", arguments.qemu, "--cpu", arguments.cpu,
        "--qemu-profile", profile, "--network", qemu_network(case),
        "--boot-timeout", str(arguments.boot_timeout),
        "--case-timeout", str(case_timeout),
        "--heartbeat-timeout", str(heartbeat_timeout),
        "--suite-timeout", str(suite_timeout),
    ]
    parameters = case.get("parameters")
    if isinstance(parameters, dict) and isinstance(parameters.get("fixture"), str):
        command.extend(["--fixture", parameters["fixture"]])
    if profile == "usb-storage":
        command.extend(["--storage-image", arguments.storage_image])
    return command


def terminate_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            capture_output=True, check=False,
        )
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


class ProcessController:
    """Mantem os processos ativos para parada cooperativa e watchdog."""

    def __init__(self, stop_file: Path | None):
        self.stop_file = stop_file
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.processes: dict[str, subprocess.Popen[bytes]] = {}

    def requested(self) -> bool:
        return self.stop_event.is_set() or bool(
            self.stop_file and self.stop_file.is_file())

    def request_stop(self) -> None:
        self.stop_event.set()
        self.terminate_all()

    def register(self, case_id: str, process: subprocess.Popen[bytes]) -> None:
        with self.lock:
            self.processes[case_id] = process

    def unregister(self, case_id: str) -> None:
        with self.lock:
            self.processes.pop(case_id, None)

    def terminate_all(self) -> None:
        with self.lock:
            active = list(self.processes.values())
        for process in active:
            terminate_process(process)


def child_result(path: Path) -> dict[str, Any] | None:
    result_path = path / "result.json"
    if not result_path.is_file():
        return None
    try:
        value = json.loads(result_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def classify_child(returncode: int | None, timed_out: bool,
                   stopped: bool, result: dict[str, Any] | None) -> str:
    if stopped:
        return "STOPPED"
    if timed_out:
        return "TIMEOUT"
    if result and isinstance(result.get("status"), str):
        return str(result["status"])
    if returncode == 0:
        return "PASS"
    if returncode == 2:
        return "BLOCKED"
    return "FAIL"


def run_case(case: dict[str, Any], index: int, arguments: argparse.Namespace,
             run_dir: Path, master_seed: int,
             controller: ProcessController) -> dict[str, Any]:
    case_id = str(case["id"])
    case_dir = run_dir / "cases" / f"{index:03d}-{slug(case_id)}"
    case_dir.mkdir(parents=True, exist_ok=True)
    child_run_id = f"qpp-{os.getpid()}-{index:03d}-{slug(case_id)}"[:47]
    case_seed = stable_seed(master_seed, case_id, index)
    command = case_command(
        case, arguments, child_run_id, case_dir, case_seed)
    started = time.monotonic()
    process: subprocess.Popen[bytes] | None = None
    stdout = b""
    stderr = b""
    returncode: int | None = None
    timed_out = False
    launch_failed = False
    stopped = False
    try:
        process = subprocess.Popen(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            start_new_session=os.name != "nt",
            creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
        )
        controller.register(case_id, process)
        deadline = time.monotonic() + case_timeout_limit(case, arguments)
        while process.poll() is None:
            if controller.requested():
                stopped = True
                terminate_process(process)
                break
            if time.monotonic() >= deadline:
                timed_out = True
                terminate_process(process)
                break
            time.sleep(0.05)
        stdout, stderr = process.communicate(timeout=5)
        returncode = process.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        if process:
            terminate_process(process)
            stdout, stderr = process.communicate()
            returncode = process.returncode
    except OSError as error:
        launch_failed = True
        stderr = str(error).encode("utf-8", errors="replace")
    finally:
        controller.unregister(case_id)
    if controller.requested() and returncode is None:
        stopped = True
    (case_dir / "stdout.log").write_bytes(stdout)
    (case_dir / "stderr.log").write_bytes(stderr)
    result = child_result(case_dir / child_run_id)
    status = "BLOCKED" if launch_failed else classify_child(
        returncode, timed_out, stopped, result)
    return {
        "id": case_id,
        "status": status,
        "returncode": returncode,
        "timed_out": timed_out,
        "duration_seconds": round(time.monotonic() - started, 6),
        "seed": case_seed,
        "command": command,
        "artifact_dir": str(case_dir / child_run_id),
        "result": result,
        "stdout": str(case_dir / "stdout.log"),
        "stderr": str(case_dir / "stderr.log"),
    }


def case_timeout_limit(case: dict[str, Any], arguments: argparse.Namespace) -> float:
    iterations = qemu_iterations(case)
    case_timeout = max(arguments.case_timeout,
                       float(case.get("timeout_seconds", arguments.case_timeout)))
    return min(
        MAX_TIMEOUT,
        arguments.boot_timeout + case_timeout * iterations + 60.0,
    )


def aggregate_status(results: list[dict[str, Any]], stopped: bool) -> tuple[str, str]:
    if stopped:
        return "STOPPED", "interrompido_pelo_usuario"
    if not results:
        return "BLOCKED", "nenhum_caso_executado"
    if any(item["status"] in {"FAIL", "TIMEOUT"} for item in results):
        return "FAIL", "caso_reprovado"
    if any(item["status"] == "BLOCKED" for item in results):
        return "BLOCKED", "caso_bloqueado"
    if any(item["status"] == "STOPPED" for item in results):
        return "STOPPED", "interrompido_pelo_usuario"
    return "PASS", "suite_concluida"


def failure_groups(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[str, dict[str, Any]] = {}
    for item in results:
        if item["status"] not in {"FAIL", "TIMEOUT", "BLOCKED"}:
            continue
        nested = item.get("result")
        cause = nested.get("cause") if isinstance(nested, dict) else None
        signature = f"{item['status']}:{cause or item['status']}"
        group = groups.setdefault(signature, {
            "signature": signature, "status": item["status"], "count": 0,
            "case_ids": [], "artifact_dirs": [],
        })
        group["count"] += 1
        group["case_ids"].append(item["id"])
        group["artifact_dirs"].append(item["artifact_dir"])
    return [groups[key] for key in sorted(groups)]


def environment(arguments: argparse.Namespace) -> dict[str, Any]:
    return {
        "os": platform.system(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "qemu": str(shutil.which(arguments.qemu) or arguments.qemu),
        "workers_cpu_count": os.cpu_count(),
    }


def initialize_controller(arguments: argparse.Namespace) -> ProcessController:
    stop_file = resolve_path(arguments.stop_file, Path()) \
        if arguments.stop_file else None
    return ProcessController(stop_file)


def run_parallel(arguments: argparse.Namespace, mode: str) -> int:
    error = validate_selection_arguments(arguments, mode)
    if error:
        raise ParallelError(error)
    image = resolve_path(arguments.image, DEFAULT_IMAGE)
    catalog_path = resolve_path(arguments.catalog, DEFAULT_CATALOG)
    results_root = resolve_path(arguments.results, DEFAULT_RESULTS)
    catalog = load_catalog(catalog_path)
    if not image.is_file():
        raise ParallelError(f"imagem_ausente:{image}")
    if not catalog_path.is_file():
        raise ParallelError(f"catalogo_ausente:{catalog_path}")
    selected = select_cases(catalog, arguments, mode)
    master_seed = arguments.seed if arguments.seed is not None else secrets.randbits(32)
    if mode == "soak":
        random.Random(master_seed).shuffle(selected)
    run_id = arguments.run_id or default_run_id()
    run_dir = results_root / run_id
    run_dir.mkdir(parents=True, exist_ok=False)
    manifest = {
        "schema": "zephyros-qemu-parallel-manifest-v1",
        "run_id": run_id,
        "mode": mode,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "git_revision": git_revision(),
        "image": str(image),
        "image_sha256": sha256_file(image),
        "catalog": str(catalog_path),
        "catalog_sha256": sha256_file(catalog_path),
        "environment": environment(arguments),
        "workers_requested": arguments.workers,
        "workers_used": min(arguments.workers, len(selected)),
        "seed": master_seed,
        "cases_selected": [str(case["id"]) for case in selected],
        "cases_ignored": [
            str(case["id"]) for case in qemu_cases(catalog)
            if case not in selected
        ],
        "parameters": {
            "boot_timeout": arguments.boot_timeout,
            "case_timeout": arguments.case_timeout,
            "heartbeat_timeout": arguments.heartbeat_timeout,
            "suite_timeout": arguments.suite_timeout,
            "qemu": arguments.qemu,
            "cpu": arguments.cpu,
        },
        "snapshot": True,
    }
    write_json_atomic(run_dir / "manifest.json", manifest)
    controller = initialize_controller(arguments)
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=arguments.workers) as executor:
        futures = {
            executor.submit(run_case, case, index, arguments, run_dir,
                            master_seed, controller): case
            for index, case in enumerate(selected, start=1)
        }
        pending = set(futures)
        while pending:
            done, pending = concurrent.futures.wait(
                pending, timeout=0.1,
                return_when=concurrent.futures.FIRST_COMPLETED,
            )
            if controller.requested():
                controller.request_stop()
                for future in pending:
                    future.cancel()
                break
            for future in done:
                try:
                    results.append(future.result())
                except concurrent.futures.CancelledError:
                    continue
                except Exception as error:
                    case = futures[future]
                    results.append({
                        "id": str(case["id"]),
                        "status": "FAIL",
                        "returncode": None,
                        "timed_out": False,
                        "duration_seconds": 0,
                        "seed": stable_seed(master_seed, str(case["id"]),
                                             0),
                        "command": [],
                        "artifact_dir": str(run_dir / "cases" /
                                                f"worker-{slug(str(case['id']))}"),
                        "result": {"cause": f"worker_exception:{error}"},
                    })
                write_json_atomic(run_dir / "partial.json", {
                    "schema": "zephyros-qemu-parallel-partial-v1",
                    "run_id": run_id,
                    "cases_completed": len(results),
                    "results": sorted(results, key=lambda item: item["id"]),
                })
        if controller.requested():
            controller.request_stop()
            for future in pending:
                if not future.cancelled():
                    try:
                        results.append(future.result(timeout=10))
                    except concurrent.futures.CancelledError:
                        continue
                    except Exception as error:
                        case = futures[future]
                        results.append({
                            "id": str(case["id"]),
                            "status": "FAIL",
                            "returncode": None,
                            "timed_out": False,
                            "duration_seconds": 0,
                            "seed": stable_seed(master_seed,
                                                 str(case["id"]), 0),
                            "command": [],
                            "artifact_dir": str(run_dir / "cases" /
                                                    f"worker-{slug(str(case['id']))}"),
                            "result": {
                                "cause": f"worker_exception:{error}"
                            },
                        })
    completed_ids = {item["id"] for item in results}
    for case in selected:
        if str(case["id"]) not in completed_ids:
            results.append({
                "id": str(case["id"]), "status": "STOPPED",
                "returncode": None, "timed_out": False, "duration_seconds": 0,
                "seed": stable_seed(master_seed, str(case["id"]), 0),
                "command": [], "artifact_dir": str(run_dir / "cases" /
                                                        f"pending-{slug(str(case['id']))}"),
                "result": None,
            })
    results.sort(key=lambda item: item["id"])
    status, cause = aggregate_status(results, controller.requested())
    report = {
        "schema": "zephyros-qemu-parallel-result-v1",
        "run_id": run_id,
        "mode": mode,
        "status": status,
        "cause": cause,
        "finished_at": datetime.now(timezone.utc).isoformat(),
        "workers_requested": arguments.workers,
        "workers_used": min(arguments.workers, len(selected)),
        "seed": master_seed,
        "cases_selected": [str(case["id"]) for case in selected],
        "cases_completed": len([item for item in results
                                 if item["status"] != "STOPPED"]),
        "cases": results,
        "failure_groups": failure_groups(results),
        "artifacts": {
            "manifest": "manifest.json",
            "partial": "partial.json",
            "result": "result.json",
            "cases": "cases/",
        },
    }
    write_json_atomic(run_dir / "result.json", report)
    print(f"QEMU parallel: {status} run={run_id}")
    print(f"Artefatos: {run_dir}")
    return 0 if status in {"PASS", "STOPPED"} else 2 if status == "BLOCKED" else 1


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    subparsers = command_parser.add_subparsers(dest="command", required=True)
    for name in ("parallel", "soak"):
        subparser = subparsers.add_parser(name)
        subparser.add_argument("--case", action="append", default=[])
        subparser.add_argument("--profile")
        subparser.add_argument("--tag", action="append", default=[])
        subparser.add_argument("--all", dest="all_cases", action="store_true")
        subparser.add_argument("--workers", type=int, default=DEFAULT_WORKERS)
        subparser.add_argument("--seed", type=int)
        subparser.add_argument("--run-id")
        subparser.add_argument("--image", default=str(DEFAULT_IMAGE))
        subparser.add_argument("--catalog", default=str(DEFAULT_CATALOG))
        subparser.add_argument("--results", default=str(DEFAULT_RESULTS))
        subparser.add_argument("--storage-image", default="build/storage-valid.img")
        subparser.add_argument("--qemu", default=os.environ.get(
            "QEMU", "qemu-system-i386"))
        subparser.add_argument("--cpu", default=os.environ.get(
            "QEMU_TEST_CPU", "max"))
        subparser.add_argument("--boot-timeout", type=float,
                               default=DEFAULT_BOOT_TIMEOUT)
        subparser.add_argument("--case-timeout", type=float,
                               default=DEFAULT_CASE_TIMEOUT)
        subparser.add_argument("--heartbeat-timeout", type=float,
                               default=DEFAULT_HEARTBEAT_TIMEOUT)
        subparser.add_argument("--suite-timeout", type=float,
                               default=DEFAULT_SUITE_TIMEOUT)
        subparser.add_argument("--stop-file", type=Path)
    return command_parser


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        return run_parallel(arguments, arguments.command)
    except ParallelError as error:
        print(f"QEMU parallel: {'BLOCKED' if error.blocked else 'FAIL'} "
              f"{error.cause}", file=sys.stderr)
        return 2 if error.blocked else 1


if __name__ == "__main__":
    raise SystemExit(main())
