import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.perf1_metrics import (
    MetricsError,
    aggregate_records,
    build_report,
    build_matrix_report,
    parse_machine_file,
    parse_machine_records,
    sample_process,
    summarize_process_samples,
    write_report,
)
from tools.perf1_baseline import _case_for_mode


def envelope(value: int = 10, sequence: int = 1) -> str:
    return (
        f"@@ZMETRIC/1 record=begin seq={sequence} baseline=boot source=guest\n"
        f"@@ZMETRIC/1 record=metric metric=pit_ticks value={value} unit=tick "
        "kind=counter source=pit context=kernel status=ok resolution=1 "
        "overflow=wrap_u32\n"
        "@@ZMETRIC/1 record=metric metric=rdtsc_cycles value=ND unit=cycle "
        "kind=counter source=rdtsc context=kernel status=unavailable "
        "resolution=1 overflow=wrap_u32\n"
        f"@@ZMETRIC/1 record=end seq={sequence} status=ok\n"
    )


class ParserTests(unittest.TestCase):
    def test_valid_machine_envelope_and_nd(self):
        records = parse_machine_records("boot text\n" + envelope())
        self.assertEqual(records[0]["seq"], 1)
        self.assertEqual(records[0]["metrics"]["pit_ticks"]["value"], "10")
        self.assertEqual(records[0]["metrics"]["rdtsc_cycles"]["value"], "ND")

    def test_incomplete_envelope_is_rejected(self):
        with self.assertRaises(MetricsError):
            parse_machine_records(envelope().rsplit("@@ZMETRIC/1 record=end", 1)[0])

    def test_partial_end_is_a_complete_protocol(self):
        partial = envelope().replace(
            "record=end seq=1 status=ok", "record=end seq=1 status=partial")
        records = parse_machine_records(partial)
        self.assertEqual(records[0]["status"], "partial")

    def test_duplicate_keys_and_metrics_are_rejected(self):
        duplicate_key = envelope().replace("seq=1 baseline=boot", "seq=1 seq=2 baseline=boot")
        lines = envelope().splitlines()
        duplicate_metric = "\n".join(lines[:2] + [lines[1]] + lines[2:]) + "\n"
        for candidate in (duplicate_key, duplicate_metric):
            with self.assertRaises(MetricsError):
                parse_machine_records(candidate)

    def test_overflow_is_rejected(self):
        with self.assertRaises(MetricsError):
            parse_machine_records(envelope(0x100000000))

    def test_metric_status_and_metadata_are_strict(self):
        invalid_nd = envelope().replace(
            "value=ND unit=cycle kind=counter",
            "value=3 unit=cycle kind=counter",
        )
        invalid_kind = envelope().replace("kind=counter source=pit", "kind=bad source=pit")
        invalid_resolution = envelope().replace("resolution=1 overflow=wrap_u32", "resolution=0 overflow=wrap_u32", 1)
        for candidate in (invalid_nd, invalid_kind, invalid_resolution):
            with self.assertRaises(MetricsError):
                parse_machine_records(candidate)

    def test_unknown_lines_are_ignored_but_missing_envelope_fails(self):
        with self.assertRaises(MetricsError):
            parse_machine_records("serial only\n")


class AggregationTests(unittest.TestCase):
    def test_aggregation_and_nd_count(self):
        records = parse_machine_records(envelope(20) + envelope(4, sequence=2))
        aggregate = aggregate_records(records)
        metric = aggregate["metrics"]["pit_ticks"]
        self.assertEqual(metric["min"], 4)
        self.assertEqual(metric["max"], 20)
        self.assertEqual(metric["median"], 4)
        self.assertEqual(aggregate["metrics"]["rdtsc_cycles"]["unavailable"], 2)

    def test_host_process_unavailable_is_explicit(self):
        sample = sample_process(0)
        self.assertEqual(sample["rss_bytes"], "ND")
        summary = summarize_process_samples([])
        self.assertEqual(summary["sample_count"], 0)
        self.assertEqual(summary["user_seconds"], "ND")

    def test_report_has_versioned_guest_host_sections(self):
        records = parse_machine_records(envelope())
        report = build_report(
            Path("missing-image.bin"), "baseline", "simple", 1, records,
            summarize_process_samples([]), wall_time_seconds="ND",
        )
        self.assertEqual(report["schema"], "zephyros-perf1-baseline-v1")
        self.assertEqual(report["image"]["sha256"], "ND")
        self.assertIn("samples", report["guest"])
        self.assertEqual(report["host"]["qemu_process"]["rss_bytes"], "ND")

    def test_file_parser_writer_and_matrix_schema(self):
        with TemporaryDirectory() as directory:
            serial = Path(directory) / "serial.log"
            report_path = Path(directory) / "report.json"
            serial.write_text(envelope(), encoding="utf-8")
            records = parse_machine_file(serial)
            report = build_report(
                Path("missing-image.bin"), "baseline", "classic", 2,
                records, summarize_process_samples([]), wall_time_seconds=1.0,
            )
            write_report(report_path, report)
            self.assertTrue(report_path.is_file())
            matrix = build_matrix_report(
                Path("missing-image.bin"), "baseline", [report])
            self.assertEqual(matrix["modes"], ["simple", "classic"])
            self.assertEqual(matrix["iterations_per_mode"], 3)

    def test_aggregation_rejects_inconsistent_metadata(self):
        second = envelope(4, sequence=2).replace(
            "source=pit context=kernel", "source=other context=kernel")
        with self.assertRaises(MetricsError):
            aggregate_records(parse_machine_records(envelope() + second))


class RunnerTests(unittest.TestCase):
    def test_mode_selection_keeps_self_contained_interaction(self):
        case = {
            "interaction": {
                "steps": [
                    {"op": "key", "key": "meta_l"},
                    {"op": "text", "text": "guimode simple"},
                ]
            }
        }
        selected = _case_for_mode(case, "classic")
        self.assertEqual(selected["interaction"]["steps"][1]["text"],
                         "guimode classic")
        self.assertEqual(case["interaction"]["steps"][1]["text"],
                         "guimode simple")


if __name__ == "__main__":
    unittest.main()
