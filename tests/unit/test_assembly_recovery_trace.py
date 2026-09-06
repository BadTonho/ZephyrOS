import unittest

from tools import assembly_recovery_trace


class AssemblyRecoveryTraceTests(unittest.TestCase):
    def test_fixture_requires_real_recovery_entrypoints(self):
        self.assertEqual(assembly_recovery_trace.RECOVERY_LBA, 6144)
        self.assertEqual(
            assembly_recovery_trace.RECOVERY_SYMBOLS,
            ("recovery_bios_write_sector", "recovery_boot_system_entry"),
        )
        self.assertIn("RECOVERY_LOADER_LBA=6144",
                      assembly_recovery_trace.STAGE2_DEFINES)


if __name__ == "__main__":
    unittest.main()
