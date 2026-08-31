#!/usr/bin/env python3
"""Executa os testes host-only da infraestrutura da TST6."""

from __future__ import annotations

import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "build" / "test-results" / "tst6-host"
TIMEOUT_SECONDS = 60


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def output_text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    return value.decode(errors="replace") if isinstance(value, bytes) else value


def main() -> int:
    command = [sys.executable, "-m", "unittest", "tests.unit.test_tst6_runner"]
    started = time.monotonic()
    status = "PASS"
    cause = None
    stdout = ""
    stderr = ""
    returncode: int | None = 0
    try:
        completed = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True,
            timeout=TIMEOUT_SECONDS,
        )
        stdout = completed.stdout
        stderr = completed.stderr
        returncode = completed.returncode
        if returncode != 0:
            status = "FAIL"
            cause = "falha nos testes host-only da TST6"
    except subprocess.TimeoutExpired as error:
        status = "FAIL"
        cause = "timeout nos testes host-only da TST6"
        stdout = output_text(error.stdout)
        stderr = output_text(error.stderr)
        returncode = None

    manifest = {
        "suite": "TST6-host",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "timeout_seconds": TIMEOUT_SECONDS,
        "command": command,
        "scope": "profiles QEMU, limites de stress e classificacao de falhas",
        "artifacts": {
            "manifest": "manifest.json",
            "result": "result.json",
            "stdout": "stdout.log",
            "stderr": "stderr.log",
        },
    }
    result = {
        "suite": "TST6-host",
        "status": status,
        "cause": cause,
        "returncode": returncode,
        "duration_seconds": round(time.monotonic() - started, 3),
        "steps": [{
            "status": status,
            "command": command,
            "stdout": stdout,
            "stderr": stderr,
        }],
    }
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    write_json(RESULT_DIR / "manifest.json", manifest)
    write_json(RESULT_DIR / "result.json", result)
    (RESULT_DIR / "stdout.log").write_text(stdout, encoding="utf-8")
    (RESULT_DIR / "stderr.log").write_text(stderr, encoding="utf-8")
    if stdout:
        print(stdout, end="")
    if stderr:
        print(stderr, end="", file=sys.stderr)
    print(f"TST6 host-only: {status}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
