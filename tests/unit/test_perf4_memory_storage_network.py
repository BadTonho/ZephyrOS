import tempfile
import unittest
from pathlib import Path

from tools import perf1_metrics
from tools import perf4_memory_storage_network as perf4
from tools import qemu_test_runner as runner


def _metric(name: str, value: int | str) -> dict[str, str]:
    duration = name == "capture_ticks"
    bytes_value = name == "net_buffer_copied_bytes" or name.startswith(
        ("socket_bytes_", "net_socket_bytes_"))
    gauge = name in {
        "memory_heap_used_bytes", "memory_heap_free_bytes",
        "memory_heap_fragmentation_percent", "memory_detailed_total_pages",
        "memory_detailed_fragmentation_percent", "memory_zone_5_pages",
        "slab_active_objects", "vfs_descriptors_open", "vfs_mounts_active",
        "vfs_pipes_active", "block_queue_depth", "block_in_flight",
        "block_peak_depth", "cache_entries", "cache_dirty_bytes",
        "cache_writeback_entries", "net_buffer_active_buffers",
        "net_buffer_peak_buffers", "sk_buff_active_buffers",
        "socket_active_count", "net_socket_active_count", "route_entry_count",
        "network_interfaces",
    } or name.startswith("memory_zone_")
    kind = "duration" if duration else "bytes" if bytes_value else \
        "gauge" if gauge else "counter"
    unit = "tick" if duration else "byte" if bytes_value or "bytes" in name \
        else "percent" if "percent" in name else "page" if "pages" in name \
        else "count"
    return {
        "metric": name,
        "value": str(value),
        "unit": unit,
        "kind": kind,
        "source": "fixture",
        "context": "test",
        "status": "unavailable" if value == "ND" else "ok",
        "resolution": "1",
        "overflow": "wrap_u32" if kind in {"counter", "bytes"} else "none",
    }


def _records() -> list[dict[str, object]]:
    names = set(perf4.PERF4_REQUIRED_METRICS) | set(perf4.PERF4_ERROR_METRICS)
    names.update(perf4.PERF4_RESIDUAL_METRICS)
    names.update(f"memory_zone_{index}_pages" for index in range(6))
    values = {name: 0 for name in names}
    values.update({
        "pit_ticks": 1000,
        "capture_ticks": 2,
        "memory_heap_used_bytes": 1024,
        "memory_heap_free_bytes": 3072,
        "memory_heap_fragmentation_percent": 5,
        "memory_pmm_owned_pages": 50,
        "memory_detailed_total_pages": 100,
        "memory_detailed_fragmentation_percent": 5,
        "memory_zone_0_pages": 10,
        "memory_zone_1_pages": 20,
        "memory_zone_2_pages": 5,
        "memory_zone_3_pages": 5,
        "memory_zone_4_pages": 10,
        "memory_zone_5_pages": 50,
        "slab_active_objects": 5,
        "vfs_descriptors_open": 3,
        "vfs_mounts_active": 4,
        "vfs_pipes_active": 0,
        "block_queue_depth": 0,
        "block_in_flight": 0,
        "block_peak_depth": 1,
        "cache_entries": 2,
        "cache_dirty_bytes": 0,
        "cache_writeback_entries": 0,
        "net_buffer_active_buffers": 0,
        "net_buffer_peak_buffers": 2,
        "net_buffer_copies": 0,
        "net_buffer_copied_bytes": 0,
        "sk_buff_active_buffers": 0,
        "socket_active_count": 0,
        "net_socket_active_count": 0,
        "route_entry_count": 1,
        "network_interfaces": 1,
        "ethernet_rx_frames": 4,
    })
    records = []
    for sequence, tick in enumerate((1000, 1300, 1800), start=1):
        current = dict(values)
        current["pit_ticks"] = tick
        current["capture_ticks"] = 2 + sequence
        current["vfs_reads"] = sequence - 1
        current["block_completed"] = sequence - 1
        current["cache_hits"] = sequence - 1
        current["ethernet_rx_frames"] = 4 + sequence - 1
        current["vfs_failures"] = 3 if sequence == 3 else 0
        current["net_buffer_dropped"] = 2 if sequence == 3 else 0
        current["sk_buff_drops"] = 2 if sequence == 3 else 0
        records.append({
            "seq": sequence,
            "baseline": "reset",
            "source": "fixture",
            "metrics": {name: _metric(name, value)
                        for name, value in current.items()},
            "status": "ok",
        })
    return records


def _trace() -> list[dict[str, object]]:
    return [{"op": "phase", "phase": phase}
            for phase in ("boot", "idle", "load", "cleanup", "final")]


