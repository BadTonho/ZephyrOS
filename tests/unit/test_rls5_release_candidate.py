import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools import rls5_release_candidate as candidate


COMMIT = "a" * 40
IMAGE = {
    "path": "build/zephyros.img",
    "size_bytes": candidate.IMAGE_SIZE_BYTES,
    "sha256": "b" * 64,
}


def _rls1_report() -> dict[str, object]:
    artifacts = {}
    for name in candidate.REQUIRED_RLS1_ARTIFACTS:
        artifacts[name] = {
            "path": "build/" + name,
            "size_bytes": 512,
            "sha256": "c" * 64,
        }
    artifacts["zephyros.img"] = dict(IMAGE)
    return {
        "schema": candidate.RLS1_SCHEMA,
        "version": 1,
        "status": "PASS",
        "build_version": candidate.BUILD_VERSION,
        "target_version": candidate.TARGET_VERSION,
        "candidate_state": "DOCUMENTAL_ONLY",
        "source": {"commit": COMMIT, "worktree_clean": True},
        "checks": {
            key: True for key in (
                "worktree_clean", "boot_512_bytes", "image_size", "layout",
                "fat32", "elf", "updater_audit", "personal_paths_absent",
            )
        },
        "artifacts": artifacts,
    }


def _rls4_report() -> dict[str, object]:
    return {
        "schema": candidate.RLS4_SCHEMA,
        "version": 1,
        "status": "PASS",
        "passed_sessions": 57,
        "total_sessions": 57,
        "passed_complementary": 30,
        "total_complementary": 30,
        "run_id": "run-test",
        "image": dict(IMAGE),
        "coverage": {"persistent_image_write": False},
        "not_applicable_lanes": [{
            "profile": "no-vesa", "mode": "classic",
            "status": "NOT_APPLICABLE",
        }],
    }


