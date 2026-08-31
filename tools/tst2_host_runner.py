#!/usr/bin/env python3
"""Executa os testes host-only da infraestrutura TST2."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = ROOT / "build" / "tests" / "test_protocol_core_host.exe"


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
    compiler_path = Path(compiler)
    if compiler_path.parent != Path('.'):
        environment["PATH"] = str(compiler_path.parent) + os.pathsep + environment.get("PATH", "")
    return environment


def run_command(command: list[str], environment: dict[str, str] | None = None) -> int:
    result = subprocess.run(command, cwd=ROOT, env=environment,
                            capture_output=True, text=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    return result.returncode


def compile_protocol_test(compiler: str, binary: Path) -> int:
    binary.parent.mkdir(parents=True, exist_ok=True)
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DZEphyros_HOST_TEST=1",
        "-I", str(ROOT / "tests" / "unit" / "host_include"),
        "-I", str(ROOT / "src" / "core"),
        "-I", str(ROOT / "src" / "include"),
        str(ROOT / "tests" / "unit" / "test_protocol_core.c"),
        str(ROOT / "src" / "core" / "test_protocol_core.c"),
        "-o", str(binary),
    ]
    return run_command(command, command_environment(compiler))


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    compiler = executable(arguments.cc)
    if not compiler:
        print(f"TST2 host: BLOCKED host compiler unavailable: {arguments.cc}",
              file=sys.stderr)
        return 2
    binary = Path(arguments.binary)
    if not binary.is_absolute(): binary = ROOT / binary
    compile_result = compile_protocol_test(compiler, binary)
    if compile_result != 0:
        print("TST2 host: ERRO compilando teste C", file=sys.stderr)
        return 1
    binary_result = run_command([str(binary)], command_environment(compiler))
    if binary_result != 0:
        print("TST2 host: ERRO no teste C do protocolo", file=sys.stderr)
        return 1
    python_result = run_command([
        sys.executable, "-m", "unittest", "discover",
        "-s", str(ROOT / "tests" / "unit"), "-p", "test_*.py"],
        command_environment(compiler))
    if python_result != 0:
        print("TST2 host: ERRO nos testes Python do runner", file=sys.stderr)
        return 1
    print("TST2 host-only: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
