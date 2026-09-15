#!/usr/bin/env python3
"""Audita os artefatos internos da linha de base da RLS1."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "zephyros-rls1-baseline-v1"
SECTOR_SIZE = 512
EXPECTED_IMAGE_BYTES = 268435456
EXPECTED_KERNEL_LBA = 64
EXPECTED_RECOVERY_LBA = 6144
EXPECTED_FAT32_LBA = 8192
DEFAULT_BUILD_VERSION = "0.1.0"
DEFAULT_TARGET_VERSION = "1.0.0"
VERSION_TEXT_RE = re.compile(
    r'#define\s+ZEPHYROS_VERSION_TEXT\s+"([^\"]+)"')
VERSION_PART_RE = re.compile(
    r'#define\s+ZEPHYROS_VERSION_(MAJOR|MINOR|PATCH)\s+([0-9]+)U?')
NM_LINE_RE = re.compile(r"^([0-9A-Fa-f]+)\s+([^\s])\s+(.+?)\s*$")
OBJDUMP_SECTION_RE = re.compile(
    r"^\s*(\d+)\s+(\S+)\s+([0-9A-Fa-f]+)\s+"
    r"([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(.+?)\s*$")


class BaselineError(Exception):
    """Falha auditável da linha de base."""

    def __init__(self, code: str, status: str = "FAIL") -> None:
        super().__init__(code)
        self.code = code
        self.status = status


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise BaselineError(f"artifact_read:{path.name}") from error
    return digest.hexdigest()


def repo_relative(path: Path, root: Path) -> str:
    root_resolved = root.resolve()
    path_resolved = path.resolve()
    try:
        return path_resolved.relative_to(root_resolved).as_posix()
    except ValueError as error:
        raise BaselineError("path_outside_repository") from error


def artifact_record(path: Path, root: Path) -> dict[str, Any]:
    relative = repo_relative(path, root)
    if not path.is_file():
        raise BaselineError(f"artifact_missing:{relative}")
    try:
        size = path.stat().st_size
    except OSError as error:
        raise BaselineError(f"artifact_stat:{relative}") from error
    return {"path": relative, "size_bytes": size, "sha256": sha256_file(path)}


def validate_boot_artifact(path: Path) -> None:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise BaselineError("boot_artifact_missing") from error
    if len(data) != SECTOR_SIZE:
        raise BaselineError("boot_size_invalid")
    if data[510:512] != b"\x55\xAA":
        raise BaselineError("boot_signature_invalid")


def version_from_header(text: str) -> str:
    text_match = VERSION_TEXT_RE.search(text)
    parts = {name: value for name, value in VERSION_PART_RE.findall(text)}
    if not text_match or set(parts) != {"MAJOR", "MINOR", "PATCH"}:
        raise BaselineError("version_header_invalid")
    version = text_match.group(1)
    numeric = ".".join(parts[name] for name in ("MAJOR", "MINOR", "PATCH"))
    if version != numeric:
        raise BaselineError("version_header_inconsistent")
    return version


def ceil_sectors(size_bytes: int) -> int:
    if size_bytes <= 0:
        raise BaselineError("artifact_empty")
    return math.ceil(size_bytes / SECTOR_SIZE)


def build_layout(artifacts: dict[str, dict[str, Any]], image_size: int,
                 kernel_lba: int = EXPECTED_KERNEL_LBA,
                 recovery_lba: int = EXPECTED_RECOVERY_LBA,
                 fat32_lba: int = EXPECTED_FAT32_LBA) -> dict[str, Any]:
    total_sectors, remainder = divmod(image_size, SECTOR_SIZE)
    if remainder:
        raise BaselineError("image_not_sector_aligned")
    required = ("boot.bin", "stage2.bin", "kernel.bin",
                "recovery_loader_padded.bin")
    if any(name not in artifacts for name in required):
        raise BaselineError("layout_artifact_missing")
    windows = [
        {"name": "boot", "start_lba": 0,
         "sectors": ceil_sectors(artifacts["boot.bin"]["size_bytes"])},
        {"name": "stage2", "start_lba": 1,
         "sectors": ceil_sectors(artifacts["stage2.bin"]["size_bytes"])},
        {"name": "kernel", "start_lba": kernel_lba,
         "sectors": ceil_sectors(artifacts["kernel.bin"]["size_bytes"])},
        {"name": "recovery_loader", "start_lba": recovery_lba,
         "sectors": ceil_sectors(
             artifacts["recovery_loader_padded.bin"]["size_bytes"])},
        {"name": "fat32", "start_lba": fat32_lba,
         "sectors": total_sectors - fat32_lba},
    ]
    for window in windows:
        window["end_lba"] = window["start_lba"] + window["sectors"] - 1
        if window["start_lba"] < 0 or window["sectors"] <= 0 or \
                window["end_lba"] >= total_sectors:
            raise BaselineError("layout_window_out_of_image")
    for index, left in enumerate(windows):
        for right in windows[index + 1:]:
            left_end = left["start_lba"] + left["sectors"]
            right_end = right["start_lba"] + right["sectors"]
            if left["start_lba"] < right_end and right["start_lba"] < left_end:
                raise BaselineError(
                    f"layout_overlap:{left['name']}:{right['name']}")
    if windows[0]["sectors"] != 1:
        raise BaselineError("boot_not_one_sector")
    return {
        "sector_size": SECTOR_SIZE,
        "total_sectors": total_sectors,
        "windows": windows,
        "kernel_lba": kernel_lba,
        "recovery_lba": recovery_lba,
        "fat32_lba": fat32_lba,
        "overlap": False,
    }


def parse_nm_listing(text: str) -> list[dict[str, Any]]:
    symbols: list[dict[str, Any]] = []
    for line in text.splitlines():
        match = NM_LINE_RE.fullmatch(line.strip())
        if not match or match.group(2).upper() in {"U", "?"}:
            continue
        symbols.append({
            "address": int(match.group(1), 16),
            "type": match.group(2),
            "symbol": match.group(3),
        })
    return sorted(symbols, key=lambda item: (item["address"], item["symbol"]))


def parse_objdump_sections(text: str) -> list[dict[str, Any]]:
    sections: list[dict[str, Any]] = []
    for line in text.splitlines():
        match = OBJDUMP_SECTION_RE.fullmatch(line)
        if not match:
            continue
        alignment = match.group(7).strip()
        sections.append({
            "index": int(match.group(1)),
            "name": match.group(2),
            "size": int(match.group(3), 16),
            "vma": int(match.group(4), 16),
            "lma": int(match.group(5), 16),
            "file_offset": int(match.group(6), 16),
            "alignment": alignment,
        })
    return sections


def normalize_tool_version(text: str) -> str:
    for line in text.splitlines():
        value = line.strip()
        if value:
            return value[:240]
    return "ND"


def status_from_errors(errors: Iterable[str], blocked: bool = False) -> str:
    return "BLOCKED" if blocked else ("FAIL" if list(errors) else "PASS")


def contains_absolute_path(value: Any) -> bool:
    if isinstance(value, dict):
        return any(contains_absolute_path(item) for item in value.values())
    if isinstance(value, (list, tuple)):
        return any(contains_absolute_path(item) for item in value)
    if not isinstance(value, str):
        return False
    return bool(re.search(r"(?:[A-Za-z]:[\\/]|^/)", value))


def display_command(command: str) -> str:
    return Path(normalize_command(command).replace("\\", "/")).name or command


def normalize_command(command: str) -> str:
    return command.strip().strip('"')


def command_available(command: str) -> bool:
    command = normalize_command(command)
    return Path(command).is_file() or shutil.which(command) is not None


def run_tool_version(command: str, arguments: list[str]) -> str:
    command = normalize_command(command)
    if not command_available(command):
        raise BaselineError(f"tool_missing:{display_command(command)}", "BLOCKED")
    try:
        result = subprocess.run(
            [command, *arguments], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BaselineError(f"tool_unavailable:{display_command(command)}",
                            "BLOCKED") from error
    if result.returncode != 0:
        raise BaselineError(f"tool_version_failed:{display_command(command)}",
                            "BLOCKED")
    return normalize_tool_version(result.stdout or result.stderr)


def run_listing(command: str, arguments: list[str], label: str) -> str:
    command = normalize_command(command)
    if not command_available(command):
        raise BaselineError(f"tool_missing:{display_command(command)}", "BLOCKED")
    try:
        result = subprocess.run(
            [command, *arguments], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BaselineError(f"{label}_tool_unavailable", "BLOCKED") from error
    if result.returncode != 0:
        raise BaselineError(f"{label}_failed")
    return result.stdout


def read_at(path: Path, offset: int, size: int) -> bytes:
    try:
        with path.open("rb") as stream:
            stream.seek(offset)
            data = stream.read(size)
    except OSError as error:
        raise BaselineError(f"image_read:{path.name}") from error
    if len(data) != size:
        raise BaselineError("image_truncated")
    return data


def validate_fat32(image_path: Path, start_lba: int,
                   image_size: int) -> dict[str, Any]:
    base = start_lba * SECTOR_SIZE
    sector = read_at(image_path, base, SECTOR_SIZE)
    if sector[510:512] != b"\x55\xAA":
        raise BaselineError("fat32_boot_signature_invalid")
    bytes_per_sector = struct.unpack_from("<H", sector, 11)[0]
    sectors_per_cluster = sector[13]
    reserved = struct.unpack_from("<H", sector, 14)[0]
    fat_count = sector[16]
    total = struct.unpack_from("<I", sector, 32)[0]
    sectors_per_fat = struct.unpack_from("<I", sector, 36)[0]
    root_cluster = struct.unpack_from("<I", sector, 44)[0]
    if (bytes_per_sector != SECTOR_SIZE or sectors_per_cluster == 0 or
            reserved == 0 or fat_count == 0 or sectors_per_fat == 0 or
            total == 0):
        raise BaselineError("fat32_bpb_invalid")
    image_sectors = image_size // SECTOR_SIZE
    if (start_lba + total > image_sectors or
            total != image_sectors - start_lba):
        raise BaselineError("fat32_partition_size_invalid")
    data_start = reserved + fat_count * sectors_per_fat
    clusters = (total - data_start) // sectors_per_cluster
    if data_start >= total or clusters < 4086 or root_cluster < 2 or \
            root_cluster >= clusters + 2:
        raise BaselineError("fat32_geometry_invalid")
    if sectors_per_fat * (SECTOR_SIZE // 4) < clusters + 2:
        raise BaselineError("fat32_capacity_invalid")
    if sector[82:87] != b"FAT32":
        raise BaselineError("fat32_type_invalid")
    return {
        "start_lba": start_lba,
        "total_sectors": total,
        "bytes_per_sector": bytes_per_sector,
        "sectors_per_cluster": sectors_per_cluster,
        "reserved_sectors": reserved,
        "fat_count": fat_count,
        "sectors_per_fat": sectors_per_fat,
        "root_cluster": root_cluster,
        "data_start_sector": data_start,
        "clusters": clusters,
    }


def validate_image(image_path: Path, artifacts: dict[str, Path],
                   image_size: int, layout: dict[str, Any]) -> dict[str, Any]:
    if image_path.stat().st_size != image_size:
        raise BaselineError("image_size_invalid")
    boot = artifacts["boot.bin"].read_bytes()
    validate_boot_artifact(artifacts["boot.bin"])
    expected_boot = bytearray(boot)
    legacy_reserved = layout["windows"][1]["end_lba"] + 1
    struct.pack_into("<H", expected_boot, 14, legacy_reserved)
    if read_at(image_path, 0, 446) != bytes(expected_boot[:446]) or \
            read_at(image_path, 510, 2) != boot[510:512]:
        raise BaselineError("image_boot_mismatch")
    if read_at(image_path, 512, artifacts["stage2.bin"].stat().st_size) != \
            artifacts["stage2.bin"].read_bytes():
        raise BaselineError("image_stage2_mismatch")
    for name, lba in (("kernel.bin", EXPECTED_KERNEL_LBA),
                      ("recovery_loader_padded.bin", EXPECTED_RECOVERY_LBA)):
        data = artifacts[name].read_bytes()
        if read_at(image_path, lba * SECTOR_SIZE, len(data)) != data:
            raise BaselineError(f"image_{name.replace('.', '_')}_mismatch")
    if read_at(image_path, 510, 2) != b"\x55\xAA":
        raise BaselineError("mbr_signature_invalid")
    entry = read_at(image_path, 446, 16)
    partition_type = entry[4]
    partition_start = struct.unpack_from("<I", entry, 8)[0]
    partition_sectors = struct.unpack_from("<I", entry, 12)[0]
    expected_sectors = image_size // SECTOR_SIZE - EXPECTED_FAT32_LBA
    if (partition_type != 0x0C or partition_start != EXPECTED_FAT32_LBA or
            partition_sectors != expected_sectors):
        raise BaselineError("fat32_partition_entry_invalid")
    fat32 = validate_fat32(image_path, EXPECTED_FAT32_LBA, image_size)
    return {
        "size_bytes": image_size,
        "sector_size": SECTOR_SIZE,
        "mbr_signature": True,
        "fat32": fat32,
        "layout_windows_valid": layout["overlap"] is False,
    }


def git_source(root: Path) -> dict[str, Any]:
    def git(args: list[str]) -> str:
        try:
            result = subprocess.run(
                ["git", *args], cwd=root, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=10, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise BaselineError("git_unavailable", "BLOCKED") from error
        if result.returncode != 0:
            raise BaselineError("git_query_failed")
        return result.stdout.strip()

    status = git(["status", "--porcelain", "--untracked-files=all"])
    return {
        "commit": git(["rev-parse", "HEAD"]),
        "branch": git(["branch", "--show-current"]) or "DETACHED",
        "worktree_clean": not bool(status),
        "worktree_entry_count": len(status.splitlines()) if status else 0,
    }


def tool_versions(arguments: argparse.Namespace) -> dict[str, Any]:
    specifications = {
        "python": (arguments.python, ["--version"]),
        "make": (arguments.make, ["--version"]),
        "nasm": (arguments.nasm, ["-v"]),
        "gcc": (arguments.gcc, ["--version"]),
        "ld": (arguments.ld, ["--version"]),
        "nm": (arguments.nm, ["--version"]),
        "objdump": (arguments.objdump, ["--version"]),
    }
    result: dict[str, Any] = {}
    for name, (command, flags) in specifications.items():
        result[name] = {
            "command": display_command(command),
            "version": run_tool_version(command, flags),
        }
    return result


def updater_audit(root: Path, image: Path, build_version: str) -> dict[str, Any]:
    try:
        result = subprocess.run(
            [sys.executable, str(root / "tools" / "updater.py"),
             "audit-image", "--image", str(image),
             "--expect-version", build_version],
            cwd=root, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=60, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BaselineError("updater_audit_unavailable", "BLOCKED") from error
    return {
        "status": "PASS" if result.returncode == 0 else "FAIL",
        "returncode": result.returncode,
        "output_line_count": len((result.stdout or "").splitlines()),
    }


def symbols_report(root: Path, elf: Path, nm: str, objdump: str,
                   output_dir: Path) -> dict[str, Any]:
    if elf.read_bytes()[:4] != b"\x7fELF":
        raise BaselineError("elf_header_invalid")
    symbols_text = run_listing(nm, ["-n", str(elf)], "nm")
    sections_text = run_listing(objdump, ["-h", str(elf)], "objdump")
    symbols = parse_nm_listing(symbols_text)
    sections = parse_objdump_sections(sections_text)
    if not symbols:
        raise BaselineError("elf_symbols_invalid")
    if not sections:
        raise BaselineError("elf_sections_invalid")
    output_dir.mkdir(parents=True, exist_ok=True)
    symbols_path = output_dir / "symbols.txt"
    sections_path = output_dir / "objdump-sections.txt"
    symbols_path.write_text(symbols_text, encoding="utf-8")
    sections_path.write_text(sections_text, encoding="utf-8")
    return {
        "format": "ELF",
        "symbols": {
            "count": len(symbols),
            "listing_sha256": hashlib.sha256(
                symbols_text.replace("\r\n", "\n").encode("utf-8")).hexdigest(),
            "listing": repo_relative(symbols_path, root),
        },
        "sections": {
            "count": len(sections),
            "listing_sha256": hashlib.sha256(
                sections_text.replace("\r\n", "\n").encode("utf-8")).hexdigest(),
            "listing": repo_relative(sections_path, root),
            "table": sections,
        },
    }


def base_report() -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "version": 1,
        "status": "FAIL",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "build_version": DEFAULT_BUILD_VERSION,
        "target_version": DEFAULT_TARGET_VERSION,
        "candidate_state": "DOCUMENTAL_ONLY",
        "errors": [],
        "checks": {},
    }


def audit(arguments: argparse.Namespace) -> dict[str, Any]:
    root = Path(arguments.repo_root).resolve()
    output_path = Path(arguments.output)
    if not output_path.is_absolute():
        output_path = root / output_path
    output_path = output_path.resolve()
    report = base_report()
    errors: list[str] = []
    blocked = False
    try:
        report["source"] = git_source(root)
        if not report["source"]["worktree_clean"]:
            errors.append("worktree_dirty")
        report["checks"]["worktree_clean"] = report["source"]["worktree_clean"]
    except BaselineError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"

    version_path = root / arguments.version_header
    try:
        version = version_from_header(version_path.read_text(encoding="utf-8"))
        report["build_version"] = version
        report["checks"]["build_version"] = version == arguments.build_version
        if version != arguments.build_version:
            errors.append("build_version_mismatch")
    except (OSError, BaselineError) as error:
        errors.append(error.code if isinstance(error, BaselineError)
                      else "version_header_missing")

    report["target_version"] = arguments.target_version
    report["checks"]["version_distinct"] = (
        report["build_version"] != arguments.target_version)
    if arguments.target_version == report["build_version"]:
        errors.append("target_version_not_distinct")

    try:
        report["tools"] = tool_versions(arguments)
    except BaselineError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"

    artifact_paths = {
        "boot.bin": root / arguments.boot,
        "stage2.bin": root / arguments.stage2,
        "kernel.bin": root / arguments.kernel,
        "recovery_loader.bin": root / arguments.recovery_loader,
        "recovery_loader_padded.bin": root / arguments.recovery_loader_padded,
        "kernel.elf": root / arguments.kernel_elf,
        "zephyros.img": root / arguments.image,
    }
    artifacts: dict[str, dict[str, Any]] = {}
    for name, path in artifact_paths.items():
        try:
            artifacts[name] = artifact_record(path, root)
        except BaselineError as error:
            errors.append(error.code)
            artifacts[name] = {"path": repo_relative(path, root), "status": "invalid"}
    report["artifacts"] = artifacts

    complete_artifacts = len(artifacts) == len(artifact_paths) and all(
        item.get("sha256") for item in artifacts.values())
    if complete_artifacts:
        try:
            boot_size = artifacts["boot.bin"]["size_bytes"] == SECTOR_SIZE
            report["checks"]["boot_512_bytes"] = boot_size
            if not boot_size:
                raise BaselineError("boot_size_invalid")
            padded_size = artifacts["recovery_loader_padded.bin"]["size_bytes"]
            if padded_size % SECTOR_SIZE:
                raise BaselineError("recovery_padding_invalid")
            raw = artifact_paths["recovery_loader.bin"].read_bytes()
            padded = artifact_paths["recovery_loader_padded.bin"].read_bytes()
            if not raw or not padded.startswith(raw):
                raise BaselineError("recovery_padding_mismatch")
            image_size = artifacts["zephyros.img"]["size_bytes"]
            report["checks"]["image_size"] = image_size == EXPECTED_IMAGE_BYTES
            if image_size != EXPECTED_IMAGE_BYTES:
                raise BaselineError("image_size_invalid")
            layout = build_layout(artifacts, image_size)
            report["layout"] = layout
            report["checks"]["layout"] = True
            report["image"] = validate_image(
                artifact_paths["zephyros.img"], artifact_paths,
                image_size, layout)
            report["checks"]["fat32"] = True
        except (OSError, BaselineError) as error:
            errors.append(error.code if isinstance(error, BaselineError)
                          else "artifact_read_failed")

    if artifact_paths["kernel.elf"].is_file():
        try:
            report["elf"] = symbols_report(
                root, artifact_paths["kernel.elf"], arguments.nm,
                arguments.objdump, output_path.parent)
            report["checks"]["elf"] = True
        except BaselineError as error:
            errors.append(error.code)
            blocked |= error.status == "BLOCKED"

    if artifact_paths["zephyros.img"].is_file():
        try:
            report["updater_audit"] = updater_audit(
                root, artifact_paths["zephyros.img"], report["build_version"])
            report["checks"]["updater_audit"] = (
                report["updater_audit"]["status"] == "PASS")
            if report["updater_audit"]["status"] != "PASS":
                errors.append("updater_audit_failed")
        except BaselineError as error:
            errors.append(error.code)
            blocked |= error.status == "BLOCKED"

    report["outputs"] = {
        "report": repo_relative(output_path, root),
        "symbols": report.get("elf", {}).get("symbols", {}).get("listing"),
        "sections": report.get("elf", {}).get("sections", {}).get("listing"),
    }
    report["checks"]["personal_paths_absent"] = not contains_absolute_path(report)
    if not report["checks"]["personal_paths_absent"]:
        errors.append("personal_path_present")
    report["errors"] = sorted(set(errors))
    report["status"] = status_from_errors(report["errors"], blocked)
    return report


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2,
                               sort_keys=True) + "\n", encoding="utf-8")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)
    collect = subparsers.add_parser("collect", help="audita a linha de base")
    collect.add_argument("--repo-root", default=".")
    collect.add_argument("--output", required=True)
    collect.add_argument("--image", default="build/zephyros.img")
    collect.add_argument("--boot", default="build/boot.bin")
    collect.add_argument("--stage2", default="build/stage2.bin")
    collect.add_argument("--kernel", default="build/kernel.bin")
    collect.add_argument("--recovery-loader", default="build/recovery_loader.bin")
    collect.add_argument("--recovery-loader-padded",
                         default="build/recovery_loader_padded.bin")
    collect.add_argument("--kernel-elf", default="build/kernel.elf")
    collect.add_argument("--version-header", default="src/include/core/version.h")
    collect.add_argument("--build-version", default=DEFAULT_BUILD_VERSION)
    collect.add_argument("--target-version", default=DEFAULT_TARGET_VERSION)
    collect.add_argument("--python", default=sys.executable)
    collect.add_argument("--make", default="make")
    collect.add_argument("--nasm", default="nasm")
    collect.add_argument("--gcc", default="i686-elf-gcc")
    collect.add_argument("--ld", default="i686-elf-ld")
    collect.add_argument("--nm", default="i686-elf-nm")
    collect.add_argument("--objdump", default="i686-elf-objdump")
    collect.set_defaults(handler=lambda args: audit(args))
    return result


def main(argv: Iterable[str] | None = None) -> int:
    arguments = parser().parse_args(list(argv) if argv is not None else None)
    try:
        report = arguments.handler(arguments)
        root = Path(arguments.repo_root).resolve()
        output = Path(arguments.output)
        if not output.is_absolute():
            output = root / output
        output = output.resolve()
        repo_relative(output, root)
        write_report(output, report)
    except BaselineError as error:
        report = base_report()
        report["status"] = error.status
        report["errors"] = [error.code]
        try:
            root = Path(arguments.repo_root).resolve()
            output = Path(arguments.output)
            if not output.is_absolute():
                output = root / output
            output = output.resolve()
            report["outputs"] = {"report": repo_relative(output, root)}
            write_report(output, report)
        except (OSError, BaselineError):
            return 2
    print(f"RLS1 baseline: {report['status']}")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
