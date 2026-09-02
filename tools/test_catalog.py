#!/usr/bin/env python3
"""Mantem o catalogo de superficies e casos de teste do ZephyrOS."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = Path("tests/catalog.json")
DEFAULT_VIEW = Path("docs/qualidade/catalogo-testes.md")
DEFAULT_COVERAGE_REGISTRY = Path("tests/coverage/registry.json")
SCHEMA = "zephyros-test-catalog-v1"
COVERAGE_REGISTRY_SCHEMA = "zephyros-coverage-registry-v1"
EXCLUDED_SOURCE_PARTS = {"vendor", "build", "generated"}
SURFACE_STATUSES = {"PENDING", "COVERED", "MANUAL", "BLOCKED"}
CASE_STATUSES = {"PENDING", "AUTOMATED", "MANUAL", "BLOCKED"}
CASE_EXECUTORS = {"host", "kernel", "qemu"}
CASE_ISOLATIONS = {"snapshot", "fixture"}
COVERAGE_MODES = {"direct", "integration"}
COVERAGE_MODE_PRIORITY = {"integration": 0, "direct": 1}
REGISTRY_SURFACE_SELECTORS = {
    "case_surface_ids", "case_surface_ids_with_implementations",
    "coverage_report",
}
BLOCKED_COVERAGE_TAG = "physical-hardware"
BLOCKED_FIELDS = (
    "blocked_reason", "blocked_capability", "blocked_evidence",
    "blocked_next_validation",
)
START_LABELS = {"start", "_start"}
ASM_DATA_DIRECTIVES = {
    "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw",
    "resd", "resq", "rest", "equ", "times",
}
COMMAND_DIAGNOSTICS = {
    "acpi", "appcheck", "blkcheck", "clock", "devcheck", "health",
    "irqstat", "kmetrics", "memcheck", "net", "proccheck", "regcheck",
    "route", "schedcheck", "slabtest", "sockstat", "storage", "timer",
    "tls", "vfs", "wait", "workq", "wqinfo",
}
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NON_FUNCTION_IDENTIFIERS = {"__attribute__", "if", "for", "while", "switch"}
GLOBAL_RE = re.compile(r"^\s*global\s+(.+?)\s*$", re.IGNORECASE)
BRACKET_GLOBAL_RE = re.compile(r"^\s*\[\s*global\s+([^\]]+)\]", re.IGNORECASE)
MACRO_RE = re.compile(r"^\s*%macro\s+([A-Za-z_][A-Za-z0-9_]*)\s+([0-9]+)", re.IGNORECASE)
MACRO_CALL_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s+([0-9]+)\b")
LABEL_RE = re.compile(r"^\s*([A-Za-z_.$?][A-Za-z0-9_.$?]*):")
TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|0[xX][0-9A-Fa-f]+|[0-9]+|[^\s]")
MACRO_FUNCTION_RE = re.compile(r"\bvoid\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")


class CatalogError(Exception):
    """Erro de estrutura, descoberta ou consistencia do catalogo."""


def has_duplicates(values: list[Any]) -> bool:
    for index, value in enumerate(values):
        if value in values[:index]:
            return True
    return False


def registry_surface_ids(entry: dict[str, Any],
                         cases: dict[str, dict[str, Any]],
                         surfaces: dict[str, dict[str, Any]] | None = None,
                         root: Path = ROOT) -> list[str]:
    surface_ids = entry.get("surface_ids")
    if (isinstance(surface_ids, list) and surface_ids) or \
            (isinstance(surface_ids, list) and
             entry.get("surface_selector") != "coverage_report"):
        return list(surface_ids)
    selector = entry.get("surface_selector")
    if selector == "coverage_report":
        report_value = entry.get("coverage_report")
        if (not isinstance(report_value, str) or not report_value.strip() or
                not surfaces):
            return []
        report_path = Path(report_value)
        if not report_path.is_absolute():
            report_path = root / report_path
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            selected_from_cases: set[str] = set()
            for case_id in entry.get("case_ids", []):
                case = cases.get(case_id)
                if not case or not isinstance(case.get("surface_ids"), list):
                    continue
                selected_from_cases.update(
                    surface_id for surface_id in case["surface_ids"]
                    if isinstance(surface_id, str))
            return sorted(selected_from_cases)
        observed = report.get("covered_surface_ids")
        if not isinstance(observed, list):
            return []
        source_filters = entry.get("coverage_sources", [])
        if not isinstance(source_filters, list):
            source_filters = []
        source_filters = {value for value in source_filters
                          if isinstance(value, str) and value}
        selected = []
        for surface_id in observed:
            if not isinstance(surface_id, str):
                continue
            surface = surfaces.get(surface_id)
            if not surface or surface.get("kind") != "c_function":
                continue
            if source_filters and surface.get("source") not in source_filters:
                continue
            selected.append(surface_id)
        if entry.get("include_public_apis"):
            implementation_keys = {
                (surface.get("source"), surface.get("symbol"))
                for surface_id in selected
                for surface in [surfaces.get(surface_id)]
                if surface is not None
            }
            implementation_sources_by_symbol: dict[str, set[str]] = {}
            for surface in surfaces.values():
                if surface.get("kind") != "c_function":
                    continue
                symbol = surface.get("symbol")
                source = surface.get("source")
                if isinstance(symbol, str) and isinstance(source, str):
                    implementation_sources_by_symbol.setdefault(symbol, set()).add(source)
            for surface in surfaces.values():
                if surface.get("kind") != "api_function":
                    continue
                source = surface.get("source", "")
                if not source.startswith("src/include/"):
                    continue
                implementation_source = source.replace(
                    "src/include/", "src/", 1).replace(".h", ".c")
                symbol = surface.get("symbol")
                same_symbol_sources = implementation_sources_by_symbol.get(symbol, set()) \
                    if isinstance(symbol, str) else set()
                if ((implementation_source, symbol) in implementation_keys or
                        (len(same_symbol_sources) == 1 and
                         next(iter(same_symbol_sources)) in {
                             item[0] for item in implementation_keys
                         })):
                    selected.append(surface["id"])
        return sorted(set(selected))
    if selector not in REGISTRY_SURFACE_SELECTORS:
        return []
    selected: set[str] = set()
    case_ids = entry.get("case_ids", [])
    if not isinstance(case_ids, list):
        return []
    for case_id in case_ids:
        if not isinstance(case_id, str):
            continue
        case = cases.get(case_id)
        if not case:
            continue
        case_surface_ids = case.get("surface_ids", [])
        if not isinstance(case_surface_ids, list):
            continue
        selected.update(surface_id for surface_id in case_surface_ids
                        if isinstance(surface_id, str))
    if selector == "case_surface_ids_with_implementations" and surfaces:
        by_symbol: dict[str, list[str]] = {}
        for surface in surfaces.values():
            if surface.get("kind") != "c_function":
                continue
            symbol = surface.get("symbol")
            identifier = surface.get("id")
            if isinstance(symbol, str) and isinstance(identifier, str):
                by_symbol.setdefault(symbol, []).append(identifier)
        for surface_id in list(selected):
            surface = surfaces.get(surface_id)
            if not surface or surface.get("kind") != "api_function":
                continue
            symbol = surface.get("symbol")
            implementations = by_symbol.get(symbol, [])
            if len(implementations) == 1:
                selected.add(implementations[0])
    return sorted(selected)


def relative_path(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def owner_from_source(source: str) -> str:
    parts = Path(source).parts
    if len(parts) >= 2 and parts[0] == "src":
        return parts[1]
    return "project"


def mask_c_text(text: str) -> str:
    chars = list(text)
    index = 0
    length = len(chars)
    while index < length:
        if chars[index:index + 2] == ["/", "/"]:
            index += 2
            while index < length and chars[index] != "\n":
                chars[index] = " "
                index += 1
            continue
        if chars[index:index + 2] == ["/", "*"]:
            chars[index] = " "
            if index + 1 < length:
                chars[index + 1] = " "
            index += 2
            while index < length:
                if chars[index:index + 2] == ["*", "/"]:
                    chars[index] = " "
                    if index + 1 < length:
                        chars[index + 1] = " "
                    index += 2
                    break
                if chars[index] != "\n":
                    chars[index] = " "
                index += 1
            continue
        if chars[index] in {'"', "'"}:
            quote = chars[index]
            chars[index] = " "
            index += 1
            while index < length:
                if chars[index] == "\\":
                    chars[index] = " "
                    index += 1
                    if index < length and chars[index] != "\n":
                        chars[index] = " "
                        index += 1
                    continue
                if chars[index] == quote:
                    chars[index] = " "
                    index += 1
                    break
                if chars[index] != "\n":
                    chars[index] = " "
                index += 1
            continue
        index += 1
    return "".join(chars)


def mask_c_preprocessor(text: str) -> str:
    chars = list(text)
    line_start = 0
    in_directive = False
    for index, char in enumerate(chars + ["\n"]):
        if char != "\n":
            continue
        line = "".join(chars[line_start:index])
        stripped = line.lstrip()
        directive = in_directive or stripped.startswith("#")
        if directive:
            for position in range(line_start, index):
                if chars[position] not in {"\r", "\n"}:
                    chars[position] = " "
        in_directive = directive and stripped.rstrip().endswith("\\")
        line_start = index + 1
    return "".join(chars)


def tokens_for_c(text: str) -> list[str]:
    return TOKEN_RE.findall(mask_c_text(mask_c_preprocessor(text)))


def matching_token(tokens: list[str], opening: int, left: str,
                   right: str) -> int | None:
    depth = 0
    for index in range(opening, len(tokens)):
        if tokens[index] == left:
            depth += 1
        elif tokens[index] == right:
            depth -= 1
            if depth == 0:
                return index
    return None


def declaration_start(tokens: list[str], name_index: int) -> int:
    index = name_index - 1
    while index >= 0 and tokens[index] not in {";", "}"}:
        index -= 1
    return index + 1


def discover_macro_functions(text: str, source: str, owner: str) -> list[dict[str, Any]]:
    lines = text.splitlines()
    macros: list[tuple[str, list[str], str]] = []
    index = 0
    while index < len(lines):
        match = re.match(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(.*)$",
                         lines[index])
        if not match:
            index += 1
            continue
        body_parts = [match.group(3)]
        while body_parts[-1].rstrip().endswith("\\") and index + 1 < len(lines):
            body_parts[-1] = body_parts[-1].rstrip()[:-1]
            index += 1
            body_parts.append(lines[index])
        body = " ".join(body_parts)
        function = MACRO_FUNCTION_RE.search(body)
        parameters = [item.strip() for item in match.group(2).split(",") if item.strip()]
        if function and function.group(1) in parameters:
            macros.append((match.group(1), parameters, function.group(1)))
        index += 1
    masked = mask_c_text(mask_c_preprocessor(text))
    surfaces: list[dict[str, Any]] = []
    for macro_name, parameters, function_parameter in macros:
        parameter_index = parameters.index(function_parameter)
        pattern = re.compile(rf"\b{re.escape(macro_name)}\s*\(([^()\n]*)\)")
        for match in pattern.finditer(masked):
            arguments = [item.strip() for item in match.group(1).split(",")]
            if len(arguments) != len(parameters):
                continue
            symbol = arguments[parameter_index]
            if not IDENTIFIER_RE.match(symbol):
                continue
            surfaces.append({
                "id": f"c:{source}:{symbol}",
                "kind": "c_function",
                "source": source,
                "symbol": symbol,
                "linkage": "macro-generated",
                "owner": owner,
            })
    return surfaces


def discover_c_functions(path: Path, root: Path) -> list[dict[str, Any]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    tokens = tokens_for_c(text)
    surfaces: list[dict[str, Any]] = []
    brace_depth = 0
    index = 0
    source = relative_path(path, root)
    while index < len(tokens):
        token = tokens[index]
        if token == "{":
            brace_depth += 1
            index += 1
            continue
        if token == "}":
            brace_depth = max(0, brace_depth - 1)
            index += 1
            continue
        if token != "(" or brace_depth != 0 or index == 0:
            index += 1
            continue
        name = tokens[index - 1]
        if not IDENTIFIER_RE.match(name) or name in NON_FUNCTION_IDENTIFIERS:
            index += 1
            continue
        if index >= 2 and tokens[index - 2] in {"*", ".", "->"}:
            index += 1
            continue
        close = matching_token(tokens, index, "(", ")")
        if close is None:
            raise CatalogError(f"parenteses desbalanceados em {source}")
        next_index = close + 1
        while next_index < len(tokens) and tokens[next_index] == "__attribute__":
            next_index += 1
            if next_index >= len(tokens) or tokens[next_index] != "(":
                break
            attribute_end = matching_token(tokens, next_index, "(", ")")
            if attribute_end is None:
                raise CatalogError(f"atributo desbalanceado em {source}")
            next_index = attribute_end + 1
        if next_index >= len(tokens) or tokens[next_index] != "{":
            index += 1
            continue
        start = declaration_start(tokens, index - 1)
        declaration = tokens[start:index]
        linkage = "static" if "static" in declaration else "external"
        surfaces.append({
            "id": f"c:{source}:{name}",
            "kind": "c_function",
            "source": source,
            "symbol": name,
            "linkage": linkage,
            "owner": owner_from_source(source),
        })
        index = next_index
    generated = discover_macro_functions(text, source, owner_from_source(source))
    known = {item["id"] for item in surfaces}
    surfaces.extend(item for item in generated if item["id"] not in known)
    return sorted(surfaces, key=lambda item: item["id"])


def discover_header_apis(path: Path, root: Path) -> list[dict[str, Any]]:
    tokens = tokens_for_c(path.read_text(encoding="utf-8", errors="replace"))
    surfaces: list[dict[str, Any]] = []
    depth = 0
    index = 0
    source = relative_path(path, root)
    while index < len(tokens):
        token = tokens[index]
        if token == "{":
            depth += 1
        elif token == "}":
            depth = max(0, depth - 1)
        elif token == "(" and depth == 0 and index > 0:
            name = tokens[index - 1]
            previous = tokens[index - 2] if index >= 2 else ""
            close = matching_token(tokens, index, "(", ")")
            following = tokens[close + 1] if close is not None and close + 1 < len(tokens) else ""
            start = declaration_start(tokens, index - 1)
            declaration = tokens[start:index]
            if (IDENTIFIER_RE.match(name) and name not in NON_FUNCTION_IDENTIFIERS
                    and previous not in {"*", ".", "->"}
                    and "typedef" not in declaration and following in {";", "{"}):
                surfaces.append({
                    "id": f"api:{source}:{name}",
                    "kind": "api_function",
                    "source": source,
                    "symbol": name,
                    "linkage": "static-inline" if "static" in declaration else "public",
                    "owner": owner_from_source(source.replace("include/", "")),
                })
        index += 1
    return surfaces


def discover_asm_entries(path: Path, root: Path) -> list[dict[str, Any]]:
    global_symbols: set[str] = set()
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    macro_templates: dict[str, list[str]] = {}
    active_macro = ""
    for line in lines:
        code = line.split(";", 1)[0]
        macro = MACRO_RE.match(code)
        if macro:
            active_macro = macro.group(1)
            macro_templates.setdefault(active_macro, [])
            continue
        if re.match(r"^\s*%endmacro\b", code, re.IGNORECASE):
            active_macro = ""
            continue
        match = GLOBAL_RE.match(code) or BRACKET_GLOBAL_RE.match(code)
        if match:
            names = [item.strip() for item in match.group(1).split(",")]
            if active_macro:
                macro_templates[active_macro].extend(names)
            else:
                global_symbols.update(names)
    generated_symbols: set[str] = set()
    for line in lines:
        call = MACRO_CALL_RE.match(line.split(";", 1)[0])
        if not call or call.group(1) not in macro_templates:
            continue
        for template in macro_templates[call.group(1)]:
            generated_symbols.add(template.replace("%1", call.group(2)))
    global_symbols.update(generated_symbols)
    source = relative_path(path, root)
    surfaces: list[dict[str, Any]] = []
    labels: list[tuple[str, int]] = []
    for line_index, line in enumerate(lines):
        match = LABEL_RE.match(line.split(";", 1)[0])
        if not match:
            continue
        labels.append((match.group(1), line_index))
    for symbol, line_index in labels:
        is_boot = source.startswith("src/boot/") and not symbol.startswith(".")
        if symbol not in global_symbols and symbol not in START_LABELS and not is_boot:
            continue
        if is_boot and symbol not in global_symbols and symbol not in START_LABELS:
            current_line = lines[line_index].split(";", 1)[0]
            current_label = LABEL_RE.match(current_line)
            next_line = current_line[current_label.end():].strip() if current_label else ""
            if not next_line:
                for candidate in lines[line_index + 1:]:
                    candidate = candidate.split(";", 1)[0].strip()
                    candidate_label = LABEL_RE.match(candidate)
                    if candidate_label:
                        candidate = candidate[candidate_label.end():].strip()
                    if candidate:
                        next_line = candidate
                        break
            directive = next_line.split(None, 1)[0].lower() if next_line else ""
            if directive in ASM_DATA_DIRECTIVES:
                continue
        surfaces.append({
            "id": f"asm:{source}:{symbol}",
            "kind": "asm_entry",
            "source": source,
            "symbol": symbol,
            "linkage": "global" if symbol in global_symbols else "boot",
            "owner": owner_from_source(source),
        })
    existing_symbols = {item["symbol"] for item in surfaces}
    for symbol in sorted(generated_symbols - existing_symbols):
        surfaces.append({
            "id": f"asm:{source}:{symbol}",
            "kind": "asm_entry",
            "source": source,
            "symbol": symbol,
            "linkage": "global",
            "owner": owner_from_source(source),
        })
    return sorted(surfaces, key=lambda item: item["id"])


def discover_commands(root: Path) -> list[dict[str, Any]]:
    path = root / "src/shell/shell_dispatch.c"
    text = path.read_text(encoding="utf-8", errors="replace")
    source = relative_path(path, root)
    surfaces = []
    pattern = re.compile(r'\{\s*"([^"]+)"\s*,\s*shell_dispatch_cmd_([A-Za-z0-9_]+)')
    for match in pattern.finditer(text):
        command = match.group(1)
        tags = ["diagnostic"] if command in COMMAND_DIAGNOSTICS else []
        surfaces.append({
            "id": f"command:{command}",
            "kind": "shell_command",
            "source": source,
            "symbol": command,
            "linkage": "dispatcher",
            "owner": "shell",
            "tags": tags,
            "handler": f"shell_dispatch_cmd_{match.group(2)}",
        })
    return surfaces


def discover_syscalls(root: Path) -> list[dict[str, Any]]:
    path = root / "src/include/core/syscall.h"
    source = relative_path(path, root)
    text = path.read_text(encoding="utf-8", errors="replace")
    surfaces = []
    pattern = re.compile(r"^#define\s+APP_SYSCALL_([A-Z0-9_]+)\s+(0[xX][0-9A-Fa-f]+|[0-9]+)", re.MULTILINE)
    for match in pattern.finditer(text):
        name = match.group(1)
        number = match.group(2)
        if name == "INVALID":
            continue
        numeric = int(number, 0)
        surfaces.append({
            "id": f"syscall:{numeric}",
            "kind": "syscall",
            "source": source,
            "symbol": f"APP_SYSCALL_{name}",
            "linkage": "abi",
            "owner": "core",
            "number": numeric,
        })
    return surfaces


def discover_surfaces(root: Path) -> list[dict[str, Any]]:
    surfaces: list[dict[str, Any]] = []
    source_root = root / "src"
    owned = lambda path: not any(part in EXCLUDED_SOURCE_PARTS
                                 for part in path.relative_to(source_root).parts)
    for path in sorted(path for path in source_root.rglob("*.c") if owned(path)):
        surfaces.extend(discover_c_functions(path, root))
    for path in sorted(path for path in source_root.rglob("*.asm") if owned(path)):
        surfaces.extend(discover_asm_entries(path, root))
    for path in sorted(path for path in (source_root / "include").rglob("*.h")
                       if owned(path)):
        surfaces.extend(discover_header_apis(path, root))
    surfaces.extend(discover_commands(root))
    surfaces.extend(discover_syscalls(root))
    ordered = sorted(surfaces, key=lambda item: item["id"])
    seen: set[str] = set()
    for item in ordered:
        if item["id"] in seen:
            raise CatalogError(f"superficie descoberta duplicada: {item['id']}")
        seen.add(item["id"])
    return ordered


def empty_catalog() -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "metadata": {
            "source_root": "src",
            "vendor_policy": "excluded_from_individual_inventory",
            "assembly_policy": "global_and_boot_entries_only",
            "identity_policy": "stable_id_without_line_numbers",
            "surface_statuses": sorted(SURFACE_STATUSES),
            "case_statuses": sorted(CASE_STATUSES),
            "strict_policy": "PENDING_and_BLOCKED_are_blocking",
        },
        "surfaces": [],
        "cases": [],
        "retired": [],
    }


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise CatalogError(f"catalogo ausente: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CatalogError(f"falha ao ler catalogo: {error}") from error
    if not isinstance(value, dict):
        raise CatalogError("catalogo deve ser um objeto JSON")
    return value


def validate_coverage_registry(registry: dict[str, Any],
                               catalog: dict[str, Any],
                               strict: bool = False,
                               root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    if registry.get("schema") != COVERAGE_REGISTRY_SCHEMA:
        return ["schema de registro de cobertura desconhecido"]
    entries = registry.get("entries")
    if not isinstance(entries, list):
        return ["entries do registro de cobertura invalido"]
    surfaces = {
        item.get("id"): item for item in catalog.get("surfaces", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    cases = {
        item.get("id"): item for item in catalog.get("cases", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    entry_ids: set[str] = set()
    registered_surfaces: set[str] = set()
    surface_modes: dict[str, set[str]] = {}
    registry_surfaces_by_case: dict[str, set[str]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("entrada de registro de cobertura invalida")
            continue
        entry_id = entry.get("id")
        if not isinstance(entry_id, str) or not entry_id:
            errors.append("entrada de cobertura sem id")
            continue
        if entry_id in entry_ids:
            errors.append(f"entrada de cobertura duplicada: {entry_id}")
        entry_ids.add(entry_id)
        for field in ("domain", "owner", "executor", "evidence"):
            if not isinstance(entry.get(field), str) or not entry[field].strip():
                errors.append(f"{field} ausente no registro: {entry_id}")
        if entry.get("executor") not in CASE_EXECUTORS:
            errors.append(f"executor invalido no registro: {entry_id}")
        mode = entry.get("coverage_mode")
        if mode not in COVERAGE_MODES:
            errors.append(f"coverage_mode invalido no registro: {entry_id}")
        case_ids = entry.get("case_ids")
        surface_ids = registry_surface_ids(entry, cases, surfaces, root)
        if strict and entry.get("surface_selector") == "coverage_report":
            report_value = entry.get("coverage_report")
            report_path = Path(report_value) if isinstance(report_value, str) else Path()
            if not report_path.is_absolute():
                report_path = root / report_path
            if not report_path.is_file():
                errors.append(f"relatorio de cobertura ausente: {entry_id}")
            else:
                try:
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError):
                    report = {}
                    errors.append(f"relatorio de cobertura invalido: {entry_id}")
                if report.get("status") != "PASS":
                    errors.append(f"relatorio de cobertura reprovado: {entry_id}")
                for field in ("unknown_addresses", "ambiguous_addresses"):
                    if report.get(field):
                        errors.append(f"relatorio de cobertura incompleto {field}: {entry_id}")
        if not isinstance(case_ids, list) or not case_ids:
            errors.append(f"casos ausentes no registro: {entry_id}")
            case_ids = []
        if not surface_ids:
            errors.append(f"superficies ausentes no registro: {entry_id}")
        if "surface_selector" in entry and entry.get("surface_selector") \
                not in REGISTRY_SURFACE_SELECTORS:
            errors.append(f"surface_selector invalido no registro: {entry_id}")
        if has_duplicates(case_ids):
            errors.append(f"casos duplicados no registro: {entry_id}")
        if has_duplicates(surface_ids):
            errors.append(f"superficies duplicadas no registro: {entry_id}")
        for case_id in case_ids:
            if not isinstance(case_id, str):
                errors.append(f"id de caso invalido no registro {entry_id}")
                continue
            case = cases.get(case_id)
            if case is None:
                errors.append(f"caso ausente no registro {case_id}: {entry_id}")
            elif strict and case.get("status") != "AUTOMATED":
                errors.append(f"caso nao automatizado no registro {case_id}: {entry_id}")
            elif strict and case.get("executor") != entry.get("executor"):
                errors.append(f"executor divergente no caso {case_id}: {entry_id}")
        for surface_id in surface_ids:
            if not isinstance(surface_id, str):
                errors.append(f"id de superficie invalido no registro {entry_id}")
                continue
            surface = surfaces.get(surface_id)
            if surface is None:
                errors.append(f"superficie ausente no registro {surface_id}: {entry_id}")
                continue
            registered_surfaces.add(surface_id)
            if mode in COVERAGE_MODES:
                surface_modes.setdefault(surface_id, set()).add(mode)
            if strict and surface.get("status") != "COVERED":
                errors.append(f"superficie nao coberta no registro {surface_id}: {entry_id}")
            surface_case_ids = surface.get("case_ids", [])
            if not isinstance(surface_case_ids, list):
                surface_case_ids = []
            if strict and not any(case_id in surface_case_ids
                                  for case_id in case_ids):
                errors.append(f"superficie sem caso do registro {surface_id}: {entry_id}")
            if entry.get("surface_selector") not in REGISTRY_SURFACE_SELECTORS:
                for case_id in case_ids:
                    if not isinstance(case_id, str):
                        continue
                    case = cases.get(case_id)
                    case_surface_ids = case.get("surface_ids", []) \
                        if case is not None else []
                    if not isinstance(case_surface_ids, list):
                        continue
                    if surface_id not in case_surface_ids:
                        errors.append(f"registro sem vinculo no caso {case_id}: {surface_id}")
        for case_id in case_ids:
            if isinstance(case_id, str):
                registry_surfaces_by_case.setdefault(case_id, set()).update(
                    surface_ids)
        if entry.get("surface_selector") != "coverage_report" and \
                entry.get("surface_selector") in REGISTRY_SURFACE_SELECTORS:
            for case_id in case_ids:
                case = cases.get(case_id)
                if case is None:
                    continue
                case_surface_ids = case.get("surface_ids", [])
                if not isinstance(case_surface_ids, list):
                    continue
                for surface_id in case_surface_ids:
                    if surface_id not in surface_ids:
                        errors.append(f"selecao nao contem superficie {surface_id}: {entry_id}")
    for case_id, registry_surfaces in registry_surfaces_by_case.items():
        case = cases.get(case_id)
        if case is None:
            continue
        case_surface_ids = case.get("surface_ids", [])
        if not isinstance(case_surface_ids, list):
            continue
        for surface_id in case_surface_ids:
            if isinstance(surface_id, str) and surface_id not in registry_surfaces:
                errors.append(f"selecao nao contem superficie {surface_id}: {case_id}")
    if strict:
        for surface_id, modes in sorted(surface_modes.items()):
            preferred_mode = max(modes, key=COVERAGE_MODE_PRIORITY.get)
            surface = surfaces.get(surface_id)
            if surface is not None and surface.get("coverage_mode") != preferred_mode:
                errors.append(f"coverage_mode divergente {surface_id}: esperado {preferred_mode}")
        covered = {
            item.get("id") for item in catalog.get("surfaces", [])
            if isinstance(item, dict) and item.get("status") == "COVERED"
        }
        for surface_id in sorted(covered - registered_surfaces):
            errors.append(f"superficie coberta fora do registro: {surface_id}")
    return errors


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def surface_defaults(discovered: dict[str, Any]) -> dict[str, Any]:
    surface = dict(discovered)
    layer_by_kind = {
        "c_function": "kernel-source",
        "asm_entry": "boot-and-assembly",
        "api_function": "public-header",
        "shell_command": "shell-dispatcher",
        "syscall": "application-abi",
    }
    surface.setdefault("layer", layer_by_kind.get(surface.get("kind"), "unknown"))
    surface.setdefault("preconditions", "definir no caso de teste associado")
    surface.setdefault("action", "definir no caso de teste associado")
    surface.setdefault("expected", "superficie localizada e contrato observado")
    surface.setdefault("errors", "definir erros canonicos aplicaveis")
    surface.setdefault("effects", "nenhum efeito durante a catalogacao")
    surface.setdefault("cleanup", "nenhuma limpeza necessaria")
    surface.setdefault("status", "PENDING")
    surface.setdefault("case_ids", [])
    surface.setdefault("tags", [])
    return surface


def sync_catalog(catalog: dict[str, Any], discovered: list[dict[str, Any]]) -> dict[str, Any]:
    existing = {item.get("id"): item for item in catalog.get("surfaces", [])
                if isinstance(item, dict) and item.get("id")}
    active_ids = {item["id"] for item in discovered}
    surfaces = []
    for item in discovered:
        old = existing.get(item["id"], {})
        merged = dict(old)
        merged.update(item)
        surfaces.append(surface_defaults(merged))
    retired = [item for item in catalog.get("retired", [])
               if isinstance(item, dict) and item.get("id") not in active_ids]
    retired_ids = {item.get("id") for item in retired}
    for identifier, old in existing.items():
        if identifier in active_ids or identifier in retired_ids:
            continue
        retired.append({
            "id": identifier,
            "kind": old.get("kind", "unknown"),
            "source": old.get("source", ""),
            "symbol": old.get("symbol", ""),
            "retired_reason": "surface ausente na fonte atual; confirmar remocao ou renomeacao",
        })
    result = dict(catalog)
    result["schema"] = SCHEMA
    result["surfaces"] = sorted(surfaces, key=lambda item: item["id"])
    result["cases"] = sorted(catalog.get("cases", []), key=lambda item: item.get("id", ""))
    result["retired"] = sorted(retired, key=lambda item: item.get("id", ""))
    synchronize_coverage_links(result)
    return result


def synchronize_coverage_links(catalog: dict[str, Any]) -> None:
    surfaces = {
        item.get("id"): item for item in catalog.get("surfaces", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    cases = {
        item.get("id"): item for item in catalog.get("cases", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    for case in cases.values():
        surface_ids = case.get("surface_ids", [])
        if not isinstance(surface_ids, list):
            continue
        for surface_id in surface_ids:
            if not isinstance(surface_id, str):
                continue
            surface = surfaces.get(surface_id)
            if not surface:
                continue
            case_ids = surface.setdefault("case_ids", [])
            if isinstance(case_ids, list) and case["id"] not in case_ids:
                case_ids.append(case["id"])
    for surface in surfaces.values():
        case_ids = surface.get("case_ids", [])
        if not isinstance(case_ids, list):
            continue
        for case_id in case_ids:
            if not isinstance(case_id, str):
                continue
            case = cases.get(case_id)
            if not case:
                continue
            surface_ids = case.setdefault("surface_ids", [])
            if isinstance(surface_ids, list) and surface["id"] not in surface_ids:
                surface_ids.append(surface["id"])
    for surface in surfaces.values():
        if isinstance(surface.get("case_ids"), list):
            surface["case_ids"] = sorted(
                surface["case_ids"], key=lambda value: str(value))
            surface["case_ids"] = list(dict.fromkeys(surface["case_ids"]))
    for case in cases.values():
        if isinstance(case.get("surface_ids"), list):
            case["surface_ids"] = sorted(
                case["surface_ids"], key=lambda value: str(value))
            case["surface_ids"] = list(dict.fromkeys(case["surface_ids"]))


def reset_coverage_links(catalog: dict[str, Any]) -> None:
    """Remove links persisted by a previous registry before rebuilding them."""
    for case in catalog.get("cases", []):
        if isinstance(case, dict) and isinstance(case.get("surface_ids"), list):
            case["surface_ids"] = []
    for surface in catalog.get("surfaces", []):
        if not isinstance(surface, dict):
            continue
        surface["case_ids"] = []
        if surface.get("status") == "COVERED":
            surface["status"] = "PENDING"
            surface.pop("coverage_mode", None)


def apply_registry_case_definitions(catalog: dict[str, Any],
                                    registry: dict[str, Any]) -> None:
    cases = catalog.setdefault("cases", [])
    case_map = {
        item.get("id"): item for item in cases
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    for entry in registry.get("entries", []):
        if not isinstance(entry, dict) or "case_definition" not in entry:
            continue
        definition = entry.get("case_definition")
        if not isinstance(definition, dict):
            raise CatalogError(f"definicao de caso invalida: {entry.get('id')}")
        case_id = definition.get("id")
        if not isinstance(case_id, str) or not case_id:
            raise CatalogError(f"definicao de caso sem id: {entry.get('id')}")
        existing = case_map.get(case_id)
        if existing is None:
            case = dict(definition)
            cases.append(case)
            case_map[case_id] = case
        else:
            existing.clear()
            existing.update(definition)


def apply_coverage_registry(catalog: dict[str, Any],
                            registry: dict[str, Any],
                            root: Path = ROOT) -> None:
    surfaces = {
        item.get("id"): item for item in catalog.get("surfaces", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    for entry in registry.get("entries", []):
        if not isinstance(entry, dict):
            continue
        mode = entry.get("coverage_mode")
        case_ids = entry.get("case_ids", [])
        if mode not in COVERAGE_MODES or not isinstance(case_ids, list):
            continue
        for surface_id in registry_surface_ids(entry, {
                item.get("id"): item for item in catalog.get("cases", [])
                if isinstance(item, dict) and isinstance(item.get("id"), str)},
                surfaces, root):
            surface = surfaces.get(surface_id)
            if surface is None:
                continue
            surface["status"] = "COVERED"
            current_mode = surface.get("coverage_mode")
            if (current_mode not in COVERAGE_MODES or
                    COVERAGE_MODE_PRIORITY[mode] >
                    COVERAGE_MODE_PRIORITY[current_mode]):
                surface["coverage_mode"] = mode
            surface_case_ids = surface.setdefault("case_ids", [])
            if isinstance(surface_case_ids, list):
                surface_case_ids.extend(case_ids)
    synchronize_coverage_links(catalog)


def validate_catalog(catalog: dict[str, Any], root: Path, strict: bool = False) -> list[str]:
    errors: list[str] = []
    if catalog.get("schema") != SCHEMA:
        errors.append("schema de catalogo desconhecido")
    if not isinstance(catalog.get("metadata"), dict):
        errors.append("metadata ausente ou invalido")
    else:
        metadata = catalog["metadata"]
        for field in ("source_root", "vendor_policy", "assembly_policy",
                      "identity_policy", "strict_policy"):
            if not isinstance(metadata.get(field), str) or not metadata[field]:
                errors.append(f"metadata sem campo textual {field}")
        if metadata.get("surface_statuses") != sorted(SURFACE_STATUSES):
            errors.append("metadata surface_statuses invalido")
        if metadata.get("case_statuses") != sorted(CASE_STATUSES):
            errors.append("metadata case_statuses invalido")
    surfaces = catalog.get("surfaces")
    cases = catalog.get("cases")
    retired = catalog.get("retired")
    if not isinstance(surfaces, list):
        errors.append("surfaces deve ser uma lista")
        surfaces = []
    if not isinstance(cases, list):
        errors.append("cases deve ser uma lista")
        cases = []
    if not isinstance(retired, list):
        errors.append("retired deve ser uma lista")
        retired = []
    identifiers: set[str] = set()
    case_map: dict[str, dict[str, Any]] = {}
    for item in cases:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            errors.append("caso sem id")
            continue
        identifier = item["id"]
        if identifier in case_map:
            errors.append(f"caso duplicado: {identifier}")
        case_map[identifier] = item
        if item.get("status") not in CASE_STATUSES:
            errors.append(f"status de caso invalido: {identifier}")
        for field in ("scenario", "owner", "layer", "preconditions", "action",
                      "expected", "errors", "effects", "cleanup"):
            if not isinstance(item.get(field), str):
                errors.append(f"caso {identifier} sem campo textual {field}")
        if item.get("executor") not in CASE_EXECUTORS:
            errors.append(f"executor de caso invalido: {identifier}")
        if not isinstance(item.get("profile"), str) or not item["profile"]:
            errors.append(f"perfil de caso invalido: {identifier}")
        if not isinstance(item.get("parameters"), dict):
            errors.append(f"parameters invalido: {identifier}")
        for field in ("timeout_seconds", "heartbeat_timeout_seconds"):
            value = item.get(field)
            if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
                errors.append(f"{field} invalido: {identifier}")
        if item.get("isolation") not in CASE_ISOLATIONS:
            errors.append(f"isolamento de caso invalido: {identifier}")
        if item.get("executor") == "qemu":
            if not isinstance(item.get("guest_case"), str) or not item["guest_case"]:
                errors.append(f"guest_case ausente: {identifier}")
            elif not re.fullmatch(r"[A-Za-z0-9_.:-]+", item["guest_case"]):
                errors.append(f"guest_case invalido: {identifier}")
        if not isinstance(item.get("surface_ids", []), list):
            errors.append(f"surface_ids invalido: {identifier}")
        elif has_duplicates(item["surface_ids"]):
            errors.append(f"surface_ids duplicado: {identifier}")
    for item in surfaces:
        if not isinstance(item, dict):
            errors.append("superficie invalida")
            continue
        identifier = item.get("id")
        if not isinstance(identifier, str) or not identifier:
            errors.append("superficie sem id")
            continue
        if identifier in identifiers:
            errors.append(f"superficie duplicada: {identifier}")
        identifiers.add(identifier)
        if item.get("kind") not in {"c_function", "asm_entry", "api_function",
                                     "shell_command", "syscall"}:
            errors.append(f"tipo de superficie invalido: {identifier}")
        prefixes = {
            "c_function": "c:", "asm_entry": "asm:",
            "api_function": "api:", "shell_command": "command:",
            "syscall": "syscall:",
        }
        expected_prefix = prefixes.get(item.get("kind"), "")
        if expected_prefix and not identifier.startswith(expected_prefix):
            errors.append(f"id incompatível com tipo: {identifier}")
        source = item.get("source")
        if not isinstance(source, str) or not source.startswith("src/"):
            errors.append(f"fonte invalida: {identifier}")
        elif not (root / source).is_file():
            errors.append(f"fonte inexistente em {identifier}: {source}")
        if not isinstance(item.get("symbol"), str) or not item["symbol"]:
            errors.append(f"superficie sem simbolo: {identifier}")
        if not isinstance(item.get("owner"), str) or not item["owner"]:
            errors.append(f"superficie sem proprietario: {identifier}")
        for field in ("layer", "preconditions", "action", "expected", "errors",
                      "effects", "cleanup"):
            if not isinstance(item.get(field), str):
                errors.append(f"superficie {identifier} sem campo textual {field}")
        if not isinstance(item.get("tags"), list):
            errors.append(f"tags invalido: {identifier}")
        if item.get("status") not in SURFACE_STATUSES:
            errors.append(f"status de superficie invalido: {identifier}")
        coverage_mode = item.get("coverage_mode")
        if coverage_mode is not None and coverage_mode not in COVERAGE_MODES:
            errors.append(f"coverage_mode invalido: {identifier}")
        if item.get("status") == "COVERED" and strict and \
                coverage_mode not in COVERAGE_MODES:
            errors.append(f"coverage_mode ausente: {identifier}")
        if item.get("status") == "BLOCKED":
            for field in BLOCKED_FIELDS:
                if strict and (not isinstance(item.get(field), str) or
                                not item[field].strip()):
                    errors.append(f"{field} ausente: {identifier}")
        case_ids = item.get("case_ids")
        if not isinstance(case_ids, list):
            errors.append(f"case_ids invalido: {identifier}")
        else:
            if has_duplicates(case_ids):
                errors.append(f"case_ids duplicado: {identifier}")
            for case_id in case_ids:
                if not isinstance(case_id, str):
                    errors.append(f"case_id invalido: {identifier}")
                    continue
                if case_id not in case_map:
                    errors.append(f"caso ausente {case_id} na superficie {identifier}")
                elif strict and case_map[case_id].get("status") != "AUTOMATED":
                    errors.append(f"caso nao automatizado {case_id}: {identifier}")
            if strict and item.get("status") == "COVERED" and not case_ids:
                errors.append(f"superficie coberta sem caso: {identifier}")
        if strict and item.get("status") == "PENDING":
            errors.append(f"cobertura pendente: {identifier}")
        if strict and item.get("status") == "BLOCKED" and \
                BLOCKED_COVERAGE_TAG not in item.get("tags", []):
            errors.append(f"bloqueio nao relacionado a hardware: {identifier}")
    surface_map = {
        item.get("id"): item for item in surfaces
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    for case in cases:
        if not isinstance(case, dict):
            continue
        case_id = case.get("id", "<sem-id>")
        case_surface_ids = case.get("surface_ids", [])
        if not isinstance(case_surface_ids, list):
            continue
        for surface_id in case_surface_ids:
            if not isinstance(surface_id, str):
                errors.append(f"surface_id invalido no caso {case_id}")
                continue
            surface = surface_map.get(surface_id)
            if surface is None:
                errors.append(f"superficie ausente {surface_id} no caso {case_id}")
            elif case_id not in surface.get("case_ids", []):
                errors.append(f"vinculo reverso ausente {case_id}: {surface_id}")
    for surface in surfaces:
        if not isinstance(surface, dict):
            continue
        surface_id = surface.get("id", "<sem-id>")
        surface_case_ids = surface.get("case_ids", [])
        if not isinstance(surface_case_ids, list):
            continue
        for case_id in surface_case_ids:
            if not isinstance(case_id, str):
                errors.append(f"case_id invalido na superficie {surface_id}")
                continue
            case = case_map.get(case_id)
            if case is not None and surface_id not in case.get("surface_ids", []):
                errors.append(f"vinculo reverso ausente {surface_id}: {case_id}")
    if strict:
        for item in cases:
            if isinstance(item, dict) and item.get("status") in {"PENDING", "BLOCKED"}:
                errors.append(f"caso pendente ou bloqueado: {item.get('id', '<sem-id>')}")
    retired_ids: set[str] = set()
    for item in retired:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            errors.append("superficie aposentada sem id")
            continue
        identifier = item["id"]
        if identifier in identifiers or identifier in retired_ids:
            errors.append(f"superficie aposentada duplicada: {identifier}")
        retired_ids.add(identifier)
        if not isinstance(item.get("retired_reason"), str) or not item["retired_reason"].strip():
            errors.append(f"motivo de aposentadoria ausente: {identifier}")
    discovered = {item["id"] for item in discover_surfaces(root)}
    missing = sorted(discovered - identifiers)
    stale = sorted(identifiers - discovered)
    errors.extend(f"superficie descoberta sem catalogo: {item}" for item in missing)
    errors.extend(f"superficie ativa ausente da fonte: {item}" for item in stale)
    return errors


def summarize(catalog: dict[str, Any]) -> dict[str, int]:
    counter = Counter(item.get("kind", "unknown") for item in catalog.get("surfaces", []))
    counter.update(f"status:{item.get('status', 'unknown')}"
                   for item in catalog.get("surfaces", []))
    counter.update(f"case_status:{item.get('status', 'unknown')}"
                   for item in catalog.get("cases", []))
    counter["cases"] = len(catalog.get("cases", []))
    counter["retired"] = len(catalog.get("retired", []))
    return dict(counter)


def render_catalog(catalog: dict[str, Any]) -> str:
    summary = summarize(catalog)
    lines = [
        "# Catálogo de testes do ZephyrOS",
        "",
        "> Arquivo gerado a partir de `tests/catalog.json`. Não editar manualmente.",
        "",
        "## Resumo",
        "",
        f"- Superfícies ativas: **{len(catalog.get('surfaces', []))}**",
        f"- Casos de teste: **{summary.get('cases', 0)}**",
        f"- Superfícies aposentadas: **{summary.get('retired', 0)}**",
        "",
        "| Tipo | Quantidade |",
        "|---|---:|",
    ]
    kinds = sorted(key for key in summary
                   if not key.startswith("status:") and
                   not key.startswith("case_status:") and
                   key not in {"cases", "retired"})
    for kind in kinds:
        lines.append(f"| `{kind}` | {summary[kind]} |")
    lines.extend(["", "| Cobertura | Quantidade |", "|---|---:|"])
    for status in sorted(SURFACE_STATUSES):
        lines.append(f"| `{status}` | {summary.get(f'status:{status}', 0)} |")
    lines.extend(["", "| Casos | Quantidade |", "|---|---:|"])
    for status in sorted(CASE_STATUSES):
        lines.append(f"| `{status}` | {summary.get(f'case_status:{status}', 0)} |")
    owners = Counter(item.get("owner", "unknown") for item in catalog.get("surfaces", []))
    lines.extend(["", "### Por subsistema", "", "| Proprietario | Superficies |", "|---|---:|"])
    for owner in sorted(owners):
        lines.append(f"| `{owner}` | {owners[owner]} |")
    lines.extend(["", "## Superfícies", ""])
    by_kind: dict[str, list[dict[str, Any]]] = {}
    for surface in catalog.get("surfaces", []):
        by_kind.setdefault(surface.get("kind", "unknown"), []).append(surface)
    for kind in sorted(by_kind):
        lines.extend([f"### {kind}", "", "| ID | Fonte | Símbolo | Proprietário | Status | Casos |", "|---|---|---|---|---|---:|"])
        for surface in by_kind[kind]:
            lines.append("| `{id}` | `{source}` | `{symbol}` | `{owner}` | `{status}` | {cases} |".format(
                id=surface["id"].replace("|", "\\|"),
                source=surface["source"].replace("|", "\\|"),
                symbol=surface["symbol"].replace("|", "\\|"),
                owner=surface["owner"].replace("|", "\\|"),
                status=surface.get("status", "PENDING"),
                cases=len(surface.get("case_ids", []))))
        lines.append("")
    lines.extend(["## Casos", ""])
    if not catalog.get("cases"):
        lines.append("Nenhum caso de teste foi associado ainda; as superfícies permanecem `PENDING`.")
    else:
        lines.extend(["| ID | Executor | Perfil | Caso guest | Status | Timeout | Heartbeat | Isolamento | Proprietario | Camada | Pre-condicoes | Acao | Resultado esperado | Erros | Efeitos | Limpeza |", "|---|---|---|---|---|---:|---:|---|---|---|---|---|---|---|---|---|"])
        for case in catalog["cases"]:
            values = {key: str(case.get(key, "-")).replace("|", "\\|").replace("\n", " ")
                      for key in ("id", "executor", "profile", "guest_case", "status",
                                  "timeout_seconds", "heartbeat_timeout_seconds",
                                  "isolation", "owner", "layer", "preconditions",
                                  "action", "expected", "errors", "effects", "cleanup")}
            lines.append("| `{id}` | `{executor}` | `{profile}` | `{guest_case}` | `{status}` | {timeout_seconds} | {heartbeat_timeout_seconds} | `{isolation}` | `{owner}` | `{layer}` | {preconditions} | {action} | {expected} | {errors} | {effects} | {cleanup} |".format(**values))
    lines.extend(["", "## Superfícies sem caso associado", ""])
    uncovered = [item for item in catalog.get("surfaces", []) if not item.get("case_ids")]
    if not uncovered:
        lines.append("Nenhuma.")
    else:
        lines.extend("- `{}`".format(item["id"]) for item in uncovered)
    lines.extend(["", "## Aposentadas", ""])
    if not catalog.get("retired"):
        lines.append("Nenhuma.")
    else:
        for item in catalog["retired"]:
            lines.append(f"- `{item['id']}` — {item['retired_reason']}")
    lines.append("")
    return "\n".join(lines)


def resolve_path(root: Path, value: str | None, default: Path) -> Path:
    path = Path(value) if value else default
    return path if path.is_absolute() else root / path


def command_scan(args: argparse.Namespace, root: Path) -> int:
    print(json.dumps({"schema": SCHEMA, "surfaces": discover_surfaces(root)},
                     ensure_ascii=False, indent=2))
    return 0


def command_sync(args: argparse.Namespace, root: Path) -> int:
    path = resolve_path(root, args.catalog, DEFAULT_CATALOG)
    catalog = load_json(path) if path.is_file() else empty_catalog()
    result = sync_catalog(catalog, discover_surfaces(root))
    registry_path = resolve_path(root, args.coverage_registry,
                                 DEFAULT_COVERAGE_REGISTRY)
    if registry_path.is_file():
        registry = load_json(registry_path)
        reset_coverage_links(result)
        apply_registry_case_definitions(result, registry)
        apply_coverage_registry(result, registry, root)
        registry_errors = validate_coverage_registry(registry, result, root=root)
        if registry_errors:
            for error in registry_errors:
                print(f"ERRO: {error}", file=sys.stderr)
            return 1
    write_json(path, result)
    print(f"Catalogo sincronizado: {len(result['surfaces'])} superficies")
    return 0


def command_validate(args: argparse.Namespace, root: Path) -> int:
    path = resolve_path(root, args.catalog, DEFAULT_CATALOG)
    catalog = load_json(path)
    errors = validate_catalog(catalog, root, strict=args.strict)
    registry_path = resolve_path(root, args.coverage_registry,
                                 DEFAULT_COVERAGE_REGISTRY)
    if registry_path.is_file():
        registry = load_json(registry_path)
        errors.extend(validate_coverage_registry(registry, catalog,
                                                 strict=args.strict,
                                                 root=root))
    elif args.strict:
        errors.append(f"registro de cobertura ausente: {registry_path}")
    if errors:
        for error in errors:
            print(f"ERRO: {error}", file=sys.stderr)
        return 1
    summary = summarize(catalog)
    print("Catalogo valido: "
          f"{len(catalog['surfaces'])} superficies, {summary['cases']} casos")
    return 0


def command_render(args: argparse.Namespace, root: Path) -> int:
    catalog_path = resolve_path(root, args.catalog, DEFAULT_CATALOG)
    view_path = resolve_path(root, args.view, DEFAULT_VIEW)
    view_path.parent.mkdir(parents=True, exist_ok=True)
    view_path.write_text(render_catalog(load_json(catalog_path)), encoding="utf-8")
    print(f"Visao renderizada: {view_path}")
    return 0


def command_check_rendered(args: argparse.Namespace, root: Path) -> int:
    catalog_path = resolve_path(root, args.catalog, DEFAULT_CATALOG)
    view_path = resolve_path(root, args.view, DEFAULT_VIEW)
    catalog = load_json(catalog_path)
    errors = validate_catalog(catalog, root, strict=False)
    if errors:
        for error in errors:
            print(f"ERRO: {error}", file=sys.stderr)
        return 1
    expected = render_catalog(catalog)
    actual = view_path.read_text(encoding="utf-8") if view_path.is_file() else ""
    if actual != expected:
        print(f"ERRO: visao gerada divergente: {view_path}", file=sys.stderr)
        return 1
    summary = summarize(catalog)
    print("Catalogo e visao validos: "
          f"{len(catalog['surfaces'])} superficies, {summary['cases']} casos")
    return 0


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--root", default=str(ROOT))
    subparsers = command_parser.add_subparsers(dest="command", required=True)
    for name in ("scan", "sync", "validate", "render", "check-rendered"):
        subparser = subparsers.add_parser(name)
        subparser.add_argument("--catalog")
        subparser.add_argument("--view")
        if name in {"sync", "validate"}:
            subparser.add_argument("--coverage-registry")
        if name == "validate":
            subparser.add_argument("--strict", action="store_true")
    return command_parser


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    root = Path(args.root).resolve()
    try:
        handlers = {
            "scan": command_scan,
            "sync": command_sync,
            "validate": command_validate,
            "render": command_render,
            "check-rendered": command_check_rendered,
        }
        return handlers[args.command](args, root)
    except CatalogError as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"ERRO de I/O: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
