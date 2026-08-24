#!/usr/bin/env python3
"""Gera e verifica discos ATA deterministas usados pela matriz Storage EP2."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import tempfile
from pathlib import Path


SECTOR_SIZE = 512
VALID_SECTORS = 131_072
SMALL_SECTORS = 16_384
MANIFEST_NAME = "storage-fixtures.json"
STAMP_NAME = "storage-fixtures.stamp"
FIXTURES = (
    "storage-valid.img",
    "storage-corrupt.img",
    "storage-unknown.img",
    "storage-fat32-no-space.img",
    "storage-fat32-fat-divergent.img",
    "storage-fat32-chain-corrupt.img",
    "storage-fat32-lfn-invalid.img",
)

FAT32_START_LBA = 16_384
FAT32_RESERVED = 32
FAT32_SECTORS_PER_FAT = 800
FAT32_DATA_START = FAT32_RESERVED + 2 * FAT32_SECTORS_PER_FAT
FAT32_ROOT_LBA = FAT32_START_LBA + FAT32_DATA_START
FAT32_CLUSTER_COUNT = 100_000 - FAT32_DATA_START


def put_u16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def put_u32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def write_at(image, offset: int, data: bytes | bytearray) -> None:
    image.seek(offset)
    image.write(data)


def write_sector(image, lba: int, data: bytes | bytearray) -> None:
    if len(data) != SECTOR_SIZE:
        raise ValueError("setor deve ter exatamente 512 bytes")
    write_at(image, lba * SECTOR_SIZE, data)


def make_image(path: Path, sector_count: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as image:
        image.truncate(sector_count * SECTOR_SIZE)


def directory_entry(name: bytes, attributes: int, cluster: int,
                    size: int = 0) -> bytes:
    if len(name) != 11:
        raise ValueError("nome FAT deve ter 11 bytes")
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attributes
    put_u16(entry, 20, (cluster >> 16) & 0xFFFF)
    put_u16(entry, 26, cluster & 0xFFFF)
    put_u32(entry, 28, size)
    return bytes(entry)


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    offset = cluster + cluster // 2
    current = fat[offset] | (fat[offset + 1] << 8)
    if cluster & 1:
        current = (current & 0x000F) | ((value & 0x0FFF) << 4)
    else:
        current = (current & 0xF000) | (value & 0x0FFF)
    fat[offset] = current & 0xFF
    fat[offset + 1] = (current >> 8) & 0xFF


def write_fat12(image, start_lba: int, total_sectors: int,
                label: str, marker: str) -> None:
    reserved = 1
    fat_count = 2
    sectors_per_fat = 9
    root_entries = 224
    root_sectors = 14
    data_start = reserved + fat_count * sectors_per_fat + root_sectors
    readme = (f"ZephyrOS EP2 fixture {marker}\r\n"
              "Volume FAT12 somente leitura.\r\n").encode("ascii")
    sample = (f"Arquivo interno de {marker}.\r\n").encode("ascii")
    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"ZEPHYR2 "
    put_u16(boot, 11, SECTOR_SIZE)
    boot[13] = 1
    put_u16(boot, 14, reserved)
    boot[16] = fat_count
    put_u16(boot, 17, root_entries)
    put_u16(boot, 19, total_sectors)
    boot[21] = 0xF8
    put_u16(boot, 22, sectors_per_fat)
    put_u16(boot, 24, 63)
    put_u16(boot, 26, 16)
    put_u32(boot, 28, start_lba)
    boot[36] = 0x80
    boot[38] = 0x29
    put_u32(boot, 39, 0x5A455000 + start_lba)
    boot[43:54] = label.upper().ljust(11)[:11].encode("ascii")
    boot[54:62] = b"FAT12   "
    boot[510:512] = b"\x55\xAA"

    fat = bytearray(sectors_per_fat * SECTOR_SIZE)
    fat[0:3] = b"\xF8\xFF\xFF"
    for cluster in (2, 3, 4):
        set_fat12_entry(fat, cluster, 0x0FFF)

    root = bytearray(root_sectors * SECTOR_SIZE)
    root[0:32] = directory_entry(label.upper().ljust(11)[:11].encode("ascii"),
                                 0x08, 0)
    root[32:64] = directory_entry(b"README  TXT", 0x20, 2, len(readme))
    root[64:96] = directory_entry(b"DOCS       ", 0x10, 3)
    docs = bytearray(SECTOR_SIZE)
    docs[0:32] = directory_entry(b".          ", 0x10, 3)
    docs[32:64] = directory_entry(b"..         ", 0x10, 0)
    docs[64:96] = directory_entry(b"SAMPLE  TXT", 0x20, 4, len(sample))

    write_sector(image, start_lba, boot)
    write_at(image, (start_lba + reserved) * SECTOR_SIZE, fat)
    write_at(image, (start_lba + reserved + sectors_per_fat) * SECTOR_SIZE,
             fat)
    write_at(image, (start_lba + reserved + fat_count * sectors_per_fat) *
             SECTOR_SIZE, root)
    write_at(image, (start_lba + data_start) * SECTOR_SIZE, readme)
    write_sector(image, start_lba + data_start + 1, docs)
    write_at(image, (start_lba + data_start + 2) * SECTOR_SIZE, sample)


def set_fat32_entry(fat: bytearray, cluster: int, value: int) -> None:
    put_u32(fat, cluster * 4, value)


def lfn_checksum(alias: bytes) -> int:
    checksum = 0
    for value in alias:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + value) & 0xFF
    return checksum


def lfn_entry(sequence: int, name: str, checksum: int) -> bytes:
    units = list(struct.unpack("<%dH" % len(name),
                               name.encode("utf-16le")))
    entry = bytearray(32)
    entry[0] = sequence
    entry[11] = 0x0F
    entry[13] = checksum
    positions = ((1, 5), (14, 6), (28, 2))
    start = ((sequence & 0x1F) - 1) * 13
    values = units[start:start + 13]
    if len(values) < 13:
        values.append(0)
    while len(values) < 13:
        values.append(0xFFFF)
    for position, count in positions:
        for index in range(count):
            put_u16(entry, position + index * 2, values.pop(0))
    return bytes(entry)


def write_fat32(image, start_lba: int, total_sectors: int,
                label: str) -> None:
    reserved = 32
    fat_count = 2
    sectors_per_fat = 800
    data_start = reserved + fat_count * sectors_per_fat
    readme = ("ZephyrOS EP2 fixture FAT32\r\n"
              "Navegacao adicional somente leitura.\r\n").encode("ascii")
    sample = b"Diretorio FAT32 validado pela EP2.\r\n"
    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"ZEPHYR2 "
    put_u16(boot, 11, SECTOR_SIZE)
    boot[13] = 1
    put_u16(boot, 14, reserved)
    boot[16] = fat_count
    put_u16(boot, 17, 0)
    put_u16(boot, 19, 0)
    boot[21] = 0xF8
    put_u16(boot, 22, 0)
    put_u16(boot, 24, 63)
    put_u16(boot, 26, 16)
    put_u32(boot, 28, start_lba)
    put_u32(boot, 32, total_sectors)
    put_u32(boot, 36, sectors_per_fat)
    put_u16(boot, 40, 0)
    put_u16(boot, 42, 0)
    put_u32(boot, 44, 2)
    put_u16(boot, 48, 1)
    put_u16(boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    put_u32(boot, 67, 0x45503232)
    boot[71:82] = label.upper().ljust(11)[:11].encode("ascii")
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"

    fsinfo = bytearray(SECTOR_SIZE)
    put_u32(fsinfo, 0, 0x41615252)
    put_u32(fsinfo, 484, 0x61417272)
    put_u32(fsinfo, 488, 0xFFFFFFFF)
    put_u32(fsinfo, 492, 6)
    put_u32(fsinfo, 508, 0xAA550000)
    fat = bytearray(sectors_per_fat * SECTOR_SIZE)
    set_fat32_entry(fat, 0, 0x0FFFFFF8)
    set_fat32_entry(fat, 1, 0x0FFFFFFF)
    for cluster in (2, 3, 4, 5, 6):
        set_fat32_entry(fat, cluster, 0x0FFFFFFF)

    root = bytearray(SECTOR_SIZE)
    long_name = "Dados de Sistema.txt"
    long_alias = b"DADOSD~1TXT"
    checksum = lfn_checksum(long_alias)
    root[0:32] = directory_entry(label.upper().ljust(11)[:11].encode("ascii"),
                                 0x08, 0)
    root[32:64] = lfn_entry(0x42, long_name, checksum)
    root[64:96] = lfn_entry(1, long_name, checksum)
    root[96:128] = directory_entry(long_alias, 0x20, 6, len(sample))
    root[128:160] = directory_entry(b"README  TXT", 0x20, 4, len(readme))
    root[160:192] = directory_entry(b"DOCS       ", 0x10, 3)
    docs = bytearray(SECTOR_SIZE)
    docs[0:32] = directory_entry(b".          ", 0x10, 3)
    docs[32:64] = directory_entry(b"..         ", 0x10, 2)
    docs[64:96] = directory_entry(b"SAMPLE  TXT", 0x20, 5, len(sample))

    write_sector(image, start_lba, boot)
    write_sector(image, start_lba + 1, fsinfo)
    write_sector(image, start_lba + 6, boot)
    write_sector(image, start_lba + 7, fsinfo)
    write_at(image, (start_lba + reserved) * SECTOR_SIZE, fat)
    write_at(image, (start_lba + reserved + sectors_per_fat) * SECTOR_SIZE,
             fat)
    write_sector(image, start_lba + data_start, root)
    write_sector(image, start_lba + data_start + 1, docs)
    write_at(image, (start_lba + data_start + 2) * SECTOR_SIZE, readme)
    write_at(image, (start_lba + data_start + 3) * SECTOR_SIZE, sample)
    write_at(image, (start_lba + data_start + 4) * SECTOR_SIZE, sample)


def mbr_entry(bootable: bool, partition_type: int, start_lba: int,
              sector_count: int) -> bytes:
    entry = bytearray(16)
    entry[0] = 0x80 if bootable else 0
    entry[4] = partition_type
    put_u32(entry, 8, start_lba)
    put_u32(entry, 12, sector_count)
    return bytes(entry)


def write_mbr(image, entries: list[bytes]) -> None:
    mbr = bytearray(SECTOR_SIZE)
    for index, entry in enumerate(entries[:4]):
        mbr[446 + index * 16:446 + (index + 1) * 16] = entry
    mbr[510:512] = b"\x55\xAA"
    write_sector(image, 0, mbr)


def generate_valid(path: Path) -> None:
    partitions = (
        (2_048, 2_880, 0x01, "EP2FAT12A", "ata1p1"),
        (6_144, 2_880, 0x01, "EP2FAT12B", "ata1p2"),
        (10_240, 2_880, 0x01, "EP2FAT12C", "ata1p3"),
        (16_384, 100_000, 0x0C, "EP2FAT32", "ata1p4"),
    )
    make_image(path, VALID_SECTORS)
    with path.open("r+b") as image:
        write_mbr(image, [
            mbr_entry(index == 0, partition_type, start, count)
            for index, (start, count, partition_type, _, _) in
            enumerate(partitions)
        ])
        for start, count, partition_type, label, marker in partitions:
            if partition_type == 0x01:
                write_fat12(image, start, count, label, marker)
            else:
                write_fat32(image, start, count, label)


def generate_corrupt(path: Path) -> None:
    make_image(path, SMALL_SECTORS)
    entries = [
        mbr_entry(True, 0x01, 2_048, 2_048),
        mbr_entry(False, 0x0C, 6_000, 3_000),
        mbr_entry(False, 0x0C, 7_000, 3_000),
        mbr_entry(False, 0x01, 16_000, 1_000),
    ]
    corrupt_bpb = bytearray(SECTOR_SIZE)
    corrupt_bpb[0:3] = b"\xEB\x3C\x90"
    put_u16(corrupt_bpb, 11, 1_024)
    corrupt_bpb[13] = 3
    put_u16(corrupt_bpb, 14, 1)
    corrupt_bpb[16] = 2
    put_u16(corrupt_bpb, 17, 224)
    put_u16(corrupt_bpb, 19, 2_048)
    put_u16(corrupt_bpb, 22, 9)
    corrupt_bpb[510:512] = b"\x55\xAA"
    with path.open("r+b") as image:
        write_mbr(image, entries)
        write_sector(image, 2_048, corrupt_bpb)


def generate_unknown(path: Path) -> None:
    make_image(path, SMALL_SECTORS)
    with path.open("r+b") as image:
        write_mbr(image, [mbr_entry(False, 0x83, 2_048, 4_096)])


def copy_valid_fixture(valid: Path, target: Path) -> None:
    shutil.copyfile(valid, target)


def mutate_fat32_fixtures(output_dir: Path) -> None:
    valid = output_dir / FIXTURES[0]
    no_space = output_dir / FIXTURES[3]
    fat_divergent = output_dir / FIXTURES[4]
    chain_corrupt = output_dir / FIXTURES[5]
    lfn_invalid = output_dir / FIXTURES[6]

    copy_valid_fixture(valid, no_space)
    with no_space.open("r+b") as image:
        fat = bytearray(FAT32_SECTORS_PER_FAT * SECTOR_SIZE)
        image.seek((FAT32_START_LBA + FAT32_RESERVED) * SECTOR_SIZE)
        fat[:] = image.read(len(fat))
        for cluster in range(2, FAT32_CLUSTER_COUNT + 2):
            set_fat32_entry(fat, cluster, 0x0FFFFFFF)
        for copy in range(2):
            image.seek((FAT32_START_LBA + FAT32_RESERVED +
                        copy * FAT32_SECTORS_PER_FAT) * SECTOR_SIZE)
            image.write(fat)

    copy_valid_fixture(valid, fat_divergent)
    with fat_divergent.open("r+b") as image:
        offset = ((FAT32_START_LBA + FAT32_RESERVED +
                   FAT32_SECTORS_PER_FAT) * SECTOR_SIZE) + 6 * 4
        image.seek(offset)
        image.write(struct.pack("<I", 0x0FFFFFF6))

    copy_valid_fixture(valid, chain_corrupt)
    with chain_corrupt.open("r+b") as image:
        for copy in range(2):
            offset = ((FAT32_START_LBA + FAT32_RESERVED +
                       copy * FAT32_SECTORS_PER_FAT) * SECTOR_SIZE) + 6 * 4
            image.seek(offset)
            image.write(struct.pack("<I", 0x0FFFFFF7))

    copy_valid_fixture(valid, lfn_invalid)
    with lfn_invalid.open("r+b") as image:
        image.seek(FAT32_ROOT_LBA * SECTOR_SIZE + 32 + 13)
        checksum = image.read(1)
        image.seek(FAT32_ROOT_LBA * SECTOR_SIZE + 32 + 13)
        image.write(bytes([checksum[0] ^ 0x01]))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def read_sector_file(path: Path, lba: int) -> bytes:
    with path.open("rb") as image:
        image.seek(lba * SECTOR_SIZE)
        data = image.read(SECTOR_SIZE)
    if len(data) != SECTOR_SIZE:
        raise RuntimeError(f"setor {lba} ausente em {path.name}")
    return data


def inspect_layout(output_dir: Path) -> None:
    valid = output_dir / FIXTURES[0]
    valid_mbr = read_sector_file(valid, 0)
    if valid_mbr[510:512] != b"\x55\xAA":
        raise RuntimeError("assinatura MBR valida ausente")
    expected = ((0x01, 2_048, 2_880), (0x01, 6_144, 2_880),
                (0x01, 10_240, 2_880), (0x0C, 16_384, 100_000))
    for index, (partition_type, start, count) in enumerate(expected):
        entry = valid_mbr[446 + index * 16:446 + (index + 1) * 16]
        actual = (entry[4], struct.unpack_from("<I", entry, 8)[0],
                  struct.unpack_from("<I", entry, 12)[0])
        if actual != (partition_type, start, count):
            raise RuntimeError(f"particao valida {index + 1} divergente")
        bpb = read_sector_file(valid, start)
        if bpb[510:512] != b"\x55\xAA" or bpb[13] != 1:
            raise RuntimeError(f"BPB valido {index + 1} divergente")
    fat32_boot = read_sector_file(valid, 16_384)
    fat32_backup_boot = read_sector_file(valid, 16_390)
    fat32_fsinfo = read_sector_file(valid, 16_385)
    if fat32_boot[71:82].rstrip(b" ") != b"EP2FAT32":
        raise RuntimeError("label FAT32 da fixture divergente")
    if fat32_boot != fat32_backup_boot:
        raise RuntimeError("boot sector FAT32 de backup divergente")
    if fat32_fsinfo != read_sector_file(valid, 16_391):
        raise RuntimeError("FSInfo FAT32 de backup divergente")
    fat32_root = read_sector_file(valid, 18_016)
    if (fat32_root[32 + 11] != 0x0F or fat32_root[64 + 11] != 0x0F or
            fat32_root[96:107] != b"DADOSD~1TXT"):
        raise RuntimeError("LFN FAT32 da fixture ausente")
    corrupt = output_dir / FIXTURES[1]
    corrupt_bpb = read_sector_file(corrupt, 2_048)
    if struct.unpack_from("<H", corrupt_bpb, 11)[0] != 1_024:
        raise RuntimeError("BPB corrompido perdeu bytes/setor invalido")
    unknown_mbr = read_sector_file(output_dir / FIXTURES[2], 0)
    if unknown_mbr[450] != 0x83:
        raise RuntimeError("fixture desconhecida perdeu tipo 0x83")


def generate(output_dir: Path) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)
    generate_valid(output_dir / FIXTURES[0])
    generate_corrupt(output_dir / FIXTURES[1])
    generate_unknown(output_dir / FIXTURES[2])
    mutate_fat32_fixtures(output_dir)
    records = {}
    purposes = (
        "MBR com tres FAT12 e uma FAT32 validas",
        "BPB invalido, sobreposicao e limite fora do disco",
        "particao com filesystem desconhecido",
        "FAT32 sem clusters livres para escrita",
        "copias FAT32 divergentes",
        "cadeia FAT32 apontando para cluster ruim",
        "checksum de entrada LFN FAT32 invalido",
    )
    for name, purpose in zip(FIXTURES, purposes):
        path = output_dir / name
        records[name] = {
            "bytes": path.stat().st_size,
            "purpose": purpose,
            "sha256": sha256(path),
        }
    manifest = {"format": 1, "sector_size": SECTOR_SIZE,
                "fixtures": records}
    (output_dir / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / STAMP_NAME).write_text("storage-fixtures-v1\n",
                                         encoding="ascii")
    return manifest


def verify(output_dir: Path, quiet: bool = False) -> bool:
    manifest_path = output_dir / MANIFEST_NAME
    if not manifest_path.is_file():
        if not quiet:
            print(f"erro: manifesto ausente: {manifest_path}")
        return False
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    ok = True
    for name, record in manifest.get("fixtures", {}).items():
        path = output_dir / name
        actual_size = path.stat().st_size if path.is_file() else -1
        actual_hash = sha256(path) if path.is_file() else "ausente"
        matches = (actual_size == record["bytes"] and
                   actual_hash == record["sha256"])
        ok = ok and matches
        if not quiet:
            state = "OK" if matches else "ALTERADO"
            print(f"{name}: {state} sha256={actual_hash}")
    return ok


def selftest() -> None:
    with tempfile.TemporaryDirectory(prefix="zephyros-storage-") as temp:
        output_dir = Path(temp)
        first = generate(output_dir)
        inspect_layout(output_dir)
        if not verify(output_dir, quiet=True):
            raise RuntimeError("fixtures recem-geradas nao passaram na verificacao")
        second = generate(output_dir)
        if first != second:
            raise RuntimeError("geracao de fixtures nao e deterministica")
        valid = output_dir / FIXTURES[0]
        with valid.open("r+b") as image:
            image.seek(SECTOR_SIZE * 2_048 + 100)
            image.write(b"X")
        if verify(output_dir, quiet=True):
            raise RuntimeError("verificacao nao detectou alteracao da imagem")
    print("Storage fixtures: self-test OK")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("generate", "verify"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--output-dir", type=Path, default=Path("build"))
    subparsers.add_parser("selftest")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "selftest":
        selftest()
        return 0
    if args.command == "generate":
        manifest = generate(args.output_dir)
        for name, record in manifest["fixtures"].items():
            print(f"{name}: sha256={record['sha256']}")
        return 0
    return 0 if verify(args.output_dir) else 1


if __name__ == "__main__":
    raise SystemExit(main())
