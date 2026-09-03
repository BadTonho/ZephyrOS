import unittest

from tools import core_host_runner


class CoreHostRunnerTests(unittest.TestCase):
    def test_compiler_command_uses_strict_flags_and_instrumentation(self):
        command = core_host_runner.compiler_command("cc", core_host_runner.DEFAULT_BINARY)
        self.assertIn("-std=c11", command)
        self.assertIn("-Wall", command)
        self.assertIn("-Wextra", command)
        self.assertIn("-Werror", command)
        self.assertIn("-finstrument-functions", command)

    def test_nm_resolution_is_local_to_compiler_toolchain(self):
        environment = {"PATH": ""}
        self.assertIsNone(core_host_runner.find_nm("missing-cc", environment))

    def test_route_case_has_isolated_sources_and_artifacts(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:network:route")
        self.assertEqual(suite, "route-host")
        self.assertEqual(result_dir, core_host_runner.ROUTE_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.ROUTE_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "route.c", sources)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "ipv4.c", sources)

    def test_ipv4_case_has_arp_and_ethernet_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:network:ipv4")
        self.assertEqual(suite, "ipv4-host")
        self.assertEqual(result_dir, core_host_runner.IPV4_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.IPV4_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "ipv4.c", sources)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "route.c", sources)

    def test_crypto_case_has_direct_crypto_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:crypto")
        self.assertEqual(suite, "crypto-host")
        self.assertEqual(result_dir, core_host_runner.CRYPTO_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.CRYPTO_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "crypto.c", sources)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "crypto_ed25519.c",
                      sources)

    def test_scheduling_case_has_queue_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:scheduling")
        self.assertEqual(suite, "scheduling-host")
        self.assertEqual(result_dir, core_host_runner.SCHEDULING_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SCHEDULING_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "wait.c", sources)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "workqueue.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "irq_deferred.c",
                      sources)

    def test_package_case_has_transaction_source(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:app-package")
        self.assertEqual(suite, "package-host")
        self.assertEqual(result_dir, core_host_runner.PACKAGE_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.PACKAGE_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "app_package.c",
                      sources)

    def test_wifi_manager_case_has_pci_and_usb_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:wifi-manager")
        self.assertEqual(suite, "wifi-manager-host")
        self.assertEqual(result_dir, core_host_runner.WIFI_MANAGER_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.WIFI_MANAGER_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "wifi_manager.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_wifi_manager_host.c", sources)

    def test_usb_manager_case_has_controller_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:usb-manager")
        self.assertEqual(suite, "usb-manager-host")
        self.assertEqual(result_dir, core_host_runner.USB_MANAGER_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.USB_MANAGER_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "usb_manager.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_usb_manager_host.c", sources)

    def test_usb_hid_case_has_driver_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:usb-hid")
        self.assertEqual(suite, "usb-hid-host")
        self.assertEqual(result_dir, core_host_runner.USB_HID_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.USB_HID_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "usb_hid.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_usb_hid_host.c", sources)

    def test_usb_msc_case_has_driver_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:usb-msc")
        self.assertEqual(suite, "usb-msc-host")
        self.assertEqual(result_dir, core_host_runner.USB_MSC_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.USB_MSC_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "usb_msc.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_usb_msc_host.c", sources)

    def test_devfs_case_has_filesystem_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:storage:devfs")
        self.assertEqual(suite, "devfs-host")
        self.assertEqual(result_dir, core_host_runner.DEVFS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.DEVFS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "fs" / "devfs.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_devfs_host.c", sources)

    def test_procfs_case_has_provider_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:storage:procfs")
        self.assertEqual(suite, "procfs-host")
        self.assertEqual(result_dir, core_host_runner.PROCFS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.PROCFS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "fs" / "procfs.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_procfs_host.c", sources)

    def test_wav_case_has_audio_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:storage:wav")
        self.assertEqual(suite, "wav-host")
        self.assertEqual(result_dir, core_host_runner.WAV_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.WAV_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "fs" / "wav.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_wav_host.c", sources)

    def test_bmp_case_has_graphics_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:storage:bmp")
        self.assertEqual(suite, "bmp-host")
        self.assertEqual(result_dir, core_host_runner.BMP_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.BMP_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "fs" / "bmp.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_bmp_host.c", sources)

    def test_rng_case_has_hardware_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:rng")
        self.assertEqual(suite, "rng-host")
        self.assertEqual(result_dir, core_host_runner.RNG_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.RNG_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "rng.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_rng_host.c", sources)

    def test_serial_case_has_uart_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:serial")
        self.assertEqual(suite, "serial-host")
        self.assertEqual(result_dir, core_host_runner.SERIAL_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SERIAL_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "serial.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_serial_host.c", sources)

    def test_tss_case_has_descriptor_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:tss")
        self.assertEqual(suite, "tss-host")
        self.assertEqual(result_dir, core_host_runner.TSS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.TSS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "tss.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_tss_host.c", sources)


if __name__ == "__main__":
    unittest.main()
