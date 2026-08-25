"""Gera o contrato imutavel do kernel legado para o recovery loader."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Gera recovery_layout.h a partir do kernel legado"
    )
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    kernel = Path(args.kernel).read_bytes()
    if not kernel:
        raise ValueError("kernel legado vazio")
    digest = hashlib.sha256(kernel).digest()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    values = ", ".join(f"0x{value:02X}U" for value in digest)
    output.write_text(
        "#ifndef RECOVERY_LAYOUT_H\n"
        "#define RECOVERY_LAYOUT_H\n\n"
        "/* Gerado pelo build; vincula o fallback ao kernel legado no disco. */\n"
        f"#define RECOVERY_LEGACY_KERNEL_SIZE {len(kernel)}U\n"
        f"static const uint8_t recovery_legacy_kernel_sha256[32] = {{ {values} }};\n\n"
        "#endif\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
