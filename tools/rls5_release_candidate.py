"""Audita a candidata documental de release sem publicar ou assinar artefatos."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import release_baseline


SCHEMA = "zephyros-rls5-release-candidate-v1"
RLS1_SCHEMA = "zephyros-rls1-baseline-v1"
RLS4_SCHEMA = "zephyros-rls4-supported-matrix-v1"
BUILD_VERSION = "0.1.0"
TARGET_VERSION = "1.0.0"
CANDIDATE_LABEL = "v0.1.0-rc1"
IMAGE_SIZE_BYTES = 268435456
BOOT_SIZE_BYTES = 512
BOOT_ATTEMPT_LIMIT = 2
RLS1_REPORT = "build/test-results/rls1-baseline/rls1-baseline.json"
RLS4_REPORT = "build/test-results/rls4-supported-matrix/rls4-supported-matrix.json"
OUTPUT_REPORT = "build/test-results/rls5-release/rls5-release.json"
VERSION_HEADER = "src/include/core/version.h"
IMAGE_PATH = "build/zephyros.img"
PERSONAL_PATH_RE = re.compile(
    r"(?:[A-Za-z]:[\\/]|\\\\|/Users/|/home/|/root/|/private/)")
REQUIRED_RLS1_ARTIFACTS = (
    "boot.bin", "stage2.bin", "kernel.bin", "recovery_loader.bin",
    "recovery_loader_padded.bin", "kernel.elf", "zephyros.img",
)


class CandidateError(Exception):
    """Falha de auditoria com estado final deterministico."""

    def __init__(self, code: str, status: str = "FAIL") -> None:
        super().__init__(code)
        self.code = code
        self.status = status


def relative_path(path: Path, root: Path = ROOT) -> str:
    """Converte um caminho do repositório para a forma publicada no relatório."""
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise CandidateError("caminho_fora_do_repositorio") from error


def contains_personal_path(value: object) -> bool:
    """Detecta caminhos absolutos ou pessoais em qualquer valor serializado."""
    if isinstance(value, dict):
        return any(contains_personal_path(key) or contains_personal_path(item)
                   for key, item in value.items())
    if isinstance(value, (list, tuple)):
        return any(contains_personal_path(item) for item in value)
    if isinstance(value, str):
        return bool(PERSONAL_PATH_RE.search(value))
    return False


def sha256_file(path: Path) -> str:
    """Calcula o SHA-256 em blocos sem carregar a imagem inteira."""
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise CandidateError("imagem_leitura_falhou") from error
    return digest.hexdigest()


def image_record(path: Path, root: Path = ROOT) -> dict[str, Any]:
    """Retorna a identidade do artefato de imagem ou bloqueia por ausência."""
    if not path.is_file():
        raise CandidateError("imagem_ausente", "BLOCKED")
    return {
        "path": relative_path(path, root),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def read_json(path: Path, label: str) -> dict[str, Any]:
    """Lê um relatório obrigatório, distinguindo ausência de conteúdo inválido."""
    if not path.is_file():
        raise CandidateError(f"{label}_ausente", "BLOCKED")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CandidateError(f"{label}_invalido") from error
    if not isinstance(value, dict):
        raise CandidateError(f"{label}_formato_invalido")
    return value


def _run_git(root: Path, arguments: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *arguments], cwd=root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CandidateError("git_indisponivel", "BLOCKED") from error
    if result.returncode != 0:
        raise CandidateError("git_consulta_falhou")
    return result.stdout.strip()


def git_state(root: Path) -> dict[str, Any]:
    """Captura commit e limpeza do worktree sem incluir caminhos pessoais."""
    status = _run_git(root, ["status", "--porcelain", "--untracked-files=all"])
    commit = _run_git(root, ["rev-parse", "HEAD"])
    branch = _run_git(root, ["branch", "--show-current"]) or "DETACHED"
    tags = _run_git(root, ["tag", "--points-at", "HEAD"])
    return {
        "commit": commit,
        "branch": branch,
        "worktree_clean": not bool(status),
        "worktree_entry_count": len(status.splitlines()) if status else 0,
        "tags_at_head": tags.splitlines() if tags else [],
    }


def _require_report_identity(report: dict[str, Any], schema: str,
                             label: str) -> None:
    if report.get("schema") != schema:
        raise CandidateError(f"{label}_schema_invalido")
    if report.get("version") != 1:
        raise CandidateError(f"{label}_versao_schema_invalida")
    if report.get("status") != "PASS":
        raise CandidateError(f"{label}_nao_aprovado")


def _artifact_summary(report: dict[str, Any]) -> dict[str, Any]:
    artifacts = report.get("artifacts")
    if not isinstance(artifacts, dict):
        raise CandidateError("rls1_artifacts_invalidos")
    if set(artifacts) != set(REQUIRED_RLS1_ARTIFACTS):
        raise CandidateError("rls1_artifacts_incompletos")
    summary: dict[str, Any] = {}
    for name in REQUIRED_RLS1_ARTIFACTS:
        item = artifacts.get(name)
        if not isinstance(item, dict) or not isinstance(item.get("path"), str):
            raise CandidateError(f"rls1_artifact_{name}_invalido")
        if not item.get("sha256") or not isinstance(item.get("size_bytes"), int):
            raise CandidateError(f"rls1_artifact_{name}_sem_identidade")
        if Path(item["path"]).is_absolute() or contains_personal_path(item["path"]):
            raise CandidateError("rls1_caminho_pessoal")
        summary[name] = {
            "path": item["path"],
            "size_bytes": item["size_bytes"],
            "sha256": item["sha256"],
        }
    return summary


def validate_rls1(report: dict[str, Any], current_commit: str,
                  image: dict[str, Any]) -> dict[str, Any]:
    """Confere a auditoria RLS1 e a proveniência da imagem candidata."""
    _require_report_identity(report, RLS1_SCHEMA, "rls1")
    if report.get("build_version") != BUILD_VERSION:
        raise CandidateError("rls1_build_version_invalida")
    if report.get("target_version") != TARGET_VERSION:
        raise CandidateError("rls1_target_version_invalida")
    if report.get("candidate_state") != "DOCUMENTAL_ONLY":
        raise CandidateError("rls1_candidate_state_invalido")
    source = report.get("source")
    if not isinstance(source, dict) or source.get("commit") != current_commit:
        raise CandidateError("rls1_commit_divergente")
    if not source.get("worktree_clean"):
        raise CandidateError("rls1_worktree_sujo")
    checks = report.get("checks")
    required_checks = (
        "worktree_clean", "boot_512_bytes", "image_size", "layout", "fat32",
        "elf", "updater_audit", "personal_paths_absent",
    )
    if not isinstance(checks, dict) or any(checks.get(name) is not True
                                           for name in required_checks):
        raise CandidateError("rls1_checks_incompletos")
    artifacts = _artifact_summary(report)
    recorded_image = artifacts["zephyros.img"]
    if recorded_image["sha256"] != image["sha256"] or \
            recorded_image["size_bytes"] != image["size_bytes"]:
        raise CandidateError("imagem_rls1_divergente")
    return {
        "schema": report["schema"],
        "status": report["status"],
        "source_commit": source["commit"],
        "artifact_count": len(artifacts),
        "artifacts": artifacts,
    }


def validate_rls4(report: dict[str, Any], image: dict[str, Any],
                  current_commit: str) -> dict[str, Any]:
    """Confere a matriz RLS4 aprovada para a mesma imagem sem reexecutá-la."""
    _require_report_identity(report, RLS4_SCHEMA, "rls4")
    if report.get("passed_sessions") != 57 or report.get("total_sessions") != 57:
        raise CandidateError("rls4_sessoes_incompletas")
    if report.get("passed_complementary") != 30 or \
            report.get("total_complementary") != 30:
        raise CandidateError("rls4_complementares_incomplementares")
    recorded_image = report.get("image")
    if not isinstance(recorded_image, dict) or \
            recorded_image.get("sha256") != image["sha256"] or \
            recorded_image.get("size_bytes") != image["size_bytes"]:
        raise CandidateError("imagem_rls4_divergente")
    coverage = report.get("coverage")
    if not isinstance(coverage, dict) or coverage.get("persistent_image_write") is not False:
        raise CandidateError("rls4_escrita_persistente")
    lanes = report.get("not_applicable_lanes")
    if not isinstance(lanes, list) or not any(
            isinstance(item, dict) and item.get("profile") == "no-vesa" and
            item.get("mode") == "classic" and
            item.get("status") == "NOT_APPLICABLE" for item in lanes):
        raise CandidateError("rls4_lane_nao_aplicavel_ausente")
    source_commit = report.get("source_commit")
    commit_provenance = "image_identity_and_rls1"
    if source_commit is not None:
        if source_commit != current_commit:
            raise CandidateError("rls4_commit_divergente")
        commit_provenance = "rls4_report"
    return {
        "schema": report["schema"],
        "status": report["status"],
        "passed_sessions": report["passed_sessions"],
        "total_sessions": report["total_sessions"],
        "passed_complementary": report["passed_complementary"],
        "total_complementary": report["total_complementary"],
        "run_id": report.get("run_id", "UNKNOWN"),
        "source_commit": source_commit or current_commit,
        "commit_provenance": commit_provenance,
    }


def run_updater_audit(root: Path, image: Path, python_command: str) -> dict[str, Any]:
    """Executa somente a auditoria offline existente do updater."""
    command = [python_command, str(root / "tools" / "updater.py"), "audit-image",
               "--image", str(image), "--expect-version", BUILD_VERSION]
    try:
        result = subprocess.run(
            command, cwd=root, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=60, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CandidateError("updater_auditoria_indisponivel", "BLOCKED") from error
    return {
        "status": "PASS" if result.returncode == 0 else "FAIL",
        "returncode": result.returncode,
        "output_line_count": len((result.stdout or "").splitlines()),
    }


def base_report() -> dict[str, Any]:
    """Cria o envelope inicial versionado da candidata."""
    return {
        "schema": SCHEMA,
        "version": 1,
        "status": "FAIL",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "build_version": BUILD_VERSION,
        "target_version": TARGET_VERSION,
        "candidate_state": "DOCUMENTAL_ONLY",
        "candidate_label": CANDIDATE_LABEL,
        "errors": [],
        "checks": {},
    }


def _status(errors: list[str], blocked: bool) -> str:
    if blocked:
        return "BLOCKED"
    return "PASS" if not errors else "FAIL"


def build_report(root: Path, image_path: Path, rls1_path: Path,
                 rls4_path: Path, version_header: Path,
                 python_command: str) -> dict[str, Any]:
    """Agrega a evidência anterior e produz o relatório RLS5."""
    report = base_report()
    errors: list[str] = []
    blocked = False
    try:
        report["source"] = git_state(root)
        report["checks"]["worktree_clean"] = report["source"]["worktree_clean"]
        if not report["source"]["worktree_clean"]:
            errors.append("worktree_dirty")
        current_commit = report["source"]["commit"]
    except CandidateError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"
        current_commit = "UNKNOWN"

    try:
        version = release_baseline.version_from_header(
            version_header.read_text(encoding="utf-8"))
        report["checks"]["build_version"] = version == BUILD_VERSION
        report["checks"]["version_distinct"] = version != TARGET_VERSION
        if version != BUILD_VERSION:
            raise CandidateError("build_version_invalida")
        if version == TARGET_VERSION:
            raise CandidateError("target_version_nao_distinto")
    except OSError:
        errors.append("version_header_ausente")
    except CandidateError as error:
        errors.append(error.code)

    try:
        image = image_record(image_path, root)
        report["image"] = image
        report["checks"]["image_size"] = image["size_bytes"] == IMAGE_SIZE_BYTES
        if image["size_bytes"] != IMAGE_SIZE_BYTES:
            raise CandidateError("image_size_invalid")
    except CandidateError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"
        image = {"path": relative_path(image_path, root),
                 "size_bytes": "ND", "sha256": "ND"}
        report["image"] = image

    rls1_report: dict[str, Any] | None = None
    try:
        rls1_report = read_json(rls1_path, "rls1_report")
        report["rls1"] = validate_rls1(rls1_report, current_commit, image)
        report["checks"]["rls1"] = True
    except CandidateError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"

    try:
        rls4_report = read_json(rls4_path, "rls4_report")
        report["rls4"] = validate_rls4(rls4_report, image, current_commit)
        report["checks"]["rls4"] = True
    except CandidateError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"

    try:
        audit = run_updater_audit(root, image_path, python_command)
        report["updater_audit"] = audit
        report["checks"]["updater_audit"] = audit["status"] == "PASS"
        if audit["status"] != "PASS":
            errors.append("updater_auditoria_falhou")
    except CandidateError as error:
        errors.append(error.code)
        blocked |= error.status == "BLOCKED"

    report["artifacts"] = {
        "distributable": ["build/zephyros.img"],
        "intermediate": [
            "build/boot.bin", "build/stage2.bin", "build/kernel.bin",
            "build/recovery_loader.bin", "build/recovery_loader_padded.bin",
            "build/kernel.elf",
        ],
    }
    report["recovery_policy"] = {
        "healthy_boot_marker": "update_system_slots_boot_confirm",
        "healthy_state": "boot_state=NONE,pending_slot=NONE",
        "boot_attempt_limit": BOOT_ATTEMPT_LIMIT,
        "rollback_route": "existing-system-slots-fixtures",
        "persistent_image_write": False,
    }
    report["publication"] = {
        "candidate_label": CANDIDATE_LABEL,
        "tag_created": False,
        "signature_created": False,
        "published": False,
    }
    report["limitations"] = [
        "candidate remains 0.1.0 until an explicit future release decision",
        "no-vesa/Classic is NOT_APPLICABLE in the supported matrix",
        "physical hardware is outside the validated QEMU matrix",
        "no network publication or new signing key is performed",
    ]
    report["checks"]["personal_paths_absent"] = not contains_personal_path(report)
    if not report["checks"]["personal_paths_absent"]:
        errors.append("personal_path_present")
    report["errors"] = sorted(set(errors))
    report["status"] = _status(report["errors"], blocked)
    return report


def write_report(path: Path, report: dict[str, Any]) -> None:
    """Grava o relatório no caminho de saída dentro do repositório."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2,
                               sort_keys=True) + "\n", encoding="utf-8")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("collect", nargs="?", default="collect",
                        help="mantido para compatibilidade com os alvos Make")
    result.add_argument("--repo-root", default=".")
    result.add_argument("--image", default=IMAGE_PATH)
    result.add_argument("--rls1-report", default=RLS1_REPORT)
    result.add_argument("--rls4-report", default=RLS4_REPORT)
    result.add_argument("--version-header", default=VERSION_HEADER)
    result.add_argument("--output", default=OUTPUT_REPORT)
    result.add_argument("--python", default=sys.executable)
    return result


def main(argv: Iterable[str] | None = None) -> int:
    arguments = parser().parse_args(list(argv) if argv is not None else None)
    root = Path(arguments.repo_root).resolve()
    output = Path(arguments.output)
    if not output.is_absolute():
        output = root / output
    try:
        relative_path(output, root)
        report = build_report(
            root, root / arguments.image, root / arguments.rls1_report,
            root / arguments.rls4_report, root / arguments.version_header,
            arguments.python)
        write_report(output, report)
    except CandidateError as error:
        report = base_report()
        report["status"] = error.status
        report["errors"] = [error.code]
        write_report(output, report)
    print(f"RLS5 release candidate: {report['status']}")
    print(f"Relatorio: {output}")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
