#!/usr/bin/env python3
"""Gera evidência de execução Assembly a partir do trace in_asm do QEMU."""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "zephyros-assembly-trace-report-v1"
SYMBOL_SCHEMA = "zephyros-assembly-symbols-v1"
TRACE_ADDRESS = re.compile(r"^\s*0x([0-9A-Fa-f]+):", re.MULTILINE)
LISTING_ENTRY = re.compile(r"^\s*(\d+)\s+([0-9A-Fa-f]{8})\s+")
SOURCE_LABEL = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*):\s*$")
NM_ENTRY = re.compile(r"^([0-9A-Fa-f]+)\s+([A-Za-z?])\s+(.+?)\s*$")


class AssemblyTraceError(Exception):
    """Erro de mapa ou de coleta do trace Assembly."""


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AssemblyTraceError(f"json_invalido:{path}:{error}") from error


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(path)


def resolve_tool(requested: str) -> str:
    if shutil.which(requested):
        return requested
    if Path(requested).is_file():
        return requested
    raise AssemblyTraceError(f"ferramenta_ausente:{requested}")


def resolve_nm(requested: str, compiler: str | None) -> str:
    try:
        return resolve_tool(requested)
    except AssemblyTraceError:
        if not compiler:
            raise
        compiler_path = Path(compiler)
        suffix = ".exe" if compiler_path.suffix.lower() == ".exe" else ""
        candidate = compiler_path.with_name(Path(requested).name + suffix)
        if candidate.is_file():
            return str(candidate)
        raise


def run_tool(command: list[str]) -> str:
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True,
                                   text=True, check=False, timeout=30.0)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AssemblyTraceError(f"ferramenta_falhou:{error}") from error
    if completed.returncode != 0:
        raise AssemblyTraceError(
            f"ferramenta_falhou:{' '.join(command)}:{completed.stderr.strip()}"
        )
    return completed.stdout


def catalog_surface_map(path: Path) -> dict[tuple[str, str], str]:
    value = read_json(path)
    result: dict[tuple[str, str], str] = {}
    for surface in value.get("surfaces", []):
        if not isinstance(surface, dict):
            continue
        source = surface.get("source")
        symbol = surface.get("symbol")
        surface_id = surface.get("id")
        if all(isinstance(item, str) for item in (source, symbol, surface_id)):
            result[(source, symbol)] = surface_id
    return result


def listing_symbols(source: Path, listing: Path, base: int,
                    surface_map: dict[tuple[str, str], str]) -> list[dict[str, Any]]:
    labels: dict[int, str] = {}
    for line_number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
        match = SOURCE_LABEL.fullmatch(line)
        if match:
            labels[line_number] = match.group(1)
    entries: list[tuple[int, int]] = []
    for line in listing.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LISTING_ENTRY.match(line)
        if match:
            entries.append((int(match.group(1)), int(match.group(2), 16)))
    result = []
    for line_number, symbol in labels.items():
        offset = next((address for source_line, address in entries
                       if source_line > line_number), None)
        if offset is None:
            continue
        item: dict[str, Any] = {
            "address": base + offset,
            "symbol": symbol,
            "source": source.relative_to(ROOT).as_posix(),
        }
        surface_id = surface_map.get((item["source"], symbol))
        if surface_id:
            item["surface_id"] = surface_id
        result.append(item)
    return result


def object_symbols(source: Path, object_path: Path, base: int, nm: str,
                   surface_map: dict[tuple[str, str], str]) -> list[dict[str, Any]]:
    output = run_tool([nm, "-n", str(object_path)])
    result = []
    source_name = source.relative_to(ROOT).as_posix()
    for line in output.splitlines():
        match = NM_ENTRY.fullmatch(line.strip())
        if not match or match.group(2).upper() in {"U", "?"}:
            continue
        symbol = match.group(3)
        if symbol.startswith("."):
            continue
        item: dict[str, Any] = {
            "address": base + int(match.group(1), 16),
            "symbol": symbol,
            "source": source_name,
        }
        surface_id = surface_map.get((source_name, symbol))
        if surface_id:
            item["surface_id"] = surface_id
        result.append(item)
    return result


def assemble_listing(nasm: str, source: Path, listing: Path,
                     output: Path, defines: list[str]) -> None:
    command = [nasm, "-f", "bin", "-l", str(listing)]
    command.extend(f"-d{value}" for value in defines)
    command.extend([str(source), "-o", str(output)])
    run_tool(command)


