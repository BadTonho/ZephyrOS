import unittest

from tools import assembly_boot_trace


class AssemblyBootTraceTests(unittest.TestCase):
    def test_scenarios_cover_pending_boot_domains(self):
        symbols = {
            symbol
            for scenario in assembly_boot_trace.SCENARIOS
            for symbol in scenario["symbols"]
        }
        self.assertIn("disk_error", symbols)
        self.assertIn("a20_enable_kbc", symbols)
        self.assertIn("read_kernel_chs", symbols)
        self.assertIn("read_kernel_lba", symbols)
        self.assertIn("load_overflow", symbols)
        self.assertIn("start", symbols)
        self.assertEqual(len(assembly_boot_trace.SCENARIOS), 8)

    def test_fixture_ids_are_unique_and_bounded(self):
        identifiers = [scenario["id"] for scenario in assembly_boot_trace.SCENARIOS]
        self.assertEqual(len(identifiers), len(set(identifiers)))
        for scenario in assembly_boot_trace.SCENARIOS:
            self.assertIn(scenario["disk"], {"floppy", "ide"})
            self.assertLessEqual(len(scenario["symbols"]), 8)


if __name__ == "__main__":
    unittest.main()
