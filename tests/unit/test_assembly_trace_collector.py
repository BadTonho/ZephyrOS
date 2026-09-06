import json
import tempfile
import unittest
from pathlib import Path

from tools import assembly_trace_collector


class AssemblyTraceCollectorTests(unittest.TestCase):
    def test_collects_only_symbols_present_in_trace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            trace = root / "trace.log"
            symbols = root / "symbols.json"
            output = root / "coverage.json"
            trace.write_text("IN:\n0x00007C00: fa\n0x00005000: fa\n",
                             encoding="utf-8")
            symbols.write_text(json.dumps({
                "symbols": [
                    {"address": 0x7C00, "symbol": "start",
                     "source": "src/boot/boot.asm", "surface_id": "boot"},
                    {"address": 0x5000, "symbol": "stage2_start",
                     "source": "src/boot/stage2.asm", "surface_id": "stage2"},
                    {"address": 0x7C10, "symbol": "missing",
                     "source": "src/boot/boot.asm", "surface_id": "missing"},
                ]
            }), encoding="utf-8")
            result = assembly_trace_collector.collect_trace(argparse_namespace(
                trace=trace, symbols=symbols, case_id="qemu:test",
                output=output))
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(result["covered_surface_ids"], ["boot", "stage2"])
            self.assertFalse(result["complete"])
            self.assertEqual(len(result["missing_symbols"]), 1)


def argparse_namespace(**values):
    return type("Arguments", (), values)()


if __name__ == "__main__":
    unittest.main()