class Perf4ValidationTests(unittest.TestCase):
    def test_guest_validation_and_memory_aggregation(self):
        status, error, observations = perf4.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("PASS", None))
        self.assertEqual(observations["final_free_pages"], 50)
        self.assertEqual(observations["diagnostic_deltas"]["vfs_reads"], 1)

    def test_rejects_nd_partial_protocol_and_blocked(self):
        incomplete = _records()
        incomplete[-1]["status"] = "partial"
        status, error, _ = perf4.validate_guest_records(
            incomplete, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "envelope_guest_partial"))

        unavailable = _records()
        unavailable[-1]["metrics"]["memory_detailed_total_pages"] = _metric(
            "memory_detailed_total_pages", "ND")
        status, error, _ = perf4.validate_guest_records(
            unavailable, _trace(), {"event": "PASS"}, [])
        self.assertIn("metricas_guest_ND", error)

        status, error, _ = perf4.validate_guest_records(
            _records(), _trace(), {"event": "PASS"}, ["duplicate_key"])
        self.assertIn("protocolo_guest", error)

        status, error, _ = perf4.validate_guest_records(
            [], _trace(), {"event": "BLOCKED"}, [])
        self.assertEqual((status, error), ("BLOCKED", "guest_blocked"))

    def test_boot_counter_nd_is_allowed_before_reset(self):
        records = _records()
        for metric in records[0]["metrics"].values():
            if metric["kind"] in {"counter", "bytes"}:
                metric.update(value="ND", status="unavailable")
        status, error, _ = perf4.validate_guest_records(
            records, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("PASS", None))

    def test_rejects_zone_inconsistency_errors_and_residuals(self):
        inconsistent = _records()
        inconsistent[1]["metrics"]["memory_zone_0_pages"] = _metric(
            "memory_zone_0_pages", 11)
        status, error, _ = perf4.validate_guest_records(
            inconsistent, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "zonas_memoria_inconsistentes"))

        errored = _records()
        errored[-1]["metrics"]["cache_errors"] = _metric("cache_errors", 1)
        status, error, _ = perf4.validate_guest_records(
            errored, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "erro_runtime:cache_errors=1"))

        expected = _records()
        expected[-1]["metrics"]["vfs_failures"] = _metric("vfs_failures", 3)
        expected[-1]["metrics"]["net_buffer_dropped"] = _metric(
            "net_buffer_dropped", 2)
        expected[-1]["metrics"]["sk_buff_drops"] = _metric("sk_buff_drops", 2)
        status, error, _ = perf4.validate_guest_records(
            expected, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("PASS", None))

        residual = _records()
        residual[-1]["metrics"]["block_queue_depth"] = _metric(
            "block_queue_depth", 1)
        status, error, _ = perf4.validate_guest_records(
            residual, _trace(), {"event": "PASS"}, [])
        self.assertEqual((status, error), ("FAIL", "residual:block_queue_depth:0->1"))

    def test_phase_and_wraparound(self):
        valid, error = perf4.validate_phase_trace(_trace())
        self.assertEqual((valid, error), (True, None))
        valid, error = perf4.validate_phase_trace(_trace()[:-1])
        self.assertFalse(valid)
        self.assertIn("fases_invalidas", error)
        current = {"metrics": {"counter": _metric("counter", 2)}}
        previous = {"metrics": {"counter": _metric("counter", 0xFFFFFFFE)}}
        self.assertEqual(perf4._delta(current, previous, "counter"), 4)

    def test_report_host_nd_and_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            report = perf4.build_run_report(
                Path(directory) / "missing.img", "baseline", "simple", 1,
                _records(), _trace(), [], {"event": "PASS"}, [], 10.0, None)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")
        self.assertEqual(report["schema"], perf4.PERF4_SCHEMA)
        self.assertIn("memory_detailed_total_pages", report["memory"])
        self.assertIn("route_entry_count", report["network"])

    def test_lane_selection_replaces_mode(self):
        case = {
            "id": perf4.PERF4_CASE, "scenario": "x", "owner": "quality",
            "layer": "qemu", "executor": "qemu", "profile": "smoke",
            "guest_case": perf4.PERF4_CASE, "qemu_profile": "baseline",
            "required_capabilities": [], "timeout_seconds": 180,
            "heartbeat_timeout_seconds": 20, "isolation": "snapshot",
            "parameters": {"iterations": 3, "idle_seconds": 3,
                            "host_sample_interval_seconds": 0.25,
                            "network": "user,model=e1000,restrict=on"},
            "preconditions": "x", "action": "x", "expected": "x",
            "errors": "x", "effects": "x", "cleanup": "x",
            "status": "AUTOMATED", "surface_ids": [],
            "interaction": {"mode": "qmp-input", "steps": [
                {"op": "text", "text": "guimode simple"},
                {"op": "phase", "phase": "boot"},
            ], "post_action": {"type": "none", "steps": []}},
        }
        selected = perf4.case_for_lane(case, "baseline", "classic")
        self.assertEqual(selected["interaction"]["steps"][0]["text"],
                         "guimode classic")

    def test_parser_and_phase_operation_contract(self):
        runner.validate_input_step({"op": "phase", "phase": "load"}, "fixture")
        with self.assertRaises(runner.RunnerError):
            runner.validate_input_step({"op": "phase", "phase": "unknown"},
                                       "fixture")
        begin = "@@ZMETRIC/1 record=begin seq=1 baseline=ND source=fixture\n"
        end = "@@ZMETRIC/1 record=end seq=1 status=ok\n"
        with self.assertRaises(perf1_metrics.MetricsError):
            perf1_metrics.parse_machine_records(begin)
        duplicate = begin + (
            "@@ZMETRIC/1 record=metric metric=x value=1 unit=count kind=counter "
            "source=fixture context=test status=ok resolution=1 overflow=wrap_u32 "
            "metric=x\n" + end)
        with self.assertRaises(perf1_metrics.MetricsError):
            perf1_metrics.parse_machine_records(duplicate)


if __name__ == "__main__":
    unittest.main()
