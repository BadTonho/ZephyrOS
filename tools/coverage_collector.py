#!/usr/bin/env python3
"""Converte os diagnósticos ZCOV em evidência de cobertura verificável."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "zephyros-coverage-report-v1"
SYMBOL_SCHEMA = "zephyros-coverage-symbols-v1"
CASE_PATTERN = re.compile(r"[A-Za-z0-9_.:-]{1,80}\Z")
HEX_PATTERN = re.compile(r"0[xX][0-9A-Fa-f]+\Z")
ZCOV_NAMES = {"ZCOV_BEGIN", "ZCOV_DATA", "ZCOV_END"}


class CoverageError(Exception):
    """Erro em evidência de cobertura incompleta ou inconsistente."""


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(f"json_invalido:{path}:{error}") from error


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(path)


def parse_hex(value: str, field: str) -> int:
    if not HEX_PATTERN.fullmatch(value):
        raise CoverageError(f"{field}_invalido:{value}")
    number = int(value, 16)
    if number > 0xFFFFFFFF:
        raise CoverageError(f"{field}_overflow:{value}")
    return number


def parse_address(value: str) -> int:
    if not HEX_PATTERN.fullmatch(value):
        raise CoverageError(f"address_invalido:{value}")
    number = int(value, 16)
    if number > 0xFFFFFFFFFFFFFFFF:
        raise CoverageError(f"address_overflow:{value}")
    return number


def parse_case(value: str) -> str:
    if not CASE_PATTERN.fullmatch(value):
        raise CoverageError(f"case_invalido:{value}")
    return value


def parse_fields(parts: list[str], line_number: int) -> dict[str, str]:
    fields: dict[str, str] = {}
    for part in parts:
        if "=" not in part:
            raise CoverageError(f"campo_sem_valor:linha={line_number}")
        key, value = part.split("=", 1)
        if not key or not value or key in fields:
            raise CoverageError(f"campo_invalido:linha={line_number}")
        fields[key] = value
    return fields


def signed_u32(value: int) -> int:
    return value - 0x100000000 if value >= 0x80000000 else value


def parse_trace(text: str) -> dict[str, Any]:
    cases: dict[str, dict[str, Any]] = {}
    errors: list[str] = []
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split("|")
        if parts[0] not in ZCOV_NAMES:
            continue
        try:
            fields = parse_fields(parts[1:], line_number)
            case_id = parse_case(fields["case"])
            value = parse_hex(fields["value"], "value") \
                if parts[0] != "ZCOV_DATA" else None
            state = cases.setdefault(case_id, {
                "case_id": case_id,
                "begin_count": 0,
                "declared_address_count": None,
                "addresses": [],
                "end_count": 0,
                "result": None,
                "lines": [],
                "errors": [],
            })
            state["lines"].append(line_number)
            if parts[0] == "ZCOV_BEGIN":
                if set(fields) != {"case", "value"}:
                    raise CoverageError("campos_begin_invalidos")
                if state["begin_count"]:
                    raise CoverageError("begin_duplicado")
                state["begin_count"] = 1
                state["declared_address_count"] = value
            elif parts[0] == "ZCOV_DATA":
                if set(fields) != {"case", "addresses"}:
                    raise CoverageError("campos_data_invalidos")
                if state["begin_count"] != 1 or state["end_count"]:
                    raise CoverageError("data_fora_da_sessao")
                addresses = fields["addresses"].split(",")
                if not addresses or any(not item for item in addresses):
                    raise CoverageError("lista_de_enderecos_invalida")
                state["addresses"].extend(
                    parse_address(item) for item in addresses)
            else:
                if set(fields) != {"case", "value"}:
                    raise CoverageError("campos_end_invalidos")
                if state["begin_count"] != 1 or state["end_count"]:
                    raise CoverageError("end_fora_da_sessao")
                state["end_count"] = 1
                state["result"] = signed_u32(value)
        except (KeyError, CoverageError) as error:
            message = str(error) if str(error) else "linha_invalida"
            errors.append(f"linha={line_number}:{message}")
            case_id = None
            if len(parts) > 1 and parts[1].startswith("case="):
                case_id = parts[1].split("=", 1)[1]
            if case_id and CASE_PATTERN.fullmatch(case_id):
                cases.setdefault(case_id, {
                    "case_id": case_id, "begin_count": 0,
                    "declared_address_count": None, "addresses": [],
                    "end_count": 0, "result": None, "lines": [],
                    "errors": [],
                })["errors"].append(message)

    reports: list[dict[str, Any]] = []
    for case_id in sorted(cases):
        state = cases[case_id]
        case_errors = list(state["errors"])
        if state["begin_count"] != 1:
            case_errors.append("begin_ausente")
        if state["end_count"] != 1:
            case_errors.append("end_ausente")
        declared = state["declared_address_count"]
        if declared is not None and declared != len(set(state["addresses"])):
            case_errors.append("quantidade_de_enderecos divergente")
        if state["result"] not in (None, 0):
            case_errors.append(f"caso_retornou:{state['result']}")
        reports.append({
            "case_id": case_id,
            "addresses": sorted(set(state["addresses"])),
            "declared_address_count": declared,
            "result": state["result"],
            "errors": sorted(set(case_errors)),
            "status": "PASS" if not case_errors else "FAIL",
        })
    if not reports:
        errors.append("nenhuma_sessao_zcov")
    return {
        "schema": SCHEMA,
        "status": "PASS" if not errors and all(
            report["status"] == "PASS" for report in reports) else "FAIL",
        "errors": sorted(set(errors)),
        "cases": reports,
    }


def parse_nm(text: str) -> list[dict[str, Any]]:
    symbols: list[dict[str, Any]] = []
    pattern = re.compile(r"^([0-9A-Fa-f]+)\s+([A-Za-z?])\s+(.+?)\s*$")
    for line in text.splitlines():
        match = pattern.fullmatch(line.strip())
        if not match or match.group(2).upper() in {"U", "?"}:
            continue
        symbols.append({
            "address": int(match.group(1), 16),
            "symbol": match.group(3),
        })
    return sorted(symbols, key=lambda item: (item["address"], item["symbol"]))


def load_symbols(path: Path) -> list[dict[str, Any]]:
    value = read_json(path)
    if isinstance(value, dict):
        if value.get("schema") != SYMBOL_SCHEMA:
            raise CoverageError(f"schema_de_simbolos_invalido:{path}")
        value = value.get("symbols")
    if not isinstance(value, list):
        raise CoverageError(f"simbolos_invalidos:{path}")
    result = []
    for item in value:
        if not isinstance(item, dict) or not isinstance(item.get("address"), int) \
                or not isinstance(item.get("symbol"), str):
            raise CoverageError(f"simbolo_invalido:{path}")
        if not 0 <= item["address"] <= 0xFFFFFFFFFFFFFFFF or not item["symbol"]:
            raise CoverageError(f"simbolo_fora_do_contrato:{path}")
        result.append(dict(item))
    return sorted(result, key=lambda item: (item["address"], item["symbol"]))


def catalog_surface_map(catalog: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    mapping: dict[str, list[dict[str, Any]]] = {}
    for surface in catalog.get("surfaces", []):
        if not isinstance(surface, dict):
            continue
        symbol = surface.get("symbol")
        if isinstance(symbol, str) and surface.get("kind") in {
                "c_function", "asm_entry"}:
            mapping.setdefault(symbol, []).append(surface)
    return mapping


def catalog_source_map(catalog: dict[str, Any]) -> dict[tuple[str, str], list[dict[str, Any]]]:
    mapping: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for surface in catalog.get("surfaces", []):
        if not isinstance(surface, dict) or surface.get("kind") not in {
                "c_function", "asm_entry"}:
            continue
        source = surface.get("source")
        symbol = surface.get("symbol")
        if isinstance(source, str) and isinstance(symbol, str):
            mapping.setdefault((source, symbol), []).append(surface)
    return mapping


def normalize_source(value: str) -> str | None:
    source = value.strip().replace("\\", "/")
    if not source or source.startswith("??"):
        return None
    if ":" in source:
        source = source.rsplit(":", 1)[0]
    marker = "/src/"
    if marker in source:
        source = "src/" + source.split(marker, 1)[1]
    elif source.startswith("src/"):
        source = source
    else:
        source = source.lstrip("./")
    return source or None


def parse_addr2line(text: str, addresses: list[int]) -> list[dict[str, Any]]:
    lines = text.splitlines()
    if len(lines) != len(addresses) * 2:
        raise CoverageError("addr2line_quantidade_invalida")
    result: list[dict[str, Any]] = []
    for index, address in enumerate(addresses):
        result.append({
            "address": address,
            "function": lines[index * 2].strip(),
            "source": normalize_source(lines[index * 2 + 1]),
        })
    return result


def infer_relocation_slide(addresses: list[int],
                           symbols: list[dict[str, Any]]) -> int:
    symbol_addresses = {item["address"] for item in symbols}
    candidates: set[int] = {0}
    for address in addresses:
        for symbol_address in symbol_addresses:
            slide = address - symbol_address
            if slide >= 0 and slide % 0x1000 == 0:
                candidates.add(slide)
    best_slide = 0
    best_score = -1
    tied = False
    for slide in candidates:
        score = sum(address - slide in symbol_addresses for address in addresses)
        if score > best_score:
            best_slide = slide
            best_score = score
            tied = False
        elif score == best_score:
            tied = True
    if best_score <= 0 or tied:
        return 0
    return best_slide


def resolve_case(case: dict[str, Any], symbols: list[dict[str, Any]],
                 catalog: dict[str, Any] | None = None) -> dict[str, Any]:
    by_address = {item["address"]: item for item in symbols}
    by_symbol = catalog_surface_map(catalog) if catalog else {}
    by_source = catalog_source_map(catalog) if catalog else {}
    relocation_slide = infer_relocation_slide(case["addresses"], symbols)
    resolved: list[dict[str, Any]] = []
    unknown: list[int] = []
    ambiguous: list[dict[str, Any]] = []
    covered: set[str] = set()
    for address in case["addresses"]:
        symbol_address = address - relocation_slide
        symbol = by_address.get(symbol_address)
        if symbol is None:
            unknown.append(address)
            continue
        item = {
            "address": address,
            "symbol": symbol["symbol"],
        }
        if isinstance(symbol.get("surface_id"), str):
            item["surface_id"] = symbol["surface_id"]
            covered.add(symbol["surface_id"])
        else:
            source = normalize_source(str(symbol.get("source", "")))
            candidates = by_source.get((source, symbol["symbol"]), []) \
                if source else by_symbol.get(symbol["symbol"], [])
            if len(candidates) == 1:
                item["surface_id"] = candidates[0]["id"]
                covered.add(candidates[0]["id"])
            elif len(candidates) > 1:
                ambiguous.append({
                    "address": address,
                    "symbol": symbol["symbol"],
                    "surface_ids": sorted(item["id"] for item in candidates),
                })
        resolved.append(item)
    return {
        "case_id": case["case_id"],
        "status": "PASS" if not unknown and not ambiguous and not case["errors"] else "FAIL",
        "addresses": case["addresses"],
        "relocation_slide": relocation_slide,
        "resolved": resolved,
        "covered_surface_ids": sorted(covered),
        "unknown_addresses": unknown,
        "ambiguous_symbols": ambiguous,
        "errors": case["errors"],
        "result": case["result"],
    }


def collect_report(trace: str, symbols: list[dict[str, Any]],
                   catalog: dict[str, Any] | None = None) -> dict[str, Any]:
    parsed = parse_trace(trace)
    resolved = [resolve_case(case, symbols, catalog) for case in parsed["cases"]]
    errors = list(parsed["errors"])
    if any(item["status"] != "PASS" for item in resolved):
        errors.append("resolucao_de_simbolos_incompleta")
    return {
        "schema": SCHEMA,
        "status": "PASS" if not errors else "FAIL",
        "errors": sorted(set(errors)),
        "cases": resolved,
        "covered_surface_ids": sorted({surface_id for item in resolved
                                        for surface_id in item["covered_surface_ids"]}),
    }


def resolve_nm(requested: str, compiler: str | None = None) -> str:
    candidates = [requested]
    if compiler:
        compiler_path = Path(compiler.strip().strip('"'))
        compiler_name = compiler_path.name
        if compiler_name.endswith("gcc.exe"):
            candidates.append(str(compiler_path.with_name(
                compiler_name[:-len("gcc.exe")] + "nm.exe")))
        elif compiler_name.endswith("gcc"):
            candidates.append(str(compiler_path.with_name(
                compiler_name[:-len("gcc")] + "nm")))
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
        if Path(candidate).is_file():
            return candidate
    return requested


def resolve_addr2line(requested: str, compiler: str | None = None) -> str:
    candidates = [requested]
    if compiler:
        compiler_path = Path(compiler.strip().strip('"'))
        compiler_name = compiler_path.name
        if compiler_name.endswith("gcc.exe"):
            candidates.append(str(compiler_path.with_name(
                compiler_name[:-len("gcc.exe")] + "addr2line.exe")))
        elif compiler_name.endswith("gcc"):
            candidates.append(str(compiler_path.with_name(
                compiler_name[:-len("gcc")] + "addr2line")))
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
        if Path(candidate).is_file():
            return candidate
    return requested


def enrich_symbols(symbols: list[dict[str, Any]], image: Path, addr2line: str,
                   timeout: float) -> list[dict[str, Any]]:
    enriched = [dict(item) for item in symbols]
    for start in range(0, len(enriched), 128):
        batch = enriched[start:start + 128]
        addresses = [item["address"] for item in batch]
        command = [addr2line, "-f", "-e", str(image),
                   *(f"0x{address:X}" for address in addresses)]
        try:
            completed = subprocess.run(command, capture_output=True, text=True,
                                       check=False, timeout=timeout)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise CoverageError(f"addr2line_indisponivel:{error}") from error
        if completed.returncode != 0:
            raise CoverageError(f"addr2line_falhou:{completed.stderr.strip()}")
        locations = parse_addr2line(completed.stdout, addresses)
        for item, location in zip(batch, locations):
            if location["source"]:
                item["source"] = location["source"]
    return enriched


def command_symbols(arguments: argparse.Namespace) -> int:
    image = Path(arguments.image)
    nm = resolve_nm(arguments.nm, arguments.compiler)
    addr2line = resolve_addr2line(arguments.addr2line, arguments.compiler)
    try:
        completed = subprocess.run([nm, "-n", str(image)], capture_output=True,
                                   text=True, check=False, timeout=arguments.timeout)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: nm indisponivel ou expirou: {error}", file=sys.stderr)
        return 1
    if completed.returncode != 0:
        print(completed.stderr, file=sys.stderr, end="")
        return 1
    symbols = parse_nm(completed.stdout)
    try:
        symbols = enrich_symbols(symbols, image, addr2line, arguments.timeout)
    except CoverageError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    write_json(Path(arguments.output), {
        "schema": SYMBOL_SCHEMA,
        "image": str(image),
        "symbols": symbols,
    })
    return 0


def command_collect(arguments: argparse.Namespace) -> int:
    trace = Path(arguments.serial).read_text(encoding="utf-8", errors="replace")
    symbols = load_symbols(Path(arguments.symbols))
    catalog = read_json(Path(arguments.catalog)) if arguments.catalog else None
    report = collect_report(trace, symbols, catalog)
    write_json(Path(arguments.output), report)
    print(f"Cobertura: {report['status']} ({len(report['covered_surface_ids'])} superficies)")
    return 0 if report["status"] == "PASS" else 1


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    subparsers = command.add_subparsers(dest="command", required=True)
    symbols = subparsers.add_parser("symbols")
    symbols.add_argument("--image", required=True)
    symbols.add_argument("--output", required=True)
    symbols.add_argument("--nm", default="nm")
    symbols.add_argument("--addr2line", default="addr2line")
    symbols.add_argument("--compiler")
    symbols.add_argument("--timeout", type=float, default=30.0)
    collect = subparsers.add_parser("collect")
    collect.add_argument("--serial", required=True)
    collect.add_argument("--symbols", required=True)
    collect.add_argument("--catalog")
    collect.add_argument("--output", required=True)
    return command


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        if arguments.command == "symbols":
            return command_symbols(arguments)
        return command_collect(arguments)
    except (CoverageError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
