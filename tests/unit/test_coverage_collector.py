import json
import tempfile
import unittest
from pathlib import Path

from tools import coverage_collector


def trace(result="0x00000000"):
    return "\n".join([
        "serial normal",
        "ZCOV_BEGIN|case=qemu:tst4:memory-slab|value=0x00000002",
        "ZCOV_DATA|case=qemu:tst4:memory-slab|addresses=0x00001000,0x00002000",
        f"ZCOV_END|case=qemu:tst4:memory-slab|value={result}",
    ])


class CoverageCollectorTests(unittest.TestCase):
    def test_fragmented_trace_is_parsed_and_deduplicated(self):
        parsed = coverage_collector.parse_trace(trace().replace(
            "0x00001000,0x00002000", "0x00001000,0x00001000,0x00002000"))
        self.assertEqual(parsed["status"], "PASS")
        self.assertEqual(parsed["cases"][0]["addresses"], [0x1000, 0x2000])

    def test_nonzero_guest_result_fails(self):
        parsed = coverage_collector.parse_trace(trace("0xFFFFFFFF"))
        self.assertEqual(parsed["status"], "FAIL")
        self.assertIn("caso_retornou:-1", parsed["cases"][0]["errors"])

    def test_malformed_or_truncated_trace_fails(self):
        parsed = coverage_collector.parse_trace(
            "ZCOV_BEGIN|case=qemu:test|value=0x00000001\n"
            "ZCOV_DATA|case=qemu:test|addresses=0x00001000\n")
        self.assertEqual(parsed["status"], "FAIL")
        self.assertIn("end_ausente", parsed["cases"][0]["errors"])

    def test_unknown_and_ambiguous_symbols_are_not_covered(self):
        catalog = {"surfaces": [
            {"id": "c:src/core/a.c:duplicate", "kind": "c_function",
             "symbol": "duplicate"},
            {"id": "c:src/core/b.c:duplicate", "kind": "c_function",
             "symbol": "duplicate"},
        ]}
        symbols = [{"address": 0x1000, "symbol": "duplicate"}]
        report = coverage_collector.collect_report(trace().replace(
            "0x00001000,0x00002000", "0x00001000,0x00003000"), symbols, catalog)
        self.assertEqual(report["status"], "FAIL")
        self.assertEqual(report["covered_surface_ids"], [])
        self.assertEqual(report["cases"][0]["unknown_addresses"], [0x3000])
        self.assertEqual(len(report["cases"][0]["ambiguous_symbols"]), 1)

    def test_explicit_surface_mapping_is_accepted(self):
        symbols = [
            {"address": 0x1000, "symbol": "a",
             "surface_id": "c:src/core/a.c:a"},
            {"address": 0x2000, "symbol": "b",
             "surface_id": "c:src/core/b.c:b"},
        ]
        report = coverage_collector.collect_report(trace(), symbols)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["covered_surface_ids"], [
            "c:src/core/a.c:a", "c:src/core/b.c:b"])

    def test_source_mapping_disambiguates_duplicate_symbols(self):
        catalog = {"surfaces": [
            {"id": "c:src/core/log.c:log_print", "kind": "c_function",
             "source": "src/core/log.c", "symbol": "log_print"},
            {"id": "c:src/boot/recovery_runtime.c:log_print",
             "kind": "c_function", "source": "src/boot/recovery_runtime.c",
             "symbol": "log_print"},
        ]}
        symbols = [{"address": 0x1000, "symbol": "log_print",
                    "source": "src/boot/recovery_runtime.c"}]
        report = coverage_collector.collect_report(
            trace().replace("0x00000002", "0x00000001").replace(
                "0x00001000,0x00002000", "0x00001000"),
            symbols, catalog)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["covered_surface_ids"], [
            "c:src/boot/recovery_runtime.c:log_print"])

    def test_assembly_symbols_are_resolved_to_assembly_surfaces(self):
        catalog = {"surfaces": [
            {"id": "asm:src/drivers/isr.asm:isr14", "kind": "asm_entry",
             "source": "src/drivers/isr.asm", "symbol": "isr14"},
            {"id": "asm:src/drivers/irq.asm:irq0", "kind": "asm_entry",
             "source": "src/drivers/irq.asm", "symbol": "irq0"},
        ]}
        symbols = [
            {"address": 0x1000, "symbol": "isr14"},
            {"address": 0x2000, "symbol": "irq0"},
        ]
        assembly_trace = trace().replace(
            "qemu:tst4:memory-slab", "qemu:tst7:assembly").replace(
                "0x00000002", "0x00000002").replace(
                    "0x00001000,0x00002000", "0x00001000,0x00002000")
        report = coverage_collector.collect_report(
            assembly_trace, symbols, catalog)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["covered_surface_ids"], [
            "asm:src/drivers/irq.asm:irq0",
            "asm:src/drivers/isr.asm:isr14"])

    def test_addr2line_locations_are_parsed(self):
        locations = coverage_collector.parse_addr2line(
            "log_print\nsrc/core/log.c:42\n??\n??:0\n",
            [0x1000, 0x2000])
        self.assertEqual(locations[0]["source"], "src/core/log.c")
        self.assertIsNone(locations[1]["source"])

    def test_json_symbol_map_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "symbols.json"
            path.write_text(json.dumps({
                "schema": coverage_collector.SYMBOL_SCHEMA,
                "symbols": [{"address": 1, "symbol": "one"}],
            }), encoding="utf-8")
            self.assertEqual(coverage_collector.load_symbols(path)[0]["symbol"],
                             "one")


if __name__ == "__main__":
    unittest.main()
