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
SCHEMA = "zephyros-test-catalog-v1"
EXCLUDED_SOURCE_PARTS = {"vendor", "build", "generated"}
SURFACE_STATUSES = {"PENDING", "COVERED", "MANUAL", "BLOCKED"}
CASE_STATUSES = {"PENDING", "AUTOMATED", "MANUAL", "BLOCKED"}
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
    return result


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
        if not isinstance(item.get("surface_ids", []), list):
            errors.append(f"surface_ids invalido: {identifier}")
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
        case_ids = item.get("case_ids")
        if not isinstance(case_ids, list):
            errors.append(f"case_ids invalido: {identifier}")
        else:
            for case_id in case_ids:
                if case_id not in case_map:
                    errors.append(f"caso ausente {case_id} na superficie {identifier}")
        if strict and item.get("status") in {"PENDING", "BLOCKED"}:
            errors.append(f"cobertura pendente ou bloqueada: {identifier}")
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
    kinds = sorted(key for key in summary if not key.startswith("status:")
                   and key not in {"cases", "retired"})
    for kind in kinds:
        lines.append(f"| `{kind}` | {summary[kind]} |")
    lines.extend(["", "| Cobertura | Quantidade |", "|---|---:|"])
    for status in sorted(SURFACE_STATUSES):
        lines.append(f"| `{status}` | {summary.get(f'status:{status}', 0)} |")
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
        lines.extend(["| ID | Proprietario | Camada | Status | Pre-condicoes | Acao | Resultado esperado | Erros | Efeitos | Limpeza |", "|---|---|---|---|---|---|---|---|---|---|"])
        for case in catalog["cases"]:
            values = {key: str(case[key]).replace("|", "\\|").replace("\n", " ")
                      for key in ("id", "owner", "layer", "status", "preconditions",
                                  "action", "expected", "errors", "effects", "cleanup")}
            lines.append("| `{id}` | `{owner}` | `{layer}` | `{status}` | {preconditions} | {action} | {expected} | {errors} | {effects} | {cleanup} |".format(**values))
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
    write_json(path, result)
    print(f"Catalogo sincronizado: {len(result['surfaces'])} superficies")
    return 0


def command_validate(args: argparse.Namespace, root: Path) -> int:
    path = resolve_path(root, args.catalog, DEFAULT_CATALOG)
    catalog = load_json(path)
    errors = validate_catalog(catalog, root, strict=args.strict)
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
