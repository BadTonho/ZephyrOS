"""Gera estado inicial redundante para fixtures FAT32 de slots ZSYS."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


CONTROL_SIZE = 512
CONTROL_HASH_OFFSET = 480
STATE_MAGIC = b"ZSI1"
STATE_VERSION = 2
STATE_SEQUENCE = 1
STATE_ACTIVE_SLOT = 0
STATE_PENDING_NONE = 0xFF
STATE_FILE_VALID = 1
STATE_FILE_EMPTY = 0
STATE_SLOT_OFFSET = 32
STATE_SLOT_SIZE = 176
SYSTEM_HEADER_SIZE = 1024
SYSTEM_IMAGE_SIZE_OFFSET = 24
SYSTEM_TARGET_VERSION_OFFSET = 60
SYSTEM_TARGET_EPOCH_OFFSET = 66
SYSTEM_RELEASE_ID_OFFSET = 214
SYSTEM_RELEASE_TAG_OFFSET = 278
IDENTIFIER_SIZE = 64


def fixed_text(raw: bytes, label: str) -> str:
    end = raw.find(b"\0")
    if end <= 0 or any(raw[end + 1 :]):
        raise ValueError(f"{label} possui padding invalido")
    try:
        return raw[:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"{label} nao usa ASCII") from error


def slot_record(package: bytes) -> bytes:
    if len(package) < SYSTEM_HEADER_SIZE:
        raise ValueError("ZSYS baseline menor que o cabecalho")
    if package[:4] != b"ZSYS":
        raise ValueError("ZSYS baseline possui magic invalido")
    image_size = struct.unpack_from("<I", package, SYSTEM_IMAGE_SIZE_OFFSET)[0]
    if len(package) != SYSTEM_HEADER_SIZE + image_size or not image_size:
        raise ValueError("ZSYS baseline possui tamanho inconsistente")
    version = struct.unpack_from("<HHH", package, SYSTEM_TARGET_VERSION_OFFSET)
    epoch = struct.unpack_from("<I", package, SYSTEM_TARGET_EPOCH_OFFSET)[0]
    release_id = fixed_text(
        package[
            SYSTEM_RELEASE_ID_OFFSET :
            SYSTEM_RELEASE_ID_OFFSET + IDENTIFIER_SIZE
        ],
        "release_id",
    )
    release_tag = fixed_text(
        package[
            SYSTEM_RELEASE_TAG_OFFSET :
            SYSTEM_RELEASE_TAG_OFFSET + IDENTIFIER_SIZE
        ],
        "release_tag",
    )
    raw = bytearray(STATE_SLOT_SIZE)
    raw[0] = STATE_FILE_VALID
    struct.pack_into("<HHH", raw, 2, *version)
    struct.pack_into("<I", raw, 8, epoch)
    struct.pack_into("<I", raw, 12, len(package))
    raw[16:48] = hashlib.sha256(package).digest()
    raw[48:112] = release_id.encode("ascii") + bytes(
        IDENTIFIER_SIZE - len(release_id)
    )
    raw[112:176] = release_tag.encode("ascii") + bytes(
        IDENTIFIER_SIZE - len(release_tag)
    )
    return bytes(raw)


def empty_slot_record() -> bytes:
    return bytes(STATE_SLOT_SIZE)


def state_record(valid_slot: bytes) -> bytes:
    raw = bytearray(CONTROL_SIZE)
    raw[:4] = STATE_MAGIC
    struct.pack_into("<HHI", raw, 4, STATE_VERSION, CONTROL_SIZE, STATE_SEQUENCE)
    raw[12] = STATE_ACTIVE_SLOT
    raw[13] = STATE_PENDING_NONE
    raw[15] = STATE_ACTIVE_SLOT
    raw[16] = STATE_PENDING_NONE
    raw[STATE_SLOT_OFFSET : STATE_SLOT_OFFSET + STATE_SLOT_SIZE] = valid_slot
    raw[STATE_SLOT_OFFSET + STATE_SLOT_SIZE :
        STATE_SLOT_OFFSET + 2 * STATE_SLOT_SIZE] = empty_slot_record()
    raw[CONTROL_HASH_OFFSET:] = hashlib.sha256(raw[:CONTROL_HASH_OFFSET]).digest()
    return bytes(raw)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Gera ZSI0.STA/ZSI1.STA para fixture EP9.1"
    )
    parser.add_argument("--package", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    package_path = Path(args.package)
    output_dir = Path(args.output_dir)
    package = package_path.read_bytes()
    output_dir.mkdir(parents=True, exist_ok=True)
    state = state_record(slot_record(package))
    (output_dir / "ZSI0.STA").write_bytes(state)
    (output_dir / "ZSI1.STA").write_bytes(state)
    (output_dir / "fixture.json").write_text(
        '{\n'
        '  "format": "zephyros-system-slots-fixtures-v2",\n'
        '  "active_slot": "ZSA0.ZSY",\n'
        '  "pending_slot": null,\n'
        '  "state": ["ZSI0.STA", "ZSI1.STA"]\n'
        '}\n',
        encoding="utf-8",
    )
    print(f"Estado inicial de slots criado em {output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
