#!/usr/bin/env python3
"""Gate leve para as regras de mudanca da etapa Q3."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


BOOT_PATH = "src/boot/boot.asm"
CATALOG_PATH = "docs/qualidade/contratos-publicos.md"
METRICS_PATH = "docs/qualidade/metricas.md"
ERROR_RETURN_RE = re.compile(r"\breturn\s+(ERR_[A-Z0-9_]+)\s*;")
FUNCTION_RE = re.compile(
    r"(?m)^[ \t]*(?:static\s+)?(?:inline\s+)?int\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{"
)
LOG_RE = re.compile(r"\bLOG_(?:ERROR|WARN)\s*\(")
CATALOG_ROW_RE = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|")
METRIC_RECORD_RE = re.compile(r"(?m)^### (\d{4}-\d{2}-\d{2} - .+)$")
METRIC_FIELDS = (
    "Cenario QEMU:",
    "Metrica observavel:",
    "Antes:",
    "Depois:",
    "Conclusao:",
    "Impacto:",
)


def run_git(repo: Path, args: list[str], check: bool = True) -> str:
    """Executa Git no repositorio informado e retorna a saida textual."""
    result = subprocess.run(
        ["git", *args],
        cwd=repo,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if check and result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(message or "Git retornou erro desconhecido")
    return result.stdout


def split_paths(raw_paths: str) -> set[str]:
    """Converte uma lista Git separada por NUL em caminhos normalizados."""
    return {path.replace("\\", "/") for path in raw_paths.split("\0") if path}


def changed_paths(repo: Path) -> set[str]:
    """Retorna arquivos staged, unstaged e nao rastreados no diretorio atual."""
    tracked = run_git(repo, ["diff", "--name-only", "-z", "HEAD"])
    untracked = run_git(repo, ["ls-files", "--others", "--exclude-standard", "-z"])
    return split_paths(tracked) | split_paths(untracked)


def read_head_file(repo: Path, path: str) -> str:
    """Le um arquivo da revisao HEAD, retornando vazio quando ainda nao existe."""
    result = subprocess.run(
        ["git", "show", f"HEAD:{path}"],
        cwd=repo,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout if result.returncode == 0 else ""


def read_worktree_file(repo: Path, path: str) -> str:
    """Le um arquivo do diretorio de trabalho, se ele ainda existir."""
    file_path = repo / path
    if not file_path.is_file():
        return ""
    return file_path.read_text(encoding="utf-8", errors="replace")


def find_matching_brace(text: str, opening_index: int) -> int:
    """Localiza a chave de fechamento sem interpretar chaves em strings."""
    depth = 0
    index = opening_index
    state = "code"
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if state == "code" and char == '"':
            state = "string"
        elif state == "code" and char == "'":
            state = "char"
        elif state == "code" and char == "/" and next_char == "/":
            state = "line_comment"
            index += 1
        elif state == "code" and char == "/" and next_char == "*":
            state = "block_comment"
            index += 1
        elif state == "string" and char == "\\":
            index += 1
        elif state == "char" and char == "\\":
            index += 1
        elif state == "string" and char == '"':
            state = "code"
        elif state == "char" and char == "'":
            state = "code"
        elif state == "line_comment" and char == "\n":
            state = "code"
        elif state == "block_comment" and char == "*" and next_char == "/":
            state = "code"
            index += 1
        elif state == "code" and char == "{":
            depth += 1
        elif state == "code" and char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def extract_int_functions(text: str) -> dict[str, str]:
    """Extrai funcoes C de retorno int para a verificacao limitada do Q3."""
    functions: dict[str, str] = {}
    for match in FUNCTION_RE.finditer(text):
        opening_index = text.find("{", match.start(), match.end())
        closing_index = find_matching_brace(text, opening_index)
        if closing_index >= 0:
            functions[match.group(1)] = text[opening_index : closing_index + 1]
    return functions


def parse_catalog(text: str) -> dict[str, str]:
    """Le o mapa de header publico para documento tecnico canonico."""
    catalog: dict[str, str] = {}
    for line in text.splitlines():
        match = CATALOG_ROW_RE.match(line)
        if match:
            catalog[match.group(1)] = match.group(2)
    return catalog


def check_whitespace(repo: Path) -> list[str]:
    """Retorna whitespace invalido do diff Git e de arquivos ainda novos."""
    result = subprocess.run(
        ["git", "diff", "--check", "HEAD"],
        cwd=repo,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    errors = [line for line in result.stdout.splitlines() if line]
    untracked = split_paths(run_git(repo, ["ls-files", "--others", "--exclude-standard", "-z"]))
    for path in sorted(untracked):
        file_path = repo / path
        if not file_path.is_file():
            continue
        content = file_path.read_bytes()
        if b"\0" in content:
            continue
        for line_number, line in enumerate(content.splitlines(), start=1):
            if line.rstrip(b" \t") != line:
                errors.append(f"{path}:{line_number}: trailing whitespace.")
    return errors


def check_new_error_functions(repo: Path, paths: set[str]) -> list[str]:
    """Exige log nas novas funcoes int que retornam um codigo ERR_ padrao."""
    errors: list[str] = []
    for path in sorted(paths):
        if not path.startswith("src/") or not path.endswith(".c"):
            continue
        before = extract_int_functions(read_head_file(repo, path))
        after = extract_int_functions(read_worktree_file(repo, path))
        for name, body in after.items():
            if name in before or not ERROR_RETURN_RE.search(body):
                continue
            if not LOG_RE.search(body):
                errors.append(f"{path}: nova funcao {name} retorna ERR_* sem LOG_ERROR/WARN")
    return errors


def check_public_contracts(repo: Path, paths: set[str]) -> list[str]:
    """Exige documento canonico atualizado para cada header publico alterado."""
    headers = {path for path in paths if path.startswith("src/include/") and path.endswith(".h")}
    if not headers:
        return []
    current_catalog = parse_catalog(read_worktree_file(repo, CATALOG_PATH))
    head_catalog = parse_catalog(read_head_file(repo, CATALOG_PATH))
    errors: list[str] = []
    for header in sorted(headers):
        document = current_catalog.get(header) or head_catalog.get(header)
        if not document:
            errors.append(f"{header}: header sem documento canonico no catalogo")
        elif document not in paths:
            errors.append(f"{header}: atualize tambem {document}")
    return errors


def check_metric_records(repo: Path) -> list[str]:
    """Valida a estrutura das evidencias de otimizacao ja registradas."""
    text = read_worktree_file(repo, METRICS_PATH)
    if not text:
        return [f"{METRICS_PATH}: registro de metricas ausente"]
    records_marker = "## Registros"
    if records_marker not in text:
        return [f"{METRICS_PATH}: secao de registros ausente"]
    records = text.split(records_marker, maxsplit=1)[1]
    matches = list(METRIC_RECORD_RE.finditer(records))
    if "Nenhuma otimizacao registrada" not in records and not matches:
        return [f"{METRICS_PATH}: registro sem cabecalho AAAA-MM-DD - resumo"]
    errors: list[str] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(records)
        body = records[match.end() : end]
        missing = [field for field in METRIC_FIELDS if field not in body]
        if missing:
            errors.append(f"{METRICS_PATH}: registro {match.group(1)} sem {', '.join(missing)}")
    return errors


def collect_results(repo: Path) -> dict[str, list[str]]:
    """Executa todas as regras do Q3 para um diretorio de trabalho Git."""
    paths = changed_paths(repo)
    return {
        "whitespace": check_whitespace(repo),
        "boot_protegido": ["src/boot/boot.asm foi alterado"] if BOOT_PATH in paths else [],
        "funcoes_falhaveis": check_new_error_functions(repo, paths),
        "contratos_publicos": check_public_contracts(repo, paths),
        "registro_metricas": check_metric_records(repo),
    }


def print_results(results: dict[str, list[str]]) -> int:
    """Exibe o resumo compacto e retorna o codigo final do gate."""
    print("Q3Check:")
    failed = False
    for label, errors in results.items():
        status = "ERRO" if errors else "OK"
        print(f"  {label} {status}")
        for error in errors:
            print(f"    {error}")
        failed = failed or bool(errors)
    print(f"  resultado {'ERRO' if failed else 'OK'}")
    return 1 if failed else 0


def write_file(path: Path, content: str) -> None:
    """Cria arquivo de fixture com diretorios pais quando necessario."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def initialize_fixture(repo: Path) -> None:
    """Prepara um repositorio minimo para os casos controlados do self-test."""
    run_git(repo, ["init"])
    write_file(repo / "README.md", "fixture\n")
    write_file(
        repo / CATALOG_PATH,
        "# Catalogo\n\n| Header publico | Documento canonico |\n"
        "|---|---|\n| `src/include/core/example.h` | `docs/exemplo.md` |\n",
    )
    write_file(repo / "docs/exemplo.md", "# Exemplo\n")
    write_file(
        repo / METRICS_PATH,
        "# Metricas\n\n## Modelo de registro\n\n## Registros\n\n"
        "Nenhuma otimizacao registrada ate o momento.\n",
    )
    write_file(repo / "src/include/core/example.h", "#ifndef EXAMPLE_H\n#define EXAMPLE_H\n#endif\n")
    write_file(repo / "src/example.c", "int example_ok(void) { return 0; }\n")
    run_git(repo, ["add", "."])
    run_git(repo, ["-c", "user.name=Q3Check", "-c", "user.email=q3check@example.invalid", "commit", "-m", "fixture"])


