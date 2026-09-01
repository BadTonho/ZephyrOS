#!/usr/bin/env python3
"""Supervisiona ciclos limitados ou contínuos da regressão TST7."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TST7_RUNNER = ROOT / "tools" / "tst7_regression_runner.py"
RESULTS_ROOT = ROOT / ".tst7-results"
DEFAULT_INTERVAL = 60.0
DEFAULT_CYCLE_TIMEOUT = 7200.0
MAX_INTERVAL = 86400.0
MAX_CYCLE_TIMEOUT = 7200.0
MAX_CYCLES = 1000
STOP_POLL_INTERVAL = 1.0
RUN_ID_RE = re.compile(r"^tst7c-[0-9]{8}T[0-9]{6}Z-[0-9]+-[0-9]+$")
MODE_TARGETS = {"quick": "quick", "full": "full", "soak": "soak"}


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def safe_run_id(value: str) -> bool:
    return bool(RUN_ID_RE.fullmatch(value)) and len(value) < 64


def cycle_run_id(cycle: int) -> str:
    return f"tst7c-{utc_stamp()}-{os.getpid()}-{cycle}"


def write_json_atomic(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def classify_returncode(returncode: int | None, timed_out: bool) -> str:
    if timed_out:
        return "TIMEOUT"
    if returncode == 0:
        return "PASS"
    if returncode == 2:
        return "BLOCKED"
    return "FAIL"


def failure_signature(item: dict[str, Any]) -> str | None:
    if item.get("status") not in {"FAIL", "TIMEOUT", "BLOCKED"}:
        return None
    nested_result = item.get("result")
    nested_cause = nested_result.get("cause") \
        if isinstance(nested_result, dict) else None
    cause = str(nested_cause or item.get("status", "unknown"))
    return f"{item.get('mode', 'unknown')}:{item.get('status')}:{cause}"


def group_failures(cycles: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[str, dict[str, Any]] = {}
    for item in cycles:
        signature = failure_signature(item)
        if signature is None:
            continue
        group = groups.setdefault(signature, {
            "signature": signature,
            "status": item.get("status"),
            "count": 0,
            "cycles": [],
            "run_ids": [],
            "artifact_dirs": [],
        })
        group["count"] += 1
        group["cycles"].append(item.get("cycle"))
        group["run_ids"].append(item.get("run_id"))
        group["artifact_dirs"].append(item.get("artifact_dir"))
    return [groups[key] for key in sorted(groups)]


def terminate_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            capture_output=True,
            check=False,
        )
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def build_command(arguments: argparse.Namespace, run_id: str) -> list[str]:
    target = MODE_TARGETS[arguments.mode]
    command = [
        sys.executable,
        str(TST7_RUNNER),
        target,
        "--run-id",
        run_id,
        "--make",
        arguments.make,
        "--qemu",
        arguments.qemu,
        "--image",
        arguments.image,
        "--catalog",
        arguments.catalog,
        "--command-timeout",
        str(arguments.command_timeout),
        "--suite-timeout",
        str(arguments.suite_timeout),
    ]
    if arguments.mode in {"full", "soak"}:
        command.append("--strict-coverage")
    command.extend(["--case-timeout", str(arguments.case_timeout)])
    return command


def read_cycle_result(run_id: str) -> dict[str, Any] | None:
    path = RESULTS_ROOT / run_id / "result.json"
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def run_cycle(arguments: argparse.Namespace, session_dir: Path,
              cycle: int) -> dict[str, Any]:
    run_id = cycle_run_id(cycle)
    command = build_command(arguments, run_id)
    started = time.monotonic()
    stdout = b""
    stderr = b""
    returncode: int | None = None
    timed_out = False
    launch_failed = False
    process: subprocess.Popen[bytes] | None = None
    try:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=os.name != "nt",
            creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
        )
        try:
            stdout, stderr = process.communicate(
                timeout=arguments.cycle_timeout)
            returncode = process.returncode
        except subprocess.TimeoutExpired as error:
            timed_out = True
            stdout = error.output or b""
            stderr = error.stderr or b""
            terminate_process(process)
            remaining_stdout, remaining_stderr = process.communicate()
            stdout += remaining_stdout or b""
            stderr += remaining_stderr or b""
    except OSError as error:
        launch_failed = True
        stderr = str(error).encode("utf-8", errors="replace")
    result = read_cycle_result(run_id)
    status = "BLOCKED" if launch_failed else classify_returncode(returncode, timed_out)
    if result is not None and not timed_out:
        status = str(result.get("status", status))
    duration = round(time.monotonic() - started, 3)
    output_dir = RESULTS_ROOT / run_id
    if not stdout and output_dir.is_dir():
        stdout = (output_dir / "stdout.log").read_bytes() if \
            (output_dir / "stdout.log").is_file() else b""
    if not stderr and output_dir.is_dir():
        stderr = (output_dir / "stderr.log").read_bytes() if \
            (output_dir / "stderr.log").is_file() else b""
    with (session_dir / "stdout.log").open("ab") as stream:
        stream.write(f"\n[cycle={cycle} run={run_id}]\n".encode())
        stream.write(stdout)
    with (session_dir / "stderr.log").open("ab") as stream:
        stream.write(f"\n[cycle={cycle} run={run_id}]\n".encode())
        stream.write(stderr)
    return {
        "cycle": cycle,
        "run_id": run_id,
        "mode": arguments.mode,
        "delegated_mode": MODE_TARGETS[arguments.mode],
        "status": status,
        "returncode": returncode,
        "timed_out": timed_out,
        "duration_seconds": duration,
        "command": command,
        "artifact_dir": str(output_dir),
        "result": result,
    }


def stop_requested(arguments: argparse.Namespace) -> bool:
    return arguments.stop_file.is_file() or arguments.stop_event


def wait_interval(arguments: argparse.Namespace) -> bool:
    deadline = time.monotonic() + arguments.interval
    while time.monotonic() < deadline:
        if stop_requested(arguments):
            return False
        time.sleep(min(STOP_POLL_INTERVAL, deadline - time.monotonic()))
    return True


def install_signal_handlers(arguments: argparse.Namespace) -> None:
    def request_stop(_signum: int, _frame: Any) -> None:
        arguments.stop_event = True

    signal.signal(signal.SIGINT, request_stop)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, request_stop)


def validate_arguments(arguments: argparse.Namespace) -> str | None:
    if arguments.interval < 0 or arguments.interval > MAX_INTERVAL:
        return "intervalo_invalido"
    if arguments.cycle_timeout <= 0 or arguments.cycle_timeout > MAX_CYCLE_TIMEOUT:
        return "timeout_de_ciclo_invalido"
    if arguments.command_timeout <= 0 or arguments.command_timeout > MAX_CYCLE_TIMEOUT:
        return "timeout_de_comando_invalido"
    if arguments.suite_timeout <= 0 or arguments.suite_timeout > MAX_CYCLE_TIMEOUT:
        return "timeout_de_suite_invalido"
    if arguments.case_timeout <= 0 or arguments.case_timeout > MAX_CYCLE_TIMEOUT:
        return "timeout_de_caso_invalido"
    if arguments.forever and arguments.max_cycles is not None:
        return "forever_e_max_cycles_sao_exclusivos"
    if not arguments.forever and arguments.max_cycles is None:
        return "defina_max_cycles_ou_forever"
    if arguments.max_cycles is not None and not 1 <= arguments.max_cycles <= MAX_CYCLES:
        return "max_cycles_invalido"
    if not arguments.stop_file.is_absolute():
        arguments.stop_file = ROOT / arguments.stop_file
    return None


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    start = subparsers.add_parser("start")
    start.add_argument("--mode", choices=tuple(MODE_TARGETS), default="full")
    lifetime = start.add_mutually_exclusive_group(required=True)
    lifetime.add_argument("--max-cycles", type=int)
    lifetime.add_argument("--forever", action="store_true")
    start.add_argument("--interval", type=float, default=DEFAULT_INTERVAL)
    start.add_argument("--cycle-timeout", type=float, default=DEFAULT_CYCLE_TIMEOUT)
    start.add_argument("--command-timeout", type=float, default=300.0)
    start.add_argument("--suite-timeout", type=float, default=7200.0)
    start.add_argument("--case-timeout", type=float, default=120.0)
    start.add_argument("--make", default=os.environ.get("MAKE", "make"))
    start.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    start.add_argument("--image", default=str(ROOT / "build" / "zephyros.img"))
    start.add_argument("--catalog", default=str(ROOT / "tests" / "catalog.json"))
    start.add_argument("--stop-file", type=Path,
                       default=Path(".tst7-results/STOP"))
    return parser.parse_args(argv)


def run_supervisor(arguments: argparse.Namespace) -> int:
    error = validate_arguments(arguments)
    if error:
        print(f"TST7 continuous: BLOCKED {error}", file=sys.stderr)
        return 2
    session_id = f"session-{utc_stamp()}-{os.getpid()}"
    session_dir = RESULTS_ROOT / "continuous" / session_id
    session_dir.mkdir(parents=True, exist_ok=False)
    (session_dir / "stdout.log").touch()
    (session_dir / "stderr.log").touch()
    arguments.stop_event = False
    install_signal_handlers(arguments)
    manifest = {
        "schema": "zephyros-tst7-continuous-manifest-v1",
        "session_id": session_id,
        "mode": arguments.mode,
        "delegated_mode": MODE_TARGETS[arguments.mode],
        "started_at": datetime.now(timezone.utc).isoformat(),
        "interval_seconds": arguments.interval,
        "cycle_timeout_seconds": arguments.cycle_timeout,
        "max_cycles": arguments.max_cycles,
        "forever": arguments.forever,
        "stop_file": str(arguments.stop_file),
    }
    write_json_atomic(session_dir / "manifest.json", manifest)
    cycles: list[dict[str, Any]] = []
    status = "PASS"
    cycle = 0
    while not stop_requested(arguments) and \
            (arguments.forever or cycle < arguments.max_cycles):
        cycle += 1
        item = run_cycle(arguments, session_dir, cycle)
        cycles.append(item)
        if item["status"] in {"FAIL", "TIMEOUT"}:
            status = "FAIL"
        elif item["status"] == "BLOCKED" and status == "PASS":
            status = "BLOCKED"
        write_json_atomic(session_dir / "cycles.json", cycles)
        write_json_atomic(session_dir / "latest.json", item)
        write_json_atomic(session_dir / "failure-groups.json", group_failures(cycles))
        print(f"TST7 continuous cycle={cycle} status={item['status']} "
              f"run={item['run_id']}")
        if not arguments.forever and cycle >= arguments.max_cycles:
            break
        if not wait_interval(arguments):
            break
    if stop_requested(arguments):
        status = "STOPPED" if status == "PASS" else status
    failure_groups = group_failures(cycles)
    result = {
        "schema": "zephyros-tst7-continuous-result-v1",
        "session_id": session_id,
        "status": status,
        "cycles_completed": len(cycles),
        "stopped_by_user": stop_requested(arguments),
        "cycles": cycles,
        "failure_groups": failure_groups,
        "artifacts": {
            "manifest": str(session_dir / "manifest.json"),
            "cycles": str(session_dir / "cycles.json"),
            "latest": str(session_dir / "latest.json"),
            "failure_groups": str(session_dir / "failure-groups.json"),
            "session_index": str(session_dir / "session-index.json"),
            "stdout": str(session_dir / "stdout.log"),
            "stderr": str(session_dir / "stderr.log"),
        },
    }
    session_index_path = RESULTS_ROOT / "continuous" / "index.json"
    previous_index: list[dict[str, Any]] = []
    if session_index_path.is_file():
        try:
            loaded_index = json.loads(session_index_path.read_text(encoding="utf-8"))
            if isinstance(loaded_index, list):
                previous_index = loaded_index
        except (OSError, json.JSONDecodeError):
            previous_index = []
    session_summary = {
        "session_id": session_id,
        "mode": arguments.mode,
        "status": status,
        "cycles_completed": len(cycles),
        "artifact_dir": str(session_dir),
        "failure_groups": failure_groups,
    }
    previous_index.append(session_summary)
    write_json_atomic(session_dir / "session-index.json", session_summary)
    write_json_atomic(session_index_path, previous_index)
    write_json_atomic(session_dir / "result.json", result)
    write_json_atomic(RESULTS_ROOT / "continuous" / "latest.json", result)
    print(f"TST7 continuous: {status} session={session_id}")
    print(f"Artefatos: {session_dir}")
    if status == "PASS" or status == "STOPPED":
        return 0
    return 2 if status == "BLOCKED" else 1


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    if arguments.command != "start":
        return 2
    return run_supervisor(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
