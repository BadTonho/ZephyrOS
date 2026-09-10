import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools import qemu_parallel_runner as runner


def case(case_id, tags=None, profile="smoke"):
    return {
        "id": case_id,
        "scenario": "caso QEMU fixture",
        "owner": "quality",
        "layer": "qemu",
        "executor": "qemu",
        "profile": profile,
        "guest_case": case_id,
        "timeout_seconds": 10,
        "heartbeat_timeout_seconds": 2,
        "isolation": "snapshot",
        "parameters": {},
        "preconditions": "imagem pronta",
        "action": "executar caso",
        "expected": "PASS",
        "errors": "falha ou timeout",
        "effects": "snapshot",
        "cleanup": "encerrar QEMU",
        "surface_ids": [],
        "tags": tags or ["qemu", "smoke"],
        "status": "AUTOMATED",
        "qemu_profile": "baseline",
    }


def arguments(*extra):
    return runner.parser().parse_args([
        "parallel", "--profile", "smoke", "--workers", "2",
        "--image", "build/zephyros.img", "--catalog", "tests/catalog.json",
        *extra,
    ])


class FakeProcess:
    next_pid = 1000

    def __init__(self):
        self.pid = FakeProcess.next_pid
        FakeProcess.next_pid += 1
        self.returncode = 0

    def poll(self):
        return self.returncode

    def communicate(self, timeout=None):
        return b"fake stdout\n", b""


class SelectionTests(unittest.TestCase):
    def test_select_by_profile_and_tag(self):
        catalog = {"cases": [
            case("qemu:one", ["qemu", "apps"]),
            case("qemu:two", ["qemu", "storage"]),
            case("qemu:three", ["qemu", "fault"], "tst6"),
        ]}
        args = arguments("--tag", "apps")
        self.assertEqual(
            [item["id"] for item in runner.select_cases(catalog, args, "parallel")],
            ["qemu:one"],
        )

    def test_profile_and_tag_are_combined(self):
        catalog = {"cases": [
            case("qemu:smoke-apps", ["qemu", "apps"], "smoke"),
            case("qemu:matrix-apps", ["qemu", "apps"], "matrix"),
            case("qemu:smoke-storage", ["qemu", "storage"], "smoke"),
        ]}
        args = arguments("--tag", "apps")
        self.assertEqual(
            [item["id"] for item in runner.select_cases(catalog, args, "parallel")],
            ["qemu:smoke-apps"],
        )

    def test_exact_case_rejects_duplicates_and_missing_ids(self):
        catalog = {"cases": [case("qemu:one")]}
        duplicate = arguments("--profile", "smoke")
        duplicate.case = ["qemu:one", "qemu:one"]
        with self.assertRaisesRegex(runner.ParallelError, "caso_duplicado"):
            runner.select_cases(catalog, duplicate, "parallel")
        missing = arguments("--profile", "smoke")
        missing.case = ["qemu:missing"]
        with self.assertRaisesRegex(runner.ParallelError, "caso_inexistente"):
            runner.select_cases(catalog, missing, "parallel")

    def test_soak_default_uses_declared_tags(self):
        catalog = {"cases": [
            case("qemu:apps", ["qemu", "apps"]),
            case("qemu:stress", ["qemu", "stress"]),
            case("qemu:matrix", ["qemu", "matrix"]),
        ]}
        args = runner.parser().parse_args(["soak"])
        self.assertEqual(
            [item["id"] for item in runner.select_cases(catalog, args, "soak")],
            ["qemu:apps", "qemu:stress"],
        )


