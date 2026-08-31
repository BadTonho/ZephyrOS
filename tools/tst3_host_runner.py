#!/usr/bin/env python3
"""Executa a suíte host-only de lógica e limites da TST3."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = ROOT / "tests" / "unit"
SOURCE_FILES = (
    TEST_DIR / "test_string_compress.c",
    ROOT / "src" / "core" / "string.c",
    ROOT / "src" / "memory" / "compress.c",
)
RESULT_DIR = ROOT / "build" / "test-results" / "tst3-host"
DEFAULT_BINARY = ROOT / "build" / "tests" / "test_string_compress_host.exe"
DEFAULT_SANITIZED_BINARY = ROOT / "build" / "tests" / "test_string_compress_sanitize.exe"
DEFAULT_TIMEOUT = 120


def executable(value: str) -> str | None:
    candidate = value.strip().strip('"')
    if not candidate:
        return None
    path = Path(candidate)
    if path.is_file():
        return str(path)
    return shutil.which(candidate)


def command_environment(compiler: str | None) -> dict[str, str]:
    environment = os.environ.copy()
    if compiler:
        parent = Path(compiler).parent
        if parent != Path('.'):
            environment["PATH"] = str(parent) + os.pathsep + environment.get("PATH", "")
    return environment


def format_output(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode(errors="replace")
    return value


def run_process(
    command: list[str],
    environment: dict[str, str] | None,
    timeout: int,
) -> dict[str, Any]:
    started = time.monotonic()
    try:
        completed = subprocess.run(
            command,
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        status = "PASS" if completed.returncode == 0 else "FAIL"
        result = {
            "status": status,
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
    except subprocess.TimeoutExpired as error:
        result = {
            "status": "TIMEOUT",
            "returncode": None,
            "stdout": format_output(error.stdout),
            "stderr": format_output(error.stderr),
        }
    result["command"] = command
    result["duration_seconds"] = round(time.monotonic() - started, 3)
    if result["stdout"]:
        print(result["stdout"], end="")
    if result["stderr"]:
        print(result["stderr"], end="", file=sys.stderr)
    return result


def compile_command(compiler: str, binary: Path, sanitize: bool) -> list[str]:
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I", str(ROOT / "tests" / "unit" / "host_include"),
        "-I", str(ROOT / "src" / "include"),
    ]
    if sanitize:
        command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
    command.extend([str(source) for source in SOURCE_FILES])
    command.extend(["-o", str(binary)])
    return command


def sanitizer_runtime_available(compiler: str, timeout: int) -> tuple[bool, dict[str, Any]]:
    """Verifica o link do runtime antes de compilar o teste real."""
    with tempfile.TemporaryDirectory(prefix="zephyros-tst3-sanitize-") as directory:
        source = Path(directory) / "probe.c"
        binary = Path(directory) / "probe.exe"
        source.write_text("int main(void) { return 0; }\n", encoding="ascii")
        command = [
            compiler,
            "-std=c11",
            "-fsanitize=address,undefined",
            str(source),
            "-o",
            str(binary),
        ]
        result = run_process(command, command_environment(compiler), timeout)
        return result["status"] == "PASS", result


def run_python_suite(compiler: str, timeout: int) -> list[dict[str, Any]]:
    environment = command_environment(compiler)
    commands = [
        [sys.executable, "-m", "unittest", "tests.unit.test_packager"],
        [sys.executable, "-m", "unittest", "tests.unit.test_updater"],
        [sys.executable, str(ROOT / "tools" / "packager.py"), "selftest"],
        [sys.executable, str(ROOT / "tools" / "updater.py"), "selftest"],
    ]
    return [run_process(command, environment, timeout) for command in commands]


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("strict", "sanitize"), default="strict")
    parser.add_argument("--cc", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--sanitize-cc", default=os.environ.get("HOST_SANITIZE_CC", "clang"))
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--sanitize-binary", default=str(DEFAULT_SANITIZED_BINARY))
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    return parser.parse_args()


def absolute_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def write_results(manifest: dict[str, Any], result: dict[str, Any]) -> None:
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    stdout = []
    stderr = []
    for step in result["steps"]:
        stdout.append(f"[{step['status']}] {' '.join(step['command'])}\n")
        stdout.append(step.get("stdout", ""))
        stderr.append(step.get("stderr", ""))
    for output_dir in (RESULT_DIR, RESULT_DIR / manifest["mode"]):
        output_dir.mkdir(parents=True, exist_ok=True)
        (output_dir / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (output_dir / "result.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (output_dir / "stdout.log").write_text(
            "".join(stdout), encoding="utf-8"
        )
        (output_dir / "stderr.log").write_text(
            "".join(stderr), encoding="utf-8"
        )


def main() -> int:
    arguments = parse_arguments()
    if arguments.timeout <= 0:
        print("TST3 host: FAIL timeout invalido", file=sys.stderr)
        return 1

    sanitize = arguments.mode == "sanitize"
    selected_compiler_value = arguments.sanitize_cc if sanitize else arguments.cc
    compiler = executable(selected_compiler_value)
    manifest = {
        "suite": "TST3-host",
        "mode": arguments.mode,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "timeout_seconds": arguments.timeout,
        "compiler_requested": selected_compiler_value,
        "compiler": compiler,
        "sources": [str(path.relative_to(ROOT)) for path in SOURCE_FILES],
        "artifacts": {
            "manifest": str((RESULT_DIR / "manifest.json").relative_to(ROOT)),
            "result": str((RESULT_DIR / "result.json").relative_to(ROOT)),
            "mode_directory": str((RESULT_DIR / arguments.mode).relative_to(ROOT)),
            "stdout": str((RESULT_DIR / "stdout.log").relative_to(ROOT)),
            "stderr": str((RESULT_DIR / "stderr.log").relative_to(ROOT)),
        },
    }
    result: dict[str, Any] = {
        "suite": "TST3-host",
        "mode": arguments.mode,
        "status": "BLOCKED" if not compiler else "PASS",
        "cause": None,
        "steps": [],
    }
    if not compiler:
        result["cause"] = f"compiler unavailable: {selected_compiler_value}"
        write_results(manifest, result)
        print(f"TST3 host-only: BLOCKED {result['cause']}", file=sys.stderr)
        return 2

    if sanitize:
        available, probe = sanitizer_runtime_available(compiler, arguments.timeout)
        result["steps"].append(probe)
        if not available:
            result["status"] = "BLOCKED"
            result["cause"] = "Clang/LLVM sanitizer runtime unavailable"
            write_results(manifest, result)
            print(f"TST3 sanitize: BLOCKED {result['cause']}", file=sys.stderr)
            return 2

    binary_value = arguments.sanitize_binary if sanitize else arguments.binary
    binary = absolute_path(binary_value)
    binary.parent.mkdir(parents=True, exist_ok=True)
    compile_result = run_process(
        compile_command(compiler, binary, sanitize),
        command_environment(compiler),
        arguments.timeout,
    )
    result["steps"].append(compile_result)
    if compile_result["status"] != "PASS":
        result["status"] = compile_result["status"]
        result["cause"] = "falha compilando teste C" if compile_result["status"] == "FAIL" else "teste C excedeu timeout"
    else:
        c_result = run_process([str(binary)], command_environment(compiler), arguments.timeout)
        result["steps"].append(c_result)
        if c_result["status"] != "PASS":
            result["status"] = c_result["status"]
            result["cause"] = "falha no teste C" if c_result["status"] == "FAIL" else "teste C excedeu timeout"
        elif not sanitize:
            for step in run_python_suite(compiler, arguments.timeout):
                result["steps"].append(step)
                if step["status"] != "PASS":
                    result["status"] = step["status"]
                    result["cause"] = "falha na suíte Python" if step["status"] == "FAIL" else "suíte Python excedeu timeout"
                    break

    write_results(manifest, result)
    print(f"TST3 {arguments.mode}: {result['status']}")
    if result["status"] == "PASS":
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
