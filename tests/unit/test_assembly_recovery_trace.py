import unittest
from pathlib import Path

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

    def test_stage2_normalizes_bios_keyboard_offsets(self):
        stage2 = (Path(__file__).resolve().parents[2] / "src" / "boot" /
                  "stage2.asm").read_text(encoding="utf-8")

        self.assertIn("BIOS_BDA_OFFSET equ 0x0400", stage2)
        self.assertIn("mov si, [BIOS_KEYBOARD_HEAD]\n"
                      "    add si, BIOS_BDA_OFFSET\n"
                      "    mov di, [BIOS_KEYBOARD_TAIL]\n"
                      "    add di, BIOS_BDA_OFFSET", stage2)
        self.assertIn("mov si, [BIOS_KEYBOARD_HEAD]\n"
                      "    add si, BIOS_BDA_OFFSET\n"
                      "    mov ax, [si]", stage2)
        self.assertIn(".key_store_head:\n"
                      "    sub si, BIOS_BDA_OFFSET\n"
                      "    mov [BIOS_KEYBOARD_HEAD], si", stage2)


if __name__ == "__main__":
    unittest.main()
