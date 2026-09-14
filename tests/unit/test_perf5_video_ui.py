import tempfile
import unittest
from pathlib import Path

from tools import perf5_video_ui as perf5
from tools import qemu_test_runner as runner


def _metric(name: str, value: int | str) -> dict[str, str]:
    state_names = {"vesa_available", "vesa_backbuffer", "taskbar_menu_open",
                   "desktop_active", "desktop_mode", "wm_focused_id", "wm_active"}
    gauge_names = {"vesa_last_region_width", "vesa_last_region_height",
                   "vesa_backbuffer_width", "vesa_backbuffer_height",
                   "video_last_dirty_x", "video_last_dirty_y",
                   "video_last_dirty_width", "video_last_dirty_height",
                   "desktop_icon_count", "wm_visible_windows", "wm_window_count"}
    kind = "state" if name in state_names else "gauge" if name in gauge_names else "counter"
    unit = "bool" if name in {"vesa_available", "vesa_backbuffer",
                              "taskbar_menu_open", "desktop_active", "wm_active"} else \
        "enum" if name in {"desktop_mode", "wm_focused_id"} else "count"
    return {
        "metric": name, "value": str(value), "unit": unit, "kind": kind,
        "source": "fixture", "context": "test",
        "status": "unavailable" if value == "ND" else "ok",
        "resolution": "1", "overflow": "wrap_u32" if kind == "counter" else "none",
    }


def _records(profile: str = "baseline", mode: str = "simple") -> list[dict[str, object]]:
    names = set(perf5.PERF5_REQUIRED_METRICS) | set(perf5.PERF5_ERROR_METRICS)
    names.update({"vesa_full_presentations", "vesa_partial_pixels",
                  "vesa_last_copy_ticks", "video_last_dirty_x",
                  "video_last_dirty_y", "video_last_dirty_width",
                  "video_last_dirty_height", "vesa_last_region_x",
                  "vesa_last_region_y", "vesa_last_region_pixels"})
    values = {name: 0 for name in names}
    values.update({
        "vesa_available": 0 if profile == "no-vesa" else 1,
        "vesa_backbuffer": 0 if profile == "no-vesa" else 1,
        "vesa_backbuffer_width": 0 if profile == "no-vesa" else 640,
        "vesa_backbuffer_height": 0 if profile == "no-vesa" else 480,
        "vesa_presentations": 4,
        "vesa_partial_presentations": 3,
        "vesa_bytes_copied": 4096,
        "vesa_last_region_width": 12,
        "vesa_last_region_height": 16,
        "vesa_last_region_pixels": 192,
        "vesa_partial_pixels": 384,
        "vesa_last_copy_ticks": 2,
        "video_last_dirty_width": 8,
        "video_last_dirty_height": 16,
        "desktop_icon_count": 3,
        "desktop_active": 0,
        "desktop_mode": 0 if profile == "no-vesa" else 1,
        "wm_visible_windows": 0,
        "wm_window_count": 0,
        "wm_focused_id": 0,
        "wm_active": 0,
    })
    return [{
        "seq": index, "baseline": "reset", "source": "fixture",
        "metrics": {name: _metric(name, value) for name, value in values.items()},
        "status": "ok",
    } for index in range(1, 6)]


def _trace(nd: bool = False) -> list[dict[str, object]]:
    phases = [{"op": "phase", "phase": phase}
              for phase in ("boot", "baseline", "ui", "diagnostics", "cleanup", "final")]
    screenshots = [{"op": "screenshot", "label": label,
                    "status": "ND" if nd else "ok"}
                   for label in perf5.PERF5_SCREENSHOTS]
    return phases + screenshots + [{"op": "key", "key": "c", "seq": 1}]


