import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import tst7_regression_runner as runner


def report(status="PASS", duration=10.0, warnings=None, covered=True):
    surface_statuses = {"c:src/core/test.c:check": "COVERED" if covered else "PENDING"}
    return {
        "schema": runner.SCHEMA,
        "mode": "full",
        "execution_status": status,
        "status": status,
        "environment": {"os": "test", "python": "3", "make": "make", "qemu": "qemu"},
        "warnings": warnings or {},
        "coverage": {
            "surface_statuses": surface_statuses,
            "covered_surface_ids": [
                "c:src/core/test.c:check"] if covered else [],
                "automated_case_ids": ["qemu:tst7:test"],
        },
        "contract": {"entries": {
            "qemu:qemu:tst7:test": {
                "status": "PASS" if status == "PASS" else status,
                "termination": "completed",
                "phase": "PASS",
                "first_error": None,
                "events": ["READY", "HEARTBEAT", "BEGIN", "PASS"],
                "duration_seconds": duration,
            },
        }},
    }


class Tst7ComparisonTests(unittest.TestCase):
    def test_missing_baseline_is_blocked(self):
        result = runner.compare_runs(report(), None)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertEqual(result["cause"], "baseline_ausente")

    def test_pass_to_fail_has_priority_over_blocked(self):
        current = report("FAIL")
        result = runner.compare_runs(current, report())
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any("pass_para_falha" in item
                            for item in result["reasons"]))

    def test_timeout_is_a_real_failure(self):
        current = report("TIMEOUT")
        result = runner.compare_runs(current, report())
        self.assertEqual(result["status"], "FAIL")

    def test_pass_to_blocked_stays_blocked(self):
        current = report("BLOCKED")
        result = runner.compare_runs(current, report())
        self.assertEqual(result["status"], "BLOCKED")

    def test_duration_requires_both_thresholds(self):
        current = report(duration=14.0)
        baseline = report(duration=10.0)
        self.assertEqual(runner.compare_runs(current, baseline)["status"], "PASS")
        current = report(duration=18.1)
        result = runner.compare_runs(current, baseline)
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["duration"]["status"], "REGRESSION")

    def test_new_warning_is_detected(self):
        current = report(warnings={"qemu:test:warning nova": 1})
        result = runner.compare_runs(current, report())
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["warnings"]["status"], "FAIL")

    def test_coverage_loss_is_detected(self):
        result = runner.compare_runs(report(covered=False), report())
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any("cobertura_perdida" in item
                            for item in result["reasons"]))

    def test_environment_difference_does_not_fail_duration(self):
        current = report(duration=100.0)
        baseline = report(duration=10.0)
        current["environment"]["qemu"] = "other"
        result = runner.compare_runs(current, baseline)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["duration"]["status"], "NOT_COMPARABLE:environment")

    def test_setup_duration_is_not_a_case_regression(self):
        current = report(duration=10.0)
        baseline = report(duration=10.0)
        current["contract"]["entries"]["host:build"] = {
            "status": "PASS",
            "termination": "completed",
            "phase": "build",
            "first_error": None,
            "events": [],
            "duration_seconds": 134.516,
        }
        baseline["contract"]["entries"]["host:build"] = {
            "status": "PASS",
            "termination": "completed",
            "phase": "build",
            "first_error": None,
            "events": [],
            "duration_seconds": 110.375,
        }
        result = runner.compare_runs(current, baseline)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["duration"]["status"], "PASS")


