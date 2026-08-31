#!/usr/bin/env python3
"""Executa os testes host-only do contrato de interacao da TST5."""

from __future__ import annotations

import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "build" / "test-results" / "tst5-host"
TIMEOUT_SECONDS = 45


def write_document(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def main() -> int:
    command = [sys.executable, "-m", "unittest", "tests.unit.test_tst5_runner"]
    started = time.monotonic()
    status = "PASS"
    cause = None
    stdout = ""
    stderr = ""
    returncode = 0
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True,
                                   text=True, timeout=TIMEOUT_SECONDS)
        stdout = completed.stdout
        stderr = completed.stderr
        returncode = completed.returncode
        if returncode != 0:
            status = "FAIL"
            cause = "falha nos testes host-only da TST5"
    except subprocess.TimeoutExpired as error:
        status = "FAIL"
        cause = "timeout nos testes host-only da TST5"
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        returncode = None
    manifest = {
        "suite": "TST5-host",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "timeout_seconds": TIMEOUT_SECONDS,
        "command": command,
        "scope": "runner interaction, QMP payloads and lifecycle states",
        "artifacts": {
            "manifest": "manifest.json",
            "result": "result.json",
            "stdout": "stdout.log",
            "stderr": "stderr.log",
        },
    }
    result = {
        "suite": "TST5-host",
        "status": status,
        "cause": cause,
        "returncode": returncode,
        "duration_seconds": round(time.monotonic() - started, 3),
        "steps": [{"status": status, "command": command,
                   "stdout": stdout, "stderr": stderr}],
    }
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    write_document(RESULT_DIR / "manifest.json", manifest)
    write_document(RESULT_DIR / "result.json", result)
    (RESULT_DIR / "stdout.log").write_text(stdout, encoding="utf-8")
    (RESULT_DIR / "stderr.log").write_text(stderr, encoding="utf-8")
    if stdout:
        print(stdout, end="")
    if stderr:
        print(stderr, end="", file=sys.stderr)
    print(f"TST5 host-only: {status}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