def build_symbols(arguments: argparse.Namespace) -> dict[str, Any]:
    build_dir = Path(arguments.build_dir)
    if not build_dir.is_absolute():
        build_dir = ROOT / build_dir
    nasm = resolve_tool(arguments.nasm)
    nm = resolve_nm(arguments.nm, getattr(arguments, "compiler", None))
    surface_map = catalog_surface_map(Path(arguments.catalog))
    kernel_path = build_dir / "kernel.bin"
    stage2_path = build_dir / "stage2.bin"
    recovery_path = build_dir / "recovery_loader_padded.bin"
    if not kernel_path.is_file() or not stage2_path.is_file() or not recovery_path.is_file():
        raise AssemblyTraceError("artefatos_de_boot_ausentes")
    kernel_bytes = kernel_path.stat().st_size
    stage2_sectors = math.ceil(stage2_path.stat().st_size / 512)
    kernel_sectors = math.ceil(kernel_bytes / 512)
    recovery_sectors = math.ceil(recovery_path.stat().st_size / 512)
    symbols: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="zephyros-asm-map-") as temporary:
        temporary_path = Path(temporary)
        flat_specs = [
            ("src/boot/boot.asm", 0x7C00, [f"STAGE2_SECTORS={stage2_sectors}"]),
            ("src/boot/stage2.asm", 0x5000, [
                f"KERNEL_SECTORS={kernel_sectors}", f"KERNEL_BYTES={kernel_bytes}",
                f"RECOVERY_LOADER_SECTORS={recovery_sectors}",
                "LEGACY_KERNEL_LBA=64", "RECOVERY_LOADER_LBA=6144",
                "FAT32_START_LBA=8192",
            ]),
            ("src/boot/system_boot.asm", 0x7C00, []),
            ("src/boot/system_stage2.asm", 0x5000, []),
        ]
        for source_name, base, defines in flat_specs:
            source = ROOT / source_name
            listing = temporary_path / (source.stem + ".lst")
            output = temporary_path / (source.stem + ".bin")
            assemble_listing(nasm, source, listing, output, defines)
            symbols.extend(listing_symbols(source, listing, base, surface_map))

        recovery_source = ROOT / "src/boot/recovery_entry.asm"
        recovery_object = temporary_path / "recovery_entry.o"
        run_tool([nasm, "-f", "elf32", str(recovery_source), "-o",
                  str(recovery_object)])
        symbols.extend(object_symbols(recovery_source, recovery_object, 0x00900000,
                                      nm, surface_map))

        entry_source = ROOT / "src/kernel/entry.asm"
        entry_object = temporary_path / "entry.o"
        run_tool([nasm, "-f", "elf32", str(entry_source), "-o",
                  str(entry_object)])
        symbols.extend(object_symbols(entry_source, entry_object, 0x00100000,
                                      nm, surface_map))
    unique = {(item["address"], item["symbol"], item["source"]): item
              for item in symbols}
    return {
        "schema": SYMBOL_SCHEMA,
        "symbols": sorted(unique.values(),
                           key=lambda item: (item["address"], item["symbol"])),
    }


def collect_trace(arguments: argparse.Namespace) -> dict[str, Any]:
    trace_path = Path(arguments.trace)
    symbols_value = read_json(Path(arguments.symbols))
    symbols = symbols_value.get("symbols", [])
    if not isinstance(symbols, list):
        raise AssemblyTraceError("simbolos_invalidos")
    text = trace_path.read_text(encoding="utf-8", errors="replace")
    addresses = {int(match, 16) for match in TRACE_ADDRESS.findall(text)}
    case_id = arguments.case_id
    source_filter = set(getattr(arguments, "source", []) or [])
    resolved = []
    missing = []
    covered = set()
    for symbol in symbols:
        if not isinstance(symbol, dict):
            continue
        if source_filter and symbol.get("source") not in source_filter:
            continue
        if not isinstance(symbol.get("surface_id"), str):
            continue
        address = symbol.get("address")
        if not isinstance(address, int):
            continue
        if address not in addresses:
            missing.append({"symbol": symbol.get("symbol"),
                            "source": symbol.get("source"),
                            "address": address})
            continue
        item = {"address": address, "symbol": symbol.get("symbol"),
                "source": symbol.get("source")}
        if isinstance(symbol.get("surface_id"), str):
            item["surface_id"] = symbol["surface_id"]
            covered.add(symbol["surface_id"])
        resolved.append(item)
    report_case = {
        "case_id": case_id,
        "status": "PASS",
        "addresses": sorted(item["address"] for item in resolved),
        "resolved": resolved,
        "unknown_addresses": [],
        "ambiguous_symbols": [],
        "errors": [],
    }
    return {
        "schema": SCHEMA,
        "status": "PASS",
        "case_id": case_id,
        "trace_addresses": len(addresses),
        "complete": not missing,
        "missing_symbols": missing,
        "covered_surface_ids": sorted(covered),
        "cases": [report_case],
    }


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    subparsers = command.add_subparsers(dest="command", required=True)
    symbols = subparsers.add_parser("symbols")
    symbols.add_argument("--build-dir", required=True)
    symbols.add_argument("--catalog", required=True)
    symbols.add_argument("--nasm", default="nasm")
    symbols.add_argument("--nm", default="nm")
    symbols.add_argument("--compiler")
    symbols.add_argument("--output", required=True)
    collect = subparsers.add_parser("collect")
    collect.add_argument("--trace", required=True)
    collect.add_argument("--symbols", required=True)
    collect.add_argument("--case-id", required=True)
    collect.add_argument("--source", action="append", default=[])
    collect.add_argument("--output", required=True)
    return command


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        if arguments.command == "symbols":
            write_json(Path(arguments.output), build_symbols(arguments))
        else:
            write_json(Path(arguments.output), collect_trace(arguments))
    except (AssemblyTraceError, OSError) as error:
        print(f"FAIL: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
