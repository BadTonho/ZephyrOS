"""Gera imagens QEMU para a matriz de recuperação dos slots ZSYS."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

from packager import (
    FAT32_EOF,
    FAT32_FREE,
    HYBRID_FAT32_START_LBA,
    fat32_geometry,
    inject_fat32_file,
)


CONTROL_SIZE = 512
CONTROL_HASH_OFFSET = 480
STATE_MAGIC = b"ZSI1"
JOURNAL_MAGIC = b"ZSJ1"
FORMAT_VERSION = 2
STATE_SEQUENCE = 1
JOURNAL_SEQUENCE = 2
ACTIVE_SLOT = 0
PENDING_NONE = 0xFF
PENDING_SLOT_B = 1
FILE_EMPTY = 0
FILE_VALID = 1
SLOT_OFFSET = 32
SLOT_SIZE = 176
IMAGE_HEADER_SIZE = 1024
IMAGE_SIZE_OFFSET = 24
VERSION_OFFSET = 60
EPOCH_OFFSET = 66
RELEASE_ID_OFFSET = 214
RELEASE_TAG_OFFSET = 278
IDENTIFIER_SIZE = 64
PHASE_PREPARED = 1
PHASE_STAGING = 2
PHASE_VERIFIED = 3
PHASE_COMMITTED = 4
BOOT_ATTEMPTED = 1
STATE_A = "ZSI0.STA"
STATE_B = "ZSI1.STA"
JOURNAL_A = "ZSI0.JRN"
JOURNAL_B = "ZSI1.JRN"


def fixed_text(raw: bytes, label: str) -> bytes:
    end = raw.find(b"\0")
    if end <= 0 or any(raw[end + 1 :]):
        raise ValueError(f"{label} possui padding invalido")
    value = raw[:end]
    try:
        value.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"{label} nao usa ASCII") from error
    if len(value) >= IDENTIFIER_SIZE:
        raise ValueError(f"{label} excede o campo fixo")
    return value


def slot_record(package: bytes) -> bytes:
    if len(package) < IMAGE_HEADER_SIZE or package[:4] != b"ZSYS":
        raise ValueError("pacote ZSYS invalido")
    image_size = struct.unpack_from("<I", package, IMAGE_SIZE_OFFSET)[0]
    if len(package) != IMAGE_HEADER_SIZE + image_size or not image_size:
        raise ValueError("tamanho ZSYS inconsistente")
    version = struct.unpack_from("<HHH", package, VERSION_OFFSET)
    epoch = struct.unpack_from("<I", package, EPOCH_OFFSET)[0]
    release_id = fixed_text(
        package[RELEASE_ID_OFFSET : RELEASE_ID_OFFSET + IDENTIFIER_SIZE],
        "release_id",
    )
    release_tag = fixed_text(
        package[RELEASE_TAG_OFFSET : RELEASE_TAG_OFFSET + IDENTIFIER_SIZE],
        "release_tag",
    )
    raw = bytearray(SLOT_SIZE)
    raw[0] = FILE_VALID
    struct.pack_into("<HHH", raw, 2, *version)
    struct.pack_into("<I", raw, 8, epoch)
    struct.pack_into("<I", raw, 12, len(package))
    raw[16:48] = hashlib.sha256(package).digest()
    raw[48:112] = release_id + bytes(IDENTIFIER_SIZE - len(release_id))
    raw[112:176] = release_tag + bytes(IDENTIFIER_SIZE - len(release_tag))
    return bytes(raw)


def state_record(active: bytes, sequence: int = STATE_SEQUENCE) -> bytes:
    raw = bytearray(CONTROL_SIZE)
    raw[:4] = STATE_MAGIC
    struct.pack_into("<HHI", raw, 4, FORMAT_VERSION, CONTROL_SIZE, sequence)
    raw[12] = ACTIVE_SLOT
    raw[13] = PENDING_NONE
    raw[15] = ACTIVE_SLOT
    raw[16] = PENDING_NONE
    raw[SLOT_OFFSET : SLOT_OFFSET + SLOT_SIZE] = active
    raw[CONTROL_HASH_OFFSET:] = hashlib.sha256(raw[:CONTROL_HASH_OFFSET]).digest()
    return bytes(raw)


def pending_state(active: bytes, candidate: bytes, sequence: int = 2) -> bytes:
    raw = bytearray(state_record(active, sequence))
    raw[13] = PENDING_SLOT_B
    raw[SLOT_OFFSET + SLOT_SIZE : SLOT_OFFSET + 2 * SLOT_SIZE] = candidate
    raw[CONTROL_HASH_OFFSET:] = hashlib.sha256(raw[:CONTROL_HASH_OFFSET]).digest()
    return bytes(raw)


def attempted_state(active: bytes, candidate: bytes) -> bytes:
    raw = bytearray(pending_state(active, candidate, sequence=3))
    raw[16] = PENDING_SLOT_B
    raw[17] = BOOT_ATTEMPTED
    struct.pack_into("<I", raw, 20, 1)
    raw[CONTROL_HASH_OFFSET:] = hashlib.sha256(raw[:CONTROL_HASH_OFFSET]).digest()
    return bytes(raw)


def journal_record(
    candidate: bytes, phase: int, sequence: int = JOURNAL_SEQUENCE
) -> bytes:
    raw = bytearray(CONTROL_SIZE)
    raw[:4] = JOURNAL_MAGIC
    struct.pack_into("<HHI", raw, 4, FORMAT_VERSION, CONTROL_SIZE, sequence)
    raw[12] = phase
    raw[13] = 1
    raw[SLOT_OFFSET : SLOT_OFFSET + SLOT_SIZE] = candidate
    raw[CONTROL_HASH_OFFSET:] = hashlib.sha256(raw[:CONTROL_HASH_OFFSET]).digest()
    return bytes(raw)


def corrupt(raw: bytes) -> bytes:
    value = bytearray(raw)
    value[CONTROL_HASH_OFFSET] ^= 0x01
    return bytes(value)


def inject(image: Path, path: str, data: bytes, start_lba: int) -> None:
    inject_fat32_file(
        data,
        image,
        path,
        replace=True,
        fat32_start_lba=start_lba,
    )


def fill_free_clusters(image: Path, start_lba: int) -> None:
    data = bytearray(image.read_bytes())
    bps, _, reserved, fat_count, spf, _, clusters = fat32_geometry(
        data, start_lba
    )
    for copy in range(fat_count):
        offset = (start_lba + reserved + copy * spf) * bps
        fat = bytearray(data[offset : offset + spf * bps])
        for cluster in range(2, clusters + 2):
            entry = struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
            if entry == FAT32_FREE:
                struct.pack_into("<I", fat, cluster * 4, FAT32_EOF)
        data[offset : offset + len(fat)] = fat
    image.write_bytes(data)


def remove_system_volume(image: Path, start_lba: int) -> None:
    data = bytearray(image.read_bytes())
    bps = 512
    for sector in (start_lba, start_lba + 6):
        offset = sector * bps
        data[offset : offset + bps] = bytes(bps)
    image.write_bytes(data)


def make_case(
    base_image: Path,
    output_dir: Path,
    name: str,
    state_a: bytes,
    state_b: bytes,
    candidate_package: bytes,
    journal: tuple[bytes, bytes] | None = None,
    staging: bool = False,
    target: bool = False,
    fill_space: bool = False,
    no_volume: bool = False,
    start_lba: int = HYBRID_FAT32_START_LBA,
) -> None:
    image = output_dir / f"{name}.img"
    shutil.copy2(base_image, image)
    inject(image, STATE_A, state_a, start_lba)
    inject(image, STATE_B, state_b, start_lba)
    if journal:
        inject(image, JOURNAL_A, journal[0], start_lba)
        inject(image, JOURNAL_B, journal[1], start_lba)
    if staging:
        inject(image, "ZSTG.ZSY", candidate_package, start_lba)
    if target:
        inject(image, "ZSB0.ZSY", candidate_package, start_lba)
    if fill_space:
        fill_free_clusters(image, start_lba)
    if no_volume:
        remove_system_volume(image, start_lba)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Gera a matriz QEMU de recuperacao dos slots ZSYS"
    )
    parser.add_argument("--base-image", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--bad-signature", required=True)
    parser.add_argument("--bad-image-hash", required=True)
    parser.add_argument("--bad-component-hash", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--fat32-start-lba", type=int, default=HYBRID_FAT32_START_LBA)
    args = parser.parse_args()

    base_image = Path(args.base_image)
    baseline = Path(args.baseline).read_bytes()
    candidate_package = Path(args.candidate).read_bytes()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    active = slot_record(baseline)
    candidate = slot_record(candidate_package)
    bad_signature = Path(args.bad_signature).read_bytes()
    bad_image_hash = Path(args.bad_image_hash).read_bytes()
    bad_component_hash = Path(args.bad_component_hash).read_bytes()
    valid_state = state_record(active)
    empty_slot_state = valid_state
    valid_journal = journal_record(candidate, PHASE_PREPARED)
    cases: dict[str, dict[str, str]] = {}

    def add(name: str, expected: str, package: bytes = candidate_package,
            **kwargs: object) -> None:
        kwargs.pop("candidate", None)
        make_case(
            base_image,
            output_dir,
            name,
            start_lba=args.fat32_start_lba,
            candidate_package=package,
            **kwargs,
        )
        cases[name] = {"image": f"{name}.img", "expected": expected}

    add("STATE_ONE_BAD", "READY; A ativo", state_a=corrupt(valid_state),
        state_b=valid_state, candidate=candidate)
    add("STATE_BOTH_BAD", "DEGRADED", state_a=corrupt(valid_state),
        state_b=corrupt(valid_state), candidate=candidate)
    add("STATE_NEWER", "READY; sequencia 2", state_a=valid_state,
        state_b=state_record(active, sequence=2), candidate=candidate)
    add("JOURNAL_PREPARED", "READY; A preservado; journal limpo",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate, journal=(valid_journal, valid_journal), staging=True)
    add("JOURNAL_STAGING", "READY; A preservado; journal limpo",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate,
        journal=(journal_record(candidate, PHASE_STAGING),
                 journal_record(candidate, PHASE_STAGING)), staging=True)
    add("JOURNAL_VERIFIED", "READY; B pendente; journal limpo",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate,
        journal=(journal_record(candidate, PHASE_VERIFIED),
                 journal_record(candidate, PHASE_VERIFIED)), target=True)
    add("JOURNAL_COMMITTED", "READY; B pendente; journal limpo",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate,
        journal=(journal_record(candidate, PHASE_COMMITTED),
                 journal_record(candidate, PHASE_COMMITTED)), target=True)
    add("JOURNAL_NEWER", "READY; journal limpo",
        state_a=valid_state, state_b=empty_slot_state, candidate=candidate,
        journal=(journal_record(candidate, PHASE_PREPARED, sequence=2),
                 journal_record(candidate, PHASE_STAGING, sequence=3)),
        staging=True)
    add("JOURNAL_ONE_BAD", "READY; journal limpo",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate,
        journal=(corrupt(valid_journal), valid_journal), staging=True)
    add("JOURNAL_BOTH_BAD", "DEGRADED",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate,
        journal=(corrupt(valid_journal), corrupt(valid_journal)))
    add("NO_SPACE", "READY; preflight SPACE",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate, fill_space=True)
    add("NO_VOLUME", "DEGRADED ou indisponivel",
        state_a=valid_state, state_b=empty_slot_state,
        candidate=candidate, no_volume=True)
    pending = pending_state(active, candidate)
    add("BOOT_ACTIVE_VALID", "A autenticado inicia automaticamente",
        state_a=valid_state, state_b=valid_state, candidate=candidate)
    add("BOOT_PENDING_VALID", "B e tentado e aguarda confirmacao",
        state_a=pending, state_b=pending, candidate=candidate, target=True)
    add("BOOT_BAD_SIGNATURE", "Fallback legado autenticado",
        package=bad_signature,
        state_a=pending_state(active, slot_record(bad_signature)),
        state_b=pending_state(active, slot_record(bad_signature)), target=True)
    add("BOOT_BAD_IMAGE_HASH", "Fallback legado autenticado",
        package=bad_image_hash,
        state_a=pending_state(active, slot_record(bad_image_hash)),
        state_b=pending_state(active, slot_record(bad_image_hash)), target=True)
    add("BOOT_BAD_COMPONENT_HASH", "Fallback legado autenticado",
        package=bad_component_hash,
        state_a=pending_state(active, slot_record(bad_component_hash)),
        state_b=pending_state(active, slot_record(bad_component_hash)), target=True)
    interrupted = attempted_state(active, candidate)
    add("BOOT_ATTEMPT_INTERRUPTED", "B marcado FAILED; A preservado",
        state_a=interrupted, state_b=interrupted, candidate=candidate, target=True)
    (output_dir / "matrix.json").write_text(
        json.dumps(
            {
                "format": "zephyros-system-slots-matrix-v1",
                "cases": cases,
                "cancel": "Use SLOTS.img and press F12 during --confirm.",
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"Matriz de slots ZSYS criada em {output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
