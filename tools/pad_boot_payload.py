"""Alinha um artefato legado para que o proximo LBA comece em setor inteiro."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Alinha payload de boot")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--alignment", type=int, default=512)
    args = parser.parse_args()
    if args.alignment <= 0 or args.alignment & (args.alignment - 1):
        raise ValueError("alinhamento precisa ser potencia de dois")
    source = Path(args.input).read_bytes()
    if not source:
        raise ValueError("payload de boot vazio")
    padding = (-len(source)) % args.alignment
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(source + bytes(padding))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
