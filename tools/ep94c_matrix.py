from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from ep94b_matrix import validate_geometry
from packager import HYBRID_FAT32_START_LBA, inject_fat32_bytes
from system_slots_matrix import (
    CONTROL_HASH_OFFSET,
    CONTROL_SIZE,
    SLOT_OFFSET,
    attempted_state,
    cache_record,
    corrupt,
    journal_record,
    pending_state,
    slot_record,
    state_record,
)


def inject(image: bytearray, path: str, data: bytes, start_lba: int) -> None:
    inject_fat32_bytes(
        data, image, path, replace=True, fat32_start_lba=start_lba
    )


def invalid_candidate_state(active: bytes, candidate: bytes) -> bytes:
    raw = bytearray(pending_state(active, candidate))
    raw[SLOT_OFFSET + 176] = 2
    raw[CONTROL_HASH_OFFSET:] = hashlib.sha256(
        raw[:CONTROL_HASH_OFFSET]
    ).digest()
    return bytes(raw)


def write_pair(directory: Path, name: str, first: bytes, second: bytes) -> None:
    case = directory / name
    case.mkdir(parents=True, exist_ok=True)
    (case / "ZSI0.STA").write_bytes(first)
    (case / "ZSI1.STA").write_bytes(second)


def create_preflight_fixtures(
    directory: Path, active: bytes, candidate: bytes
) -> None:
    ready = pending_state(active, candidate, sequence=2)
    no_pending = state_record(active, sequence=2)
    attempted = attempted_state(active, candidate)
    invalid = invalid_candidate_state(active, candidate)
    changed = pending_state(active, candidate, sequence=3)
    write_pair(directory, "READY", ready, ready)
    write_pair(directory, "NO_PENDING", no_pending, no_pending)
    write_pair(directory, "STATE_ONE_BAD", ready, corrupt(ready))
    write_pair(directory, "STATE_BOTH_BAD", corrupt(ready), corrupt(ready))
    write_pair(directory, "STATE_DIVERGENT", ready, changed)
    write_pair(directory, "ATTEMPTED", attempted, attempted)
    write_pair(directory, "CANDIDATE_INVALID", invalid, invalid)
    write_pair(directory, "SEQUENCE_CHANGED", ready, changed)
    journal = directory / "JOURNAL_PENDING"
    write_pair(directory, "JOURNAL_PENDING", ready, ready)
    (journal / "ZSI0.JRN").write_bytes(journal_record(candidate, 1))
    manifest = {
        "phase": "EP9.4C",
        "control_size": CONTROL_SIZE,
        "cases": [
            "READY",
            "NO_PENDING",
            "STATE_ONE_BAD",
            "STATE_BOTH_BAD",
            "STATE_DIVERGENT",
            "JOURNAL_PENDING",
            "ATTEMPTED",
            "CANDIDATE_INVALID",
            "SEQUENCE_CHANGED",
        ],
    }
    (directory / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="ascii"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Gera a sessao guiada unica e fixtures da EP9.4C"
    )
    parser.add_argument("--base-image", required=True)
    parser.add_argument("--active", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--fixtures-dir", required=True)
    parser.add_argument("--fat32-start-lba", type=int,
                        default=HYBRID_FAT32_START_LBA)
    args = parser.parse_args()

    output = Path(args.output)
    fixtures = Path(args.fixtures_dir)
    output.parent.mkdir(parents=True, exist_ok=True)
    fixtures.mkdir(parents=True, exist_ok=True)
    raw = bytearray(Path(args.base_image).read_bytes())
    validate_geometry(raw, args.fat32_start_lba)
    active_package = Path(args.active).read_bytes()
    candidate_package = Path(args.candidate).read_bytes()
    active = slot_record(active_package)
    candidate = slot_record(candidate_package)
    state = state_record(active)
    cache = cache_record(candidate_package)
    for path, data in (
        ("ZSA0.ZSY", active_package),
        ("ZSI0.STA", state),
        ("ZSI1.STA", state),
        ("ZSC0.ZSY", candidate_package),
        ("ZSC0.STA", cache),
        ("ZSC1.STA", cache),
    ):
        inject(raw, path, data, args.fat32_start_lba)
    validate_geometry(raw, args.fat32_start_lba)
    output.write_bytes(raw)
    create_preflight_fixtures(fixtures, active, candidate)
    print(f"Matriz EP9.4C criada: {output}")
    print(f"Fixtures de preflight EP9.4C: {fixtures}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
