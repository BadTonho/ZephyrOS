from __future__ import annotations

import argparse
from pathlib import Path


SECTOR_SIZE = 512


def main() -> int:
    parser = argparse.ArgumentParser(description="Substitui o stage2 de uma imagem")
    parser.add_argument("--base", required=True)
    parser.add_argument("--stage2", required=True)
    parser.add_argument("--reference-stage2", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--stage2-lba", type=int, required=True)
    parser.add_argument("--kernel-lba", type=int, required=True)
    args = parser.parse_args()

    base_path = Path(args.base)
    stage2_path = Path(args.stage2)
    reference_path = Path(args.reference_stage2)
    output_path = Path(args.output)
    stage2 = stage2_path.read_bytes()
    reference = reference_path.read_bytes()
    image = bytearray(base_path.read_bytes())
    start = args.stage2_lba * SECTOR_SIZE
    limit = args.kernel_lba * SECTOR_SIZE

    if output_path.resolve() == base_path.resolve():
        raise ValueError("a imagem de origem nao pode ser sobrescrita")
    if output_path.resolve() in {stage2_path.resolve(), reference_path.resolve()}:
        raise ValueError("a saida nao pode sobrescrever um stage2")
    if args.stage2_lba <= 0 or args.kernel_lba <= args.stage2_lba:
        raise ValueError("janela do stage2 invalida")
    if not stage2 or len(stage2) != len(reference):
        raise ValueError("stage2 VGA diverge do tamanho do stage2 de referencia")
    if len(stage2) % SECTOR_SIZE:
        raise ValueError("stage2 VGA nao esta alinhado a setor")
    if start + len(stage2) > limit or limit > len(image):
        raise ValueError("stage2 VGA excede a janela anterior ao kernel")
    if image[start:start + len(reference)] != reference:
        raise ValueError("fixture nao corresponde ao stage2 de referencia")

    image[start:start + len(stage2)] = stage2
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