class Tst7RunnerContractTests(unittest.TestCase):
    def test_host_case_allowlist_is_explicit(self):
        catalog = {"cases": [
            {"id": "host:tst2:protocol-core", "status": "AUTOMATED",
             "executor": "host"},
            {"id": "host:unknown", "status": "AUTOMATED",
             "executor": "host"},
        ]}
        self.assertEqual(
            [item["id"] for item in runner.host_cases(catalog)],
            ["host:tst2:protocol-core", "host:unknown"])
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:tst3:string-compress"],
            "test-tst3-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:core:wifi-manager"],
            "test-wifi-manager-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:core:usb-manager"],
            "test-usb-manager-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:drivers:usb-hid"],
            "test-usb-hid-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:drivers:usb-msc"],
            "test-usb-msc-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:storage:devfs"],
            "test-devfs-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:storage:procfs"],
            "test-procfs-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:storage:wav"],
            "test-wav-host")
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:storage:bmp"],
            "test-bmp-host")

    def test_strict_coverage_option_is_parseable(self):
        arguments = runner.parser().parse_args([
            "full", "--strict-coverage"])
        self.assertTrue(arguments.strict_coverage)

    def test_unmapped_automated_host_case_is_reported(self):
        catalog = {"cases": [{"id": "host:unknown", "status": "AUTOMATED",
                               "executor": "host"}]}
        with patch("tools.test_catalog.validate_catalog", return_value=[]):
            errors = runner.validate_catalog_for_regression(catalog)
        self.assertIn("executor_host_ausente:host:unknown", errors)

    def test_unapproved_qemu_fixture_is_reported(self):
        catalog = {"cases": [{
            "id": "qemu:test",
            "status": "AUTOMATED",
            "executor": "qemu",
            "parameters": {"fixture": "arbitrary"},
        }]}
        with patch("tools.test_catalog.validate_catalog", return_value=[]):
            errors = runner.validate_catalog_for_regression(catalog)
        self.assertIn("fixture_qemu_ausente:qemu:test:arbitrary", errors)

    def test_exit_code_two_is_fail_without_explicit_blocked_output(self):
        self.assertEqual(runner.result_status(2), "FAIL")
        self.assertEqual(runner.result_status(2, blocked=True), "BLOCKED")

    def test_qemu_network_policy_isolated_by_capability(self):
        self.assertEqual(runner.qemu_network({"id": "qemu:tst5:apps"}), "none")
        self.assertEqual(runner.qemu_network({"id": "qemu:tst4:network"}),
                         "user,model=e1000,restrict=on")
        self.assertEqual(runner.qemu_network({"id": "qemu:tst6:matrix:network",
                                              "qemu_profile": "network"}),
                         "user,model=e1000,restrict=on")

    def test_rng_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:drivers:rng"],
                         "test-rng-host")

    def test_serial_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:drivers:serial"],
                         "test-serial-host")

    def test_tss_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:drivers:tss"],
                         "test-tss-host")

    def test_speaker_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:drivers:speaker"],
                         "test-speaker-host")

    def test_keyboard_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:drivers:keyboard"],
                         "test-keyboard-host")

    def test_ac97_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:drivers:ac97"],
                         "test-ac97-host")

    def test_remaining_driver_host_cases_are_mapped(self):
        expected = {
            "host:drivers:acpi": "test-acpi-host",
            "host:drivers:ata": "test-ata-host",
            "host:drivers:e1000": "test-e1000-host",
            "host:drivers:ehci": "test-ehci-host",
            "host:drivers:idt": "test-idt-host",
            "host:drivers:mouse": "test-mouse-host",
            "host:drivers:rtl8139": "test-rtl8139-host",
            "host:drivers:rtl8811cu": "test-rtl8811cu-host",
            "host:drivers:uhci": "test-uhci-host",
        }
        self.assertEqual(
            {case_id: runner.HOST_CASE_TARGETS[case_id]
             for case_id in expected}, expected)

    def test_protocol_adapter_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS[
            "host:tst2:protocol-adapter"], "test-protocol-adapter-host")

    def test_shell_commands_core_host_case_is_mapped(self):
        self.assertEqual(
            runner.HOST_CASE_TARGETS["host:shell:commands-core"],
            "test-shell-commands-core-host")

    def test_blackbox_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:tst5:blackbox"],
                         "test-blackbox-host")

    def test_test_coverage_host_case_is_mapped(self):
        self.assertEqual(runner.HOST_CASE_TARGETS["host:core:test-coverage"],
                         "test-coverage-host")

    def test_seed_is_reproducible(self):
        self.assertEqual(runner.stable_seed("qemu:tst6:stress:kernel"),
                         runner.stable_seed("qemu:tst6:stress:kernel"))
        self.assertNotEqual(runner.stable_seed("qemu:tst6:stress:kernel"),
                            runner.stable_seed("qemu:tst6:stress:apps"))

    def test_regression_manifest_requires_case_condition_and_origin(self):
        catalog = {"cases": [{"id": "qemu:tst7:test", "status": "AUTOMATED",
                               "executor": "qemu"}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps({
                "schema": runner.REGRESSION_SCHEMA,
                "entries": [{"id": "r1", "case_id": "qemu:tst7:test",
                              "condition": "PASS event", "origin": "test"}],
            }), encoding="utf-8")
            self.assertEqual(runner.validate_regression_manifest(path, catalog), [])
            path.write_text(json.dumps({
                "schema": runner.REGRESSION_SCHEMA,
                "entries": [{"id": "r1", "case_id": "missing",
                              "condition": "", "origin": ""}],
            }), encoding="utf-8")
            self.assertGreaterEqual(
                len(runner.validate_regression_manifest(path, catalog)), 3)

    def test_explicit_approval_writes_versioned_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir = root / "tst7-run"
            run_dir.mkdir()
            value = report()
            value.update({
                "run_id": "tst7-run",
                "execution_status": "PASS",
                "cases": [{"id": "qemu:tst7:test", "status": "PASS"}],
                "steps": [{"label": "build", "status": "PASS"}],
                "catalog_errors": [],
                "limitations": [],
            })
            (run_dir / "result.json").write_text(
                json.dumps(value), encoding="utf-8")
            baseline = root / "baseline.json"
            with patch.object(runner, "RESULTS_ROOT", root), \
                    patch.object(runner, "BASELINE_PATH", baseline):
                self.assertEqual(runner.approve_run("tst7-run"), 0)
            saved = json.loads(baseline.read_text(encoding="utf-8"))
            self.assertEqual(saved["schema"], runner.BASELINE_SCHEMA)
            self.assertEqual(saved["approved_run_id"], "tst7-run")

    def test_explicit_approval_rejects_failed_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir = root / "tst7-run"
            run_dir.mkdir()
            value = report()
            value.update({
                "run_id": "tst7-run",
                "execution_status": "PASS",
                "cases": [{"id": "qemu:tst7:test", "status": "PASS"}],
                "steps": [{"label": "build", "status": "PASS"}],
                "catalog_errors": [],
                "limitations": [],
                "comparison": {"status": "FAIL", "reasons": ["regressao"]},
            })
            (run_dir / "result.json").write_text(
                json.dumps(value), encoding="utf-8")
            baseline = root / "baseline.json"
            with patch.object(runner, "RESULTS_ROOT", root), \
                    patch.object(runner, "BASELINE_PATH", baseline):
                self.assertEqual(runner.approve_run("tst7-run"), 1)
            self.assertFalse(baseline.exists())


if __name__ == "__main__":
    unittest.main()