def self_test_case(name: str, action, expected: int) -> tuple[str, bool, dict[str, list[str]]]:
    """Executa um caso isolado e compara o resultado com o esperado."""
    with tempfile.TemporaryDirectory(prefix="zephyros-q3check-") as temp_dir:
        repo = Path(temp_dir)
        initialize_fixture(repo)
        action(repo)
        results = collect_results(repo)
        result = 1 if any(results.values()) else 0
        return name, result == expected, results


def run_self_test() -> int:
    """Valida caminhos aprovados e rejeitados sem alterar o repositorio real."""
    cases = [
        ("limpo", lambda repo: None, 0),
        ("boot", lambda repo: write_file(repo / BOOT_PATH, "alterado\n"), 1),
        (
            "funcao_sem_log",
            lambda repo: write_file(repo / "src/new.c", "int new_fail(void) { return ERR_STATE; }\n"),
            1,
        ),
        (
            "header_sem_documento",
            lambda repo: write_file(repo / "src/include/core/example.h", "#define EXAMPLE 1\n"),
            1,
        ),
        (
            "whitespace",
            lambda repo: write_file(repo / "new.txt", "fixture  \n"),
            1,
        ),
        (
            "metrica_invalida",
            lambda repo: write_file(
                repo / METRICS_PATH,
                "# Metricas\n\n## Registros\n\n### 2026-07-24 - teste\n"
                "- Cenario QEMU: teste\n",
            ),
            1,
        ),
        (
            "metrica_valida",
            lambda repo: write_file(
                repo / METRICS_PATH,
                "# Metricas\n\n## Registros\n\n### 2026-07-24 - teste\n"
                "- Cenario QEMU: teste\n- Metrica observavel: ticks\n"
                "- Antes: 10\n- Depois: 9\n- Conclusao: ganho\n"
                "- Impacto: contratos preservados\n",
            ),
            0,
        ),
        (
            "contrato_valido",
            lambda repo: (
                write_file(repo / "src/include/core/example.h", "#define EXAMPLE 1\n"),
                write_file(repo / "docs/exemplo.md", "# Exemplo atualizado\n"),
                write_file(repo / "src/new.c", "int new_fail(void) { LOG_WARN(\"TEST\", \"Falha\"); return ERR_STATE; }\n"),
            ),
            0,
        ),
    ]
    if shutil.which("git") is None:
        print("Q3Check self-test: ERRO (Git indisponivel)")
        return 1
    passed = True
    for name, action, expected in cases:
        case_name, approved, results = self_test_case(name, action, expected)
        print(f"selftest_{case_name} {'OK' if approved else 'ERRO'}")
        if not approved:
            print_results(results)
        passed = passed and approved
    print(f"Q3Check self-test {'OK' if passed else 'ERRO'}")
    return 0 if passed else 1


def main() -> int:
    """Interpreta os argumentos e executa o gate solicitado."""
    parser = argparse.ArgumentParser(description="Gate leve da etapa Q3")
    parser.add_argument("--self-test", action="store_true", help="executa fixtures temporarias")
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()
    try:
        return print_results(collect_results(Path.cwd()))
    except RuntimeError as error:
        print(f"Q3Check: ERRO ({error})")
        return 1


if __name__ == "__main__":
    sys.exit(main())
