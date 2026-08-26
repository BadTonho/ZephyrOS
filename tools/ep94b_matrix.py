from __future__ import annotations

import argparse
import struct
from pathlib import Path

from packager import fat32_geometry, inject_fat32_bytes
from system_slots_matrix import cache_record, slot_record, state_record


DISK_BYTES = 268435456
SECTOR_SIZE = 512
FAT32_TYPE_LBA = 0x0C
FAT32_MIN_CLUSTERS = 65525


def inject(image: bytearray, path: str, data: bytes, start_lba: int) -> None:
    inject_fat32_bytes(
        data, image, path, replace=True, fat32_start_lba=start_lba
    )


def validate_geometry(raw: bytearray, start_lba: int) -> None:
    if len(raw) != DISK_BYTES:
        raise ValueError("imagem EP9.4B nao possui 256 MiB")
    entry = 446
    if (raw[entry + 4] != FAT32_TYPE_LBA or
            struct.unpack_from("<I", raw, entry + 8)[0] != start_lba):
        raise ValueError("particao FAT32 EP9.4B invalida")
    bps, spc, _, _, _, _, clusters = fat32_geometry(raw, start_lba)
    if bps != SECTOR_SIZE or spc != 4 or clusters < FAT32_MIN_CLUSTERS:
        raise ValueError("geometria FAT32 EP9.4B invalida")
    boot = raw[start_lba * SECTOR_SIZE:(start_lba + 1) * SECTOR_SIZE]
    backup = raw[(start_lba + 6) * SECTOR_SIZE:(start_lba + 7) * SECTOR_SIZE]
    if boot != backup:
        raise ValueError("backup do BPB FAT32 diverge")
    fsinfo = raw[(start_lba + 1) * SECTOR_SIZE:(start_lba + 2) * SECTOR_SIZE]
    if (struct.unpack_from("<I", fsinfo, 0)[0] != 0x41615252 or
            struct.unpack_from("<I", fsinfo, 484)[0] != 0x61417272 or
            struct.unpack_from("<I", fsinfo, 508)[0] != 0xAA550000):
        raise ValueError("FSInfo FAT32 invalido")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Gera a imagem guiada unica da EP9.4B"
    )
    parser.add_argument("--base-image", required=True)
    parser.add_argument("--abi1", required=True)
    parser.add_argument("--abi2", required=True)
    parser.add_argument("--bad-boot", required=True)
    parser.add_argument("--bad-stage2", required=True)
    parser.add_argument("--bad-kernel", required=True)
    parser.add_argument("--handoff-invalid", required=True)
    parser.add_argument("--returning-boot", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--fat32-start-lba", type=int, default=4096)
    args = parser.parse_args()

    source = Path(args.base_image)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    raw = bytearray(source.read_bytes())
    validate_geometry(raw, args.fat32_start_lba)

    abi1 = Path(args.abi1).read_bytes()
    abi2 = Path(args.abi2).read_bytes()
    active = slot_record(abi1)
    state = state_record(active)
    cache = cache_record(abi2)
    for path, data in (
        ("ZSA0.ZSY", abi1),
        ("ZSI0.STA", state),
        ("ZSI1.STA", state),
        ("ZSC0.ZSY", abi2),
        ("ZSC0.STA", cache),
        ("ZSC1.STA", cache),
        ("ABI2.ZSY", abi2),
        ("BADBOOT.ZSY", Path(args.bad_boot).read_bytes()),
        ("BADSTG2.ZSY", Path(args.bad_stage2).read_bytes()),
        ("BADKERN.ZSY", Path(args.bad_kernel).read_bytes()),
        ("BADHAND.ZSY", Path(args.handoff_invalid).read_bytes()),
        ("RETURN.ZSY", Path(args.returning_boot).read_bytes()),
    ):
        inject(raw, path, data, args.fat32_start_lba)
    validate_geometry(raw, args.fat32_start_lba)
    output.write_bytes(raw)
    print(f"Matriz EP9.4B criada: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