class ContractTests(unittest.TestCase):
    def test_workers_and_selection_are_bounded(self):
        args = arguments("--workers", "65")
        self.assertEqual(
            runner.validate_selection_arguments(args, "parallel"),
            "workers_fora_do_limite",
        )
        args = runner.parser().parse_args(["parallel"])
        self.assertEqual(
            runner.validate_selection_arguments(args, "parallel"),
            "selecao_deve_conter_case_profile_tag_ou_all",
        )
        args = runner.parser().parse_args(["soak", "--all"])
        self.assertEqual(
            runner.validate_selection_arguments(args, "soak"),
            "soak_usa_tags",
        )
        args = runner.parser().parse_args([
            "soak", "--suite-timeout", "7200"])
        self.assertIsNone(runner.validate_selection_arguments(args, "soak"))

    def test_seed_is_stable_and_case_specific(self):
        first = runner.stable_seed(12345, "qemu:apps", 1)
        self.assertEqual(first, runner.stable_seed(12345, "qemu:apps", 1))
        self.assertNotEqual(first, runner.stable_seed(12345, "qemu:apps", 2))
        self.assertNotEqual(first, runner.stable_seed(12345, "qemu:storage", 1))

    def test_case_command_preserves_profile_and_snapshot_contract(self):
        args = arguments()
        command = runner.case_command(
            case("qemu:apps"), args, "qpp-child", Path("build/results"), 99)
        self.assertIn("qemu_test_runner.py", command[1])
        self.assertIn("--qemu-profile", command)
        self.assertIn("baseline", command)
        self.assertNotIn("--no-snapshot", command)

    def test_aggregate_status_preserves_failure_and_stop(self):
        results = [{"id": "qemu:a", "status": "PASS"},
                   {"id": "qemu:b", "status": "FAIL"}]
        self.assertEqual(runner.aggregate_status(results, False),
                         ("FAIL", "caso_reprovado"))
        self.assertEqual(runner.aggregate_status(results, True),
                         ("STOPPED", "interrompido_pelo_usuario"))


class ExecutionTests(unittest.TestCase):
    def test_parallel_execution_is_mocked_per_case_and_writes_report(self):
        catalog = {"schema": "zephyros-test-catalog-v1", "cases": [
            case("qemu:apps", ["qemu", "apps"]),
            case("qemu:storage", ["qemu", "storage"]),
        ]}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "zephyros.img"
            catalog_path = root / "catalog.json"
            results = root / "results"
            image.write_bytes(b"image")
            catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
            args = runner.parser().parse_args([
                "parallel", "--profile", "smoke", "--workers", "2",
                "--image", str(image), "--catalog", str(catalog_path),
                "--results", str(results), "--seed", "123",
            ])
            with patch.object(runner, "git_revision", return_value="test"), \
                    patch.object(runner, "environment", return_value={}), \
                    patch.object(runner.subprocess, "Popen",
                                 side_effect=lambda *a, **k: FakeProcess()):
                self.assertEqual(runner.run_parallel(args, "parallel"), 0)
            report_path = next(results.glob("*/result.json"))
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "PASS")
            self.assertEqual(len(report["cases"]), 2)
            self.assertEqual({item["status"] for item in report["cases"]}, {"PASS"})
            self.assertTrue((report_path.parent / "manifest.json").is_file())

    def test_worker_exception_is_reported_without_losing_other_cases(self):
        catalog = {"schema": "zephyros-test-catalog-v1", "cases": [
            case("qemu:apps", ["qemu", "apps"]),
            case("qemu:storage", ["qemu", "storage"]),
        ]}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "zephyros.img"
            catalog_path = root / "catalog.json"
            results = root / "results"
            image.write_bytes(b"image")
            catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
            args = runner.parser().parse_args([
                "parallel", "--profile", "smoke", "--workers", "2",
                "--image", str(image), "--catalog", str(catalog_path),
                "--results", str(results), "--seed", "123",
            ])

            def fake_run_case(selected_case, *_args):
                if selected_case["id"] == "qemu:apps":
                    raise RuntimeError("mock worker failure")
                return {"id": selected_case["id"], "status": "PASS"}

            with patch.object(runner, "git_revision", return_value="test"), \
                    patch.object(runner, "environment", return_value={}), \
                    patch.object(runner, "run_case", side_effect=fake_run_case):
                self.assertEqual(runner.run_parallel(args, "parallel"), 1)
            report_path = next(results.glob("*/result.json"))
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "FAIL")
            self.assertEqual({item["id"] for item in report["cases"]},
                             {"qemu:apps", "qemu:storage"})
            self.assertIn("worker_exception", report["failure_groups"][0]["signature"])


if __name__ == "__main__":
    unittest.main()
