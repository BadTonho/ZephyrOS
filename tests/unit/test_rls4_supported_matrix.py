"""Testes host-only do agregador da matriz RLS4."""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace

from tools import rls4_supported_matrix as rls4
from tools.perf1_metrics import MetricsError, summarize_process_samples


class Rls4SupportedMatrixTests(unittest.TestCase):
    def _case(self, case_id: str = "case") -> dict:
        return {
            "id": case_id,
            "executor": "qemu",
            "profile": "smoke",
            "guest_case": case_id,
            "qemu_profile": "baseline",
            "required_capabilities": [],
            "timeout_seconds": 30,
            "heartbeat_timeout_seconds": 5,
            "isolation": "snapshot",
            "interaction": {
                "mode": "qmp-keyboard",
                "steps": [
                    {"op": "phase", "phase": "scenes"},
                    {"op": "text", "text": "guimode simple"},
                    {"op": "key", "key": "esc"},
                ],
                "post_action": {"type": "none", "steps": []},
            },
        }

    def test_matrix_has_57_primary_sessions_and_30_profile_runs(self) -> None:
        self.assertEqual(len(rls4._lane_plan()), 57)
        self.assertEqual(len(rls4._profile_plan()), 30)
        self.assertEqual(len(rls4.matrix_plan()), 87)
        self.assertEqual(rls4.RLS4_NA_LANES[0]["status"], "NOT_APPLICABLE")
        self.assertEqual(rls4.RLS4_NA_LANES[0]["mode"], "classic")

    def test_worker_count_is_bounded(self) -> None:
        self.assertEqual(rls4.worker_count(4, 10), 4)
        with self.assertRaises(rls4.Rls4Error):
            rls4.worker_count(0, 10)
        with self.assertRaises(rls4.Rls4Error):
            rls4.worker_count(11, 10)

    def test_storage_arguments_and_host_sampling_interval(self) -> None:
        self.assertEqual(rls4._storage_args(None), [])
        arguments = rls4._storage_args(Path("build/storage-valid.img"))
        self.assertEqual(arguments[0:2], ["-drive", arguments[1]])
        self.assertNotIn("readonly=on", arguments[1])
        readonly_arguments = rls4._storage_args(
            Path("build/storage-valid.img"), snapshot=False)
        self.assertIn("readonly=on", readonly_arguments[1])
        self.assertEqual(rls4._sample_interval([]), "ND")
        self.assertEqual(rls4._sample_interval([
            {"monotonic_seconds": 1.0},
            {"monotonic_seconds": 1.25},
        ]), 0.25)
        samples: list[dict] = []
        rls4._sample_host(None, samples, [0.0], True)
        self.assertEqual(samples, [])

    def test_network_policy_is_restricted_or_none(self) -> None:
        self.assertEqual(rls4._network_for_profile("baseline"),
                         "user,model=e1000,restrict=on")
        self.assertEqual(rls4._network_for_profile("network"),
                         "user,model=e1000,restrict=on")
        self.assertEqual(rls4._network_for_profile("no-nic"), "none")
        self.assertEqual(rls4._network_for_profile("no-vesa"), "none")

    def test_mode_case_rewrites_mode_and_classic_scene_close(self) -> None:
        selected = rls4._mode_case(self._case(), "classic")
        steps = selected["interaction"]["steps"]
        self.assertEqual(steps[1]["text"], "guimode classic")
        self.assertEqual(steps[2], {"op": "keys", "keys": ["alt", "f4"]})

    def test_no_vesa_fallback_uses_simple_close_key(self) -> None:
        case = self._case("qemu:shell5:system-update")
        case["interaction"]["steps"].append(
            {"op": "keys", "keys": ["alt", "f4"]})
        selected = rls4.prepare_case(
            {case["id"]: case}, case["id"], "no-vesa", "simple")
        self.assertEqual(
            selected["interaction"]["steps"][-1],
            {"op": "key", "key": "esc"})

    def test_prepare_case_rejects_missing_capability(self) -> None:
        case = self._case()
        case["required_capabilities"] = ["network-e1000"]
        with self.assertRaises(rls4.Rls4Error) as raised:
            rls4.prepare_case({"case": case}, "case", "no-nic", "simple")
        self.assertTrue(raised.exception.blocked)
        self.assertIn("capacidade_rls4_ausente", str(raised.exception))

    def test_prepare_case_paces_interactive_cases(self) -> None:
        selected = rls4.prepare_case(
            {"case": self._case()}, "case", "baseline", "simple")
        self.assertEqual(
            selected["parameters"]["input_key_gap_seconds"],
            rls4.RLS4_INPUT_KEY_GAP_SECONDS)

    def test_arguments_preserve_explicit_input_transport(self) -> None:
        options = SimpleNamespace(
            image="build/zephyros.img", catalog="tests/catalog.json",
            results="build/test-results", qemu="qemu-system-i386",
            cpu="max", snapshot=True, boot_timeout=60.0,
            case_timeout=300.0, suite_timeout=300.0,
            heartbeat_timeout=30.0)
        arguments = rls4._arguments(
            options, "usb-hid", [], None, None,
            rls4.qemu.QEMU_INPUT_TRANSPORT_PS2_FALLBACK)
        self.assertEqual(
            arguments.input_transport,
            rls4.qemu.QEMU_INPUT_TRANSPORT_PS2_FALLBACK)

    def test_catalog_contains_all_reused_cases(self) -> None:
        catalog = Path(__file__).resolve().parents[2] / "tests" / "catalog.json"
        cases = rls4.select_cases(catalog)
        self.assertEqual(set(cases), rls4.RLS4_REQUIRED_CASES)
        catalog_data = json.loads(catalog.read_text(encoding="utf-8"))
        catalog_case_ids = {case["id"] for case in catalog_data["cases"]}
        self.assertIn("host:rls4:supported-matrix", catalog_case_ids)
        self.assertNotIn("host:quality:rls4-supported-matrix", catalog_case_ids)

    def test_parse_machine_detects_duplicate_and_incomplete_envelopes(self) -> None:
        valid = (
            "@@ZMETRIC/1 record=begin seq=1 source=shell\n"
            "@@ZMETRIC/1 record=metric metric=a value=1 unit=count "
            "kind=counter source=test context=test status=ok resolution=1 "
            "overflow=wrap_u32\n"
            "@@ZMETRIC/1 record=end seq=1 status=ok\n"
        )
        records, error = rls4._parse_records(valid)
        self.assertIsNone(error)
        self.assertEqual(len(records), 1)
        records, error = rls4._parse_records(valid.replace(
            "metric=a value=1", "metric=a value=1 metric=a"))
        self.assertEqual(records, [])
        self.assertIn("chave_duplicada", error or "")
        records, error = rls4._parse_records(valid.replace(
            "@@ZMETRIC/1 record=end seq=1 status=ok\n", ""))
        self.assertEqual(records, [])
        self.assertIn("envelope_incompleto", error or "")

    def test_child_status_distinguishes_fail_and_blocked(self) -> None:
        self.assertEqual(
            rls4._child_status({"event": "BLOCKED"}, [], "", [], None,
                               False, False),
            ("BLOCKED", "caso_guest_blocked"),
        )
        self.assertEqual(
            rls4._child_status({"event": "PASS"}, ["duplicate"], "", [],
                               None, False, False)[0],
            "FAIL",
        )
        self.assertEqual(
            rls4._child_status({"event": "PASS"}, [], "zephyr> ", [], None,
                               True, True),
            ("FAIL", "envelope_guest_ausente"),
        )

    def test_guest_blackbox_prompt_can_be_confirmed_by_pass_event(self) -> None:
        event = {"event": "PASS", "case": rls4.RLS4_UI_CASE}
        self.assertTrue(rls4._prompt_observed(event, "", True))
        self.assertEqual(
            rls4._child_status(event, [], "", [], None, False, True),
            ("PASS", None),
        )
        self.assertEqual(
            rls4._child_status({"event": "PASS", "case": "other"},
                               [], "", [], None, False, True)[0],
            "FAIL",
        )
        update_event = {"event": "PASS", "case": "qemu:tst5:shell5-system-update"}
        self.assertTrue(rls4._prompt_observed(
            update_event, "", True, "qemu:tst5:shell5-system-update"))

    def test_failure_status_and_host_nd(self) -> None:
        self.assertEqual(rls4._failure_status([{"status": "PASS"}]),
                         ("PASS", None))
        self.assertEqual(rls4._failure_status([{"status": "FAIL", "error": "x"}]),
                         ("FAIL", "x"))
        self.assertEqual(rls4._failure_status([{"status": "BLOCKED"}]),
                         ("BLOCKED", "caso_qemu_bloqueado"))
        host = summarize_process_samples([], "ND")
        self.assertEqual(host["user_seconds"], "ND")
        self.assertEqual(host["peak_rss_bytes"], "ND")

    def test_failure_status_prioritizes_blocked_over_fail(self) -> None:
        self.assertEqual(rls4._failure_status([
            {"status": "FAIL", "error": "protocol"},
            {"status": "BLOCKED", "error": "qmp"},
        ]), ("BLOCKED", "caso_qemu_bloqueado"))

    def test_main_blocks_when_required_image_is_missing(self) -> None:
        with TemporaryDirectory() as temporary:
            result = rls4.main([
                "--image", str(Path(temporary) / "missing.img"),
                "--catalog", str(Path(temporary) / "catalog.json"),
                "--results", str(Path(temporary) / "results"),
            ])
        self.assertEqual(result, 2)

    def test_build_report_contains_schema_and_relative_artifacts(self) -> None:
        with TemporaryDirectory() as temporary:
            image = Path(temporary) / "zephyros.img"
            image.write_bytes(b"image")
            catalog = Path(temporary) / "catalog.json"
            catalog.write_text("{}", encoding="utf-8")
            jobs = [{
                "id": "lane-baseline-simple-1", "kind": "lane",
                "profile": "baseline", "mode": "simple", "iteration": 1,
                "status": "PASS",
            }]
            report = rls4.build_report(image, catalog, jobs, 1, 2404)
        self.assertEqual(report["schema"], rls4.RLS4_SCHEMA)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["total_sessions"], 1)
        self.assertEqual(report["not_applicable_lanes"][0]["status"],
                         "NOT_APPLICABLE")
        self.assertNotIn("C:\\Users", report["image"]["path"])


if __name__ == "__main__":
    unittest.main()
