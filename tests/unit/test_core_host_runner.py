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

    def test_socket_runtime_case_has_socket_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:network:socket-runtime")
        self.assertEqual(suite, "socket-runtime-host")
        self.assertEqual(result_dir, core_host_runner.SOCKET_RUNTIME_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SOCKET_RUNTIME_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "socket.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_socket_host.c", sources)

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

    def test_sysfs_case_has_inventory_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:storage:sysfs")
        self.assertEqual(suite, "sysfs-host")
        self.assertEqual(result_dir, core_host_runner.SYSFS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SYSFS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "fs" / "sysfs.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_sysfs_host.c", sources)

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

    def test_mediaplayer_case_has_runtime_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:shell:mediaplayer")
        self.assertEqual(suite, "mediaplayer-host")
        self.assertEqual(result_dir, core_host_runner.MEDIAPLAYER_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.MEDIAPLAYER_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "shell" /
                      "mediaplayer.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_mediaplayer_host.c", sources)

    def test_shell_job_case_has_executor_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:shell:job")
        self.assertEqual(suite, "shell-job-host")
        self.assertEqual(result_dir, core_host_runner.SHELL_JOB_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SHELL_JOB_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "shell" /
                      "shell_job.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_shell_job_host.c", sources)

    def test_shell_pipeline_case_has_executor_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:shell:pipeline")
        self.assertEqual(suite, "shell-pipeline-host")
        self.assertEqual(result_dir, core_host_runner.SHELL_PIPELINE_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SHELL_PIPELINE_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "shell" /
                      "shell_pipeline.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_shell_pipeline_host.c", sources)

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

    def test_speaker_case_has_port_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:speaker")
        self.assertEqual(suite, "speaker-host")
        self.assertEqual(result_dir, core_host_runner.SPEAKER_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SPEAKER_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "speaker.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_speaker_host.c", sources)

    def test_keyboard_case_has_controller_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:keyboard")
        self.assertEqual(suite, "keyboard-host")
        self.assertEqual(result_dir, core_host_runner.KEYBOARD_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.KEYBOARD_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "keyboard.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_keyboard_host.c", sources)

    def test_protocol_adapter_case_has_transport_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:tst2:protocol-adapter")
        self.assertEqual(suite, "protocol-adapter-host")
        self.assertEqual(result_dir, core_host_runner.PROTOCOL_ADAPTER_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.PROTOCOL_ADAPTER_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" /
                      "test_protocol.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_protocol_host.c", sources)

    def test_blackbox_case_has_terminal_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:tst5:blackbox")
        self.assertEqual(suite, "blackbox-host")
        self.assertEqual(result_dir, core_host_runner.BLACKBOX_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.BLACKBOX_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" /
                      "kernel_tests_blackbox.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_kernel_tests_blackbox_host.c", sources)

    def test_test_coverage_case_has_serial_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:test-coverage")
        self.assertEqual(suite, "test-coverage-host")
        self.assertEqual(result_dir, core_host_runner.TEST_COVERAGE_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.TEST_COVERAGE_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" /
                      "test_coverage.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_coverage_host.c", sources)

    def test_shell_hosted_case_has_window_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:shell:hosted")
        self.assertEqual(suite, "shell-hosted-host")
        self.assertEqual(result_dir, core_host_runner.SHELL_HOSTED_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SHELL_HOSTED_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "shell" /
                      "shell_hosted.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_shell_hosted_host.c", sources)

    def test_display_case_has_gui_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:gui:display")
        self.assertEqual(suite, "display-host")
        self.assertEqual(result_dir, core_host_runner.DISPLAY_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.DISPLAY_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "gui" /
                      "display.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_display_host.c", sources)

    def test_video_case_has_driver_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:video")
        self.assertEqual(suite, "video-host")
        self.assertEqual(result_dir, core_host_runner.VIDEO_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.VIDEO_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "video.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_video_host.c", sources)

    def test_usb_transport_case_has_dispatch_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:usb-transport")
        self.assertEqual(suite, "usb-transport-host")
        self.assertEqual(result_dir, core_host_runner.USB_TRANSPORT_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.USB_TRANSPORT_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" /
                      "usb_transport.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_usb_transport_host.c", sources)

    def test_gui_case_has_widget_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:gui:widgets")
        self.assertEqual(suite, "gui-host")
        self.assertEqual(result_dir, core_host_runner.GUI_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.GUI_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "gui" / "gui.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_gui_host.c", sources)

    def test_taskbar_case_has_ui_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:ui:taskbar")
        self.assertEqual(suite, "taskbar-host")
        self.assertEqual(result_dir, core_host_runner.TASKBAR_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.TASKBAR_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "taskbar" /
                      "taskbar.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_taskbar_host.c", sources)

    def test_updater_case_has_contract_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:ui:updater")
        self.assertEqual(suite, "updater-host")
        self.assertEqual(result_dir, core_host_runner.UPDATER_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.UPDATER_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "updater" /
                      "updater.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_updater_host.c", sources)

    def test_syscall_case_has_dispatcher_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:core:syscall")
        self.assertEqual(suite, "syscall-host")
        self.assertEqual(result_dir, core_host_runner.SYSCALL_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SYSCALL_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" / "syscall.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_syscall_host.c", sources)

    def test_process_runtime_case_has_process_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:process:runtime")
        self.assertEqual(suite, "process-host")
        self.assertEqual(result_dir, core_host_runner.PROCESS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.PROCESS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "process" /
                      "process.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_process_host.c", sources)

    def test_thread_case_has_scheduler_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:process:threads")
        self.assertEqual(suite, "thread-host")
        self.assertEqual(result_dir, core_host_runner.THREAD_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.THREAD_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "thread" / "thread.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_thread_host.c", sources)

    def test_shell_commands_vfs_case_has_pipeline_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:shell:commands-vfs")
        self.assertEqual(suite, "shell-commands-vfs-host")
        self.assertEqual(result_dir, core_host_runner.SHELL_COMMANDS_VFS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SHELL_COMMANDS_VFS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "shell" /
                      "shell_commands_vfs.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_shell_commands_vfs_host.c", sources)

    def test_recovery_runtime_case_has_freestanding_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:boot:recovery-runtime")
        self.assertEqual(suite, "recovery-runtime-host")
        self.assertEqual(result_dir, core_host_runner.RECOVERY_RUNTIME_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.RECOVERY_RUNTIME_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "boot" /
                      "recovery_runtime.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_recovery_runtime_host.c", sources)

    def test_panic_case_has_halt_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:kernel:panic")
        self.assertEqual(suite, "panic-host")
        self.assertEqual(result_dir, core_host_runner.PANIC_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.PANIC_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "kernel" / "panic.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_panic_host.c", sources)

    def test_pci_case_has_io_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:pci")
        self.assertEqual(suite, "pci-host")
        self.assertEqual(result_dir, core_host_runner.PCI_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.PCI_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "pci.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_pci_host.c", sources)

    def test_icons_case_has_bitmap_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:ui:icons")
        self.assertEqual(suite, "icons-host")
        self.assertEqual(result_dir, core_host_runner.ICONS_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.ICONS_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "icons" / "icons.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_icons_host.c", sources)

    def test_vesa_case_has_framebuffer_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:drivers:vesa")
        self.assertEqual(suite, "vesa-host")
        self.assertEqual(result_dir, core_host_runner.VESA_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.VESA_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "drivers" / "vesa.c",
                      sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_vesa_host.c", sources)

    def test_shell_core_case_has_shell_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:shell:core")
        self.assertEqual(suite, "shell-core-host")
        self.assertEqual(result_dir, core_host_runner.SHELL_CORE_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.SHELL_CORE_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "shell" /
                      "shell.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_shell_host.c", sources)

    def test_tls_client_case_has_adapter_fixture_sources(self):
        result_dir, binary, sources, suite = core_host_runner.case_configuration(
            "host:security:tls-client")
        self.assertEqual(suite, "tls-client-host")
        self.assertEqual(result_dir, core_host_runner.TLS_CLIENT_RESULT_DIR)
        self.assertEqual(binary, core_host_runner.TLS_CLIENT_BINARY)
        self.assertIn(core_host_runner.ROOT / "src" / "core" /
                      "tls_client.c", sources)
        self.assertIn(core_host_runner.ROOT / "tests" / "unit" /
                      "test_tls_client_host.c", sources)


if __name__ == "__main__":
    unittest.main()