class Perf5ValidationTests(unittest.TestCase):
    def test_guest_validation_and_regions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label in perf5.PERF5_SCREENSHOTS:
                (root / f"screenshot-{label}.ppm").write_bytes(b"P6\n")
            status, error, observations = perf5.validate_guest_records(
                _records(), _trace(), {"event": "PASS"}, [], "baseline", root)
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["presentation_cost"]["bytes_copied"], 4096)
        self.assertEqual(observations["regions"]["vesa_last"]["width"], 12)

    def test_fallback_allows_nd_screenshots_and_vesa_metrics(self):
        status, error, observations = perf5.validate_guest_records(
            _records("no-vesa"), _trace(True), {"event": "PASS"}, [],
            "no-vesa", Path("missing-artifacts"))
        self.assertEqual((status, error), ("PASS", None))
        self.assertTrue(observations["screenshots"]["expected_nd"])

    def test_classic_sequence_restores_prompt_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label in perf5.PERF5_SCREENSHOTS:
                (root / f"screenshot-{label}.ppm").write_bytes(b"P6\n")
            status, error, _ = perf5.validate_guest_records(
                _records("baseline", "classic"), _trace(),
                {"event": "PASS"}, [], "baseline", root)
        self.assertEqual((status, error), ("PASS", None))

    def test_rejects_partial_duplicate_or_missing_phase(self):
        records = _records()
        records[-1]["status"] = "partial"
        status, error, _ = perf5.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [], "baseline", Path("."))
        self.assertEqual((status, error), ("FAIL", "envelope_guest_partial"))
        invalid_trace = [entry for entry in _trace()
                         if entry.get("phase") != "final"]
        valid, error = perf5.validate_phase_trace(invalid_trace)
        self.assertFalse(valid)
        self.assertIn("fases_invalidas", error)
        status, error, _ = perf5.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, ["duplicate_key"],
            "baseline", Path("."))
        self.assertIn("protocolo_guest", error)

    def test_wraparound_and_host_nd_report(self):
        current = {"metrics": {"x": _metric("x", 2)}}
        previous = {"metrics": {"x": _metric("x", 0xFFFFFFFF)}}
        self.assertEqual(perf5._delta(current, previous, "x"), 3)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label in perf5.PERF5_SCREENSHOTS:
                (root / f"screenshot-{label}.ppm").write_bytes(b"P6\n")
            report = perf5.build_run_report(
                root / "missing.img", "baseline", "simple", 1, root,
                _records(), _trace(), [], {"event": "PASS"}, [], 1.0, None)
        self.assertEqual(report["schema"], perf5.PERF5_SCHEMA)
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")

    def test_catalog_lane_and_runner_operations(self):
        case = {
            "id": perf5.PERF5_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": perf5.PERF5_CASE, "qemu_profile": "baseline",
            "timeout_seconds": 240, "heartbeat_timeout_seconds": 20,
            "isolation": "snapshot", "parameters": {
                "iterations": 3, "idle_seconds": 3,
                "host_sample_interval_seconds": 0.25,
                "network": "user,model=e1000,restrict=on"},
            "preconditions": "x", "action": "x", "expected": "x",
            "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "surface_ids": [],
            "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "phase", "phase": "boot"},
                {"op": "screenshot", "label": "boot"}],
                "post_action": {"type": "none", "steps": []}},
        }
        selected = perf5.case_for_lane(case, "baseline", "classic")
        self.assertEqual(selected["interaction"]["steps"][0]["text"],
                         "guimode classic")
        steps = perf5._normalize_classic_ui_steps([
            {"op": "key", "key": "meta_l"},
            {"op": "key", "key": "meta_l"},
            {"op": "key", "key": "meta_l"},
            {"op": "key", "key": "meta_l"},
            {"op": "key", "key": "meta_l"},
            {"op": "keys", "keys": ["alt", "tab"]},
            {"op": "keys", "keys": ["alt", "f9"]},
            {"op": "keys", "keys": ["alt", "f10"]},
            {"op": "keys", "keys": ["alt", "f4"]},
            {"op": "text", "text": "kmetrics machine"},
            {"op": "text", "text": "kmetrics machine"},
            {"op": "text", "text": "kmetrics machine"},
        ])
        alt_sequences = [step.get("keys") for step in steps
                         if step.get("op") == "keys"]
        self.assertLess(alt_sequences.index(["alt", "tab"]),
                        alt_sequences.index(["alt", "f9"]))
        self.assertEqual(alt_sequences.count(["alt", "f4"]), 3)
        runner.validate_input_step({"op": "phase", "phase": "ui"}, "fixture")
        runner.validate_input_step({"op": "screenshot", "label": "final"}, "fixture")
        with self.assertRaises(runner.RunnerError):
            runner.validate_input_step({"op": "screenshot", "label": "bad label"}, "fixture")

    def test_input_trace_has_monotonic_sequence(self):
        with tempfile.TemporaryDirectory() as directory:
            session = runner.QemuSession.__new__(runner.QemuSession)
            session.artifact_dir = Path(directory)
            session.input_trace = []
            session._record_input({"op": "phase", "phase": "boot"})
            session._record_input({"op": "key", "key": "c"})
            self.assertEqual([entry["seq"] for entry in session.input_trace], [1, 2])


if __name__ == "__main__":
    unittest.main()