class Rls5ReleaseCandidateTests(unittest.TestCase):
    def _write_reports(self, root: Path) -> tuple[Path, Path]:
        rls1 = root / "rls1.json"
        rls4 = root / "rls4.json"
        rls1.write_text(json.dumps(_rls1_report()), encoding="utf-8")
        rls4.write_text(json.dumps(_rls4_report()), encoding="utf-8")
        return rls1, rls4

    def _build(self, root: Path, **overrides: object) -> dict[str, object]:
        default_rls1 = root / "rls1.json"
        default_rls4 = root / "rls4.json"
        values = {
            "root": root,
            "image_path": root / "build/zephyros.img",
            "rls1_path": default_rls1,
            "rls4_path": default_rls4,
            "version_header": root / "src/include/core/version.h",
            "python_command": "python",
        }
        git_result = {
            "commit": COMMIT, "branch": "main", "worktree_clean": True,
            "worktree_entry_count": 0, "tags_at_head": []}
        image_result = dict(IMAGE)
        audit_result: object = {
            "status": "PASS", "returncode": 0, "output_line_count": 2}
        if "git_result" in overrides:
            git_result = overrides.pop("git_result")
        if "image_result" in overrides:
            image_result = overrides.pop("image_result")
        if "audit_result" in overrides:
            audit_result = overrides.pop("audit_result")
        values.update(overrides)
        if "rls1_path" not in overrides:
            default_rls1.write_text(json.dumps(_rls1_report()), encoding="utf-8")
        if "rls4_path" not in overrides:
            default_rls4.write_text(json.dumps(_rls4_report()), encoding="utf-8")
        values["version_header"].parent.mkdir(parents=True, exist_ok=True)
        values["version_header"].write_text(
            '#define ZEPHYROS_VERSION_MAJOR 0U\n'
            '#define ZEPHYROS_VERSION_MINOR 1U\n'
            '#define ZEPHYROS_VERSION_PATCH 0U\n'
            '#define ZEPHYROS_VERSION_TEXT "0.1.0"\n', encoding="utf-8")
        audit_patch = patch("tools.rls5_release_candidate.run_updater_audit",
                            return_value=audit_result)
        if isinstance(audit_result, BaseException):
            audit_patch = patch(
                "tools.rls5_release_candidate.run_updater_audit",
                side_effect=audit_result)
        with patch("tools.rls5_release_candidate.git_state",
                   return_value=git_result), \
                patch("tools.rls5_release_candidate.image_record",
                      return_value=image_result), audit_patch:
            return candidate.build_report(**values)

    def test_pass_preserves_candidate_versions_and_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self._build(Path(directory))
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["schema"], candidate.SCHEMA)
        self.assertEqual(report["build_version"], "0.1.0")
        self.assertEqual(report["target_version"], "1.0.0")
        self.assertEqual(report["candidate_state"], "DOCUMENTAL_ONLY")
        self.assertEqual(report["candidate_label"], "v0.1.0-rc1")
        self.assertEqual(report["recovery_policy"]["boot_attempt_limit"], 2)
        self.assertFalse(report["publication"]["tag_created"])
        self.assertFalse(report["publication"]["signature_created"])
        self.assertFalse(report["publication"]["published"])

    def test_requires_rls1_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            root.joinpath("src/include/core").mkdir(parents=True)
            root.joinpath("src/include/core/version.h").write_text(
                '#define ZEPHYROS_VERSION_TEXT "0.1.0"\n', encoding="utf-8")
            report = self._build(root, rls1_path=root / "missing.json")
        self.assertEqual(report["status"], "BLOCKED")
        self.assertIn("rls1_report_ausente", report["errors"])

    def test_rejects_rls4_image_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rls1, rls4 = self._write_reports(root)
            value = _rls4_report()
            value["image"] = dict(IMAGE, sha256="d" * 64)
            rls4.write_text(json.dumps(value), encoding="utf-8")
            report = self._build(root, rls1_path=rls1, rls4_path=rls4)
        self.assertEqual(report["status"], "FAIL")
        self.assertIn("imagem_rls4_divergente", report["errors"])

    def test_dirty_worktree_is_not_a_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self._build(root, git_result={
                "commit": COMMIT, "branch": "main", "worktree_clean": False,
                "worktree_entry_count": 1, "tags_at_head": []})
        self.assertEqual(report["status"], "FAIL")
        self.assertIn("worktree_dirty", report["errors"])

    def test_missing_updater_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self._build(root, audit_result=candidate.CandidateError(
                "updater_auditoria_indisponivel", "BLOCKED"))
        self.assertEqual(report["status"], "BLOCKED")
        self.assertIn("updater_auditoria_indisponivel", report["errors"])

    def test_rejects_r1_version_and_missing_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rls1, _ = self._write_reports(root)
            value = _rls1_report()
            value["build_version"] = "1.0.0"
            value["checks"]["elf"] = False
            rls1.write_text(json.dumps(value), encoding="utf-8")
            report = self._build(root, rls1_path=rls1)
        self.assertEqual(report["status"], "FAIL")
        self.assertIn("rls1_build_version_invalida", report["errors"])

    def test_rejects_incomplete_rls4_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rls1, rls4 = self._write_reports(root)
            value = _rls4_report()
            value["passed_sessions"] = 56
            rls4.write_text(json.dumps(value), encoding="utf-8")
            report = self._build(root, rls1_path=rls1, rls4_path=rls4)
        self.assertEqual(report["status"], "FAIL")
        self.assertIn("rls4_sessoes_incompletas", report["errors"])

    def test_personal_paths_are_detected(self):
        self.assertTrue(candidate.contains_personal_path("C:/Users/Admin/file"))
        self.assertTrue(candidate.contains_personal_path({"path": "/home/user/file"}))
        self.assertFalse(candidate.contains_personal_path("build/zephyros.img"))

    def test_relative_path_rejects_external_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(candidate.CandidateError,
                                        "caminho_fora_do_repositorio"):
                candidate.relative_path(Path(directory).parent / "outside", root)

    def test_rls4_without_explicit_commit_uses_image_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = candidate.validate_rls4(_rls4_report(), IMAGE, COMMIT)
        self.assertEqual(result["source_commit"], COMMIT)
        self.assertEqual(result["commit_provenance"], "image_identity_and_rls1")

    def test_report_writer_and_parser_are_stable(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.json"
            candidate.write_report(path, candidate.base_report())
            loaded = json.loads(path.read_text(encoding="utf-8"))
            args = candidate.parser().parse_args([])
        self.assertEqual(loaded["schema"], candidate.SCHEMA)
        self.assertEqual(args.output, candidate.OUTPUT_REPORT)

    def test_image_hash_and_json_helpers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "build/zephyros.img"
            image.parent.mkdir(parents=True)
            image.write_bytes(b"fixture-image")
            record = candidate.image_record(image, root)
            self.assertEqual(record["path"], "build/zephyros.img")
            self.assertEqual(record["sha256"], candidate.sha256_file(image))
            report = root / "report.json"
            report.write_text('{"status": "PASS"}', encoding="utf-8")
            self.assertEqual(candidate.read_json(report, "report")["status"],
                             "PASS")
            report.write_text("{invalid", encoding="utf-8")
            with self.assertRaisesRegex(candidate.CandidateError,
                                        "report_invalido"):
                candidate.read_json(report, "report")

    def test_git_state_and_git_command_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch("tools.rls5_release_candidate.subprocess.run",
                       return_value=SimpleNamespace(returncode=0,
                                                    stdout="ok\n")) as run:
                self.assertEqual(candidate._run_git(root, ["status"]), "ok")
                run.assert_called_once()
            with patch("tools.rls5_release_candidate.subprocess.run",
                       return_value=SimpleNamespace(returncode=1,
                                                    stdout="")):
                with self.assertRaisesRegex(candidate.CandidateError,
                                            "git_consulta_falhou"):
                    candidate._run_git(root, ["status"])
            with patch("tools.rls5_release_candidate._run_git",
                       side_effect=[" M file", COMMIT, "main", "v0.1.0"]):
                state = candidate.git_state(root)
        self.assertFalse(state["worktree_clean"])
        self.assertEqual(state["worktree_entry_count"], 1)
        self.assertEqual(state["tags_at_head"], ["v0.1.0"])

    def test_updater_audit_and_main_success_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = SimpleNamespace(returncode=0, stdout="line1\nline2\n")
            with patch("tools.rls5_release_candidate.subprocess.run",
                       return_value=result) as run:
                audit = candidate.run_updater_audit(
                    root, root / "build/zephyros.img", "python")
            command = run.call_args.args[0]
            self.assertIn("audit-image", command)
            self.assertEqual(audit["status"], "PASS")
            self.assertEqual(audit["output_line_count"], 2)
            report = candidate.base_report()
            report["status"] = "PASS"
            output = root / "candidate.json"
            with patch("tools.rls5_release_candidate.build_report",
                       return_value=report) as build, \
                    patch("tools.rls5_release_candidate.write_report") as write:
                self.assertEqual(candidate.main([
                    "collect", "--repo-root", str(root),
                    "--output", str(output)]), 0)
            build.assert_called_once()
            write.assert_called_once_with(output, report)

    def test_status_mapping_is_deterministic(self):
        self.assertEqual(candidate._status([], False), "PASS")
        self.assertEqual(candidate._status(["error"], False), "FAIL")
        self.assertEqual(candidate._status(["error"], True), "BLOCKED")


if __name__ == "__main__":
    unittest.main()
