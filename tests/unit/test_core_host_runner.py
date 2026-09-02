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


if __name__ == "__main__":
    unittest.main()
