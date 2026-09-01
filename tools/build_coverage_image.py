#!/usr/bin/env python3
"""Gera a imagem instrumentada em um diretório de build separado."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def executable(value: str) -> str | None:
    candidate = value.strip().strip('"')
    if not candidate:
        return None
    path = Path(candidate)
    if path.is_file():
        return str(path)
    return shutil.which(candidate)


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    command.add_argument("--make", default="make")
    command.add_argument("--build-dir", default="build-coverage")
    command.add_argument("--cflags", default="-DZEPHYROS_TEST_COVERAGE -finstrument-functions")
    return command


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    make = executable(arguments.make)
    if not make:
        print(f"Coverage image: BLOCKED make unavailable: {arguments.make}",
              file=sys.stderr)
        return 2
    environment = os.environ.copy()
    command = [make, "-B", "BUILD_DIR=" + arguments.build_dir,
               "CFLAGS_EXTRA=" + arguments.cflags, "all", "kernel-elf"]
    try:
        completed = subprocess.run(command, cwd=ROOT, env=environment,
                                   check=False)
    except OSError as error:
        print(f"Coverage image: BLOCKED {error}", file=sys.stderr)
        return 2
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
