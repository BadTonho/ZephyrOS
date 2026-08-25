"""Monta o layout legado sem mover o FAT32 nem alterar boot.asm."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


SECTOR_SIZE = 512


def main() -> int:
    parser = argparse.ArgumentParser(description="Monta imagem com recovery loader")
    parser.add_argument("--boot", required=True)
    parser.add_argument("--stage2", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--loader", required=True)
    parser.add_argument("--kernel-lba", type=int, required=True)
    parser.add_argument("--loader-lba", type=int, required=True)
    parser.add_argument("--fat32-start-lba", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    boot = bytearray(Path(args.boot).read_bytes())
    stage2 = Path(args.stage2).read_bytes()
    kernel = Path(args.kernel).read_bytes()
    loader = Path(args.loader).read_bytes()
    if len(boot) != SECTOR_SIZE or not stage2 or not kernel or not loader:
        raise ValueError("artefato legado invalido")
    pre_kernel = boot + stage2
    if math.ceil(len(pre_kernel) / SECTOR_SIZE) > args.kernel_lba:
        raise ValueError("stage2 invade o LBA fixo do kernel legado")
    kernel_end_lba = args.kernel_lba + math.ceil(len(kernel) / SECTOR_SIZE)
    if kernel_end_lba > args.loader_lba:
        raise ValueError("kernel legado invade a janela do recovery loader")
    if args.loader_lba + math.ceil(len(loader) / SECTOR_SIZE) > args.fat32_start_lba:
        raise ValueError("recovery loader invade o FAT32")
    reserved = math.ceil(len(pre_kernel) / SECTOR_SIZE)
    struct.pack_into("<H", pre_kernel, 14, reserved)
    image = (pre_kernel + bytes(args.kernel_lba * SECTOR_SIZE - len(pre_kernel)) +
             kernel + bytes(args.loader_lba * SECTOR_SIZE -
                            (args.kernel_lba * SECTOR_SIZE + len(kernel))) + loader)
    Path(args.output).write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
